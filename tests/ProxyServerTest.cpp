//
// Integration tests for proxy_server.
// Architecture: MockLlmServer (httplib) <-- ProxyServerImpl <-- OpenAIChatImpl (ACurl)
//

#include <gtest/gtest.h>
#include <httplib.h>
#include <thread>
#include <mutex>
#include <queue>

#include "common.h"
#include "proxy_server/proxy_server.h"
#include "OpenAIChatImpl.h"
#include "AUI/Json/Conversion.h"
#include "util/await_synchronously.h"

using namespace std::chrono_literals;

static constexpr auto LOG_TAG = "ProxyServerTest";

// ─── MockLlmServer ────────────────────────────────────────────────────────────
// Listens on a free port. Enqueue SSE response bodies; each POST /v1/chat/completions
// pops and serves the next one. All received request bodies are recorded.

struct MockLlmService {
    httplib::Server server;
    std::thread thread;
    int port;

    std::queue<std::string> responseQueue;
    std::vector<AJson> receivedRequests;

    MockLlmService() {
        server.Post("/v1/chat/completions", [this](const httplib::Request& req, httplib::Response& res) {
            std::string body;
            {
                receivedRequests.push_back(AJson::fromString(req.body));
                EXPECT_FALSE(responseQueue.empty()) << "MockLlmServer: unexpected request (queue empty)";
                if (responseQueue.empty()) {
                    res.status = 500;
                    res.set_content("No responses enqueued", "text/plain");
                    return;
                }
                body = std::move(responseQueue.front());
                responseQueue.pop();
            }
            res.status = 200;
            res.set_content(body, "text/event-stream");
        });

        port = server.bind_to_any_port("127.0.0.1");
        thread = std::thread([this] { server.listen_after_bind(); });
        server.wait_until_ready();
    }

    ~MockLlmService() {
        server.stop();
        thread.join();
    }

    void enqueue(const std::string& sseBody) {
        responseQueue.push(sseBody);
    }

    // Helper: build a minimal SSE response with a single text chunk
    static std::string makeSseText(const std::string& content, const std::string& model = "test-model") {
        AJson chunk = AJson::Object {
            { "id", "chatcmpl-test" },
            { "object", "chat.completion.chunk" },
            { "created", int64_t(0) },
            { "model", model },
            { "choices", AJson::Array {
                AJson::Object {
                    { "index", int64_t(0) },
                    { "delta", AJson::Object {
                        { "role", "assistant" },
                        { "content", content },
                    }},
                    { "finish_reason", "stop" },
                }
            }},
        };
        return "data: " + AJson::toString(chunk).toStdString() + "\n\ndata: [DONE]\n\n";
    }

    // Helper: build a SSE response that contains a tool_call chunk followed by [DONE]
    static std::string makeSseToolCall(
        const std::string& toolName,
        const std::string& toolArgs,
        const std::string& toolCallId = "call_test_001",
        const std::string& model = "test-model")
    {
        AJson chunk = AJson::Object {
            { "id", "chatcmpl-test-tc" },
            { "object", "chat.completion.chunk" },
            { "created", int64_t(0) },
            { "model", model },
            { "choices", AJson::Array {
                AJson::Object {
                    { "index", int64_t(0) },
                    { "delta", AJson::Object {
                        { "role", "assistant" },
                        { "content", "" },
                        { "tool_calls", AJson::Array {
                            AJson::Object {
                                { "index", int64_t(0) },
                                { "id", toolCallId },
                                { "type", "function" },
                                { "function", AJson::Object {
                                    { "name", toolName },
                                    { "arguments", toolArgs },
                                }},
                            }
                        }},
                    }},
                    { "finish_reason", "tool_calls" },
                }
            }},
        };
        return "data: " + AJson::toString(chunk).toStdString() + "\n\ndata: [DONE]\n\n";
    }
};

// ─── Test fixture ─────────────────────────────────────────────────────────────

struct ProxyServerTest : ::testing::Test {
    static constexpr int PROXY_PORT = 19434; // well away from real proxy port

    std::shared_ptr<OpenAIChatImpl> client;
    // |             ^
    // V             |
    std::shared_ptr<proxy_server::IProxyServer> proxy;
    // | ^  tool   | ^
    // V |  calls  V |
    MockLlmService llm;


    IOpenAIChat::Params clientParams() {
        return IOpenAIChat::Params {
            .systemPrompt = "You are a test assistant.",
            .config = EndpointAndModel {
                .endpoint = {
                    .baseUrl = "http://127.0.0.1:{}/v1/"_format(PROXY_PORT),
                    .bearerKey = "",
                },
                .model = "test-model",
            },
        };
    }

    void startProxy(proxy_server::ToolFactory factory = [](const AVector<IOpenAIChat::Message>&) { return OpenAITools{}; }) {
        proxy = proxy_server::init(
            std::move(factory),
            {
                .upstreamEndpoint = {
                    .baseUrl = "http://127.0.0.1:{}/v1/"_format(llm.port),
                    .bearerKey = "",
                },
                .port = PROXY_PORT,
            });
        proxy->waitUntilReady();
        client = std::make_shared<OpenAIChatImpl>();
    }

    // Runs chatStreaming and blocks until completed. Returns the final accumulated response.
    IOpenAIChat::Response awaitStreaming(AVector<IOpenAIChat::Message> messages) {
        auto sr = client->chatStreaming(clientParams(), std::move(messages));
        util::await_synchronously(std::move(sr->completed));
        return *sr->response;
    }
};

// ─── Tests ────────────────────────────────────────────────────────────────────

// Basic passthrough: no injected tools, LLM returns plain text.
TEST_F(ProxyServerTest, BasicPassthrough) {
    llm.enqueue(MockLlmService::makeSseText("Hello from LLM"));
    startProxy();

    auto response = awaitStreaming({ { IOpenAIChat::Message::Role::USER, "hi" } });

    ASSERT_EQ(llm.receivedRequests.size(), 1u);
    ASSERT_FALSE(response.choices.empty());
    EXPECT_EQ(response.choices.at(0).message.content, "Hello from LLM");
    EXPECT_EQ(static_cast<AString&>(response.choices.at(0).finish_reason), "stop");
}

// Tool injection: proxy intercepts a tool_call, executes it locally, then makes a second
// request to the LLM with the tool result, and returns the final text to the client.
TEST_F(ProxyServerTest, ToolInjection) {
    // First LLM response: tool call
    llm.enqueue(MockLlmService::makeSseToolCall("test_tool", R"({"input":"ping"})"));
    // Second LLM response: final answer after tool result is injected
    llm.enqueue(MockLlmService::makeSseText("Tool said: pong. Done."));

    int toolCallCount = 0;

    startProxy([&toolCallCount](const AVector<IOpenAIChat::Message>&) {
        return OpenAITools {{
            .name = "test_tool",
            .description = "A test tool",
            .parameters = {
                .properties = { { "input", { .type = "string", .description = "input" } } },
                .required = { "input" },
            },
            .handler = [&toolCallCount](OpenAITools::Ctx ctx) -> AFuture<AString> {
                ++toolCallCount;
                EXPECT_EQ(toolCallCount, 1) << "test_tool must be called exactly once";
                co_return "pong";
            },
        }};
    });

    auto response = awaitStreaming({ { IOpenAIChat::Message::Role::USER, "ping" } });

    // Proxy should have made 2 requests to the mock LLM
    ASSERT_EQ(llm.receivedRequests.size(), 2u);
    EXPECT_EQ(aui::from_json<AVector<IOpenAIChat::Message>>(llm.receivedRequests.at(1)["messages"]).last().content, "pong");

    EXPECT_EQ(toolCallCount, 1);

    // Client sees the final answer, not the raw tool_call
    ASSERT_FALSE(response.choices.empty());
    EXPECT_EQ(response.choices.at(0).message.content, "Tool said: pong. Done.");
    EXPECT_EQ(static_cast<AString&>(response.choices.at(0).finish_reason), "stop");

    // Second request to LLM must contain the assistant (with tool_calls) and the tool result
    const auto& secondReqMessages = llm.receivedRequests.at(1)["messages"].asArray();
    bool foundToolResult = false;
    for (const auto& msg : secondReqMessages) {
        if (msg["role"].asStringOpt().valueOr("") == "tool") {
            foundToolResult = true;
            EXPECT_EQ(msg["content"].asStringOpt().valueOr(""), "pong");
        }
    }
    EXPECT_TRUE(foundToolResult) << "Second LLM request must contain a tool result message";
}

// Hidden context injection: after a session with tool calls, the proxy stores hidden messages
// (assistant tool_calls + tool results). On the next request that references the same assistant
// content, those hidden messages are re-injected transparently.
TEST_F(ProxyServerTest, HiddenContextInjection) {
    // Session 1: tool call → final answer "Final answer"
    llm.enqueue(MockLlmService::makeSseToolCall("ctx_tool", R"({"q":"x"})"));
    llm.enqueue(MockLlmService::makeSseText("Final answer"));

    int ctxToolCallCount = 0;

    startProxy([&ctxToolCallCount](const AVector<IOpenAIChat::Message>&) {
        return OpenAITools {{
            .name = "ctx_tool",
            .description = "ctx tool",
            .parameters = {
                .properties = { { "q", { .type = "string", .description = "q" } } },
                .required = { "q" },
            },
            .handler = [&ctxToolCallCount](OpenAITools::Ctx) -> AFuture<AString> {
                ++ctxToolCallCount;
                EXPECT_EQ(ctxToolCallCount, 1) << "ctx_tool must be called exactly once (only in session 1)";
                co_return "ctx_result";
            },
        }};
    });

    // First session
    awaitStreaming({ { IOpenAIChat::Message::Role::USER, "question" } });
    ASSERT_EQ(llm.receivedRequests.size(), 2u);
    EXPECT_EQ(ctxToolCallCount, 1) << "ctx_tool must have been called exactly once after session 1";

    // Session 2: client sends back the assistant message from session 1, plus a new user message.
    // The proxy should inject the hidden tool_call + tool_result before the assistant message.
    llm.enqueue(MockLlmService::makeSseText("Second answer"));

    auto response = awaitStreaming({
        { IOpenAIChat::Message::Role::USER,      "question"     },
        { IOpenAIChat::Message::Role::ASSISTANT, "Final answer" },
        { IOpenAIChat::Message::Role::USER,      "follow up"    },
    });

    ASSERT_EQ(llm.receivedRequests.size(), 3u);

    // The third request must contain the hidden assistant (tool_calls) and tool result messages
    const auto& thirdReqMessages = llm.receivedRequests.at(2)["messages"].asArray();
    bool foundHiddenToolResult = false;
    for (const auto& msg : thirdReqMessages) {
        if (msg["role"].asStringOpt().valueOr("") == "tool") {
            foundHiddenToolResult = true;
            EXPECT_EQ(msg["content"].asStringOpt().valueOr(""), "ctx_result");
        }
    }
    EXPECT_TRUE(foundHiddenToolResult) << "Third LLM request must contain hidden tool result from session 1";

    ASSERT_FALSE(response.choices.empty());
    EXPECT_EQ(response.choices.at(0).message.content, "Second answer");
    EXPECT_EQ(static_cast<AString&>(response.choices.at(0).finish_reason), "stop");

    EXPECT_EQ(ctxToolCallCount, 1) << "ctx_tool must not be called again in session 2";
}

// New chat after injected tool session: starting a fresh conversation (no messages from the previous
// session) must NOT inject any hidden tool messages into the request.
TEST_F(ProxyServerTest, NewChatAfterToolSession) {
    // Session 1: tool call → final answer "Final answer"
    llm.enqueue(MockLlmService::makeSseToolCall("nc_tool", R"({"q":"x"})"));
    llm.enqueue(MockLlmService::makeSseText("Final answer"));

    int ncToolCallCount = 0;

    startProxy([&ncToolCallCount](const AVector<IOpenAIChat::Message>&) {
        return OpenAITools {{
            .name = "nc_tool",
            .description = "nc tool",
            .parameters = {
                .properties = { { "q", { .type = "string", .description = "q" } } },
                .required = { "q" },
            },
            .handler = [&ncToolCallCount](OpenAITools::Ctx) -> AFuture<AString> {
                ++ncToolCallCount;
                EXPECT_EQ(ncToolCallCount, 1) << "nc_tool must be called exactly once (only in session 1)";
                co_return "nc_result";
            },
        }};
    });

    // Complete the first session so hidden context is stored in the proxy.
    awaitStreaming({ { IOpenAIChat::Message::Role::USER, "question" } });
    ASSERT_EQ(llm.receivedRequests.size(), 2u);
    EXPECT_EQ(ncToolCallCount, 1) << "nc_tool must have been called exactly once in session 1";

    // New chat: client starts completely fresh — only a single user message, no assistant history.
    llm.enqueue(MockLlmService::makeSseText("Clean answer"));

    auto response = awaitStreaming({
        { IOpenAIChat::Message::Role::USER, "brand new question" },
    });

    ASSERT_EQ(llm.receivedRequests.size(), 3u);

    // The new-chat request must contain exactly the messages the client sent —
    // no hidden tool_call / tool result from the previous session.
    const auto& newChatMessages = llm.receivedRequests.at(2)["messages"].asArray();
    for (const auto& msg : newChatMessages) {
        const auto role = msg["role"].asStringOpt().valueOr("");
        EXPECT_NE(role, "tool") << "New chat request must not contain tool result from a previous session";
        if (role == "assistant") {
            // Any assistant message must not carry tool_calls from the old session
            EXPECT_FALSE(msg.contains("tool_calls") && !msg["tool_calls"].asArray().empty())
                << "New chat request must not contain hidden tool_calls from a previous session";
        }
    }

    ASSERT_FALSE(response.choices.empty());
    EXPECT_EQ(response.choices.at(0).message.content, "Clean answer");
    EXPECT_EQ(static_cast<AString&>(response.choices.at(0).finish_reason), "stop");

    EXPECT_EQ(ncToolCallCount, 1) << "nc_tool must not be called in the new chat session";
}

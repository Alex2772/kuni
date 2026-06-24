//
// Unit tests for proxy_server::ContextBridge.
//
// Architecture:
//   collectRequestToLLM() --> ContextBridge --> MockLlmEndpoint (httplib)
//                                           --> MockDiary
//
// The tests exercise:
//  1. Session tracking via collectRequestToLLM / collectAndSaveSessionsNotNewerThan
//  2. Session identity (salt): same prefix = same session, disjoint = separate sessions
//  3. LLM summarization request and diary persistence on session flush
//  4. processChatHistoryMessage inserts flushed sessions for PAPIK, passes through for others
//

#include <gtest/gtest.h>
#include <httplib.h>
#include <thread>

#include "proxy_server/context_bridge.h"
#include "config.h"

#include <AUI/Thread/AEventLoop.h>
#include <AUI/Thread/AAsyncHolder.h>
#include <AUI/IO/APath.h>
#include <AUI/Json/AJson.h>
#include <AUI/Json/Conversion.h>

using namespace std::chrono_literals;

// ─── Helpers ──────────────────────────────────────────────────────────────────

// Build a minimal OpenAI-style request JSON with the given user messages.
static AJson makeRequest(const AVector<AString>& userMessages) {
    AJson::Array messages;
    for (const auto& text : userMessages) {
        messages << AJson::Object{
            {"role", "user"},
            {"content", text},
        };
    }
    return AJson::Object{
        {"model", "test-model"},
        {"messages", std::move(messages)},
        {"stream", true},
    };
}

// Build a minimal OpenAI non-streaming chat response SSE body (single choice).
static std::string makeChatResponseJson(const std::string& content) {
    AJson resp = AJson::Object{
        {"id", "chatcmpl-test"},
        {"object", "chat.completion"},
        {"created", int64_t(0)},
        {"model", "test-model"},
        {"choices", AJson::Array{
            AJson::Object{
                {"index", int64_t(0)},
                {"message", AJson::Object{
                    {"role", "assistant"},
                    {"content", content},
                }},
                {"finish_reason", "stop"},
            },
        }},
        {"usage", AJson::Object{
            {"prompt_tokens", int64_t(10)},
            {"completion_tokens", int64_t(5)},
            {"total_tokens", int64_t(15)},
        }},
    };
    return AJson::toString(resp).toStdString();
}

// ─── MockLlmEndpoint ──────────────────────────────────────────────────────────
// Simple httplib server that serves canned responses.

struct MockLlmEndpoint {
    httplib::Server server;
    std::thread thread;
    int port{};

    std::queue<std::string> responseQueue;
    std::vector<AJson> receivedRequests;

    MockLlmEndpoint() {
        server.Post("/v1/chat/completions", [this](const httplib::Request& req, httplib::Response& res) {
            try {
                receivedRequests.push_back(AJson::fromString(req.body));
                EXPECT_FALSE(responseQueue.empty()) << "MockLlmEndpoint: unexpected request (queue empty)";
                if (responseQueue.empty()) {
                    res.status = 500;
                    res.set_content("no responses enqueued", "text/plain");
                    return;
                }
                auto body = std::move(responseQueue.front());
                responseQueue.pop();
                res.status = 200;
                res.set_content(body, "application/json");
            } catch (const AException& e) {
                ALogger::err("MockLlmEndpoint") << "POST /v1/chat/completions: failed to process request" << e;
            }
        });

        port = server.bind_to_any_port("127.0.0.1");
        thread = std::thread([this] { server.listen_after_bind(); });
        server.wait_until_ready();
    }

    ~MockLlmEndpoint() {
        server.stop();
        thread.join();
    }

    void enqueue(const std::string& body) { responseQueue.push(body); }
};

class MockOpenAI: public IOpenAIChat {
public:
    ~MockOpenAI() override = default;
    _<StreamingResponse> chatStreaming(Params params, IOpenAIChat::Session messages) override {
        throw AException("unimplemented");
    }
    AFuture<std::valarray<double>> embedding(Params params, AString input) override {
        co_return std::valarray{0.0, 1.0, 2.0};
    }
};

// ─── MockDiary ────────────────────────────────────────────────────────────────

class MockDiary : public Diary {
public:
    // Use a temp directory for filesystem diary operations; openAI() is not
    // called by ContextBridge directly (summarization goes via makeHttpRequest),
    // so we only need the diary's save/reload API.
    explicit MockDiary(const APath& dir) : Diary(Init{.diaryDir = dir, .openAI = _new<MockOpenAI>()}) {}

    // Track save calls for assertions.
    AVector<AString> savedEntries;

    void save(const EntryEx& entry) override {
        savedEntries << entry.freeformBody;
        Diary::save(entry);
    }
};

// ─── Fixture ──────────────────────────────────────────────────────────────────

struct ContextBridgeTest : ::testing::Test {
    APath testDir;
    MockLlmEndpoint llm;
    _<MockDiary> diary;
    _<proxy_server::ContextBridge> bridge;
    AEventLoop loop;
    IEventLoop::Handle loopHandle{&loop};

    void SetUp() override {
        testDir = APath("test_data_cb") / AString::number(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count());
        testDir.removeFileRecursive();

        diary = _new<MockDiary>(testDir);
        bridge = _new<proxy_server::ContextBridge>(proxy_server::ContextBridge::Config{
            .endpoint = {
                .baseUrl = "http://127.0.0.1:{}/v1/"_format(llm.port),
                .bearerKey = "",
            },
            .diary = diary,
            .timeout = 0s,   // immediately expired — all sessions are flushable
        });
        bridge->setSlotsCallsOnlyOnMyThread(false);
    }

    void TearDown() override {
        testDir.removeFileRecursive();
    }

    // Drain the event loop until all pending async tasks are done.
    void drainLoop(AAsyncHolder& async) {
        while (!async.empty()) {
            loop.iteration();
        }
    }

    // Convenience: run collectAndSaveSessionsNotNewerThan(now) and wait.
    AVector<AString> flushAll() {
        AAsyncHolder async;
        AVector<AString> result;
        async << bridge->collectAndSaveSessionsNotNewerThan(std::chrono::system_clock::now())
                     .onSuccess([&](AVector<AString> v) { result = std::move(v); });
        drainLoop(async);
        return result;
    }
};

// ─── Tests ────────────────────────────────────────────────────────────────────

// 1. No sessions collected initially.
TEST_F(ContextBridgeTest, EmptyFlush) {
    auto result = flushAll();
    EXPECT_TRUE(result.empty());
    EXPECT_TRUE(llm.receivedRequests.empty());
}

// 2. Single session is flushed: LLM is called once, diary entry is saved.
TEST_F(ContextBridgeTest, SingleSessionFlushed) {
    // Summarization LLM response
    llm.enqueue(makeChatResponseJson("EEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEntry A---EEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEntry B"));

    bridge->collectRequestToLLM(makeRequest({"hello", "world"}));
    auto result = flushAll();

    // LLM must have received exactly one summarization request
    ASSERT_EQ(llm.receivedRequests.size(), 1u);
    // The request must have stream=false (non-streaming mode for summarization)
    EXPECT_FALSE(llm.receivedRequests.at(0)["stream"].asBoolOpt().valueOr(true));
    // Diary entries must be saved
    EXPECT_GE(diary->savedEntries.size(), 1u);
}

// 3. Updating an existing session (same salt prefix) does NOT create a duplicate.
TEST_F(ContextBridgeTest, SessionUpdateNoDuplicate) {
    // First request: user says "hi"
    bridge->collectRequestToLLM(makeRequest({"hi"}));
    // Second request: same conversation, extended
    bridge->collectRequestToLLM(makeRequest({"hi", "how are you"}));

    // Only one session should be pending → one LLM call on flush
    llm.enqueue(makeChatResponseJson("Loooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooong summary"));
    auto result = flushAll();

    EXPECT_EQ(llm.receivedRequests.size(), 1u) << "Extended session must not create a second pending session";
}

// 4. Two unrelated sessions (different salts) flush independently.
TEST_F(ContextBridgeTest, TwoIndependentSessionsFlushed) {
    llm.enqueue(makeChatResponseJson("Loooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooong summary1"));
    llm.enqueue(makeChatResponseJson("Loooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooong summary2"));

    bridge->collectRequestToLLM(makeRequest({"session A - message 1"}));
    bridge->collectRequestToLLM(makeRequest({"session B - different message entirely"}));

    auto result = flushAll();

    EXPECT_EQ(llm.receivedRequests.size(), 2u) << "Two distinct sessions must produce two summarization requests";
}

// 5. Sessions NOT older than the time-point are NOT flushed.
TEST_F(ContextBridgeTest, SessionNotFlushedBeforeTimeout) {
    // Use a bridge with a 1-hour timeout (sessions won't expire yet)
    auto longBridge = _new<proxy_server::ContextBridge>(proxy_server::ContextBridge::Config{
        .endpoint = {
            .baseUrl = "http://127.0.0.1:{}/v1/"_format(llm.port),
            .bearerKey = "",
        },
        .diary = diary,
        .timeout = 1h,
    });
    longBridge->setSlotsCallsOnlyOnMyThread(false);
    longBridge->collectRequestToLLM(makeRequest({"this session is fresh"}));

    // Flush only sessions older than `now - 1h`. The session was just created,
    // so it should NOT be flushed.
    AAsyncHolder async;
    AVector<AString> result;
    auto cutoff = std::chrono::system_clock::now() - 2h;
    async << longBridge->collectAndSaveSessionsNotNewerThan(cutoff)
                 .onSuccess([&](AVector<AString> v) { result = std::move(v); });
    drainLoop(async);

    EXPECT_TRUE(result.empty()) << "Fresh session must not be flushed before its timeout";
    EXPECT_TRUE(llm.receivedRequests.empty());
}

// 6. processChatHistoryMessage: non-papik chat passes through without flushing.
TEST_F(ContextBridgeTest, ProcessMessageNonPapikPassThrough) {
    bridge->collectRequestToLLM(makeRequest({"some request"}));

    td::td_api::chat chat;
    chat.id_ = 9999999;   // not PAPIK_CHAT_ID
    td::td_api::message msg;
    msg.date_ = static_cast<int32_t>(
        std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()));

    AAsyncHolder async;
    AString result;
    async << bridge->processChatHistoryMessage(chat, msg, "original text")
                 .onSuccess([&](AString v) { result = std::move(v); });
    drainLoop(async);

    // No LLM call, text unchanged
    EXPECT_TRUE(llm.receivedRequests.empty());
    EXPECT_EQ(result, "original text");
}

// 7. processChatHistoryMessage: PAPIK chat flushes sessions and prepends work items.
TEST_F(ContextBridgeTest, ProcessMessagePapikFlushesAndPrepends) {
    llm.enqueue(makeChatResponseJson("Work item looooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooong summary"));

    bridge->collectRequestToLLM(makeRequest({"some request about work"}));

    td::td_api::chat chat;
    chat.id_ = config::PAPIK_CHAT_ID;
    td::td_api::message msg;
    // set msg.date_ to "now" so collectAndSaveSessionsNotNewerThan(from_time_t(now)) flushes
    msg.date_ = static_cast<int32_t>(
        std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()) + 1);

    AAsyncHolder async;
    AString result;
    async << bridge->processChatHistoryMessage(chat, msg, "original text")
                 .onSuccess([&](AString v) { result = std::move(v); });
    drainLoop(async);

    // LLM was called for summarization
    EXPECT_GE(llm.receivedRequests.size(), 1u);
    // Result must start with work item XML tag (prepended before original text)
    EXPECT_TRUE(result.startsWith("<work_item>")) << "Expected work_item tag prepended, got: " << result;
    EXPECT_TRUE(result.contains("original text")) << "Original text must still be present";
}

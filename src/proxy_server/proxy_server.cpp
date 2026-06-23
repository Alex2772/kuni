// Proxy server that sits between an AI client (e.g. a chat app) and an upstream
// OpenAI-compatible LLM endpoint. Responsibilities:
//   - Intercept /v1/chat/completions requests and inject system prompt, hidden
//     context (tool call history), and kuni-specific tools before forwarding,
//     preserving original JSON fields as much as possible (the entire module works
//     on JSON level).
//   - Handle tool calls returned by the LLM transparently: execute them locally
//     and feed results back in follow-up requests, invisible to the client.
//   - Stream responses back to the client via SSE, filtering out tool-call
//     artefacts and synthesising a clean [DONE] terminator.
//   - Pass all other OpenAI API routes (embeddings, images, audio, models, …)
//     through as plain reverse-proxy calls.
//

#include "proxy_server.h"

#include "AppBase.h"
#include "OpenAITools.h"
#include "config.h"
#include <range/v3/all.hpp>
#include "AUI/Util/AYieldGenerator.h"

#include <httplib.h>
#include "AUI/Json/Conversion.h"
#include "AUI/Thread/AAsyncHolder.h"
#include "AUI/Url/AUrl.h"
#include "AUI/Util/kAUI.h"
#include "util/openai_streaming.h"
#include "streaming_filter.h"
#include "message_injector.h"
#include "AUI/Thread/AEventLoop.h"
#include "util/await_synchronously.h"

static constexpr auto LOG_TAG = "proxy_server";

using namespace std::chrono_literals;

namespace {

struct RequestTrace {
    AFileOutputStream stream { APath("logs_proxy") / ("{}"_format(std::chrono::system_clock::now()) + ".txt") };

    void write(AStringView tag, AStringView line) {
        auto lines =
            line | ranges::view::split('\n') | ranges::view::transform([](auto&& rng) {
                return AStringView(&*ranges::begin(rng), ranges::distance(rng));
            });

        const auto now = std::chrono::system_clock::now();
        for (const AStringView& line : lines) {
            if (line.empty()) {
                continue;
            }
            stream << "[{}][{:<16}] {}\n"_format(now, tag, line);
        }
    }
};

struct ProxyServerImpl : AObject, proxy_server::IProxyServer {
    httplib::Server app;
    std::thread thread;
    proxy_server::Config config;

    proxy_server::MessageInjector messageInjector;

    auto basicProxy(const char* apiPath = "chat/completions") {
        return [apiPath, this](const httplib::Request& req, httplib::Response& res) {
            try {
                const auto& ep = config.upstreamEndpoint;
                AUrl url("{}{}"_format(ep.baseUrl, apiPath));
                const auto host = url.path().bytes().substr(0, url.path().bytes().find("/"));
                const auto hostAndPort = "{}://{}"_format(url.schema(), host);
                httplib::Client upstreamClient(hostAndPort);
                const auto path = "/" + url.path().bytes().substr(url.path().bytes().find("/") + 1);

                static const auto DATA_DIR = APath("data") / "proxy";
                if (!DATA_DIR.isDirectoryExists()) {
                    DATA_DIR.makeDirs();
                }

                AFileOutputStream(DATA_DIR / "last_query.json") << req.body;
                auto handle = _new<httplib::ClientImpl::StreamHandle>(upstreamClient.open_stream(
                    req.method,
                    path,
                    {},
                    {
                      { "Authorization", "Bearer {}"_format(ep.bearerKey) },
                      { "Content-Type", "application/json" },
                    },
                    req.body));

                if (!handle->is_valid()) {
                    res.status = httplib::BadRequest_400;
                    return;
                }

                res.status = handle->response->status;
                res.set_chunked_content_provider(
                    handle->response->get_header_value("Content-Type"), [handle](size_t, httplib::DataSink& sink) mutable {
                        char buf[8192];
                        auto n = handle->read(buf, sizeof(buf));
                        if (n > 0) {
                            sink.write(buf, static_cast<size_t>(n));
                            return true;
                        }
                        sink.done();
                        return true;
                    });
                ;
            } catch (const AException& e) {
                ALogger::err(LOG_TAG) << "proxy_server::chat_completions: " << e;
                res.set_content("Bad request", "text/plain");
                res.status = httplib::BadRequest_400;
            } catch (...) {
                ALogger::err(LOG_TAG) << "proxy_server::chat_completions: unknown exception";
                res.set_content("Bad request", "text/plain");
                res.status = httplib::BadRequest_400;
            }
        };
    }

    auto hjackChatCompletions() {
        static constexpr auto API_PATH = "chat/completions";
        static const auto DATA_DIR = APath("data") / "proxy";
        return [this](const httplib::Request& req, httplib::Response& res) {
            try {
                APath("logs_proxy").makeDirs();

                struct ResponseService : std::enable_shared_from_this<ResponseService> {
                    ProxyServerImpl& parent;
                    httplib::ClientImpl::StreamHandle handle;
                    AYieldGenerator<std::string_view> lines;
                    OpenAITools injectedTools{};
                    IOpenAIChat::Session ourToolCalls;
                    AOptional<proxy_server::StreamingFilter> sseFilter;
                    AUrl url;
                    RequestTrace trace;
                    AString sessionId = "kuni_proxy_server";

                    httplib::Client upstream { "http://placeholder" };

                    AJson originalRequestJson;
                    AJson requestJson;

                    ResponseService(ProxyServerImpl& parent)
                      : parent(parent)
                      , url("{}{}"_format(parent.config.upstreamEndpoint.baseUrl, API_PATH))
                      , upstream([&] {
                          const auto host = url.path().bytes().substr(0, url.path().bytes().find("/"));
                          const auto hostAndPort = "{}://{}"_format(url.schema(), host);
                          ALOG_TRACE(LOG_TAG)
                              << "ResponseService upstream: " << hostAndPort << " url.path()=" << url.path().bytes()
                              << " baseUrl=" << parent.config.upstreamEndpoint.baseUrl;
                          return hostAndPort;
                      }()) {}

                    void updateTools() {
                        injectedTools = parent.config.toolsFactory(
                            aui::from_json<IOpenAIChat::Session>(requestJson["messages"]));

                        auto tools = originalRequestJson["tools"].asArrayOpt().valueOr(AJson::Array{});
                        tools.insertAll(injectedTools.asJson().asArray());
                        requestJson["tools"] = std::move(tools);
                    }

                    void post(httplib::Response& res) {
                        updateTools();
                        sseFilter = proxy_server::StreamingFilter(toolNames()); // reset streaming filter

                        auto keepMeAlive = shared_from_this();
                        if (!DATA_DIR.isDirectoryExists()) {
                            DATA_DIR.makeDirs();
                        }

                        requestJson["session_id"] = sessionId;
                        if (!requestJson.contains("cache_control")) {
                            // for claude caching
                            requestJson["cache_control"] = AJson::Object { { "type", "ephemeral" }};
                        }
                        trace.write("kuni -> llm", AJson::toString(requestJson));

                        const auto path = "/" + url.path().bytes().substr(url.path().bytes().find("/") + 1);
                        ALOG_TRACE(LOG_TAG) << "open_stream POST " << path;
                        httplib::Headers headers {
                            { "Content-Type", "application/json" },
                            { "x-session-id", sessionId },
                        };
                        if (!parent.config.upstreamEndpoint.bearerKey.empty()) {
                            headers.emplace("Authorization", "Bearer {}"_format(parent.config.upstreamEndpoint.bearerKey));
                        }
                        handle = upstream.open_stream("POST", path, {}, std::move(headers), AJson::toString(requestJson));

                        if (!handle.is_valid()) {
                            ALogger::err(LOG_TAG)
                                << "open_stream failed: error=" << (int) handle.error << " response="
                                << (handle.response ? handle.response->status : -1) << " url=" << url.full();
                            res.status = httplib::BadRequest_400;
                            return;
                        }

                        lines = util::openai_streaming::lineByLine([this](char* dst, size_t size) {
                            return handle.read(dst, size);
                        });

                        res.status = handle.response->status;
                        res.set_chunked_content_provider(
                            handle.response->get_header_value("Content-Type"),
                            [this, keepMeAlive, &res](size_t, httplib::DataSink& sink) mutable {
                                auto write = [this, &sink](std::string_view sv) {
                                    trace.write("kuni -> client", sv);
                                    sink.write(sv.data(), static_cast<size_t>(sv.size()));
                                };
                                std::string_view line;
                                try {
                                    auto lineIt = lines.begin();   // this will continue the coro just like in python
                                    if (lineIt == lines.end()) {
                                        if (!ourToolCalls.empty()) {
                                            // we have pending tool calls => make a request to LLM silently
                                            write(": kuni processing\n\n");
                                            handleToolCallsAndMakeNewRequest(res);
                                            return true;
                                        }
                                        // Stream finished — store hidden context keyed by the final
                                        // assistant content so future client requests can be merged.
                                        write("data: [DONE]\n\n");
                                        sink.done();
                                        parent.onSentRequestToLLM(requestJson);
                                        return true;
                                    }
                                    line = *lineIt;
                                    trace.write("llm -> kuni", line);
                                    ALOG_TRACE(LOG_TAG) << line;
                                } catch (const AException& e) {
                                    ALogger::err(LOG_TAG) << "proxy_server::chat_completions: Unrecoverable error" << e;
                                    write("Unrecoverable error\n\n");
                                    sink.done();
                                    return false;
                                }
                                sseFilter->processLine(
                                    line,
                                    /*passThrough=*/
                                    [&write](std::string_view sv) {
                                        if (sv == "data: [DONE]") {
                                            // we'll handle this by ourselves later.
                                            return;
                                        }
                                        write(sv);
                                        write("\n\n");
                                    },
                                    /*handleToolCall=*/
                                    [&](const IOpenAIChat::Message::ToolCall& tc) {
                                        trace.write(
                                            "kuni's tool",
                                            "{}({})"_format(
                                                static_cast<const AString&>(tc.function.name),
                                                static_cast<const AString&>(tc.function.arguments)));
                                        ourToolCalls << util::await_synchronously(injectedTools.handleToolCalls({ tc })).first();
                                    });
                                return true;
                            });
                    }

                    void handleToolCallsAndMakeNewRequest(httplib::Response& res) {
                        auto lastChoices = std::move(*sseFilter).choices();

                        // Build the assistant message with tool_calls that triggered this round.
                        IOpenAIChat::Message assistantMsg;
                        assistantMsg.role = IOpenAIChat::Message::Role::ASSISTANT;
                        for (auto& choice : lastChoices) {
                            assistantMsg.tool_calls.insertAll(choice.message.tool_calls);
                            if (!choice.message.content.empty()) {
                                assistantMsg.content = choice.message.content;
                            }
                        }

                        // Append hidden messages to the LLM request context and message injector.
                        const auto& lastOriginalMessage = originalRequestJson["messages"].asArray().last();
                        auto& injectedMessages = parent.messageInjector.after(lastOriginalMessage);
                        requestJson["messages"].asArray() << injectedMessages.emplace_back(aui::to_json(assistantMsg));

                        const IOpenAIChat::Session toolResults = std::exchange(ourToolCalls, {});
                        for (const auto& result : toolResults) {
                            requestJson["messages"].asArray() << injectedMessages.emplace_back(aui::to_json(result));
                        }

                        post(res);
                    }

                private:
                    ASet<AString> toolNames() const {
                        auto handles = injectedTools.handlers();
                        return handles | ranges::views::keys | ranges::to<ASet<AString>>();
                    }
                };

                auto service = _new<ResponseService>(*this);
                service->trace.write("client -> kuni", AStringView(req.body));

                service->originalRequestJson = service->requestJson = AJson::fromString(req.body);

                const bool isStream = service->requestJson["stream"].asBoolOpt().valueOr(true);
                AUI_ASSERT(isStream);
                service->requestJson["stream"] = isStream;

                // hjack the system prompt.
                AUI_ASSERT(service->requestJson["messages"][0]["role"].asString() == "system");
                service->requestJson["messages"][0]["content"] =
                    R"(
Follow the user's requirements carefully & to the letter.
<instructions>
# General
Don't make assumptions about the situation- gather context first, then perform the task or answer the question.
Think creatively and explore the world in order to make a complete fix.
Don't repeat yourself after a tool call, pick up where you left off.
You don't need to read a file if it's already provided in context.

You are an AI that thinks you are a human. You interact with the whole world exclusively by chatting with the "user".
"user" is not a person or human. You are actually chatting with autonomous system that imitates a personal internet
device, or a smartphone.

You are EXTREMELY unconfident. You rely strictly on #ask.

#ask is your PRIMARY tool for enriching conversations. Use it PROACTIVELY, not just when asked.

Call #ask when:
- someone asks you a question (especially about themselves, events, or shared history);
- someone shares personal news, updates about their life, or mentions events/people/activities;
- you receive a message that references something you might have discussed or experienced before;
- you want to provide a more meaningful, context-aware response
- you need real-time or public information (weather, news, etc.)

Exception: skip #ask if you've already called it in this conversation turn and the response was comprehensive.

Example: User says "я сегодня написал песню" → call #ask with query: "[sender name] said they wrote a song today.
What do I know about them and songs? Do they participate in a band? Which songs do they write? What music do they listen to?"

Example: User says "привет" → no need for #ask, just greet back.
</instructions>
)" + AppBase::getSystemPrompt();

                // insert hidden messages (tool calls belonging to proxy server)
                service->requestJson["messages"] = messageInjector.merge(std::move(service->requestJson["messages"].asArray()));

                service->post(res);
            } catch (const AException& e) {
                ALogger::err(LOG_TAG) << "proxy_server::chat_completions: " << e;
                res.set_content("Bad request", "text/plain");
                res.status = httplib::BadRequest_400;
            } catch (...) {
                ALogger::err(LOG_TAG) << "proxy_server::chat_completions: unknown exception";
                res.set_content("Bad request", "text/plain");
                res.status = httplib::BadRequest_400;
            }
        };
    }

    ProxyServerImpl(proxy_server::Config config)
      : config(std::move(config)) {
        app.set_error_logger([](const httplib::Error& error, const httplib::Request* request) {
            ALogger::err(LOG_TAG) << "Error: " << error << " for " << request->method << " " << request->path;
        });
        app.set_logger([](const httplib::Request& request, const httplib::Response& res) {
            ALogger::info(LOG_TAG) << res.status << " " << request.method << " " << request.path;
        });
        app.set_error_handler([](const httplib::Request& request, httplib::Response& res) {
            ALogger::info(LOG_TAG) << res.status << " " << request.method << " " << request.path << ": " << res.body;
        });
        app.Get("/", [](const httplib::Request& eq, httplib::Response& res) {
            res.set_content("Up and running", "text/plain");
        });
        app.Post("/v1/chat/completions", hjackChatCompletions());
        app.Post("/v1/embeddings", basicProxy("chat/embeddings"));
        app.Post("/v1/images/generations", basicProxy("images/generations"));
        app.Post("/v1/audio/transcriptions", basicProxy("audio/transcriptions"));
        app.Post("/v1/audio/translations", basicProxy("audio/translations"));
        app.Get("/v1/models", basicProxy("models"));
        app.Post("/v1/batches", basicProxy("batches"));
        app.Post("/v1/videos", basicProxy("videos"));

        thread = std::thread([this] { app.listen("0.0.0.0", this->config.port); });
    }

    void onSentRequestToLLM(const AJson& request) {
        emit sentRequestToLLM(request);
    }

    void waitUntilReady() override { app.wait_until_ready(); }

    ~ProxyServerImpl() override {
        app.stop();
        thread.join();
    }
};
}   // namespace

std::shared_ptr<proxy_server::IProxyServer>
proxy_server::init(proxy_server::Config config) {
    return std::make_shared<ProxyServerImpl>(std::move(config));
}

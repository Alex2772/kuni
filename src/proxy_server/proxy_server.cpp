//
// Created by alex2772 on 6/7/26.
//

#include "proxy_server.h"

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
        auto lines = line
            | ranges::view::split('\n')
            | ranges::view::transform([](auto&& rng) {
                return AStringView(&*ranges::begin(rng), ranges::distance(rng));
              });

        for (const AStringView& line : lines) {
            if (line.empty()) {
                continue;
            }
            stream << "[{:<16}] {}\n"_format(tag, line);
        }
    }
};

struct ProxyServerImpl : proxy_server::IProxyServer {
    httplib::Server app;
    std::thread thread;
    proxy_server::ToolFactory toolFactory;
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
                    httplib::ClientImpl::StreamHandle handle;
                    AYieldGenerator<std::string_view> lines;
                    AVector<IOpenAIChat::Message> temporaryContext;
                    OpenAITools injectedTools;
                    AVector<AFuture<IOpenAIChat::Message>> ourToolCalls;
                    proxy_server::StreamingFilter sseFilter { toolNames() };
                    proxy_server::MessageInjector& messageInjector;
                    Endpoint upstreamEndpoint;
                    AUrl url;
                    RequestTrace trace;

                    httplib::Client upstream { "http://placeholder" };

                    AJson requestJson;

                    ResponseService(proxy_server::ToolFactory toolsFactory, proxy_server::MessageInjector& injector, Endpoint ep)
                      : injectedTools(toolsFactory(temporaryContext))
                      , messageInjector(injector)
                      , upstreamEndpoint(std::move(ep))
                      , url("{}{}"_format(upstreamEndpoint.baseUrl, API_PATH))
                      , upstream([&] {
                            const auto host = url.path().bytes().substr(0, url.path().bytes().find("/"));
                            const auto hostAndPort = "{}://{}"_format(url.schema(), host);
                            ALOG_TRACE(LOG_TAG) << "ResponseService upstream: " << hostAndPort
                                                << " url.path()=" << url.path().bytes()
                                                << " baseUrl=" << upstreamEndpoint.baseUrl;
                            return hostAndPort;
                        }()) {}

                    void post(httplib::Response& res) {
                        auto keepMeAlive = shared_from_this();
                        if (!DATA_DIR.isDirectoryExists()) {
                            DATA_DIR.makeDirs();
                        }

                        trace.write("kuni -> llm", AJson::toString(requestJson));

                        const auto path = "/" + url.path().bytes().substr(url.path().bytes().find("/") + 1);
                        ALOG_TRACE(LOG_TAG) << "open_stream POST " << path;
                        httplib::Headers headers {
                          { "Content-Type", "application/json" },
                        };
                        if (!upstreamEndpoint.bearerKey.empty()) {
                            headers.emplace("Authorization", "Bearer {}"_format(upstreamEndpoint.bearerKey));
                        }
                        handle = upstream.open_stream(
                            "POST",
                            path,
                            {},
                            {
                            },
                            AJson::toString(requestJson));

                        if (!handle.is_valid()) {
                            ALogger::err(LOG_TAG) << "open_stream failed: error=" << (int)handle.error
                                                    << " response=" << (handle.response ? handle.response->status : -1)
                                                    << " url=" << url.full();
                            res.status = httplib::BadRequest_400;
                            return;
                        }

                        lines = util::openai_streaming::lineByLine([this](char* dst, size_t size) {
                            auto readBytes = handle.read(dst, size);
                            trace.write("llm -> kuni", AStringView(dst, readBytes));
                            return readBytes;
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
                                        {
                                            const auto& finalChoices = sseFilter.choices();
                                            if (!finalChoices.empty()) {
                                                storeHiddenContext(finalChoices.first().message.content);
                                            }
                                        }
                                        write("data: [DONE]\n\n");
                                        sink.done();
                                        return true;
                                    }
                                    line = *lineIt;
                                    ALOG_TRACE(LOG_TAG) << line;
                                } catch (const AException& e) {
                                    ALogger::err(LOG_TAG) << "proxy_server::chat_completions: Unrecoverable error" << e;
                                    write("Unrecoverable error\n\n");
                                    sink.done();
                                    return false;
                                }
                                sseFilter.processLine(
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
                                        ourToolCalls << injectedTools.handleToolCalls({ tc }).map(
                                            [](const AVector<IOpenAIChat::Message>& results) { return results.first(); });
                                    });
                                return true;
                            });
                    }

                    void handleToolCallsAndMakeNewRequest(httplib::Response& res) {
                        auto lastChoices = std::move(sseFilter).choices();

                        // Build the assistant message with tool_calls that triggered this round.
                        IOpenAIChat::Message assistantMsg;
                        assistantMsg.role = IOpenAIChat::Message::Role::ASSISTANT;
                        for (auto& choice : lastChoices) {
                            assistantMsg.tool_calls.insertAll(choice.message.tool_calls);
                            if (!choice.message.content.empty()) {
                                assistantMsg.content = choice.message.content;
                            }
                        }

                        // Await all tool call results.
                        AVector<IOpenAIChat::Message> toolResults;
                        for (auto& future : ourToolCalls) {
                            toolResults << util::await_synchronously(std::move(future));
                        }
                        ourToolCalls.clear();

                        // Append hidden messages to the LLM request context.
                        requestJson["messages"].asArray() << aui::to_json(assistantMsg);
                        for (auto& result : toolResults) {
                            requestJson["messages"].asArray() << aui::to_json(result);
                        }

                        sseFilter = proxy_server::StreamingFilter(toolNames());
                        post(res);
                    }

                    // Called after the final LLM response is fully streamed to the client.
                    // Stores the hidden context so future client requests can be merged.
                    void storeHiddenContext(const AString& visibleAssistantContent) {
                        // Hidden messages = everything after the original client messages.
                        const auto& allMessages = requestJson["messages"].asArray();
                        const std::size_t originalCount = temporaryContext.size();
                        if (allMessages.size() <= originalCount) {
                            return;
                        }
                        proxy_server::MessageInjector::Messages hidden;
                        for (std::size_t i = originalCount; i < allMessages.size(); ++i) {
                            IOpenAIChat::Message msg;
                            aui::from_json(allMessages[i], msg);
                            hidden << std::move(msg);
                        }
                        messageInjector.store(visibleAssistantContent, std::move(hidden));
                    }

                private:
                    ASet<AString> toolNames() const {
                        auto handles = injectedTools.handlers();
                        return handles | ranges::views::keys | ranges::to<ASet<AString>>();
                    }
                };

                auto service = _new<ResponseService>(toolFactory, messageInjector, config.upstreamEndpoint);
                service->trace.write("client -> kuni", AStringView(req.body));

                service->requestJson = AJson::fromString(req.body);

                const bool isStream = service->requestJson["stream"].asBoolOpt().valueOr(true);
                AUI_ASSERT(isStream);
                service->requestJson["stream"] = isStream;

                // Deserialize client messages, merge any hidden messages stored from previous sessions.
                aui::from_json(service->requestJson["messages"], service->temporaryContext);
                auto mergedMessages = messageInjector.merge(service->temporaryContext);
                service->requestJson["messages"] = aui::to_json(mergedMessages);

                if (!service->requestJson.contains("tools")) {
                    service->requestJson["tools"] = AJson::Array {};
                }
                service->requestJson["tools"].asArray().insertAll(service->injectedTools.asJson().asArray());
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

    ProxyServerImpl(proxy_server::ToolFactory toolFactory, proxy_server::Config config)
      : toolFactory(std::move(toolFactory)), config(std::move(config)) {
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

    void waitUntilReady() override {
        app.wait_until_ready();
    }

    ~ProxyServerImpl() override {
        app.stop();
        thread.join();
    }
};
}   // namespace

std::shared_ptr<proxy_server::IProxyServer> proxy_server::init(proxy_server::ToolFactory toolFactory, proxy_server::Config config) {
    return std::make_shared<ProxyServerImpl>(std::move(toolFactory), std::move(config));
}

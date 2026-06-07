//
// Created by alex2772 on 6/7/26.
//

#include "proxy_server.h"

#include "config.h"

#include <httplib.h>
#include "AUI/Json/Conversion.h"
#include "AUI/Thread/AAsyncHolder.h"
#include "AUI/Url/AUrl.h"
#include "AUI/Util/kAUI.h"

static constexpr auto LOG_TAG = "proxy_server";

using namespace std::chrono_literals;

namespace {

auto basicProxy(const char* apiPath = "chat/completions") {
    return [apiPath](const httplib::Request& req, httplib::Response& res) {
        try {
            static const auto CONFIG = config::ENDPOINT_MAIN;
            AUrl url("{}{}"_format(CONFIG.endpoint.baseUrl, apiPath));
            const auto host = url.path().bytes().substr(0, url.path().bytes().find("/"));
            const auto hostAndPort = "{}://{}"_format(url.schema(), host);
            httplib::Client upstream(hostAndPort);
            const auto path = "/" + url.path().bytes().substr(url.path().bytes().find("/") + 1);

            static const auto DATA_DIR = APath("data") / "proxy";
            if (!DATA_DIR.isDirectoryExists()) {
                DATA_DIR.makeDirs();
            }

            AFileOutputStream(DATA_DIR / "last_query.json") << req.body;
            auto handle = _new<httplib::ClientImpl::StreamHandle>(upstream.open_stream(
                req.method,
                path,
                {},
                {
                  { "Authorization", "Bearer {}"_format(CONFIG.endpoint.bearerKey) },
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

auto hjackChatCompletions(const char* apiPath = "chat/completions", const char* method = "POST") {
    return [apiPath](const httplib::Request& req, httplib::Response& res) {
        try {
            static const auto CONFIG = config::ENDPOINT_MAIN;
            auto json = AJson::fromString(req.body);
            const bool isStream = json["stream"].asBoolOpt().valueOr(false);
            json["stream"] = isStream;

            AUrl url("{}{}"_format(CONFIG.endpoint.baseUrl, apiPath));
            const auto host = url.path().bytes().substr(0, url.path().bytes().find("/"));
            const auto hostAndPort = "{}://{}"_format(url.schema(), host);
            httplib::Client upstream(hostAndPort);
            const auto path = "/" + url.path().bytes().substr(url.path().bytes().find("/") + 1);

            static const auto DATA_DIR = APath("data") / "proxy";
            if (!DATA_DIR.isDirectoryExists()) {
                DATA_DIR.makeDirs();
            }

            AFileOutputStream(DATA_DIR / "last_query.json") << AJson::toString(json);
            auto handle = _new<httplib::ClientImpl::StreamHandle>(upstream.open_stream(
                "POST",
                path,
                {},
                {
                  { "Authorization", "Bearer {}"_format(CONFIG.endpoint.bearerKey) },
                  { "Content-Type", "application/json" },
                },
                AJson::toString(json)));

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

struct ProxyServerImpl : proxy_server::IProxyServer {
    httplib::Server app;
    AAsyncHolder async;
    std::thread thread;

    ProxyServerImpl() {
        app.set_error_logger([](const httplib::Error& error, const httplib::Request* request) {
            ALogger::err(LOG_TAG) << "Error: " << error << " for " << request->method << " " << request->path;
        });
        app.set_error_handler([](const httplib::Request& request, httplib::Response& res) {
            ALOG_DEBUG(LOG_TAG) << res.status << " " << request.method << " " << request.path;
            if (res.status >= 400) {
                ALogger::err(LOG_TAG) << res.status << " " << request.method << " " << request.path;
            }
        });
        app.Get("/", [](const httplib::Request& eq, httplib::Response& res) {
            res.set_content("Up and running", "text/plain");
        });
        app.Post("/v1/chat/completions", basicProxy("chat/completions"));
        app.Post("/v1/embeddings", basicProxy("chat/embeddings"));
        app.Post("/v1/images/generations", basicProxy("images/generations"));
        app.Post("/v1/audio/transcriptions", basicProxy("audio/transcriptions"));
        app.Post("/v1/audio/translations", basicProxy("audio/translations"));
        app.Get("/v1/models", basicProxy("models"));
        app.Post("/v1/batches", basicProxy("batches"));
        app.Post("/v1/videos", basicProxy("videos"));

        // http://localhost:10434
        thread = std::thread([this] { app.listen("0.0.0.0", 10434); });
    }
    ~ProxyServerImpl() override {
        app.stop();
        thread.join();
    }
};
}   // namespace

std::shared_ptr<proxy_server::IProxyServer> proxy_server::init() { return std::make_shared<ProxyServerImpl>(); }

//
// Created by alex2772 on 4/27/26.
//

#include <httplib.h>

#include "proxy_server.h"

#include "OpenAIChat.h"
#include "AUI/Json/Conversion.h"
#include "AUI/Thread/AAsyncHolder.h"
#include "AUI/Util/kAUI.h"

static constexpr auto LOG_TAG = "proxy_server";

using namespace std::chrono_literals;

namespace {
struct ProxyServerImpl : proxy_server::ProxyServer {
    httplib::Server app;
    AAsyncHolder async;
    std::thread thread;

    ProxyServerImpl() {
        app.Get("/", [](const httplib::Request& eq, httplib::Response& res) {
            res.set_content("Up and running", "text/plain");
        });

        app.Post("/chat/completions", [](const httplib::Request& req, httplib::Response& res) {
            try {
                static const auto CONFIG = config::ENDPOINT_MAIN;
                auto json = AJson::fromString(req.body);
                const bool isStream = json["stream"].asBoolOpt().valueOr(false);
                json["stream"] = isStream;
                json["model"] = CONFIG.model;

                AUrl url("{}/chat/completions"_format(CONFIG.endpoint.baseUrl));
                const auto hostAndPort = "{}://{}"_format(url.schema(), url.path().bytes().substr(0, url.path().bytes().find("/")));
                httplib::Client upstream(hostAndPort);
                const auto path = url.path().bytes().substr(url.path().bytes().find("/") + 1);
                auto handle = _new<httplib::ClientImpl::StreamHandle>(upstream.open_stream("POST", path, {}, {
                    { "Content-Type", "application/json"},
                    { "Authorization", "Bearer {}"_format(CONFIG.endpoint.bearerKey)},
                }));

                if (!handle->is_valid()) {
                    res.status = httplib::BadRequest_400;
                    return;
                }

                res.status = handle->response->status;
                res.set_chunked_content_provider(
                    handle->response->get_header_value("Content-Type"),
                    [handle](size_t, httplib::DataSink& sink) mutable {
                        char buf[8192];
                        auto n = handle->read(buf, sizeof(buf));
                        if (n > 0) {
                            sink.write(buf, static_cast<size_t>(n));
                            return true;
                        }
                        sink.done();
                        return true;
                    }
                );
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
        });

        thread = std::thread([this] { app.listen("0.0.0.0", 10434); });
    }
    ~ProxyServerImpl() override {
        app.stop();
        thread.join();
    }
    void stop() override {
        app.stop();
    }
};
}

_<proxy_server::ProxyServer> proxy_server::init() {
    return _new<ProxyServerImpl>();
}
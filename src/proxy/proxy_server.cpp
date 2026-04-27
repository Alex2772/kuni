//
// Created by alex2772 on 4/27/26.
//

#include "crow.h"

#include "proxy_server.h"

#include "OpenAIChat.h"
#include "AUI/Json/Conversion.h"
#include "AUI/Thread/AAsyncHolder.h"
#include "AUI/Util/kAUI.h"

static constexpr auto LOG_TAG = "proxy_server";

using namespace std::chrono_literals;

namespace {
struct ProxyServerImpl : proxy_server::ProxyServer {
    crow::SimpleApp app;
    std::thread thread;
    AAsyncHolder async;

    ProxyServerImpl() {
        CROW_ROUTE(app, "/")
        ([](){
            return "Up and running";
        });

        CROW_ROUTE(app, "/chat/completions")
        .methods("POST"_method)
        ([this](const crow::request& req, crow::response& response){
            try {
                auto json = AJson::fromString(req.body);
                async << [](crow::response& response) -> AFuture<> {
                    for (;;) {
                        response.write("Hello, world!\n");
                        co_await AThread::asyncSleep(1s);
                    }
                    response.end();
                }(response);
                return;

            } catch (const AException& e) {
                ALogger::err(LOG_TAG) << "proxy_server::chat_completions: " << e;
                response = crow::response(crow::BAD_REQUEST);
            } catch (...) {
                ALogger::err(LOG_TAG) << "proxy_server::chat_completions: unknown exception";
                response = crow::response(crow::BAD_REQUEST);
            }
        });

        thread = std::thread([this] {
            // deep in run, there's same govnocode: std::thread and join
            app.port(10434).run();
        });
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
#include "proxy/proxy_server.h"
#include "OpenAIChat.h"
#include "AUI/Thread/AAsyncHolder.h"
#include "AUI/Thread/AEventLoop.h"

#include <gtest/gtest.h>

TEST(Proxy, Basic) {
    AEventLoop loop;
    IEventLoop::Handle h(&loop);
    AAsyncHolder async;
    async << []() -> AFuture<> {
        auto proxy = proxy_server::init();
        OpenAIChat session{
            .systemPrompt = "You are an assistant",
            .config = {
                .endpoint = {
                    .baseUrl = "http://127.0.0.1:10434/",
                },
                .model = config::ENDPOINT_MAIN.model
            },
        };
        auto response = (co_await session.chat("Answer SHORTLY. What time is it? Do not make up information; if you don't have access to a tool, report it.")).choices.at(0).message.content;
        EXPECT_TRUE(response.contains("content") ||
                    response.contains("information") || response.contains("cannot") ||
                    response.contains("provide") || response.contains("time"))
            << response;
    }();
    async.setOnException([](const AException& e) {
        GTEST_FAIL() << e;
    });

    while (async.size() > 0) {
        loop.iteration();
    }
}


#include "config.h"
#include <gmock/gmock.h>

#include "AUI/Thread/AAsyncHolder.h"
#include "AUI/Thread/AEventLoop.h"
#include "telegram/TelegramClientImpl.h"
#include "util/post_message.h"

TEST(TelegramIntegration, PostMessage) {
    AEventLoop loop;
    IEventLoop::Handle h(&loop);
    auto telegram = _new<TelegramClientImpl>();
    AThread::processMessages();
    [](_<ITelegramClient> telegram, AEventLoop& loop) -> AFuture<> {
        try {
            co_await telegram->waitForConnection();
            co_await util::telegramPostMessage(*telegram, config::PAPIK_CHAT_ID, "Hello");
        } catch (const AException& e) {
            ALogger::err("TelegramTests") << "Unhandled exception: " << e;
            GTEST_NONFATAL_FAILURE_("Unhandled exception");
        }
        loop.stop();
        co_return;
    }(telegram, loop);

    {
    }

    {

        // telegram->sendQuery(std::move(msg), [&](td::td_api::message& result) {
        //     loop.stop();
        // });
    }
    loop.loop();
}



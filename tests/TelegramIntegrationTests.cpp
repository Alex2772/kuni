#include "config.h"
#include <gmock/gmock.h>

#include "AUI/Thread/AAsyncHolder.h"
#include "AUI/Thread/AEventLoop.h"
#include "telegram/TelegramClient.h"


TEST(TelegramIntegration, PostMessage) {
    AEventLoop loop;
    IEventLoop::Handle h(&loop);
    auto telegram = _new<TelegramClient>();
    AThread::processMessages();
    [](_<TelegramClient> telegram, AEventLoop& loop) -> AFuture<> {
        try {
            co_await telegram->waitForConnection();
            auto result = co_await telegram->sendQueryWithResult([&] {
                auto msg = td::td_api::make_object<td::td_api::sendMessage>();
                msg->chat_id_= config::PAPIK_CHAT_ID;
                msg->input_message_content_ = [&] {
                    auto content = td::td_api::make_object<td::td_api::inputMessageText>();
                    content->text_ = [&] {
                        auto t = td::td_api::make_object<td::td_api::formattedText>();
                        t->text_ = "Hello";
                        return t;
                    }();
                    return content;
                }();
                return msg;
            }());
            EXPECT_GE(result->id_, 0);
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



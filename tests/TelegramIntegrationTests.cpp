#include "config.h"
#include <gmock/gmock.h>
#include <range/v3/all.hpp>

#include "AUI/Thread/AAsyncHolder.h"
#include "AUI/Thread/AEventLoop.h"
#include "llmui/audio.h"
#include "llmui/telegram.h"
#include "telegram/TelegramClientImpl.h"
#include "util/post_message.h"

// ---------------------------------------------------------------------------
// Mock IOpenAIChat
// ---------------------------------------------------------------------------
class OpenAIMock : public IOpenAIChat {
public:
    MOCK_METHOD(_<StreamingResponse>, chatStreaming, (Params params, IOpenAIChat::Session messages), (override));
    MOCK_METHOD(AFuture<std::valarray<double>>, embedding, (Params params, AString input), (override));
};

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


TEST(TelegramIntegration, GiftsFormatting) {
    // telegram gifts have weird formatting
    AEventLoop loop;
    IEventLoop::Handle h(&loop);
    auto telegram = _new<TelegramClientImpl>();
    AThread::processMessages();
    AAsyncHolder async;
    async << [](_<ITelegramClient> telegram) -> AFuture<> {
        try {
            OpenAIMock openAI;
            static constexpr auto CHAT_ID = 5979666573;
            static constexpr auto MESSAGE_ID = 52919533568;
            co_await telegram->waitForConnection();
            auto messages = co_await telegram->sendQueryWithResult(ITelegramClient::toPtr(td::td_api::getChatHistory(CHAT_ID, MESSAGE_ID, -1, 10, true)));
            auto giftMessage = ranges::find(messages->messages_, MESSAGE_ID, [](const td::td_api::object_ptr<td::td_api::message>& m) {
                return m->id_;
            });
            EXPECT_FALSE(giftMessage == messages->messages_.end());
            auto formatting = co_await llmui::formatChatHistoryMessage(*telegram, **giftMessage, *co_await telegram->sendQueryWithResult(ITelegramClient::toPtr(td::td_api::getChat(CHAT_ID))), openAI, {});
            static constexpr auto EXPECTED_FORMATTING = R"XYI(<message message_id="52919533568" date="2026-05-21 06:38:08" sender="Итальянский Премьер (@dio5556)">
<gift cost="25 stars" text="куни куни😊😊😊" emoji="🌹" description>
This media type is not supported
</gift cost="25 stars" text="куни куни😊😊😊" emoji="🌹">[gift]
</message message_id="52919533568" date="2026-05-21 06:38:08" sender="Итальянский Премьер (@dio5556)">)XYI";
            EXPECT_EQ(formatting, EXPECTED_FORMATTING) << "Got formatting:\n" << formatting;
        } catch (const AException& e) {
            ALogger::err("TelegramTests") << "Unhandled exception: " << e;
            ADD_FAILURE() << "Unhandled exception";
        }
        co_return;
    }(telegram);

    while (!async.empty()) {
        loop.iteration();
    }
}


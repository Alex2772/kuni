//
// view_messages_around tests
//

#include "tools/view_messages_around.h"
#include "../common.h"
#include "config.h"
#include "../OpenAIMock.h"
#include "AUI/Thread/AAsyncHolder.h"
#include "AUI/Thread/AEventLoop.h"
#include "util/await_synchronously.h"

#include <gmock/gmock.h>

namespace {
// ---------------------------------------------------------------------------
// Mock ITelegramClient
// ---------------------------------------------------------------------------
class TelegramMock : public ITelegramClient {
public:
    MOCK_METHOD(AFuture<Object>, sendQuery, (td::td_api::object_ptr<td::td_api::Function> f), (override));
    MOCK_METHOD(const AFuture<>&, waitForConnection, (), (const, noexcept, override));
    MOCK_METHOD(int64_t, myId, (), (const, override));

    TelegramMock() {
        ON_CALL(*this, waitForConnection).WillByDefault([]() -> const AFuture<>& {
            static AFuture<> ready;
            ready.supplyValue();
            return ready;
        });
        ON_CALL(*this, myId).WillByDefault(testing::Return(0));
    }
};

td::td_api::object_ptr<td::td_api::message> makeTextMessage(int64_t id, int64_t chatId, const AString& text) {
    auto msg = td::td_api::make_object<td::td_api::message>();
    msg->id_ = id;
    msg->chat_id_ = chatId;
    msg->date_ = 1700000000;
    msg->sender_id_ = td::td_api::make_object<td::td_api::messageSenderUser>(0);
    msg->content_ = td::td_api::make_object<td::td_api::messageText>();
    auto& t = static_cast<td::td_api::messageText&>(*msg->content_);
    t.text_ = td::td_api::make_object<td::td_api::formattedText>();
    t.text_->text_ = text.toStdString();
    return msg;
}
}   // namespace

// ===========================================================================
// viewMessagesAround – chat not accessible from lockdown
// ===========================================================================
TEST(ViewMessagesAroundTest, LockdownBlocksInaccessibleChat) {
    auto telegram = _new<TelegramMock>();
    auto openAI = _new<OpenAIMock>();

    EXPECT_CALL(*telegram, sendQuery(testing::_)).Times(0);

    IOpenAIChat::Session temporaryContext;
    auto tool = tools::viewMessagesAround(telegram, openAI, temporaryContext);

    OpenAITools tools{};
    auto result = util::await_synchronously(tool.handler({
        .tools = tools,
        .args = AJson::Object{{"chat_id", config().papikChatId + 1}, {"message_id", 1}},
        .temporaryContext = {},
        .allToolCalls = {},
    }));

    EXPECT_TRUE(result.contains("not accessible")) << "result = " << result;
}

// ===========================================================================
// viewMessagesAround – message not found
// ===========================================================================
TEST(ViewMessagesAroundTest, MessageNotFound) {
    auto telegram = _new<TelegramMock>();
    auto openAI = _new<OpenAIMock>();

    const auto chatId = config().papikChatId;

    EXPECT_CALL(*telegram, sendQuery(testing::_))
        .WillOnce([&](td::td_api::object_ptr<td::td_api::Function> f) -> AFuture<ITelegramClient::Object> {
            EXPECT_EQ(f->get_id(), td::td_api::getMessage::ID);
            throw AException("Message not found");
        });

    IOpenAIChat::Session temporaryContext;
    auto tool = tools::viewMessagesAround(telegram, openAI, temporaryContext);

    OpenAITools tools{};
    auto result = util::await_synchronously(tool.handler({
        .tools = tools,
        .args = AJson::Object{{"chat_id", chatId}, {"message_id", 42}},
        .temporaryContext = {},
        .allToolCalls = {},
    }));

    EXPECT_TRUE(result.contains("not found")) << "result = " << result;
}

// ===========================================================================
// viewMessagesAround – returns messages before/after, target marked
// ===========================================================================
TEST(ViewMessagesAroundTest, ReturnsSurroundingMessages) {
    auto telegram = _new<TelegramMock>();
    auto openAI = _new<OpenAIMock>();

    const auto chatId = config().papikChatId;
    constexpr int64_t targetId = 100;

    EXPECT_CALL(*telegram, sendQuery(testing::_))
        .WillOnce([&](td::td_api::object_ptr<td::td_api::Function> f) -> AFuture<ITelegramClient::Object> {
            // getMessage remap
            EXPECT_EQ(f->get_id(), td::td_api::getMessage::ID);
            co_return makeTextMessage(targetId, chatId, "target");
        })
        .WillOnce([&](td::td_api::object_ptr<td::td_api::Function> f) -> AFuture<ITelegramClient::Object> {
            EXPECT_EQ(f->get_id(), td::td_api::getChatHistory::ID);
            auto* q = static_cast<td::td_api::getChatHistory*>(f.get());
            EXPECT_EQ(q->chat_id_, chatId);
            EXPECT_EQ(q->from_message_id_, targetId);

            auto result = td::td_api::make_object<td::td_api::messages>();
            // newest-first: after(1), target, before(1)
            result->messages_.push_back(makeTextMessage(targetId + 1, chatId, "after"));
            result->messages_.push_back(makeTextMessage(targetId, chatId, "target"));
            result->messages_.push_back(makeTextMessage(targetId - 1, chatId, "before"));
            co_return result;
        })
        .WillOnce([&](td::td_api::object_ptr<td::td_api::Function> f) -> AFuture<ITelegramClient::Object> {
            EXPECT_EQ(f->get_id(), td::td_api::getChat::ID);
            auto chat = td::td_api::make_object<td::td_api::chat>();
            chat->id_ = chatId;
            chat->title_ = "Chat";
            chat->type_ = td::td_api::make_object<td::td_api::chatTypePrivate>();
            co_return chat;
        });

    IOpenAIChat::Session temporaryContext;
    auto tool = tools::viewMessagesAround(telegram, openAI, temporaryContext);

    OpenAITools tools{};
    auto result = util::await_synchronously(tool.handler({
        .tools = tools,
        .args = AJson::Object{{"chat_id", chatId}, {"message_id", targetId}, {"before", 1}, {"after", 1}},
        .temporaryContext = {},
        .allToolCalls = {},
    }));

    EXPECT_TRUE(result.contains("before")) << "result = " << result;
    EXPECT_TRUE(result.contains("target")) << "result = " << result;
    EXPECT_TRUE(result.contains("after")) << "result = " << result;
    EXPECT_TRUE(result.contains("message target")) << "result = " << result;
}

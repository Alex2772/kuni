//
// Created by alex2772 on 5/9/26.
//

#include "tools/react_with_emoji.h"
#include "../common.h"
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

    // Default implementation for waitForConnection
    TelegramMock() {
        auto* p = this;
        ON_CALL(*p, waitForConnection).WillByDefault([]() -> const AFuture<>& {
            static AFuture<> ready;
            ready.supplyValue();
            return ready;
        });
    }
};
}

// ---------------------------------------------------------------------------
// Helper: create a mock chat object using AUI shared pointer
// ---------------------------------------------------------------------------
static _<td::td_api::chat> makeChat(int64_t id, const AString& title) {
    auto chat = _new<td::td_api::chat>();
    chat->id_ = id;
    chat->title_ = title.toStdString();
    chat->type_ = td::td_api::make_object<td::td_api::chatTypePrivate>();
    return chat;
}

// ===========================================================================
// reactWithEmoji – Success
// ===========================================================================
TEST(ReactWithEmojiTest, Success) {
    auto telegram = _new<TelegramMock>();
    auto chat = makeChat(12345, "Test Chat");

    // Expect sendQuery for addMessageReaction
    EXPECT_CALL(*telegram, sendQuery(testing::_))
        .WillOnce([](td::td_api::object_ptr<td::td_api::Function> f) -> AFuture<ITelegramClient::Object> {
            // Verify it's an addMessageReaction
            EXPECT_EQ(f->get_id(), td::td_api::addMessageReaction::ID);
            auto* reaction = static_cast<td::td_api::addMessageReaction*>(f.get());
            EXPECT_EQ(reaction->chat_id_, 12345);
            EXPECT_EQ(reaction->message_id_, 42);
            EXPECT_EQ(reaction->reaction_type_->get_id(), td::td_api::reactionTypeEmoji::ID);
            auto* emoji = static_cast<td::td_api::reactionTypeEmoji*>(reaction->reaction_type_.get());
            EXPECT_EQ(emoji->emoji_, "🔥");
            co_return td::td_api::make_object<td::td_api::ok>();
        });

    auto tool = tools::reactWithEmoji(std::move(telegram), std::move(chat));

    OpenAITools tools{};
    auto result = util::await_synchronously(tool.handler({
        .tools = tools,
        .args = AJson::Object{{"message_id", 42}, {"emoji", "🔥"}},
        .allToolCalls = {},
    }));

    EXPECT_TRUE(result.contains("🔥")) << "result = " << result;
    EXPECT_TRUE(result.contains("added successfully")) << "result = " << result;
}

// ===========================================================================
// reactWithEmoji – Wrong chat_id returns error
// ===========================================================================
TEST(ReactWithEmojiTest, WrongChatId) {
    auto telegram = _new<TelegramMock>();
    auto chat = makeChat(12345, "Test Chat");

    // No sendQuery should be called
    EXPECT_CALL(*telegram, sendQuery(testing::_)).Times(0);

    auto tool = tools::reactWithEmoji(std::move(telegram), std::move(chat));

    OpenAITools tools{};
    auto result = util::await_synchronously(tool.handler({
        .tools = tools,
        .args = AJson::Object{{"chat_id", 99999}, {"message_id", 42}, {"emoji", "🔥"}},
        .allToolCalls = {},
    }));

    EXPECT_TRUE(result.contains("Error")) << "result = " << result;
    EXPECT_TRUE(result.contains("can't send messages to other chats")) << "result = " << result;
    EXPECT_TRUE(result.contains("Test Chat")) << "result = " << result;
}

// ===========================================================================
// reactWithEmoji – Missing message_id throws
// ===========================================================================
TEST(ReactWithEmojiTest, MissingMessageIdThrows) {
    auto telegram = _new<TelegramMock>();
    auto chat = makeChat(12345, "Test Chat");

    EXPECT_CALL(*telegram, sendQuery(testing::_)).Times(0);

    auto tool = tools::reactWithEmoji(std::move(telegram), std::move(chat));

    OpenAITools tools{};
    EXPECT_THROW(
        util::await_synchronously(tool.handler({
            .tools = tools,
            .args = AJson::Object{{"emoji", "🔥"}},
            .allToolCalls = {},
        })),
        AException
    );
}

// ===========================================================================
// reactWithEmoji – Missing emoji throws
// ===========================================================================
TEST(ReactWithEmojiTest, MissingEmojiThrows) {
    auto telegram = _new<TelegramMock>();
    auto chat = makeChat(12345, "Test Chat");

    EXPECT_CALL(*telegram, sendQuery(testing::_)).Times(0);

    auto tool = tools::reactWithEmoji(std::move(telegram), std::move(chat));

    OpenAITools tools{};
    EXPECT_THROW(
        util::await_synchronously(tool.handler({
            .tools = tools,
            .args = AJson::Object{{"message_id", 42}},
            .allToolCalls = {},
        })),
        AException
    );
}

//
// Created by alex2772 on 5/9/26.
//

#include "tools/remove_and_ban_chat.h"
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

    TelegramMock() {
        ON_CALL(*this, waitForConnection).WillByDefault([]() -> const AFuture<>& {
            static AFuture<> ready;
            ready.supplyValue();
            return ready;
        });
        ON_CALL(*this, myId).WillByDefault(testing::Return(0));
    }
};
}

// ===========================================================================
// removeAndBanChat – PAPIK_CHAT_ID returns "Failed"
// ===========================================================================
TEST(RemoveAndBanChatTest, PapikChatIdReturnsFailed) {
    auto telegram = _new<TelegramMock>();

    // No sendQuery should be called — the guard check happens before any API call
    EXPECT_CALL(*telegram, sendQuery(testing::_)).Times(0);

    auto tool = tools::removeAndBanChat(std::move(telegram));

    OpenAITools tools{};
    auto result = util::await_synchronously(tool.handler({
        .tools = tools,
        .args = AJson::Object{{"chat_id", config::PAPIK_CHAT_ID}},
        .allToolCalls = {},
    }));

    EXPECT_EQ(result, "Failed");
}

// ===========================================================================
// removeAndBanChat – Basic group chat calls leaveChat
// ===========================================================================
TEST(RemoveAndBanChatTest, BasicGroupCallsLeaveChat) {
    auto telegram = _new<TelegramMock>();

    constexpr int64_t CHAT_ID = 99999;

    // Expect getChat → returns a basic group chat
    EXPECT_CALL(*telegram, sendQuery(testing::_))
        .WillOnce([&](td::td_api::object_ptr<td::td_api::Function> f) -> AFuture<ITelegramClient::Object> {
            EXPECT_EQ(f->get_id(), td::td_api::getChat::ID);
            auto* getChat = static_cast<td::td_api::getChat*>(f.get());
            EXPECT_EQ(getChat->chat_id_, CHAT_ID);

            auto chat = td::td_api::make_object<td::td_api::chat>();
            chat->id_ = CHAT_ID;
            chat->title_ = "Test Group";
            chat->type_ = td::td_api::make_object<td::td_api::chatTypeBasicGroup>();
            co_return chat;
        })
        .WillOnce([&](td::td_api::object_ptr<td::td_api::Function> f) -> AFuture<ITelegramClient::Object> {
            // Expect leaveChat
            EXPECT_EQ(f->get_id(), td::td_api::leaveChat::ID);
            auto* leave = static_cast<td::td_api::leaveChat*>(f.get());
            EXPECT_EQ(leave->chat_id_, CHAT_ID);
            co_return td::td_api::make_object<td::td_api::ok>();
        });

    auto tool = tools::removeAndBanChat(std::move(telegram));

    OpenAITools tools{};
    auto result = util::await_synchronously(tool.handler({
        .tools = tools,
        .args = AJson::Object{{"chat_id", CHAT_ID}},
        .allToolCalls = {},
    }));

    EXPECT_EQ(result, "Success");
}

// ===========================================================================
// removeAndBanChat – Supergroup chat calls leaveChat
// ===========================================================================
TEST(RemoveAndBanChatTest, SupergroupCallsLeaveChat) {
    auto telegram = _new<TelegramMock>();

    constexpr int64_t CHAT_ID = 88888;

    EXPECT_CALL(*telegram, sendQuery(testing::_))
        .WillOnce([&](td::td_api::object_ptr<td::td_api::Function> f) -> AFuture<ITelegramClient::Object> {
            EXPECT_EQ(f->get_id(), td::td_api::getChat::ID);
            auto* getChat = static_cast<td::td_api::getChat*>(f.get());
            EXPECT_EQ(getChat->chat_id_, CHAT_ID);

            auto chat = td::td_api::make_object<td::td_api::chat>();
            chat->id_ = CHAT_ID;
            chat->title_ = "Test Supergroup";
            chat->type_ = td::td_api::make_object<td::td_api::chatTypeSupergroup>();
            co_return chat;
        })
        .WillOnce([&](td::td_api::object_ptr<td::td_api::Function> f) -> AFuture<ITelegramClient::Object> {
            EXPECT_EQ(f->get_id(), td::td_api::leaveChat::ID);
            auto* leave = static_cast<td::td_api::leaveChat*>(f.get());
            EXPECT_EQ(leave->chat_id_, CHAT_ID);
            co_return td::td_api::make_object<td::td_api::ok>();
        });

    auto tool = tools::removeAndBanChat(std::move(telegram));

    OpenAITools tools{};
    auto result = util::await_synchronously(tool.handler({
        .tools = tools,
        .args = AJson::Object{{"chat_id", CHAT_ID}},
        .allToolCalls = {},
    }));

    EXPECT_EQ(result, "Success");
}

// ===========================================================================
// removeAndBanChat – Missing chat_id throws
// ===========================================================================
TEST(RemoveAndBanChatTest, MissingChatIdThrows) {
    auto telegram = _new<TelegramMock>();

    EXPECT_CALL(*telegram, sendQuery(testing::_)).Times(0);

    auto tool = tools::removeAndBanChat(std::move(telegram));

    OpenAITools tools{};
    EXPECT_THROW(
        util::await_synchronously(tool.handler({
            .tools = tools,
            .args = AJson::Object{},  // empty args, no "chat_id"
            .allToolCalls = {},
        })),
        AException
    );
}

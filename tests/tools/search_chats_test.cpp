//
// Created by alex2772 on 5/9/26.
//

#include "tools/search_chats.h"
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
// searchChats – Missing query throws
// ===========================================================================
TEST(SearchChatsTest, MissingQueryThrows) {
    auto telegram = _new<TelegramMock>();

    EXPECT_CALL(*telegram, sendQuery(testing::_)).Times(0);

    auto tool = tools::searchChats(std::move(telegram));

    OpenAITools tools{};
    EXPECT_THROW(
        util::await_synchronously(tool.handler({
            .tools = tools,
            .args = AJson::Object{},  // empty args, no "query"
            .allToolCalls = {},
        })),
        AException
    );
}

// ===========================================================================
// searchChats – No results found
// ===========================================================================
TEST(SearchChatsTest, NoResults) {
    auto telegram = _new<TelegramMock>();

    // Expect searchChatsOnServer → empty results
    // Expect searchPublicChat → empty result (id_ = 0)
    EXPECT_CALL(*telegram, sendQuery(testing::_))
        .WillOnce([&](td::td_api::object_ptr<td::td_api::Function> f) -> AFuture<ITelegramClient::Object> {
            // First call: searchChatsOnServer
            EXPECT_EQ(f->get_id(), td::td_api::searchChatsOnServer::ID);
            auto result = td::td_api::make_object<td::td_api::chats>();
            // empty chat_ids
            co_return result;
        })
        .WillOnce([&](td::td_api::object_ptr<td::td_api::Function> f) -> AFuture<ITelegramClient::Object> {
            // Second call: searchPublicChat
            EXPECT_EQ(f->get_id(), td::td_api::searchPublicChat::ID);
            auto result = td::td_api::make_object<td::td_api::chat>();
            result->id_ = 0;  // not found (id_ = 0)
            result->type_ = td::td_api::make_object<td::td_api::chatTypePrivate>();
            co_return result;
        });

    auto tool = tools::searchChats(std::move(telegram));

    OpenAITools tools{};
    auto result = util::await_synchronously(tool.handler({
        .tools = tools,
        .args = AJson::Object{{"query", "nonexistent_chat"}},
        .allToolCalls = {},
    }));

    EXPECT_TRUE(result.contains("No chats found")) << "result = " << result;
}

// ===========================================================================
// searchChats – With results
// ===========================================================================
TEST(SearchChatsTest, WithResults) {
    auto telegram = _new<TelegramMock>();

    // Expect:
    // 1. searchChatsOnServer → returns chat_ids with CHAT_ID
    // 2. searchPublicChat → returns a chat with a valid id
    // 3. getChat(CHAT_ID) → returns a chat object (for formatChatList)
    // 4. getChat(PUBLIC_CHAT_ID) → returns a chat object (for formatChatSingle)
    EXPECT_CALL(*telegram, sendQuery(testing::_))
        .WillOnce([&](td::td_api::object_ptr<td::td_api::Function> f) -> AFuture<ITelegramClient::Object> {
            EXPECT_EQ(f->get_id(), td::td_api::searchChatsOnServer::ID);
            auto result = td::td_api::make_object<td::td_api::chats>();
            result->chat_ids_ = { config::PAPIK_CHAT_ID };
            co_return result;
        })
        .WillOnce([&](td::td_api::object_ptr<td::td_api::Function> f) -> AFuture<ITelegramClient::Object> {
            EXPECT_EQ(f->get_id(), td::td_api::searchPublicChat::ID);
            auto result = td::td_api::make_object<td::td_api::chat>();
            result->id_ = config::PAPIK_CHAT_ID;
            result->title_ = "Public Chat";
            result->type_ = td::td_api::make_object<td::td_api::chatTypePrivate>();
            co_return result;
        })
        .WillOnce([&](td::td_api::object_ptr<td::td_api::Function> f) -> AFuture<ITelegramClient::Object> {
            // getChat for formatChatList
            EXPECT_EQ(f->get_id(), td::td_api::getChat::ID);
            auto* getChat = static_cast<td::td_api::getChat*>(f.get());
            EXPECT_EQ(getChat->chat_id_, config::PAPIK_CHAT_ID);

            auto chat = td::td_api::make_object<td::td_api::chat>();
            chat->id_ = config::PAPIK_CHAT_ID;
            chat->title_ = "Found Chat";
            chat->type_ = td::td_api::make_object<td::td_api::chatTypePrivate>();
            co_return chat;
        });

    auto tool = tools::searchChats(std::move(telegram));

    OpenAITools tools{};
    auto result = util::await_synchronously(tool.handler({
        .tools = tools,
        .args = AJson::Object{{"query", "test_chat"}},
        .allToolCalls = {},
    }));

    EXPECT_TRUE(result.contains("Found Chat")) << "result = " << result;
    EXPECT_TRUE(result.contains("existing_chats")) << "result = " << result;
    EXPECT_TRUE(result.contains("global_search")) << "result = " << result;
}

// ===========================================================================
// searchChats – Query with @ prefix is stripped
// ===========================================================================
TEST(SearchChatsTest, AtPrefixStripped) {
    auto telegram = _new<TelegramMock>();

    // The handler strips "@" prefix before searching.
    // We verify this by checking the query passed to searchChatsOnServer.
    EXPECT_CALL(*telegram, sendQuery(testing::_))
        .WillOnce([&](td::td_api::object_ptr<td::td_api::Function> f) -> AFuture<ITelegramClient::Object> {
            EXPECT_EQ(f->get_id(), td::td_api::searchChatsOnServer::ID);
            auto* search = static_cast<td::td_api::searchChatsOnServer*>(f.get());
            // The "@" should have been stripped
            EXPECT_EQ(search->query_, "username");
            auto result = td::td_api::make_object<td::td_api::chats>();
            // empty — we just care about the query stripping
            co_return result;
        })
        .WillOnce([&](td::td_api::object_ptr<td::td_api::Function> f) -> AFuture<ITelegramClient::Object> {
            EXPECT_EQ(f->get_id(), td::td_api::searchPublicChat::ID);
            auto* search = static_cast<td::td_api::searchPublicChat*>(f.get());
            EXPECT_EQ(search->username_, "username");
            auto result = td::td_api::make_object<td::td_api::chat>();
            result->id_ = 0;
            result->type_ = td::td_api::make_object<td::td_api::chatTypePrivate>();
            co_return result;
        });

    auto tool = tools::searchChats(std::move(telegram));

    OpenAITools tools{};
    auto result = util::await_synchronously(tool.handler({
        .tools = tools,
        .args = AJson::Object{{"query", "@username"}},
        .allToolCalls = {},
    }));

    EXPECT_TRUE(result.contains("No chats found")) << "result = " << result;
}

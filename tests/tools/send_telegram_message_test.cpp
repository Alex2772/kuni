//
// Created by alex2772 on 5/9/26.
//

#include "tools/send_telegram_message.h"
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

    AFuture<> ready;

    TelegramMock() {
        ready.supplyValue();
        ON_CALL(*this, waitForConnection).WillByDefault([this]() -> const AFuture<>& {
            return ready;
        });
        ON_CALL(*this, myId).WillByDefault(testing::Return(123456));
    }
};

// ---------------------------------------------------------------------------
// Mock IOpenAIChat
// ---------------------------------------------------------------------------
class OpenAIMock : public IOpenAIChat {
public:
    MOCK_METHOD(_<IOpenAIChat::StreamingResponse>, chatStreaming, (Params params, IOpenAIChat::Session messages), (override));
    MOCK_METHOD(AFuture<std::valarray<double>>, embedding, (Params params, AString input), (override));
};
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
static _<td::td_api::chat> makeChat(const AString& title) {
    auto chat = _new<td::td_api::chat>();
    chat->id_ = config().papikChatId;
    chat->title_ = title.toStdString();
    chat->type_ = td::td_api::make_object<td::td_api::chatTypePrivate>();
    return chat;
}

static td::td_api::object_ptr<td::td_api::message> makeMessage(int64_t id, int64_t senderUserId, const AString& text) {
    auto msg = td::td_api::make_object<td::td_api::message>();
    msg->id_ = id;
    msg->sender_id_ = td::td_api::make_object<td::td_api::messageSenderUser>(senderUserId);
    msg->content_ = td::td_api::make_object<td::td_api::messageText>();
    auto& msgText = static_cast<td::td_api::messageText&>(*msg->content_);
    msgText.text_ = td::td_api::make_object<td::td_api::formattedText>();
    msgText.text_->text_ = text.toStdString();
    return msg;
}

/** Convenience: creates a messages array wrapped in AUI shared_ptr as expected by sendTelegramMessage. */
static _<td::td_api::array<td::td_api::object_ptr<td::td_api::message>>> makeMessages() {
    return _new<td::td_api::array<td::td_api::object_ptr<td::td_api::message>>>();
}

/**
 * @brief Returns the correct TDLib response object based on the function ID.
 *
 * sendChatAction -> ok
 * sendMessage    -> message (dummy)
 * All others     -> ok
 */
static AFuture<ITelegramClient::Object> dispatchSendQuery(td::td_api::object_ptr<td::td_api::Function> f) {
    switch (f->get_id()) {
    case td::td_api::sendMessage::ID: {
        auto msg = td::td_api::make_object<td::td_api::message>();
        msg->content_ = [] {
            auto content = td::td_api::make_object<td::td_api::messageText>();
            content->text_ = [] {
                auto text = td::td_api::make_object<td::td_api::formattedText>();
                text->text_ = "Ololo";
                return text;
            }();
            return content;
        }();
        co_return msg;
    }
    default:
        co_return td::td_api::make_object<td::td_api::ok>();
    }
}

// ===========================================================================
// sendTelegramMessage – Success: sends a simple text message
// ===========================================================================
TEST(SendTelegramMessageTest, SuccessSimpleText) {
    auto telegram = _new<TelegramMock>();
    auto openAI = _new<OpenAIMock>();
    auto chat = makeChat("Test Chat");

    // No previous messages from Kuni — embedding won't be called for repeat check
    auto messages = _new<td::td_api::array<td::td_api::object_ptr<td::td_api::message>>>();
    messages->push_back(makeMessage(1, 99999, "Hello from user"));

    // Expect sendQuery for typing action + sendMessage.
    // Use the dispatch helper so sendMessage returns a message (not ok).
    EXPECT_CALL(*telegram, sendQuery(testing::_))
        .Times(testing::AtLeast(2))
        .WillRepeatedly([](td::td_api::object_ptr<td::td_api::Function> f) -> AFuture<ITelegramClient::Object> {
            // Verify that at least one call is chatActionTyping
            EXPECT_TRUE(f->get_id() == td::td_api::sendChatAction::ID ||
                        f->get_id() == td::td_api::sendMessage::ID);
            co_return co_await dispatchSendQuery(std::move(f));
        });

    // Embedding is called once for the target message (repeat-check logic),
    // but since there are no Kuni's previous messages, the loop over history is skipped.
    EXPECT_CALL(*openAI, embedding(testing::_, testing::_))
        .Times(1)
        .WillRepeatedly(testing::Return(AFuture(std::valarray<double>{-0.2})));

    auto tool = tools::sendTelegramMessage(
        telegram, openAI, chat, std::move(messages));

    OpenAITools tools{};
    auto result = util::await_synchronously(tool.handler({
        .tools = tools,
        .args = AJson::Object{{"text", "Hello!"}},
        .allToolCalls = {},
    }));

    EXPECT_TRUE(result.contains("sent successfully")) << "result = " << result;
    EXPECT_TRUE(result.contains("Test Chat")) << "result = " << result;
}

// ===========================================================================
// sendTelegramMessage – Wrong chat_id returns error
// ===========================================================================
TEST(SendTelegramMessageTest, WrongChatId) {
    auto telegram = _new<TelegramMock>();
    auto openAI = _new<OpenAIMock>();
    auto chat = makeChat("Test Chat");

    auto messages = _new<td::td_api::array<td::td_api::object_ptr<td::td_api::message>>>();

    EXPECT_CALL(*telegram, sendQuery(testing::_))
        .Times(testing::AtLeast(1))
        .WillRepeatedly([](td::td_api::object_ptr<td::td_api::Function> f) -> AFuture<ITelegramClient::Object> {
            // Verify that at least one call is chatActionTyping
            EXPECT_TRUE(f->get_id() == td::td_api::sendChatAction::ID);
            co_return co_await dispatchSendQuery(std::move(f));
        });
    EXPECT_CALL(*openAI, embedding(testing::_, testing::_)).Times(0);

    auto tool = tools::sendTelegramMessage(
        telegram, openAI, chat, std::move(messages));

    OpenAITools tools{};
    auto result = util::await_synchronously(tool.handler({
        .tools = tools,
        .args = AJson::Object{{"text", "Hello!"}, {"chat_id", 99999}},
        .allToolCalls = {},
    }));

    EXPECT_TRUE(result.contains("Error")) << "result = " << result;
    EXPECT_TRUE(result.contains("can't send messages to other chats")) << "result = " << result;
    EXPECT_TRUE(result.contains("Test Chat")) << "result = " << result;
}

// ===========================================================================
// sendTelegramMessage – Missing text, photo_filename, and audio_filename
// ===========================================================================
TEST(SendTelegramMessageTest, MissingAllContent) {
    auto telegram = _new<TelegramMock>();
    auto openAI = _new<OpenAIMock>();
    auto chat = makeChat("Test Chat");

    auto messages = _new<td::td_api::array<td::td_api::object_ptr<td::td_api::message>>>();

    EXPECT_CALL(*telegram, sendQuery(testing::_))
        .Times(1)
        .WillRepeatedly([](td::td_api::object_ptr<td::td_api::Function> f) -> AFuture<ITelegramClient::Object> {
            // Verify that at least one call is chatActionTyping
            EXPECT_TRUE(f->get_id() == td::td_api::sendChatAction::ID ||
                        f->get_id() == td::td_api::sendMessage::ID);
            co_return co_await dispatchSendQuery(std::move(f));
        });
    EXPECT_CALL(*openAI, embedding(testing::_, testing::_)).Times(0);

    auto tool = tools::sendTelegramMessage(
        telegram, openAI, chat, std::move(messages));

    OpenAITools tools{};
    auto result = util::await_synchronously(tool.handler({
        .tools = tools,
        .args = AJson::Object{},  // empty args
        .allToolCalls = {},
    }));

    EXPECT_TRUE(result.contains("Error")) << "result = " << result;
    EXPECT_TRUE(result.contains("At least one")) << "result = " << result;
}

// ===========================================================================
// sendTelegramMessage – Both photo and audio specified returns error
// ===========================================================================
TEST(SendTelegramMessageTest, BothPhotoAndAudioError) {
    auto telegram = _new<TelegramMock>();
    auto openAI = _new<OpenAIMock>();
    auto chat = makeChat("Test Chat");

    auto messages = _new<td::td_api::array<td::td_api::object_ptr<td::td_api::message>>>();

    EXPECT_CALL(*telegram, sendQuery(testing::_))
        .Times(1)
        .WillRepeatedly([](td::td_api::object_ptr<td::td_api::Function> f) -> AFuture<ITelegramClient::Object> {
            // Verify that at least one call is chatActionTyping
            EXPECT_TRUE(f->get_id() == td::td_api::sendChatAction::ID ||
                        f->get_id() == td::td_api::sendMessage::ID);
            co_return co_await dispatchSendQuery(std::move(f));
        });

    EXPECT_CALL(*openAI, embedding(testing::_, testing::_)).Times(0);

    auto tool = tools::sendTelegramMessage(
        telegram, openAI, chat, std::move(messages));

    OpenAITools tools{};
    auto result = util::await_synchronously(tool.handler({
        .tools = tools,
        .args = AJson::Object{
            {"text", "hello"},
            {"photo_filename", "photo.jpg"},
            {"audio_filename", "audio.ogg"},
        },
        .allToolCalls = {},
    }));

    EXPECT_TRUE(result.contains("Error")) << "result = " << result;
    EXPECT_TRUE(result.contains("cannot attach both")) << "result = " << result;
}

// ===========================================================================
// sendTelegramMessage – Reply to message from another chat throws
// ===========================================================================
TEST(SendTelegramMessageTest, ReplyToMessageFromAnotherChatThrows) {
    auto telegram = _new<TelegramMock>();
    auto openAI = _new<OpenAIMock>();
    auto chat = makeChat("Test Chat");

    // Messages in this chat have ids 10, 11 — not 42
    auto messages = _new<td::td_api::array<td::td_api::object_ptr<td::td_api::message>>>();
    messages->push_back(makeMessage(10, 99999, "msg1"));
    messages->push_back(makeMessage(11, 99999, "msg2"));

    EXPECT_CALL(*telegram, sendQuery(testing::_))
        .Times(1)
        .WillRepeatedly([](td::td_api::object_ptr<td::td_api::Function> f) -> AFuture<ITelegramClient::Object> {
            // Verify that at least one call is chatActionTyping
            EXPECT_TRUE(f->get_id() == td::td_api::sendChatAction::ID ||
                        f->get_id() == td::td_api::sendMessage::ID);
            co_return co_await dispatchSendQuery(std::move(f));
        });
    EXPECT_CALL(*openAI, embedding(testing::_, testing::_)).Times(0);

    auto tool = tools::sendTelegramMessage(
        telegram, openAI, chat, std::move(messages));

    OpenAITools tools{};
    EXPECT_THROW(
        util::await_synchronously(tool.handler({
            .tools = tools,
            .args = AJson::Object{
                {"text", "Hello!"},
                {"reply_to_message_id", 42},
            },
            .allToolCalls = {},
        })),
        AException
    );
}

// ===========================================================================
// sendTelegramMessage – Reply to existing message succeeds
// ===========================================================================
TEST(SendTelegramMessageTest, ReplyToExistingMessage) {
    auto telegram = _new<TelegramMock>();
    auto openAI = _new<OpenAIMock>();
    auto chat = makeChat("Test Chat");

    auto messages = _new<td::td_api::array<td::td_api::object_ptr<td::td_api::message>>>();
    messages->push_back(makeMessage(10, 99999, "msg1"));
    messages->push_back(makeMessage(42, 99999, "target message"));

    EXPECT_CALL(*telegram, sendQuery(testing::_))
        .Times(testing::AtLeast(1))
        .WillRepeatedly([](td::td_api::object_ptr<td::td_api::Function> f) -> AFuture<ITelegramClient::Object> {
            co_return co_await dispatchSendQuery(std::move(f));
        });

    EXPECT_CALL(*openAI, embedding(testing::_, testing::_))
        .Times(1)
        .WillRepeatedly(testing::Return(AFuture(std::valarray<double>{-0.2})));

    auto tool = tools::sendTelegramMessage(
        telegram, openAI, chat, std::move(messages));

    OpenAITools tools{};
    auto result = util::await_synchronously(tool.handler({
        .tools = tools,
        .args = AJson::Object{
            {"text", "Hello!"},
            {"reply_to_message_id", 42},
        },
        .allToolCalls = {},
    }));

    EXPECT_TRUE(result.contains("sent successfully")) << "result = " << result;
}

// ===========================================================================
// sendTelegramMessage – Too many messages in a row throws
// ===========================================================================
TEST(SendTelegramMessageTest, TooManyMessagesInRowThrows) {
    auto telegram = _new<TelegramMock>();
    auto openAI = _new<OpenAIMock>();
    auto chat = makeChat("Test Chat");

    auto messages = _new<td::td_api::array<td::td_api::object_ptr<td::td_api::message>>>();

    EXPECT_CALL(*telegram, sendQuery(testing::_))
        .Times(testing::AtLeast(1))
        .WillRepeatedly([](td::td_api::object_ptr<td::td_api::Function> f) -> AFuture<ITelegramClient::Object> {
            co_return co_await dispatchSendQuery(std::move(f));
        });

    EXPECT_CALL(*openAI, embedding(testing::_, testing::_))
        .Times(testing::AtLeast(1))
        .WillRepeatedly(testing::Return(AFuture(std::valarray<double>{-0.2})));

    auto tool = tools::sendTelegramMessage(
        telegram, openAI, chat, std::move(messages));

    OpenAITools tools{};
    // Send 11 messages — the 11th should throw
    auto test = [&] {
        for (int i = 0; i < 20; ++i) {
            auto result = util::await_synchronously(tool.handler({
                .tools = tools,
                .args = AJson::Object{{"text", "msg{}"_format(i)}},
                .allToolCalls = {},
            }));
            EXPECT_TRUE(result.contains("sent successfully")) << "at iteration " << i << ": " << result;
        }
    };

    // ~11th call should throw
    EXPECT_THROW(test(), AException);
}

// ===========================================================================
// sendTelegramMessage – Multi-line message gets split
// ===========================================================================
TEST(SendTelegramMessageTest, MultiLineMessageGetsSplit) {
    auto telegram = _new<TelegramMock>();
    auto openAI = _new<OpenAIMock>();
    auto chat = makeChat("Test Chat");

    auto messages = _new<td::td_api::array<td::td_api::object_ptr<td::td_api::message>>>();

    // Expect sendQuery for typing + sendMessage for each line
    EXPECT_CALL(*telegram, sendQuery(testing::_))
        .Times(testing::AtLeast(3))
        .WillRepeatedly([](td::td_api::object_ptr<td::td_api::Function> f) -> AFuture<ITelegramClient::Object> {
            co_return co_await dispatchSendQuery(std::move(f));
        });

    EXPECT_CALL(*openAI, embedding(testing::_, testing::_))
        .Times(testing::AtLeast(1))
        .WillRepeatedly(testing::Return(AFuture(std::valarray<double>{-0.2})));

    auto tool = tools::sendTelegramMessage(
        telegram, openAI, chat, std::move(messages));

    OpenAITools tools{};
    auto result = util::await_synchronously(tool.handler({
        .tools = tools,
        .args = AJson::Object{{"text", "line1\nline2\nline3"}},
        .allToolCalls = {},
    }));

    EXPECT_TRUE(result.contains("sent successfully")) << "result = " << result;
}

// ===========================================================================
// sendTelegramMessage – Invalid photo filename with "/" throws
// ===========================================================================
TEST(SendTelegramMessageTest, InvalidPhotoFilenameSlash) {
    auto telegram = _new<TelegramMock>();
    auto openAI = _new<OpenAIMock>();
    auto chat = makeChat("Test Chat");

    auto messages = _new<td::td_api::array<td::td_api::object_ptr<td::td_api::message>>>();

    EXPECT_CALL(*telegram, sendQuery(testing::_))
        .WillRepeatedly([](td::td_api::object_ptr<td::td_api::Function> f) -> AFuture<ITelegramClient::Object> {
            EXPECT_EQ(f->get_id(), td::td_api::sendChatAction::ID);
            co_return co_await dispatchSendQuery(std::move(f));
        });
    EXPECT_CALL(*openAI, embedding(testing::_, testing::_))
        .Times(0)
        .WillRepeatedly(testing::Return(AFuture(std::valarray<double>{-0.2})));

    auto tool = tools::sendTelegramMessage(
        telegram, openAI, chat, std::move(messages));

    OpenAITools tools{};
    EXPECT_THROW(
        util::await_synchronously(tool.handler({
            .tools = tools,
            .args = AJson::Object{
                {"text", "hello"},
                {"photo_filename", "subdir/photo.jpg"},
            },
            .allToolCalls = {},
        })),
        AException
    );
}

// ===========================================================================
// sendTelegramMessage – Invalid photo filename with ".." throws
// ===========================================================================
TEST(SendTelegramMessageTest, InvalidPhotoFilenameDotDot) {
    auto telegram = _new<TelegramMock>();
    auto openAI = _new<OpenAIMock>();
    auto chat = makeChat("Test Chat");

    auto messages = _new<td::td_api::array<td::td_api::object_ptr<td::td_api::message>>>();

    EXPECT_CALL(*telegram, sendQuery(testing::_))
        .WillRepeatedly([](td::td_api::object_ptr<td::td_api::Function> f) -> AFuture<ITelegramClient::Object> {
            EXPECT_EQ(f->get_id(), td::td_api::sendChatAction::ID);
            co_return co_await dispatchSendQuery(std::move(f));
        });
    EXPECT_CALL(*openAI, embedding(testing::_, testing::_))
        .Times(0)
        .WillRepeatedly(testing::Return(AFuture(std::valarray<double>{-0.2})));

    auto tool = tools::sendTelegramMessage(
        telegram, openAI, chat, std::move(messages));

    OpenAITools tools{};
    EXPECT_THROW(
        util::await_synchronously(tool.handler({
            .tools = tools,
            .args = AJson::Object{
                {"text", "hello"},
                {"photo_filename", "../photo.jpg"},
            },
            .allToolCalls = {},
        })),
        AException
    );
}

// ===========================================================================
// sendTelegramMessage – Invalid audio filename with "/" throws
// ===========================================================================
TEST(SendTelegramMessageTest, InvalidAudioFilenameSlash) {
    auto telegram = _new<TelegramMock>();
    auto openAI = _new<OpenAIMock>();
    auto chat = makeChat("Test Chat");

    auto messages = _new<td::td_api::array<td::td_api::object_ptr<td::td_api::message>>>();

    EXPECT_CALL(*telegram, sendQuery(testing::_))
        .WillRepeatedly([](td::td_api::object_ptr<td::td_api::Function> f) -> AFuture<ITelegramClient::Object> {
            EXPECT_EQ(f->get_id(), td::td_api::sendChatAction::ID);
            co_return co_await dispatchSendQuery(std::move(f));
        });
    EXPECT_CALL(*openAI, embedding(testing::_, testing::_))
        .Times(0)
        .WillRepeatedly(testing::Return(AFuture(std::valarray<double>{-0.2})));

    auto tool = tools::sendTelegramMessage(
        telegram, openAI, chat, std::move(messages));

    OpenAITools tools{};
    EXPECT_THROW(
        util::await_synchronously(tool.handler({
            .tools = tools,
            .args = AJson::Object{
                {"audio_filename", "subdir/audio.ogg"},
            },
            .allToolCalls = {},
        })),
        AException
    );
}

// ===========================================================================
// sendTelegramMessage – Invalid audio filename with ".." throws
// ===========================================================================
TEST(SendTelegramMessageTest, InvalidAudioFilenameDotDot) {
    auto telegram = _new<TelegramMock>();
    auto openAI = _new<OpenAIMock>();
    auto chat = makeChat("Test Chat");

    auto messages = _new<td::td_api::array<td::td_api::object_ptr<td::td_api::message>>>();

    EXPECT_CALL(*telegram, sendQuery(testing::_))
        .WillRepeatedly([](td::td_api::object_ptr<td::td_api::Function> f) -> AFuture<ITelegramClient::Object> {
            EXPECT_EQ(f->get_id(), td::td_api::sendChatAction::ID);
            co_return co_await dispatchSendQuery(std::move(f));
        });
    EXPECT_CALL(*openAI, embedding(testing::_, testing::_))
        .Times(0);

    auto tool = tools::sendTelegramMessage(
        telegram, openAI, chat, std::move(messages));

    OpenAITools tools{};
    EXPECT_THROW(
        util::await_synchronously(tool.handler({
            .tools = tools,
            .args = AJson::Object{
                {"audio_filename", "../audio.ogg"},
            },
            .allToolCalls = {},
        })),
        AException
    );
}

// ===========================================================================
// sendTelegramMessage – Repeat detection: similar message throws
// ===========================================================================
TEST(SendTelegramMessageTest, RepeatDetectionThrows) {
    auto telegram = _new<TelegramMock>();
    auto openAI = _new<OpenAIMock>();
    auto chat = makeChat("Test Chat");

    // Previous messages: one from Kuni with text "Hello there!"
    auto messages = _new<td::td_api::array<td::td_api::object_ptr<td::td_api::message>>>();
    messages->push_back(makeMessage(1, 123456 /* Kuni's ID */, "Hello there!"));

    EXPECT_CALL(*telegram, myId())
        .Times(testing::AtLeast(1))
        .WillRepeatedly(testing::Return(123456 /* Kuni's ID */));

    // Embedding: return a vector that will produce high similarity
    // We return the same vector for both calls (target and history) -> cosine_similarity = 1.0
    std::valarray<double> embeddingVec = {0.1, 0.2, 0.3, 0.4, 0.5};

    EXPECT_CALL(*openAI, embedding(testing::_, testing::_))
        .Times(testing::AtLeast(2))
        .WillRepeatedly(testing::Return(AFuture(embeddingVec)));

    // sendQuery may be called for typing action before the throw
    EXPECT_CALL(*telegram, sendQuery(testing::_))
        .WillRepeatedly([](td::td_api::object_ptr<td::td_api::Function> f) -> AFuture<ITelegramClient::Object> {
            EXPECT_EQ(f->get_id(), td::td_api::sendChatAction::ID);
            co_return co_await dispatchSendQuery(std::move(f));
        });

    auto tool = tools::sendTelegramMessage(
        telegram, openAI, chat, std::move(messages));

    OpenAITools tools{};
    EXPECT_THROW(
        util::await_synchronously(tool.handler({
            .tools = tools,
            .args = AJson::Object{{"text", "Hello there!"}},
            .allToolCalls = {},
        })),
        AException
    );
}

// ===========================================================================
// sendTelegramMessage – First message encourages follow-up
// ===========================================================================
TEST(SendTelegramMessageTest, FirstMessageEncouragesFollowUp) {
    auto telegram = _new<TelegramMock>();
    auto openAI = _new<OpenAIMock>();
    auto chat = makeChat("Test Chat");

    auto messages = _new<td::td_api::array<td::td_api::object_ptr<td::td_api::message>>>();

    EXPECT_CALL(*telegram, sendQuery(testing::_))
        .Times(testing::AtLeast(1))
        .WillRepeatedly([](td::td_api::object_ptr<td::td_api::Function> f) -> AFuture<ITelegramClient::Object> {
            co_return co_await dispatchSendQuery(std::move(f));
        });

    EXPECT_CALL(*openAI, embedding(testing::_, testing::_))
        .Times(1)
        .WillRepeatedly(testing::Return(AFuture(std::valarray<double>{-0.2})));

    auto tool = tools::sendTelegramMessage(
        telegram, openAI, chat, std::move(messages));

    OpenAITools tools{};
    auto result = util::await_synchronously(tool.handler({
        .tools = tools,
        .args = AJson::Object{{"text", "Hi!"}},
        .allToolCalls = {},
    }));

    EXPECT_TRUE(result.contains("follow-up")) << "result = " << result;
}

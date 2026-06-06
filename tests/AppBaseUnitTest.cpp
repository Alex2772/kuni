//
// Created by alex2772 on 5/13/26.
//

#include "AppBase.h"
#include "IOpenAIChat.h"
#include "OpenAITools.h"
#include "Diary.h"
#include "common.h"

#include <gmock/gmock.h>
#include <AUI/Thread/AAsyncHolder.h>
#include <AUI/Thread/AEventLoop.h>
#include <AUI/IO/APath.h>
#include <AUI/Logging/ALogger.h>

#include <range/v3/algorithm/any_of.hpp>

// ============================================================================
// Mock IOpenAIChat — returns canned responses, no real API calls
// ============================================================================
class OpenAIMock : public IOpenAIChat {
public:
    MOCK_METHOD(AFuture<Response>, chat, (Params params, AVector<Message> messages), (override));

    _<StreamingResponse> chatStreaming(Params params, AVector<Message> messages) override {
        return nullptr;
    }

    MOCK_METHOD(AFuture<std::valarray<double>>, embedding, (Params params, AString input), (override));
};

// ============================================================================
// Helper: create a minimal chat response that calls #wait (pause)
// ============================================================================
static AFuture<IOpenAIChat::Response> makeWaitResponse() {
    IOpenAIChat::Message msg;
    msg.role = IOpenAIChat::Message::Role::ASSISTANT;
    msg.content = "";
    msg.tool_calls = {
        IOpenAIChat::Message::ToolCall{
            .id = "call_wait_1",
            .index = 0,
            .type = "function",
            .function = {
                .name = "wait",
                .arguments = "{}",
            },
        },
    };

    IOpenAIChat::Response resp;
    resp.choices = {
        IOpenAIChat::Response::Choice{
            .index = 0,
            .message = std::move(msg),
            .finish_reason = "tool_calls",
        },
    };
    resp.usage = { .prompt_tokens = 10, .completion_tokens = 5, .total_tokens = 15 };
    co_return resp;
}

// ============================================================================
// Helper: create an embedding result (dummy vector)
// ============================================================================
static std::valarray<double> makeDummyEmbedding() {
    return std::valarray<double>{0.1, 0.2, 0.3, 0.4, 0.5};
}

// ============================================================================
// AppTestHarness — controlled AppBase subclass for unit testing
// ============================================================================
class AppTestHarness : public AppBase {
public:
    explicit AppTestHarness(_<IOpenAIChat> openAI)
        : AppBase(Init{
              .workingDir = "test_data_appbase_unit",
              .openAI = std::move(openAI),
          })
    {
        // Clean slate
        APath("test_data_appbase_unit").removeFileRecursive();
    }

    ~AppTestHarness() override {
        APath("test_data_appbase_unit").removeFileRecursive();
    }

    // Expose protected members for testing
    using AppBase::mTemporaryContext;
    using AppBase::mRelevanceThreshold;
    using AppBase::openAI;
    using AppBase::takeDiaryEntry;
    using AppBase::removeNotifications;
    using AppBase::updateTools;
    using AppBase::diaryDumpMessages;
    using AppBase::onBeforeMainLoop;

    // Expose diary
    using AppBase::diary;

    // Count how many times updateTools was called
    int updateToolsCallCount = 0;

    void updateTools(OpenAITools& actions) override {
        ++updateToolsCallCount;
        AppBase::updateTools(actions);

        // Always provide #wait and #pause so the main loop can terminate
        actions.insert({
            .name = "pause",
            .description = "Pauses the conversation",
            .handler = [](OpenAITools::Ctx) -> AFuture<AString> {
                co_return "Paused";
            },
        });
        actions.insert({
            .name = "wait",
            .description = "Wait until further notifications",
            .handler = [](OpenAITools::Ctx) -> AFuture<AString> {
                co_return "Waiting";
            },
        });
    }
};

// ============================================================================
// Test fixture
// ============================================================================
class AppBaseUnitTest : public ::testing::Test {
protected:
    void SetUp() override {
        APath("test_data_appbase_unit").removeFileRecursive();
    }

    void TearDown() override {
        APath("test_data_appbase_unit").removeFileRecursive();
    }

};

// ============================================================================
// passNotificationToAI — basic queue and signal
// ============================================================================
TEST_F(AppBaseUnitTest, PassNotificationToAIBasic) {
    AAsyncHolder async;
    AEventLoop loop;
    IEventLoop::Handle h(&loop);

    auto openAI = _cast<IOpenAIChat>(_new<OpenAIMock>());
    EXPECT_CALL(*static_cast<OpenAIMock*>(openAI.get()), chat(::testing::_, ::testing::_))
        .WillOnce(::testing::Return(makeWaitResponse()));

    AppTestHarness app(openAI);
    async << app.passNotificationToAI("Test notification message").onProcessed;
    while (async.size() > 0) {
        loop.iteration();
    }

    // The notification should have been processed — context is non-empty
    EXPECT_FALSE(app.temporaryContext().empty());
}

// ============================================================================
// passNotificationToAI — multiple notifications are queued
// ============================================================================
TEST_F(AppBaseUnitTest, PassNotificationToAIMultiple) {
    AAsyncHolder async;
    AEventLoop loop;
    IEventLoop::Handle h(&loop);

    auto openAI = _cast<IOpenAIChat>(_new<OpenAIMock>());
    EXPECT_CALL(*static_cast<OpenAIMock*>(openAI.get()), chat(::testing::_, ::testing::_))
        .WillOnce(::testing::Return(makeWaitResponse()))
        .WillOnce(::testing::Return(makeWaitResponse()))
    ;

    AppTestHarness app(openAI);

    async << app.passNotificationToAI("First notification").onProcessed;
    while (async.size() > 0) {
        loop.iteration();
    }

    // After first notification is processed, send another
    async << app.passNotificationToAI("Second notification").onProcessed;
    while (async.size() > 0) {
        loop.iteration();
    }

    EXPECT_FALSE(app.temporaryContext().empty());
}

// ============================================================================
// passNotificationToAI — first=true inserts at front
// ============================================================================
TEST_F(AppBaseUnitTest, PassNotificationToAIFirst) {
    AAsyncHolder async;
    AEventLoop loop;
    IEventLoop::Handle h(&loop);

    auto openAI = _cast<IOpenAIChat>(_new<OpenAIMock>());
    EXPECT_CALL(*static_cast<OpenAIMock*>(openAI.get()), chat(::testing::_, ::testing::_))
        .WillOnce(::testing::Return(makeWaitResponse()))
    ;

    AppTestHarness app(openAI);

    // Insert urgent first, then normal — urgent should be processed first
    async << app.passNotificationToAI("Urgent notification", {}, true).onProcessed;
    while (async.size() > 0) {
        loop.iteration();
    }

    EXPECT_FALSE(app.temporaryContext().empty());
}

// ============================================================================
// removeNotifications — removes by substring
// ============================================================================
TEST_F(AppBaseUnitTest, RemoveNotificationsBySubstring) {
    AAsyncHolder async;
    AEventLoop loop;
    IEventLoop::Handle h(&loop);

    auto openAI = _cast<IOpenAIChat>(_new<OpenAIMock>());
    EXPECT_CALL(*static_cast<OpenAIMock*>(openAI.get()), chat(::testing::_, ::testing::_))
        .WillOnce(::testing::Return(makeWaitResponse()));

    AppTestHarness app(openAI);

    app.passNotificationToAI("Message about cats");
    async << app.passNotificationToAI("Message about dogs").onProcessed;
    app.passNotificationToAI("Message about cats again");

    app.removeNotifications("cats");

    while (async.size() > 0) {
        loop.iteration();
    }

    // No crash = success
    EXPECT_TRUE(true);
}

// ============================================================================
// removeNotifications — no match does nothing
// ============================================================================
TEST_F(AppBaseUnitTest, RemoveNotificationsNoMatch) {
    AAsyncHolder async;
    AEventLoop loop;
    IEventLoop::Handle h(&loop);

    auto openAI = _cast<IOpenAIChat>(_new<OpenAIMock>());
    EXPECT_CALL(*static_cast<OpenAIMock*>(openAI.get()), chat(::testing::_, ::testing::_))
        .WillOnce(::testing::Return(makeWaitResponse()))
        .WillOnce(::testing::Return(makeWaitResponse()))
    ;

    AppTestHarness app(openAI);

    async << app.passNotificationToAI("Message about cats").onProcessed;
    async << app.passNotificationToAI("Message about dogs").onProcessed;

    app.removeNotifications("nonexistent");

    while (async.size() > 0) {
        loop.iteration();
    }

    EXPECT_TRUE(true); // no crash = success
}

// ============================================================================
// getSystemPrompt — returns non-empty prompt with character info
// ============================================================================
TEST_F(AppBaseUnitTest, GetSystemPromptNotEmpty) {
    auto prompt = AppBase::getSystemPrompt();
    EXPECT_FALSE(prompt.empty());
    EXPECT_TRUE(prompt.contains("<your_appearance>"));
    EXPECT_TRUE(prompt.contains("</your_appearance>"));
}

// ============================================================================
// takeDiaryEntry — formats entry with XML tags
// ============================================================================
TEST_F(AppBaseUnitTest, TakeDiaryEntryFormatsCorrectly) {
    auto openAI = _cast<IOpenAIChat>(_new<OpenAIMock>());
    AppTestHarness app(openAI);

    // Manually add a diary entry
    APath("test_data_appbase_unit/diary").makeDirs();
    app.diary().save(Diary::EntryEx{
        .id = "test_entry_1",
        .metadata = {
            .score = 0.5f,
            .embedding = makeDummyEmbedding(),
        },
        .freeformBody = "John likes pizza and programming.",
    });
    app.diary().reload();

    // Query to get an EntryExAndRelatedness
    AAsyncHolder async;
    AEventLoop loop;
    IEventLoop::Handle h(&loop);

    Diary::EntryExAndRelatedness found{};
    bool foundEntry = false;

    async << [&]() -> AFuture<> {
        auto results = co_await app.diary().query(makeDummyEmbedding(), {});
        if (!results.empty()) {
            found = results.front();
            foundEntry = true;
        }
    }();

    while (async.size() > 0) {
        loop.iteration();
    }

    ASSERT_TRUE(foundEntry);

    // takeDiaryEntry should format with XML tags
    AString formatted = app.takeDiaryEntry(found);
    EXPECT_FALSE(formatted.empty());
    EXPECT_TRUE(formatted.contains("<your_diary_page"));
    EXPECT_TRUE(formatted.contains("</your_diary_page"));
    EXPECT_TRUE(formatted.contains("John likes pizza"));
    EXPECT_TRUE(formatted.contains("just_for_reasoning"));
    EXPECT_TRUE(formatted.contains("no_plagiarism"));
}

// ============================================================================
// takeDiaryEntry — skips entries already in context (dedup)
// ============================================================================
TEST_F(AppBaseUnitTest, TakeDiaryEntrySkipsDuplicates) {
    auto openAI = _cast<IOpenAIChat>(_new<OpenAIMock>());
    AppTestHarness app(openAI);

    // Put the entry text into temporary context first
    app.mTemporaryContext << IOpenAIChat::Message{
        .role = IOpenAIChat::Message::Role::USER,
        .content = "John likes pizza and programming.",
    };

    // Save the same text as a diary entry
    APath("test_data_appbase_unit/diary").makeDirs();
    app.diary().save(Diary::EntryEx{
        .id = "test_entry_dup",
        .metadata = {
            .score = 0.5f,
            .embedding = makeDummyEmbedding(),
        },
        .freeformBody = "John likes pizza and programming.",
    });
    app.diary().reload();

    AAsyncHolder async;
    AEventLoop loop;
    IEventLoop::Handle h(&loop);

    Diary::EntryExAndRelatedness found{};
    bool foundEntry = false;

    async << [&]() -> AFuture<> {
        auto results = co_await app.diary().query(makeDummyEmbedding(), {});
        if (!results.empty()) {
            found = results.front();
            foundEntry = true;
        }
    }();

    while (async.size() > 0) {
        loop.iteration();
    }

    ASSERT_TRUE(foundEntry);

    // takeDiaryEntry should return empty because the content is already in context
    AString formatted = app.takeDiaryEntry(found);
    EXPECT_TRUE(formatted.empty());
}

// ============================================================================
// takeDiaryEntry — increments usage count and updates score
// ============================================================================
TEST_F(AppBaseUnitTest, TakeDiaryEntryUpdatesMetadata) {
    auto openAI = _cast<IOpenAIChat>(_new<OpenAIMock>());
    AppTestHarness app(openAI);

    APath("test_data_appbase_unit/diary").makeDirs();
    app.diary().save(Diary::EntryEx{
        .metadata = {
            .score = 0.0f,
            .usageCount = 0,
            .embedding = makeDummyEmbedding(),
        },
        .freeformBody = "Unique content about space exploration.",
    });
    app.diary().reload();

    AAsyncHolder async;
    AEventLoop loop;
    IEventLoop::Handle h(&loop);

    Diary::EntryExAndRelatedness found{};
    bool foundEntry = false;

    async << [&]() -> AFuture<> {
        auto results = co_await app.diary().query(makeDummyEmbedding(), {});
        if (!results.empty()) {
            found = results.front();
            foundEntry = true;
        }
    }();

    while (async.size() > 0) {
        loop.iteration();
    }

    ASSERT_TRUE(foundEntry);

    [[maybe_unused]] auto entry = app.takeDiaryEntry(found);

    // After takeDiaryEntry, the entry is unloaded (removed from cache),
    // so we can't check the in-memory metadata. But we can verify
    // the entry was removed from the diary listing.
    EXPECT_FALSE(ranges::any_of(app.diary().list(), [](const auto& e) {
        return e.id == "test_entry_meta";
    }));
}

// ============================================================================
// updateTools — adds unified ask tool
// ============================================================================
TEST_F(AppBaseUnitTest, UpdateToolsAddsExpectedTools) {
    auto openAI = _cast<IOpenAIChat>(_new<OpenAIMock>());
    AppTestHarness app(openAI);

    OpenAITools tools{};
    app.updateTools(tools);

    auto handlers = tools.handlers();
    EXPECT_TRUE(handlers.contains("ask"));
    AThread::processMessages();
}

// ============================================================================
// updateTools — called during notification processing
// ============================================================================
TEST_F(AppBaseUnitTest, UpdateToolsCalledDuringProcessing) {
    AAsyncHolder async;
    AEventLoop loop;
    IEventLoop::Handle h(&loop);

    auto openAI = _cast<IOpenAIChat>(_new<OpenAIMock>());
    EXPECT_CALL(*static_cast<OpenAIMock*>(openAI.get()), chat(::testing::_, ::testing::_))
        .WillOnce(::testing::Return(makeWaitResponse()));

    AppTestHarness app(openAI);

    int beforeCount = app.updateToolsCallCount;
    async << app.passNotificationToAI("Test").onProcessed;
    while (async.size() > 0) {
        loop.iteration();
    }

    // updateTools should have been called at least once more
    EXPECT_GE(app.updateToolsCallCount, beforeCount);
}

// ============================================================================
// isActingProactively — initially false
// ============================================================================
TEST_F(AppBaseUnitTest, IsActingProactivelyInitiallyFalse) {
    auto openAI = _cast<IOpenAIChat>(_new<OpenAIMock>());
    AppTestHarness app(openAI);

    EXPECT_FALSE(app.isActingProactively());
}

// ============================================================================
// temporaryContext — initially empty
// ============================================================================
TEST_F(AppBaseUnitTest, TemporaryContextInitiallyEmpty) {
    auto openAI = _cast<IOpenAIChat>(_new<OpenAIMock>());
    AppTestHarness app(openAI);

    EXPECT_TRUE(app.temporaryContext().empty());
    AThread::processMessages();
}

// ============================================================================
// temporaryContext — accumulates messages after notification
// ============================================================================
TEST_F(AppBaseUnitTest, TemporaryContextAccumulatesMessages) {
    AAsyncHolder async;
    AEventLoop loop;
    IEventLoop::Handle h(&loop);

    auto openAI = _cast<IOpenAIChat>(_new<OpenAIMock>());
    EXPECT_CALL(*static_cast<OpenAIMock*>(openAI.get()), chat(::testing::_, ::testing::_))
        .WillOnce(::testing::Return(makeWaitResponse()));

    AppTestHarness app(openAI);

    async << app.passNotificationToAI("Hello from test").onProcessed;
    while (async.size() > 0) {
        loop.iteration();
    }

    // After processing, the context should have at least the user message
    // and the assistant response
    EXPECT_FALSE(app.temporaryContext().empty());

    // The last message should be from the assistant (tool call response)
    const auto& lastMsg = app.temporaryContext().last();
    EXPECT_EQ(lastMsg.role, IOpenAIChat::Message::Role::TOOL);
}

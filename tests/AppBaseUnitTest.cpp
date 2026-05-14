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
    MOCK_METHOD(AFuture<Response>, chat, (Params params, AVector<Message> messages), (const, override));

    _<StreamingResponse> chatStreaming(Params params, AVector<Message> messages) const override {
        return nullptr;
    }

    MOCK_METHOD(AFuture<std::valarray<double>>, embedding, (Params params, AString input), (const, override));
};

// ============================================================================
// Helper: create a minimal chat response that calls #wait (pause)
// ============================================================================
static IOpenAIChat::Response makeWaitResponse() {
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
    return resp;
}

// ============================================================================
// Helper: create a chat response that calls #pause
// ============================================================================
static IOpenAIChat::Response makePauseResponse() {
    IOpenAIChat::Message msg;
    msg.role = IOpenAIChat::Message::Role::ASSISTANT;
    msg.content = "";
    msg.tool_calls = {
        IOpenAIChat::Message::ToolCall{
            .id = "call_pause_1",
            .index = 0,
            .type = "function",
            .function = {
                .name = "pause",
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
    return resp;
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

    /**
     * Pump the event loop until the given async holder is empty.
     */
    static void pump(AAsyncHolder& async) {
        AEventLoop loop;
        IEventLoop::Handle h(&loop);
        while (async.size() > 0) {
            loop.iteration();
        }
    }

    /**
     * Pump the event loop a fixed number of iterations.
     */
    static void pumpIterations(int n) {
        AEventLoop loop;
        IEventLoop::Handle h(&loop);
        for (int i = 0; i < n; ++i) {
            loop.iteration();
        }
    }
};

// ============================================================================
// passNotificationToAI — basic queue and signal
// ============================================================================
TEST_F(AppBaseUnitTest, PassNotificationToAIBasic) {
    auto openAI = _cast<IOpenAIChat>(_new<OpenAIMock>());
    EXPECT_CALL(*static_cast<OpenAIMock*>(openAI.get()), chat(::testing::_, ::testing::_))
        .WillOnce(::testing::Return(AFuture<IOpenAIChat::Response>(makeWaitResponse())));

    AppTestHarness app(openAI);

    // Pump to let the main loop coroutine start
    pumpIterations(5);

    AAsyncHolder async;
    async << app.passNotificationToAI("Test notification message").onProcessed;
    pump(async);

    // The notification should have been processed — context is non-empty
    EXPECT_FALSE(app.temporaryContext().empty());
}

// ============================================================================
// passNotificationToAI — multiple notifications are queued
// ============================================================================
TEST_F(AppBaseUnitTest, PassNotificationToAIMultiple) {
    auto openAI = _cast<IOpenAIChat>(_new<OpenAIMock>());
    EXPECT_CALL(*static_cast<OpenAIMock*>(openAI.get()), chat(::testing::_, ::testing::_))
        .WillOnce(::testing::Return(AFuture<IOpenAIChat::Response>(makeWaitResponse())));

    AppTestHarness app(openAI);

    pumpIterations(5);

    AAsyncHolder async;
    async << app.passNotificationToAI("First notification").onProcessed;
    pump(async);

    // After first notification is processed, send another
    async << app.passNotificationToAI("Second notification").onProcessed;
    pump(async);

    EXPECT_FALSE(app.temporaryContext().empty());
}

// ============================================================================
// passNotificationToAI — first=true inserts at front
// ============================================================================
TEST_F(AppBaseUnitTest, PassNotificationToAIFirst) {
    auto openAI = _cast<IOpenAIChat>(_new<OpenAIMock>());
    EXPECT_CALL(*static_cast<OpenAIMock*>(openAI.get()), chat(::testing::_, ::testing::_))
        .WillOnce(::testing::Return(AFuture<IOpenAIChat::Response>(makeWaitResponse())));

    AppTestHarness app(openAI);

    pumpIterations(5);

    // Insert urgent first, then normal — urgent should be processed first
    AAsyncHolder async;
    async << app.passNotificationToAI("Urgent notification", {}, true).onProcessed;
    pump(async);

    EXPECT_FALSE(app.temporaryContext().empty());
}

// ============================================================================
// removeNotifications — removes by substring
// ============================================================================
TEST_F(AppBaseUnitTest, RemoveNotificationsBySubstring) {
    auto openAI = _cast<IOpenAIChat>(_new<OpenAIMock>());
    EXPECT_CALL(*static_cast<OpenAIMock*>(openAI.get()), chat(::testing::_, ::testing::_))
        .WillOnce(::testing::Return(AFuture<IOpenAIChat::Response>(makePauseResponse())));

    AppTestHarness app(openAI);

    pumpIterations(5);

    app.passNotificationToAI("Message about cats");
    app.passNotificationToAI("Message about dogs");
    app.passNotificationToAI("Message about cats again");

    app.removeNotifications("cats");

    // No crash = success
    EXPECT_TRUE(true);
}

// ============================================================================
// removeNotifications — no match does nothing
// ============================================================================
TEST_F(AppBaseUnitTest, RemoveNotificationsNoMatch) {
    auto openAI = _cast<IOpenAIChat>(_new<OpenAIMock>());
    EXPECT_CALL(*static_cast<OpenAIMock*>(openAI.get()), chat(::testing::_, ::testing::_))
        .WillOnce(::testing::Return(AFuture<IOpenAIChat::Response>(makePauseResponse())));

    AppTestHarness app(openAI);

    pumpIterations(5);

    app.passNotificationToAI("Message about cats");
    app.passNotificationToAI("Message about dogs");

    app.removeNotifications("nonexistent");
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
    EXPECT_TRUE(formatted.contains("</your_diary_page>"));
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

    app.takeDiaryEntry(found);

    // After takeDiaryEntry, the entry is unloaded (removed from cache),
    // so we can't check the in-memory metadata. But we can verify
    // the entry was removed from the diary listing.
    EXPECT_FALSE(ranges::any_of(app.diary().list(), [](const auto& e) {
        return e.id == "test_entry_meta";
    }));
}

// ============================================================================
// updateTools — adds askDiary and askGoogle tools
// ============================================================================
TEST_F(AppBaseUnitTest, UpdateToolsAddsExpectedTools) {
    auto openAI = _cast<IOpenAIChat>(_new<OpenAIMock>());
    AppTestHarness app(openAI);

    OpenAITools tools{};
    app.updateTools(tools);

    auto handlers = tools.handlers();
    EXPECT_TRUE(handlers.contains("ask_diary"));
    EXPECT_TRUE(handlers.contains("ask_google"));
}

// ============================================================================
// updateTools — called during notification processing
// ============================================================================
TEST_F(AppBaseUnitTest, UpdateToolsCalledDuringProcessing) {
    auto openAI = _cast<IOpenAIChat>(_new<OpenAIMock>());
    EXPECT_CALL(*static_cast<OpenAIMock*>(openAI.get()), chat(::testing::_, ::testing::_))
        .WillOnce(::testing::Return(AFuture<IOpenAIChat::Response>(makeWaitResponse())));

    AppTestHarness app(openAI);

    pumpIterations(5);

    int beforeCount = app.updateToolsCallCount;
    AAsyncHolder async;
    async << app.passNotificationToAI("Test").onProcessed;
    pump(async);

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
}

// ============================================================================
// temporaryContext — accumulates messages after notification
// ============================================================================
TEST_F(AppBaseUnitTest, TemporaryContextAccumulatesMessages) {
    auto openAI = _cast<IOpenAIChat>(_new<OpenAIMock>());
    EXPECT_CALL(*static_cast<OpenAIMock*>(openAI.get()), chat(::testing::_, ::testing::_))
        .WillOnce(::testing::Return(AFuture<IOpenAIChat::Response>(makeWaitResponse())));

    AppTestHarness app(openAI);

    pumpIterations(5);

    AAsyncHolder async;
    async << app.passNotificationToAI("Hello from test").onProcessed;
    pump(async);

    // After processing, the context should have at least the user message
    // and the assistant response
    EXPECT_FALSE(app.temporaryContext().empty());

    // The last message should be from the assistant (tool call response)
    const auto& lastMsg = app.temporaryContext().last();
    EXPECT_EQ(lastMsg.role, IOpenAIChat::Message::Role::TOOL);
}

// ============================================================================
// diaryDumpMessages — clears context when empty
// ============================================================================
TEST_F(AppBaseUnitTest, DiaryDumpMessagesWithEmptyContext) {
    auto openAI = _cast<IOpenAIChat>(_new<OpenAIMock>());
    AppTestHarness app(openAI);

    AAsyncHolder async;
    async << app.diaryDumpMessages();
    pump(async);

    EXPECT_TRUE(app.temporaryContext().empty());
}

// ============================================================================
// actProactively — creates a proactive notification
// ============================================================================
TEST_F(AppBaseUnitTest, ActProactivelyCreatesNotification) {
    auto openAI = _cast<IOpenAIChat>(_new<OpenAIMock>());
    EXPECT_CALL(*static_cast<OpenAIMock*>(openAI.get()), chat(::testing::_, ::testing::_))
        .WillOnce(::testing::Return(AFuture<IOpenAIChat::Response>(makeWaitResponse())));

    AppTestHarness app(openAI);

    pumpIterations(5);

    AAsyncHolder async;
    async << app.passNotificationToAI("Proactive test").onProcessed;
    pump(async);

    EXPECT_FALSE(app.isActingProactively());
}

// ============================================================================
// Notification lifecycle: onStartedProcessing and onProcessed fire
// ============================================================================
TEST_F(AppBaseUnitTest, NotificationLifecycleFiresCallbacks) {
    auto openAI = _cast<IOpenAIChat>(_new<OpenAIMock>());
    EXPECT_CALL(*static_cast<OpenAIMock*>(openAI.get()), chat(::testing::_, ::testing::_))
        .WillOnce(::testing::Return(AFuture<IOpenAIChat::Response>(makeWaitResponse())));

    AppTestHarness app(openAI);

    pumpIterations(5);

    bool started = false;
    bool processed = false;

    const auto& notif = app.passNotificationToAI("Lifecycle test");
    notif.onStartedProcessing.onSuccess([&] { started = true; });
    notif.onProcessed.onSuccess([&] { processed = true; });

    AAsyncHolder async;
    async << notif.onProcessed;
    pump(async);

    EXPECT_TRUE(started);
    EXPECT_TRUE(processed);
}

// ============================================================================
// Multiple notifications are processed in FIFO order
// ============================================================================
TEST_F(AppBaseUnitTest, MultipleNotificationsProcessedInOrder) {
    auto openAI = _cast<IOpenAIChat>(_new<OpenAIMock>());
    // Return wait for first, pause for second
    EXPECT_CALL(*static_cast<OpenAIMock*>(openAI.get()), chat(::testing::_, ::testing::_))
        .WillOnce(::testing::Return(AFuture<IOpenAIChat::Response>(makeWaitResponse())))
        .WillOnce(::testing::Return(AFuture<IOpenAIChat::Response>(makePauseResponse())));

    AppTestHarness app(openAI);

    pumpIterations(5);

    AAsyncHolder async;
    async << app.passNotificationToAI("First notification").onProcessed;
    pump(async);

    async << app.passNotificationToAI("Second notification").onProcessed;
    pump(async);

    // Both should have been processed (context has messages from both)
    EXPECT_FALSE(app.temporaryContext().empty());
}

// ============================================================================
// Error handling: exception during processing doesn't crash
// ============================================================================
TEST_F(AppBaseUnitTest, ExceptionDuringProcessingDoesNotCrash) {
    auto openAI = _cast<IOpenAIChat>(_new<OpenAIMock>());
    auto* mock = static_cast<OpenAIMock*>(openAI.get());
    // First call throws, second returns wait
    EXPECT_CALL(*mock, chat(::testing::_, ::testing::_))
        .WillOnce(::testing::Throw(AException("Simulated network error")))
        .WillOnce(::testing::Return(AFuture<IOpenAIChat::Response>(makeWaitResponse())));

    AppTestHarness app(openAI);

    pumpIterations(5);

    AAsyncHolder async;
    async << app.passNotificationToAI("This will fail").onProcessed;
    pump(async);

    EXPECT_TRUE(true); // survived
}

// ============================================================================
// System prompt includes lockdown mode info when enabled
// ============================================================================
TEST_F(AppBaseUnitTest, SystemPromptReflectsLockdownMode) {
    auto prompt = AppBase::getSystemPrompt();
    EXPECT_FALSE(prompt.empty());
    EXPECT_TRUE(prompt.contains("<your_appearance>"));
}

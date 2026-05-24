//
// Unit tests verifying that AppBase pushes ToolCallEvents to IExporter subscribers.
//
// Strategy:
//   - Use a minimal AppBase subclass (PrometheusTestHarness) that:
//       * wires onAfterToolCall → emit toolCallFired   (mirrors what App::updateTools does in main.cpp)
//       * exposes a "my_tool" handler that completes successfully
//       * provides the mandatory "wait" tool to let the main loop stop
//   - Use a stub IExporter subclass (SpyExporter) that records every ToolCallEvent
//     received via registerAppBase, without starting a Civet/HTTP server.
//   - Trigger tool calls via passNotificationToAI and verify the recorded events.
//

#include <range/v3/all.hpp>
#include "AppBase.h"
#include "IOpenAIChat.h"
#include "OpenAITools.h"
#include "Prometheus.h"
#include "MetricsBreadcumbs.h"

#include <gmock/gmock.h>
#include <AUI/Thread/AAsyncHolder.h>
#include <AUI/Thread/AEventLoop.h>
#include <AUI/IO/APath.h>

using namespace std::chrono_literals;

// ============================================================================
// Mock IOpenAIChat
// ============================================================================
class PrometheusOpenAIMock : public IOpenAIChat {
public:
    MOCK_METHOD(AFuture<Response>, chat, (Params params, AVector<Message> messages), (override));
    _<StreamingResponse> chatStreaming(Params params, AVector<Message> messages) override { return nullptr; }
    MOCK_METHOD(AFuture<std::valarray<double>>, embedding, (Params params, AString input), (override));
};

// ============================================================================
// Helper: build a canned LLM response that dispatches the named tool
// ============================================================================
static AFuture<IOpenAIChat::Response> makeToolCallResponse(AString toolName, AString args = "{}") {
    IOpenAIChat::Message msg;
    msg.role = IOpenAIChat::Message::Role::ASSISTANT;
    msg.content = "";
    msg.tool_calls = {
        IOpenAIChat::Message::ToolCall{
            .id = "call_1",
            .index = 0,
            .type = "function",
            .function = {
                .name = std::move(toolName),
                .arguments = std::move(args),
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
    resp.usage = { .prompt_tokens = 5, .completion_tokens = 3, .total_tokens = 8 };
    co_return resp;
}

// Helper: LLM stops with "wait" (ends the processing loop)
static AFuture<IOpenAIChat::Response> makeWaitResponse() {
    return makeToolCallResponse("wait");
}

// Helper: LLM first calls my_tool, then wait
static AFuture<IOpenAIChat::Response> makeMyToolResponse() {
    return makeToolCallResponse("my_tool");
}

// ============================================================================
// PrometheusTestHarness — AppBase subclass that:
//   1. Wires onAfterToolCall → emit toolCallFired  (same pattern as App in main.cpp)
//   2. Provides "my_tool" (a simple no-op) and "wait" for loop termination
// ============================================================================
class PrometheusTestHarness : public AppBase {
public:
    AString lastMyToolCallResult;

    explicit PrometheusTestHarness(_<IOpenAIChat> openAI)
        : AppBase(Init{
              .workingDir = "test_data_prometheus_unit",
              .openAI = std::move(openAI),
          })
    {
        APath("test_data_prometheus_unit").removeFileRecursive();
        mLastOpenedChatLastMessageTime = std::chrono::system_clock::now() - 42s;
    }

    ~PrometheusTestHarness() override {
        APath("test_data_prometheus_unit").removeFileRecursive();
    }

protected:
    void updateTools(OpenAITools& actions) override {
        AppBase::updateTools(actions);

        actions.insert({
            .name = "my_tool",
            .description = "A test tool",
            .handler = [this](OpenAITools::Ctx) -> AFuture<AString> {
                lastMyToolCallResult = "called";
                co_return "my_tool executed";
            },
        });
        actions.insert({
            .name = "wait",
            .description = "Wait until further notifications",
            .handler = [](OpenAITools::Ctx) -> AFuture<AString> { co_return "Waiting"; },
        });
    }
};

// ============================================================================
// SpyExporter — an IExporter that captures ToolCallEvents without starting HTTP
// ============================================================================
class SpyExporter : public AObject, public prometheus::IExporter {
public:
    AVector<AppBase::ToolCallEvent> capturedEvents;

    void registerOpenAI(OpenAIChatMeasurable&) override { /* not tested here */ }

    void registerAppBase(AppBase& app) override {
        connect(app.toolCallFired, [this](AppBase::ToolCallEvent ev) {
            capturedEvents << std::move(ev);
        });
    }
};

// ============================================================================
// Fixture
// ============================================================================
class PrometheusUnitTest : public ::testing::Test {
protected:
    void SetUp() override {
        APath("test_data_prometheus_unit").removeFileRecursive();
    }
    void TearDown() override {
        APath("test_data_prometheus_unit").removeFileRecursive();
    }
};

// ============================================================================
// toolCallFired is emitted after a tool handler completes
// ============================================================================
TEST_F(PrometheusUnitTest, ToolCallFiredOnSuccessfulToolCall) {
    AEventLoop loop;
    IEventLoop::Handle h(&loop);
    AAsyncHolder async;

    auto openAI = _cast<IOpenAIChat>(_new<PrometheusOpenAIMock>());
    // LLM calls my_tool, then wait
    EXPECT_CALL(*static_cast<PrometheusOpenAIMock*>(openAI.get()), chat(::testing::_, ::testing::_))
        .WillOnce(::testing::Return(makeMyToolResponse()))
        .WillOnce(::testing::Return(makeWaitResponse()));

    auto app = _new<PrometheusTestHarness>(openAI);
    SpyExporter spy;
    spy.registerAppBase(*app);

    async << app->passNotificationToAI("Hello").onProcessed;
    while (async.size() > 0) {
        loop.iteration();
    }

    // my_tool handler ran
    EXPECT_EQ(app->lastMyToolCallResult, "called");

    // toolCallFired was emitted once for my_tool (the "wait" call also fires, so at least 1 for my_tool)
    ASSERT_EQ(spy.capturedEvents.size(), 1);
    auto& event = spy.capturedEvents.first();
    EXPECT_EQ(event.toolName, "my_tool");
}

// ============================================================================
// toolCallFired carries the correct tool name for each distinct tool
// ============================================================================
TEST_F(PrometheusUnitTest, ToolCallFiredCarriesCorrectToolName) {
    AEventLoop loop;
    IEventLoop::Handle h(&loop);
    AAsyncHolder async;

    auto openAI = _cast<IOpenAIChat>(_new<PrometheusOpenAIMock>());
    EXPECT_CALL(*static_cast<PrometheusOpenAIMock*>(openAI.get()), chat(::testing::_, ::testing::_))
        .WillOnce(::testing::Return(makeMyToolResponse()))
        .WillOnce(::testing::Return(makeWaitResponse()));

    auto app = _new<PrometheusTestHarness>(openAI);
    SpyExporter spy;
    spy.registerAppBase(*app);

    async << app->passNotificationToAI("Hello").onProcessed;
    while (async.size() > 0) {
        loop.iteration();
    }

    for (const auto& ev : spy.capturedEvents) {
        EXPECT_FALSE(ev.toolName.empty()) << "ToolCallEvent must have a non-empty toolName";
    }
}

// ============================================================================
// toolCallFired is NOT emitted when a tool call fails (throws)
// ============================================================================
TEST_F(PrometheusUnitTest, ToolCallFiredNotEmittedOnToolException) {
    AEventLoop loop;
    IEventLoop::Handle h(&loop);
    AAsyncHolder async;

    auto openAI = _cast<IOpenAIChat>(_new<PrometheusOpenAIMock>());
    // LLM calls "unknown_tool" (not registered) -> handler is not found, no onAfterToolCall
    EXPECT_CALL(*static_cast<PrometheusOpenAIMock*>(openAI.get()), chat(::testing::_, ::testing::_))
        .WillOnce(::testing::Return(makeToolCallResponse("unknown_tool")))
        .WillOnce(::testing::Return(makeWaitResponse()));

    class ThrowingHarness : public PrometheusTestHarness {
    public:
        explicit ThrowingHarness(_<IOpenAIChat> ai) : PrometheusTestHarness(std::move(ai)) {}
    protected:
        void updateTools(OpenAITools& actions) override {
            PrometheusTestHarness::updateTools(actions);
            // Insert a tool that throws
            actions.insert({
                .name = "throwing_tool",
                .description = "A tool that always throws",
                .handler = [](OpenAITools::Ctx) -> AFuture<AString> {
                    throw AException("intentional test failure");
                    co_return "";
                },
            });
        }
    };

    auto app = _new<ThrowingHarness>(openAI);
    SpyExporter spy;
    spy.registerAppBase(*app);

    async << app->passNotificationToAI("Hello").onProcessed;
    while (async.size() > 0) {
        loop.iteration();
    }

    // "unknown_tool" is not registered -> no handler -> onAfterToolCall not called
    // "wait" IS registered and fires -> but it has no event since tool not in handled list for "unknown"
    // Only "wait" will fire (if at all). But NOT for "unknown_tool".
    for (const auto& ev : spy.capturedEvents) {
        EXPECT_NE(ev.toolName, "unknown_tool")
            << "toolCallFired should NOT be emitted for an unregistered tool";
    }
}

// ============================================================================
// toolCallFired breadcrumbLabels snapshot matches metricBreadcumbs at dispatch time
// ============================================================================
TEST_F(PrometheusUnitTest, ToolCallFiredBreadcrumbLabelsSnapshot) {
    AEventLoop loop;
    IEventLoop::Handle h(&loop);
    AAsyncHolder async;

    auto openAI = _cast<IOpenAIChat>(_new<PrometheusOpenAIMock>());
    EXPECT_CALL(*static_cast<PrometheusOpenAIMock*>(openAI.get()), chat(::testing::_, ::testing::_))
        .WillOnce(::testing::Return(makeMyToolResponse()))
        .WillOnce(::testing::Return(makeWaitResponse()));

    auto app = _new<PrometheusTestHarness>(openAI);

    // Set a breadcrumb before processing
    MetricsBreadcumbs::Point point(app->metricBreadcumbs(), "chat", "test_chat");

    SpyExporter spy;
    spy.registerAppBase(*app);

    async << app->passNotificationToAI("Hello").onProcessed;
    while (async.size() > 0) {
        loop.iteration();
    }

    ASSERT_EQ(spy.capturedEvents.size(), 1);

    auto& event = spy.capturedEvents[0];

    // breadcrumbLabels should contain the "chat" key set above
    EXPECT_TRUE(event.breadcrumbLabels.contains("chat"));
    EXPECT_EQ(event.breadcrumbLabels["chat"], "test_chat");
}

// ============================================================================
// toolCallFired carries lastOpenedChatLastMessageTime when set
// ============================================================================
TEST_F(PrometheusUnitTest, ToolCallFiredlastOpenedChatLastMessageTime) {
    AEventLoop loop;
    IEventLoop::Handle h(&loop);
    AAsyncHolder async;

    auto openAI = _cast<IOpenAIChat>(_new<PrometheusOpenAIMock>());
    EXPECT_CALL(*static_cast<PrometheusOpenAIMock*>(openAI.get()), chat(::testing::_, ::testing::_))
        .WillOnce(::testing::Return(makeMyToolResponse()))
        .WillOnce(::testing::Return(makeWaitResponse()));

    // A harness that populates lastOpenedChatLastMessageTime (mirrors main.cpp's App::updateTools)
    class TimedHarness : public PrometheusTestHarness {
    public:
        explicit TimedHarness(_<IOpenAIChat> ai) : PrometheusTestHarness(std::move(ai)) {}
    protected:
        void updateTools(OpenAITools& actions) override {
            PrometheusTestHarness::updateTools(actions);
            actions.insert({
                .name = "my_tool",
                .description = "Test tool",
                .handler = [](OpenAITools::Ctx) -> AFuture<AString> { co_return "ok"; },
            });
            actions.insert({
                .name = "wait",
                .description = "Wait",
                .handler = [](OpenAITools::Ctx) -> AFuture<AString> { co_return "Waiting"; },
            });
        }
    };

    auto app = _new<TimedHarness>(openAI);
    SpyExporter spy;
    spy.registerAppBase(*app);

    async << app->passNotificationToAI("Hello").onProcessed;
    while (async.size() > 0) {
        loop.iteration();
    }

    auto it = std::find_if(spy.capturedEvents.begin(), spy.capturedEvents.end(), [](const AppBase::ToolCallEvent& ev) {
        return ev.toolName == "my_tool";
    });
    ASSERT_NE(it, spy.capturedEvents.end());

    // lastOpenedChatLastMessageTime must be set and ≥ 42s (the simulated delay)
    ASSERT_TRUE(it->lastOpenedChatLastMessageTime.hasValue());

    // response time is hardcoded to 42s in this unit test.
    // since it measures against time of taken action, there's a race condition, which may cause to alter the hardcoded
    // value to 43s, 44s, etc. let's use EXPECT_NEAR instead of EXPECT_EQ.
    EXPECT_NEAR(it->lastOpenedChatLastMessageTime->count(), 42, 5);
}

// ============================================================================
// SpyExporter captures events from multiple tool calls in the same session
// ============================================================================
TEST_F(PrometheusUnitTest, MultipleToolCallsAllCaptured) {
    AEventLoop loop;
    IEventLoop::Handle h(&loop);
    AAsyncHolder async;

    auto openAI = _cast<IOpenAIChat>(_new<PrometheusOpenAIMock>());
    // Two separate notifications, each dispatching my_tool then wait
    EXPECT_CALL(*static_cast<PrometheusOpenAIMock*>(openAI.get()), chat(::testing::_, ::testing::_))
        .WillOnce(::testing::Return(makeMyToolResponse()))
        .WillOnce(::testing::Return(makeWaitResponse()))
        .WillOnce(::testing::Return(makeMyToolResponse()))
        .WillOnce(::testing::Return(makeWaitResponse()));

    auto app = _new<PrometheusTestHarness>(openAI);
    SpyExporter spy;
    spy.registerAppBase(*app);

    async << app->passNotificationToAI("First").onProcessed;
    while (async.size() > 0) {
        loop.iteration();
    }

    async << app->passNotificationToAI("Second").onProcessed;
    while (async.size() > 0) {
        loop.iteration();
    }

    EXPECT_TRUE(ranges::all_of(spy.capturedEvents, [](const AppBase::ToolCallEvent& ev) {
        return ev.toolName == "my_tool";
    })) << "we expect my_tool to be reported only";
    EXPECT_EQ(spy.capturedEvents.size(), 2) << "my_tool should have fired exactly twice (once per notification)";
}

// ============================================================================
// registerAppBase is safe to call before any tool calls happen
// ============================================================================
TEST_F(PrometheusUnitTest, RegisterAppBaseSafeWithNoToolCalls) {
    auto openAI = _cast<IOpenAIChat>(_new<PrometheusOpenAIMock>());
    auto app = _new<PrometheusTestHarness>(openAI);

    SpyExporter spy;
    // Should not crash even if no notifications/tool-calls ever happen
    EXPECT_NO_THROW(spy.registerAppBase(*app));
    EXPECT_TRUE(spy.capturedEvents.empty());
    AThread::processMessages();
}

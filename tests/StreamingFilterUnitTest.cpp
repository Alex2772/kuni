#include <range/v3/view/drop_last.hpp>
#include "proxy_server/streaming_filter.h"
#include "util/openai_streaming.h"

#include <gmock/gmock.h>
#include <string>
#include <vector>

using namespace proxy_server;
using namespace testing;

// ── Helpers ──────────────────────────────────────────────────────────────────

struct FilterResult {
    std::vector<std::string> passedThrough;
    std::vector<IOpenAIChat::Message::ToolCall> toolCalls;

    auto collectPassedThrough() {
        // we don't really care about amount and chunking of synthetic SSE. the only stuff we care is arguments were
        // passed properly. so we accumulate these lines delta by delta and check the results.
        AVector<IOpenAIChat::Response::Choice> accumulator;
        for (const auto& line : passedThrough | ranges::views::drop_last(1)) {
            auto str = line.substr(line.find(": ") + 2);
            aui::from_json<util::openai_streaming::StreamingChunk>(AJson::fromString(str)).collectTo(accumulator);
        }
        return accumulator;
    }
};

static FilterResult run(StreamingFilter& filter, std::vector<std::string_view> lines) {
    FilterResult result;
    for (auto line : lines) {
        filter.processLine(
            line,
            [&](std::string_view sv) { result.passedThrough.emplace_back(sv); },
            [&](const IOpenAIChat::Message::ToolCall& tc) { result.toolCalls.push_back(tc); });
    }
    return result;
}

// JSON-escape a raw string so it can be embedded inside a JSON string value.
static std::string jsonEscape(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:   out += c;      break;
        }
    }
    return out;
}

// Build a minimal "data: {...}" SSE line for a streaming chunk.
// delta fields are optional; only non-empty strings are included.
// tcArgs should be the raw argument string (will be JSON-escaped automatically).
static std::string makeChunk(int choiceIndex,
                              std::string_view content       = {},
                              std::string_view reasoning     = {},
                              std::string_view finishReason  = {},
                              // tool_call fields (index -1 means no tool_call)
                              int    tcIndex    = -1,
                              std::string_view tcId       = {},
                              std::string_view tcName     = {},
                              std::string_view tcArgs     = {}) {
    std::string delta = R"({"role":"assistant")";
    if (!content.empty())   delta += R"(,"content":")" + std::string(content) + '"';
    if (!reasoning.empty()) delta += R"(,"reasoning":")" + std::string(reasoning) + '"';

    if (tcIndex >= 0) {
        delta += R"(,"tool_calls":[{"index":)" + std::to_string(tcIndex);
        if (!tcId.empty())   delta += R"(,"id":")" + std::string(tcId) + '"';
        if (!tcName.empty()) delta += R"(,"function":{"name":")" + std::string(tcName) + R"(","arguments":")" + jsonEscape(tcArgs) + R"("}})";
        else if (!tcArgs.empty()) delta += R"(,"function":{"arguments":")" + jsonEscape(tcArgs) + R"("}})";
        else delta += '}';
        delta += ']';
    }
    delta += '}';

    std::string choice = R"({"index":)" + std::to_string(choiceIndex) + R"(,"delta":)" + delta;
    if (!finishReason.empty()) choice += R"(,"finish_reason":")" + std::string(finishReason) + '"';
    choice += '}';

    return R"(data: {"id":"x","object":"chat.completion.chunk","created":0,"model":"m","choices":[)" + choice + "]}";
}

// ── Tests ─────────────────────────────────────────────────────────────────────

// Plain content chunks (no tool calls) must pass straight through.
TEST(StreamingFilter, PlainContentPassesThrough) {
    StreamingFilter filter({"ask"});
    auto result = run(filter, {
        makeChunk(0, "Hello"),
        makeChunk(0, " world", {}, "stop"),
        "data: [DONE]",
    });
    EXPECT_EQ(result.toolCalls.size(), 0u);
    ASSERT_EQ(result.passedThrough.size(), 3u);
    EXPECT_THAT(result.passedThrough[0], HasSubstr(R"("content":"Hello")"));
    EXPECT_EQ(result.passedThrough[2], "data: [DONE]");
}

// SSE comment lines (": OPENROUTER PROCESSING") must pass through unchanged.
TEST(StreamingFilter, SseCommentPassesThrough) {
    StreamingFilter filter({"ask"});
    auto result = run(filter, {": OPENROUTER PROCESSING"});
    ASSERT_EQ(result.passedThrough.size(), 1u);
    EXPECT_EQ(result.passedThrough[0], ": OPENROUTER PROCESSING");
}

// A tool call to a NON-injected tool must pass through as-is.
TEST(StreamingFilter, NonInjectedToolPassesThrough) {
    StreamingFilter filter({"ask"});   // "search" is NOT injected
    auto result = run(filter, {
        makeChunk(0, {}, {}, {}, 0, "call_1", "search", ""),
        makeChunk(0, {}, {}, {}, 0, {},        {},       R"({"q":"test"})"),
        makeChunk(0, {}, {}, "tool_calls"),
        "data: [DONE]",
    });
    EXPECT_EQ(result.toolCalls.size(), 0u);
    ASSERT_FALSE(result.passedThrough.empty());

    // we expect the last line to be data: [DONE]
    EXPECT_EQ(result.passedThrough.back(), "data: [DONE]");
    auto accumulator = result.collectPassedThrough();
    ASSERT_EQ(accumulator.size(), 1u);
    EXPECT_EQ(accumulator[0].index, 0);
    EXPECT_EQ(accumulator[0].finish_reason, "tool_calls");
    EXPECT_EQ(accumulator[0].message.content, "");
    EXPECT_EQ(accumulator[0].message.reasoning, "");
    EXPECT_EQ(accumulator[0].message.role, IOpenAIChat::Message::Role::ASSISTANT);
    ASSERT_EQ(accumulator[0].message.tool_calls.size(), 1u);
    EXPECT_EQ(accumulator[0].message.tool_calls[0].index, 0);
    EXPECT_EQ(accumulator[0].message.tool_calls[0].id, "call_1");
    EXPECT_EQ(accumulator[0].message.tool_calls[0].function.name, "search");
    EXPECT_EQ(accumulator[0].message.tool_calls[0].function.arguments, R"({"q":"test"})");
}

// A tool call to a NON-injected tool must pass through as-is.
TEST(StreamingFilter, NonInjectedToolPassesThrough2) {
    StreamingFilter filter({"ask"});   // "search" is NOT injected
    auto result = run(filter, {
        makeChunk(0, {}, {}, {}, 0, "call_0", "ask", ""),
        makeChunk(0, {}, {}, {}, 1, "call_1", "search", ""),
        makeChunk(0, {}, {}, {}, 1, {},        {},       R"({"q":"test"})"),
        makeChunk(0, {}, {}, "tool_calls"),
        "data: [DONE]",
    });
    ASSERT_EQ(result.toolCalls.size(), 1u);
    EXPECT_EQ(result.toolCalls[0].function.name, "ask");
    ASSERT_FALSE(result.passedThrough.empty());

    // we expect the last line to be data: [DONE]
    EXPECT_EQ(result.passedThrough.back(), "data: [DONE]");

    auto accumulator = result.collectPassedThrough();
    ASSERT_EQ(accumulator.size(), 1u);
    EXPECT_EQ(accumulator[0].index, 0); // important: the client should see index 0 because we silently stolen "ask" from response
    EXPECT_EQ(accumulator[0].finish_reason, "tool_calls");
    EXPECT_EQ(accumulator[0].message.content, "");
    EXPECT_EQ(accumulator[0].message.reasoning, "");
    EXPECT_EQ(accumulator[0].message.role, IOpenAIChat::Message::Role::ASSISTANT);
    ASSERT_EQ(accumulator[0].message.tool_calls.size(), 1u);
    EXPECT_EQ(accumulator[0].message.tool_calls[0].index, 0);
    EXPECT_EQ(accumulator[0].message.tool_calls[0].id, "call_1");
    EXPECT_EQ(accumulator[0].message.tool_calls[0].function.name, "search");
    EXPECT_EQ(accumulator[0].message.tool_calls[0].function.arguments, R"({"q":"test"})");
}

// A tool call to an injected tool must be intercepted (NOT passed through),
// and the handler must be called exactly once with the complete accumulated call.
// also we expect "finish_reason": "tool_calls" frame to be filtered out.
TEST(StreamingFilter, InjectedToolIsIntercepted) {
    StreamingFilter filter({"ask"});
    // Simulate the real streaming sequence from the log:
    // 1. Chunk with name="ask" and empty arguments
    // 2. Several argument fragments
    // 3. finish_reason="tool_calls"
    auto result = run(filter, {
        makeChunk(0, {}, {}, {}, 0, "call_abc", "ask", ""),
        makeChunk(0, {}, {}, {}, 0, {}, {}, R"({"query": "Hello)"),
        makeChunk(0, {}, {}, {}, 0, {}, {}, R"(, this is a test})"),
        makeChunk(0, {}, {}, "tool_calls", 0),
        "data: [DONE]",
    });
    ASSERT_EQ(result.passedThrough.size(), 2);
    EXPECT_EQ(result.passedThrough[0], R"(data: {"id":"x","object":"chat.completion.chunk","created":0,"model":"m","choices":[{"index":0,"delta":{"role":"assistant"}}]})");
    EXPECT_EQ(result.passedThrough[1], "data: [DONE]");

    ASSERT_EQ(result.toolCalls.size(), 1u);
    EXPECT_THAT(std::string(result.toolCalls[0].function.arguments), HasSubstr("Hello"));
}

// When ALL tool calls are injected but the client already received
// content/reasoning, They should NOT receive "finish_reason": "stop".
// Sending "tool_calls" would confuse clients into expecting tool-call
// results that never come.
TEST(StreamingFilter, FinishReasonToolCallsDoesNotLeak) {
    StreamingFilter filter({"ask"});
    auto result = run(filter, {
        makeChunk(0, {}, "Let me think..."),           // reasoning — passes through
        makeChunk(0, {}, {}, {}, 0, "call_1", "ask", ""),
        makeChunk(0, {}, {}, {}, 0, {}, {}, R"({"q":"hi"})"),
        makeChunk(0, {}, {}, "tool_calls", 0),
        "data: [DONE]",
    });
    // Tool call must be intercepted.
    ASSERT_EQ(result.toolCalls.size(), 1u);
    EXPECT_EQ(result.toolCalls[0].function.name, "ask");

    // reasoning chunk
    auto passedThrough = result.collectPassedThrough();
    ASSERT_EQ(passedThrough.size(), 1u);
    EXPECT_EQ(passedThrough[0].message.reasoning, "Let me think...") << "Reasoning should have had passed";
    EXPECT_EQ(static_cast<AString&>(passedThrough[0].finish_reason), "") << "tools_calls should have had filtered out";
}

TEST(StreamingFilter, InterceptedToolCallCallsExactlyOnce) {
    StreamingFilter filter({"ask"});
    auto result = run(filter, {
        makeChunk(0, {}, "Let me think..."),           // reasoning — passes through
        makeChunk(0, {}, {}, {}, 0, "call_1", "ask", ""),
        makeChunk(0, {}, {}, {}, 0, {}, {}, R"({"q":"hi"})"),
        makeChunk(0, {}, {}, "tool_calls", 0),
        makeChunk(0, {}, {}, "tool_calls", 0),
        "data: [DONE]",
    });
    // Tool call must be intercepted.
    ASSERT_EQ(result.toolCalls.size(), 1u);
    EXPECT_EQ(result.toolCalls[0].function.name, "ask");
}


// Reasoning/content chunks that arrive BEFORE a tool-call chunk must pass
// through; only the tool-call chunks should be intercepted.
TEST(StreamingFilter, ReasoningBeforeToolCallPassesThrough) {
    StreamingFilter filter({"ask"});
    auto result = run(filter, {
        makeChunk(0, {}, "The user wants me to call ask"),
        makeChunk(0, {}, "More reasoning"),
        // Now the tool call starts
        makeChunk(0, {}, {}, {}, 0, "call_1", "ask", ""),
        makeChunk(0, {}, {}, {}, 0, {}, {}, R"({"q":"hi"})"),
        makeChunk(0, {}, {}, "tool_calls", 0),
        "data: [DONE]",
    });
    // The first two reasoning chunks must have been flushed through.
    EXPECT_THAT(result.passedThrough, Contains(HasSubstr("The user wants me to call ask")));
    EXPECT_THAT(result.passedThrough, Contains(HasSubstr("More reasoning")));

    // we don't expect tool_calls.
    EXPECT_THAT(result.passedThrough, Not(Contains(HasSubstr("tool_calls"))));

    // the last frame should have been flushed.
    EXPECT_THAT(result.passedThrough, Contains("data: [DONE]"));

    // Tool call chunks must NOT be forwarded.
    for (const auto& line : result.passedThrough) {
        EXPECT_THAT(line, Not(HasSubstr(R"("name":"ask")")));
    }
    ASSERT_EQ(result.passedThrough.size(), 4u);
    EXPECT_EQ(result.toolCalls[0].function.name, "ask");
}


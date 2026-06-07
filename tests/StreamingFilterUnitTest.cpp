#include "proxy_server/streaming_filter.h"
#include <gmock/gmock.h>
#include <string>
#include <vector>

using namespace proxy_server;
using namespace testing;

// ── Helpers ──────────────────────────────────────────────────────────────────

struct FilterResult {
    std::vector<std::string> passedThrough;
    std::vector<IOpenAIChat::Message::ToolCall> toolCalls;
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
    EXPECT_TRUE(result.toolCalls.empty());
}

// A tool call to a NON-injected tool must pass through as-is.
TEST(StreamingFilter, NonInjectedToolPassesThrough) {
    StreamingFilter filter({"ask"});   // "search" is NOT injected
    auto result = run(filter, {
        makeChunk(0, {}, {}, {}, 0, "call_1", "search", ""),
        makeChunk(0, {}, {}, {}, 0, {},        {},       R"({"q":"test"})"),
        makeChunk(0, {}, {}, "tool_calls", 0),
    });
    EXPECT_EQ(result.toolCalls.size(), 0u);
    EXPECT_EQ(result.passedThrough.size(), 3u);
}

// A tool call to an injected tool must be intercepted (NOT passed through),
// and the handler must be called exactly once with the complete accumulated call.
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
    });
    EXPECT_EQ(result.passedThrough.size(), 0u);
    ASSERT_EQ(result.toolCalls.size(), 1u);
    EXPECT_EQ(result.toolCalls[0].function.name, "ask");
    EXPECT_EQ(result.toolCalls[0].id, "call_abc");
    EXPECT_THAT(std::string(result.toolCalls[0].function.arguments), HasSubstr("Hello"));
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
    EXPECT_THAT(result.passedThrough, Contains("data: [DONE]"));
    // Tool call chunks must NOT be forwarded.
    for (const auto& line : result.passedThrough) {
        EXPECT_THAT(line, Not(HasSubstr(R"("name":"ask")")));
    }
    ASSERT_EQ(result.toolCalls.size(), 1u);
    EXPECT_EQ(result.toolCalls[0].function.name, "ask");
}

// When there are two choices and one has an injected tool call while the other
// has plain content, the plain-content choice passes through and the tool call
// choice is intercepted.
TEST(StreamingFilter, TwoChoicesMixedInterception) {
    StreamingFilter filter({"ask"});
    auto result = run(filter, {
        makeChunk(0, "hi"),                                              // choice 0: content
        makeChunk(1, {}, {}, {}, 0, "call_2", "ask", ""),              // choice 1: injected tool
        makeChunk(1, {}, {}, {}, 0, {}, {}, R"({"q":"test"})"),
        makeChunk(0, " there", {}, "stop"),                              // choice 0 finishes
        makeChunk(1, {}, {}, "tool_calls", 0),                          // choice 1 finishes
    });
    // choice 0 lines must pass through
    EXPECT_THAT(result.passedThrough, Contains(HasSubstr(R"("content":"hi")")));
    // choice 1 tool call must be intercepted
    ASSERT_EQ(result.toolCalls.size(), 1u);
    EXPECT_EQ(result.toolCalls[0].function.name, "ask");
}

// Arguments arrive before the function name (edge case: index chunk without
// name, then name arrives in next chunk) — must buffer until name is known.
TEST(StreamingFilter, BuffersUntilToolNameArrives) {
    StreamingFilter filter({"ask"});

    // First chunk: tool_call with index but no name yet
    std::string chunkNoName = R"(data: {"id":"x","object":"chat.completion.chunk","created":0,"model":"m","choices":[{"index":0,"delta":{"role":"assistant","tool_calls":[{"index":0,"function":{"arguments":""}}]}}]})";
    // Second chunk: name arrives
    std::string chunkWithName = R"(data: {"id":"x","object":"chat.completion.chunk","created":0,"model":"m","choices":[{"index":0,"delta":{"role":"assistant","tool_calls":[{"index":0,"id":"call_x","function":{"name":"ask","arguments":""}}]}}]})";
    std::string chunkArgs = makeChunk(0, {}, {}, {}, 0, {}, {}, R"({"q":"hello"})");
    std::string chunkDone = makeChunk(0, {}, {}, "tool_calls", 0);

    auto result = run(filter, {chunkNoName, chunkWithName, chunkArgs, chunkDone});
    EXPECT_EQ(result.passedThrough.size(), 0u);
    ASSERT_EQ(result.toolCalls.size(), 1u);
    EXPECT_EQ(result.toolCalls[0].function.name, "ask");
}

// Reproduce the exact real-world sequence from the debug log (condensed).
TEST(StreamingFilter, RealWorldSequenceFromLog) {
    StreamingFilter filter({"ask"});

    // Reasoning chunks
    std::vector<std::string_view> lines;
    lines.push_back(": OPENROUTER PROCESSING");
    auto r1 = makeChunk(0, {}, "The user wants me to");
    lines.push_back(r1);
    auto r2 = makeChunk(0, {}, " try calling the ask tool");
    lines.push_back(r2);

    // Tool call starts — name in first chunk, arguments in subsequent chunks
    auto tc1 = R"(data: {"id":"gen-x","object":"chat.completion.chunk","created":0,"model":"m","choices":[{"index":0,"delta":{"content":null,"role":"assistant","tool_calls":[{"index":0,"id":"call_bbc0740c61de436a97603323","type":"function","function":{"name":"ask","arguments":""}}]},"finish_reason":null}]})";
    lines.push_back(tc1);

    auto args1 = R"(data: {"id":"gen-x","object":"chat.completion.chunk","created":0,"model":"m","choices":[{"index":0,"delta":{"content":null,"role":"assistant","tool_calls":[{"index":0,"function":{"arguments":"{\"query\": \"Hello, this is a test query\""}}]},"finish_reason":null}]})";
    lines.push_back(args1);

    auto args2 = R"(data: {"id":"gen-x","object":"chat.completion.chunk","created":0,"model":"m","choices":[{"index":0,"delta":{"content":null,"role":"assistant","tool_calls":[{"index":0,"function":{"arguments":"}"}}]},"finish_reason":null}]})";
    lines.push_back(args2);

    auto finish = R"(data: {"id":"gen-x","object":"chat.completion.chunk","created":0,"model":"m","choices":[{"index":0,"delta":{"content":"","role":"assistant","reasoning":null},"finish_reason":"tool_calls"}]})";
    lines.push_back(finish);

    lines.push_back("data: [DONE]");

    auto result = run(filter, lines);

    // Reasoning and comment must pass through
    EXPECT_THAT(result.passedThrough, Contains(": OPENROUTER PROCESSING"));
    EXPECT_THAT(result.passedThrough, Contains(HasSubstr("The user wants me to")));
    EXPECT_THAT(result.passedThrough, Contains("data: [DONE]"));

    // Tool call chunks must NOT pass through
    for (const auto& line : result.passedThrough) {
        EXPECT_THAT(line, Not(HasSubstr(R"("name":"ask")")));
    }

    // Handler must be called once with the full accumulated call
    ASSERT_EQ(result.toolCalls.size(), 1u);
    EXPECT_EQ(result.toolCalls[0].function.name, "ask");
    EXPECT_EQ(result.toolCalls[0].id, "call_bbc0740c61de436a97603323");
    EXPECT_THAT(std::string(result.toolCalls[0].function.arguments), HasSubstr("Hello, this is a test query"));
}

// Empty input produces no output.
TEST(StreamingFilter, EmptyInput) {
    StreamingFilter filter({"ask"});
    auto result = run(filter, {});
    EXPECT_TRUE(result.passedThrough.empty());
    EXPECT_TRUE(result.toolCalls.empty());
}

// Malformed JSON passes through unchanged.
TEST(StreamingFilter, MalformedJsonPassesThrough) {
    StreamingFilter filter({"ask"});
    auto result = run(filter, {"data: {not valid json"});
    ASSERT_EQ(result.passedThrough.size(), 1u);
    EXPECT_EQ(result.passedThrough[0], "data: {not valid json");
}

#pragma once

//
// Created by alex2772 on 6/7/26.
//
// Stateful filter that processes SSE lines from an upstream LLM and decides
// whether each line should be forwarded to the downstream client as-is, or
// whether it belongs to a tool call that is handled locally (injected tools).
//
// Usage:
//   StreamingFilter filter({"ask", "search"});
//   // for each SSE line (without the trailing \n\n):
//   filter.processLine(line, writeLine, handleToolCall);
//

#include <functional>
#include <string>
#include <string_view>

#include "AUI/Common/AString.h"
#include "AUI/Common/ASet.h"
#include "IOpenAIChat.h"

namespace proxy_server {

/**
 * @brief Incremental SSE line filter for the proxy server.
 *
 * Accumulates streaming chunks, detects tool_calls in completed choices, and
 * routes them either to the injected-tool handler or back to the upstream
 * client unchanged.
 */
class StreamingFilter {
public:
    /**
     * @param injectedToolNames  Set of tool names handled locally by the proxy.
     *                           Comparisons are case-sensitive.
     */
    explicit StreamingFilter(ASet<AString> injectedToolNames);

    /**
     * @brief Process one SSE event line (the payload, WITHOUT the trailing \n\n).
     *
     * @param line           The raw SSE line, e.g. "data: {...}" or ": comment".
     * @param passThrough    Called with lines that should be forwarded to the client
     *                       (including the trailing \n\n that was stripped by lineByLine).
     * @param handleToolCall Called once per accumulated tool-call that belongs to an
     *                       injected tool.  The accumulator for that choice is reset
     *                       afterwards so its chunks are NOT forwarded.
     *                       Signature: void(const IOpenAIChat::Message::ToolCall&)
     */
    void processLine(
        std::string_view line,
        const std::function<void(std::string_view)>& passThrough,
        const std::function<void(const IOpenAIChat::Message::ToolCall&)>& handleToolCall);

private:
    ASet<AString> mInjectedToolNames;

    // Per-choice accumulator for streaming delta messages.
    struct ChoiceState {
        IOpenAIChat::Message accumulated;
        bool intercepted = false;   // true once we determined this choice has an injected tool call
    };
    AVector<ChoiceState> mChoices;

    // Raw SSE lines accumulated while we are not sure if a choice will be intercepted.
    // Flushed to passThrough if interception does not apply.
    AVector<std::string> mPendingLines;
};

}   // namespace proxy_server

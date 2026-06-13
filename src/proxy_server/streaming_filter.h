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
     * @param filterToolNames  Set of tool names handled locally by the proxy.
     *                           Comparisons are case-sensitive.
     */
    explicit StreamingFilter(ASet<AString> filterToolNames);

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

    [[nodiscard]] const AVector<IOpenAIChat::Response::Choice>& choices() const & { return mChoices; }
    [[nodiscard]] AVector<IOpenAIChat::Response::Choice> choices() && { return std::exchange(mChoices, {}); }

private:
    // Tool names whose we shouldn't passthrough.
    ASet<AString> mFilteredToolNames;
    AVector<IOpenAIChat::Response::Choice> mChoices;

    // Raw SSE lines accumulated while we are not sure if a choice will be intercepted.
    // Flushed to passThrough if interception does not apply.
    AVector<std::string> mPendingLines;

    // True once any content/reasoning chunk has been forwarded to the client before
    // the tool-call phase started.  If true and all tool calls are injected, we must
    // emit a synthetic finish_reason chunk so the client knows the stream ended.
    bool mHasPassedThroughContent = false;

    // True once injected tool calls have been dispatched to handleToolCall.
    // Prevents double-dispatch if the upstream sends duplicate finish_reason chunks.
    bool mDispatched = false;
};

}   // namespace proxy_server

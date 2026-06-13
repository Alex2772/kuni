//
// Created by alex2772 on 6/7/26.
//

#include "streaming_filter.h"

#include "AUI/Json/Conversion.h"
#include "AUI/Logging/ALogger.h"
#include "util/openai_streaming.h"

#include <range/v3/algorithm/any_of.hpp>

static constexpr auto LOG_TAG = "streaming_filter";

namespace proxy_server {

StreamingFilter::StreamingFilter(ASet<AString> filteredToolNames) : mFilteredToolNames(std::move(filteredToolNames)) {}

void StreamingFilter::processLine(
    std::string_view line,
    const std::function<void(std::string_view)>& passThrough,
    const std::function<void(const IOpenAIChat::Message::ToolCall&)>& handleToolCall) {
    auto delim = line.find(": ");
    if (delim == std::string_view::npos) {
        // Not a key-value SSE line (e.g. ": OPENROUTER PROCESSING") — pass through.
        passThrough(line);
        return;
    }

    auto command = line.substr(0, delim);
    auto value = line.substr(delim + 2);

    if (command != "data") {
        passThrough(line);
        return;
    }

    if (value == "[DONE]") {
        passThrough(line);
        return;
    }

    if (value.empty() || value[0] != '{') {
        passThrough(line);
        return;
    }

    util::openai_streaming::StreamingChunk chunk;
    try {
        auto json = AJson::fromBuffer(AByteBufferView(value.data(), value.size()));
        chunk = aui::from_json<util::openai_streaming::StreamingChunk>(json);
    } catch (const AException& e) {
        ALOG_DEBUG(LOG_TAG) << "streaming_filter: JSON parse error, passing through: " << e;
        passThrough(line);
        return;
    }

    chunk.collectTo(mChoices);

    const bool anyUnresolved = ranges::any_of(chunk.choices, [&](const util::openai_streaming::StreamingChunk::Choice& choice) {
        auto& state = mChoices[choice.index];
        return ranges::any_of(state.message.tool_calls, [&](const IOpenAIChat::Message::ToolCall& toolCall) {
            return toolCall.function.name.empty();
        });
    });

    const bool hasToolCalls = ranges::any_of(chunk.choices, [&](const util::openai_streaming::StreamingChunk::Choice& choice) {
        auto& state = mChoices[choice.index];
        return !state.message.tool_calls.empty();
    });

    if (!hasToolCalls) {
        // No tool calls involved — flush any pending lines and pass this one through.
        for (auto& pending : mPendingLines) {
            passThrough(pending);
        }
        mPendingLines.clear();
        mHasPassedThroughContent = true;
        passThrough(line);
        return;
    }

    if (anyUnresolved) {
        // Buffer this line until we know the tool name.
        mPendingLines.emplace_back(line);
        return;
    }

    // All tool calls in the current chunk have names resolved. Determine if any are injected.
    const bool anyInjected = ranges::any_of(chunk.choices, [&](const util::openai_streaming::StreamingChunk::Choice& c) {
        auto& state = mChoices[c.index];
        return ranges::any_of(state.message.tool_calls, [&](const IOpenAIChat::Message::ToolCall& tc) {
            return mFilteredToolNames.contains(tc.function.name);
        });
    });

    if (anyInjected) {
        // Check whether finish_reason has arrived (i.e. accumulation is complete).
        const bool finished = ranges::any_of(mChoices, [](const IOpenAIChat::Response::Choice& c) {
            return !c.finish_reason.empty();
        });

        if (!finished) {
            // Keep buffering until finish_reason arrives.
            mPendingLines.emplace_back(line);
            return;
        }

        // finish_reason arrived — classify all tool calls and dispatch/reconstruct.
        // Guard against duplicate finish_reason chunks from upstream.
        if (mDispatched) {
            return;
        }
        mDispatched = true;

        AVector<IOpenAIChat::Message::ToolCall> nonInjectedToolCalls;
        for (auto& choice : mChoices) {
            for (auto& toolCall : choice.message.tool_calls) {
                if (mFilteredToolNames.contains(toolCall.function.name)) {
                    handleToolCall(toolCall);
                } else {
                    auto reindexed = toolCall;
                    reindexed.index = static_cast<int64_t>(nonInjectedToolCalls.size());
                    nonInjectedToolCalls.emplace_back(std::move(reindexed));
                }
            }
        }

        // Drop pending lines — they contain injected tool call chunks.
        mPendingLines.clear();

        // Always emit a synthetic SSE chunk so the client sees a proper finish.
        // If there are non-injected tool calls, emit them with finish_reason="tool_calls".
        // If all tool calls were injected, emit an empty delta with finish_reason="stop"
        // so the client knows the stream ended cleanly.
        AJson deltaJson = AJson::Object{{"role", AString("assistant")}};

        if (!nonInjectedToolCalls.empty()) {
            AJson::Array toolCallsJson;
            for (auto& tc : nonInjectedToolCalls) {
                toolCallsJson.push_back(AJson::Object{
                    {"index",    static_cast<int64_t>(tc.index)},
                    {"id",       tc.id},
                    {"type",     AString("function")},
                    {"function", AJson::Object{
                        {"name",      tc.function.name},
                        {"arguments", tc.function.arguments},
                    }},
                });
            }
            deltaJson["tool_calls"] = std::move(toolCallsJson);
        }

        AJson choiceJson = AJson::Object{
            {"index",         static_cast<int64_t>(0)},
            {"delta",         std::move(deltaJson)},
        };
        if (!nonInjectedToolCalls.empty()) {
            choiceJson["finish_reason"] = "tool_calls";
        }
        AJson responseJson = AJson::Object{
            {"id",      chunk.id},
            {"object",  AString("chat.completion.chunk")},
            {"created", chunk.created},
            {"model",   chunk.model},
            {"choices", AJson::Array{std::move(choiceJson)}},
        };
        auto reconstructed = "data: " + AJson::toString(responseJson);
        passThrough(reconstructed);
    } else {
        // No injected tool calls at all — flush pending lines and pass the current line through.
        for (auto& pending : mPendingLines) {
            passThrough(pending);
        }
        mPendingLines.clear();
        passThrough(line);
    }
}

}   // namespace proxy_server

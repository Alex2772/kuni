//
// Created by alex2772 on 6/7/26.
//

#include "streaming_filter.h"

#include "AUI/Json/Conversion.h"
#include "AUI/Logging/ALogger.h"
#include "util/openai_streaming.h"

static constexpr auto LOG_TAG = "streaming_filter";

namespace proxy_server {

StreamingFilter::StreamingFilter(ASet<AString> injectedToolNames) : mInjectedToolNames(std::move(injectedToolNames)) {}

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

    for (auto& choice : chunk.choices) {
        while (mChoices.size() <= static_cast<size_t>(choice.index)) {
            mChoices.emplace_back();
        }
        auto& state = mChoices[choice.index];

        state.accumulated += choice.delta;

        if (!state.intercepted) {
            for (const auto& tc : state.accumulated.tool_calls) {
                if (!tc.function.name.empty()) {
                    if (mInjectedToolNames.contains(tc.function.name)) {
                        state.intercepted = true;
                    }
                }
            }
        }
    }

    bool anyUnresolved = false;
    for (auto& choice : chunk.choices) {
        auto& state = mChoices[choice.index];
        if (state.intercepted)
            continue;
        for (const auto& tc : state.accumulated.tool_calls) {
            if (tc.function.name.empty()) {
                anyUnresolved = true;
            }
        }
    }

    bool hasToolCalls = false;
    for (auto& choice : chunk.choices) {
        if (!mChoices[choice.index].accumulated.tool_calls.empty()) {
            hasToolCalls = true;
            break;
        }
    }

    if (!hasToolCalls) {
        // No tool calls involved — flush any pending lines and pass this one through.
        for (auto& pending : mPendingLines) {
            passThrough(pending);
        }
        mPendingLines.clear();
        passThrough(line);
        return;
    }

    if (anyUnresolved) {
        // Buffer this line until we know the tool name.
        mPendingLines.emplace_back(line);
        return;
    }

    bool allIntercepted = true;
    for (auto& choice : chunk.choices) {
        if (!mChoices[choice.index].intercepted) {
            allIntercepted = false;
            break;
        }
    }

    if (allIntercepted) {
        mPendingLines.clear();
        for (auto& choice : chunk.choices) {
            auto& state = mChoices[choice.index];
            if (!choice.finish_reason.empty()) {
                for (const auto& tc : state.accumulated.tool_calls) {
                    handleToolCall(tc);
                }
            }
        }
    } else {
        for (auto& pending : mPendingLines) {
            passThrough(pending);
        }
        mPendingLines.clear();
        passThrough(line);
    }
}

}   // namespace proxy_server

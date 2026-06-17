#pragma once

#include "IOpenAIChat.h"

#include <functional>
#include "AUI/Util/AYieldGenerator.h"

namespace util::openai_streaming {

AYieldGenerator<std::string_view> lineByLine(std::function<size_t(char* dst, size_t size)> read);


struct StreamingChunk {
    IOpenAIChat::String id;
    IOpenAIChat::String object;
    IOpenAIChat::String model;
    IOpenAIChat::String system_fingerprint;
    int64_t created;
    struct Choice {
        int index{};
        IOpenAIChat::Message delta;
        IOpenAIChat::String finish_reason;
    };
    AVector<Choice> choices;
    IOpenAIChat::Response::Usage usage;

    void collectTo(AVector<IOpenAIChat::Response::Choice>& out) const {
        for (auto& choice : choices) {
            while (out.size() <= static_cast<size_t>(choice.index)) {
                const auto i = out.size();
                out.emplace_back().index = i;
            }
            out[choice.index].message += choice.delta;
            out[choice.index].message.role = IOpenAIChat::Message::Role::ASSISTANT;
            if (!choice.finish_reason.empty()) {
#if AUI_DEBUG
                // we don't really expect finish_reason to be segmented.
                // also, we don't expect the llm service to send multiple chunks with different finish_reason.
                AUI_ASSERT(out[choice.index].finish_reason.empty() || out[choice.index].finish_reason == choice.finish_reason);
#endif
                out[choice.index].finish_reason += choice.finish_reason;
            }
        }
    }
};

}

AJSON_FIELDS(util::openai_streaming::StreamingChunk,
    (id, "id", AJsonFieldFlags::OPTIONAL)
    (object, "object", AJsonFieldFlags::OPTIONAL)
    (model, "model", AJsonFieldFlags::OPTIONAL)
    (system_fingerprint, "system_fingerprint", AJsonFieldFlags::OPTIONAL)
    (choices, "choices", AJsonFieldFlags::OPTIONAL)
    (created, "created", AJsonFieldFlags::OPTIONAL)
    (usage, "usage", AJsonFieldFlags::OPTIONAL)
    )


AJSON_FIELDS(util::openai_streaming::StreamingChunk::Choice,
    (index, "index", AJsonFieldFlags::OPTIONAL)
    (delta, "delta", AJsonFieldFlags::OPTIONAL)
    (finish_reason, "finish_reason", AJsonFieldFlags::OPTIONAL)
    )


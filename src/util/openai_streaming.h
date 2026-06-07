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
};

}

AJSON_FIELDS(util::openai_streaming::StreamingChunk,
    (id, "id", AJsonFieldFlags::OPTIONAL)
    (object, "object", AJsonFieldFlags::OPTIONAL)
    (model, "model", AJsonFieldFlags::OPTIONAL)
    (system_fingerprint, "system_fingerprint", AJsonFieldFlags::OPTIONAL)
    (choices, "choices", AJsonFieldFlags::OPTIONAL)
    (created, "created", AJsonFieldFlags::OPTIONAL)
    )


AJSON_FIELDS(util::openai_streaming::StreamingChunk::Choice,
    (index, "index", AJsonFieldFlags::OPTIONAL)
    (delta, "delta", AJsonFieldFlags::OPTIONAL)
    (finish_reason, "finish_reason", AJsonFieldFlags::OPTIONAL)
    )


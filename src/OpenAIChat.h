#pragma once
#include "AUI/Image/AImage.h"
#include "AUI/Json/AJson.h"
#include "AUI/Thread/AFuture.h"
#include "AUI/Util/APreprocessor.h"

struct OpenAIChat {
    AString systemPrompt;
    AString baseUrl =  "http://" AUI_PP_STRINGIZE(OPENAICHAT_ADDRESS) "/";

    AJson tools = AJson::Array{};


    struct Message {
        enum class Role {
            ASSISTANT,
            SYSTEM_PROMPT,
            USER,
            TOOL,
          } role;
        AString content;
        using Attachment = std::variant<_<AImage>>;
        AVector<Attachment> attachments;
        AString tool_call_id;
        AString reasoning;
        struct ToolCall {
            AString id;
            int64_t index;
            AString type;
            struct Function {
                AString name;
                AString arguments;
            } function;
        };
        AVector<ToolCall> tool_calls;
    };

    struct Response {
        AString id;
        AString object;
        int64_t created;
        AString model;
        AString system_fingerprint;
        struct Choice {
            int64_t index;
            Message message;
            AString finish_reason;
        };
        AVector<Choice> choices;
        struct Usage {
            int64_t prompt_tokens;
            int64_t completion_tokens;
            int64_t total_tokens;
        } usage;
    };

    AFuture<Response> chat(AString message);
    AFuture<Response> chat(AVector<Message> messages);
};

template<>
struct AJsonConv<OpenAIChat::Message::Role> {
    static AJson toJson(OpenAIChat::Message::Role v) {
        switch (v) {
            case OpenAIChat::Message::Role::ASSISTANT: return "assistant";
            case OpenAIChat::Message::Role::USER: return "user";
            case OpenAIChat::Message::Role::SYSTEM_PROMPT: return "system";
            case OpenAIChat::Message::Role::TOOL: return "tool";
        }
        return "unknown";
    }

    static void fromJson(const AJson& json, OpenAIChat::Message::Role& out) {
        const auto& str = json.asString();
        if (str == "assistant") {
            out = OpenAIChat::Message::Role::ASSISTANT;
            return;
        }
        if (str == "user") {
            out = OpenAIChat::Message::Role::USER;
            return;
        }
        if (str == "system") {
            out = OpenAIChat::Message::Role::SYSTEM_PROMPT;
            return;
        }
        if (str == "tool") {
            out = OpenAIChat::Message::Role::TOOL;
            return;
        }
        throw AException("invalid role: " + str);
    }
};
#pragma once
#include <valarray>

#include "AUI/Image/AImage.h"
#include "AUI/Json/AJson.h"
#include "AUI/Thread/AFuture.h"
#include "AUI/Util/APreprocessor.h"
#include "config.h"
#include "AUI/Common/AProperty.h"

struct OpenAIChat {
    AString systemPrompt;
    int maxOutputTokens = 8192;
    EndpointAndModel config = ::config::ENDPOINT_MAIN;
    AOptional<int64_t> seed;

    AJson tools = AJson::Array{};

    static constexpr auto EMBEDDING_TAG = "kuni_embedding";
    static AString embedImage(AImageView image);

    struct String: AString {
        using AString::AString;
    };

    struct Message {
        enum class Role {
            ASSISTANT,
            SYSTEM_PROMPT,
            USER,
            TOOL,
          } role;
        String content;
        String tool_call_id;
        String reasoning;
        String reasoning_content; // deepseek requires this
        struct ToolCall {
            String id;
            int64_t index;
            String type;
            struct Function {
                String name;
                String arguments;
            } function;

            ToolCall& operator+=(const ToolCall& other) {
                id += other.id;
                index = other.index;
                type += other.type;
                function.name += other.function.name;
                function.arguments += other.function.arguments;
                return *this;
            }
        };
        AVector<ToolCall> tool_calls;

        Message& operator+=(const Message& other) {
            // for streaming
            role = other.role;
            content += other.content;
            tool_call_id = other.tool_call_id;
            reasoning += other.reasoning;
            reasoning_content += other.reasoning_content;

            for (const auto& toolCall : other.tool_calls) {
                while (tool_calls.size() <= toolCall.index) {
                    tool_calls.emplace_back();
                }
                tool_calls[toolCall.index] += toolCall;
            }
            return *this;
        }
    };

    struct Response {
        AString id;
        AString object;
        int64_t created;
        AString model;
        AOptional<AString> system_fingerprint;
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

    struct StreamingResponse {
        AProperty<Response> response;
        AFuture<> completed;
    };

    AFuture<Response> chat(AString message);
    AFuture<Response> chat(AVector<Message> messages);
    _<StreamingResponse> chatStreaming(AVector<Message> messages);

    AFuture<std::valarray<double>> embedding(AString input);

private:
    AJson makeQueryString(AVector<Message> messages);
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

template<>
struct AJsonConv<OpenAIChat::String> {
    static AJson toJson(const OpenAIChat::String& v) {
        return static_cast<const AString&>(v);
    }

    static void fromJson(const AJson& json, OpenAIChat::String& out) {
        if (json.isNull()) {
            out = {};
            return;
        }
        AJsonConv<AString>::fromJson(json, out);
    }
};

AJSON_FIELDS(OpenAIChat::Message::ToolCall::Function,
             (name, "name", AJsonFieldFlags::OPTIONAL)
             (arguments, "arguments", AJsonFieldFlags::OPTIONAL)
             )

AJSON_FIELDS(OpenAIChat::Message::ToolCall,
             (id, "id", AJsonFieldFlags::OPTIONAL)
             (type, "type", AJsonFieldFlags::OPTIONAL)
             (function, "function", AJsonFieldFlags::OPTIONAL)
             AJSON_FIELDS_ENTRY(index))

AJSON_FIELDS(OpenAIChat::Message,
             (role, "role", AJsonFieldFlags::OPTIONAL)
             (content, "content", AJsonFieldFlags::OPTIONAL)
             (reasoning, "reasoning", AJsonFieldFlags::OPTIONAL)
             (reasoning_content, "reasoning_content", AJsonFieldFlags::OPTIONAL)
             (tool_call_id, "tool_call_id", AJsonFieldFlags::OPTIONAL)(tool_calls, "tool_calls",
                                                                          AJsonFieldFlags::OPTIONAL))

AJSON_FIELDS(OpenAIChat::Response::Choice,
             AJSON_FIELDS_ENTRY(index) AJSON_FIELDS_ENTRY(message) AJSON_FIELDS_ENTRY(finish_reason))

AJSON_FIELDS(OpenAIChat::Response,
             AJSON_FIELDS_ENTRY(id) AJSON_FIELDS_ENTRY(object) AJSON_FIELDS_ENTRY(created) AJSON_FIELDS_ENTRY(model)
                 AJSON_FIELDS_ENTRY(system_fingerprint) AJSON_FIELDS_ENTRY(choices) AJSON_FIELDS_ENTRY(usage))

AJSON_FIELDS(OpenAIChat::Response::Usage,
             AJSON_FIELDS_ENTRY(prompt_tokens) AJSON_FIELDS_ENTRY(completion_tokens) AJSON_FIELDS_ENTRY(total_tokens)

)


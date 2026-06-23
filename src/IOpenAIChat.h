#pragma once
#include <valarray>

#include "AUI/Image/AImage.h"
#include "AUI/Json/AJson.h"
#include "AUI/Thread/AFuture.h"
#include "AUI/Util/APreprocessor.h"
#include "config.h"
#include "AUI/Common/AProperty.h"

/**
 * @brief Abstract interface for OpenAI-compatible chat API clients.
 */
struct IOpenAIChat {
    virtual ~IOpenAIChat() = default;

    /**
     * @brief Parameters for chat and embedding operations.
     *
     * This struct replaces the mutable fields that were previously stored
     * directly in IOpenAIChat, making the interface stateless.
     */
    struct Params {
        AString systemPrompt;
        int maxOutputTokens = 8192;
        EndpointAndModel config = ::config::ENDPOINT_MAIN;
        AOptional<int64_t> seed;
        AJson tools = AJson::Array{};
    };

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
            int64_t index{};
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
        AString provider;
        AOptional<AString> system_fingerprint;
        AOptional<double> cost;
        AJson prompt_tokens_details;
        AJson cost_details;
        struct Choice {
            int64_t index;
            Message message;
            String finish_reason;
        };
        AVector<Choice> choices;
        struct Usage {
            int64_t prompt_tokens{};
            int64_t completion_tokens{};
            int64_t total_tokens{};
            int64_t prompt_cache_hit_tokens{};
            int64_t prompt_cache_miss_tokens{};
        } usage{};
    };

    struct StreamingResponse {
        AProperty<Response> response = Response{};
        AFuture<> completed{};
    };

    struct Session: AVector<Message> {
    public:
        using AVector<Message>::AVector;
        AString sessionId = nextSessionId();

    private:
        static AString nextSessionId();
    };

    AFuture<Response> chat(Params params, IOpenAIChat::Session messages);
    virtual _<StreamingResponse> chatStreaming(Params params, IOpenAIChat::Session messages) = 0;
    virtual AFuture<std::valarray<double>> embedding(Params params, AString input) = 0;
};

template<>
struct AJsonConv<IOpenAIChat::Message::Role> {
    static AJson toJson(IOpenAIChat::Message::Role v) {
        switch (v) {
            case IOpenAIChat::Message::Role::ASSISTANT: return "assistant";
            case IOpenAIChat::Message::Role::USER: return "user";
            case IOpenAIChat::Message::Role::SYSTEM_PROMPT: return "system";
            case IOpenAIChat::Message::Role::TOOL: return "tool";
        }
        return "unknown";
    }

    static void fromJson(const AJson& json, IOpenAIChat::Message::Role& out) {
        const auto& str = json.asString();
        if (str == "assistant") {
            out = IOpenAIChat::Message::Role::ASSISTANT;
            return;
        }
        if (str == "user") {
            out = IOpenAIChat::Message::Role::USER;
            return;
        }
        if (str == "system") {
            out = IOpenAIChat::Message::Role::SYSTEM_PROMPT;
            return;
        }
        if (str == "tool") {
            out = IOpenAIChat::Message::Role::TOOL;
            return;
        }
        throw AException("invalid role: " + str);
    }
};

template<>
struct AJsonConv<IOpenAIChat::String> {
    static AJson toJson(const IOpenAIChat::String& v) {
        return static_cast<const AString&>(v);
    }

    static void fromJson(const AJson& json, IOpenAIChat::String& out) {
        if (json.isNull()) {
            out = {};
            return;
        }
        AJsonConv<AString>::fromJson(json, out);
    }
};

AJSON_FIELDS(IOpenAIChat::Message::ToolCall::Function,
             (name, "name", AJsonFieldFlags::OPTIONAL)
             (arguments, "arguments", AJsonFieldFlags::OPTIONAL)
             )

AJSON_FIELDS(IOpenAIChat::Message::ToolCall,
             (id, "id", AJsonFieldFlags::OPTIONAL)
             (type, "type", AJsonFieldFlags::OPTIONAL)
             (function, "function", AJsonFieldFlags::OPTIONAL)
             (index, "index", AJsonFieldFlags::OPTIONAL)
             )

AJSON_FIELDS(IOpenAIChat::Message,
             (role, "role", AJsonFieldFlags::OPTIONAL)
             (content, "content", AJsonFieldFlags::OPTIONAL)
             (reasoning, "reasoning", AJsonFieldFlags::OPTIONAL)
             (reasoning_content, "reasoning_content", AJsonFieldFlags::OPTIONAL)
             (tool_call_id, "tool_call_id", AJsonFieldFlags::OPTIONAL)(tool_calls, "tool_calls",
                                                                          AJsonFieldFlags::OPTIONAL))

AJSON_FIELDS(IOpenAIChat::Response::Choice,
             AJSON_FIELDS_ENTRY(index) AJSON_FIELDS_ENTRY(message) AJSON_FIELDS_ENTRY(finish_reason))

AJSON_FIELDS(IOpenAIChat::Response,
             AJSON_FIELDS_ENTRY(id)
             AJSON_FIELDS_ENTRY(object)
             AJSON_FIELDS_ENTRY(created)
             AJSON_FIELDS_ENTRY(model)
             (system_fingerprint, "system_fingerprint", AJsonFieldFlags::OPTIONAL)
             AJSON_FIELDS_ENTRY(choices)
             (usage, "usage", AJsonFieldFlags::OPTIONAL)
             (provider, "provider", AJsonFieldFlags::OPTIONAL)
             (cost, "cost", AJsonFieldFlags::OPTIONAL)
             (cost_details, "cost_details", AJsonFieldFlags::OPTIONAL)
             (prompt_tokens_details, "prompt_tokens_details", AJsonFieldFlags::OPTIONAL)
             )

template<>
struct AJsonConv<IOpenAIChat::Response::Usage> {
    static void fromJson(const AJson& v, IOpenAIChat::Response::Usage& dst);
    static AJson toJson(const IOpenAIChat::Response::Usage& from);
};

template<>
struct AJsonConv<IOpenAIChat::Session> {
    static AJson toJson(const IOpenAIChat::Session& v);
    static void fromJson(AJson json, IOpenAIChat::Session& dst);
};

template<>
struct AJsonConv<AJson> {
    static AJson toJson(const AJson& json) {
        return json;
    }
    static void fromJson(const AJson& from, AJson& dst) {
        dst = from;
    }
};


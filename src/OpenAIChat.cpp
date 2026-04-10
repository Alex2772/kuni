//
// Created by alex2772 on 2/27/26.
//

#include "OpenAIChat.h"

#include <chrono>
#include <optional>
#include <random>
#include <range/v3/algorithm/generate.hpp>
#include <range/v3/range/conversion.hpp>
#include <range/v3/view.hpp>

#include "AUI/Curl/ACurl.h"
#include "AUI/IO/AFileOutputStream.h"
#include "AUI/Image/jpg/JpgImageLoader.h"
#include "AUI/Json/Conversion.h"
#include "AUI/Logging/ALogger.h"
#include "AUI/Reflect/AEnumerate.h"
#include "AUI/Util/kAUI.h"
#include "config.h"

static constexpr auto LOG_TAG = "OpenAIChat";

using namespace std::chrono_literals;


AJSON_FIELDS(OpenAIChat::Message::ToolCall::Function, AJSON_FIELDS_ENTRY(name) AJSON_FIELDS_ENTRY(arguments))

AJSON_FIELDS(OpenAIChat::Message::ToolCall,
             AJSON_FIELDS_ENTRY(id) AJSON_FIELDS_ENTRY(index) AJSON_FIELDS_ENTRY(type) AJSON_FIELDS_ENTRY(function))

AJSON_FIELDS(OpenAIChat::Message,
             AJSON_FIELDS_ENTRY(role)
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

struct StreamingResponse {
    AString id;
    AString object;
    AString model;
    AString system_fingerprint;
    int64_t created;
    struct Choice {
        int index{};
        OpenAIChat::Message delta;
    };
    AVector<Choice> choices;
};

AJSON_FIELDS(StreamingResponse,
    AJSON_FIELDS_ENTRY(id)
    AJSON_FIELDS_ENTRY(object)
    AJSON_FIELDS_ENTRY(model)
    AJSON_FIELDS_ENTRY(system_fingerprint)
    AJSON_FIELDS_ENTRY(choices)
    AJSON_FIELDS_ENTRY(created)
    )


AJSON_FIELDS(StreamingResponse::Choice,
    AJSON_FIELDS_ENTRY(index)
    AJSON_FIELDS_ENTRY(delta)
    )

AFuture<OpenAIChat::Response> OpenAIChat::chat(AString message) {
    return chat({
        {Message::Role::USER, std::move(message)},
    });
}

template<>
struct AJsonConv<AVector<OpenAIChat::Message>> {
    static AJson toJson(const AVector<OpenAIChat::Message>& v) {
        AJson::Array result;
        for (const auto& message: v) {
            // reverse engineered from vscode copilot plugin
            if (message.content.contains("</{}>"_format(OpenAIChat::EMBEDDING_TAG))) {
                auto content = std::string_view(message.content);
                auto append = [&](const OpenAIChat::Message& msg) {
                    if (msg.content.empty()) {
                        return;
                    }
                    result << aui::to_json(msg);
                };
                for (;;) {
                    auto tagPos = content.find("<{}>"_format(OpenAIChat::EMBEDDING_TAG));
                    append(OpenAIChat::Message{
                        .role = message.role,
                        .content = content.substr(0, tagPos),
                    });
                    if (tagPos == std::string::npos) {
                        break;
                    }
                    content = content.substr(tagPos);
                    content = content.substr(content.find(">") + 1);
                    auto body = content.substr(0, content.find("</{}>"_format(OpenAIChat::EMBEDDING_TAG)));
                    append(OpenAIChat::Message{
                        .role = message.role,
                        .content = "<attachments>",
                    });
                    result << AJson::Object{
                        {"role", aui::to_json(message.role)},
                        {"content",
                         AJson::Array{
                             AJson::Object{{"type", "image_url"}, {"image_url", body}},
                         }},
                    };
                    append(OpenAIChat::Message{
                        .role = message.role,
                        .content = "</attachments>",
                    });
                    content = content.substr(body.length());
                    content = content.substr(content.find(">") + 1);
                }
                continue;
            }
            result << aui::to_json(message);
        }
        return result;
    }
};


AString OpenAIChat::embedImage(AImageView image) {
    AByteBuffer jpg;
    auto resized = image.resizedLinearDownscale({672, 672});
    JpgImageLoader::save(jpg, resized);
    // JpgImageLoader::save(AFileOutputStream("test.jpg"), resized);
    return "<{}>data:image/jpg;base64,{}</{}>"_format(EMBEDDING_TAG, jpg.toBase64String(), EMBEDDING_TAG);
}

AJson OpenAIChat::makeQueryString(AVector<OpenAIChat::Message> messages) {
    AJson json {
        {
          "messages",
          aui::to_json(messages),
        },
        { "max_tokens", maxTokens },   // hopefully helps with stuck prediction (infinite reasoning)
        { "stream", false },
        { "use_context", false },
        { "include_sources", true },
        { "model", config.model },
        { "tools", tools },
        { "temperature", config::TEMPERATURE },
    };
    if (seed) {
        json["seed"] = *seed;
    }
    return json;
}

AFuture<OpenAIChat::Response> OpenAIChat::chat(AVector<Message> messages) {
    messages.insert(messages.begin(), {Message::Role::SYSTEM_PROMPT, systemPrompt});
    AString query = AJson::toString(makeQueryString(messages));
    AFileOutputStream("last_query.json") << query.toStdString();
    const auto logsDir = APath("logs");
    logsDir.makeDirs();
    auto now = std::chrono::system_clock::now();
    AFileOutputStream(logsDir / "{}.0query.json"_format(now)) << query.toStdString();

    ALOG_TRACE(LOG_TAG) << "Query: " << query;
    AVector<AString> headers = {"Content-Type: application/json"};
    if (!config.endpoint.bearerKey.empty()) {
        headers << "Authorization: Bearer {}"_format(config.endpoint.bearerKey);
    }
    auto response = AJson::fromBuffer((co_await ACurl::Builder(config.endpoint.baseUrl + "chat/completions")
                                           .withMethod(ACurl::Method::HTTP_POST)
                                           .withTimeout(config::REQUEST_TIMEOUT)
                                           .withHeaders(std::move(headers))
                                           .withBody(query.toStdString())
                                           .runAsync())
                                          .body);
    if (response.contains("error")) {
        throw AException("Ollama error: " + AJson::toString(response["error"]));
    }
    AFileOutputStream("last_response.json") << response;
    AFileOutputStream(logsDir / "{}.1response.json"_format(now)) << response;
    ALOG_TRACE(LOG_TAG) << "Response: " << AJson::toString(response);
    auto responseResult = aui::from_json<Response>(response);
    if (!responseResult.choices.empty() && !ALogger::global().isTrace()) {
        ALOG_DEBUG(LOG_TAG) << "Response reasoning: " << responseResult.choices.at(0).message.reasoning_content << responseResult.choices.at(0).message.reasoning;
    }
    co_return responseResult;
}
_<OpenAIChat::StreamingResponse> OpenAIChat::chatStreaming(AVector<Message> messages) {
    messages.insert(messages.begin(), {Message::Role::SYSTEM_PROMPT, systemPrompt});
    AString query = [&] {
        auto json = makeQueryString(messages);
        json["stream"] = true;
        return AJson::toString(json);
    }();
    AFileOutputStream("last_query.json") << query.toStdString();
    const auto logsDir = APath("logs");
    logsDir.makeDirs();
    auto now = std::chrono::system_clock::now();
    AFileOutputStream(logsDir / "{}.0query.json"_format(now)) << query.toStdString();

    ALOG_TRACE(LOG_TAG) << "QueryStreaming: " << query;
    AVector<AString> headers = {"Content-Type: application/json"};
    if (!config.endpoint.bearerKey.empty()) {
        headers << "Authorization: Bearer {}"_format(config.endpoint.bearerKey);
    }
    auto result = _new<OpenAIChat::StreamingResponse>();
    auto processJson = [=, caller = AThread::current()](AJson json) {
        caller->enqueue([=, json = std::move(json)] {
            auto response = aui::from_json<::StreamingResponse>(json);
            auto out = result->response.writeScope();
            out->id = response.id;
            out->created = response.created;
            out->model = response.model;
            out->system_fingerprint = response.system_fingerprint;
            for (const auto& choice: response.choices) {
                while (out->choices.size() <= choice.index) {
                    out->choices.emplace_back().index = out->choices.size();
                }
                auto& choiceOut = out->choices.at(choice.index);
                choiceOut.message.role = choice.delta.role;
                choiceOut.message.content += choice.delta.content;
                choiceOut.message.reasoning+= choice.delta.reasoning;
                choiceOut.message.reasoning_content += choice.delta.reasoning_content;
            }
        });
    };

    result->completed = [&]() -> AFuture<> {
        co_await ACurl::Builder(config.endpoint.baseUrl + "chat/completions")
                                               .withMethod(ACurl::Method::HTTP_POST)
                                               .withTimeout(config::REQUEST_TIMEOUT)
                                               .withHeaders(std::move(headers))
                                               .withBody(query.toStdString())
                                               .withWriteCallback([=](AByteBufferView piece) -> size_t {
                                                   try {
                                                       AStringView asString(piece.toStdStringView());
                                                       ALOG_DEBUG(LOG_TAG) << "QueryStreaming piece " << asString;
                                                       if (asString.length() <= 6) {
                                                           return 0;
                                                       }
                                                       if (!asString.startsWith("data: ")) {
                                                           throw AException("Expected 'data:' prefix");
                                                       }
                                                       asString = asString.substr(6);
                                                       if (asString.startsWith("[DONE]")) {
                                                           return piece.size();
                                                       }
                                                       processJson(AJson::fromString(asString));

                                                       return piece.size();
                                                   } catch (const AEOFException&) {
                                                       return 0;
                                                   }

                                               })
                                               .runAsync();
    }();
    return result;
}

AFuture<std::valarray<double>> OpenAIChat::embedding(AString input) {
    AUI_ASSERT(!input.empty());
    AVector<AString> headers = {"Content-Type: application/json"};
    if (!config.endpoint.bearerKey.empty()) {
        headers << "Authorization: Bearer {}"_format(config.endpoint.bearerKey);
    }
    auto response = AJson::fromBuffer((co_await ACurl::Builder(config.endpoint.baseUrl + "embeddings")
                                           .withMethod(ACurl::Method::HTTP_POST)
                                           .withTimeout(config::REQUEST_TIMEOUT)
                                           .withHeaders(std::move(headers))
                                           .withBody(AJson::toString(AJson::Object{
                                               {"model", config.model},
                                               {"input", std::move(input)},
                                           }))
                                           .runAsync())
                                          .body);
    if (response.contains("error")) {
        throw AException("Ollama error: " + AJson::toString(response["error"]));
    }
    const auto& array = response["data"][0]["embedding"].asArray();

    std::valarray result(0.0, array.size());
    for (const auto& [i, v]: array | ranges::view::enumerate) {
        result[i] = v.asNumber();
    }
    co_return result;
}

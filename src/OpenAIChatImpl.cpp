//
// Created by alex2772 on 2/27/26.
//

#include "OpenAIChatImpl.h"

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
#include "AUI/IO/AByteBufferInputStream.h"
#include "AUI/Util/ATokenizer.h"

static constexpr auto LOG_TAG = "OpenAIChat";

using namespace std::chrono_literals;


struct StreamingResponse {
    AString id;
    AString object;
    AString model;
    AString system_fingerprint;
    int64_t created;
    struct Choice {
        int index{};
        IOpenAIChat::Message delta;
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

AString IOpenAIChat::embedImage(AImageView image) {
    ALOG_TRACE(LOG_TAG) << "embedImage";
    AByteBuffer jpg;
    auto resized = image.resizedLinearDownscale({672, 672});
    JpgImageLoader::save(jpg, resized);
    // JpgImageLoader::save(AFileOutputStream("test.jpg"), resized);
    return "<{}>data:image/jpg;base64,{}</{}>"_format(EMBEDDING_TAG, jpg.toBase64String(), EMBEDDING_TAG);
}


AJson OpenAIChatImpl::makeQueryString(Params params, AVector<IOpenAIChat::Message> messages) {
    ALOG_TRACE(LOG_TAG) << "makeQueryString";
    AJson json {
        {
          "messages",
          aui::to_json(messages),
        },
        { "max_tokens", params.maxOutputTokens },   // hopefully helps with stuck prediction (infinite reasoning)
        { "stream", false },
        { "use_context", false },
        { "include_sources", true },
        { "model", params.config.model },
        { "tools", params.tools },
    };

    if (config::TEMPERATURE) {
        json["temperature"] = *config::TEMPERATURE;
    }
    if (config::TOP_P) {
        json["top_p"] = *config::TOP_P;
    }
    if (config::TOP_K) {
        json["top_k"] = *config::TOP_K;
    }
    if (config::MIN_P) {
        json["min_p"] = *config::MIN_P;
    }
    if (config::PRESENCE_PENALTY) {
        json["presence_penalty"] = *config::PRESENCE_PENALTY;
    }
    if (config::REPETITION_PENALTY) {
        json["repeat_penalty"] = *config::REPETITION_PENALTY;
    }
    if (params.seed) {
        json["seed"] = *params.seed;
    }
    return json;
}

AFuture<IOpenAIChat::Response> OpenAIChatImpl::chat(Params params, AVector<Message> messages) const {
    messages.insert(messages.begin(), {Message::Role::SYSTEM_PROMPT, params.systemPrompt});
    AString query = AJson::toString(makeQueryString(params, messages));
    AFileOutputStream("last_query.json") << query.toStdString();
    const auto logsDir = APath("logs");
    logsDir.makeDirs();
    auto now = std::chrono::system_clock::now();
    AFileOutputStream(logsDir / "{}.0query.json"_format(now)) << query.toStdString();

    ALOG_TRACE(LOG_TAG) << "Query: " << query;
    AVector<AString> headers = {"Content-Type: application/json"};
    if (!params.config.endpoint.bearerKey.empty()) {
        headers << "Authorization: Bearer {}"_format(params.config.endpoint.bearerKey);
    }
    tryAgain:
    auto response = AJson::fromBuffer((co_await ACurl::Builder(params.config.endpoint.baseUrl + "chat/completions")
                                           .withMethod(ACurl::Method::HTTP_POST)
                                           .withTimeout(config::REQUEST_TIMEOUT)
                                           .withHeaders(headers)
                                           .withBody(query.toStdString())
                                           .runAsync())
                                          .body);
    if (response.contains("error")) {
        auto message = AJson::toString(response["error"]);
        if (message.contains("model failed to load, this may be due to resource limitations or an internal error")) {
            // if vram is VERY low, ollama even fails to unload the previous model.
            // in ollama_setup.sh, we set OLLAMA_KEEP_ALIVE=1m, so we will just wait the ollama to unload the model,
            // then try again.
            ALogger::warn(LOG_TAG) << "Ollama model failed to load, wait and retry...";
            co_await AThread::asyncSleep(1min / 2);
            goto tryAgain;
        }
        throw AException("Ollama error: " + message);
    }
    AFileOutputStream("last_response.json") << response;
    AFileOutputStream(logsDir / "{}.1response.json"_format(now)) << response;
    ALOG_DEBUG(LOG_TAG) << "Response: " << AJson::toString(response).replaceAll("\\n", "\n");
    auto responseResult = aui::from_json<Response>(response);
    // if (!responseResult.choices.empty() && !ALogger::global().isTrace()) {
    //     ALOG_DEBUG(LOG_TAG) << "Response reasoning: " << responseResult.choices.at(0).message.reasoning_content << responseResult.choices.at(0).message.reasoning;
    // }
    co_return responseResult;
}
_<IOpenAIChat::StreamingResponse> OpenAIChatImpl::chatStreaming(Params params, AVector<Message> messages) const {
    messages.insert(messages.begin(), {Message::Role::SYSTEM_PROMPT, params.systemPrompt});
    AString query = [&] {
        auto json = makeQueryString(params, messages);
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
    if (!params.config.endpoint.bearerKey.empty()) {
        headers << "Authorization: Bearer {}"_format(params.config.endpoint.bearerKey);
    }
    auto result = _new<IOpenAIChat::StreamingResponse>();

    result->completed = [&]() -> AFuture<> {
        const auto caller = AThread::current();
        auto processJson = [=](AJson json) {
            caller->enqueue([=, json = std::move(json)] {
                auto response = aui::from_json<::StreamingResponse>(json);
                auto out = result->response.writeScope();
                out->id = response.id;
                out->created = response.created;
                out->model = response.model;
                out->system_fingerprint = response.system_fingerprint;
                for (auto& choice: response.choices) {
                    choice.delta.role = Message::Role::ASSISTANT;
                    while (out->choices.size() <= choice.index) {
                        out->choices.emplace_back().index = out->choices.size();
                    }
                    out->choices.at(choice.index).message += choice.delta;
                }
            });
        };

        AByteBuffer jsonTempBuffer;
        auto parseBuffer = [&, processJson] {
            while (!jsonTempBuffer.empty()) {
                ATokenizer tokenizer(std::make_unique<AByteBufferInputStream>(jsonTempBuffer));
                AString command = tokenizer.readStringWhile([](char c) {
                    return c != '{';
                });
                if (command.startsWith("data: [DONE]")) {
                    break;
                }
                if (!command.startsWith("data:")) {
                    break;
                }
                auto json = AJson::fromBuffer(jsonTempBuffer.slice(command.bytes().length()));
                processJson(std::move(json));
                const auto end = AStringView(jsonTempBuffer.data(), jsonTempBuffer.size()).find("\n\n");
                const auto at = end == std::string::npos ? jsonTempBuffer.end() : jsonTempBuffer.begin() + end + 2;
                jsonTempBuffer.erase(jsonTempBuffer.begin(), at);
            }
        };
        co_await ACurl::Builder(params.config.endpoint.baseUrl + "chat/completions")
                                               .withMethod(ACurl::Method::HTTP_POST)
                                               .withTimeout(config::REQUEST_TIMEOUT)
                                               .withHeaders(std::move(headers))
                                               .withBody(query.toStdString())
                                               .withWriteCallback([&parseBuffer, &jsonTempBuffer](AByteBufferView buffer) -> size_t {
                                                   ALOG_DEBUG(LOG_TAG) << "QueryStreaming piece " << buffer.toStdStringView();
                                                   jsonTempBuffer << buffer;
                                                   try {
                                                       parseBuffer();
                                                   } catch (const AJsonException& e) {
                                                       // "unexpected" eof, parse later
                                                   }
                                                   return buffer.size();
                                               })
                                               .runAsync();
        // finalize
        parseBuffer();

        // ensure we delivered all events before finishing the coroutine.
#if AUI_DEBUG
        AUI_ASSERT(AThread::current() == caller);
#endif
        AThread::processMessages();
    }();
    return result;
}

AFuture<std::valarray<double>> OpenAIChatImpl::embedding(Params params, AString input) const {
    ALOG_TRACE(LOG_TAG) << "embedding";
    if (input.empty()) {
        input = " ";
    }
    AVector<AString> headers = {"Content-Type: application/json"};
    if (!params.config.endpoint.bearerKey.empty()) {
        headers << "Authorization: Bearer {}"_format(params.config.endpoint.bearerKey);
    }
    tryAgain:
    auto response = AJson::fromBuffer((co_await ACurl::Builder(params.config.endpoint.baseUrl + "embeddings")
                                           .withMethod(ACurl::Method::HTTP_POST)
                                           .withTimeout(config::REQUEST_TIMEOUT)
                                           .withHeaders(headers)
                                           .withBody(AJson::toString(AJson::Object{
                                               {"model", params.config.model},
                                               {"input", input},
                                           }))
                                           .runAsync())
                                          .body);
    if (response.contains("error")) {
        auto message = AJson::toString(response["error"]);
        if (message.contains("model failed to load, this may be due to resource limitations or an internal error")) {
            // if vram is VERY low, ollama even fails to unload the previous model.
            // in ollama_setup.sh, we set OLLAMA_KEEP_ALIVE=1m, so we will just wait the ollama to unload the model,
            // then try again.
            ALogger::warn(LOG_TAG) << "Ollama model failed to load, wait and retry...";
            co_await AThread::asyncSleep(1min / 2);
            goto tryAgain;
        }
        throw AException("Ollama error: {}"_format(message));
    }

    const auto& array = response["data"][0]["embedding"].asArray();

    std::valarray result(0.0, array.size());
    for (const auto& [i, v]: array | ranges::view::enumerate) {
        result[i] = v.asNumber();
    }
    co_return result;
}

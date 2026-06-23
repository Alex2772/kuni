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
#include "util/openai_streaming.h"
#include "util/tui_streaming.h"

static constexpr auto LOG_TAG = "OpenAIChat";

using namespace std::chrono_literals;



AString IOpenAIChat::embedImage(AImageView image) {
    ALOG_TRACE(LOG_TAG) << "embedImage";
    AByteBuffer jpg;
    auto resized = image.resizedLinearDownscale({672, 672});
    JpgImageLoader::save(jpg, resized);
    // JpgImageLoader::save(AFileOutputStream("test.jpg"), resized);
    return "<{}>data:image/jpg;base64,{}</{}>"_format(EMBEDDING_TAG, jpg.toBase64String(), EMBEDDING_TAG);
}


AJson OpenAIChatImpl::makeQueryString(Params params, const IOpenAIChat::Session& messages) {
    ALOG_TRACE(LOG_TAG) << "makeQueryString";
    AUI_ASSERT(!messages.sessionId.empty());
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
        { "session_id", messages.sessionId },
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

AFuture<AJson> OpenAIChatImpl::makeHttpRequest(Endpoint endpoint, std::string query, std::string_view sessionId) {
    ALOG_TRACE(LOG_TAG) << "Query: " << query;
    AVector<AString> headers = {"Content-Type: application/json", "x-session-id: {}"_format(sessionId) };
    if (!endpoint.bearerKey.empty()) {
        headers << "Authorization: Bearer {}"_format(endpoint.bearerKey);
    }

    tryAgain:
    auto response = AJson::fromBuffer((co_await ACurl::Builder(endpoint.baseUrl + "chat/completions")
                                           .withMethod(ACurl::Method::HTTP_POST)
                                           .withTimeout(config::REQUEST_TIMEOUT)
                                           .withHeaders(headers)
                                           .withBody(query)
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
    co_return response;
}


_<IOpenAIChat::StreamingResponse> OpenAIChatImpl::chatStreaming(Params params, IOpenAIChat::Session messages) {
    messages.insert(messages.begin(), {Message::Role::SYSTEM_PROMPT, params.systemPrompt});
    AString query = [&] {
        auto json = makeQueryString(params, messages);
        json["stream"] = true;
        return AJson::toString(json);
    }();
    AFileOutputStream("last_query.json") << query.toStdString();
    static const auto logsDir = APath("logs");
    logsDir.makeDirs();
    const auto now = std::chrono::system_clock::now();
    AFileOutputStream(logsDir / "{}.0query.json"_format(now)) << query.toStdString();

    ALOG_TRACE(LOG_TAG) << "QueryStreaming: " << query;
    auto result = _new<IOpenAIChat::StreamingResponse>();

    // Subscribe to response changes and print incrementally to the TUI.
    auto printer = std::make_shared<TuiStreamingPrinter>();
    AObject::connect(result->response.changed, AObject::GENERIC_OBSERVER,
                     [printer, weak = result.weak()] {
                         if (auto s = weak.lock()) {
                             printer->update(*s->response);
                         }
                     });

    result->completed =
        [](AString query, _<StreamingResponse> result, Params params, _<TuiStreamingPrinter> printer, std::chrono::system_clock::time_point now, AString sessionId)
        -> AFuture<> {
        const auto caller = AThread::current();
        auto processJson = [=](AJson json) {
            caller->enqueue([=, json = std::move(json)] {
                auto chunk = aui::from_json<util::openai_streaming::StreamingChunk>(json);
                auto out = result->response.writeScope();
                out->id = chunk.id;
                out->created = chunk.created;
                out->model = chunk.model;
                out->system_fingerprint = chunk.system_fingerprint;
                out->usage = chunk.usage;
                chunk.collectTo(out->choices);
            });
        };

        AByteBuffer jsonTempBuffer;
        auto parseBuffer = [&, processJson] {
            while (!jsonTempBuffer.empty()) {
                ATokenizer tokenizer(std::make_unique<AByteBufferInputStream>(jsonTempBuffer));
                AString command = tokenizer.readStringWhile([](char c) {
                    return c != '{' && c != '\n';
                });
                if (command.startsWith("data: [DONE]")) {
                    break;
                }
                AUI_DEFER {
                    const auto end = AStringView(jsonTempBuffer.data(), jsonTempBuffer.size()).find("\n\n");
                    const auto at = end == std::string::npos ? jsonTempBuffer.end() : jsonTempBuffer.begin() + end + 2;
                    jsonTempBuffer.erase(jsonTempBuffer.begin(), at);
                };

                if (!command.startsWith("data:")) {
                    continue;
                }

                auto json = AJson::fromBuffer(jsonTempBuffer.slice(command.bytes().length()));
                processJson(std::move(json));

            }
        };

        AUI_ASSERT(!sessionId.empty());
        AVector<AString> headers = {"Content-Type: application/json", "x-session-id: {}"_format(sessionId) };
        if (!params.config.endpoint.bearerKey.empty()) {
            headers << "Authorization: Bearer {}"_format(params.config.endpoint.bearerKey);
        }
        auto httpResponse = co_await ACurl::Builder(params.config.endpoint.baseUrl + "chat/completions")
                                               .withMethod(ACurl::Method::HTTP_POST)
                                               .withTimeout(config::REQUEST_TIMEOUT)
                                               .withHeaders(std::move(headers))
                                               .withBody(query.toStdString())
                                               .withWriteCallback([&parseBuffer, &jsonTempBuffer](AByteBufferView buffer) -> size_t {
                                                   ALOG_TRACE(LOG_TAG) << "QueryStreaming piece " << buffer.toStdStringView();
                                                   jsonTempBuffer << buffer;
                                                   try {
                                                       parseBuffer();
                                                   } catch (const AJsonException& e) {
                                                       // "unexpected" eof, parse later
                                                   }
                                                   return buffer.size();
                                               })
                                               .runAsync();
        if (httpResponse.code != ACurl::ResponseCode::HTTP_200_OK) {
            ALogger::warn(LOG_TAG) << "chatStreaming: status=" << httpResponse.code;
        }
        // finalize
        parseBuffer();

        // ensure we delivered all events before finishing the coroutine.
#if AUI_DEBUG
        AUI_ASSERT(AThread::current() == caller);
#endif
        AThread::processMessages();
        printer->finish();
        if (auto promptTokensDetails = result->response->prompt_tokens_details.asObjectOpt()) {
            if (auto cacheWriteTokens = (*promptTokensDetails)["cache_write_tokens"].asLongIntOpt()) {
                if (*cacheWriteTokens == 0) {
                    ALogger::warn(LOG_TAG) << "Response states that no tokens were written to cache! Provider: \"" << result->response->provider << "\"";
                }
            }
        }
        AFileOutputStream(logsDir / "{}.1response.json"_format(now)) << AJson::toString(aui::to_json(*result->response));
    }(std::move(query), result, std::move(params), std::move(printer), now, messages.sessionId);
    return result;
}

AFuture<std::valarray<double>> OpenAIChatImpl::embedding(Params params, AString input) {
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

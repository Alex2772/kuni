#include "OpenAISpeechClient.h"
#include "config.h"
#include "AUI/Curl/ACurl.h"
#include "AUI/Json/Conversion.h"
#include "AUI/Logging/ALogger.h"

static constexpr auto LOG_TAG = "OpenAISpeechClient";

AJSON_FIELDS(OpenAISpeechClient::TextToSpeechRequest,
             AJSON_FIELDS_ENTRY(input)
             AJSON_FIELDS_ENTRY(model)
             AJSON_FIELDS_ENTRY(voice)
             AJSON_FIELDS_ENTRY(response_format)
             AJSON_FIELDS_ENTRY(speed))

AFuture<OpenAISpeechClient::TextToSpeechResponse>
OpenAISpeechClient::textToSpeech(const TextToSpeechRequest& request) {
    ALOG_TRACE(LOG_TAG) << "textToSpeech";
    if (baseUrl.empty()) {
        throw AException("OpenAI Speech endpoint URL not configured");
    }

    auto requestBody = AJson::toString(aui::to_json(request));
    ALOG_TRACE(LOG_TAG) << "Request body: " << requestBody;

    AString apiUrl = "{}audio/speech"_format(baseUrl);

    AVector<AString> headers = {
        "Content-Type: application/json",
    };
    if (!apiKey.empty()) {
        headers << "Authorization: Bearer {}"_format(apiKey);
    }

    auto response =
        (co_await ACurl::Builder(apiUrl)
            .withMethod(ACurl::Method::HTTP_POST)
            .withHeaders(std::move(headers))
            .withBody(requestBody.toStdString())
            .withTimeout(Config::REQUEST_TIMEOUT)
            .runAsync());
    if (response.code != ACurl::ResponseCode::HTTP_200_OK) {
        throw AException("OpenAISpeechClient failed: {}"_format(AString::fromUtf8(response.body)));
    }

    co_return TextToSpeechResponse{ .audioData = std::move(response.body) };
}

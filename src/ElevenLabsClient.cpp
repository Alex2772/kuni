#include "ElevenLabsClient.h"
#include "AUI/Curl/ACurl.h"
#include "AUI/Json/Conversion.h"
#include "AUI/Logging/ALogger.h"
#include "config.h"

static constexpr auto LOG_TAG = "ElevenLabsClient";

AJSON_FIELDS(ElevenLabsClient::VoiceSettings,
             AJSON_FIELDS_ENTRY(speed))

AJSON_FIELDS(ElevenLabsClient::TextToSpeechRequest,
             AJSON_FIELDS_ENTRY(text)
             AJSON_FIELDS_ENTRY(model_id)
             AJSON_FIELDS_ENTRY(language_code)
             AJSON_FIELDS_ENTRY(voice_settings))

AFuture<ElevenLabsClient::TextToSpeechResponse>
ElevenLabsClient::textToSpeech(const TextToSpeechRequest& request) {
    if (baseUrl.empty()) {
        throw AException("ElevenLabs endpoint URL not configured");
    }
    if (apiKey.empty()) {
        throw AException("ElevenLabs API key not configured");
    }
    if (voiceId.empty()) {
        throw AException("ElevenLabs voice ID not configured");
    }

    auto requestBody = AJson::toString(aui::to_json(request));
    ALOG_TRACE(LOG_TAG) << "Request body: " << requestBody;

    AString apiUrl =
        "{}v1/text-to-speech/{}?output_format=mp3_44100_128"_format(baseUrl, voiceId);

    AVector<AString> headers = {
        "Content-Type: application/json",
        "xi-api-key: {}"_format(apiKey)
    };

    auto responseBody =
        (co_await ACurl::Builder(apiUrl)
            .withMethod(ACurl::Method::HTTP_POST)
            .withHeaders(std::move(headers))
            .withBody(requestBody.toStdString())
            .withTimeout(config::REQUEST_TIMEOUT)
            .runAsync())
        .body;

    TextToSpeechResponse response;
    response.audioData = responseBody;

    co_return response;
}
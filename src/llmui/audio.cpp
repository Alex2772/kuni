//
// Created by alex2772 on 5/9/26.
//

#include "audio.h"
#include "config.h"
#include "AUI/Curl/ACurl.h"
#include "AUI/IO/AFileInputStream.h"
#include "AUI/Json/AJson.h"

static constexpr auto LOG_TAG = "llmui";


AFuture<AString> llmui::voiceMessage(AStringView pathToVoice) {
    ALOG_TRACE(LOG_TAG) << "voiceMessage pathToVoice=" << pathToVoice;
    try {
        AJson payload;
        payload["model"] = config().llmAudioToText.model;

        AFileInputStream stream(pathToVoice);
        AByteBuffer audio = AByteBuffer::fromStream(stream);
        AJson inputAudio;
        inputAudio["data"] = audio.toBase64String();
        inputAudio["format"] = "ogg";
        payload["input_audio"] = inputAudio;

        AVector<AString> headers = {
            "Authorization: Bearer {}"_format(config().llmAudioToText.endpoint.bearerKey),
            "Content-Type: application/json"
        };

        auto response =
            co_await ACurl::Builder(config().llmAudioToText.endpoint.baseUrl + "audio/transcriptions")
                .withMethod(ACurl::Method::HTTP_POST)
                .withBody(AJson::toString(payload))
                .withHeaders(std::move(headers))
                .withTimeout(Config::REQUEST_TIMEOUT)
                .runAsync();

        AJson responseJson = AJson::fromBuffer(response.body);
        co_return "<voice>\n" + AJson::toString(responseJson["text"]) + "\n</voice>";
    } catch (const AException& e) {
        ALogger::err(LOG_TAG) << "Can't transcribe audio" << e;
        co_return "";
    }
}

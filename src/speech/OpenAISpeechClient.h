#pragma once
#include "AUI/Common/AString.h"
#include "AUI/Common/AByteBuffer.h"
#include "AUI/Thread/AFuture.h"

struct OpenAISpeechClient {
    AString baseUrl = "https://api.openai.com/v1/";
    AString apiKey;
    AString model = "tts-1";
    AString voice = "alloy";

    struct TextToSpeechRequest {
        AString input;
        AString model;
        AString voice;
        AString response_format = "mp3";
        double speed = 1.0;
    };

    struct TextToSpeechResponse {
        AByteBuffer audioData;
    };

    AFuture<TextToSpeechResponse> textToSpeech(const TextToSpeechRequest& request);
};

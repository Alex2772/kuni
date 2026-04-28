#pragma once
#include "config.h"
#include "AUI/Common/AString.h"
#include "AUI/Common/AVector.h"
#include "AUI/Common/AByteBuffer.h"
#include "AUI/Thread/AFuture.h"
#include "Endpoint.h"

struct ElevenLabsClient {
    AString baseUrl = "https://api.elevenlabs.io/";
    AString apiKey;  // ElevenLabs API key
    AString voiceId;  // Default voice ID to use

    struct VoiceSettings {
        double speed = 1.0;
    };

    struct TextToSpeechRequest {
        AString text;
        AString model_id = "eleven_multilingual_v2";
        AString language_code = "en";  // ISO 639-1 language code (e.g., "en", "ru", "es")
        VoiceSettings voice_settings;
    };

    struct TextToSpeechResponse {
        AByteBuffer audioData;
    };

    AFuture<TextToSpeechResponse> textToSpeech(const TextToSpeechRequest& request);
};

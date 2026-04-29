#include "VoiceGenerator.h"
#include "AUI/Logging/ALogger.h"
#include "AUI/IO/AFileOutputStream.h"
#include <chrono>

static constexpr auto LOG_TAG = "VoiceGenerator";

AFuture<VoiceGenerator::VoiceMessage> VoiceGenerator::generate(AString text, AString languageCode, double speed) {
    ALogger::info(LOG_TAG) << "Generating voice message for text: " << text.substr(0, 50) << "...";

    try {
        APath voiceDir("data/voice_messages");
        voiceDir.makeDirs();

        ElevenLabsClient ttsClient{
            .baseUrl = "https://api.elevenlabs.io/",
            .apiKey = mApiKey,
            .voiceId = mVoiceId,
        };

        ElevenLabsClient::TextToSpeechRequest request{
            .text = text,
            .model_id = "eleven_v3",
            .language_code = languageCode,
            .voice_settings = {.speed = speed},
        };

        auto ttsResponse = co_await ttsClient.textToSpeech(request);

        if (ttsResponse.audioData.empty()) {
            throw AException("ElevenLabs returned empty audio data");
        }

        auto timestamp = std::chrono::system_clock::now().time_since_epoch().count();
        APath outputPath = voiceDir / "{}.mp3"_format(timestamp);

        AFileOutputStream stream(outputPath);
        stream.write(
            reinterpret_cast<const char*>(ttsResponse.audioData.data()),
            ttsResponse.audioData.getSize()
        );
        stream.close();

        ALogger::info(LOG_TAG) << "Voice message saved to: " << outputPath.absolute();

        co_return VoiceMessage{ .path = outputPath.absolute() };
    } catch (const AException& e) {
        ALogger::err(LOG_TAG) << "Failed to generate voice message: " << e;
        throw;
    }
}

#pragma once
#include "AUI/Common/AString.h"
#include "AUI/Thread/AFuture.h"
#include "AUI/IO/APath.h"
#include "ElevenLabsClient.h"


class VoiceGenerator {
public:
    VoiceGenerator(AString apiKey, AString voiceId = "pPdl9cQBQq4p6mRkZy2Z")
        : mApiKey(std::move(apiKey)), mVoiceId(std::move(voiceId)) {}

    struct VoiceMessage {
        APath path;
    };

    AFuture<VoiceMessage> generate(AString text, AString languageCode = "en", double speed = 1.0);

private:
    AString mApiKey;
    AString mVoiceId;
};

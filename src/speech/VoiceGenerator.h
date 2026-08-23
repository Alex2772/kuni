#pragma once
#include "AUI/Common/AString.h"
#include "AUI/Thread/AFuture.h"
#include "AUI/IO/APath.h"


class VoiceGenerator {
public:
    VoiceGenerator() = default;

    struct VoiceMessage {
        APath path;
    };

    AFuture<VoiceMessage> generate(AString text, AString languageCode = "en", double speed = 1.0);
    struct GenerateOpts {
        AString languageCode = "en";
        AString responseFormat = "mp3";
        double speed = 1.0;
    };
    AFuture<AByteBuffer> generate(AString text, GenerateOpts config);
};

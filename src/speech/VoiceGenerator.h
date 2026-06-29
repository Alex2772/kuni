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
};

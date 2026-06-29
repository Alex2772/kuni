#pragma once

#include "AUI/IO/APath.h"

#include <AUI/Common/AString.h>

struct Prompts {
    static AString load(const APath& path, AStringView defaultPrompt);
    AString system;
    AString characterBase;
    AString characterAppearance;
    AString photoToText;
    AString stickerToText;
    AString antiRepeatPrompt;
    AString diarySave;
    AString sleepConsolidator;
    AString recordAudioSpeech;
};

const Prompts& prompts();

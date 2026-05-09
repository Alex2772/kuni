#pragma once
#include "AUI/Thread/AFuture.h"

namespace llmui {


AFuture<AString> voiceMessage(AStringView pathToVoice);
}
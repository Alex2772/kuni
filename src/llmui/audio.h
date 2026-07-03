#pragma once
#include "AUI/Thread/AFuture.h"

struct IOpenAIChat;

namespace llmui {


AFuture<AString> voiceMessage(AStringView pathToVoice, IOpenAIChat& openAI);
}
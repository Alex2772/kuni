#pragma once

#include <Diary.h>
#include <IOpenAIChat.h>
#include <OpenAITools.h>
#include <functional>

namespace tools {
OpenAITools::Tool ask(std::function<AString()> additionalDetails, _<IOpenAIChat> openAI, Diary& diary);
}

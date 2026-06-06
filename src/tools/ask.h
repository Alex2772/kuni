#pragma once

#include <Diary.h>
#include <IOpenAIChat.h>
#include <OpenAITools.h>

namespace tools {
OpenAITools::Tool ask(const AVector<IOpenAIChat::Message>& temporaryContext, _<IOpenAIChat> openAI, Diary& diary);
}

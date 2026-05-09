#pragma once
#include "OpenAITools.h"
#include "Diary.h"
#include "IOpenAIChat.h"
#include "telegram/ITelegramClient.h"

namespace tools {
OpenAITools::Tool getTelegramChats(_<ITelegramClient> telegram,
                                   _<IOpenAIChat> openAI,
                                   bool isActingProactively);
}

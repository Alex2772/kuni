#pragma once
#include "OpenAITools.h"
#include "IOpenAIChat.h"
#include "telegram/ITelegramClient.h"

namespace tools {
OpenAITools::Tool getChatPhoto(_<ITelegramClient> telegram,
                               _<IOpenAIChat> openAI,
                               _<td::td_api::chat> chat,
                               const IOpenAIChat::Session& temporaryContext);
}

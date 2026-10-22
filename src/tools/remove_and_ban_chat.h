#pragma once
#include "ChatDatabase.h"
#include "OpenAITools.h"
#include "telegram/ITelegramClient.h"

namespace tools {
OpenAITools::Tool removeAndBanChat(_<ITelegramClient> telegram, _<ChatDatabase> chatDatabase);
}

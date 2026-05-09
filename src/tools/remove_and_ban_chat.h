#pragma once
#include "OpenAITools.h"
#include "telegram/ITelegramClient.h"

namespace tools {
OpenAITools::Tool removeAndBanChat(_<ITelegramClient> telegram);
}

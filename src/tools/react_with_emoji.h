#pragma once
#include "OpenAITools.h"
#include "telegram/ITelegramClient.h"

namespace tools {
OpenAITools::Tool reactWithEmoji(_<ITelegramClient> telegram, _<td::td_api::chat> chat);
}

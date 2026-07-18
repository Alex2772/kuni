#pragma once
#include "OpenAITools.h"
#include "telegram/ITelegramClient.h"

namespace tools {
OpenAITools::Tool groupAdminRemoveMessage(_<ITelegramClient> telegram, _<td::td_api::chat> chat);
}

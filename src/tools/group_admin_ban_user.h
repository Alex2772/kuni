#pragma once
#include "OpenAITools.h"
#include "telegram/ITelegramClient.h"

namespace tools {
OpenAITools::Tool groupAdminBanUser(_<ITelegramClient> telegram, _<td::td_api::chat> chat);
}

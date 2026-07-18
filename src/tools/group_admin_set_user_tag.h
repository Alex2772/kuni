//
// Created by alex2772 on 6/18/26.
//

#pragma once
#include "OpenAITools.h"
#include "telegram/ITelegramClient.h"

namespace tools {
OpenAITools::Tool groupAdminSetUserTag(_<ITelegramClient> telegram, _<td::td_api::chat> chat);
}

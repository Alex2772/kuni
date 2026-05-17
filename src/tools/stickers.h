#pragma once
#include "OpenAITools.h"
#include "telegram/ITelegramClient.h"

namespace llmui {
AFuture<AString> listFavoriteStickers(ITelegramClient& telegram, IOpenAIChat& openAI);
}

namespace tools::stickers {
AMap<int64_t, td::td_api::object_ptr<td::td_api::sticker>>& knownStickers();

OpenAITools::Tool list(_<ITelegramClient> telegram, _<IOpenAIChat> openAI);
OpenAITools::Tool save(_<ITelegramClient> telegram);
OpenAITools::Tool send(_<ITelegramClient> telegram, _<td::td_api::chat> chat);
}

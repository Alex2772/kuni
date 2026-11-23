#pragma once
#include "IStableDiffusionClient.h"
#include "OpenAITools.h"
#include "telegram/ITelegramClient.h"

namespace tools {
struct SendTelegramMessageOpts {
    AOptional<size_t> maxPossibleMessagesInARow = 10;
};
OpenAITools::Tool sendTelegramMessage(
    _<ITelegramClient> telegram,
    _<IOpenAIChat> openAI,
    _<td::td_api::chat> chat,
    _<td::td_api::array<td::td_api::object_ptr<td::td_api::message>>> messages,
    SendTelegramMessageOpts opts = {});
}

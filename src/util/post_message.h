#pragma once
#include "AUI/Image/AImage.h"
#include "AUI/Thread/AFuture.h"
#include "telegram/ITelegramClient.h"

namespace util {

[[nodiscard]]
AFuture<td::td_api::object_ptr<td::td_api::message>> telegramPostMessage(
    ITelegramClient& telegram,
    int64_t chatId,
    AString text,
    AOptional<_<AImage>> photo = std::nullopt,
    AOptional<APath> audioPath = std::nullopt,
    int64_t replyTo = 0);
}
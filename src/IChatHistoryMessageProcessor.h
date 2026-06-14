#pragma once

#include <telegram/ITelegramClient.h>
#include <AUI/Thread/AFuture.h>

class IChatHistoryMessageProcessor {
public:
    virtual ~IChatHistoryMessageProcessor() = default;
    virtual AFuture<AString> processChatHistoryMessage(const td::td_api::chat& chat, const td::td_api::message& msg, AString formatted) = 0;
};
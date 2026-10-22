#pragma once

#include <AppBase.h>
#include "telegram/ITelegramClient.h"

class IPlugin {
public:
    virtual ~IPlugin() {}
    virtual AFuture<> updateChatPriority(int& priority, AArc<td::td_api::chat> chat) { co_return; }

};
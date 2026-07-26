#pragma once
#include "OpenAITools.h"
#include "telegram/ITelegramClient.h"

class ChatDatabase {
public:
    explicit ChatDatabase(AArc<ITelegramClient> telegramClient) : mTelegramClient(std::move(telegramClient)) {}
    void patchAskTool(OpenAITools& tools, int64_t chatId);
    AOptional<AString> getLastAskResult(int64_t chatId);

private:
    AArc<ITelegramClient> mTelegramClient;
    AAsyncHolder mAsync;
    APath getChatPath(int64_t chatId);

};
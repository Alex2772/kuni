//
// Created by alex2772 on 7/26/26.
//

#include "ChatDatabase.h"

#include "AUI/IO/AFileInputStream.h"

void ChatDatabase::patchAskTool(OpenAITools& tools, int64_t chatId) {
    for (auto& i : tools.handlers()) {
        if (i.first != "ask") {
            continue;
        }
        i.second.handler = [path = getChatPath(chatId), original = std::move(i.second.handler)](OpenAITools::Ctx ctx) -> AFuture<AString> {
            auto result = co_await original(std::move(ctx));
            AFileOutputStream(path / "last_ask.md") << result;
            co_return result;
        };
        return;
    }
    AUI_ASSERT_NO_CONDITION("no ask tool to patch");
}

AOptional<AString> ChatDatabase::getLastAskResult(int64_t chatId) {
    if (auto path = getChatPath(chatId) / "last_ask.md"; path.isRegularFileExists()) {
        return AString::fromUtf8(AByteBuffer::fromStream(AFileInputStream(path)));
    }
    return std::nullopt;
}

APath ChatDatabase::getChatPath(int64_t chatId) {
    auto path = APath("chats") / "{}"_format(chatId);
    if (!path.isDirectoryExists()) {
        path.makeDirs();
        mAsync << [this, path, chatId]() -> AFuture<> {
            AFileOutputStream(path / "name") << (co_await mTelegramClient->getChat(chatId))->title_;
        }();
    }
    return path;
}


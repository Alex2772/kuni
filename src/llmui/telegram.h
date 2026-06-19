#pragma once
#include "IOpenAIChat.h"
#include "AUI/Thread/AFuture.h"
#include <telegram/ITelegramClient.h>

namespace llmui {
[[nodiscard]]
AFuture<AString> extractSenderName(ITelegramClient& telegram, td::td_api::MessageSender& sender);

[[nodiscard]]
AFuture<> formatChatSingle(
    ITelegramClient& telegram, AString& result, td::td_api::chat& chat);

[[nodiscard]]
AString extractMessageTypeAndText(td::td_api::message& msg);

[[nodiscard]]
AFuture<>
formatChatList(ITelegramClient& telegram, AString& result, std::span<_<td::td_api::chat>> chats);

[[nodiscard]]
AFuture<AString> formatChatHistoryMessage(
    ITelegramClient& telegram,
    td::td_api::message& msg,
    const td::td_api::chat& chat,
    IOpenAIChat& openAI,
    std::span<const IOpenAIChat::Message> temporaryContext,
    AStringView xmlTag = "message");

[[nodiscard]]
AFuture<APath> fetchMedia(ITelegramClient& telegram, td::td_api::object_ptr<td::td_api::file>& file);

[[nodiscard]]
AString formatLinkPreview(const td::td_api::linkPreview& preview);
}   // namespace llmui
//
// Created by alex2772 on 5/9/26.
//

#include "get_telegram_chats.h"

#include "config.h"
#include "llmui/telegram.h"

#include <range/v3/action/reverse.hpp>
#include <range/v3/algorithm/remove_if.hpp>
#include <range/v3/range/conversion.hpp>
#include <range/v3/view/transform.hpp>

OpenAITools::Tool tools::getTelegramChats(_<ITelegramClient> telegram,
                                          _<IOpenAIChat> openAI,
                                          bool isActingProactively) {
    return {
        .name = "get_telegram_chats",
        .description = "Returns a list of Telegram chats. Use this to seek chat_ids, looking for existing "
                       "chats and unread chats, or to start a new conversation.",
        .handler = [telegram = std::move(telegram),
                    openAI = std::move(openAI),
                    isActingProactively](OpenAITools::Ctx ctx) -> AFuture<AString> {
            co_await telegram->waitForConnection();

            // get chats
            auto chatList = co_await telegram->sendQueryWithResult(ITelegramClient::toPtr(
                td::td_api::getChats(ITelegramClient::toPtr(td::td_api::chatListMain()), 50)));

            // chatIdsToChats
            auto chats =
                chatList->chat_ids_ | ranges::view::transform([&](td::td_api::int53 chatId) {
                    return telegram->sendQueryWithResult(ITelegramClient::toPtr(td::td_api::getChat(chatId)));
                }) |
                ranges::to_vector;
            AVector<td::td_api::object_ptr<td::td_api::chat>> resultChats;
            resultChats.reserve(chats.size());
            for (const auto& chat : chats) {
                resultChats.push_back(co_await chat);
            }

            // oldest first, newest last (chats in queue), because LLM tends to prioritize chats from the
            // top of the list.
            //
            // however, LLM sometimes decides to prioritize people, based on message preview, count and
            // a person.
            resultChats |= ranges::actions::reverse;

            if (isActingProactively) {
                // the whole point of "acting proactively" is to revisit older chats with no activity (no inbox
                // messages). LLM is likely to call get_telegram_chats during acting proactively, so we remove
                // active chats from the output during "acting proactively" phase.
                //
                // don't worry about active chats -- LLM will receive notifications from them anyway.
                resultChats.removeIf([](const td::td_api::object_ptr<td::td_api::chat>& chat) {
                    return chat->unread_count_ > 0;
                });
            }

            AString result =
                "You are currently looking at Telegram's main screen. Use see the following chats:\n";
            co_await llmui::formatChatList(*telegram, result, resultChats);

            result += "<instructions>\n"
            "Chat list view is limited. Use #search_chats to search for a specific chat.\n\n"
            "You should not use #get_telegram_chats just to check if someone texted you. You'll receive "
            "notification if something happened. Use #wait instead.\n"
            "</instructions>";
            co_return result;
        },
    };
}

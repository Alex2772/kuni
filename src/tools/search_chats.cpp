//
// Created by alex2772 on 5/9/26.
//

#include "search_chats.h"

#include "llmui/telegram.h"

#include <range/v3/range/conversion.hpp>
#include <range/v3/view/transform.hpp>

OpenAITools::Tool tools::searchChats(_<ITelegramClient> telegram) {
    return {
        .name = "search_chats",
        .description = "Searches for chats by @username or name.",
        .parameters =
            {
                .properties =
                    {
                        {"query", {.type = "string", .description = "The username or name of the "
                            "chat. Examples: \n"
                            "- @alex2772sc\n"
                            "- Alex2772\n"
                        }},
                    },
                .required = {"query"},
            },
        .handler = [telegram = std::move(telegram)](OpenAITools::Ctx ctx) -> AFuture<AString> {
            auto query = ctx.args["query"].asStringOpt().valueOrException("query string is required");
            if (query.startsWith("@")) {
                query = query.substr(1);
            }

            auto queryResult = co_await telegram->sendQueryWithResult(ITelegramClient::toPtr(td::td_api::searchChatsOnServer(query, 50)));
            auto usernameQueryResult = co_await telegram->sendQueryWithResult(ITelegramClient::toPtr(td::td_api::searchPublicChat(query)));

            if (queryResult->chat_ids_.empty() && !usernameQueryResult->id_) {
                co_return "No chats found satisfying your query.";
            }

            AString result;
            AVector<_<td::td_api::chat>> chats;
            try {
                auto chatFutures =
                    queryResult->chat_ids_ | ranges::view::transform([&](td::td_api::int53 chatId) {
                        return telegram->getChat(chatId);
                    }) |
                    ranges::to_vector;
                chats.reserve(chatFutures.size());
                for (const auto& chat : chatFutures) {
                    chats.push_back(co_await chat);
                }
            } catch (const AException& e) {
                ALogger::err("search_chats") << "chatIdsToChats(queryResult->chat_ids_) failed: " << e;
            }
            _<td::td_api::chat> publicChat;

            try {
                publicChat = co_await telegram->getChat(usernameQueryResult->id_);
            } catch (const AException& e) {
                ALogger::err("search_chats") << "chatIdToChat(usernameQueryResult->id_) failed: " << e;
            }

            result += "<existing_chats comment=\"Chats that you participate already\">\n";
            co_await llmui::formatChatList(*telegram, result, chats);
            result += "</existing_chats>\n";
            result += "<global_search comment=\"Chats that don't know about you\">\n";
            co_await llmui::formatChatSingle(*telegram, result, *publicChat);
            result += "</global_search>\n";

            co_return result;
        },
    };
}

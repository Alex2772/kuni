//
// Created by alex2772 on 5/9/26.
//

#include "remove_and_ban_chat.h"

#include "config.h"
#include "AUI/Thread/AThread.h"
#include "util/post_message.h"

#include <range/v3/algorithm/contains.hpp>

#include "util/json_utils.h"

using namespace std::chrono_literals;

OpenAITools::Tool tools::removeAndBanChat(_<ITelegramClient> telegram) {
    return {
        .name = "remove_and_ban_chat",
        .description = "Removes specified chat from chat list. If a chat is a DM with a user, prevents this "
            "specific user to DM you.\n"
            "You should use this if:\n"
            "- you don't like this chat and person;\n"
            "- participant(s) are consistently rude to you (use #ask_diary beforehand before making a final decision);\n"
            "- they are needy:\n"
            "  - asking to roleplay;\n"
            "  - asking to write a python program;\n"
            "  - asking to solve any other task;\n"
        ,
        .parameters =
            {
                .properties =
                    {
                        {"chat_id", {.type = "integer", .description = "chat_id to block."
                        }},
                    },
                .required = {"chat_id"},
            },
        .handler = [telegram = std::move(telegram)](OpenAITools::Ctx ctx) -> AFuture<AString> {
            auto chatId = util::jsonAsLongInt(ctx.args["chat_id"]).valueOrException("chat_id integer is required");
            ALogger::info("remove_and_ban_chat") << "remove_and_ban_chat: chat_id" << chatId;
            if (chatId == config::PAPIK_CHAT_ID) {
                // precaution -- can't delete chat with papik
                co_return "Failed";
            }

            auto chat = co_await telegram->sendQueryWithResult(
                ITelegramClient::toPtr(td::td_api::getChat(chatId)));

            switch (chat->type_->get_id()) {
                case td::td_api::chatTypePrivate::ID:
                    try {
                        // comment their dickpic.
                        co_await util::telegramPostMessage(*telegram, chatId, "фу какой маленький");
                        // give them a chance to see the last message, because later we will delete the entire chat.
                        co_await AThread::asyncSleep(5s);
                    } catch (const AException& e) {}
                    // Block the user from sending new DMs
                    co_await telegram->sendQueryWithResult(
                        ITelegramClient::toPtr(td::td_api::setMessageSenderBlockList(
                            td::td_api::make_object<td::td_api::messageSenderUser>(chatId),
                            td::td_api::make_object<td::td_api::blockListMain>())));
                    co_await telegram->sendQueryWithResult(
                        ITelegramClient::toPtr(td::td_api::deleteChatHistory(chatId, true, true)));
                    break;
                case td::td_api::chatTypeBasicGroup::ID:
                case td::td_api::chatTypeSupergroup::ID:
                    // leaveChat: Removes the current user from chat members. Private and secret
                    // chats can't be left using this method.
                    co_await telegram->sendQueryWithResult(
                        ITelegramClient::toPtr(td::td_api::leaveChat(chatId)));
                    break;
                default:
                    break;
            }

            co_return "Success";
        },
    };
}

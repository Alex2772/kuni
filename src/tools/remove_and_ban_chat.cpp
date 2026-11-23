//
// Created by alex2772 on 5/9/26.
//

#include "remove_and_ban_chat.h"

#include "config.h"

#include "util/json_utils.h"

static constexpr auto LOG_TAG = "remove_and_ban_chat";

namespace {
/**
 * @brief Human-readable identity of a user: `@username (First Last), user_id=123`.
 * @details
 * The model tends to mix up ids, so every ban/unban reports who exactly was affected, letting the model notice the
 * mistake and revert it.
 */
AString describeUser(const td::td_api::user& user) {
    AString result;
    if (user.usernames_ && !user.usernames_->active_usernames_.empty()) {
        result += "@" + user.usernames_->active_usernames_.at(0) + " ";
    } else {
        result += "(no username) ";
    }
    AString fullName = user.first_name_;
    if (!user.last_name_.empty()) {
        fullName += " " + user.last_name_;
    }
    result += "({}), user_id={}"_format(fullName, user.id_);
    return result;
}

AFuture<td::td_api::object_ptr<td::td_api::chat>> getChatForTool(ITelegramClient& telegram, const AJson& args) {
    auto chatId = util::jsonAsLongInt(args["chat_id"]).valueOrException("chat_id integer is required");
    co_return co_await telegram.sendQueryWithResult(ITelegramClient::toPtr(td::td_api::getChat(chatId)));
}
}   // namespace

OpenAITools::Tool tools::removeAndBanChat(_<ITelegramClient> telegram, _<ChatDatabase> chatDatabase) {
    return {
        .name = "remove_and_ban_chat",
        .description = "If a chat is a DM with a user, blocks this specific user so they can't DM you anymore (the "
            "chat history is kept). If a chat is a group, leaves it.\n"
            "You should use this if:\n"
            "- you don't like this chat and person;\n"
            "- participant(s) are consistently rude to you (use #ask beforehand before making a final decision);\n"
            "- they are needy:\n"
            "  - asking to roleplay;\n"
            "  - asking to write a python program;\n"
            "  - asking to solve any other task;\n"
            "The result tells you the username and the name of the blocked person. Make sure it is the person you "
            "meant; if you blocked the wrong person, call #unban_chat with the same chat_id right away."
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
        .handler = [telegram = std::move(telegram), chatDatabase = std::move(chatDatabase)](OpenAITools::Ctx ctx) -> AFuture<AString> {
            auto chatId = util::jsonAsLongInt(ctx.args["chat_id"]).valueOrException("chat_id integer is required");
            ALogger::info(LOG_TAG) << "remove_and_ban_chat: chat_id" << chatId;
            if (chatId == config().papikChatId) {
                // precaution -- can't ban papik
                co_return "Failed";
            }
            if (chatDatabase && chatDatabase->getPriorityOverrideFor(chatId).valueOr(0) >= 100) {
                // precaution -- can't ban important people
                co_return "Failed";
            }

            auto chat = co_await getChatForTool(*telegram, ctx.args);

            switch (chat->type_->get_id()) {
                case td::td_api::chatTypePrivate::ID: {
                    auto userId = static_cast<td::td_api::chatTypePrivate&>(*chat->type_).user_id_;
                    auto user = co_await telegram->getUser(userId);
                    // Non-destructive: only block the user from sending new DMs. The chat is kept so the ban can
                    // be reverted with unban_chat without losing anything.
                    co_await telegram->sendQueryWithResult(
                        ITelegramClient::toPtr(td::td_api::setMessageSenderBlockList(
                            td::td_api::make_object<td::td_api::messageSenderUser>(userId),
                            td::td_api::make_object<td::td_api::blockListMain>())));
                    auto description = describeUser(*user);
                    ALogger::info(LOG_TAG) << "Blocked " << description;
                    co_return "Blocked user {}. Is this the person you meant? If not, you have mixed up the ids: "
                              "call #unban_chat with chat_id={} immediately."_format(description, chatId);
                }
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

            co_return "Successfully left chat: \"{}\""_format(chat->title_);
        },
    };
}

OpenAITools::Tool tools::unbanChat(_<ITelegramClient> telegram) {
    return {
        .name = "unban_chat",
        .description = "Reverts #remove_and_ban_chat for a DM: unblocks the user so they can DM you again. Use this "
                       "if you blocked a wrong person (e.g., mixed up ids) or changed your mind.",
        .parameters =
            {
                .properties =
                    {
                        {"chat_id", {.type = "integer", .description = "chat_id to unblock."}},
                    },
                .required = {"chat_id"},
            },
        .handler = [telegram = std::move(telegram)](OpenAITools::Ctx ctx) -> AFuture<AString> {
            auto chat = co_await getChatForTool(*telegram, ctx.args);
            if (chat->type_->get_id() != td::td_api::chatTypePrivate::ID) {
                co_return "Error: \"{}\" is not a DM. Groups you left can't be unbanned; join them again "
                          "instead."_format(chat->title_);
            }
            auto userId = static_cast<td::td_api::chatTypePrivate&>(*chat->type_).user_id_;
            auto user = co_await telegram->getUser(userId);
            // null block list = remove the sender from all block lists.
            co_await telegram->sendQueryWithResult(ITelegramClient::toPtr(td::td_api::setMessageSenderBlockList(
                td::td_api::make_object<td::td_api::messageSenderUser>(userId), nullptr)));
            auto description = describeUser(*user);
            ALogger::info(LOG_TAG) << "Unblocked " << description;
            co_return "Unblocked user {}. They can DM you again."_format(description);
        },
    };
}

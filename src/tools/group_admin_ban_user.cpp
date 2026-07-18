//
// Created by alex2772 on 6/18/26.
//

#include "group_admin_ban_user.h"

#include "util/json_utils.h"

OpenAITools::Tool tools::groupAdminBanUser(_<ITelegramClient> telegram, _<td::td_api::chat> chat) {
    return {
        .name = "group_admin_ban_user",
        .description = "Administrative tool for \"{}\" chat. Permanently deletes the specified user from the group "
                       "chat. Use this if you consistently feel this user is offensive to you, other people or "
                       "otherwise not worth the time (e.g., is consistently rude, toxic, or the "
                       "user brings no value to others). This is a serious, irreversible decision - use #ask "
                       "beforehand to double check your reasoning if unsure. This tool is only available in "
                       "group chats; for a private chat/DM with a specific person, use #remove_and_ban_chat "
                       "instead"_format(chat->title_),
        .parameters = {
            .properties = {
                {"user_id", {.type = "integer", .description = "ID of the user to remove. Can be acquired from sender_id."}},
            },
            .required = {"user_id" },
        },
        .handler = [telegram = std::move(telegram), chat = std::move(chat)](OpenAITools::Ctx ctx) -> AFuture<AString> {
            if (ctx.args.contains("chat_id")) {
                if (ctx.args["chat_id"].asLongInt() != chat->id_) {
                    co_return "Error: you can't ban users from other chats. Open them first. You are currently in chat \"{}\""_format(chat->title_);
                }
            }
            if (!ctx.args.contains("user_id")) {
                throw AException("user_id is a mandatory argument");
            }
            const auto targetUserId = util::jsonAsLongInt(ctx.args["user_id"]).valueOrException("user_id");

            auto user = co_await telegram->getUser(targetUserId);
            const auto result = "User {} {} were banned successfully from \"{}\" chat."_format(user->first_name_, user->last_name_, chat->title_);

            auto ok = co_await telegram->sendQueryWithResult(ITelegramClient::toPtr(td::td_api::banChatMember(
                chat->id_, ITelegramClient::toPtr(td::td_api::messageSenderUser(user->id_)), 0, false)));
            co_return result;
        },
    };
}

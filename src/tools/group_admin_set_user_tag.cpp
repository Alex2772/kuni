//
// Created by alex2772 on 6/18/26.
//

#include "group_admin_set_user_tag.h"

#include "util/json_utils.h"

OpenAITools::Tool tools::groupAdminSetUserTag(_<ITelegramClient> telegram, _<td::td_api::chat> chat) {
    return {
        .name = "group_admin_set_user_tag",
        .description = "Administrative tool for \"{}\" chat. Sets (or clears) the member tag shown next to a "
                       "user's name on every message they send in this group. Telegram member tags are normally "
                       "used to pin a short label to someone — their role, what they're into, a running joke, or "
                       "just a nickname that sticks. Unlike a one-off insult in chat, a tag is persistent and "
                       "visible on literally everything that person says afterwards, so this is a real vibe-check: "
                       "only tag someone if you've actually formed an opinion about them worth broadcasting "
                       "permanently, not on a whim. Set a tag based on your perception of the user (use #ask). Max 16"
                       "characters, no emoji. Pass an empty tag to remove an existing one."_format(chat->title_),
        .parameters = {
            .properties = {
                {"user_id", {.type = "integer", .description = "ID of the user to tag. Can be acquired from sender_id."}},
                {"tag", {.type = "string", .description = "New tag text, up to 16 characters, no emoji. Pass an empty string to remove the current tag."}},
            },
            .required = {"user_id", "tag"},
        },
        .handler = [telegram = std::move(telegram), chat = std::move(chat)](OpenAITools::Ctx ctx) -> AFuture<AString> {
            if (ctx.args.contains("chat_id")) {
                if (ctx.args["chat_id"].asLongInt() != chat->id_) {
                    co_return "Error: you can't tag users from other chats. Open them first. You are currently in chat \"{}\""_format(chat->title_);
                }
            }
            if (!ctx.args.contains("user_id")) {
                throw AException("user_id is a mandatory argument");
            }
            if (!ctx.args.contains("tag")) {
                throw AException("tag is a mandatory argument");
            }
            const auto targetUserId = util::jsonAsLongInt(ctx.args["user_id"]).valueOrException("user_id");
            const auto tag = ctx.args["tag"].asStringOpt().valueOrException("tag string is required");

            auto user = co_await telegram->getUser(targetUserId);
            const auto result = tag.empty()
                ? "Tag for user {} {} was removed successfully in \"{}\" chat."_format(user->first_name_, user->last_name_, chat->title_)
                : "User {} {} was tagged as \"{}\" successfully in \"{}\" chat."_format(user->first_name_, user->last_name_, tag, chat->title_);

            auto ok = co_await telegram->sendQueryWithResult(
                ITelegramClient::toPtr(td::td_api::setChatMemberTag(chat->id_, user->id_, tag.toStdString())));
            co_return result;
        },
    };
}

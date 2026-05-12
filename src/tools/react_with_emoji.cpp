//
// Created by alex2772 on 5/9/26.
//

#include "react_with_emoji.h"

#include "util/json_utils.h"

OpenAITools::Tool tools::reactWithEmoji(_<ITelegramClient> telegram, _<td::td_api::chat> chat) {
    return {
        .name = "react_with_emoji",
        .description = "Add an emoji reaction to a message. You can use only one of these emojis only: "
                       "👍 👎 ❤️ 🔥 🥰 👏 😁 🤔 🤯 😱 🤬 😢 🎉 🤩 🤮 💩 🙏 👌 🕊 🤡 🥱 🥴 😍 🐳 🌚 🌭 💯 🤣 ⚡️ 🍌 🏆 💔 🤨 😐 🍓 🍾 💋 😈 😴 😭 🤓 👻 👀 🎃 😇 😨 🤝 🤗 🎅 💅 🤪 🗿 🆒 💘 🦄 😘 💊 😎 👾 🤷 😡",
        .parameters = {
            .properties = {
                {"message_id", {.type = "integer", .description = "ID of the message to react to. Taken from message_id attribute in <message> tag."}},
                {"emoji", {.type = "string", .description = "A single emoji from the allowed list only. Do not use emojis outside the list."}},
            },
            .required = {"message_id", "emoji"},
        },
        .handler = [telegram = std::move(telegram), chat = std::move(chat)](OpenAITools::Ctx ctx) -> AFuture<AString> {
            if (ctx.args.contains("chat_id")) {
                if (ctx.args["chat_id"].asLongInt() != chat->id_) {
                    co_return "Error: you can't send messages to other chats. Open them first. You are currently in chat \"{}\""_format(chat->title_);
                }
            }
            auto messageId = util::jsonAsLongInt(ctx.args["message_id"]).valueOrException("message_id integer required");
            auto emoji = ctx.args["emoji"].asStringOpt().valueOrException("emoji required");

            auto reaction = td::td_api::make_object<td::td_api::addMessageReaction>();
            reaction->chat_id_ = chat->id_;
            reaction->message_id_ = messageId;
            reaction->reaction_type_ = td::td_api::make_object<td::td_api::reactionTypeEmoji>(emoji.toStdString());
            reaction->is_big_ = false;
            reaction->update_recent_reactions_ = true;

            co_await telegram->sendQueryWithResult(std::move(reaction));
            co_return "Reaction {} added successfully."_format(emoji);
        },
    };
}

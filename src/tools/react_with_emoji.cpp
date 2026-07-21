//
// Created by alex2772 on 5/9/26.
//

#include "react_with_emoji.h"

#include "util/json_utils.h"

// Telegram stores reaction emojis with or without the U+FE0F variation selector (e.g. "❤️" vs "❤",
// "⚡️" vs "⚡"). Strip it so the model's emoji matches the chat's allowed-reaction list reliably.
static std::string stripVariationSelector(std::string s) {
    static const std::string kVs16 = "\xEF\xB8\x8F"; // U+FE0F in UTF-8
    for (auto pos = s.find(kVs16); pos != std::string::npos; pos = s.find(kVs16)) {
        s.erase(pos, kVs16.size());
    }
    return s;
}

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

            // Some chats (news channels, restricted groups) only allow a subset of reactions. Firing
            // addMessageReaction blindly there returns REACTION_INVALID ("The reaction isn't available
            // for the message"), which the model can't recover from and rationalizes as a broken tool.
            // Private chats and unrestricted chats report chatAvailableReactionsAll and are left alone.
            if (const auto* avail = chat->available_reactions_.get();
                avail && avail->get_id() == td::td_api::chatAvailableReactionsSome::ID) {
                const auto* some = static_cast<const td::td_api::chatAvailableReactionsSome*>(avail);
                const auto wanted = stripVariationSelector(emoji.toStdString());
                AStringVector allowed;
                bool ok = false;
                for (const auto& r : some->reactions_) {
                    if (r && r->get_id() == td::td_api::reactionTypeEmoji::ID) {
                        const auto& e = static_cast<const td::td_api::reactionTypeEmoji*>(r.get())->emoji_;
                        allowed << AString::fromUtf8(e);
                        if (stripVariationSelector(e) == wanted) {
                            ok = true;
                        }
                    }
                }
                if (!ok) {
                    if (allowed.empty()) {
                        co_return "Reactions are disabled for this message - you can't react here. Just skip it, this is not an error.";
                    }
                    co_return "The reaction {} isn't available for this message. Allowed reactions here: {}. Pick one of those, or just skip reacting."_format(
                        emoji, allowed.join(' '));
                }
            }

            auto reaction = td::td_api::make_object<td::td_api::addMessageReaction>();
            reaction->chat_id_ = chat->id_;
            reaction->message_id_ = messageId;
            // Telegram's active emoji reactions are stored WITHOUT the U+FE0F variation selector
            // (e.g. the heart reaction is "❤" = U+2764, not "❤️" = U+2764 U+FE0F). Sending the
            // FE0F form makes addMessageReaction fail with REACTION_INVALID ("The reaction isn't
            // available for the message") - this is exactly why ❤️/⚡️ failed while single-codepoint
            // emojis (🔥 👍 🤔 …) worked. Strip it so the byte form matches the server's.
            reaction->reaction_type_ = td::td_api::make_object<td::td_api::reactionTypeEmoji>(stripVariationSelector(emoji.toStdString()));
            reaction->is_big_ = false;
            reaction->update_recent_reactions_ = true;

            try {
                co_await telegram->sendQueryWithResult(std::move(reaction));
            } catch (const AException& e) {
                // Residual per-message restrictions or transient state. Report gracefully so the model
                // treats it as "can't react to this one" instead of "reactions are globally broken".
                co_return "Couldn't add the reaction {} to this message ({}). It happens with some messages - just skip reacting here, it's not a real error."_format(
                    emoji, e.getMessage());
            }
            co_return "Reaction {} added successfully."_format(emoji);
        },
    };
}

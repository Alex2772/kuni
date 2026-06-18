//
// Created by alex2772 on 6/18/26.
//

#include "remove_message.h"

#include "util/json_utils.h"

OpenAITools::Tool tools::removeMessage(_<ITelegramClient> telegram, _<td::td_api::chat> chat) {
    return {
        .name = "remove_message",
        .description = "Deletes specified message for both you and participant(s) in \"{}\" chat.\n"
                       "You should use this when you mistakenly sent a message to a wrong chat or to remove an inappropriate message in group chats."_format(chat->title_),
        .parameters = {
            .properties = {
                {"message_id", {.type = "integer|array", .description = "ID(s) of the message(s) to delete. Taken from message_id attribute in <message> tag."}},
            },
            .required = {"message_id" },
        },
        .handler = [telegram = std::move(telegram), chat = std::move(chat)](OpenAITools::Ctx ctx) -> AFuture<AString> {
            if (ctx.args.contains("chat_id")) {
                if (ctx.args["chat_id"].asLongInt() != chat->id_) {
                    co_return "Error: you can't send messages to other chats. Open them first. You are currently in chat \"{}\""_format(chat->title_);
                }
            }
            if (!ctx.args.contains("message_id")) {
                throw AException("message_id is a mandatory argument");
            }

            td::td_api::array<td::td_api::int53> messages;
            if (ctx.args["message_id"].isArray()) {
                for (const auto& i : ctx.args["message_id"].asArray()) {
                    messages.push_back(util::jsonAsLongInt(i).valueOrException("expected integer"));
                }
            } else {
                messages.push_back(util::jsonAsLongInt(ctx.args["message_id"]).valueOrException("message_id"));
            }

            const auto result = "Messages {} were deleted successfully."_format(std::span(messages));

            for (auto& i : messages) {
                // remap client-side messageId (which was reported to llm to server-side messageId)
                i = (co_await telegram->getMessage(chat->id_, i))->id_;
            }

            auto ok = co_await telegram->sendQueryWithResult(ITelegramClient::toPtr(td::td_api::deleteMessages(chat->id_, std::move(messages), true)));
            co_return result;
        },
    };
}

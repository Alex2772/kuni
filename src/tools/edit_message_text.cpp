//
// Created by alex2772 on 6/18/26.
//

#include "edit_message_text.h"

#include "util/json_utils.h"

OpenAITools::Tool tools::editMessageText(_<ITelegramClient> telegram, _<td::td_api::chat> chat) {
    return {
        .name = "edit_message_text",
        .description = "Edits the text of a previously sent message in \"{}\" chat.\n"
                       "You should use this to fix a typo or correct a mistake in a message you already sent."_format(chat->title_),
        .parameters = {
            .properties = {
                {"message_id", {.type = "integer", .description = "ID of the message to edit. Taken from message_id attribute in <message> tag."}},
                {"text", {.type = "string", .description = "New text for the message. The previous text will be replaced with this one."}},
            },
            .required = {"message_id", "text"},
        },
        .handler = [telegram = std::move(telegram), chat = std::move(chat)](OpenAITools::Ctx ctx) -> AFuture<AString> {
            auto messageId = util::jsonAsLongInt(ctx.args["message_id"]).valueOrException("message_id integer is required");
            auto text = ctx.args["text"].asStringOpt().valueOrException("text string is required");

            // remap client-side messageId (which was reported to llm to server-side messageId)
            messageId = (co_await telegram->getMessage(chat->id_, messageId))->id_;

            auto content = td::td_api::make_object<td::td_api::inputMessageText>();
            content->text_ = [&] {
                auto t = td::td_api::make_object<td::td_api::formattedText>();
                t->text_ = text.toStdString();
                return t;
            }();

            co_await telegram->sendQueryWithResult(
                ITelegramClient::toPtr(td::td_api::editMessageText(chat->id_, messageId, nullptr, std::move(content))));

            co_return "Message {} was edited successfully."_format(messageId);
        },
    };
}

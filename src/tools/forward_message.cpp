//
// Created by kuni on 6/24/26.
//

#include "forward_message.h"

#include "util/json_utils.h"

static constexpr auto LOG_TAG = "tools::forwardMessage";

OpenAITools::Tool tools::forwardMessage(_<ITelegramClient> telegram, _<td::td_api::chat> fromChat) {
    return {
        .name = "forward_message",
        .description = "Forwards one or more messages from the currently opened channel to another chat (e.g., to a friend, "
                       "a group, or to Saved Messages). Use this when you find a post in a channel interesting "
                       "enough to share. You can optionally add a comment that will be sent as a separate message "
                       "after the forward.",
        .parameters = {
            .properties = {
                {"message_ids", {
                    .type = "array",
                    .description = "IDs of the messages to forward. Taken from message_id attributes in <message> tags. "
                                   "Pass a single-element array to forward just one message.",
                }},
                {"to_chat_id", {
                    .type = "integer",
                    .description = "ID of the destination chat. Use #get_telegram_chats to find chat IDs. "
                                   "Use your own chat ID (Saved Messages) to forward to yourself.",
                }},
                {"comment", {
                    .type = "string",
                    .description = "Optional comment to send after the forwarded message. "
                                   "Express your reaction, thoughts, or why you found this interesting.",
                }},
            },
            .required = {"message_ids", "to_chat_id"},
        },
        .handler = [telegram, fromChat](OpenAITools::Ctx ctx) -> AFuture<AString> {
            auto toChatId  = util::jsonAsLongInt(ctx.args["to_chat_id"]).valueOrException("to_chat_id integer required");
            auto comment   = ctx.args["comment"].asStringOpt();

            auto& idsJson = ctx.args["message_ids"];
            if (!idsJson.isArray()) throw AException("message_ids must be an array");
            std::vector<std::int64_t> messageIds;
            for (const auto& v : idsJson.asArray()) {
                messageIds.push_back(util::jsonAsLongInt(v).valueOrException("message_ids must contain integers"));
            }
            if (messageIds.empty()) throw AException("message_ids must not be empty");

            auto messageCount = messageIds.size();

            ALogger::info(LOG_TAG) << "Forwarding " << messageCount << " message(s)"
                                   << " from chat " << fromChat->id_
                                   << " to chat " << toChatId;

            auto fwd = td::td_api::make_object<td::td_api::forwardMessages>();
            fwd->chat_id_      = toChatId;
            fwd->from_chat_id_ = fromChat->id_;
            fwd->message_ids_  = std::move(messageIds);
            fwd->send_copy_    = false;
            fwd->remove_caption_ = false;

            co_await telegram->sendQueryWithResult(std::move(fwd));

            if (comment && !comment->empty()) {
                auto sendMsg = td::td_api::make_object<td::td_api::sendMessage>();
                sendMsg->chat_id_ = toChatId;
                auto content = td::td_api::make_object<td::td_api::inputMessageText>();
                auto formattedText = td::td_api::make_object<td::td_api::formattedText>();
                formattedText->text_ = comment->toStdString();
                content->text_ = std::move(formattedText);
                sendMsg->input_message_content_ = std::move(content);
                co_await telegram->sendQueryWithResult(std::move(sendMsg));
            }

            co_return "Forwarded {} message(s) successfully to chat_id={}"_format(messageCount, toChatId);
        },
    };
}

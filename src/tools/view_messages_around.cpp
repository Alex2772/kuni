//
// view_messages_around: fetches the messages surrounding a specific message in a chat, so the LLM can
// pull in more context than what a text search alone would return.
//

#include "view_messages_around.h"

#include "llmui/telegram.h"
#include "util/is_accessible_from_lockdown.h"
#include "util/json_utils.h"

#include <range/v3/view/reverse.hpp>

namespace {
constexpr auto LOG_TAG = "view_messages_around";
constexpr int32_t DEFAULT_BEFORE = 10;
constexpr int32_t DEFAULT_AFTER = 10;
constexpr int32_t MAX_TOTAL = 50;
}   // namespace

OpenAITools::Tool tools::viewMessagesAround(_<ITelegramClient> telegram, _<IOpenAIChat> openAI, const IOpenAIChat::Session& temporaryContext) {
    return {
        .name = "view_messages_around",
        .description =
            "Fetches the chat messages surrounding a specific message, so you can see what was said before and "
            "after it. Use this after #search_messages found an interesting message but you need more context "
            "around it (e.g. to understand what was being discussed).\n"
            "- `message_id` is taken from the `message_id` or `reply_to` attribute of a <message> tag you've previously seen.\n"
            "- The target message itself is included in the result and marked with a `target` attribute.",
        .parameters =
            {
                .properties =
                    {
                        {"chat_id", {.type = "integer", .description = "The chat the message belongs to. Use get_telegram_chats/search_chats to find chat_ids."}},
                        {"message_id", {.type = "integer", .description = "The message_id to center the view on, taken from a <message> tag."}},
                        {"before", {.type = "integer", .description = "(optional) How many older messages to include before the target message. Default {}."_format(DEFAULT_BEFORE)}},
                        {"after", {.type = "integer", .description = "(optional) How many newer messages to include after the target message. Default {}."_format(DEFAULT_AFTER)}},
                    },
                .required = {"chat_id", "message_id"},
            },
        .handler = [telegram = std::move(telegram), openAI = std::move(openAI), &temporaryContext](OpenAITools::Ctx ctx) -> AFuture<AString> {
            auto chatId = util::jsonAsLongInt(ctx.args["chat_id"]).valueOrException("chat_id integer is required");
            auto messageId = util::jsonAsLongInt(ctx.args["message_id"]).valueOrException("message_id integer is required");

            if (!co_await util::isAccessibleFromLockdown(*telegram, chatId)) {
                co_return "Error: this chat is not accessible.";
            }

            int32_t before = DEFAULT_BEFORE;
            if (ctx.args.contains("before")) {
                if (auto v = util::jsonAsLongInt(ctx.args["before"])) {
                    before = std::clamp(static_cast<int32_t>(*v), 0, MAX_TOTAL);
                }
            }
            int32_t after = DEFAULT_AFTER;
            if (ctx.args.contains("after")) {
                if (auto v = util::jsonAsLongInt(ctx.args["after"])) {
                    after = std::clamp(static_cast<int32_t>(*v), 0, MAX_TOTAL);
                }
            }
            // cap the total window so we don't flood the LLM's context.
            if (before + after + 1 > MAX_TOTAL) {
                const auto excess = (before + after + 1) - MAX_TOTAL;
                const auto cutBefore = std::min(before, (excess + 1) / 2);
                before -= cutBefore;
                after -= (excess - cutBefore);
                after = std::max(after, 0);
            }

            // remap client-side messageId (which was reported to the LLM) to the server-side messageId.
            td::td_api::int53 serverMessageId;
            try {
                serverMessageId = (co_await telegram->getMessage(chatId, messageId))->id_;
            } catch (const AException& e) {
                ALogger::warn(LOG_TAG) << "message " << messageId << " not found in chat " << chatId << ": " << e;
                co_return "Error: message {} was not found in this chat."_format(messageId);
            }

            // getChatHistory(chat_id, from_message_id, offset, limit, ...): offset is how many messages newer
            // than from_message_id to include before it in the result; from_message_id itself is included.
            // requesting offset=-after, limit=after+1+before yields exactly [after newer, target, before older].
            auto response = co_await telegram->sendQueryWithResult(ITelegramClient::toPtr(
                td::td_api::getChatHistory(chatId, serverMessageId, -after, after + 1 + before, false)));

            if (response->messages_.empty()) {
                co_return "Error: message {} was not found in this chat."_format(messageId);
            }

            auto chat = co_await telegram->getChat(chatId);

            AString result = "<messages_around message_id=\"{}\">\n"_format(messageId);
            // messages_ comes newest-first; flip to chronological order (oldest first) to match chat history
            // presentation conventions elsewhere in the codebase.
            for (auto& msg : response->messages_ | ranges::view::reverse) {
                AStringView xmlTag = msg->id_ == serverMessageId ? "message target" : "message";
                result += co_await llmui::formatChatHistoryMessage(*telegram, *msg, *chat, *openAI, temporaryContext, xmlTag);
            }
            result += "</messages_around>\n";

            co_return result;
        },
    };
}

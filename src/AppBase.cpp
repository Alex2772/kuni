//
// Created by alex2772 on 2/27/26.
//

#include "AppBase.h"

#include <random>
#include <range/v3/algorithm/remove_if.hpp>

#include "AUI/Logging/ALogger.h"
#include "AUI/Thread/AEventLoop.h"
#include "AUI/Thread/AThreadPool.h"
#include "AUI/Util/kAUI.h"
#include "OpenAIChat.h"
#include "config.h"

using namespace std::chrono_literals;

static constexpr auto LOG_TAG = "App";

AppBase::AppBase(): mWakeupTimer(_new<ATimer>(2h)) {
    // mTools.addTool({
    //     .name = "send_telegram_message",
    //     .description = "Sends a message to a Telegram user.",
    //     .parameters = {
    //         .properties = {
    //             {"chat_id", { .type = "integer", .description = "The ID of the Telegram chat" }},
    //             {"message", { .type = "string", .description = "Contents of the message" }},
    //         },
    //         .required = {"chat_id", "message"},
    //     },
    // }, [this](const AJson& args) -> AFuture<AString> {
    //     const auto& object = args.asObjectOpt().valueOrException("object expected");
    //     auto chatId = object["chat_id"].asLongIntOpt().valueOrException("`chat_id` integer expected");
    //     auto message = object["message"].asStringOpt().valueOrException("`message` string expected");
    //     co_await telegramPostMessage(chatId, message);
    //     co_return "Message sent successfully.";
    // });

    connect(mWakeupTimer->fired, me::actProactively);
    mWakeupTimer->start();

    mAsync << [](AppBase& self) -> AFuture<> {
        for (;;) {
            AUI_ASSERT(AThread::current() == self.getThread());
            if (self.mNotifications.empty()) {
                co_await self.mNotificationsSignal;
            }
            AUI_ASSERT(AThread::current() == self.getThread());
            self.mNotificationsSignal = AFuture<>(); // reset
            if (self.mNotifications.empty()) {
                continue;
            }
            auto notification = std::move(self.mNotifications.front());
            self.mNotifications.pop();
            self.mTemporaryContext << OpenAIChat::Message{
                .role = OpenAIChat::Message::Role::USER,
                .content = {},
            };

            for (auto it = self.mCachedDiary->begin(); it != self.mCachedDiary->end();) {
                if (!co_await self.diaryEntryIsRelatedToCurrentContext(*it)) {
                    ++it;
                    continue;
                }
                self.mTemporaryContext.last().content += "<your_diary_page additional_context just_for_reasoning no_plagiarism no_copy>\n" + *it + "\n</your_diary_page>\n";
                it = self.mCachedDiary->erase(it);
            }
            self.mTemporaryContext.last().content += notification.message;

            bool pauseFlag = false;
            naxyi:
            self.updateTools(notification.actions);
            auto escape = [&](OpenAITools::Ctx ctx) -> AFuture<AString> {
                pauseFlag = true;
                co_return "Success";
            };
            notification.actions.insert({
                .name = "pause",
                .description = "Pauses the conversation",
                .handler = escape,
            });
            notification.actions.insert({
                .name = "wait",
                .description = "Wait until further notifications",
                .handler = escape,
            });
            OpenAIChat llm {
                .systemPrompt = config::SYSTEM_PROMPT,
                .tools = notification.actions.asJson(),
            };

            OpenAIChat::Response botAnswer = co_await llm.chat(self.mTemporaryContext);
            AUI_ASSERT(AThread::current() == self.getThread());

            self.mTemporaryContext << botAnswer.choices.at(0).message;

            if (botAnswer.choices.empty() || botAnswer.choices.at(0).message.tool_calls.empty()) {
                if (botAnswer.usage.total_tokens >= config::DIARY_TOKEN_COUNT_TRIGGER) {
                    co_await self.diaryDumpMessages();
                }
                continue;
            }

            self.mTemporaryContext << co_await notification.actions.handleToolCalls(botAnswer.choices.at(0).message.tool_calls);
            ALOG_DEBUG(LOG_TAG) << "Tool call response: " << self.mTemporaryContext.last().content;
            AUI_ASSERT(AThread::current() == self.getThread());

            if (pauseFlag) {
                continue;
            }
            if (!notification.actions.handlers().empty()) {
                self.mTemporaryContext.last().content += "\nWhat's your next action? Use a `tool` to act. The following tools available: " + AStringVector(notification.actions.handlers().keyVector()).join(", ");
            }
            goto naxyi;
        }
        co_return;
    }(*this);
}

void AppBase::passNotificationToAI(AString notification, OpenAITools actions) {
    mNotifications.push({ std::move(notification), std::move(actions) });
    mNotificationsSignal.supplyValue();
}

AFuture<> AppBase::diaryDumpMessages() {
    AUI_DEFER { mCachedDiary.reset(); };
    if (mTemporaryContext.empty()) {
        co_return;
    }
    mTemporaryContext << OpenAIChat::Message{
        .role = OpenAIChat::Message::Role::USER,
        .content = config::DIARY_PROMPT,
    };

    OpenAIChat chat {
        .systemPrompt = config::SYSTEM_PROMPT,
        // .tools = mTools.asJson, // no tools should be involved.
    };
    naxyi:
    OpenAIChat::Response botAnswer = co_await chat.chat(mTemporaryContext);
    if (botAnswer.choices.at(0).message.content.empty()) {
        goto naxyi;
    }
    diarySave(botAnswer.choices.at(0).message.content);
    mTemporaryContext.clear();
}

void AppBase::actProactively() {
    static std::default_random_engine re(std::time(nullptr));
    AString prompt = "<your_diary_page just_for_reasoning no_plagiarism no_copy>\n";
    if (!mCachedDiary->empty()) {
        auto entry = mCachedDiary->begin() + re() % mCachedDiary->size();
        prompt += *entry;
        mCachedDiary->erase(entry);
    }
    prompt += R"(
</your_diary_page>

It's time to reflect on your thoughts!
  - maybe make some reasoning?\n"
  - maybe do some reflection?\n"
  - maybe write to a person and initiate a dialogue with #send_telegram_message?\n"
Act proactively!
)";
    passNotificationToAI(std::move(prompt));
}

AFuture<bool> AppBase::diaryEntryIsRelatedToCurrentContext(const AString& diaryEntry) {
    if (diaryEntry.empty()) {
        co_return false;
    }
    if (diaryEntry.contains("<important_note")) {
        co_return true;
    }
    AString basePrompt = config::DIARY_IS_RELATED_PROMPT;
    basePrompt += "\n<context>\n";
    AUI_ASSERT(!mTemporaryContext.empty());
    for (const auto& message: mTemporaryContext) {
        basePrompt += message.content + "\n\n";
    }
    basePrompt += "</context>\n";
    OpenAIChat chat {
        .systemPrompt = std::move(basePrompt),
    };
    auto decision = (co_await chat.chat(diaryEntry)).choices.at(0).message.content.lowercase();
    co_return decision.contains("yes") || decision.contains("y") || decision.contains("true") || decision.contains("1") || decision.contains("maybe");
}

void AppBase::removeNotifications(const AString& substring) {
    std::queue<Notification> remaining;
    while (!mNotifications.empty()) {
        auto n = std::move(mNotifications.front());
        mNotifications.pop();
        if (n.message.contains(substring)) {
            continue; // drop it
        }
        remaining.push(std::move(n));
    }
    mNotifications = std::move(remaining);
}

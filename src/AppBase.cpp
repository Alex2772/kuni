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
    mTools.addTool({
        .name = "send_telegram_message",
        .description = "Sends a message to a Telegram user.",
        .parameters = {
            .properties = {
                {"chat_id", { .type = "integer", .description = "The ID of the Telegram chat" }},
                {"message", { .type = "string", .description = "Contents of the message" }},
            },
            .required = {"chat_id", "message"},
        },
    }, [this](const AJson& args) -> AFuture<AString> {
        const auto& object = args.asObjectOpt().valueOrException("object expected");
        auto chatId = object["chat_id"].asLongIntOpt().valueOrException("`chat_id` integer expected");
        auto message = object["message"].asStringOpt().valueOrException("`message` string expected");
        co_await telegramPostMessage(chatId, message);
        co_return "Message sent successfully.";
    });

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
            self.mTemporaryContext << OpenAIChat::Message{
                .role = OpenAIChat::Message::Role::USER,
                .content = std::move(self.mNotifications.back()),
            };
            self.mNotifications.pop();

            for (auto it = self.mCachedDairy->begin(); it != self.mCachedDairy->end();) {
                if (!co_await self.dairyEntryIsRelatedToCurrentContext(*it)) {
                    ++it;
                    continue;
                }
                self.mTemporaryContext.last().content += "\n<kumi>\nAfter reading this notification, it reminded me my dairy page:\n" + *it + "\n</kumi>\n";
                it = self.mCachedDairy->erase(it);
            }

            OpenAIChat llm {
                .systemPrompt = config::SYSTEM_PROMPT,
                .tools = self.mTools.asJson,
            };

            naxyi:
            OpenAIChat::Response botAnswer = co_await llm.chat(self.mTemporaryContext);
            AUI_ASSERT(AThread::current() == self.getThread());

            self.mTemporaryContext << botAnswer.choices.at(0).message;

            if (botAnswer.choices.empty() || botAnswer.choices.at(0).message.tool_calls.empty()) {
                if (botAnswer.usage.total_tokens >= config::DAIRY_TOKEN_COUNT_TRIGGER) {
                    co_await self.dairyDumpMessages();
                }
                continue;
            }

            self.mTemporaryContext << co_await self.mTools.handleToolCalls(botAnswer.choices.at(0).message.tool_calls);
            AUI_ASSERT(AThread::current() == self.getThread());
            self.mTemporaryContext.last().content += "\nWhat's your next action? Use a `tool` to act. The following tools available: " + AStringVector(self.mTools.handlers.keyVector()).join(", ");
            goto naxyi;
        }
        co_return;
    }(*this);
}


/**
 * @brief Passes an event to the AI to process
 * @param notification notification text message in natural language (i.e., "you received a message from "...": ...; an
 * alarm triggerred, etc...)
 * @details
 * Think of it as your phone's notifications: you receive a notification, read it and (maybe) react to it.
 */
void AppBase::passEventToAI(AString notification) {
    mNotifications.push(std::move(notification));
    mNotificationsSignal.supplyValue();
}

AFuture<> AppBase::dairyDumpMessages() {
    AUI_DEFER { mCachedDairy.reset(); };
    if (mTemporaryContext.empty()) {
        co_return;
    }
    mTemporaryContext << OpenAIChat::Message{
        .role = OpenAIChat::Message::Role::USER,
        .content = config::DAIRY_PROMPT,
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
    dairySave(botAnswer.choices.at(0).message.content);
    mTemporaryContext.clear();
}

void AppBase::actProactively() {
    static std::default_random_engine re(std::time(nullptr));
    AString prompt;
    if (!mCachedDairy->empty()) {
        auto entry = mCachedDairy->begin() + re() % mCachedDairy->size();
        prompt = std::move(*entry);
        mCachedDairy->erase(entry);
        prompt += "\n\nYou opened a random diary page.\n\n";
    }
    prompt += "It's time to reflect on your thoughts!\n"
    " - maybe make some reasoning?\n"
    " - maybe do some reflection?\n"
    " - maybe write to a person and initiate a dialogue with #send_telegram_message?\n"
    "Act proactively!";
    passEventToAI(std::move(prompt));
}

AFuture<bool> AppBase::dairyEntryIsRelatedToCurrentContext(const AString& dairyEntry) {
    if (dairyEntry.empty()) {
        co_return false;
    }
    AString basePrompt = config::DAIRY_IS_RELATED_PROMPT;
    basePrompt += "\n<context>\n";
    AUI_ASSERT(!mTemporaryContext.empty());
    for (const auto& message: mTemporaryContext) {
        basePrompt += message.content + "\n\n";
    }
    basePrompt += "</context>\n";
    OpenAIChat chat {
        .systemPrompt = std::move(basePrompt),
    };
    auto decision = (co_await chat.chat(dairyEntry)).choices.at(0).message.content.lowercase();
    co_return decision.contains("yes") || decision.contains("y") || decision.contains("true") || decision.contains("1") || decision.contains("maybe");
}

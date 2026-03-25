//
// Created by alex2772 on 2/27/26.
//

#include "AppBase.h"

#include <random>
#include <range/v3/algorithm/any_of.hpp>
#include <range/v3/algorithm/remove_if.hpp>
#include <range/v3/algorithm/sort.hpp>
#include <range/v3/range/conversion.hpp>
#include <range/v3/view/take.hpp>
#include <range/v3/view/take_last.hpp>
#include <range/v3/view/transform.hpp>

#include "AUI/Logging/ALogger.h"
#include "AUI/Thread/AEventLoop.h"
#include "AUI/Thread/AThreadPool.h"
#include "AUI/Util/kAUI.h"
#include "OpenAIChat.h"
#include "config.h"
#include "KuniCharacter.h"
#include "util/cosine_similarity.h"

using namespace std::chrono_literals;

static constexpr auto LOG_TAG = "App";


AFuture<std::valarray<double>> contextEmbedding(ranges::range auto && rng) {
    AString basePrompt;
    AUI_ASSERT(!ranges::empty(rng));
    for (const OpenAIChat::Message& message: rng) {
        if (!message.reasoning.empty()) {
            basePrompt += message.reasoning;
            basePrompt += "\n\n";
        }
        if (!message.reasoning_content.empty()) {
            basePrompt += message.reasoning_content;
            basePrompt += "\n\n";
        }
        basePrompt += message.content;
        basePrompt += "\n\n---\n\n";
    }
    OpenAIChat chat{.config = config::ENDPOINT_EMBEDDING};
    return chat.embedding(basePrompt);
}

AppBase::AppBase(APath workingDir): mDiary(workingDir / "diary"), mWakeupTimer(_new<ATimer>(2h)) {
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
        // co_await self.mDiary.sleepingConsolidation();
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
            AUI_DEFER { notification.onProcessed.supplyValue(); };
            try {
                self.mNotifications.pop();
                self.mTemporaryContext << OpenAIChat::Message{
                    .role = OpenAIChat::Message::Role::USER,
                    .content = std::move(notification.message),
                };

                bool noopWarning = true;
                bool toolCallsHappened = false;


                // naxyi was here.
                // the reasons why I have moved it below diary lookup:
                // 1. Each lookup adds ~1s delay. So each time LLM uses send_telegram_message, there is a diary lookup.
                // 2. Once again send_telegram_message. Instead of one big message, LLM is encouraged to send multiple small
                //    messages instead (in the chatting culture the latter is more natural). When we insert occasional
                //    diary entries between LLMs send_telegram_message calls, it simply loses its focus and starts to spam
                //    with messages filled with random cues from the diary.
                //
                //    This feels like your participant has ADHD, and they can't finish their thought; instead they remember
                //    random fact from their sick brain and start yelling "DID YOU KNOW U SHOULD SHIT STANDING UPRIGHT"
                //    while didn't finish their explanation on why c++ is better than rust.
                bool pauseFlag = false;
                naxyi_populate_ctx:
                if (!self.mDiary.list().empty()) {
                    AString diary;

                    // performs scan on diary based on entire context.
                    // this will find common cues which are related to current conversation.
                    {
                        auto currentContext = co_await contextEmbedding(self.mTemporaryContext);
                        auto relatednesses = co_await self.mDiary.query(currentContext, {.confidenceFactor = 0.f});

                        for (const auto& i : relatednesses) {
                            const auto&[entryIt, relatedness] = i;
                            if (relatedness < self.mRelevanceThreshold) {
                                if (diary.empty()) {
                                    // relax threshold for future queries.
                                    self.mRelevanceThreshold = glm::mix(0.5f, float(relatedness), 0.9f);
                                }
                                break;
                            }
                            if (diary.length() > config::DIARY_INJECTION_MAX_LENGTH) {
                                // set the minimum constraint for the future queries
                                self.mRelevanceThreshold = relatedness;
                                break;
                            }
                            diary += self.takeDiaryEntry(i);
                        }
                    }

                    if (!diary.empty()) {
                        diary += self.mTemporaryContext.last().content;
                        self.mTemporaryContext.last().content = std::move(diary);
                    }
                }

                naxyi_preserve_ctx:
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
                    .systemPrompt = getSystemPrompt(),
                    .tools = notification.actions.asJson(),
                };

                OpenAIChat::Response botAnswer = co_await llm.chat(self.mTemporaryContext);
                AUI_ASSERT(AThread::current() == self.getThread());

                if (botAnswer.choices.empty() || botAnswer.choices.at(0).message.tool_calls.empty()) {
                    // no tool calls.
                    if (!botAnswer.choices.empty()) {
                        if (botAnswer.choices.at(0).message.content.contains("tool call #send_telegram_message")) {
                            // the bot misused tool calls capabilities. try again lol.
                            goto naxyi_populate_ctx;
                        }
                    }
                    ALogger::info(LOG_TAG) << "toolCallHappened=" << toolCallsHappened << " noopWarning=" << noopWarning;
                    if (!toolCallsHappened) {
                        if (std::exchange(noopWarning, false)) {
                            // punish llm for not performing tool calls.
                            ALogger::warn(LOG_TAG) << "LLM didn't perform any action.";
                            self.mTemporaryContext << OpenAIChat::Message{
                                .role = OpenAIChat::Message::Role::USER,
                                .content = "You didn't perform any action. Make sure you made tool calls."
                            };
                            goto naxyi_preserve_ctx;
                        }
                    }
                    // this is a normal response.
                    // we wont store it in temporary context because its excess noise.
                    goto finish;
                } else {
                    toolCallsHappened = true;
                }
                {
                    auto toolCalls = co_await notification.actions.handleToolCalls(botAnswer.choices.at(0).message.tool_calls);
                    if (ranges::any_of(toolCalls, [](const OpenAIChat::Message& msg) { return msg.content.contains(OpenAIChat::EMBEDDING_TAG); })) {
                        // Indicates a low quality tool call.
                        //
                        // This tag is used as an exception condition within a tool handler, and handled by AppBase.
                        // When caught, LLM's tool call appends to the user's last message, and user's last message will
                        // be sent again.
                        //
                        // This allows the feedback workflow: when a low quality message was passed to
                        // send_telegram_message, it can throw EMBEDDING_TAG to rollback before LLM's
                        // #send_telegram_message and slightly adjust LLM's following action. This differs from the
                        // standard AException workflow which is used for technical errors (such as you were banned, or
                        // no internet connection) whose are meaningful to LLM and it can adopt to.

                        if (botAnswer.usage.prompt_tokens > config::DIARY_TOKEN_COUNT_TRIGGER) {
                            // we are stuck; ignore the event
                            ALogger::warn("AppBase") << "LLM can't find proper response to the notification; "
                                                        "context is overflown. Ignoring event and dumping context";
                            co_await self.diaryDumpMessages();
                            continue;
                        }
                        goto naxyi_preserve_ctx;
                    }
                    self.mTemporaryContext << botAnswer.choices.at(0).message;
                    self.mTemporaryContext << std::move(toolCalls);
                    ALOG_DEBUG(LOG_TAG) << "Tool call response: " << self.mTemporaryContext.last().content;
                    AUI_ASSERT(AThread::current() == self.getThread());
                }

                if (pauseFlag) {
                    finish:
                    if (botAnswer.usage.total_tokens >= config::DIARY_TOKEN_COUNT_TRIGGER) {
                        co_await self.diaryDumpMessages();
                    }
                    continue;
                }
                if (!notification.actions.handlers().empty()) {
                    self.mTemporaryContext.last().content += "\nWhat's your next action? Use a `tool` to act. The following tools available: " + AStringVector(notification.actions.handlers().keyVector()).join(", ");
                }
                if (ranges::any_of(botAnswer.choices.at(0).message.tool_calls, [](const OpenAIChat::Message::ToolCall& t){ return t.function.name == "send_telegram_message"; })) {
                    goto naxyi_preserve_ctx;
                } else {
                    goto naxyi_populate_ctx;
                }
            } catch (const AException& e) {
                ALogger::err(LOG_TAG) << "Failed to process notification: \"" << notification.message << "\"" << e;
            }
        }
        co_return;
    }(*this);
}

const AFuture<>& AppBase::passNotificationToAI(AString notification, OpenAITools actions) {
    const auto& result = mNotifications.emplace(std::move(notification), std::move(actions)).onProcessed;
    mNotificationsSignal.supplyValue();
    return result;
}

AFuture<> AppBase::diaryDumpMessages() {
    // mDiary.reload(); // will find plagiarism against all entries. // commented out: exclude plagiarism checks for
    // included entries
    AUI_DEFER { mDiary.reload(); };
    if (mTemporaryContext.empty()) {
        co_return;
    }
    mTemporaryContext << OpenAIChat::Message{
        .role = OpenAIChat::Message::Role::USER,
        .content = config::DIARY_PROMPT,
    };

    OpenAIChat chat {
        .systemPrompt = getSystemPrompt(),
        // .tools = mTools.asJson, // no tools should be involved.
    };
    naxyi:
    OpenAIChat::Response botAnswer = co_await chat.chat(mTemporaryContext);
    if (botAnswer.choices.at(0).message.content.empty()) {
        goto naxyi;
    }
    mTemporaryContext << botAnswer.choices.at(0).message;
    auto id = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    auto message = botAnswer.choices.at(0).message.content;

    // stupid AI sometimes messes up with separators
    message.replaceAll("- --", "---");
    message.replaceAll("-- -", "---");
    auto split = message.split("---");

    if (ranges::any_of(split, [](const auto& s) { return s.length() > 3000; })) {
        mTemporaryContext << OpenAIChat::Message {
            .role = OpenAIChat::Message::Role::USER,
            .content = "One of your sections are too big. Shorten then and ensure correct division by \"---\".",
        };
    }

    for (const auto& take : split) {
        if (take.length() < 20) {
            continue; // random shit
        }
        auto embedding = co_await OpenAIChat{.config = config::ENDPOINT_EMBEDDING}.embedding(take);
        if (auto query = co_await mDiary.query(embedding, {.confidenceFactor = 0}); !query.empty()) {
            ALogger::info("AppBase") << "{}.md"_format(id) << ": plagiarism factor other_id=\"" << query.first().entry->id << "\" relatedness =" << float(query.first().relatedness);
            if (query.first().relatedness > config::DIARY_PLAGIARISM_THRESHOLD) {
                ALogger::info("AppBase") << "{}.md"_format(id) << ": won't store because it's plagiarism other_id=\"" << query.first().entry->id << "\"";
                continue;
            }
        }

        mDiary.save({
            .id = "{}"_format(id++),
            .metadata = {
                .embedding = std::move(embedding),
            },
            .freeformBody = std::move(take),
        });
    }
    mTemporaryContext.clear();
}

void AppBase::actProactively() {
    static std::default_random_engine re(std::time(nullptr));
    AString prompt = "<your_diary_page just_for_reasoning no_plagiarism no_copy>\n";
    if (!mDiary.list().empty()) {
        auto idx = re() % mDiary.list().size();
        auto entry = mDiary.list().begin();
        while (idx--) {
            entry++;
        }
        prompt += entry->freeformBody;
        mDiary.unload(entry);
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


void AppBase::removeNotifications(const AString& substring) {
    std::queue<Notification> remaining;
    while (!mNotifications.empty()) {
        auto n = std::move(mNotifications.front());
        mNotifications.pop();
        if (n.message.contains(substring)) {
            n.onProcessed.supplyException(std::make_exception_ptr(AException("notification was removed")));
            continue; // drop it
        }
        remaining.push(std::move(n));
    }
    mNotifications = std::move(remaining);
}

AString AppBase::takeDiaryEntry(const Diary::EntryExAndRelatedness& i) {
    i.entry->metadata.score += (i.relatedness - 0.5f) * 2.f;
    i.entry->incrementUsageCount();
    ALogger::info("AppBase") << "Loaded into context: " << i.entry->id << ".md relatedness=" << i.relatedness << "\n" << i.entry->freeformBody;
    auto formattedTag = "{} additional_context just_for_reasoning no_plagiarism no_copy"_format("your_diary_page");
    AString result = "<{}>\n{}\n</{}>\n"_format(formattedTag, i.entry->freeformBody, formattedTag);
    mDiary.unload(i.entry);
    return result;
}

AString AppBase::getSystemPrompt()
{
    return "{}\n\n<your_appearance>\n{}\n</your_appearance>"_format(kuni_character::getBasePrompt(), kuni_character::getAppearancePrompt());
}

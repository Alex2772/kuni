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
#include "IOpenAIChat.h"
#include "OpenAIChatImpl.h"
#include "config.h"
#include "MetricsBreadcumbs.h"
#include "WebSearch.h"
#include "AUI/IO/AFileInputStream.h"
#include "tools/ask.h"
#include "util/cosine_similarity.h"
#include "util/diary_save_entries.h"
#include "util/important_things_to_remember.h"

#include <range/v3/action/erase.hpp>

static std::default_random_engine re(std::time(nullptr));

using namespace std::chrono_literals;

static constexpr auto LOG_TAG = "App";
static const auto WORKING_MEMORY_PATH = "working_memory.md";


AFuture<std::valarray<double>> contextEmbedding(IOpenAIChat& openAI, ranges::range auto && rng) {
    ALOG_TRACE(LOG_TAG) << "contextEmbedding";
    AString basePrompt;
    AUI_ASSERT(!ranges::empty(rng));
    for (const IOpenAIChat::Message& message: rng) {
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
    co_return co_await openAI.embedding({ .config = config().embedding }, basePrompt);
}

AppBase::AppBase(Init init): mInit(std::move(init)), mDiary({
    .diaryDir = mInit.workingDir / "diary",
    .openAI = mInit.openAI,
}), mWakeupTimer(_new<ATimer>(27min)) {
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

    connect(mWakeupTimer->fired, [this] {
        if (std::uniform_real_distribution<double>(0.0, 1.0)(re) < 0.5) {
            return;
        }
        actProactively();
    });
    mWakeupTimer->start();

    getThread()->enqueue([&] {
        mAsync << [](AppBase& self) -> AFuture<> {
            // co_await self.mDiary.sleepingConsolidation();

            co_await self.onBeforeMainLoop();
            for (;;) {
                self.onOffline();
                if (self.mTemporaryContext.size() <= 1) {
                    // Alex2772 (Apr 19 2026):
                    // This approach is okay to revisit unfinished chats. However, if there are many unread chats,
                    // a long toolcall chain will occur, leading to context high usage and high processing costs.
                    // this happens because chat between C++ <-> Kuni's main LLM (mTemporaryContext) never gives
                    // turn to OpenAIChat::Role::USER, "conversation" happens between OpenAIChat::Role::ASSISTANT and
                    // OpenAIChat::Role::TOOL only. We ask to dump context on OpenAIChat::Role::USER's only.
                    //
                    // Solution: before infinite loop of this coroutine, send notifications on per-chat basis
                    // to read these chats (onBeforeMainLoop()).

                    try {
                        // this thing emulates "middle" memory of human - tasks, promises and other stuff
                        // in timespan 1-3d.
                        auto msg = co_await self.onCleanContext();
                        if (!msg.empty()) {
                            self.passNotificationToAI(std::move(msg), {}, true);
                        }
                    } catch (const AException& e) {
                        ALogger::err(LOG_TAG) << "Can't open " << (self.mInit.workingDir / WORKING_MEMORY_PATH) << ": "<< e;
                    }
                }
    #ifndef AUI_TESTS_MODULE
                if (config().randomlyGoSleep) {
                    if (std::uniform_real_distribution(0.0, 1.0)(re) < 0.1) {
                        // 1. randomly go afk is humane
                        // 2. reduce resource usage:
                        //    - less conversations would be made
                        //    - in case of group chats and telegram channels, messages would be processed in batches
                        const auto duration = std::chrono::minutes(std::uniform_int_distribution(15, 120)(re));
                        ALogger::info(LOG_TAG) << "Going to sleep for " << std::chrono::duration_cast<std::chrono::minutes>(duration).count() << " minutes";
                        self.mWakeup = false;
                        for (int i = 0; i < std::chrono::duration_cast<std::chrono::seconds>(duration).count(); ++i) {
                            // костыль ну да сойдёт
                            if (self.mWakeup) {
                                ALogger::info(LOG_TAG) << "Early wake up";
                                break;
                            }
                            co_await AThread::asyncSleep(1s);
                        }
                    }
                }
    #endif
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
                self.mNotifications.pop_front();
                self.mAskCalledThisTurn = false;
                notification.message += "\nCurrent time: {} UTC"_format(std::chrono::system_clock::now());
                notification.onStartedProcessing.supplyValue();
                AUI_DEFER { notification.onProcessed.supplyValue(); };
                try {
                    if (notification.actions.handlers().size() == 1) {
                        const auto& action = notification.actions.handlers().begin()->second;
                        if (action.name == "open" && action.parameters.properties.size() == 0) {
                            // shortcut/optimization: if the notification gives the only option to open it, there's no
                            // need to ask LLM whether it wants to open the notification because it does it
                            // in 100% cases.
                            // Also, this greatly fits in the current architecture, because we can't change notification
                            // text at runtime, BUT we can provide more recent data by giving the notification code
                            // control by calling "open()".
                            notification.message = co_await action.handler({
                                .tools = notification.actions,
                                .args = AJson {},
                                .allToolCalls = {},
                            });
                        }
                    }

                    self.mTemporaryContext << IOpenAIChat::Message{
                        .role = IOpenAIChat::Message::Role::USER,
                        .content = std::move(notification.message),
                    };

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
                            auto currentContext = co_await contextEmbedding(*self.openAI(), self.mTemporaryContext | ranges::view::take_last(3));
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
                                if (diary.length() >= config().diaryInjectionMaxLength) {
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
                    if (!self.mAskCalledThisTurn) {
                        // remind LLM to call #ask before responding.
                        // Injected as a system-level checkpoint so LLM sees it right before generating its next action.
                        self.mTemporaryContext.last().content +=
                            "\n[system] Have you called #ask yet this turn? "
                            "If the message involves personal topics, past events, questions, or people you know — "
                            "call #ask BEFORE send_telegram_message.";
                    }
                    auto escape = [&](OpenAITools::Ctx ctx) -> AFuture<AString> {
                        pauseFlag = true;
                        if (self.mActingProactively) {
                            // at the end of "actProactively", let's try to encourage LLM to write someone, still.
                            // if LLM's haven't written to anyone at this point, this notification will guide the LLM
                            // that dismissive action is not acceptable and LLM will try to revisit some older dialog
                            // despite no cue.
                            // if LLM actually have written to someone at this point, LLM will initiate a dialog with
                            // one more person.
                            self.passNotificationToAI("You should write someone else and be more proactive.", {}, true);
                        }
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
                    IOpenAIChat::Response botAnswer = co_await [&]() -> AFuture<IOpenAIChat::Response> {
                        MetricsBreadcumbs::Point metric(self.metricBreadcumbs(), "function", "notification processing loop");
                        auto response = self.openAI()->chatStreaming( {
                            .systemPrompt = getSystemPrompt(),
                            .tools = notification.actions.asJson(),
                        }, self.mTemporaryContext);
                        connect(response->response.changed, self, [&self](IOpenAIChat::Response response) {
                            self.onResponseAssembling(std::move(response));
                        });
                        co_await response->completed;
                        co_return std::move(*response->response);
                    }();
                    AUI_ASSERT(AThread::current() == self.getThread());

                    if (botAnswer.choices.empty() || botAnswer.choices.at(0).message.tool_calls.empty()) {
                        // no tool calls.
                        // each LLMs turn should end with "wait" or "pause"
                        ALogger::warn(LOG_TAG) << "LLM didn't perform any action.";
                        if (!botAnswer.choices.empty()) {
                            // guiderails to make LLM tool-centric.
                            const auto& content = botAnswer.choices.at(0).message.content;
                            if (content.contains("#send_telegram_message")) {
                                // qwen3.5 bug: misused examples
                                self.mTemporaryContext << IOpenAIChat::Message{
                                    .role = IOpenAIChat::Message::Role::USER,
                                    .content = "Nice thoughts! However you should be tool-centric. Make sure you "
                                    "made tool calls. The message you provided is not visible to anyone but you.",
                                };
                                goto naxyi_preserve_ctx;
                            }
                            if (content.contains("<message") && content.contains("</message>")) {
                                // gemma4 bug: does not perform tool calls, instead, replies with the following content
                                // <message message_id=\"8759834210\" date=\"2026-04-16 01:56:10\" sender=\"You (Kuni)\">
                                // Ой, и что же ты там читаешь? Надеюсь, только самое милое! 😼✨
                                // </message>

                                self.mTemporaryContext << IOpenAIChat::Message{
                                    .role = IOpenAIChat::Message::Role::USER,
                                    .content = "Nice thoughts! However you should be tool-centric. Make sure you "
                                    "made tool calls. The message you provided is not visible to anyone but you. Call "
                                    "#wait if you are unsure.",
                                };
                                goto naxyi_preserve_ctx;
                            }
                        }
                        // punish llm for not performing tool calls.
                        self.mTemporaryContext << IOpenAIChat::Message{
                            .role = IOpenAIChat::Message::Role::USER,
                            .content = "Nice thoughts! However you should be tool-centric. Make sure you "
                            "made tool calls. The message you provided is not visible to anyone but you. Call #wait if "
                            "you are unsure.",
                        };
                        goto naxyi_preserve_ctx;
                    }
                    {
                        auto toolCalls = co_await notification.actions.handleToolCalls(botAnswer.choices.at(0).message.tool_calls, self.metricBreadcumbs());
                        if (ranges::any_of(toolCalls, [](const IOpenAIChat::Message& msg) { return msg.content.contains(IOpenAIChat::EMBEDDING_TAG); })) {
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

                            if (botAnswer.usage.prompt_tokens > config().diaryTokenCountTrigger) {
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
                        if (botAnswer.usage.total_tokens >= config().diaryTokenCountTrigger) {
                            co_await self.diaryDumpMessages();
                        }
                        continue;
                    }
                    if (!notification.actions.handlers().empty()) {
                        self.mTemporaryContext.last().content += "\nWhat's your next action? Use a `tool` to act. Use #ask to consult with your knowledge database. The following tools available: " + AStringVector(notification.actions.handlers().keyVector()).join(", ");
                    }
                    if (ranges::any_of(botAnswer.choices.at(0).message.tool_calls, [](const IOpenAIChat::Message::ToolCall& t){ return t.function.name == "send_telegram_message"; })) {
                        // if LLM sent a message without ever calling #ask this turn,
                        // inject a reminder into the next turn's context.
                        if (!self.mAskCalledThisTurn) {
                            self.mTemporaryContext.last().content +=
                                "\n[system] Note: you sent a message without consulting #ask this turn. "
                                "Next time, call #ask before send_telegram_message to enrich your response "
                                "with memories and context.";
                        }
                        goto naxyi_preserve_ctx;
                    } else {
                        goto naxyi_populate_ctx;
                    }
                } catch (const AException& e) {
                    ALogger::err(LOG_TAG) << "Failed to process notification: \"" << notification.message << "\"" << e;
                    if (e.getMessage().lowercase().contains("json")) {
                        // If there's a JSON error, it means we have irreversibly damaged context. Best way to solve this
                        // is to drop the temporary context entirery.
                        ALogger::warn("AppBase") << "Context is damaged. Dropping context";
                        self.mTemporaryContext.clear();
                    }
                }
            }
            co_return;
        }(*this);
    });
}

const AppBase::Notification& AppBase::passNotificationToAI(AString notification, OpenAITools actions, bool first) {
    ALOG_TRACE(LOG_TAG) << "passNotificationToAI";
    const auto& result = *mNotifications.emplace(first ? mNotifications.begin() : mNotifications.end(), std::move(notification), std::move(actions));
    mNotificationsSignal.supplyValue();
    return result;
}

AFuture<> AppBase::diaryDumpMessages() {
    MetricsBreadcumbs::Point metric(metricBreadcumbs(), "function", "diaryDumpMessages");
    ALOG_TRACE(LOG_TAG) << "diaryDumpMessages";
    // mDiary.reload(); // will find plagiarism against all entries. // commented out: exclude plagiarism checks for
    // included entries
    AUI_DEFER { mDiary.reload(); };
    if (mTemporaryContext.empty()) {
        co_return;
    }
    AString previousWorkingMemory;
    if ((mInit.workingDir / WORKING_MEMORY_PATH).isRegularFileExists()) {
        AByteBuffer buf;
        buf << AFileInputStream(mInit.workingDir / WORKING_MEMORY_PATH);
        previousWorkingMemory = AStringView(buf.data(), buf.size());
    }
    auto importantThingsToRemember = util::importantThingsToRemember(*openAI(), mTemporaryContext, previousWorkingMemory);

    co_await util::diarySaveEntries(mDiary, mTemporaryContext, {
        .systemPrompt = getSystemPrompt(),
        // no tools should be involved.
    });
    {
        // do it in separate lines: first, we wait for LLM response, second, we overwrite file (destructive operation).
        auto workingMemoryMd = co_await importantThingsToRemember;
        AFileOutputStream(mInit.workingDir / WORKING_MEMORY_PATH) << workingMemoryMd;
    }
    mTemporaryContext.clear();
}

void AppBase::actProactively() {
    ALOG_TRACE(LOG_TAG) << "actProactively";
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
  - maybe write to a person and initiate a dialogue? whom you would like to write? maybe call #get_telegram_chats? You
    can open one chat at a time - choose wisely!\n"
Act proactively!
)";
    const auto& notification = passNotificationToAI(std::move(prompt));
    struct State {
        AOptional<MetricsBreadcumbs::Point> metric;
    };
    auto state = _new<State>();
    notification.onStartedProcessing.onSuccess([this, state] {
        mActingProactively = true;
        state->metric.emplace(metricBreadcumbs(), "function", "actProactively");
    });
    notification.onProcessed.onSuccess([this, state] {
        mActingProactively = false;
        state->metric = std::nullopt;
    });
}

AFuture<AString> AppBase::onCleanContext() {
    if ((mInit.workingDir / WORKING_MEMORY_PATH).isRegularFileExists()) {
        AByteBuffer workingMemory;
        workingMemory << AFileInputStream(mInit.workingDir / WORKING_MEMORY_PATH);
        co_return R"(<things_to_remember>
{}
</things_to_remember>
<instructions>
Your behaviour must be highly influenced by "physical state" and "emotional state" mentioned above.

<example>
Emotional state: anger
...
send_telegram_message("text":"иди нахуй заебал")
</example>
<example>
Emotional state: amused
...
send_telegram_message("text":"мррр~")
</example>
</instruction>
)"_format(AStringView(workingMemory.data(), workingMemory.size()));
    }
    co_return "";
}


void AppBase::updateTools(OpenAITools& actions) {
    ALOG_TRACE(LOG_TAG) << "updateTools";
    actions.insert(tools::ask([this] { return mTemporaryContext.empty() ? AString{} : mTemporaryContext.last().content; }, openAI(), mDiary));
    actions.onAfterToolCall = [this](const AString& toolName) {
        if (toolName == "wait") {
            return;
        }
        if (toolName == "pause") {
            return;
        }
        if (toolName == "ask") {
            mAskCalledThisTurn = true;
        }
        auto labels = metricBreadcumbs()->value();
        emit toolCallFired(AppBase::ToolCallEvent{
            .toolName = toolName,
            .breadcrumbLabels = std::move(labels),
            .lastOpenedChatLastMessageTime = mLastOpenedChatLastMessageTime.map([](std::chrono::system_clock::time_point t) {
                return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now() - t);
            }),
        });
    };

}

void AppBase::removeNotifications(const AString& substring) {
    ALOG_TRACE(LOG_TAG) << "removeNotifications: " << substring;
    mNotifications.erase(ranges::remove_if(mNotifications, [&](const Notification& n) {
        return n.message.contains(substring);
    }), mNotifications.end());
}

AString AppBase::takeDiaryEntry(const Diary::EntryExAndRelatedness& i) {
    ALOG_TRACE(LOG_TAG) << "takeDiaryEntry: " << i.entry->id;
    if (ranges::any_of(mTemporaryContext, [&](const IOpenAIChat::Message& m) {
        return m.content.contains(i.entry->freeformBody);
    })) {
        // if mTemporaryContext already contains this diary entry verbatim - we don't need to reinclude it - it makes
        // no sense to consume tokens for the same thing.
        //
        // the copypasted diary entry would not receive score.
        return {};
    }

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
    auto prompt = "{}\n\n{}\n\n<your_appearance>\n{}\n</your_appearance>"_format(prompts().system, prompts().characterBase, prompts().characterAppearance);
    if (config().lockdown != Config::LockdownMode::NONE) {
        prompt += "\nYou are currently under lockdown mode. This means you can access to subset of chats only.";
    }
    return prompt;
}

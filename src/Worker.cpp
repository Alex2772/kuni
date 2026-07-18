//
// Created by alex2772 on 7/14/26.
//

#include "Worker.h"

#include <range/v3/all.hpp>

#include "AppBase.h"
#include "NotificationManager.h"
#include "AppBase.h"
#include "IOpenAIChat.h"

#include <random>

static constexpr auto LOG_TAG = "Worker";

using namespace std::chrono_literals;

extern std::default_random_engine gRandomEngine;


AFuture<std::valarray<double>> contextEmbedding(ALogger& logger, IOpenAIChat& openAI, ranges::range auto&& rng) {
    logger.trace(LOG_TAG) << "contextEmbedding";
    AString basePrompt;
    AUI_ASSERT(!ranges::empty(rng));
    for (const IOpenAIChat::Message& message : rng) {
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

[[nodiscard]]
static AFuture<> processRandomlyGoSleep(ALogger& logger, bool& wakeUp) {
    if (config().randomlyGoSleep) {
        if (std::uniform_real_distribution(0.0, 1.0)(gRandomEngine) < 0.01) {
            // 1. randomly go afk is humane
            // 2. reduce resource usage:
            //    - less conversations would be made
            //    - in case of group chats and telegram channels, messages would be processed in batches
            const auto duration = std::chrono::minutes(std::uniform_int_distribution(15, 120)(gRandomEngine));
            logger.info(LOG_TAG)
                << "Going to sleep for " << std::chrono::duration_cast<std::chrono::minutes>(duration).count() << " minutes";
            wakeUp = false;
            for (int i = 0; i < std::chrono::duration_cast<std::chrono::seconds>(duration).count(); ++i) {
                // костыль ну да сойдёт
                if (wakeUp) {
                    logger.info(LOG_TAG) << "Early wake up";
                    break;
                }
                co_await AThread::asyncSleep(1s);
            }
        }
    }
}

[[nodiscard]]
static AFuture<> processShortcutOpen(NotificationManager::Notification& notification, const IOpenAIChat::Session& temporaryContext) {
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
              .temporaryContext = temporaryContext,
              .allToolCalls = {},
            });
        }
    }
}

[[nodiscard]]
static bool
processIgnoreChance(ALogger& logger, IOpenAIChat::Session& temporaryContext, bool& canIgnore, const IOpenAIChat::Message& lastLLMResponse) {
#ifdef AUI_TESTS_MODULE
    if (std::uniform_real_distribution(0.f, 1.f)(gRandomEngine) < config().suggestIgnoreChance) {   // attempt to make
                                                                                                    // LLM lazy and
                                                                                                    // ignore message :)
        // if (std::exchange(canIgnore, false))
        {   // avoid subsequent knockbacks
            for (const auto& tc : lastLLMResponse.tool_calls) {
                if (tc.function.name == "wait" || tc.function.name == "pause") {
                    return false;
                }
                temporaryContext << IOpenAIChat::Message {
                    .role = IOpenAIChat::Message::Role::TOOL,
                    .content =
                        "Error: do you really want to continue? Think again; repeat `{}` to continue or call wait() to finish."_format(
                            AStringView(tc.function.name)),
                    .tool_call_id = tc.id,
                };
            }
            logger.info(LOG_TAG) << "Begging LLM to be lazy (ignore message)";
            return true;
        }
    }
#endif
    return false;
}

AFuture<> Worker::handleNotification(std::shared_ptr<bool> alive, NotificationManager::Notification notification) {
#ifndef AUI_TESTS_MODULE
    co_await processRandomlyGoSleep(mLogger, mWakeUp);
#endif
    AUI_ASSERT(AThread::current() == getThread());
    AUI_DEFER { mApp.onOffline(); };
    AUI_ASSERT(*alive);
    mAskCalledThisTurn = false;
    try {
        bool canIgnore = true;
        co_await processShortcutOpen(notification, mTemporaryContext);

        mLogger.info(LOG_TAG) << "Processing notification: " << notification.message;

        mTemporaryContext << IOpenAIChat::Message {
            .role = IOpenAIChat::Message::Role::USER,
            .content = std::move(notification.message),
        };

        // naxyi was here.
        // the reasons why I have moved it below diary lookup:
        // 1. Each lookup adds ~1s delay. So each time LLM uses send_telegram_message, there is a diary
        // lookup.
        // 2. Once again send_telegram_message. Instead of one big message, LLM is encouraged to send
        // multiple small
        //    messages instead (in the chatting culture the latter is more natural). When we insert
        //    occasional diary entries between LLMs send_telegram_message calls, it simply loses its
        //    focus and starts to spam with messages filled with random cues from the diary.
        //
        //    This feels like your participant has ADHD, and they can't finish their thought; instead
        //    they remember random fact from their sick brain and start yelling "DID YOU KNOW U SHOULD
        //    SHIT STANDING UPRIGHT" while didn't finish their explanation on why c++ is better than
        //    rust.
        bool pauseFlag = false;
    naxyi_populate_ctx:
        if (!mApp.diary().list().empty()) {
            AString diary;

            // performs scan on diary based on entire context.
            // this will find common cues which are related to current conversation.
            if (config().diaryInjectionMaxLength > 0) {
                auto currentContext =
                    co_await contextEmbedding(mLogger, *mApp.openAI(), mTemporaryContext | ranges::view::take_last(3));
                auto relatednesses = co_await mApp.diary().query(currentContext, { .confidenceFactor = 0.f });

                for (const auto& i : relatednesses) {
                    const auto& [entryIt, relatedness] = i;
                    if (relatedness < mRelevanceThreshold) {
                        if (diary.empty()) {
                            // relax threshold for future queries.
                            mRelevanceThreshold = glm::mix(0.5f, float(relatedness), 0.9f);
                        }
                        break;
                    }
                    if (diary.length() >= config().diaryInjectionMaxLength) {
                        // set the minimum constraint for the future queries
                        mRelevanceThreshold = relatedness;
                        break;
                    }
                    diary += takeDiaryEntry(i);
                }
            }

            if (!diary.empty()) {
                diary += mTemporaryContext.last().content;
                mTemporaryContext.last().content = std::move(diary);
            }
        }

    naxyi_preserve_ctx:
        updateTools(notification.actions);
        if (!mAskCalledThisTurn) {
            // remind LLM to call #ask before responding.
            // Injected as a system-level checkpoint so LLM sees it right before generating its next
            // action.
            if (config().remindUseAsk) {
                mTemporaryContext.last().content +=
                    "\n[system] Have you called #ask yet this turn? "
                    "If the message involves personal topics, past events, questions, or people you "
                    "know — "
                    "call #ask BEFORE send_telegram_message.";
            }
        }
        auto escape = [&](OpenAITools::Ctx ctx) -> AFuture<AString> {
            pauseFlag = true;
            if (mApp.isActingProactively()) {
                // at the end of "actProactively", let's try to encourage LLM to write someone, still.
                // if LLM's haven't written to anyone at this point, this notification will guide the
                // LLM that dismissive action is not acceptable and LLM will try to revisit some older
                // dialog despite no cue. if LLM actually have written to someone at this point, LLM
                // will initiate a dialog with one more person.
                mApp.notificationManager().passNotificationToAI({
                    .message = "You should write someone else and be more proactive.",
                    .pin = "<act_proactively />",
                });
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
            MetricsBreadcumbs::Point metric(mApp.metricBreadcumbs(), "function", "notification processing loop");
            auto response = mApp.openAI()->chatStreaming(
                {
                  .systemPrompt = mApp.getSystemPrompt(),
                  .tools = notification.actions.asJson(),
                },
                mTemporaryContext);
            connect(response->response.changed, mApp, [&](IOpenAIChat::Response response) {
                mApp.onResponseAssembling(std::move(response));
            });
            co_await response->completed;
            co_return std::move(*response->response);
        }();
        AUI_ASSERT(AThread::current() == getThread());

        if (botAnswer.choices.empty() || botAnswer.choices.at(0).message.tool_calls.empty()) {
            // no tool calls.
            // each LLMs turn should end with "wait" or "pause"
            mLogger.warn(LOG_TAG) << "LLM didn't perform any action.";
            if (!botAnswer.choices.empty()) {
                // guiderails to make LLM tool-centric.
                const auto& content = botAnswer.choices.at(0).message.content;
                if (content.contains("#send_telegram_message")) {
                    // qwen3.5 bug: misused examples
                    mTemporaryContext << IOpenAIChat::Message {
                        .role = IOpenAIChat::Message::Role::USER,
                        .content =
                            "Nice thoughts! However you should be tool-centric. Make sure you "
                            "made tool calls. The message you provided is not visible to anyone but "
                            "you.",
                    };
                    goto naxyi_preserve_ctx;
                }
                if (content.contains("<message") && content.contains("</message>")) {
                    // gemma4 bug: does not perform tool calls, instead, replies with the following
                    // content <message message_id=\"8759834210\" date=\"2026-04-16 01:56:10\"
                    // sender=\"You (Kuni)\"> Ой, и что же ты там читаешь? Надеюсь, только самое милое!
                    // 😼✨
                    // </message>

                    mTemporaryContext << IOpenAIChat::Message {
                        .role = IOpenAIChat::Message::Role::USER,
                        .content =
                            "Nice thoughts! However you should be tool-centric. Make sure you "
                            "made tool calls. The message you provided is not visible to anyone but "
                            "you. Call "
                            "#wait if you are unsure.",
                    };
                    goto naxyi_preserve_ctx;
                }
            }
            // punish llm for not performing tool calls.
            mTemporaryContext << IOpenAIChat::Message {
                .role = IOpenAIChat::Message::Role::USER,
                .content =
                    "Nice thoughts! However you should be tool-centric. Make sure you "
                    "made tool calls. The message you provided is not visible to anyone but you. Call "
                    "#wait if "
                    "you are unsure.",
            };
            goto naxyi_preserve_ctx;
        }

        if (processIgnoreChance(mLogger, mTemporaryContext, canIgnore, botAnswer.choices.at(0).message)) {
            goto naxyi_preserve_ctx;
        }

        {
            auto toolCalls = co_await notification.actions.handleToolCalls(
                botAnswer.choices.at(0).message.tool_calls, mApp.metricBreadcumbs(), mTemporaryContext, mLogger);
            if (ranges::any_of(toolCalls, [](const IOpenAIChat::Message& msg) {
                    return msg.content.contains(IOpenAIChat::EMBEDDING_TAG);
                })) {
                // Indicates a low quality tool call.
                //
                // This tag is used as an exception condition within a tool handler, and handled by
                // AppBase. When caught, LLM's tool call appends to the user's last message, and user's
                // last message will be sent again.
                //
                // This allows the feedback workflow: when a low quality message was passed to
                // send_telegram_message, it can throw EMBEDDING_TAG to rollback before LLM's
                // #send_telegram_message and slightly adjust LLM's following action. This differs from
                // the standard AException workflow which is used for technical errors (such as you were
                // banned, or no internet connection) whose are meaningful to LLM and it can adopt to.

                if (botAnswer.usage.prompt_tokens > config().diaryTokenCountTrigger) {
                    // we are stuck; ignore the event
                    mLogger.warn("AppBase")
                        << "LLM can't find proper response to the notification; "
                           "context is overflown. Ignoring event and dumping context";
                    co_await diaryDumpMessages();
                    co_return;
                }
                goto naxyi_preserve_ctx;
            }
            mTemporaryContext << botAnswer.choices.at(0).message;
            mTemporaryContext << std::move(toolCalls);
            mLogger.info(LOG_TAG) << "Tool call response: " << mTemporaryContext.last().content;
            AUI_ASSERT(AThread::current() == getThread());
        }

        if (pauseFlag) {
        finish:
            if (botAnswer.usage.total_tokens >= config().diaryTokenCountTrigger) {
                co_await diaryDumpMessages();
            }
            co_return;
        }
        if (!notification.actions.handlers().empty()) {
            mTemporaryContext.last().content +=
                "\nWhat's your next action? Use a `tool` to act. Use #ask to consult with your "
                "knowledge database. The following tools available: " +
                AStringVector(notification.actions.handlers().keyVector()).join(", ");
        }
        if (ranges::any_of(botAnswer.choices.at(0).message.tool_calls, [](const IOpenAIChat::Message::ToolCall& t) {
                return t.function.name == "send_telegram_message";
            })) {
            // if LLM sent a message without ever calling #ask this turn,
            // inject a reminder into the next turn's context.
            if (!mAskCalledThisTurn && config().remindUseAsk) {
                mTemporaryContext.last().content +=
                    "\n[system] Note: you sent a message without consulting #ask this turn. "
                    "Next time, call #ask before send_telegram_message to enrich your response "
                    "with memories and context.";
            }
            goto naxyi_preserve_ctx;
        } else {
            goto naxyi_populate_ctx;
        }
    } catch (const AException& e) {
        mLogger.err(LOG_TAG) << "Failed to process notification: \"" << notification.message << "\"" << e;
        if (e.getMessage().lowercase().contains("json")) {
            // If there's a JSON error, it means we have irreversibly damaged context. Best way to solve
            // this is to drop the temporary context entirery.
            mLogger.warn("AppBase") << "Context is damaged. Dropping context";
            mTemporaryContext.clear();
        }
    }
}

Worker::Worker(size_t name, AppBase& app): mName(name), mApp(app) {
    mAliveToken = _new<bool>(true);

    getThread()->enqueue([=, alive = mAliveToken] {
        if (!*alive)
            return;
        mCoroutine = mApp.notificationManager().run(mWorkerPins, [=](NotificationManager::Notification notification) -> AFuture<bool> {
            co_await handleNotification(alive, std::move(notification));
            co_return *alive;
        });
    });
}

Worker::~Worker() { *mAliveToken = false; }

AString Worker::takeDiaryEntry(const Diary::EntryExAndRelatedness& i) {
    mLogger.trace(LOG_TAG) << "takeDiaryEntry: " << i.entry->id;
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
    mLogger.info("AppBase") << "Loaded into context: " << i.entry->id << ".md relatedness=" << i.relatedness << "\n" << i.entry->freeformBody;
    auto formattedTag = "{} additional_context just_for_reasoning no_plagiarism no_copy"_format("your_diary_page");
    AString result = "<{}>\n{}\n</{}>\n"_format(formattedTag, i.entry->freeformBody, formattedTag);
    mApp.diary().unload(i.entry);
    return result;
}

void Worker::updateTools(OpenAITools& tools) {
    mApp.updateTools(tools, mTemporaryContext);
    tools.onAfterToolCall << [this](const AString& toolName) {
        if (toolName == "ask") {
            mAskCalledThisTurn = true;
        }
    };
}

AFuture<> Worker::diaryDumpMessages() {
    mWorkerPins.clear();
    co_await mApp.diaryDumpMessages(mTemporaryContext);
}

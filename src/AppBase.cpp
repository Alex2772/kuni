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
#include "WebSearch.h"
#include "util/cosine_similarity.h"

#include <range/v3/action/erase.hpp>

static std::default_random_engine re(std::time(nullptr));

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
    co_return co_await chat.embedding(basePrompt);
}

AppBase::AppBase(APath workingDir): mDiary(workingDir / "diary"), mWakeupTimer(_new<ATimer>(200min)) {
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
                if (self.mTemporaryContext.size() <= 1 && self.mNotifications.empty()) {
                    if (std::uniform_real_distribution(0.0, 1.0)(re) < 0.1) {
                        // revisit chats when Kuni does nothing.

                        // Alex2772 (Apr 19 2026):
                        // This approach is okay to revisit unfinished chats. However, if there are many unread chats,
                        // a long toolcall chain will occur, leading to context high usage and high processing costs.
                        // this happens because chat between C++ <-> Kuni's main LLM (mTemporaryContext) never gives
                        // turn to OpenAIChat::Role::USER, "conversation" happens between OpenAIChat::Role::ASSISTANT and
                        // OpenAIChat::Role::TOOL only. We ask to dump context on OpenAIChat::Role::USER's only.
                        //
                        // Solution: before infinite loop of this coroutine, send notifications on per-chat basis
                        // to read these chats (onBeforeMainLoop()).
                        self.passNotificationToAI("Check your chats.", {}, true);
                    }
                }
    #ifndef AUI_TESTS_MODULE
                if constexpr (config::RANDOMLY_GO_SLEEP) {
                    if (std::uniform_real_distribution(0.0, 1.0)(re) < 0.1) {
                        // 1. randomly go afk is humane
                        // 2. reduce resource usage:
                        //    - less conversations would be made
                        //    - in case of group chats and telegram channels, messages would be processed in batches
                        const auto minutes = std::uniform_int_distribution(15, 60)(re);
                        ALogger::info(LOG_TAG) << "Going to sleep for " << minutes << " minutes";
                        for (int i = 0; i < minutes; ++i) {
                            // костыль ну да сойдёт
                            if (!self.mNotifications.empty()) {
                                if (self.mNotifications.front().message.contains("{}"_format(config::PAPIK_CHAT_ID))) {
                                    ALogger::info(LOG_TAG) << "Daddy woke me up";
                                    break;
                                }
                            }
                            co_await AThread::asyncSleep(1min);
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
                            notification.message = co_await action.handler({notification.actions, AJson {}});
                        }
                    }

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
                            // guiderails to make LLM tool-centric.
                            const auto& content = botAnswer.choices.at(0).message.content;
                            if (content.contains("#send_telegram_message")) {
                                // qwen3.5 bug: misused examples
                                if (std::exchange(noopWarning, false)) {
                                    self.mTemporaryContext << OpenAIChat::Message{
                                        .role = OpenAIChat::Message::Role::USER,
                                        .content = "You should be tool-centric. Make sure you made tool calls. The message "
                                        "you provided is not visible to anyone but you.",
                                    };
                                    goto naxyi_preserve_ctx;
                                }
                            }
                            if (content.contains("<message") && content.contains("</message>")) {
                                // gemma4 bug: does not perform tool calls, instead, replies with the following content
                                // <message message_id=\"8759834210\" date=\"2026-04-16 01:56:10\" sender=\"You (Kuni)\">
                                // Ой, и что же ты там читаешь? Надеюсь, только самое милое! 😼✨
                                // </message>

                                if (std::exchange(noopWarning, false)) {
                                    self.mTemporaryContext << OpenAIChat::Message{
                                        .role = OpenAIChat::Message::Role::USER,
                                        .content = "You should be tool-centric. Make sure you made tool calls. The message "
                                        "you provided is not visible to anyone but you.",
                                    };
                                    goto naxyi_preserve_ctx;
                                }
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
                        self.mTemporaryContext.last().content += "\nWhat's your next action? Use a `tool` to act. Use #ask_diary to consult with your knowledge database. The following tools available: " + AStringVector(notification.actions.handlers().keyVector()).join(", ");
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
    });
}

const AppBase::Notification& AppBase::passNotificationToAI(AString notification, OpenAITools actions, bool first) {
    const auto& result = *mNotifications.emplace(first ? mNotifications.begin() : mNotifications.end(), std::move(notification), std::move(actions));
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
    notification.onStartedProcessing.onSuccess([&] {
        mActingProactively = true;
    });
    notification.onProcessed.onSuccess([&] {
        mActingProactively = false;
    });
}


void AppBase::updateTools(OpenAITools& actions) {
    actions.insert({
        .name = "ask_diary",
        .description = "Consult with Kuni's main knowledge database (subagent). Use this to retrieve additional pages from diary. USE THIS PROACTIVELY.",
        .parameters = {
            .properties =
                {
                    {"query", {.type = "string", .description = "Freeform question to diary. Provide as much context as possible."}},
                },
            .required = {"query"},
        },
        .handler = [this](OpenAITools::Ctx ctx) -> AFuture<AString> {
            auto query = ctx.args["query"].asStringOpt().valueOrException("\"query\" string is required");
            if (query.length() < 60) {
                // Alex2772 16-04-2026:
                // changed from throw AException to co_return.
                // AException is a technical error and the engine would load additional diary entries
                // based on embedding search, which LLM might mistakenly interpret as a success call to ask_diary,
                // losing guiderail to provide more context.
                // if we return the string as is, the engine would not include diary entries; so the llm
                // will see a clean response guiding it to provide more context.
                /* throw AException */ co_return (R"(error: too short query! please provide more context to ask_diary:
- chat name (if any)
- previous messages
- sender's name
- search cues
- source event
- everything else to populate query
)");
            }
            co_return (co_await mDiary.queryAI(query, {.confidenceFactor = 0.f})) + "\nIf response above is dismissive, try rephrasing your query and include other details";
        },
    });
    actions.insert({
        .name = "ask_google",
        .description = "Perform web search subagent",
        .parameters = {
            .properties =
                {
                    {"query", {.type = "string", .description = "Question to ask the subagent. Not a standard search engine query; form a question in natural language"}},
                },
            .required = {"query"},
        },
        .handler = [this](OpenAITools::Ctx ctx) -> AFuture<AString> {
            auto query = ctx.args["query"].asStringOpt().valueOrException("\"query\" string is required");
            co_return "{}\nPretend you didn't Google the information on the Internet."_format(co_await web::searchAI(query));
        },
    });
}

void AppBase::removeNotifications(const AString& substring) {
    mNotifications.erase(ranges::remove_if(mNotifications, [&](const Notification& n) {
        return n.message.contains(substring);
    }), mNotifications.end());
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

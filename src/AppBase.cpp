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
#include "util/cosine_similarity.h"

using namespace std::chrono_literals;

static constexpr auto LOG_TAG = "App";


AFuture<std::valarray<float>> contextEmbedding(ranges::range auto && rng) {
    AString basePrompt;
    AUI_ASSERT(!ranges::empty(rng));
    for (const auto& message: rng) {
        basePrompt += message.content + "\n\n";
    }
    OpenAIChat chat{};
    return chat.embedding(basePrompt);
}

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
            AUI_DEFER { notification.onProcessed.supplyValue(); };
            self.mNotifications.pop();
            self.mTemporaryContext << OpenAIChat::Message{
                .role = OpenAIChat::Message::Role::USER,
                .content = std::move(notification.message),
            };


            naxyi_populate_ctx:

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
            if (!self.mCachedDiary->empty()) {
                AString diary;

                auto pickup = [&](const DiaryEntryExAndRelatedness& i, AStringView tag = "your_diary_page") {
                    i.entry->metadata.score += i.relatedness;
                    i.entry->incrementUsageCount();
                    self.diarySave(*i.entry);
                    diary += "<{} additional_context just_for_reasoning no_plagiarism no_copy>\n{}\n</{}>\n"_format(tag, i.entry->freeformBody, tag);
                    self.mCachedDiary->erase(i.entry);
                };

                // performs scan on diary based on entire context.
                // this will find common cues which are related to current conversation.
                {
                    auto currentContext = co_await contextEmbedding(self.mTemporaryContext);
                    auto relatednesses = co_await self.diaryQuery(currentContext);

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
                        pickup(i);
                    }
                }

                // address the last 1-2 entries from the temporary context.
                // this includes original notification or follow-up from tool responses (i.e., result of reading chat).
                // this helps switching between unrelated contexts.
                {
                    auto query = co_await contextEmbedding(self.mTemporaryContext | ranges::view::take_last(2));
                    auto relatednesses = co_await self.diaryQuery(query);
                    for (const auto& i : relatednesses | ranges::view::take(2)) {
                        pickup(i, "kuni's_random_thought");
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
                .systemPrompt = config::SYSTEM_PROMPT,
                .tools = notification.actions.asJson(),
            };

            OpenAIChat::Response botAnswer = co_await llm.chat(self.mTemporaryContext);
            AUI_ASSERT(AThread::current() == self.getThread());

            self.mTemporaryContext << botAnswer.choices.at(0).message;

            if (botAnswer.choices.empty() || botAnswer.choices.at(0).message.tool_calls.empty()) {
                goto finish;
            }

            self.mTemporaryContext << co_await notification.actions.handleToolCalls(botAnswer.choices.at(0).message.tool_calls);
            ALOG_DEBUG(LOG_TAG) << "Tool call response: " << self.mTemporaryContext.last().content;
            AUI_ASSERT(AThread::current() == self.getThread());

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
    auto id = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    for (const auto& take : botAnswer.choices.at(0).message.content.split("---")) {
        if (take.length() < 20) {
            continue; // random shit
        }
        auto embedding = co_await chat.embedding(take);
        if (auto query = co_await diaryQuery(embedding); !query.empty()) {
            ALogger::info("AppBase") << "{}.md"_format(id) << ": plagiarism factor other_id=\"" << query.first().entry->id << "\" relatedness =" << float(query.first().relatedness);
        }

        diarySave({
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
    if (!mCachedDiary->empty()) {
        auto idx = re() % mCachedDiary->size();
        auto entry = mCachedDiary->begin();
        while (idx--) {
            entry++;
        }
        prompt += entry->freeformBody;
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


AFuture<AVector<AppBase::DiaryEntryExAndRelatedness>> AppBase::diaryQuery(const std::valarray<float>& query) {
    struct DiaryEntryExAndRelatednessF {
        std::list<DiaryEntryEx>::iterator entry;
        AFuture<aui::float_within_0_1> relatedness;
    };

    AVector<DiaryEntryExAndRelatednessF> relatednesses;

    for (auto it = mCachedDiary->begin(); it != mCachedDiary->end(); ++it) {
        relatednesses << DiaryEntryExAndRelatednessF{
            .entry = it,
            .relatedness = diaryEntryIsRelated(query, *it)
        };
    }
    for (const auto&[_, relatedness] : relatednesses) {
        co_await relatedness;
    }
    AVector<DiaryEntryExAndRelatedness> result = relatednesses | ranges::view::transform([](const auto& f) {
        return DiaryEntryExAndRelatedness{
            .entry = f.entry,
            .relatedness = *f.relatedness,
        };
    }) | ranges::to<AVector<DiaryEntryExAndRelatedness>>;
    ranges::sort(result, [](const auto& a, const auto& b) { return a.relatedness > b.relatedness; });
    co_return result;
}

AFuture<aui::float_within_0_1> AppBase::diaryEntryIsRelated(const std::valarray<float>& context, AppBase::DiaryEntryEx& entry) {
    if (entry.freeformBody.empty()) {
        co_return 0.f;
    }
    if (entry.freeformBody.contains("<important_note")) {
        co_return 1.f;
    }
    if (entry.metadata.embedding.size() != context.size()) {
        OpenAIChat chat {};
        entry.metadata.embedding = co_await chat.embedding(entry.freeformBody);
        diarySave(entry);
    }
    auto threadPoolTask = AUI_THREADPOOL_X [&] {
        return aui::float_within_0_1((util::cosine_similarity(context, entry.metadata.embedding) + 1.f) / 2.f);
    };
    co_return co_await threadPoolTask;
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

AJSON_FIELDS(AppBase::DiaryEntryEx::Metadata,
    AJSON_FIELDS_ENTRY(score)
    AJSON_FIELDS_ENTRY(lastUsed)
    AJSON_FIELDS_ENTRY(usageCount)
    AJSON_FIELDS_ENTRY(embedding)
    )

std::list<AppBase::DiaryEntryEx> AppBase::diaryParse(AVector<DiaryEntry> diary) {
    // ---
    // { ... }
    // ---
    //
    // parse prologue to from md file with metadata.
    return diary | ranges::view::transform([](DiaryEntry& entry) {
        try {
            if (!entry.text.startsWith("---")) {
                return DiaryEntryEx {
                    .id = std::move(entry.id),
                    .freeformBody = std::move(entry.text),
                };
            }
            auto end = entry.text.bytes().find("---", 4);
            if (end == std::string::npos) {
                return DiaryEntryEx {
                    .id = std::move(entry.id),
                    .freeformBody = std::move(entry.text),
                };
            }
            auto json = AJson::fromString(entry.text.bytes().substr(4, end - 4));
            return DiaryEntryEx {
                .id = std::move(entry.id),
                .metadata = aui::from_json<DiaryEntryEx::Metadata>(std::move(json)),
                .freeformBody = std::move(AStringView(entry.text.bytes().substr(end + 4))),
            };
        } catch (const AException& e) {
            ALogger::err("AppBase") << "diaryParse can't parse " << entry.id << ": " << e;
            return DiaryEntryEx {
                .id = std::move(entry.id),
                .freeformBody = std::move(entry.text),
            };
        }
    }) | ranges::to<std::list<AppBase::DiaryEntryEx>>;
}

void AppBase::diarySave(const DiaryEntryEx& dairyEntry) {
    // inject
    // ---
    // { ... }
    // ---
    //
    // prologue to the md file with metadata.
    diarySave(DiaryEntry{
        .id = dairyEntry.id,
        .text = "---\n{}\n---\n{}\n"_format(AJson::toString(aui::to_json(dairyEntry.metadata)), dairyEntry.freeformBody),
    });
}

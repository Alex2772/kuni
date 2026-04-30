#include "Diary.h"

#include <random>
#include <range/v3/action/sort.hpp>

#include "AUI/IO/AFileInputStream.h"
#include "AUI/IO/AFileOutputStream.h"
#include "AUI/Logging/ALogger.h"
#include "AUI/Thread/AThreadPool.h"
#include "AUI/Util/kAUI.h"
#include "OpenAIChat.h"
#include "util/cosine_similarity.h"

#include <range/v3/algorithm/sort.hpp>
#include <range/v3/range/conversion.hpp>
#include <range/v3/view/enumerate.hpp>
#include <range/v3/view/transform.hpp>

#include "OpenAITools.h"

using namespace std::chrono_literals;

static constexpr auto LOG_TAG = "Diary";

Diary::Diary(APath diaryDir)
    : mDiaryDir(std::move(diaryDir)) {
    ALOG_TRACE(LOG_TAG) << "Diary::Diary: " << diaryDir;
    mDiaryDir.makeDirs();
}

AVector<Diary::Entry> Diary::read(const APath& diaryDir) {
    ALOG_TRACE(LOG_TAG) << "Diary::read: " << diaryDir;
    AVector<Entry> result;
    diaryDir.makeDirs();
    for (const auto& entry : diaryDir.listDir()) {
        if (!entry.isRegularFileExists()) {
            continue;
        }
        if (entry.extension() != "md") {
            continue;
        }
        result << Entry{ .id = entry.filenameWithoutExtension(), .text = AString::fromUtf8(AByteBuffer::fromStream(AFileInputStream(entry))) };
    }
    return result;
}

void Diary::save(const Entry& entry) {
    ALOG_TRACE(LOG_TAG) << "Diary::save: " << entry.id;
    AFileOutputStream(mDiaryDir / (entry.id + ".md")) << entry.text;
}

void Diary::save(const EntryEx& entry) {
    ALOG_TRACE(LOG_TAG) << "Diary::save(EntryEx): " << entry.id;
    // inject
    // ---
    // { ... }
    // ---
    //
    // prologue to the md file with metadata.
    save(Entry{
        .id = entry.id,
        .text = "---\n{}\n---\n{}\n"_format(AJson::toString(aui::to_json(entry.metadata)), entry.freeformBody),
    });
}

AFuture<AVector<Diary::EntryExAndRelatedness>> Diary::query(const std::valarray<double>& query, QueryOpts opts) {
    ALOG_TRACE(LOG_TAG) << "Diary::query";
    struct DiaryEntryExAndRelatednessF {
        std::list<EntryEx>::iterator entry;
        AFuture<double> relatedness;
    };

    AVector<DiaryEntryExAndRelatednessF> relatednesses;

    for (auto it = mCachedDiary->begin(); it != mCachedDiary->end(); ++it) {
        if (!opts.filter(*it)) {
            continue;
        }
        relatednesses << DiaryEntryExAndRelatednessF{
            .entry = it,
            .relatedness = entryIsRelated(query, *it, opts)
        };
    }
    for (const auto&[_, relatedness] : relatednesses) {
        co_await relatedness;
    }
    AVector<EntryExAndRelatedness> result = relatednesses | ranges::view::transform([](const auto& f) {
        return EntryExAndRelatedness{
            .entry = f.entry,
            .relatedness = *f.relatedness,
        };
    }) | ranges::to<AVector<EntryExAndRelatedness>>;
    auto maxEntryCountCappedIt = result.begin() + std::min(opts.maxEntryCount, result.size());
    ranges::partial_sort(result, maxEntryCountCappedIt, [](const auto& a, const auto& b) { return a.relatedness > b.relatedness; });
    result.erase(maxEntryCountCappedIt, result.end());
    AUI_ASSERT(result.size() <= opts.maxEntryCount);
    // avoid returning results that equal to the query.
    // while (!result.empty()) {
    //     auto& [i, relevance] = result.front();
    //     if (relevance < 0.9999f) {
    //         break;
    //     }
    //     result.erase(result.begin());
    // }
    co_return result;
}

AFuture<double> Diary::entryIsRelated(const std::valarray<double>& context, EntryEx& entry, QueryOpts opts) {
    if (entry.freeformBody.empty()) {
        co_return 0.0;
    }
    if (entry.metadata.embedding.size() != context.size()) {
        try {
            entry.metadata.embedding = co_await OpenAIChat{.config = config::ENDPOINT_EMBEDDING}.embedding(
                entry.freeformBody.replaceAll("\t", "  "));
            save(entry);
        } catch (const AException&) {
            throw AException("while generating embedding vector for {}"_format(entry.id));
        }
    }
    auto task = AUI_THREADPOOL_X [&] {
        return ((util::cosine_similarity(context, entry.metadata.embedding) + 1.0) / 2.0) + entry.metadata.confidence * opts.confidenceFactor;
    };
    co_return co_await task;
}

std::list<Diary::EntryEx> Diary::parse(AVector<Entry> diary) {
    // parse
    // ---
    // { ... }
    // ---
    //
    // prologue in the md file with metadata.
    return diary | ranges::views::transform([](Entry& entry) {
        entry.text = entry.text.trim('\n');
        try {
            if (!entry.text.startsWith("---")) {
                return EntryEx{.id = std::move(entry.id), .freeformBody = std::move(entry.text)};
            }
            auto end = entry.text.bytes().find("---", 4);
            if (end == std::string::npos) {
                return EntryEx{.id = std::move(entry.id), .freeformBody = std::move(entry.text)};
            }
            auto json = AJson::fromString(entry.text.bytes().substr(4, end - 4));
            return EntryEx{.id = std::move(entry.id), .metadata = aui::from_json<EntryEx::Metadata>(std::move(json)), .freeformBody = std::move(AStringView(entry.text.bytes().substr(end + 4)))};
        } catch (const AException& e) {
            ALogger::err("DiaryManager") << "parse can't parse " << entry.id << ": " << e;
            return EntryEx{.id = std::move(entry.id), .freeformBody = std::move(entry.text)};
        }
    }) | ranges::to<std::list<EntryEx>>;
}

void Diary::unload(std::list<EntryEx>::const_iterator it) {
    save(*it);
    mCachedDiary->erase(it);
}

struct SleepingConsolidationMeta {
    float confidence;
};

AJSON_FIELDS(SleepingConsolidationMeta, AJSON_FIELDS_ENTRY(confidence))

AFuture<> Diary::sleepingConsolidation() {
    // emulates human sleeping behavior against diary.

    static std::default_random_engine re(std::time(nullptr));
    // reload();
    for (;;) {
        mCachedDiary = parse(read(mDiaryDir))
            | ranges::to_vector
            | ranges::action::sort([](const EntryEx& a, const EntryEx& b) {
                return a.id > b.id; // recent first, old last
            })
            | ranges::to<std::list<EntryEx>>;

        auto id = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        const auto count = mCachedDiary->size();
        const auto sleepStartTime = std::chrono::steady_clock::now();
        while (!mCachedDiary->empty()) { // we'll remove considered diary entries from mCachedDiary and save them to disk.
            if (std::chrono::steady_clock::now() - sleepStartTime >= config::SLEEP_MAX_TIME) {
                // we reached sleep max time.
                // how many memory pieces did we cover? this depends on LLM's processing speed.
                // both cloud deepseek-chat and (qwen3.5:9b on a RTX4090) can process about 500 Kuni's diary entries per
                // hour.
                // anyway, we don't need to process all entries, neither human brain does.
                // this also means our algorithm does not require memory decay (i.e., deleting old unrelevant entries).
                // Kuni will remember everything, but they would not spend LLMs processing power on reflecting about
                // same shit everytime.
                // there's a small chance Kuni will remember old stuff, see below.
                goto reachedMaxSleepTime;
            }

            auto target = [&] {
                // during your sleep its likely you see the past couple of days. however, have you encountered
                // some random shit from 2 years ago were you screwed up on a party? the random condition emulates exactly
                // this
                if (std::uniform_real_distribution<>(0.0, 1.0)(re) < 0.8) [[likely]] {
                    // likely branch.
                    // we will pick up the most recent target.
                    // also, LLM really likes to write new entries that mostly duplicate contents of those that were loaded
                    // into its context via RAG from context's embedding; AFAIK new details or reflections were added.
                    // maybe it's not a bad thing; considering the fact that below we will find related entries as well
                    // and mix them together.
                    auto entry = mCachedDiary->begin();
                    auto asValue = std::move(*entry);
                    mCachedDiary->erase(entry);
                    return asValue;
                }
                // unlikely branch.
                // pick a random target.
                // in perspective, this gives more shuffled chunks, so each sleep consolidation slightly different chunks
                // are compared.
                // this gives uniform distribution of information and merging/splitting behavior.
                auto idx = re() % mCachedDiary->size();
                auto entry = mCachedDiary->begin();
                while (idx--) {
                    entry++;
                }
                auto asValue = std::move(*entry);
                mCachedDiary->erase(entry);
                return asValue;
            }();
            if (target.metadata.embedding.size() == 0) {
                target.metadata.embedding = co_await OpenAIChat{.config = config::ENDPOINT_EMBEDDING}.embedding(target.freeformBody);
            }
            tryAgain:
            AVector<EntryExAndRelatedness> results;
            try {
                results = co_await query(target.metadata.embedding, {.confidenceFactor = 0.f /* we need just embedding relatedness */});
            } catch (const AException& e) {
                ALogger::err("Diary") << "sleepingConsolidation can't query " << e;
                goto tryAgain;
            }

            AStringVector ids;
            AStringVector idsToRemove;

            ids << target.id;

            auto body = [&] {
                AString body;
                for (const auto&[i, entry] : results | ranges::view::enumerate) {
                    if (body.length() > config::DIARY_SLEEP_MAX_LENGTH && i >= 2) {
                        break;
                    }
                    if (!body.empty()) {
                        body += "\n\n---\n\n";
                    }
                    body += AJson::toString(aui::to_json(SleepingConsolidationMeta{
                        .confidence = entry.entry->metadata.confidence,
                    }));
                    body += entry.entry->freeformBody;
                    body += "\n";
                    ids << entry.entry->id;
                    if (entry.entry->metadata.confidence < 0.9999999f) {
                        idsToRemove << std::move(entry.entry->id);
                    }
                    mCachedDiary->erase(entry.entry);
                }
                return body;
            }();
            ALogger::info("Diary") << "[" << (count - mCachedDiary->size()) << "/" << count << "] sleepingConsolidation: " << ids;
            ALOG_DEBUG("Diary") << "Prompt: " << body;

            naxyi:
            OpenAIChat chat { .systemPrompt = config::SLEEP_CONSOLIDATOR_PROMPT, .config = config::ENDPOINT_SLEEPING };

            tryAgain2:
            OpenAIChat::Response response;
            try {
                response = co_await chat.chat(body);
            } catch (const AException& e) {
                ALogger::err("Diary") << "sleepingConsolidation can't chat " << e;
                goto tryAgain2;
            }

            try {
                ALOG_DEBUG("Diary") << "Response: " << response.choices.at(0).message.content;
                for (const auto& entry : response.choices.at(0).message.content.split("\n---")) {
                    if (entry.length() < 10) {
                        continue; // unknown shit
                    }
                    auto metadata = aui::from_json<SleepingConsolidationMeta>(AJson::fromString(entry));
                    auto freeformBody = AStringView(AStringView(entry).bytes().substr(entry.bytes().find("}") + 1));
                    freeformBody = freeformBody.trim('\n');

                    if (metadata.confidence < -0.99999f) {
                        // drop.
                        continue;
                    }

                    // IMPORTANT CONSIDERATION: we'll delete old entries, replacing them with the newer ones. this will
                    // effectively update their ids, thus, during next sleep, they will appear again closer to the beginning
                    // of the processing queue.
                    // this algorithm encourages to keep in mind the most recent events and reflect on them specifically.
                    save(EntryEx{
                        .id = "{}"_format(id++),
                        .metadata = {
                            .confidence = glm::clamp(metadata.confidence, -0.99f, 0.99f),
                        },
                        .freeformBody = std::move(freeformBody),
                    });
                }
            } catch (const AException& e) {
                ALogger::err("Diary") << "sleepingConsolidation can't parse " << e;
                goto naxyi;
            }

	    // since we have written refined diary entries, we don't need
	    // old ones.
	    for (const auto& id : idsToRemove) {
		auto file = mDiaryDir / "{}.md"_format(id);
		if (file.isRegularFileExists()) {
		    file.removeFile();
		}
	    }
        }
    }
    reachedMaxSleepTime:
    reload();
}

AFuture<AString> Diary::queryAI(const AString& query, QueryOpts opts) {
    ALOG_DEBUG("Diary") << "queryAI query=\"" << query << "\"";
    ASet<AString> includedIds;
    OpenAITools tools {
        OpenAITools::Tool {
            .name = "query",
            .description = "Perform embedding-based search on RAG database",
            .parameters = {
                .properties = {
                    {"text", {.type = "string", .description =
                        "Retrieval cue that is likely to appear in memory pieces. This text will be transformed to "
                        "embedding as is, and its embedding will be compared against memory pieces. Include as many"
                        "keywords as possible; maintain meaning of the request."
                    }},
                },
                .required = {"text"},
            },
            .handler = [this, opts, &includedIds](OpenAITools::Ctx ctx) -> AFuture<AString> {
                auto cue = ctx.args["text"].asStringOpt().valueOrException("text is required string");
                auto diaryResponse = co_await this->query(co_await OpenAIChat{.config = config::ENDPOINT_EMBEDDING}.embedding(cue), opts);
                AString formattedResponse;
                ALOG_DEBUG("Diary") << "queryAI cue=\"" << cue << "\" found=" << (diaryResponse | ranges::view::transform([&](const EntryExAndRelatedness& e) -> AString {
                    if (includedIds.contains(e.entry->id)) {
                        return "";
                    }
                    return "({}.md,relatedness={})"_format(e.entry->id, e.relatedness);
                }));
                for (const auto& i : diaryResponse) {
                    if (includedIds.contains(i.entry->id)) {
                        continue;
                    }
                    includedIds << i.entry->id;
                    formattedResponse += R"(<memory_piece relatedness="{}">
{}
</memory_piece>
)"_format(i.relatedness, i.entry->freeformBody);
                }
                if (formattedResponse.empty()) {
                    if (!diaryResponse.empty()) {
                        co_return "All memory pieces conforming your query were provided already. Use other query.";
                    }
                    co_return "No data was found";
                }
                co_return formattedResponse;
            },
        },
    };
    OpenAIChat chat {
        .systemPrompt = R"(
You are a database searcher and summarizer.

The user asks you a question. Your job is to retrieve data solely from #query tool. Your job is to output data that
fully satisfies user's query and would be helpful.

Also, please include additional details that does not necessarily address the question (i.e., dates, names, events) but
might be helpful to improve quality of subsequent processing of your response.

Do not alter facts.

Do not make up facts. Rely exclusively on provided context.
)",
        .tools = tools.asJson(),
    };

    AVector<OpenAIChat::Message> messages = {
        OpenAIChat::Message {
            .role = OpenAIChat::Message::Role::USER,
            .content = query,
        },
    };

    bool toolCallHappened = false;

    for (;;) {
        auto botAnswer = (co_await chat.chat(messages)).choices.at(0).message;
        messages << botAnswer;
        if (botAnswer.tool_calls.empty()) {
            if (!toolCallHappened) {
                ALogger::warn("Diary") << "queryAI: no tool call happened, pointing that out to the LLM and trying again";
                messages << OpenAIChat::Message {
                    .role = OpenAIChat::Message::Role::USER,
                    .content = "you must perform at least one call to #query",
                };
                continue;
            }
            co_return botAnswer.content;
        }
        toolCallHappened = true;
        auto toolCalls = co_await tools.handleToolCalls(botAnswer.tool_calls);
        messages << toolCalls;
    }
}


#include "Diary.h"

#include <random>

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

using namespace std::chrono_literals;

Diary::Diary(APath diaryDir)
    : mDiaryDir(std::move(diaryDir)) {
    mDiaryDir.makeDirs();
}

AVector<Diary::Entry> Diary::read(const APath& diaryDir) {
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
    AFileOutputStream(mDiaryDir / (entry.id + ".md")) << entry.text;
}

void Diary::save(const EntryEx& entry) {
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
    ranges::sort(result, [](const auto& a, const auto& b) { return a.relatedness > b.relatedness; });
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
        entry.metadata.embedding = co_await OpenAIChat{.config = config::ENDPOINT_EMBEDDING}.embedding(entry.freeformBody);
        save(entry);
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
    reload();
    auto id = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    const auto count = mCachedDiary->size();
    while (!mCachedDiary->empty()) {
        // auto middle = mCachedDiary->begin();
        // for (int i = 0; i < config::DIARY_TOKEN_COUNT_TRIGGER / config::DIARY_AVERAGE_ENTRY_SIZE && middle != mCachedDiary->end(); ++i, ++middle);
        // ranges::partial_sort(mCachedDiary, middle,)

        auto target = [&] {
            // pick a random target.
            // in perspective, this gives more shuffled chunks, so each sleep consolidation slightly different chunks
            // are compared.
            // this gives uniform distribution of information and merging/splitting behavior.
            static std::default_random_engine re(std::time(nullptr));
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
            results = co_await query(target.metadata.embedding, {.confidenceFactor = 0.f /* we need just embedding relatence */});
        } catch (const AException& e) {
            ALogger::err("Diary") << "sleepingConsolidation can't query " << e;
            goto tryAgain;
        }

        AStringVector ids;
        AStringVector idsToRemove;

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
        OpenAIChat chat { .systemPrompt = config::SLEEP_CONSOLIDATOR_PROMPT};

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
                save(EntryEx{
                    .id = "{}"_format(id++),
                    .metadata = {
                        .confidence = glm::clamp(metadata.confidence, -0.99f, 0.99f),
                    },
                    .freeformBody = std::move(freeformBody),
                });
                for (const auto& id : idsToRemove) {
                    auto file = mDiaryDir / "{}.md"_format(id);
                    if (file.isRegularFileExists()) {
                        file.removeFile();
                    }
                }
            }
        } catch (const AException& e) {
            ALogger::err("Diary") << "sleepingConsolidation can't parse " << e;
            goto naxyi;
        }
    }
    reload();
}

AFuture<AString> Diary::queryAI(const AString& query, QueryOpts opts) {
    OpenAIChat chat {
        .systemPrompt = R"(
You are a database searcher and summarizer.

The user prompts a series of memory pieces separated by markdown line. Your job is to output data that fully satisfies
user's query and would be helpful.

Do not alter facts.

Do not make up facts. Rely exclusively on provided context.
)",
    };
    auto result = co_await this->query(co_await chat.embedding(query), std::move(opts));
    AString body;
    for (const auto&[i, relatedness] : result) {
        body += i->freeformBody;
        body += "\n\n---\n\n";
        if (body.length() >= config::DIARY_INJECTION_MAX_LENGTH) {
            break;
        }
    }
    body += "\nQuery:\n";
    body += query;
    co_return (co_await chat.chat(body)).choices.at(0).message.content;
}


#pragma once

#include <IOpenAIChat.h>
#include <Diary.h>
#include <config.h>
#include "AUI/Logging/ALogger.h"

namespace util {

inline AFuture<AVector<Diary::EntryEx>> diarySaveEntries(Diary& diary, AString llmSummartization) {
    auto id = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();

    // stupid AI sometimes messes up with separators
    llmSummartization.replaceAll("- --", "---");
    llmSummartization.replaceAll("-- -", "---");
    auto split = llmSummartization.split("---");
    AVector<Diary::EntryEx> result;

    for (const auto& take : split) {
        if (take.length() < 20) {
            continue;   // random shit
        }
        auto embedding = co_await diary.openAI()->embedding({ .config = config::ENDPOINT_EMBEDDING }, take);
        if (auto query = co_await diary.query(embedding, { .confidenceFactor = 0 }); !query.empty()) {
            ALogger::info("diarySaveEntries")
                << "{}.md"_format(id) << ": plagiarism factor other_id=\"" << query.first().entry->id
                << "\" relatedness =" << float(query.first().relatedness);
            if (query.first().relatedness > config::DIARY_PLAGIARISM_THRESHOLD) {
                ALogger::info("diarySaveEntries")
                    << "{}.md"_format(id) << ": won't store because it's plagiarism other_id=\"" << query.first().entry->id << "\"";
                continue;
            }
        }

        Diary::EntryEx entry{
            .id = "{}"_format(id++),
            .metadata = {
                .embedding = std::move(embedding),
            },
            .freeformBody = std::move(take),
        };
        diary.save(entry);
        result << std::move(entry);
    }
    co_return result;
}

/**
 * @brief Asks the LLM to write diary entries from the given context, then persists them to the diary.
 *
 * Sends `config::DIARY_PROMPT` to the LLM, splits the response by `---`, runs a plagiarism
 * check against the existing diary, and saves non-duplicate entries.
 *
 * @param openAI      The OpenAI chat instance to use for inference.
 * @param diary       The diary to save entries to.
 * @param context     The conversation context. The DIARY_PROMPT USER message is appended to it in-place.
 * @param chatParams  Chat parameters (system prompt, etc.). Tools should NOT be included.
 *
 * @return Saved diary entries.
 */
inline AFuture<AVector<Diary::EntryEx>>
diarySaveEntries(Diary& diary, IOpenAIChat::Session context, IOpenAIChat::Params chatParams) {
    context << IOpenAIChat::Message {
        .role = IOpenAIChat::Message::Role::USER,
        .content = config::DIARY_PROMPT,
    };

naxyi:
    IOpenAIChat::Response botAnswer = co_await diary.openAI()->chat(chatParams, context);
    if (botAnswer.choices.at(0).message.content.empty()) {
        goto naxyi;
    }
    context << botAnswer.choices.at(0).message;
    co_return co_await diarySaveEntries(diary, botAnswer.choices.at(0).message.content);
}

}   // namespace util

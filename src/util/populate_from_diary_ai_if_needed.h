#pragma once

#include "Diary.h"
#include "IOpenAIChat.h"

#include <AUI/Common/AStringView.h>
#include <AUI/Thread/AFuture.h>

namespace util {

/**
 * @return Format string to prompt for past 48 hours from now.
 */
AString formatPastHours(std::chrono::hours pastHours = std::chrono::hours(48));

/**
 * @brief Returns a queryAI summary from diary, if needed.
 * @param tag tag which will be included to the result. If this tag appears in mTemporaryContext, an empty
 *        string will be returned.
 * @param prompt prompt to queryAI.
 * @return An empty string with diary's AI response to prompt, empty string if tag is appeared in
 * mTemporaryContext
 * @details
 * Empty string means no additional clarification is needed, because mTemporaryContext has such summarization
 * already, indicated by `tag`.
 *
 * `tag` is formatted as an XML tag and included to the result.
 *
 * This function was introduced as a mechanism to remember important things and reminders about chats. This
 * function specifically addresses reminders and Kuni's promises, as well as implicit chat rules.
 *
 * While LLM can call `ask_diary` to get comprehensive information about a person or chat, it does not do it
 * often. Diary::queryAI demonstrated short, high quality diary retrievals against prompts at a cost of time
 * and evolving expensive AI computations and time.
 *
 * (Alex2772 18-04-2026):
 * I initially thought to replace vector-based injection of diary pages with Diary::queryAI, but because of
 * the reasons above I decided to keep vector-based search in place. As a compromise, I introduced this function
 * to get_telegram_chats and chat listing, that will run once per context.
 *
 * This overall improves per-chat awareness of Kuni, enables multiple chat management capabilities, keep reminders and
 * promises in place.
 */
AFuture<AString> populateFromDiaryAIIfNeeded(const AVector<IOpenAIChat::Message>& temporaryContext, Diary& diary, AStringView tag, AStringView prompt);

}   // namespace util
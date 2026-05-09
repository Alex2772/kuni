//
// Created by alex2772 on 4/18/26.
//

#include <chrono>
#include "populate_from_diary_ai_if_needed.h"
#include "AUI/Logging/ALogger.h"

#include <range/v3/algorithm/any_of.hpp>

using namespace std::chrono_literals;

AString util::formatPastHours(std::chrono::hours pastHours) {
    return "{}-{}"_format(std::chrono::system_clock::now() - pastHours, std::chrono::system_clock::now());
}

AFuture<AString> util::populateFromDiaryAIIfNeeded(const AVector<IOpenAIChat::Message>& temporaryContext, Diary& diary, AStringView tag, AStringView prompt) {
    ALOG_TRACE("populateFromDiaryAIIfNeeded") << "tag=" << tag;
    auto xmlTag = "<populated tag=\"{}\"/>"_format(tag);
    if (ranges::any_of(temporaryContext, [&](const auto& m) { return m.content.contains(xmlTag); })) {
        co_return ""; // no retrieval needed, as we did this already.
    }
    static AMap<AString, AString> cache;
    static std::chrono::system_clock::time_point lastCacheClear = std::chrono::system_clock::now();
    if (std::chrono::system_clock::now() - lastCacheClear > 4h) {
        cache.clear();
    }
    if (auto c = cache.contains(tag)) {
        co_return c->second;
    }
    auto result = "{}\n{}\n"_format(
        xmlTag,
        co_await diary.queryAI(prompt, { .confidenceFactor = 0.f }));
    cache[tag] = result;
    co_return result;
}
#pragma once

#include <AUI/Common/AString.h>
#include <chrono>

namespace util {

/**
 * @brief Formats a past time point as a human-readable relative string for LLM context.
 *
 * Examples:
 *   "just now"
 *   "1 minute ago"
 *   "12 minutes ago"
 *   "4 hours ago"
 *   "1 day ago"
 *   "1 week ago"
 *
 * @param tp A past time point (must be <= now).
 * @return Human-readable relative time string.
 */
AString timeAgo(std::chrono::system_clock::time_point tp);

}  // namespace util

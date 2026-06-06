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

}   // namespace util
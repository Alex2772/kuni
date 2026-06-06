//
// Created by alex2772 on 4/18/26.
//

#include <chrono>
#include "format_past_hours.h"
#include "AUI/Logging/ALogger.h"

#include <range/v3/algorithm/any_of.hpp>

using namespace std::chrono_literals;

AString util::formatPastHours(std::chrono::hours pastHours) {
    return "{}-{}"_format(std::chrono::system_clock::now() - pastHours, std::chrono::system_clock::now());
}

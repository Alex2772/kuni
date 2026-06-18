#include "time_ago.h"

#include <AUI/Common/AString.h>
#include <AUI/Traits/strings.h>

using namespace std::chrono_literals;

AString util::timeAgo(std::chrono::system_clock::time_point tp) {
    auto now = std::chrono::system_clock::now();
    auto diff = now - tp;

    auto minutes = std::chrono::duration_cast<std::chrono::minutes>(diff).count();
    auto hours   = std::chrono::duration_cast<std::chrono::hours>(diff).count();
    auto days    = hours / 24;
    auto weeks   = days / 7;

    if (minutes < 1) {
        return "just now";
    }
    if (minutes == 1) {
        return "1 minute ago";
    }
    if (minutes < 60) {
        return "{} minutes ago"_format(minutes);
    }
    if (hours == 1) {
        return "1 hour ago";
    }
    if (hours < 24) {
        return "{} hours ago"_format(hours);
    }
    if (days == 1) {
        return "1 day ago";
    }
    if (days < 7) {
        return "{} days ago"_format(days);
    }
    if (weeks == 1) {
        return "1 week ago";
    }
    return "{} weeks ago"_format(weeks);
}

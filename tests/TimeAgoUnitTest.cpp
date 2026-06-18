#include "util/time_ago.h"
#include <gmock/gmock.h>

using namespace std::chrono_literals;

static std::chrono::system_clock::time_point ago(std::chrono::system_clock::duration d) {
    return std::chrono::system_clock::now() - d;
}

TEST(TimeAgo, JustNow) {
    EXPECT_EQ(util::timeAgo(ago(30s)), "just now");
}

TEST(TimeAgo, OneMinute) {
    EXPECT_EQ(util::timeAgo(ago(1min)), "1 minute ago");
}

TEST(TimeAgo, SeveralMinutes) {
    EXPECT_EQ(util::timeAgo(ago(12min)), "12 minutes ago");
}

TEST(TimeAgo, OneHour) {
    EXPECT_EQ(util::timeAgo(ago(1h)), "1 hour ago");
}

TEST(TimeAgo, SeveralHours) {
    EXPECT_EQ(util::timeAgo(ago(4h)), "4 hours ago");
}

TEST(TimeAgo, OneDay) {
    EXPECT_EQ(util::timeAgo(ago(24h)), "1 day ago");
}

TEST(TimeAgo, SeveralDays) {
    EXPECT_EQ(util::timeAgo(ago(3 * 24h)), "3 days ago");
}

TEST(TimeAgo, OneWeek) {
    EXPECT_EQ(util::timeAgo(ago(7 * 24h)), "1 week ago");
}

TEST(TimeAgo, SeveralWeeks) {
    EXPECT_EQ(util::timeAgo(ago(14 * 24h)), "2 weeks ago");
}

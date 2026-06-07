#include "util/openai_streaming.h"

#include <cstring>
#include <gmock/gmock.h>

// Helper: create a read function from a string, simulating chunked streaming.
// chunkSize controls how many bytes are returned per call (default: all at once).
static std::function<size_t(char*, size_t)> makeReader(std::string data, size_t chunkSize = std::string::npos) {
    auto shared = std::make_shared<std::string>(std::move(data));
    auto pos = std::make_shared<size_t>(0);
    return [shared, pos, chunkSize](char* dst, size_t size) -> size_t {
        if (*pos >= shared->size()) return 0;
        size_t available = shared->size() - *pos;
        size_t toRead = std::min({size, available, chunkSize == std::string::npos ? available : chunkSize});
        std::memcpy(dst, shared->data() + *pos, toRead);
        *pos += toRead;
        return toRead;
    };
}

// Collect all yielded lines from the generator into a vector.
static std::vector<std::string> collect(AYieldGenerator<std::string_view> gen) {
    std::vector<std::string> result;
    for (auto sv : gen) {
        result.emplace_back(sv);
    }
    return result;
}

// =====================================================================
// lineByLine — splits SSE-style \n\n-delimited messages
// =====================================================================

TEST(LineByLineUnit, SingleEvent) {
    auto lines = collect(util::openai_streaming::lineByLine(makeReader("data: hello\n\n")));
    ASSERT_EQ(lines.size(), 1u);
    EXPECT_EQ(lines[0], "data: hello");
}

TEST(LineByLineUnit, MultipleEvents) {
    auto lines = collect(util::openai_streaming::lineByLine(
        makeReader("data: first\n\ndata: second\n\ndata: third\n\n")));
    ASSERT_EQ(lines.size(), 3u);
    EXPECT_EQ(lines[0], "data: first");
    EXPECT_EQ(lines[1], "data: second");
    EXPECT_EQ(lines[2], "data: third");
}

TEST(LineByLineUnit, EmptyInput) {
    auto lines = collect(util::openai_streaming::lineByLine(makeReader("")));
    EXPECT_TRUE(lines.empty());
}

TEST(LineByLineUnit, NoDelimiter) {
    // Data without \n\n should produce no events (incomplete event).
    auto lines = collect(util::openai_streaming::lineByLine(makeReader("data: incomplete")));
    EXPECT_TRUE(lines.empty());
}

TEST(LineByLineUnit, EventWithInternalNewline) {
    // A single event that spans multiple lines (only \n\n terminates it).
    auto lines = collect(util::openai_streaming::lineByLine(
        makeReader("data: line1\ndata: line2\n\n")));
    ASSERT_EQ(lines.size(), 1u);
    EXPECT_EQ(lines[0], "data: line1\ndata: line2");
}

TEST(LineByLineUnit, DelimiterOnlyInput) {
    // Just \n\n — one empty event.
    auto lines = collect(util::openai_streaming::lineByLine(makeReader("\n\n")));
    ASSERT_EQ(lines.size(), 1u);
    EXPECT_EQ(lines[0], "");
}

TEST(LineByLineUnit, TrailingDataWithoutDelimiter) {
    // Two complete events followed by incomplete data — only completed events yielded.
    auto lines = collect(util::openai_streaming::lineByLine(
        makeReader("data: a\n\ndata: b\n\ndata: c")));
    ASSERT_EQ(lines.size(), 2u);
    EXPECT_EQ(lines[0], "data: a");
    EXPECT_EQ(lines[1], "data: b");
}

TEST(LineByLineUnit, ChunkedDeliveryOneByteAtATime) {
    // Same as SingleEvent but data arrives one byte per read call.
    auto lines = collect(util::openai_streaming::lineByLine(
        makeReader("data: hello\n\n", 1)));
    ASSERT_EQ(lines.size(), 1u);
    EXPECT_EQ(lines[0], "data: hello");
}

TEST(LineByLineUnit, ChunkedDeliveryMultipleEvents) {
    // Small chunk size to exercise buffer growth and partial-read logic.
    auto lines = collect(util::openai_streaming::lineByLine(
        makeReader("data: first\n\ndata: second\n\n", 3)));
    ASSERT_EQ(lines.size(), 2u);
    EXPECT_EQ(lines[0], "data: first");
    EXPECT_EQ(lines[1], "data: second");
}

TEST(LineByLineUnit, LargeEventForcesBufferGrowth) {
    // An event larger than the initial buffer size (0x100 = 256 bytes).
    std::string bigPayload = "data: " + std::string(300, 'x');
    std::string input = bigPayload + "\n\n";
    auto lines = collect(util::openai_streaming::lineByLine(makeReader(input)));
    ASSERT_EQ(lines.size(), 1u);
    EXPECT_EQ(lines[0], bigPayload);
}

TEST(LineByLineUnit, ManyConsecutiveDelimiters) {
    // Three \n\n in a row — three empty events.
    auto lines = collect(util::openai_streaming::lineByLine(makeReader("\n\n\n\n\n\n")));
    ASSERT_EQ(lines.size(), 3u);
    for (const auto& l : lines) EXPECT_EQ(l, "");
}

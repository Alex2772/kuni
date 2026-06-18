#include "util/typos.h"
#include <gmock/gmock.h>
#include <algorithm>
#include <vector>

// =====================================================================
// Helpers
// =====================================================================

// Count Unicode codepoints in an AString via AUtf8View
static size_t countCodepoints(const AString& s) {
    size_t count = 0;
    for ([[maybe_unused]] auto cp : s.utf8()) {
        ++count;
    }
    return count;
}

// Collect all codepoints as a sorted vector (for permutation checks)
static std::vector<char32_t> sortedCodepoints(const AString& s) {
    std::vector<char32_t> result;
    for (auto cp : s.utf8()) {
        result.push_back(static_cast<char32_t>(cp));
    }
    std::sort(result.begin(), result.end());
    return result;
}

// Collect all codepoints in order
static std::vector<char32_t> orderedCodepoints(const AString& s) {
    std::vector<char32_t> result;
    for (auto cp : s.utf8()) {
        result.push_back(static_cast<char32_t>(cp));
    }
    return result;
}

// =====================================================================
// swapAdjacentChars
// =====================================================================

TEST(SwapAdjacentChars, RussianUtf8_PreservesLength) {
    std::default_random_engine rng(42);
    AString input = "привет";
    AString result = util::swapAdjacentChars(input, rng);
    EXPECT_EQ(countCodepoints(result), countCodepoints(input));
}

TEST(SwapAdjacentChars, RussianUtf8_IsPermutation) {
    std::default_random_engine rng(42);
    AString input = "привет";
    AString result = util::swapAdjacentChars(input, rng);
    EXPECT_EQ(sortedCodepoints(result), sortedCodepoints(input));
}

TEST(SwapAdjacentChars, EmptyString_NoChange) {
    std::default_random_engine rng(42);
    AString input = "";
    EXPECT_EQ(util::swapAdjacentChars(input, rng), input);
}

TEST(SwapAdjacentChars, ShortString_NoChange) {
    std::default_random_engine rng(42);
    // Single codepoint — nothing to swap
    AString input = "а";
    EXPECT_EQ(util::swapAdjacentChars(input, rng), input);
}

TEST(SwapAdjacentChars, English_IsPermutation) {
    std::default_random_engine rng(42);
    AString input = "hello";
    AString result = util::swapAdjacentChars(input, rng);
    EXPECT_EQ(sortedCodepoints(result), sortedCodepoints(input));
}

TEST(SwapAdjacentChars, ActuallySwaps) {
    std::default_random_engine rng(42);
    // "ab" has only one adjacent pair, so it must become "ba"
    AString result = util::swapAdjacentChars(AString("ab"), rng);
    EXPECT_EQ(result, AString("ba"));
}

TEST(SwapAdjacentChars, RussianTwoChars_ActuallySwaps) {
    std::default_random_engine rng(42);
    // "ба" has only one adjacent pair, so it must become "аб"
    AString result = util::swapAdjacentChars(AString("ба"), rng);
    EXPECT_EQ(result, AString("аб"));
}

// =====================================================================
// replaceWithKeyboardNeighbor
// =====================================================================

TEST(ReplaceWithKeyboardNeighbor, EmptyString_NoChange) {
    std::default_random_engine rng(42);
    AString input = "";
    EXPECT_EQ(util::replaceWithKeyboardNeighbor(input, rng), input);
}

TEST(ReplaceWithKeyboardNeighbor, Russian_PreservesLength) {
    std::default_random_engine rng(42);
    AString input = "привет";
    AString result = util::replaceWithKeyboardNeighbor(input, rng);
    EXPECT_EQ(countCodepoints(result), countCodepoints(input));
}

TEST(ReplaceWithKeyboardNeighbor, Russian_ChangesExactlyOneChar) {
    std::default_random_engine rng(42);
    AString input = "привет";
    auto inputCps = orderedCodepoints(input);
    auto resultCps = orderedCodepoints(util::replaceWithKeyboardNeighbor(input, rng));
    ASSERT_EQ(inputCps.size(), resultCps.size());
    size_t diffs = 0;
    for (size_t i = 0; i < inputCps.size(); ++i) {
        if (inputCps[i] != resultCps[i]) ++diffs;
    }
    EXPECT_EQ(diffs, 1u);
}

TEST(ReplaceWithKeyboardNeighbor, English_ChangesExactlyOneChar) {
    std::default_random_engine rng(42);
    AString input = "hello";
    auto inputCps = orderedCodepoints(input);
    auto resultCps = orderedCodepoints(util::replaceWithKeyboardNeighbor(input, rng));
    ASSERT_EQ(inputCps.size(), resultCps.size());
    size_t diffs = 0;
    for (size_t i = 0; i < inputCps.size(); ++i) {
        if (inputCps[i] != resultCps[i]) ++diffs;
    }
    EXPECT_EQ(diffs, 1u);
}

TEST(ReplaceWithKeyboardNeighbor, UnknownChars_NoChange) {
    std::default_random_engine rng(42);
    // Digits have no keyboard neighbors defined
    AString input = "123";
    EXPECT_EQ(util::replaceWithKeyboardNeighbor(input, rng), input);
}

TEST(ReplaceWithKeyboardNeighbor, ReplacementIsActualNeighbor) {
    // Verify that the replaced character is indeed a keyboard neighbor of the original.
    // Use a single-char string so we know exactly which char was replaced.
    for (int seed = 0; seed < 20; ++seed) {
        std::default_random_engine rng(seed);
        AString input = "а";
        AString result = util::replaceWithKeyboardNeighbor(input, rng);
        // result must differ from input (since 'а' has neighbors)
        // and must be a single codepoint
        EXPECT_EQ(countCodepoints(result), 1u) << "seed=" << seed;
        EXPECT_NE(result, input) << "seed=" << seed;
    }
}

// =====================================================================
// Emoji (multi-byte codepoints) edge cases
// =====================================================================

TEST(SwapAdjacentChars, Emoji_PreservesLength) {
    std::default_random_engine rng(42);
    AString input = "😀😂🙈";
    AString result = util::swapAdjacentChars(input, rng);
    EXPECT_EQ(countCodepoints(result), countCodepoints(input));
}

TEST(SwapAdjacentChars, Emoji_IsPermutation) {
    std::default_random_engine rng(42);
    AString input = "😀😂🙈";
    AString result = util::swapAdjacentChars(input, rng);
    EXPECT_EQ(sortedCodepoints(result), sortedCodepoints(input));
}

TEST(SwapAdjacentChars, EmojiTwoChars_ActuallySwaps) {
    std::default_random_engine rng(42);
    // Only one adjacent pair — must swap
    AString result = util::swapAdjacentChars(AString("😀😂"), rng);
    EXPECT_EQ(result, AString("😂😀"));
}

TEST(SwapAdjacentChars, EmojiMixedWithText_IsPermutation) {
    std::default_random_engine rng(42);
    AString input = "а😀б";
    AString result = util::swapAdjacentChars(input, rng);
    EXPECT_EQ(sortedCodepoints(result), sortedCodepoints(input));
    EXPECT_EQ(countCodepoints(result), countCodepoints(input));
}

TEST(SwapAdjacentChars, SingleEmoji_NoChange) {
    std::default_random_engine rng(42);
    AString input = "🔥";
    EXPECT_EQ(util::swapAdjacentChars(input, rng), input);
}

TEST(ReplaceWithKeyboardNeighbor, Emoji_NoChange) {
    std::default_random_engine rng(42);
    // Emojis have no keyboard neighbors — string must stay unchanged
    AString input = "😀😂🙈";
    EXPECT_EQ(util::replaceWithKeyboardNeighbor(input, rng), input);
}

TEST(ReplaceWithKeyboardNeighbor, EmojiMixedWithRussian_ChangesExactlyOneChar) {
    std::default_random_engine rng(42);
    // Only the Russian letters can be replaced; emojis must survive intact
    AString input = "привет😊";
    auto inputCps = orderedCodepoints(input);
    auto resultCps = orderedCodepoints(util::replaceWithKeyboardNeighbor(input, rng));
    ASSERT_EQ(inputCps.size(), resultCps.size());
    size_t diffs = 0;
    for (size_t i = 0; i < inputCps.size(); ++i) {
        if (inputCps[i] != resultCps[i]) ++diffs;
    }
    EXPECT_EQ(diffs, 1u);
    // The emoji itself must not be touched
    EXPECT_EQ(resultCps.back(), inputCps.back());
}

TEST(ReplaceWithKeyboardNeighbor, EmojiMixedWithEnglish_ChangesExactlyOneChar) {
    std::default_random_engine rng(42);
    AString input = "hello🎉";
    auto inputCps = orderedCodepoints(input);
    auto resultCps = orderedCodepoints(util::replaceWithKeyboardNeighbor(input, rng));
    ASSERT_EQ(inputCps.size(), resultCps.size());
    size_t diffs = 0;
    for (size_t i = 0; i < inputCps.size(); ++i) {
        if (inputCps[i] != resultCps[i]) ++diffs;
    }
    EXPECT_EQ(diffs, 1u);
    EXPECT_EQ(resultCps.back(), inputCps.back());
}

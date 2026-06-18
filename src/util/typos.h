#pragma once

#include <AUI/Common/AString.h>
#include <AUI/Common/AUtf8.hpp>
#include <random>
#include <vector>
#include <unordered_map>

namespace util {

/**
 * @brief Swaps two random adjacent UTF-8 characters in the string.
 *
 * Always performs the swap (caller is responsible for probability check).
 * UTF-8 safe: operates on codepoint boundaries via AUtf8View iterator.
 */
AString swapAdjacentChars(AString original, std::default_random_engine& rng);

/**
 * @brief Replaces a random character with an adjacent key on the keyboard.
 *
 * Always performs the replacement (caller is responsible for probability check).
 * Supports Russian (ЙЦУКЕН) and English (QWERTY) layouts, lowercase only.
 */
AString replaceWithKeyboardNeighbor(AString original, std::default_random_engine& rng);

}  // namespace util

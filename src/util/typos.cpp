#include "typos.h"

#include <range/v3/all.hpp>

AString util::swapAdjacentChars(AString original, std::default_random_engine& rng) {
    const auto utf8View = original.utf8();
    std::vector<char32_t> chars = utf8View | ranges::views::transform([](AChar c) { return char32_t(c); }) | ranges::to_vector;
    if (chars.size() < 2) {
        return original;
    }
    const auto index = std::uniform_int_distribution<size_t>(0, chars.size() - 2)(rng);
    std::swap(chars[index], chars[index + 1]);

    return AString::fromUtf32(std::u32string_view(chars.data(), chars.size()));
}
AString util::replaceWithKeyboardNeighbor(AString original, std::default_random_engine& rng) {
    static const std::unordered_map<char32_t, std::vector<char32_t>> kNeighbors = []() {
        std::unordered_map<char32_t, std::vector<char32_t>> map;

        // For each key in a layout row, neighbors = left/right on same row
        // + diagonally adjacent keys from row above/below (like a real keyboard stagger).
        auto buildLayout = [&](const std::vector<std::vector<char32_t>>& rows) {
            for (size_t row = 0; row < rows.size(); ++row) {
                const auto& r = rows[row];
                for (size_t col = 0; col < r.size(); ++col) {
                    char32_t key = r[col];
                    std::vector<char32_t> neighbors;

                    // Same row: left and right
                    if (col > 0)
                        neighbors.push_back(r[col - 1]);
                    if (col + 1 < r.size())
                        neighbors.push_back(r[col + 1]);

                    // Row above: col-1 and col (keyboard stagger)
                    if (row > 0) {
                        const auto& above = rows[row - 1];
                        if (col < above.size())
                            neighbors.push_back(above[col]);
                    }

                    // Row below: col and col+1 (keyboard stagger)
                    if (row + 1 < rows.size()) {
                        const auto& below = rows[row + 1];
                        if (col < below.size())
                            neighbors.push_back(below[col]);
                    }

                    map[key] = std::move(neighbors);
                }
            }
        };

        // English QWERTY (lowercase)
        buildLayout({
          { U'q', U'w', U'e', U'r', U't', U'y', U'u', U'i', U'o', U'p' },
          { U'a', U's', U'd', U'f', U'g', U'h', U'j', U'k', U'l' },
          { U'z', U'x', U'c', U'v', U'b', U'n', U'm' },
        });

        // Russian ЙЦУКЕН (lowercase)
        buildLayout({
          { U'й', U'ц', U'у', U'к', U'е', U'н', U'г', U'ш', U'щ', U'з', U'х', U'ъ' },
          { U'ф', U'ы', U'в', U'а', U'п', U'р', U'о', U'л', U'д', U'ж', U'э' },
          { U'я', U'ч', U'с', U'м', U'и', U'т', U'ь', U'б', U'ю' },
        });

        return map;
    }();

    const auto utf8View = original.utf8();
    std::vector<char32_t> chars = utf8View | ranges::views::transform([](AChar c) { return char32_t(c); }) | ranges::to_vector;
    if (chars.empty()) {
        return original;
    }
    const auto index = std::uniform_int_distribution<size_t>(0, chars.size() - 1)(rng);
    auto& target = chars[index];
    const auto replacementsIt = kNeighbors.find(AString(AChar(target).toString()).lowercase().utf8().first());
    if (replacementsIt == kNeighbors.end()) {
        return original;
    }
    const auto& replacements = replacementsIt->second;
    if (replacements.empty()) {
        return original;
    }
    target = replacements.at(std::uniform_int_distribution<size_t>(0, replacements.size() - 1)(rng));

    return AString::fromUtf32(std::u32string_view(chars.data(), chars.size()));
}


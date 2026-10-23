// ASCII case folding: the one copy every name comparison in the project calls.
//
// Not std::tolower: its answer follows the process locale, so the same name
// could fold differently on two machines (UTA-0219). UE1 compares its names
// ignoring ASCII case whatever the machine, and every other byte is kept as
// it is.

#pragma once

#include <algorithm>
#include <string>
#include <string_view>

namespace uta {

/// `A`-`Z` become `a`-`z`; every other byte, including any above 0x7F, is kept.
[[nodiscard]] constexpr char asciiLower(char character) noexcept {
    return character >= 'A' && character <= 'Z' ? static_cast<char>(character - 'A' + 'a') : character;
}

/// `text` with asciiLower applied to each byte.
[[nodiscard]] inline std::string asciiFolded(std::string_view text) {
    std::string folded{text};
    for (char& character : folded) character = asciiLower(character);
    return folded;
}

/// Whether `left` and `right` are equal once both are folded.
[[nodiscard]] constexpr bool equalsIgnoringAsciiCase(std::string_view left, std::string_view right) noexcept {
    return std::ranges::equal(left, right, [](char a, char b) { return asciiLower(a) == asciiLower(b); });
}

} // namespace uta

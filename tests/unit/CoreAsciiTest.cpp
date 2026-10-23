// Locks core/Ascii.h: only `A`-`Z` fold, every other byte is kept, so a
// name folds the same on every machine whatever its locale (UTA-0219).

#include "core/Ascii.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

TEST_CASE("asciiFolded lowers A to Z and keeps every other byte", "[core][ascii]") {
    CHECK(uta::asciiFolded("NavigationPoint_09@[`{") == "navigationpoint_09@[`{");
    // 0xC4 is Latin-1's A-umlaut, which a Latin-1 locale's tolower would turn
    // into 0xE4.
    const std::string high = "\xC4\xD6Z";
    CHECK(uta::asciiFolded(high) == "\xC4\xD6z");
    CHECK(uta::asciiFolded("") == "");
}

TEST_CASE("equalsIgnoringAsciiCase compares folded bytes and lengths", "[core][ascii]") {
    CHECK(uta::equalsIgnoringAsciiCase("None", "nONE"));
    CHECK_FALSE(uta::equalsIgnoringAsciiCase("None", "Non"));
    CHECK_FALSE(uta::equalsIgnoringAsciiCase("\xC4", "\xE4"));
    CHECK_FALSE(uta::equalsIgnoringAsciiCase("[", "{"));
}

// Locks core/IniText.h, the line reader the recipe and the weapon set share
// (docs/specs/UTA-0019-weapon-set.md SS 4.3). The recipe's own tests lock
// its words for the recipe; these lock that the reader names the format it
// was given, and the decimal reader the weapon set added.

#include "core/IniText.h"

#include <catch2/catch_test_macros.hpp>

#include <limits>
#include <string>
#include <vector>

namespace {

constexpr uta::ini::Format FORMAT{.noun = "test file", .header = "ut-ants test ", .version = 3};

/// Every line the reader returns, or its refusal's message.
struct Read {
    std::vector<uta::ini::Line> lines;
    std::string refusal;
    uta::ErrorCode code = uta::ErrorCode::Unknown;
};

Read readAll(std::string_view text) {
    uta::ini::Reader reader(FORMAT, text);
    Read read;
    while (true) {
        auto next = reader.next();
        if (!next) {
            read.refusal = next.error().message();
            read.code = next.error().code();
            return read;
        }
        if (!*next) return read;
        read.lines.push_back(**next);
    }
}

} // namespace

TEST_CASE("The reader returns the header, headings and keys in order", "[core][ini]") {
    const Read read = readAll("\xEF\xBB\xBF# a comment\r\nut-ants test 2  # trailing\n\n[one]\na = 1\nb = \"x # y\"\n"
                              "[two thing]\n");
    REQUIRE(read.refusal.empty());
    REQUIRE(read.lines.size() == 5);
    CHECK(read.lines[0].kind == uta::ini::Line::Kind::Header);
    CHECK(read.lines[0].number == 2);
    CHECK(read.lines[1].heading == "one");
    CHECK(read.lines[1].number == 4);
    CHECK(read.lines[2].key == "a");
    CHECK(read.lines[2].value == "1");
    CHECK(read.lines[3].value == "x # y");
    CHECK(read.lines[4].heading == "two thing");
}

TEST_CASE("keysSeen holds the closed section until the next call", "[core][ini]") {
    uta::ini::Reader reader(FORMAT, "ut-ants test 1\n[s]\na = 1\n[t]\na = 2\n");
    for (int i = 0; i < 4; ++i) REQUIRE(reader.next().value());
    // The [t] heading was just returned: [s]'s key is still there.
    CHECK(reader.keysSeen().contains("a"));
    const auto key = reader.next().value();
    REQUIRE(key);
    CHECK(key->value == "2"); // not refused as given twice: [t] is a new section
    CHECK(reader.version() == 1);
}

TEST_CASE("Each malformed line is refused naming the format and the line", "[core][ini]") {
    struct Case {
        std::string text;
        std::string message;
    };
    const std::vector<Case> cases{
        {"", "test file line 1: the test file is empty; its first line must be `ut-ants test 3`"},
        {"# only\n", "test file line 1: the test file is empty; its first line must be `ut-ants test 3`"},
        {"ut-ants recipe 1\n", "test file line 1: the first line must be `ut-ants test 3`"},
        {"ut-ants test x\n", "test file line 1: 'x' is not a test file version"},
        {"ut-ants test 0\n", "test file line 1: '0' is not a test file version"},
        {"ut-ants test 1\n[s\n", "test file line 2: a [section] heading with no ]"},
        {"ut-ants test 1\n[s] x\n", "test file line 2: text after a [section] heading"},
        {"ut-ants test 1\na = 1\n", "test file line 2: a key before any [section]"},
        {"ut-ants test 1\n[s]\nnothing\n", "test file line 3: expected `key = value`"},
        {"ut-ants test 1\n[s]\na = 1\na = 2\n", "test file line 4: 'a' is given twice in one section"},
        {"ut-ants test 1\n[s]\na =\n", "test file line 3: the key has no value"},
        {"ut-ants test 1\n[s]\na = \"x\n", "test file line 3: an unclosed quote"},
        {"ut-ants test 1\n[s]\na = \"x\" y\n", "test file line 3: text after the closing quote"},
        {"ut-ants test 1\n[s]\na = \"\\x\"\n", "test file line 3: a quoted value's only escapes are \\\" and \\\\"},
    };
    for (const Case& refusal : cases) {
        CAPTURE(refusal.text);
        const Read read = readAll(refusal.text);
        CHECK(read.refusal == refusal.message);
        CHECK(read.code == uta::ErrorCode::MalformedData);
    }
}

TEST_CASE("A newer version is refused as unsupported", "[core][ini]") {
    const Read read = readAll("ut-ants test 4\n");
    CHECK(read.code == uta::ErrorCode::UnsupportedVersion);
    CHECK(read.refusal == "test file line 1: the test file is version 4, and this game reads up to version 3");
}

TEST_CASE("decimalOf reads a finite decimal in range, with no locale", "[core][ini]") {
    CHECK(uta::ini::decimalOf(FORMAT, "1.5", 0, 2, 7).value() == 1.5);
    CHECK(uta::ini::decimalOf(FORMAT, "-0.25", -1, 0, 7).value() == -0.25);
    CHECK(uta::ini::decimalOf(FORMAT, "2", 0, 2, 7).value() == 2);
    for (const std::string_view bad : {"2.01", "-0.1", "1,5", "1.5x", "", "nan", "inf", " 1"}) {
        CAPTURE(bad);
        const auto read = uta::ini::decimalOf(FORMAT, bad, 0, 2, 7);
        REQUIRE_FALSE(read);
        CHECK(read.error().message().starts_with("test file line 7: "));
    }
}

TEST_CASE("decimalText reads back to the same double", "[core][ini]") {
    for (const double value : {0.0, 1.0, 0.1, 1.0 / 3.0, 1e-9, 12345.6789, -2.5,
                               std::numeric_limits<double>::denorm_min(), std::numeric_limits<double>::max()}) {
        CAPTURE(value);
        const std::string text = uta::ini::decimalText(value);
        const auto read = uta::ini::decimalOf(FORMAT, text, -std::numeric_limits<double>::max(),
                                              std::numeric_limits<double>::max(), 1);
        REQUIRE(read);
        CHECK(*read == value);
    }
    CHECK(uta::ini::decimalText(0.5) == "0.5");
    CHECK(uta::ini::decimalText(3) == "3");
}

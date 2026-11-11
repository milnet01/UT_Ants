// The `.ini`-style text the recipe and the weapon set share: UTF-8, `#`
// comments, a header line naming the format and its version, `[section]`
// headings and `key = value` lines (docs/specs/UTA-0113-recipe-format.md
// SS 4.2, docs/specs/UTA-0019-weapon-set.md SS 4.3).
//
// This is the line reader only. What a section or a key means is the
// format's own; both formats refuse the same malformed line in the same
// words because both read it here.

#pragma once

#include "core/Error.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace uta::ini {

/// What a format calls itself, so a refusal names the file the player wrote.
struct Format {
    std::string_view noun;   ///< "recipe", "weapon set": each refusal opens "<noun> line N: "
    std::string_view header; ///< the first line up to its version, "ut-ants recipe "
    std::uint32_t version;   ///< the newest version this game reads
};

[[nodiscard]] bool isBlank(char character);
[[nodiscard]] std::string_view trimmed(std::string_view text);
/// What is left after a value or a heading: nothing, or a comment.
[[nodiscard]] bool onlyComment(std::string_view rest);

/// `MalformedData`, "<noun> line <line>: <why>".
[[nodiscard]] std::unexpected<Error> refuse(const Format& format, std::size_t line, std::string_view why);

/// A key's value: quoted, with `\"` and `\\` its only escapes, or bare up to
/// a comment.
[[nodiscard]] Result<std::string> valueOf(const Format& format, std::string_view rest, std::size_t line);
[[nodiscard]] Result<bool> boolOf(const Format& format, std::string_view value, std::size_t line);
/// A decimal in [0, max], digits only.
[[nodiscard]] Result<std::uint32_t> numberOf(const Format& format, std::string_view value, std::uint32_t max,
                                             std::size_t line);
/// A finite decimal in [min, max], read with from_chars: no locale reaches it.
[[nodiscard]] Result<double> decimalOf(const Format& format, std::string_view value, double min, double max,
                                       std::size_t line);
/// The text of a `[...]` heading, or a refusal.
[[nodiscard]] Result<std::string_view> headingOf(const Format& format, std::string_view text, std::size_t line);
/// The name after `keyword` in a `[keyword NAME]` heading -- empty for a bare
/// `[keyword]` -- or nullopt where the heading is not that section's.
[[nodiscard]] std::optional<std::string_view> nameAfter(std::string_view heading, std::string_view keyword);
/// The blank-separated words of a value.
[[nodiscard]] std::vector<std::string_view> wordsOf(std::string_view value);
/// `value` as a writer emits it: bare where it reads back the same, else quoted.
[[nodiscard]] std::string quotedIfNeeded(std::string_view value, bool always);
/// `value` as the shortest text that reads back to the same double.
[[nodiscard]] std::string decimalText(double value);

/// One meaningful line: the header, a heading or a key.
struct Line {
    enum class Kind : std::uint8_t { Header, Heading, Key };
    Kind kind;
    std::size_t number;
    std::string_view heading; ///< Heading: the text between the brackets, trimmed
    std::string_view key;     ///< Key: the key, trimmed
    std::string value;        ///< Key: the value, unquoted
};

/// Reads a file's lines in order. It skips a byte-order mark, blank lines
/// and comments; checks the header; and refuses a malformed heading, a line
/// with no `=`, a key before any heading and a key given twice in one
/// section. Everything else is the caller's.
class Reader {
public:
    Reader(const Format& format, std::string_view text);

    /// The next meaningful line, or nullopt at the end. The first is always
    /// the header; an empty file is refused.
    [[nodiscard]] Result<std::optional<Line>> next();

    /// The header's version; 0 before the header is read.
    [[nodiscard]] std::uint32_t version() const { return version_; }

    /// The keys given in the open section. After a heading is returned it
    /// still holds the section that heading closed, until the next call.
    [[nodiscard]] const std::set<std::string, std::less<>>& keysSeen() const { return keysSeen_; }

private:
    const Format& format_;
    std::string_view text_;
    std::size_t line_ = 0;
    std::uint32_t version_ = 0;
    bool inSection_ = false;
    bool clearKeys_ = false;
    std::set<std::string, std::less<>> keysSeen_;
};

} // namespace uta::ini

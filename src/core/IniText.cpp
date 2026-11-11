// The shared line reader -- core/IniText.h. Moved out of urecipe/Recipe.cpp
// (UTA-0019 SS 4.3) with its words unchanged, so no recipe reads differently.

#include "core/IniText.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <format>

namespace uta::ini {

namespace {
constexpr std::string_view BOM = "\xEF\xBB\xBF";
}

bool isBlank(char character) { return character == ' ' || character == '\t'; }

std::string_view trimmed(std::string_view text) {
    while (!text.empty() && isBlank(text.front())) text.remove_prefix(1);
    while (!text.empty() && isBlank(text.back())) text.remove_suffix(1);
    return text;
}

bool onlyComment(std::string_view rest) {
    rest = trimmed(rest);
    return rest.empty() || rest.front() == '#';
}

std::unexpected<Error> refuse(const Format& format, std::size_t line, std::string_view why) {
    return fail(ErrorCode::MalformedData, std::format("{} line {}: {}", format.noun, line, why));
}

Result<std::string> valueOf(const Format& format, std::string_view rest, std::size_t line) {
    rest = trimmed(rest);
    if (rest.empty() || rest.front() != '"') {
        const std::string_view bare = trimmed(rest.substr(0, rest.find('#')));
        if (bare.empty()) return refuse(format, line, "the key has no value");
        return std::string(bare);
    }
    std::string value;
    for (std::size_t at = 1; at < rest.size(); ++at) {
        const char character = rest[at];
        if (character == '"') {
            if (!onlyComment(rest.substr(at + 1))) return refuse(format, line, "text after the closing quote");
            return value;
        }
        if (character == '\\') {
            if (at + 1 >= rest.size() || (rest[at + 1] != '"' && rest[at + 1] != '\\'))
                return refuse(format, line, "a quoted value's only escapes are \\\" and \\\\");
            ++at;
        }
        value += rest[at];
    }
    return refuse(format, line, "an unclosed quote");
}

Result<bool> boolOf(const Format& format, std::string_view value, std::size_t line) {
    if (value == "true") return true;
    if (value == "false") return false;
    return refuse(format, line, std::format("'{}' is not true or false", value));
}

Result<std::uint32_t> numberOf(const Format& format, std::string_view value, std::uint32_t max, std::size_t line) {
    std::uint32_t number = 0;
    const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), number);
    if (error != std::errc{} || end != value.data() + value.size() || number > max)
        return refuse(format, line, std::format("'{}' is not a whole number from 0 to {}", value, max));
    return number;
}

Result<double> decimalOf(const Format& format, std::string_view value, double min, double max, std::size_t line) {
    double number = 0;
    const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), number);
    if (error != std::errc{} || end != value.data() + value.size() || !std::isfinite(number) || number < min
        || number > max)
        return refuse(format, line, std::format("'{}' is not a number from {} to {}", value, min, max));
    return number;
}

Result<std::string_view> headingOf(const Format& format, std::string_view text, std::size_t line) {
    const std::size_t close = text.find(']');
    if (close == std::string_view::npos) return refuse(format, line, "a [section] heading with no ]");
    if (!onlyComment(text.substr(close + 1))) return refuse(format, line, "text after a [section] heading");
    return trimmed(text.substr(1, close - 1));
}

std::optional<std::string_view> nameAfter(std::string_view heading, std::string_view keyword) {
    if (!heading.starts_with(keyword)) return std::nullopt;
    const std::string_view rest = heading.substr(keyword.size());
    if (!rest.empty() && !isBlank(rest.front())) return std::nullopt;
    return trimmed(rest);
}

std::vector<std::string_view> wordsOf(std::string_view value) {
    std::vector<std::string_view> words;
    while (!(value = trimmed(value)).empty()) {
        const std::size_t end = std::min(value.find(' '), value.find('\t'));
        words.push_back(value.substr(0, end));
        value.remove_prefix(end == std::string_view::npos ? value.size() : end);
    }
    return words;
}

std::string quotedIfNeeded(std::string_view value, bool always) {
    const bool needs = always || value.empty() || isBlank(value.front()) || isBlank(value.back())
                       || value.front() == '"' || value.find('#') != std::string_view::npos;
    if (!needs) return std::string(value);
    std::string out = "\"";
    for (const char character : value) {
        if (character == '"' || character == '\\') out += '\\';
        out += character;
    }
    return out + '"';
}

std::string decimalText(double value) {
    // std::format's default for a double is the shortest round-trip form.
    return std::format("{}", value);
}

Reader::Reader(const Format& format, std::string_view text) : format_(format), text_(text) {
    if (text_.starts_with(BOM)) text_.remove_prefix(BOM.size()); // what Windows editors write
}

Result<std::optional<Line>> Reader::next() {
    if (clearKeys_) {
        keysSeen_.clear();
        clearKeys_ = false;
    }
    // An empty text still reads as one empty line, so line 0 runs once.
    while (!text_.empty() || line_ == 0) {
        ++line_;
        const std::size_t end = text_.find('\n');
        std::string_view raw = text_.substr(0, end);
        text_.remove_prefix(end == std::string_view::npos ? text_.size() : end + 1);
        if (raw.ends_with('\r')) raw.remove_suffix(1);
        const std::string_view content = trimmed(raw);
        if (content.empty() || content.front() == '#') continue;

        if (version_ == 0) {
            const std::string_view header = trimmed(content.substr(0, content.find('#')));
            if (!header.starts_with(format_.header))
                return refuse(format_, line_,
                              std::format("the first line must be `{}{}`", format_.header, format_.version));
            const std::string_view digits = header.substr(format_.header.size());
            std::uint32_t version = 0;
            const auto [last, error] = std::from_chars(digits.data(), digits.data() + digits.size(), version);
            if (error != std::errc{} || last != digits.data() + digits.size() || version == 0)
                return refuse(format_, line_, std::format("'{}' is not a {} version", digits, format_.noun));
            if (version > format_.version)
                return fail(ErrorCode::UnsupportedVersion,
                            std::format("{} line {}: the {} is version {}, and this game reads up to version {}",
                                        format_.noun, line_, format_.noun, version, format_.version));
            version_ = version;
            return Line{.kind = Line::Kind::Header, .number = line_};
        }
        if (content.front() == '[') {
            UTA_TRY(const std::string_view heading, headingOf(format_, content, line_));
            inSection_ = true;
            clearKeys_ = true;
            return Line{.kind = Line::Kind::Heading, .number = line_, .heading = heading};
        }
        const std::size_t equals = content.find('=');
        if (equals == std::string_view::npos) return refuse(format_, line_, "expected `key = value`");
        const std::string_view key = trimmed(content.substr(0, equals));
        if (!inSection_) return refuse(format_, line_, "a key before any [section]");
        if (!keysSeen_.insert(std::string(key)).second)
            return refuse(format_, line_, std::format("'{}' is given twice in one section", key));
        UTA_TRY(std::string value, valueOf(format_, content.substr(equals + 1), line_));
        return Line{.kind = Line::Kind::Key, .number = line_, .key = key, .value = std::move(value)};
    }
    if (version_ == 0)
        return refuse(format_, 1,
                      std::format("the {} is empty; its first line must be `{}{}`", format_.noun, format_.header,
                                  format_.version));
    return std::optional<Line>{};
}

} // namespace uta::ini

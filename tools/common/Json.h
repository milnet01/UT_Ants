// The one JSON string escaper the tools write with --
// docs/specs/UTA-0121-bot-path-seeds.md SS 4.8.
//
// Header-only. ut-dump, ut-bake and ut-paths each hand-write an output shape
// they own entirely, and docs/design.md keeps the dependency list short on
// purpose; this is the one piece of that writing they share.
//
// It cannot emit a string that is not valid UTF-8 -- UTA-0202. That guarantee
// lives here rather than at the call sites because the defect it fixes was a
// call site picking the wrong writer: the repair existed, as writeJsonText,
// and every UE1 name in all three tools went through the raw escaper instead.
// A second entry point is a second chance to choose wrong, so there is one.

#pragma once

#include <cstdio>
#include <ostream>
#include <string>
#include <string_view>

namespace uta::tools {

/// Whether `text` is well-formed UTF-8: no overlong form, no surrogate, nothing
/// past U+10FFFF.
inline bool isUtf8(std::string_view text) {
    std::size_t i = 0;
    const auto continuation = [&](std::size_t at, unsigned char low, unsigned char high) {
        if (at >= text.size()) return false;
        const auto ch = static_cast<unsigned char>(text[at]);
        return ch >= low && ch <= high;
    };
    while (i < text.size()) {
        const auto lead = static_cast<unsigned char>(text[i]);
        std::size_t length = 0;
        bool ok = false;
        if (lead < 0x80) { length = 1; ok = true; }
        else if (lead >= 0xC2 && lead <= 0xDF) { length = 2; ok = continuation(i + 1, 0x80, 0xBF); }
        else if (lead == 0xE0) { length = 3; ok = continuation(i + 1, 0xA0, 0xBF) && continuation(i + 2, 0x80, 0xBF); }
        else if (lead == 0xED) { length = 3; ok = continuation(i + 1, 0x80, 0x9F) && continuation(i + 2, 0x80, 0xBF); }
        else if (lead >= 0xE1 && lead <= 0xEF) { length = 3; ok = continuation(i + 1, 0x80, 0xBF) && continuation(i + 2, 0x80, 0xBF); }
        else if (lead == 0xF0) { length = 4; ok = continuation(i + 1, 0x90, 0xBF) && continuation(i + 2, 0x80, 0xBF) && continuation(i + 3, 0x80, 0xBF); }
        else if (lead >= 0xF1 && lead <= 0xF3) { length = 4; ok = continuation(i + 1, 0x80, 0xBF) && continuation(i + 2, 0x80, 0xBF) && continuation(i + 3, 0x80, 0xBF); }
        else if (lead == 0xF4) { length = 4; ok = continuation(i + 1, 0x80, 0x8F) && continuation(i + 2, 0x80, 0xBF) && continuation(i + 3, 0x80, 0xBF); }
        if (!ok) return false;
        i += length;
    }
    return true;
}

namespace detail {

/// `text` read as UT99's 8-bit text and re-encoded as UTF-8 -- UTA-0101. The
/// rule measured over the map library: decode as Windows-1252, and a byte
/// Windows-1252 leaves undefined as Latin-1.
///
/// Whole-string, not byte-wise. A UE1 name is 8-bit throughout, so a name that
/// happens to parse as UTF-8 is a coincidence; decoding only the bytes that
/// fail would corrupt the common case to flatter the rare one.
inline std::string utf8FromEightBit(std::string_view text) {
    // 0x80 to 0x9F; zero where Windows-1252 defines nothing.
    static constexpr char16_t WINDOWS_1252[32] = {
        0x20AC, 0,      0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
        0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0,      0x017D, 0,
        0,      0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
        0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0,      0x017E, 0x0178};
    std::string decoded;
    for (const char raw : text) {
        const auto ch = static_cast<unsigned char>(raw);
        if (ch < 0x80) {
            decoded += raw;
            continue;
        }
        const char16_t mapped = ch < 0xA0 ? WINDOWS_1252[ch - 0x80] : 0;
        const char32_t point = mapped != 0 ? mapped : ch;
        if (point < 0x800) {
            decoded += static_cast<char>(0xC0 | (point >> 6));
            decoded += static_cast<char>(0x80 | (point & 0x3F));
        } else {
            decoded += static_cast<char>(0xE0 | (point >> 12));
            decoded += static_cast<char>(0x80 | ((point >> 6) & 0x3F));
            decoded += static_cast<char>(0x80 | (point & 0x3F));
        }
    }
    return decoded;
}

} // namespace detail

/// `text` as a JSON string: the escapes JSON requires, plus the C0 range, which
/// a package or file name can contain and which would otherwise emit invalid
/// JSON.
///
/// Bytes that are not valid UTF-8 are repaired first -- UTA-0202. JSON is UTF-8
/// by definition (RFC 8259 SS 8.1), and UE1 hands us 8-bit names and text with
/// no declared encoding, so a byte copied through makes the whole file
/// unreadable rather than the one string. Text that is already valid UTF-8 is
/// written byte for byte, so the repair changes only output that was invalid.
inline void writeJsonString(std::ostream& out, std::string_view text) {
    std::string repaired;
    if (!isUtf8(text)) {
        repaired = detail::utf8FromEightBit(text);
        text = repaired;
    }
    out << '"';
    for (const char raw : text) {
        const auto ch = static_cast<unsigned char>(raw);
        switch (ch) {
        case '"': out << "\\\""; break;
        case '\\': out << "\\\\"; break;
        case '\n': out << "\\n"; break;
        case '\r': out << "\\r"; break;
        case '\t': out << "\\t"; break;
        default:
            if (ch < 0x20) {
                char buf[7];
                std::snprintf(buf, sizeof buf, "\\u%04x", ch);
                out << buf;
            } else {
                out << raw;
            }
        }
    }
    out << '"';
}

} // namespace uta::tools

// The one JSON string escaper the tools write with --
// docs/specs/UTA-0121-bot-path-seeds.md SS 4.8.
//
// Header-only. ut-dump, ut-bake and ut-paths each hand-write an output shape
// they own entirely, and docs/design.md keeps the dependency list short on
// purpose; this is the one piece of that writing they share.

#pragma once

#include <cstdio>
#include <ostream>
#include <string_view>

namespace uta::tools {

/// `text` as a JSON string: the escapes JSON requires, plus the C0 range,
/// which a package or file name can contain and which would otherwise emit
/// invalid JSON. Every other byte is written as it is.
inline void writeJsonString(std::ostream& out, std::string_view text) {
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

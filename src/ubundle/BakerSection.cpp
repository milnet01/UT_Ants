// The BAKR section: the version of the baker that wrote the bundle -- UTA-0349.
//
// One string: a u32 length, then that many bytes. Its rules are its own, so it
// is validated as soon as it is decoded.

#include "Sections.h"

#include <string>
#include <utility>

namespace uta::ubundle::detail {

Result<std::string> readBakerSection(Cursor& cursor) { return readString(cursor); }

Result<void> validateBaker(const std::string& baker, ErrorCode code) {
    if (baker.empty()) return fail(code, "BAKR: the baker version is empty");
    if (baker.size() > MAX_BAKER_LENGTH)
        return fail(code, "BAKR: the baker version is longer than "
                              + std::to_string(MAX_BAKER_LENGTH) + " characters");
    for (const char part : baker)
        if (part < 0x21 || part > 0x7E)
            return fail(code, "BAKR: the baker version holds a character that is not printable ASCII");
    return {};
}

Result<std::vector<std::byte>> encodeBaker(const std::string& baker) {
    Sink sink;
    sink.putString(baker);
    return std::move(sink).finish("BAKR");
}

} // namespace uta::ubundle::detail

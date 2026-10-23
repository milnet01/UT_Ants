#include "upkg/ReadSupport.h"

#include <utility>

namespace uta::upkg::detail {

Error malformed(std::string message) {
    return Error(ErrorCode::MalformedData, std::move(message));
}

Result<void> checkCount(const ByteReader& reader, std::int64_t count, std::size_t minElementBytes,
                        std::string_view owner, std::string_view noun) {
    const std::string declares =
        std::string(owner) + " declares " + std::to_string(count) + " " + std::string(noun);
    if (count < 0) {
        return std::unexpected(malformed(declares));
    }
    if (static_cast<std::uint64_t>(count) > reader.remaining() / minElementBytes) {
        return std::unexpected(malformed(declares + ", more than the " +
                                         std::to_string(reader.remaining()) +
                                         " bytes remaining can hold"));
    }
    return {};
}

Result<std::uint32_t> checkNameIndex(std::int32_t index, std::size_t nameCount,
                                     std::string_view names) {
    if (index < 0 || static_cast<std::size_t>(index) >= nameCount) {
        return std::unexpected(malformed(std::string(names) + " index " + std::to_string(index) +
                                         ", which is outside the name table's " +
                                         std::to_string(nameCount) + " entries"));
    }
    return static_cast<std::uint32_t>(index);
}

Result<void> expectConsumed(const ByteReader& reader, std::string_view owner) {
    if (reader.remaining() != 0) {
        return std::unexpected(malformed(std::string(owner) + " left " +
                                         std::to_string(reader.remaining()) +
                                         " bytes unread; the layout does not match the export"));
    }
    return {};
}

} // namespace uta::upkg::detail

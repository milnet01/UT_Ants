#include "upkg/ByteReader.h"

#include <bit>
#include <string>

namespace uta::upkg {
namespace {

/// A compact index is at most five bytes: six value bits in the first and
/// seven in each of at most four more. Six plus four sevens already exceeds
/// 32, so a fifth continuation byte cannot contribute -- and without the cap a
/// run of 0xFF is an unbounded read (INV-4).
constexpr int MAX_CONTINUATION_BYTES = 4;

Error shortRead(std::size_t wanted, std::size_t available) {
    return Error(ErrorCode::MalformedData,
                 "package ends early: " + std::to_string(wanted) +
                     " bytes needed, " + std::to_string(available) + " remaining");
}

} // namespace

ByteReader::ByteReader(std::span<const std::byte> bytes, std::size_t position) noexcept
    : bytes_(bytes), position_(position < bytes.size() ? position : bytes.size()) {}

Result<std::uint64_t> ByteReader::readLittleEndian(std::size_t count) {
    if (remaining() < count) {
        return std::unexpected(shortRead(count, remaining()));
    }
    std::uint64_t value = 0;
    for (std::size_t i = 0; i < count; ++i) {
        value |= static_cast<std::uint64_t>(
                     static_cast<std::uint8_t>(bytes_[position_ + i]))
                 << (8 * i);
    }
    position_ += count;
    return value;
}

Result<std::uint8_t> ByteReader::readU8() {
    UTA_TRY(const std::uint64_t value, readLittleEndian(1));
    return static_cast<std::uint8_t>(value);
}

Result<std::uint16_t> ByteReader::readU16() {
    UTA_TRY(const std::uint64_t value, readLittleEndian(2));
    return static_cast<std::uint16_t>(value);
}

Result<std::uint32_t> ByteReader::readU32() {
    UTA_TRY(const std::uint64_t value, readLittleEndian(4));
    return static_cast<std::uint32_t>(value);
}

Result<std::int32_t> ByteReader::readI32() {
    UTA_TRY(const std::uint64_t value, readLittleEndian(4));
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(value));
}

Result<std::int64_t> ByteReader::readI64() {
    UTA_TRY(const std::uint64_t value, readLittleEndian(8));
    return static_cast<std::int64_t>(value);
}

Result<float> ByteReader::readFloat() {
    UTA_TRY(const std::uint64_t value, readLittleEndian(4));
    return std::bit_cast<float>(static_cast<std::uint32_t>(value));
}

Result<std::int32_t> ByteReader::readIndex() {
    const std::size_t start = position_;

    UTA_TRY(const std::uint8_t first, readU8());

    // Bit 7 is the sign and bit 6 the continuation flag -- only in the FIRST
    // byte. Reading it by the later-byte rule mistakes -1 (0x81, complete)
    // for an unterminated sequence.
    const bool negative = (first & 0x80u) != 0;
    std::uint32_t magnitude = first & 0x3Fu;
    bool more = (first & 0x40u) != 0;

    // Accumulate unsigned. The final shift places bits at position 27, so a
    // signed accumulator overflows on a hostile input.
    for (int i = 0; more && i < MAX_CONTINUATION_BYTES; ++i) {
        auto next = readU8();
        if (!next.has_value()) {
            position_ = start;
            return std::unexpected(std::move(next).error());
        }
        magnitude |= static_cast<std::uint32_t>(*next & 0x7Fu) << (6 + 7 * i);
        more = (*next & 0x80u) != 0;
    }

    if (more) {
        position_ = start;
        return std::unexpected(Error(
            ErrorCode::MalformedData,
            "compact index at offset " + std::to_string(start) +
                " continues past five bytes, which cannot encode a 32-bit value"));
    }

    // Negate through a wider type: the magnitude of INT32_MIN does not fit in
    // an int32_t, so negating in place would be undefined.
    if (negative) {
        return static_cast<std::int32_t>(-static_cast<std::int64_t>(magnitude));
    }
    return static_cast<std::int32_t>(magnitude);
}

Result<std::span<const std::byte>> ByteReader::readBytes(std::size_t count) {
    if (remaining() < count) {
        return std::unexpected(shortRead(count, remaining()));
    }
    const std::span<const std::byte> view = bytes_.subspan(position_, count);
    position_ += count;
    return view;
}

Result<void> ByteReader::seek(std::size_t position) {
    if (position > bytes_.size()) {
        return std::unexpected(
            Error(ErrorCode::MalformedData,
                  "offset " + std::to_string(position) + " is past the end of a " +
                      std::to_string(bytes_.size()) + "-byte package"));
    }
    position_ = position;
    return {};
}

Result<void> ByteReader::skip(std::size_t count) {
    // Compared against what remains rather than added to the position, so a
    // count near the maximum cannot wrap past the end.
    if (count > remaining()) {
        return std::unexpected(shortRead(count, remaining()));
    }
    position_ += count;
    return {};
}

} // namespace uta::upkg

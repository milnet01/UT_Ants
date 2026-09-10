// The byte-level primitives every .utab section is read and written with.
//
// INTERNAL to uta_ubundle: included by its own .cpp files and by nothing
// outside src/ubundle/. Split out of Bundle.cpp (UTA-0091) so that each
// section's codec can live in a file of its own.
//
// docs/specs/UTA-0008-bundle-container-and-origin.md SS 4.2 is the encoding.

#pragma once

#include "core/Error.h"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace uta::ubundle::detail {

/// A section id or the magic -- SS 4.4. Byte sequences and not integers, so
/// there is no endianness to get wrong and a hex dump reads them left to right.
using SectionId = std::array<std::uint8_t, 4>;

// The minimum number of bytes one element of each type can occupy -- SS 4.2's
// table, derived from the layouts in SS 4.6 to SS 4.8. These size the DIVISION
// in readVector; every one of them must be non-zero, which is what makes that
// division safe. The section-specific minimums live beside their section.
constexpr std::uint64_t MIN_U8 = 1;
constexpr std::uint64_t MIN_U16 = 2;
constexpr std::uint64_t MIN_U32 = 4;
constexpr std::uint64_t MIN_F32 = 4;

/// A vector's own framing: the u32 count alone, with no elements. This is the
/// minimum for a NESTED vector -- a Footprint's holes are a vector of vectors,
/// and an empty inner one occupies four bytes.
constexpr std::uint64_t MIN_VECTOR = 4;

// ---------------------------------------------------------------------------
// Reading
// ---------------------------------------------------------------------------

/// A cursor bounded by ONE section's span -- SS 4.2 rule 2. Every read is
/// bounded by the span it was constructed with and never by the file, so a
/// section claiming more bytes than it was given fails inside its own span
/// rather than reading a neighbour's.
///
/// Multi-byte values are assembled byte by byte rather than by casting a
/// pointer: the format permits an unaligned offset everywhere, and nothing is
/// memcpy'd from or into a struct (SS 4.2 rule 3).
class Cursor {
public:
    explicit Cursor(std::span<const std::byte> bytes) noexcept : bytes_(bytes) {}

    [[nodiscard]] std::size_t remaining() const noexcept {
        return bytes_.size() - position_;
    }

    [[nodiscard]] Result<std::uint8_t> readU8() {
        if (remaining() < 1) return shortRead("u8");
        return static_cast<std::uint8_t>(bytes_[position_++]);
    }

    [[nodiscard]] Result<std::uint16_t> readU16() {
        UTA_TRY(const std::uint64_t value, readLittleEndian(2));
        return static_cast<std::uint16_t>(value);
    }

    [[nodiscard]] Result<std::uint32_t> readU32() {
        UTA_TRY(const std::uint64_t value, readLittleEndian(4));
        return static_cast<std::uint32_t>(value);
    }

    [[nodiscard]] Result<std::uint64_t> readU64() { return readLittleEndian(8); }

    [[nodiscard]] Result<std::int32_t> readI32() {
        UTA_TRY(const std::uint32_t value, readU32());
        return static_cast<std::int32_t>(value);
    }

    /// IEEE-754 binary32 moved through its bit pattern -- SS 4.2. Never
    /// through a wider type and never compared with ==, so -0.0, both
    /// infinities, a quiet NaN and a subnormal all survive (INV-9).
    [[nodiscard]] Result<float> readF32() {
        UTA_TRY(const std::uint32_t bits, readU32());
        return std::bit_cast<float>(bits);
    }

    [[nodiscard]] Result<std::span<const std::byte>> readBytes(std::size_t count) {
        if (remaining() < count) return shortRead("byte run");
        const std::span<const std::byte> out = bytes_.subspan(position_, count);
        position_ += count;
        return out;
    }

private:
    [[nodiscard]] static std::unexpected<Error> shortRead(const char* what) {
        return fail(ErrorCode::MalformedData,
                    std::string("section ran out of bytes reading a ") + what);
    }

    [[nodiscard]] Result<std::uint64_t> readLittleEndian(std::size_t count) {
        if (remaining() < count) return shortRead("multi-byte value");
        std::uint64_t value = 0;
        for (std::size_t i = 0; i < count; ++i)
            value |= static_cast<std::uint64_t>(static_cast<std::uint8_t>(bytes_[position_ + i]))
                     << (8U * i);
        position_ += count;
        return value;
    }

    std::span<const std::byte> bytes_;
    std::size_t position_ = 0;
};

/// A counted run of elements -- SS 4.2's `vector<T>`.
///
/// SS 4.2 rule 1, and the whole of this reader's allocation safety: the count
/// is checked against the bytes remaining in its own section BEFORE it sizes
/// anything. The check is a DIVISION, never `count * minElement` -- that
/// product is a u64 here, but the rule exists so no later edit reintroduces
/// the wrap, and INV-2's second case grades exactly it.
template <class T, class Decode>
[[nodiscard]] Result<std::vector<T>> readVector(Cursor& cursor,
                                                std::uint64_t minElement,
                                                const char* what,
                                                Decode decode) {
    UTA_TRY(const std::uint32_t count, cursor.readU32());
    if (count > cursor.remaining() / minElement)
        return fail(ErrorCode::MalformedData,
                    std::string("a count of ") + std::to_string(count) + " " + what
                        + " exceeds the bytes remaining in its section");

    std::vector<T> out;
    out.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        UTA_TRY(T value, decode(cursor));
        out.push_back(std::move(value));
    }
    return out;
}

/// A u32 byte length, then exactly that many bytes; no terminator.
///
/// The bytes are OPAQUE and are round-tripped verbatim -- SS 4.2. They are
/// assumed UTF-8 and are not validated: the three strings this format carries
/// come out of a UE1 name table, and that file format guarantees no encoding,
/// so a validating reader would refuse bundles for maps that exist.
[[nodiscard]] inline Result<std::string> readString(Cursor& cursor) {
    UTA_TRY(const std::uint32_t length, cursor.readU32());
    if (length == 0) return std::string{};
    // The bound is readBytes', and only readBytes'. A second length check here
    // would be a second implementation of one rule -- src/upkg/ByteReader.h
    // records what that costs, its sibling engine's loader having collected
    // bounds-check fixes one call site at a time. Nothing is sized from
    // `length`; the string is sized from the span readBytes returned.
    UTA_TRY(const std::span<const std::byte> raw, cursor.readBytes(length));
    std::string out(raw.size(), '\0');
    std::memcpy(out.data(), raw.data(), raw.size());
    return out;
}

[[nodiscard]] inline Result<std::uint8_t> readU8Element(Cursor& cursor) { return cursor.readU8(); }
[[nodiscard]] inline Result<std::uint16_t> readU16Element(Cursor& cursor) { return cursor.readU16(); }
[[nodiscard]] inline Result<std::uint32_t> readU32Element(Cursor& cursor) { return cursor.readU32(); }
[[nodiscard]] inline Result<float> readF32Element(Cursor& cursor) { return cursor.readF32(); }

/// `first + count <= size`, computed so it cannot overflow. Shared by the two
/// graph validators, each of which checks a node's run against its table.
[[nodiscard]] inline bool runWithin(std::uint32_t first, std::uint32_t count,
                                    std::size_t size) noexcept {
    if (first > size) return false;
    return count <= size - first;
}

// ---------------------------------------------------------------------------
// Writing
// ---------------------------------------------------------------------------

/// Append-only byte sink. Every field is emitted individually, in the order
/// its SS 4.6 to SS 4.8 table gives; nothing is memcpy'd from a struct, so no
/// padding byte ever reaches the file and the output is identical on GCC,
/// Clang and MSVC (INV-7, INV-8).
class Sink {
public:
    void putU8(std::uint8_t value) { bytes_.push_back(static_cast<std::byte>(value)); }

    void putU16(std::uint16_t value) { putLittleEndian(value, 2); }
    void putU32(std::uint32_t value) { putLittleEndian(value, 4); }
    void putU64(std::uint64_t value) { putLittleEndian(value, 8); }
    void putI32(std::int32_t value) { putU32(static_cast<std::uint32_t>(value)); }
    void putF32(float value) { putU32(std::bit_cast<std::uint32_t>(value)); }

    void putId(const SectionId& id) {
        for (const std::uint8_t part : id) putU8(part);
    }

    void putString(const std::string& value) {
        putU32(static_cast<std::uint32_t>(value.size()));
        for (const char part : value) putU8(static_cast<std::uint8_t>(part));
    }

    template <class T, class Encode>
    void putVector(const std::vector<T>& values, Encode encode) {
        putU32(static_cast<std::uint32_t>(values.size()));
        for (const T& value : values) encode(*this, value);
    }

    [[nodiscard]] std::size_t size() const noexcept { return bytes_.size(); }
    [[nodiscard]] std::vector<std::byte> take() && noexcept { return std::move(bytes_); }

    void append(const std::vector<std::byte>& other) {
        bytes_.insert(bytes_.end(), other.begin(), other.end());
    }

private:
    void putLittleEndian(std::uint64_t value, std::size_t count) {
        for (std::size_t i = 0; i < count; ++i)
            bytes_.push_back(static_cast<std::byte>((value >> (8U * i)) & 0xFFU));
    }

    std::vector<std::byte> bytes_;
};

} // namespace uta::ubundle::detail

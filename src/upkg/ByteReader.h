// The bounds-checked cursor every other part of upkg reads through.
//
// One type owns every bounds check. The alternative -- checking at each call
// site -- is what the sibling Vestige engine's glTF loader did, and its
// history records bounds-check fixes landing one call site at a time
// afterwards. docs/specs/UTA-0003-package-container.md SS 4.2.
//
// The cursor holds bytes rather than a file, so a short read is a malformed
// package and never an I/O failure: MalformedData is the one code for it
// anywhere in upkg, which SS 4.5, SS 6 and INV-9 all rely on.

#pragma once

#include "core/Error.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace uta::upkg {

class ByteReader {
public:
    explicit ByteReader(std::span<const std::byte> bytes, std::size_t position = 0) noexcept;

    // Multi-byte integers are little-endian and are assembled byte by byte
    // rather than by casting a pointer, so an unaligned offset -- which the
    // format allows everywhere -- is not undefined behaviour.
    [[nodiscard]] Result<std::uint8_t> readU8();
    [[nodiscard]] Result<std::uint16_t> readU16();
    [[nodiscard]] Result<std::uint32_t> readU32();
    [[nodiscard]] Result<std::int32_t> readI32();
    [[nodiscard]] Result<std::int64_t> readI64();
    [[nodiscard]] Result<float> readFloat();

    /// Unreal's compact index: a signed 32-bit value in one to five bytes.
    [[nodiscard]] Result<std::int32_t> readIndex();

    /// A view into the underlying span -- no copy, no allocation.
    [[nodiscard]] Result<std::span<const std::byte>> readBytes(std::size_t count);

    [[nodiscard]] Result<void> seek(std::size_t position);
    [[nodiscard]] Result<void> skip(std::size_t count);

    [[nodiscard]] std::size_t position() const noexcept { return position_; }
    [[nodiscard]] std::size_t remaining() const noexcept {
        return bytes_.size() - position_;
    }

private:
    /// Assemble `count` little-endian bytes, or fail without moving.
    [[nodiscard]] Result<std::uint64_t> readLittleEndian(std::size_t count);

    std::span<const std::byte> bytes_;
    std::size_t position_ = 0;
};

} // namespace uta::upkg

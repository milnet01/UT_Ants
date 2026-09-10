// SHA-256, as FIPS 180-4 defines it.
//
// docs/specs/UTA-0011-map-baker.md SS 4.4. In core rather than in ubake
// because unet's fingerprint manifest and the stock manifest's hashes
// (docs/design.md rule 15) are runtime work, and a runtime target cannot link
// the baker. A bake's name is SHA-256 rather than umat's FNV-1a because the
// name is also a key in a player's cache, and FNV-1a is not collision
// resistant (that spec's SS 8).

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace uta {

/// FIPS 180-4 SHA-256, fed in any number of pieces.
class Sha256 {
public:
    Sha256() noexcept;

    void add(std::span<const std::byte> bytes) noexcept;

    /// The digest of everything added, which also leaves the object as newly
    /// constructed.
    [[nodiscard]] std::array<std::byte, 32> finish() noexcept;

private:
    static constexpr std::size_t BLOCK = 64;

    void compress(const std::byte* block) noexcept;

    std::array<std::uint32_t, 8> state_;
    std::array<std::byte, BLOCK> buffer_{};
    std::size_t buffered_ = 0;
    std::uint64_t length_ = 0; ///< bytes added so far
};

[[nodiscard]] std::array<std::byte, 32> sha256(std::span<const std::byte> bytes) noexcept;

} // namespace uta

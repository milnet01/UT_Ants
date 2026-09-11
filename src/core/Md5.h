// MD5, as RFC 1321 defines it.
//
// docs/specs/UTA-0121-bot-path-seeds.md SS 4.8. The hand-off agreed with
// UT_MonsterHunt names each map by the MD5 of its file (that spec's SS 3
// decision 2). In core beside Sha256, which it mirrors: the algorithm is fixed
// by its RFC, and docs/design.md rule 1 lets core depend on nothing beyond the
// C++ standard library.
//
// NOT FOR ANYTHING THAT NEEDS COLLISION RESISTANCE. MD5 has none, which is why
// a bake's name is SHA-256 (UTA-0011 SS 8).

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace uta {

/// RFC 1321 MD5, fed in any number of pieces, as Sha256 is.
class Md5 {
public:
    Md5() noexcept;

    void update(std::span<const std::byte> bytes) noexcept;

    /// The digest of everything fed in, which also leaves the object as newly
    /// constructed.
    [[nodiscard]] std::array<std::byte, 16> finish() noexcept;

private:
    static constexpr std::size_t BLOCK = 64;

    void compress(const std::byte* block) noexcept;

    std::array<std::uint32_t, 4> state_;
    std::array<std::byte, BLOCK> buffer_{};
    std::size_t buffered_ = 0;
    std::uint64_t length_ = 0; ///< bytes fed in so far
};

[[nodiscard]] std::array<std::byte, 16> md5(std::span<const std::byte> bytes) noexcept;

} // namespace uta

// RFC 1321 MD5 -- docs/specs/UTA-0121-bot-path-seeds.md SS 4.8, INV-1.
//
// Written here rather than taken from a dependency, as Sha256.cpp is. Source:
// https://www.rfc-editor.org/rfc/rfc1321 -- SS 3.1 and SS 3.2 for the padding
// and the length, SS 3.3 for the initial value and SS 3.4 for the rounds.
// T[i] is floor(2^32 * abs(sin(i + 1))), SS 3.4, written out rather than
// computed so that no maths library decides a digit.

#include "core/Md5.h"

#include <bit>

namespace uta {
namespace {

constexpr std::array<std::uint32_t, 64> T = {
    0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
    0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
    0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
    0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
    0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
    0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
    0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
    0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391};

/// Each round's four shifts (SS 3.4).
constexpr std::array<std::array<int, 4>, 4> SHIFTS = {{
    {7, 12, 17, 22}, {5, 9, 14, 20}, {4, 11, 16, 23}, {6, 10, 15, 21}}};

constexpr std::array<std::uint32_t, 4> INITIAL = {0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476};

/// Where the 64-bit length field starts in a block (SS 3.2).
constexpr std::size_t LENGTH_AT = 56;

} // namespace

Md5::Md5() noexcept : state_(INITIAL) {}

void Md5::add(std::span<const std::byte> bytes) noexcept {
    blocks_.add(bytes, [this](const std::byte* block) { compress(block); });
}

std::array<std::byte, 16> Md5::finish() noexcept {
    // The length in BITS, taken before the padding is fed through add().
    const std::uint64_t bits = blocks_.length() * 8;

    // One 0x80 byte, zeroes to the length field, then the length
    // LITTLE-endian, where SHA-256 writes it big-endian. Where fewer than nine
    // bytes are left in this block the padding runs on into a second.
    const std::size_t buffered = blocks_.buffered();
    std::array<std::byte, BLOCK + 8> padding{};
    padding[0] = std::byte{0x80};
    const std::size_t zeroes =
        buffered < LENGTH_AT ? LENGTH_AT - 1 - buffered : BLOCK + LENGTH_AT - 1 - buffered;
    std::size_t used = 1 + zeroes;
    for (int shift = 0; shift < 64; shift += 8)
        padding[used++] = static_cast<std::byte>(static_cast<std::uint8_t>(bits >> shift));
    add(std::span<const std::byte>(padding.data(), used));

    std::array<std::byte, 16> digest{};
    for (std::size_t word = 0; word < state_.size(); ++word)
        for (std::size_t part = 0; part < 4; ++part)
            digest[4 * word + part] =
                static_cast<std::byte>(static_cast<std::uint8_t>(state_[word] >> (8 * part)));

    *this = Md5{};
    return digest;
}

void Md5::compress(const std::byte* block) noexcept {
    std::array<std::uint32_t, 16> x{};
    for (std::size_t j = 0; j < 16; ++j) {
        x[j] = std::to_integer<std::uint32_t>(block[4 * j])
               | (std::to_integer<std::uint32_t>(block[4 * j + 1]) << 8)
               | (std::to_integer<std::uint32_t>(block[4 * j + 2]) << 16)
               | (std::to_integer<std::uint32_t>(block[4 * j + 3]) << 24);
    }

    std::uint32_t a = state_[0], b = state_[1], c = state_[2], d = state_[3];
    for (std::size_t i = 0; i < 64; ++i) {
        const std::size_t round = i / 16;
        std::uint32_t mixed = 0;
        std::size_t k = 0;
        switch (round) {
        case 0: mixed = (b & c) | (~b & d); k = i; break;
        case 1: mixed = (b & d) | (c & ~d); k = (5 * i + 1) % 16; break;
        case 2: mixed = b ^ c ^ d; k = (3 * i + 5) % 16; break;
        default: mixed = c ^ (b | ~d); k = (7 * i) % 16; break;
        }
        const std::uint32_t next = b + std::rotl(a + mixed + x[k] + T[i], SHIFTS[round][i % 4]);
        a = d;
        d = c;
        c = b;
        b = next;
    }
    state_[0] += a;
    state_[1] += b;
    state_[2] += c;
    state_[3] += d;
}

std::array<std::byte, 16> md5(std::span<const std::byte> bytes) noexcept {
    Md5 hasher;
    hasher.add(bytes);
    return hasher.finish();
}

} // namespace uta

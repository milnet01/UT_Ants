// FIPS 180-4 SHA-256 -- docs/specs/UTA-0011-map-baker.md SS 4.4, INV-6.
//
// Written here rather than taken from a dependency: the algorithm is fixed by
// the standard, and docs/design.md rule 1 lets core depend on nothing beyond
// the C++ standard library. Source:
// https://csrc.nist.gov/pubs/fips/180-4/upd1/final -- SS 4.2.2 for the
// constants, SS 5.1.1 for the padding, SS 5.3.3 for the initial value and
// SS 6.2.2 for the computation.

#include "core/Sha256.h"

#include <algorithm>
#include <bit>
#include <cstring>

namespace uta {
namespace {

constexpr std::array<std::uint32_t, 64> K = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

constexpr std::array<std::uint32_t, 8> INITIAL = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                                  0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};

/// Where the 64-bit length field starts in a block (SS 5.1.1).
constexpr std::size_t LENGTH_AT = 56;

} // namespace

Sha256::Sha256() noexcept : state_(INITIAL) {}

void Sha256::add(std::span<const std::byte> bytes) noexcept {
    length_ += bytes.size();
    std::size_t offset = 0;

    // Top up a partly filled block first, so every block compressed below is
    // read straight out of the caller's span.
    if (buffered_ != 0) {
        const std::size_t take = std::min(BLOCK - buffered_, bytes.size());
        std::memcpy(buffer_.data() + buffered_, bytes.data(), take);
        buffered_ += take;
        offset = take;
        if (buffered_ < BLOCK) return;
        compress(buffer_.data());
        buffered_ = 0;
    }

    for (; bytes.size() - offset >= BLOCK; offset += BLOCK) compress(bytes.data() + offset);

    buffered_ = bytes.size() - offset;
    if (buffered_ != 0) std::memcpy(buffer_.data(), bytes.data() + offset, buffered_);
}

std::array<std::byte, 32> Sha256::finish() noexcept {
    // The length in BITS, taken before the padding is added through add().
    const std::uint64_t bits = length_ * 8;

    // One 0x80 byte, zeroes to the length field, then the length big-endian.
    // Where fewer than nine bytes are left in this block the length cannot
    // fit after the 0x80, so the padding runs on into a second block -- the
    // case INV-6's 56-byte message exists to reach.
    std::array<std::byte, BLOCK + 8> padding{};
    padding[0] = std::byte{0x80};
    const std::size_t zeroes =
        buffered_ < LENGTH_AT ? LENGTH_AT - 1 - buffered_ : BLOCK + LENGTH_AT - 1 - buffered_;
    std::size_t used = 1 + zeroes;
    for (int shift = 56; shift >= 0; shift -= 8)
        padding[used++] = static_cast<std::byte>(static_cast<std::uint8_t>(bits >> shift));
    add(std::span<const std::byte>(padding.data(), used));

    std::array<std::byte, 32> digest{};
    for (std::size_t word = 0; word < state_.size(); ++word)
        for (std::size_t part = 0; part < 4; ++part)
            digest[4 * word + part] =
                static_cast<std::byte>(static_cast<std::uint8_t>(state_[word] >> (24 - 8 * part)));

    *this = Sha256{};
    return digest;
}

void Sha256::compress(const std::byte* block) noexcept {
    std::array<std::uint32_t, 64> w{};
    for (std::size_t t = 0; t < 16; ++t) {
        w[t] = (std::to_integer<std::uint32_t>(block[4 * t]) << 24)
               | (std::to_integer<std::uint32_t>(block[4 * t + 1]) << 16)
               | (std::to_integer<std::uint32_t>(block[4 * t + 2]) << 8)
               | std::to_integer<std::uint32_t>(block[4 * t + 3]);
    }
    for (std::size_t t = 16; t < 64; ++t) {
        const std::uint32_t s0 = std::rotr(w[t - 15], 7) ^ std::rotr(w[t - 15], 18) ^ (w[t - 15] >> 3);
        const std::uint32_t s1 = std::rotr(w[t - 2], 17) ^ std::rotr(w[t - 2], 19) ^ (w[t - 2] >> 10);
        w[t] = s1 + w[t - 7] + s0 + w[t - 16];
    }

    std::uint32_t a = state_[0], b = state_[1], c = state_[2], d = state_[3];
    std::uint32_t e = state_[4], f = state_[5], g = state_[6], h = state_[7];
    for (std::size_t t = 0; t < 64; ++t) {
        const std::uint32_t bigS1 = std::rotr(e, 6) ^ std::rotr(e, 11) ^ std::rotr(e, 25);
        const std::uint32_t choose = (e & f) ^ (~e & g);
        const std::uint32_t t1 = h + bigS1 + choose + K[t] + w[t];
        const std::uint32_t bigS0 = std::rotr(a, 2) ^ std::rotr(a, 13) ^ std::rotr(a, 22);
        const std::uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
        const std::uint32_t t2 = bigS0 + majority;
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }
    state_[0] += a;
    state_[1] += b;
    state_[2] += c;
    state_[3] += d;
    state_[4] += e;
    state_[5] += f;
    state_[6] += g;
    state_[7] += h;
}

std::array<std::byte, 32> sha256(std::span<const std::byte> bytes) noexcept {
    Sha256 hasher;
    hasher.add(bytes);
    return hasher.finish();
}

} // namespace uta

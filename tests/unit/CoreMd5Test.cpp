// Locks INV-1 of docs/specs/UTA-0121-bot-path-seeds.md: md5 gives RFC 1321's
// appendix A.5 digests, whole and fed a byte at a time.
//
// The last two strings are the ones that matter, as CoreSha256Test.cpp's
// 56-byte message is there. Each leaves too little room in its last block for
// the 0x80 byte and the length, so the padding runs into a second block.

#include "core/Md5.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>

namespace {

std::string hex(const std::array<std::byte, 16>& digest) {
    static constexpr std::string_view DIGITS = "0123456789abcdef";
    std::string out;
    for (const std::byte part : digest) {
        const auto value = std::to_integer<unsigned>(part);
        out += DIGITS[value >> 4];
        out += DIGITS[value & 0xFU];
    }
    return out;
}

std::span<const std::byte> bytesOf(std::string_view text) {
    return std::as_bytes(std::span<const char>(text.data(), text.size()));
}

struct Example {
    std::string_view message;
    std::string_view digest;
};

/// RFC 1321's appendix A.5, every string in it.
constexpr std::array<Example, 7> SUITE = {{
    {"", "d41d8cd98f00b204e9800998ecf8427e"},
    {"a", "0cc175b9c0f1b6a831c399e269772661"},
    {"abc", "900150983cd24fb0d6963f7d28e17f72"},
    {"message digest", "f96b697d7cb7938d525a2f31aaf161d0"},
    {"abcdefghijklmnopqrstuvwxyz", "c3fcd3d76192e4007dfb496cca67e13b"},
    {"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789",
     "d174ab98d277d9f5a5611c2c9f419d9f"},
    {"12345678901234567890123456789012345678901234567890123456789012345678901234567890",
     "57edf4a22be3c955ac49da2e2107b67a"},
}};

} // namespace

TEST_CASE("INV-1: md5 gives RFC 1321's appendix A.5 digests", "[core][md5]") {
    for (const Example& example : SUITE) {
        INFO("message: \"" << example.message << "\"");
        CHECK(hex(uta::md5(bytesOf(example.message))) == example.digest);
    }
}

TEST_CASE("INV-1: Md5 fed one byte at a time gives the one-shot digest", "[core][md5]") {
    for (const Example& example : SUITE) {
        INFO("message: \"" << example.message << "\"");
        uta::Md5 hasher;
        for (const std::byte part : bytesOf(example.message))
            hasher.update(std::span<const std::byte>(&part, 1));
        CHECK(hex(hasher.finish()) == example.digest);
    }
}

TEST_CASE("Md5::finish leaves the hasher as newly constructed", "[core][md5]") {
    // The header promises this, so a caller may reuse one hasher.
    uta::Md5 hasher;
    hasher.update(bytesOf("abc"));
    (void)hasher.finish();
    CHECK(hex(hasher.finish()) == SUITE[0].digest);
}

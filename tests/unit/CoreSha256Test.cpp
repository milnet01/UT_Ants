// Locks INV-6 of docs/specs/UTA-0011-map-baker.md: sha256 gives FIPS 180-4's
// example digests, and Sha256 fed in pieces gives the one-shot digest.
//
// The 56-byte message is the one that matters. It leaves too little room in
// its last block for the length field, so the padding must run into a second
// block, and a padding rule that is wrong only there passes the other two.

#include "core/Sha256.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>

namespace {

std::string hex(const std::array<std::byte, 32>& digest) {
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

/// FIPS 180-4's examples, as the spec's INV-6 quotes them.
constexpr std::array<Example, 3> EXAMPLES = {{
    {"", "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"},
    {"abc", "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"},
    {"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
     "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"},
}};

} // namespace

TEST_CASE("sha256 gives FIPS 180-4's example digests", "[core][sha256]") {
    REQUIRE(EXAMPLES[2].message.size() == 56);
    for (const Example& example : EXAMPLES) {
        INFO("message: \"" << example.message << "\"");
        CHECK(hex(uta::sha256(bytesOf(example.message))) == example.digest);
    }
}

TEST_CASE("Sha256 fed one byte at a time gives the one-shot digest", "[core][sha256]") {
    for (const Example& example : EXAMPLES) {
        INFO("message: \"" << example.message << "\"");
        uta::Sha256 hasher;
        for (const std::byte part : bytesOf(example.message))
            hasher.add(std::span<const std::byte>(&part, 1));
        CHECK(hex(hasher.finish()) == example.digest);
    }
}

TEST_CASE("Sha256::finish leaves the hasher as newly constructed", "[core][sha256]") {
    // The header promises this, so a caller may reuse one hasher.
    uta::Sha256 hasher;
    hasher.add(bytesOf("abc"));
    (void)hasher.finish();
    CHECK(hex(hasher.finish()) == EXAMPLES[0].digest);
}

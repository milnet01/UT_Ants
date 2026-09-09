// A little-endian byte emitter for the container tests.
//
// Emits the primitives UTA-0008 SS 4.2 defines and NOTHING about the layout:
// which field follows which is said by the CALL ORDER at each call site, read
// off the spec. That separation is what makes a golden fixture independent of
// src/ubundle/Bundle.cpp -- a helper that knew the layout would agree with the
// reader by construction, and a swap present in both would round-trip
// perfectly.
//
// Shared rather than copied. It stood as two identical copies until UTA-0052
// needed a third (docs/standards/coding.md's Rule of Three); the copies
// differed only in their comments.

#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace uta::testing {

/// Emits primitives in SS 4.2's encoding. Deliberately layout-ignorant: what
/// order the fields go in is said by the caller.
class Bytes {
public:
    void u8(std::uint8_t value) { data_.push_back(static_cast<std::byte>(value)); }
    void u16(std::uint16_t value) { little(value, 2); }
    void u32(std::uint32_t value) { little(value, 4); }
    void u64(std::uint64_t value) { little(value, 8); }
    void i32(std::int32_t value) { u32(static_cast<std::uint32_t>(value)); }
    void f32(float value) { u32(std::bit_cast<std::uint32_t>(value)); }

    /// Four literal bytes -- a section id or the magic. SS 4.4: a byte
    /// sequence and not an integer, so there is no endianness to get wrong.
    void id(std::string_view four) {
        for (const char part : four) u8(static_cast<std::uint8_t>(part));
    }

    /// SS 4.2: a u32 byte length, then exactly that many bytes; no terminator.
    void str(std::string_view value) {
        u32(static_cast<std::uint32_t>(value.size()));
        for (const char part : value) u8(static_cast<std::uint8_t>(part));
    }

    void append(const Bytes& other) {
        data_.insert(data_.end(), other.data_.begin(), other.data_.end());
    }

    [[nodiscard]] std::size_t size() const noexcept { return data_.size(); }
    [[nodiscard]] const std::vector<std::byte>& data() const noexcept { return data_; }

private:
    void little(std::uint64_t value, std::size_t count) {
        for (std::size_t i = 0; i < count; ++i)
            data_.push_back(static_cast<std::byte>((value >> (8U * i)) & 0xFFU));
    }

    std::vector<std::byte> data_;
};

} // namespace uta::testing

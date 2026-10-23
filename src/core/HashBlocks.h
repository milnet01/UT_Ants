// The block buffering Md5 and Sha256 share. Both hash 64-byte blocks fed in
// pieces of any size; only what a block does to the hash's state differs, and
// each passes that in as `compress`.

#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

namespace uta::detail {

/// Bytes fed in pieces of any size, handed on a whole block at a time.
template <std::size_t Block>
class HashBlocks {
public:
    /// Feed `bytes`, calling `compress(const std::byte* block)` once for each
    /// block completed. The rest waits for the next call.
    template <class Compress>
    void add(std::span<const std::byte> bytes, Compress&& compress) noexcept {
        // An empty span may carry a null pointer, and memcpy from null is
        // undefined even for zero bytes.
        if (bytes.empty()) return;
        length_ += bytes.size();
        std::size_t offset = 0;

        // Top up a partly filled block first, so every block compressed below
        // is read straight out of the caller's span.
        if (buffered_ != 0) {
            const std::size_t take = std::min(Block - buffered_, bytes.size());
            std::memcpy(buffer_.data() + buffered_, bytes.data(), take);
            buffered_ += take;
            offset = take;
            if (buffered_ < Block) return;
            compress(buffer_.data());
            buffered_ = 0;
        }

        for (; bytes.size() - offset >= Block; offset += Block) compress(bytes.data() + offset);

        buffered_ = bytes.size() - offset;
        if (buffered_ != 0) std::memcpy(buffer_.data(), bytes.data() + offset, buffered_);
    }

    /// Bytes fed in so far.
    [[nodiscard]] std::uint64_t length() const noexcept { return length_; }

    /// Bytes waiting in the partly filled block.
    [[nodiscard]] std::size_t buffered() const noexcept { return buffered_; }

private:
    std::array<std::byte, Block> buffer_{};
    std::size_t buffered_ = 0;
    std::uint64_t length_ = 0;
};

} // namespace uta::detail

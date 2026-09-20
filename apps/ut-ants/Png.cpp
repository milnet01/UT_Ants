// Png.h says what grades this.
//
// This is the single translation unit that instantiates stb_image_write.
// third_party/stb/README.md records the commit and why the copy is vendored
// rather than fetched, and names the two switches set below.

#include "Png.h"

#include <string>

// No FILE-taking entry point: every write in this project goes through
// core/FileSystem.h, which reports an Error rather than setting errno.
#define STBI_WRITE_NO_STDIO
// A malformed call must not abort the client. encodePng validates its own
// arguments and returns an Error instead.
#define STBIW_ASSERT(x) ((void)0)
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

namespace uta::client {

namespace {

/// Where stb appends. It writes in pieces, so this grows rather than being
/// sized up front.
void collect(void* context, void* data, int size) {
    if (size <= 0) return;
    auto& out = *static_cast<std::vector<std::byte>*>(context);
    const auto* const bytes = static_cast<const std::byte*>(data);
    out.insert(out.end(), bytes, bytes + static_cast<std::size_t>(size));
}

} // namespace

Result<std::vector<std::byte>> encodePng(std::span<const std::byte> pixels, std::uint32_t width,
                                         std::uint32_t height) {
    if (width == 0 || height == 0)
        return std::unexpected(Error(ErrorCode::InvalidArgument,
                                     "PNG of a zero-sized image: " + std::to_string(width) + "x" +
                                         std::to_string(height)));

    const std::size_t expected =
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * PNG_BYTES_PER_PIXEL;
    if (pixels.size() != expected)
        return std::unexpected(Error(ErrorCode::InvalidArgument,
                                     "PNG of " + std::to_string(width) + "x" +
                                         std::to_string(height) + " wants " +
                                         std::to_string(expected) + " bytes, given " +
                                         std::to_string(pixels.size())));

    std::vector<std::byte> out;
    // The encoder is lossless, so the result is bounded by the input plus its
    // own framing. Reserving the input's size spares the common case a handful
    // of reallocations without capping anything.
    out.reserve(expected);

    const int stride = static_cast<int>(width) * static_cast<int>(PNG_BYTES_PER_PIXEL);
    const int wrote = stbi_write_png_to_func(&collect, &out, static_cast<int>(width),
                                             static_cast<int>(height),
                                             static_cast<int>(PNG_BYTES_PER_PIXEL), pixels.data(),
                                             stride);
    if (wrote == 0 || out.empty())
        return std::unexpected(Error(ErrorCode::IoFailure, "the PNG encoder failed"));

    return out;
}

} // namespace uta::client

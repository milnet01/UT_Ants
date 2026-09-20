// Capture.h says what grades this.

#include "Capture.h"

#include "Png.h"

#include "core/FileSystem.h"
#include "core/Sha256.h"

#include <array>
#include <format>
#include <span>
#include <system_error>

namespace uta::client {

namespace {

Result<void> writeBytes(const std::filesystem::path& path, std::span<const std::byte> bytes) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec)
        return std::unexpected(Error(ErrorCode::IoFailure, "cannot make " +
                                                               path.parent_path().string() + ": " +
                                                               ec.message()));
    return fs::writeFileAtomically(path, bytes);
}

Result<void> writeText(const std::filesystem::path& path, std::string_view text) {
    return writeBytes(path, std::as_bytes(std::span(text.data(), text.size())));
}

/// Encode one image and write it, naming which of the two failed. Both images
/// go through the same path, and an error saying only "the PNG encoder failed"
/// would not say which.
Result<void> writeImage(const std::filesystem::path& path, std::string_view which,
                        std::span<const std::byte> rgba, std::uint32_t width,
                        std::uint32_t height) {
    auto png = encodePng(rgba, width, height);
    if (!png) return std::unexpected(png.error().withContext(which));
    return writeBytes(path, *png);
}

} // namespace

std::string bundleHashHex(std::span<const std::byte> bundleBytes) {
    const std::array<std::byte, 32> digest = sha256(bundleBytes);
    std::string hex;
    hex.reserve(digest.size() * 2);
    for (const std::byte byte : digest) hex += std::format("{:02x}", std::to_integer<unsigned>(byte));
    return hex;
}

std::string captureFolderName(std::string_view map, std::chrono::system_clock::time_point when) {
    const auto seconds = std::chrono::floor<std::chrono::seconds>(when);
    return std::format("{}-{:%Y%m%d-%H%M%S}", map, seconds);
}

std::string captureDetails(const CaptureInfo& info) {
    // One key a line, in the order a reader wants them: what map, seen from
    // where, then what drew it.
    return std::format("map: {}\n"
                       "camera: {}\n"
                       "bundle-hash: {}\n"
                       "bundle-format: {}\n"
                       "baker-version: {}\n"
                       "commit: {}\n"
                       "tier: {}\n"
                       "render-size: {}x{}\n"
                       "render-scale: {:.3f}\n"
                       "window-size: {}x{}\n"
                       "light-seconds: {:.3f}\n",
                       info.map, info.cameraLine, info.bundleHash, info.formatVersion,
                       info.bakerVersion.empty() ? "unknown" : info.bakerVersion, info.commit,
                       info.tier, info.renderWidth, info.renderHeight, info.renderScale,
                       info.windowWidth, info.windowHeight, info.lightSeconds);
}

Result<std::filesystem::path> writeCapture(const std::filesystem::path& captures,
                                           const CaptureInfo& info,
                                           std::span<const std::byte> shownRgba,
                                           std::span<const std::byte> linearRgba,
                                           std::chrono::system_clock::time_point when) {
    const std::size_t expected = static_cast<std::size_t>(info.renderWidth) *
                                 static_cast<std::size_t>(info.renderHeight) * PNG_BYTES_PER_PIXEL;
    if (shownRgba.size() != expected || linearRgba.size() != expected)
        return std::unexpected(
            Error(ErrorCode::InvalidArgument,
                  std::format("a capture of {}x{} wants {} bytes an image, given {} and {}",
                              info.renderWidth, info.renderHeight, expected, shownRgba.size(),
                              linearRgba.size())));

    // resolveUnder is core's trust boundary, and a map name reaches here from
    // a file on disk. A name carrying a separator or ".." would otherwise put
    // the folder outside the captures directory.
    const auto folder = fs::resolveUnder(captures, captureFolderName(info.map, when));
    if (!folder) return std::unexpected(folder.error());

    if (const auto wrote = writeImage(*folder / "frame.png", "the frame as shown", shownRgba,
                                      info.renderWidth, info.renderHeight);
        !wrote)
        return std::unexpected(wrote.error());

    if (const auto wrote = writeImage(*folder / "linear.png", "the linear view", linearRgba,
                                      info.renderWidth, info.renderHeight);
        !wrote)
        return std::unexpected(wrote.error());

    if (const auto wrote = writeText(*folder / "details.txt", captureDetails(info)); !wrote)
        return std::unexpected(wrote.error());

    // The camera on its own, in the form ut-shot reads from standard input, so
    // the view can be drawn again by piping this file in rather than by
    // copying a field out of details.txt by hand. That is what makes the
    // folder reproduce the view rather than merely describe it.
    if (const auto wrote = writeText(*folder / "camera.txt", info.cameraLine + "\n"); !wrote)
        return std::unexpected(wrote.error());

    return *folder;
}

} // namespace uta::client

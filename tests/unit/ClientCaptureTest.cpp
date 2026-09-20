// UTA-0191: the capture folder's parts that need no window.
// apps/ut-ants/Capture.cpp and apps/ut-ants/Png.cpp are compiled into this
// binary.
//
// WHAT THIS DOES NOT GRADE, said here rather than left to be assumed: nothing
// below decodes a PNG, so these cases check the container's own header and the
// wrapper's arguments, not that the pixels came back. The pixels are the
// vendored encoder's business (third_party/stb/README.md), and the defect
// class this project can actually introduce is in the wrapper -- a swapped
// width and height, a stride in the wrong unit, a span whose length nobody
// checked. The IHDR case below fails on the first of those.
//
// The frame a capture holds is graded by neither: reading it back needs a
// device and a presented frame, and no CI leg has a display
// (docs/specs/UTA-0014-vulkan-draw-path.md SS 4.12).
//
// NO TEST NAME CONTAINS A COMMA -- BakeCliTest.cpp says why.

#include "ut-ants/Capture.h"
#include "ut-ants/Png.h"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace uta::client;
namespace stdfs = std::filesystem;

namespace {

class TempDir {
public:
    TempDir() {
        static std::atomic<int> counter{0};
        path_ = stdfs::temp_directory_path() /
                ("uta-capture-" +
                 std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" +
                 std::to_string(counter++));
        stdfs::create_directories(path_);
    }
    ~TempDir() {
        std::error_code ec;
        stdfs::remove_all(path_, ec);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
    [[nodiscard]] const stdfs::path& path() const { return path_; }

private:
    stdfs::path path_;
};

std::vector<std::byte> solidImage(std::uint32_t width, std::uint32_t height, std::uint8_t value) {
    return std::vector<std::byte>(static_cast<std::size_t>(width) * height * PNG_BYTES_PER_PIXEL,
                                  std::byte{value});
}

/// A big-endian 32-bit field, which is how PNG stores every length and every
/// IHDR dimension.
std::uint32_t beAt(const std::vector<std::byte>& bytes, std::size_t offset) {
    return (std::to_integer<std::uint32_t>(bytes[offset]) << 24) |
           (std::to_integer<std::uint32_t>(bytes[offset + 1]) << 16) |
           (std::to_integer<std::uint32_t>(bytes[offset + 2]) << 8) |
           std::to_integer<std::uint32_t>(bytes[offset + 3]);
}

std::string readWhole(const stdfs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

/// A time that is not midnight and not a round minute, so a formatting mistake
/// in any of the three fields shows rather than coinciding with a zero.
constexpr std::chrono::system_clock::time_point WHEN =
    std::chrono::sys_days{std::chrono::year{2026} / 9 / 20} + std::chrono::hours{14} +
    std::chrono::minutes{30} + std::chrono::seconds{5};

CaptureInfo sampleInfo() {
    return CaptureInfo{
        .map = "MH-Example",
        .bundleHash = "0123456789abcdef",
        .formatVersion = 14,
        .bakerVersion = "r21-f14-labcdef",
        .commit = "5caef74",
        .tier = "high",
        .renderWidth = 4,
        .renderHeight = 3,
        .renderScale = 0.75,
        .windowWidth = 1280,
        .windowHeight = 720,
        .lightSeconds = 12.5,
        .cameraLine = "100 200 300 0 16384 0 90",
    };
}

} // namespace

TEST_CASE("encodePng writes a PNG whose header names the size it was given", "[client][capture]") {
    const auto png = encodePng(solidImage(4, 3, 0x7f), 4, 3);
    REQUIRE(png.has_value());
    REQUIRE(png->size() > 24);

    // The eight-byte signature every PNG opens with.
    const std::vector<std::byte> signature{std::byte{0x89}, std::byte{'P'},  std::byte{'N'},
                                           std::byte{'G'},  std::byte{0x0d}, std::byte{0x0a},
                                           std::byte{0x1a}, std::byte{0x0a}};
    CHECK(std::vector<std::byte>(png->begin(), png->begin() + 8) == signature);

    // IHDR is the first chunk: length and type occupy bytes 8 to 15, so its
    // width and height are the two fields at 16 and 20. This is what fails if
    // the wrapper ever passes width and height the wrong way round.
    CHECK(beAt(*png, 16) == 4u);
    CHECK(beAt(*png, 20) == 3u);
    CHECK(std::to_integer<int>((*png)[24]) == 8);  // bits a channel
    CHECK(std::to_integer<int>((*png)[25]) == 6);  // colour type 6: RGBA
}

TEST_CASE("encodePng is refused a span that is not the image's size", "[client][capture]") {
    // Short and long. A plain test cannot see a short span go wrong -- the
    // encoder would read past the end, which is undefined rather than a wrong
    // answer -- so the guard is what makes the case gradeable at all.
    CHECK_FALSE(encodePng(solidImage(4, 3, 0), 4, 4).has_value());
    CHECK_FALSE(encodePng(solidImage(4, 4, 0), 4, 3).has_value());
    CHECK_FALSE(encodePng({}, 0, 3).has_value());
    CHECK_FALSE(encodePng({}, 4, 0).has_value());

    // NOT covered here, deliberately: a transposed image. 4x3 and 3x4 hold the
    // same number of bytes, so a length check cannot tell them apart and
    // asserting that it does would be asserting something untrue. The IHDR
    // case above is what catches the wrapper passing the two the wrong way
    // round, which is the mistake this project could actually make.
    CHECK(encodePng(solidImage(4, 3, 0), 3, 4).has_value());
}

TEST_CASE("bundleHashHex is the SHA-256 of the bytes as lowercase hex", "[client][capture]") {
    const std::string abc = "abc";
    const auto bytes = std::as_bytes(std::span(abc.data(), abc.size()));
    // FIPS 180-4's own worked example.
    CHECK(bundleHashHex(bytes) ==
          "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

TEST_CASE("captureFolderName names the map and the time in UTC", "[client][capture]") {
    CHECK(captureFolderName("MH-Example", WHEN) == "MH-Example-20260920-143005");
}

TEST_CASE("captureDetails carries every field the item asks for", "[client][capture]") {
    const std::string text = captureDetails(sampleInfo());
    CHECK(text.find("map: MH-Example\n") != std::string::npos);
    CHECK(text.find("camera: 100 200 300 0 16384 0 90\n") != std::string::npos);
    CHECK(text.find("bundle-hash: 0123456789abcdef\n") != std::string::npos);
    CHECK(text.find("bundle-format: 14\n") != std::string::npos);
    CHECK(text.find("baker-version: r21-f14-labcdef\n") != std::string::npos);
    CHECK(text.find("commit: 5caef74\n") != std::string::npos);
    CHECK(text.find("tier: high\n") != std::string::npos);
    CHECK(text.find("render-size: 4x3\n") != std::string::npos);
    CHECK(text.find("render-scale: 0.750\n") != std::string::npos);
    CHECK(text.find("window-size: 1280x720\n") != std::string::npos);
    CHECK(text.find("light-seconds: 12.500\n") != std::string::npos);
}

TEST_CASE("captureDetails says unknown rather than leaving the baker version blank",
          "[client][capture]") {
    CaptureInfo info = sampleInfo();
    info.bakerVersion.clear();
    const std::string text = captureDetails(info);
    // The line must still be there. An omitted key and a baker that reported
    // nothing read the same way to whoever opens the folder later.
    CHECK(text.find("baker-version: unknown\n") != std::string::npos);
}

TEST_CASE("writeCapture writes both images and both text files", "[client][capture]") {
    const TempDir dir;
    const CaptureInfo info = sampleInfo();
    const auto folder = writeCapture(dir.path(), info, solidImage(4, 3, 0x20),
                                     solidImage(4, 3, 0x40), WHEN);
    REQUIRE(folder.has_value());

    // The FOLDER's own name, not the whole path: resolveUnder canonicalises,
    // and the Windows runner's temp directory is an 8.3 name that expands --
    // so comparing against dir.path() / name fails there for no real reason.
    CHECK(folder->filename() == "MH-Example-20260920-143005");
    CHECK(stdfs::exists(*folder / "frame.png"));
    CHECK(stdfs::exists(*folder / "linear.png"));
    CHECK(stdfs::exists(*folder / "details.txt"));
    CHECK(stdfs::exists(*folder / "camera.txt"));

    // camera.txt is what gets piped back into ut-shot, so it holds the camera
    // and nothing else.
    CHECK(readWhole(*folder / "camera.txt") == "100 200 300 0 16384 0 90\n");
    CHECK(readWhole(*folder / "details.txt") == captureDetails(info));

    // The two images were drawn at different brightnesses, so a capture that
    // wrote one of them twice is caught here.
    CHECK(readWhole(*folder / "frame.png") != readWhole(*folder / "linear.png"));
}

TEST_CASE("writeCapture is refused images that disagree with the recorded size",
          "[client][capture]") {
    const TempDir dir;
    const CaptureInfo info = sampleInfo(); // 4x3
    CHECK_FALSE(
        writeCapture(dir.path(), info, solidImage(4, 3, 0), solidImage(2, 2, 0), WHEN).has_value());
    CHECK_FALSE(
        writeCapture(dir.path(), info, solidImage(2, 2, 0), solidImage(4, 3, 0), WHEN).has_value());
}

TEST_CASE("writeCapture refuses a map name that would escape the captures directory",
          "[client][capture]") {
    const TempDir dir;
    CaptureInfo info = sampleInfo();
    // A map name reaches the viewer from a file on disk, so this is the trust
    // boundary core/FileSystem.h's resolveUnder exists for.
    info.map = "../escaped";
    const auto folder =
        writeCapture(dir.path(), info, solidImage(4, 3, 0), solidImage(4, 3, 0), WHEN);
    CHECK_FALSE(folder.has_value());
}

// What the device tier shares -- docs/specs/UTA-0014-vulkan-draw-path.md SS 7.
//
// A DEVICE TEST THAT FINDS NO DEVICE FAILS; IT NEVER SKIPS (SS 3 decision 6,
// INV-5). requireRenderer is the only way a test here gets a Renderer, and it
// fails the test with the refusal's own words.

#pragma once

#include "ubundle/Bundle.h"
#include "urender/Renderer.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <ostream>
#include <span>
#include <string>
#include <vector>

namespace uta::test::render {

/// UT99's PolyFlags a fixture sets -- UTA-0014 SS 4.5.
inline constexpr std::uint32_t PF_MASKED = 0x00000002u;
inline constexpr std::uint32_t PF_TRANSLUCENT = 0x00000004u;
inline constexpr std::uint32_t PF_MODULATED = 0x00000040u;
inline constexpr std::uint32_t PF_TWO_SIDED = 0x00000100u;
inline constexpr std::uint32_t PF_UNLIT = 0x00400000u;
inline constexpr std::uint32_t PF_PORTAL = 0x04000000u;

/// Take DISPLAY and WAYLAND_DISPLAY out of this process's environment, so the
/// draw path is shown to need no display (INV-4).
void removeDisplay();

/// A renderer, or a FAILED test naming why there is none.
[[nodiscard]] urender::Renderer requireRenderer(const urender::Config& config);

/// A Result<void> that must hold, failing the test with its message if not.
void requireOk(const Result<void>& result);

struct Rgba {
    std::uint8_t r = 0, g = 0, b = 0, a = 0;
    bool operator==(const Rgba&) const = default;
};
std::ostream& operator<<(std::ostream& out, const Rgba& pixel);

/// The pixel at (x, y) of a Colour readback `width` pixels wide.
[[nodiscard]] Rgba pixelAt(std::span<const std::byte> image, std::uint32_t width, std::uint32_t x,
                           std::uint32_t y);

/// Append a square batch to `geometry`: `half` units either side of (y, z),
/// on the plane x = `distance`, so a default Camera looking along +X sees it.
/// It faces the camera unless `facingAway`: its corners wind about its normal
/// as UTA-0109 SS 4.3 winds every GEOM polygon.
void addSquare(ubundle::Geometry& geometry, float distance, float y, float z, float half,
               const std::string& material, std::uint32_t polyFlags, bool facingAway = false);

/// A bundle holding `geometry` and nothing else.
[[nodiscard]] ubundle::Bundle bundleOf(ubundle::Geometry geometry);

/// One BC7 block, mode 6, every texel exactly `colour`.
///
/// Mode 6 stores each endpoint as seven bits per channel plus ONE p-bit shared
/// by all four channels, so an exact colour needs every channel odd or every
/// channel even. The helper REQUIREs that rather than rounding a channel.
[[nodiscard]] std::vector<std::byte> bc7Solid(const Rgba& colour);

/// Add material `id` to `bundle`: a MATS record, and a 4x4 BC7 base map of
/// `colour` named `<id>:base`, as UTA-0011 SS 4.10 names one.
void addSolidMaterial(ubundle::Bundle& bundle, const std::string& id, const Rgba& colour);

/// One BC5 block whose every texel stores `x` in red and `y` in green.
[[nodiscard]] std::vector<std::byte> bc5Solid(std::uint8_t x, std::uint8_t y);

/// Add material `id`: a solid `base` map and a solid normal map storing
/// `normalX` and `normalY` as umat encodes them (127.5 * c + 128).
void addNormalMappedMaterial(ubundle::Bundle& bundle, const std::string& id, const Rgba& base,
                             std::uint8_t normalX, std::uint8_t normalY);

/// A steady white light: LT_Steady, LE_None, full saturation.
[[nodiscard]] ubundle::Light steadyLight(std::array<float, 3> location, std::uint8_t brightness,
                                         std::uint8_t radius);

/// IEC 61966-2-1's encoding of linear `c`, as an 8-bit value.
[[nodiscard]] double srgbByte(double c);

/// The two floats at (x, y) of a Velocity readback `width` pixels wide.
[[nodiscard]] std::array<float, 2> velocityAt(std::span<const std::byte> image, std::uint32_t width,
                                              std::uint32_t x, std::uint32_t y);

/// Sets an environment variable for one scope and restores it after -- the
/// shape tests/unit/CoreFileSystemTest.cpp uses for the same job.
class EnvScope {
public:
    EnvScope(const char* name, const char* value);
    ~EnvScope();
    EnvScope(const EnvScope&) = delete;
    EnvScope& operator=(const EnvScope&) = delete;

private:
    void set(const char* value);
    const char* name_;
    bool had_ = false;
    std::string previous_;
};

} // namespace uta::test::render

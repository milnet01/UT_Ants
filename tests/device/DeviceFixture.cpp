// What the device tier shares -- DeviceFixture.h.

#include "device/DeviceFixture.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdlib>
#include <utility>

namespace uta::test::render {

void removeDisplay() {
#ifdef _WIN32
    _putenv_s("DISPLAY", "");
    _putenv_s("WAYLAND_DISPLAY", "");
#else
    ::unsetenv("DISPLAY");
    ::unsetenv("WAYLAND_DISPLAY");
#endif
}

urender::Renderer requireRenderer(const urender::Config& config) {
    auto renderer = urender::Renderer::create(config);
    if (!renderer.has_value()) FAIL("no renderer, so this device test cannot pass: " << renderer.error().message());
    return std::move(*renderer);
}

void requireOk(const Result<void>& result) {
    if (!result.has_value()) FAIL(result.error().message());
}

std::ostream& operator<<(std::ostream& out, const Rgba& pixel) {
    return out << "rgba(" << int(pixel.r) << ", " << int(pixel.g) << ", " << int(pixel.b) << ", " << int(pixel.a)
               << ")";
}

Rgba pixelAt(std::span<const std::byte> image, std::uint32_t width, std::uint32_t x, std::uint32_t y) {
    const std::size_t at = (static_cast<std::size_t>(y) * width + x) * 4;
    REQUIRE(at + 4 <= image.size());
    return {std::to_integer<std::uint8_t>(image[at]), std::to_integer<std::uint8_t>(image[at + 1]),
            std::to_integer<std::uint8_t>(image[at + 2]), std::to_integer<std::uint8_t>(image[at + 3])};
}

void addSquare(ubundle::Geometry& geometry, float distance, float y, float z, float half,
               const std::string& material, std::uint32_t polyFlags, bool facingAway) {
    const auto first = static_cast<std::uint32_t>(geometry.vertices.size());
    const float normalX = facingAway ? 1.0f : -1.0f;
    // Seen from the camera: bottom-left, top-left, top-right, bottom-right.
    // ((P1 - P0) x (P2 - P0)) points along -X, toward the camera.
    std::array<std::array<float, 3>, 4> corners = {{
        {distance, y - half, z - half},
        {distance, y - half, z + half},
        {distance, y + half, z + half},
        {distance, y + half, z - half},
    }};
    if (facingAway) std::swap(corners[1], corners[3]);
    const std::array<std::array<float, 2>, 4> uvs = {{{0, 1}, {0, 0}, {1, 0}, {1, 1}}};
    for (std::size_t i = 0; i < 4; ++i)
        geometry.vertices.push_back({corners[i], {normalX, 0, 0}, uvs[i][0], uvs[i][1]});

    const auto firstIndex = static_cast<std::uint32_t>(geometry.indices.size());
    for (const std::uint32_t k : {0u, 1u, 2u, 0u, 2u, 3u}) geometry.indices.push_back(first + k);
    geometry.batches.push_back({material, polyFlags, firstIndex, 6});
}

ubundle::Bundle bundleOf(ubundle::Geometry geometry) {
    ubundle::Bundle bundle;
    bundle.geometry = std::move(geometry);
    return bundle;
}

EnvScope::EnvScope(const char* name, const char* value) : name_(name) {
    if (const char* previous = std::getenv(name)) {
        had_ = true;
        previous_ = previous;
    }
    set(value);
}

EnvScope::~EnvScope() { set(had_ ? previous_.c_str() : nullptr); }

void EnvScope::set(const char* value) {
#ifdef _WIN32
    _putenv_s(name_, value ? value : "");
#else
    if (value) ::setenv(name_, value, 1);
    else ::unsetenv(name_);
#endif
}

} // namespace uta::test::render

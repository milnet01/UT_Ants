// What the device tier shares -- DeviceFixture.h.

#include "device/DeviceFixture.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
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

std::vector<std::byte> bc7Solid(const Rgba& colour) {
    const int parity = colour.r & 1;
    REQUIRE((colour.g & 1) == parity);
    REQUIRE((colour.b & 1) == parity);
    REQUIRE((colour.a & 1) == parity);

    std::array<std::uint8_t, 16> block{};
    int bit = 0;
    // Fields are written least significant bit first.
    const auto put = [&](unsigned value, int count) {
        for (int i = 0; i < count; ++i, ++bit)
            if (((value >> i) & 1u) != 0) block[bit / 8] |= static_cast<std::uint8_t>(1u << (bit % 8));
    };
    put(1u << 6, 7); // mode 6: six zero bits, then a one
    // Endpoints R0 R1 G0 G1 B0 B1 A0 A1, seven bits each; both endpoints the
    // colour, so every index interpolates to it exactly.
    for (const std::uint8_t channel : {colour.r, colour.g, colour.b, colour.a}) {
        put(channel >> 1u, 7);
        put(channel >> 1u, 7);
    }
    put(static_cast<unsigned>(parity), 1); // P0
    put(static_cast<unsigned>(parity), 1); // P1
    put(0, 3);                             // texel 0's index, its top bit implied
    for (int texel = 1; texel < 16; ++texel) put(0, 4);
    REQUIRE(bit == 128);

    std::vector<std::byte> bytes;
    for (const std::uint8_t b : block) bytes.push_back(std::byte{b});
    return bytes;
}

void addSolidMaterial(ubundle::Bundle& bundle, const std::string& id, const Rgba& colour) {
    if (!bundle.materials) bundle.materials.emplace();
    if (!bundle.textures) bundle.textures.emplace();
    bundle.materials->push_back({id, false});
    ubundle::CompressedTexture texture;
    texture.name = id + ":base";
    texture.format = ubundle::BlockFormat::BC7;
    texture.width = texture.height = 4;
    texture.sourceWidth = texture.sourceHeight = 4;
    texture.mipCount = 1;
    texture.blocks = bc7Solid(colour);
    bundle.textures->push_back(std::move(texture));
}

std::vector<std::byte> bc5Solid(std::uint8_t x, std::uint8_t y) {
    // Two BC4 blocks: two endpoint bytes and 48 bits of three-bit indices. Both
    // endpoints the value and every index 0, so every texel is exactly it.
    std::vector<std::byte> bytes(16, std::byte{0});
    bytes[0] = bytes[1] = std::byte{x};
    bytes[8] = bytes[9] = std::byte{y};
    return bytes;
}

void addNormalMappedMaterial(ubundle::Bundle& bundle, const std::string& id, const Rgba& base,
                             std::uint8_t normalX, std::uint8_t normalY) {
    addSolidMaterial(bundle, id, base);
    ubundle::CompressedTexture normal;
    normal.name = id + ":normal";
    normal.format = ubundle::BlockFormat::BC5;
    normal.width = normal.height = 4;
    normal.sourceWidth = normal.sourceHeight = 4;
    normal.mipCount = 1;
    normal.blocks = bc5Solid(normalX, normalY);
    bundle.textures->push_back(std::move(normal));
}

ubundle::Light steadyLight(std::array<float, 3> location, std::uint8_t brightness, std::uint8_t radius) {
    ubundle::Light light;
    light.location = location;
    light.type = 1;
    light.brightness = brightness;
    light.saturation = 255;
    light.radius = radius;
    return light;
}

double srgbByte(double c) {
    const double encoded = c <= 0.0031308 ? 12.92 * c : 1.055 * std::pow(c, 1.0 / 2.4) - 0.055;
    return 255.0 * encoded;
}

std::array<float, 2> velocityAt(std::span<const std::byte> image, std::uint32_t width, std::uint32_t x,
                                std::uint32_t y) {
    const std::size_t at = (static_cast<std::size_t>(y) * width + x) * 2 * sizeof(float);
    REQUIRE(at + 2 * sizeof(float) <= image.size());
    std::array<float, 2> v{};
    std::memcpy(v.data(), image.data() + at, sizeof(v));
    return v;
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

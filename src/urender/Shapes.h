// UTA-0019 SS 4.8: the stand-in shapes a frame draws over the level --
// projectiles, beams, explosions and dummies until real models replace them.
//
// Renderer.h includes this, so no Vulkan or glm type may appear in it (INV-2).
// Renderer::setShapes takes them; Frame.cpp's recordShapes draws them, flat
// and unlit, depth-tested against the level.

#pragma once

#include <array>
#include <cstdint>

namespace uta::urender {

struct Shape {
    enum class Kind : std::uint8_t { Sphere, Line, Cylinder };
    Kind kind = Kind::Sphere;
    std::array<float, 3> a{};      ///< a sphere's or cylinder's centre; a line's start. World units.
    std::array<float, 3> b{};      ///< a line's end; unused otherwise
    float radius = 1;              ///< a sphere's or upright cylinder's radius; a line's half-thickness
    float halfHeight = 0;          ///< an upright (world Z) cylinder's half-height; unused otherwise
    std::array<float, 4> colour{1, 1, 1, 1}; ///< flat colour, linear RGB; alpha below 1 blends over the level
};

} // namespace uta::urender

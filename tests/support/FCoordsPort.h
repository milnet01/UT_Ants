// A port of Unreal Tournament's FCoords operators, for grading a mover's
// placement against the engine's own -- docs/specs/UTA-0119-mover-shapes.md
// SS 4.5 and INV-7.
//
// Ported from the 432 headers, as the Surreal tree carries them:
// Core/Inc/UnMath.h (FVector::TransformPointBy and TransformVectorBy; the
// FCoords operator*= taking a FVector, a FRotator, a FScale and a FCoords;
// FSheerSnap; GMath's SinTab and CosTab) and Engine/Inc/ABrush.h (ToWorld).
//
// THE ORACLE, NOT THE PRODUCT. Nothing in src/ calls this. UTA-0119 INV-7
// grades SS 4.5's formula against it, tests/real/RealMoversTest.cpp places
// static brushes with it to show it is the engine's, and UTA-0014's renderer
// grades its own placement against it. One copy, so all three grade against
// the same thing.
//
// Double throughout. The "fast solution" in operator*=(FCoords) is kept as
// the engine wrote it: it composes exactly for rotations and unsheared
// scales, and not for a shear, which is why UTA-0119 leaves shear out.

#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>

namespace uta::test::fcoords {

struct Vec {
    double x = 0, y = 0, z = 0;
};

inline Vec operator-(Vec a, Vec b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline double dot(Vec a, Vec b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

/// An origin and three axes, as FCoords holds them.
struct Coords {
    Vec origin{0, 0, 0};
    Vec xAxis{1, 0, 0};
    Vec yAxis{0, 1, 0};
    Vec zAxis{0, 0, 1};
};

/// FVector::TransformPointBy.
inline Vec transformPointBy(Vec point, const Coords& coords) {
    const Vec t = point - coords.origin;
    return {dot(t, coords.xAxis), dot(t, coords.yAxis), dot(t, coords.zAxis)};
}

/// FVector::TransformVectorBy.
inline Vec transformVectorBy(Vec vector, const Coords& coords) {
    return {dot(vector, coords.xAxis), dot(vector, coords.yAxis), dot(vector, coords.zAxis)};
}

/// FCoords::operator*=( const FCoords& ), the engine's "fast solution".
inline void multiply(Coords& coords, const Coords& by) {
    coords.origin = transformPointBy(coords.origin, by);
    coords.xAxis = transformVectorBy(coords.xAxis, by);
    coords.yAxis = transformVectorBy(coords.yAxis, by);
    coords.zAxis = transformVectorBy(coords.zAxis, by);
}

/// FCoords::operator*=( const FVector& ).
inline void multiply(Coords& coords, Vec point) { coords.origin = coords.origin - point; }

/// Which sine and cosine a rotation uses.
enum class Trig {
    Table, ///< GMath's: 16384 entries, the angle's lowest two bits dropped
    Exact, ///< std::sin and std::cos of 2 pi angle / 65536
};

inline double sine(std::int32_t angle, Trig trig) {
    if (trig == Trig::Exact) return std::sin(angle * 2.0 * std::numbers::pi / 65536.0);
    static const std::array<float, 16384> TABLE = [] {
        std::array<float, 16384> table{};
        for (int i = 0; i < 16384; ++i)
            table[i] = static_cast<float>(std::sin(2.0 * std::numbers::pi * i / 16384.0));
        return table;
    }();
    return TABLE[(angle >> 2) & 16383];
}

inline double cosine(std::int32_t angle, Trig trig) {
    if (trig == Trig::Exact) return std::cos(angle * 2.0 * std::numbers::pi / 65536.0);
    return sine(angle + 16384, Trig::Table);
}

struct Rotator {
    std::int32_t pitch = 0, yaw = 0, roll = 0;
};

/// FCoords::operator*=( const FRotator& ): yaw, then pitch, then roll.
inline void multiply(Coords& coords, const Rotator& r, Trig trig) {
    const double cy = cosine(r.yaw, trig), sy = sine(r.yaw, trig);
    const double cp = cosine(r.pitch, trig), sp = sine(r.pitch, trig);
    const double cr = cosine(r.roll, trig), sr = sine(r.roll, trig);
    multiply(coords, Coords{{0, 0, 0}, {cy, sy, 0}, {-sy, cy, 0}, {0, 0, 1}});
    multiply(coords, Coords{{0, 0, 0}, {cp, 0, sp}, {0, 1, 0}, {-sp, 0, cp}});
    multiply(coords, Coords{{0, 0, 0}, {1, 0, 0}, {0, cr, -sr}, {0, sr, cr}});
}

/// FScale: Scale, SheerRate and SheerAxis (SHEER_None = 0 ... SHEER_ZY = 6).
struct Scale {
    Vec scale{1, 1, 1};
    double sheerRate = 0;
    int sheerAxis = 0;
};

/// FSheerSnap.
inline double sheerSnap(double sheer) {
    if (sheer < -0.65) return sheer + 0.15;
    if (sheer > +0.65) return sheer - 0.15;
    if (sheer < -0.55) return -0.50;
    if (sheer > +0.55) return 0.50;
    if (sheer < -0.05) return sheer + 0.05;
    if (sheer > +0.05) return sheer - 0.05;
    return 0;
}

/// FCoords::operator*=( const FScale& ): the shear, then the scale.
inline void multiply(Coords& coords, const Scale& s) {
    const double sheer = sheerSnap(s.sheerRate);
    Coords shear;
    switch (s.sheerAxis) {
    case 1: shear.xAxis.y = sheer; break; // SHEER_XY
    case 2: shear.xAxis.z = sheer; break; // SHEER_XZ
    case 3: shear.yAxis.x = sheer; break; // SHEER_YX
    case 4: shear.yAxis.z = sheer; break; // SHEER_YZ
    case 5: shear.zAxis.x = sheer; break; // SHEER_ZX
    case 6: shear.zAxis.y = sheer; break; // SHEER_ZY
    default: break;
    }
    multiply(coords, shear);
    for (Vec* axis : {&coords.xAxis, &coords.yAxis, &coords.zAxis})
        *axis = {axis->x * s.scale.x, axis->y * s.scale.y, axis->z * s.scale.z};
    coords.origin = {coords.origin.x / s.scale.x, coords.origin.y / s.scale.y,
                     coords.origin.z / s.scale.z};
}

/// ABrush::ToWorld: UnitCoords * Location * PostScale * Rotation * MainScale * -PrePivot.
/// A brush-space point p lands at transformPointBy(p, toWorld(...)).
inline Coords toWorld(Vec location, const Rotator& rotation, Vec prePivot, const Scale& mainScale,
                      const Scale& postScale, Trig trig) {
    Coords coords;
    multiply(coords, location);
    multiply(coords, postScale);
    multiply(coords, rotation, trig);
    multiply(coords, mainScale);
    multiply(coords, Vec{-prePivot.x, -prePivot.y, -prePivot.z});
    return coords;
}

} // namespace uta::test::fcoords

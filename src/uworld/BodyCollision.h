// A round body against the level's collision and its movers' --
// docs/specs/UTA-0457-body-collision.md. Where a body stops, and how it slides
// along what it hits; never how it wants to move, which is UTA-0017's.
//
// Positions are a body's centre, in UT99's units and on its axes. The body is
// an upright cylinder (SS 3 item 1), swept by Jolt Physics (SS 3 item 4); no
// Jolt type appears here.

#pragma once

#include "core/Error.h"
#include "ubundle/Bundle.h"
#include "uworld/CollisionQuery.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace uta::uworld {

/// A body's size: an upright cylinder `radius` across and `halfHeight` above
/// and below its centre. The default is the player's, UT99's TMale1
/// (UTA-0121); every character brings its own (SS 3 item 2).
struct BodySize {
    double radius = 17, halfHeight = 39;
};

/// How far short of a surface a sweep stops (SS 4.4): far below what an eye
/// can see, so a body touches what it hits.
inline constexpr double SKIN = 0.125;

/// How many times `slide` turns along a surface before it stops (SS 4.4).
inline constexpr int MAX_SLIDES = 2;

struct BodyHit {
    double fraction = 1;       ///< of the move travelled; 1 when nothing was hit
    Vec3 normal{};             ///< the hit surface's, a unit vector facing the body
    bool startsInside = false; ///< the body overlapped something before it moved
};

struct BodyMove {
    Vec3 end{};                ///< where the body came to rest
    std::vector<BodyHit> hits; ///< each sweep's, in order; at most MAX_SLIDES + 1
};

class BodyWorld {
public:
    /// The level's mesh and each mover's, each mover at its MOVR pose.
    /// `movers` is the bundle's MOVR list; each of `collision.movers` must
    /// have a shape there with its export index, and a mesh of at least one
    /// triangle, or the build refuses naming it.
    [[nodiscard]] static Result<BodyWorld> build(const ubundle::Collision& collision,
                                                 std::span<const ubundle::MoverShape> movers);

    BodyWorld(BodyWorld&&) noexcept;
    BodyWorld& operator=(BodyWorld&&) noexcept;
    ~BodyWorld();

    /// How many movers the world holds: `collision.movers`' count.
    [[nodiscard]] std::size_t moverCount() const noexcept;

    /// Moves mover `i` (its index in `collision.movers`) to a new pose, placed
    /// by UTA-0119 SS 4.5's formula with its MOVR shape's postScale. `rotation`
    /// is pitch, yaw and roll, 65536 to a turn. Refuses an `i` past
    /// moverCount(), changing nothing.
    [[nodiscard]] Result<void> placeMover(std::size_t i, const Vec3& location,
                                          const std::array<std::int32_t, 3>& rotation);

    /// Sweeps a body from `from` towards `to`, stopping SKIN short of the
    /// first surface it meets. A body overlapping something at `from` does
    /// not move: fraction 0, startsInside, and `normal` the way out -- zero
    /// where its centre is in solid, where no face can say which way is out.
    [[nodiscard]] BodyHit sweep(const BodySize& size, const Vec3& from, const Vec3& to) const;

    /// Sweeps `delta`, then slides along what it hits: along the surface, then
    /// along the crease of two, for at most MAX_SLIDES turns.
    [[nodiscard]] BodyMove slide(const BodySize& size, const Vec3& from, const Vec3& delta) const;

    /// Whether a body at `at` overlaps nothing: no surface by more than a
    /// hundredth of a unit, and its centre not in solid.
    [[nodiscard]] bool fits(const BodySize& size, const Vec3& at) const;

private:
    struct State;
    explicit BodyWorld(std::unique_ptr<State> state) noexcept;
    std::unique_ptr<State> state_;
};

} // namespace uta::uworld

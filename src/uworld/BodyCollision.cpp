// A round body against the level's collision -- BodyCollision.h.

#include "uworld/BodyCollision.h"

#include "core/Angles.h"
#include "uworld/CollisionMesh.h"

// Jolt.h comes before any other Jolt header, as Jolt requires.
#include <Jolt/Jolt.h>

#include <Jolt/Core/Factory.h>
#include <Jolt/Physics/Collision/CollideShape.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/CollisionDispatch.h>
#include <Jolt/Physics/Collision/ShapeCast.h>
#include <Jolt/Physics/Collision/Shape/CylinderShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/RegisterTypes.h>

#include <algorithm>
#include <cmath>
#include <mutex>
#include <optional>
#include <string>
#include <utility>

namespace uta::uworld {
namespace {

/// SS 4.5: UT99 units to Jolt's. A power of two, so the scaling is exact, and
/// a map's +-32768 becomes +-512, where Jolt's metre-tuned tolerances fit.
constexpr double TO_JOLT = 1.0 / 64.0;

/// SS 4.5: how much of the cylinder's rim Jolt rounds, inside its size.
constexpr double RIM = 0.5;

/// Deeper than this is overlapping; shallower is touching. Well under SKIN,
/// well over Jolt's own tolerance (1e-4 of its units, 0.0064 of ours).
constexpr double OVERLAP = 0.01;

/// The steepest a sweep may meet a surface and still back off SKIN / |d.n|:
/// past it the back-off is capped at SKIN / GRAZE (4 units), and the body
/// stops closer than SKIN to a surface it is gliding along.
constexpr double GRAZE = 1.0 / 32.0;

/// UT99 is Z up and Jolt's cylinder Y up. A rotation, not a mirror, so a
/// triangle's front face stays its front face: (x, y, z) -> (x, z, -y).
JPH::Vec3 toJoltDirection(const Vec3& v) {
    return JPH::Vec3(static_cast<float>(v.x * TO_JOLT), static_cast<float>(v.z * TO_JOLT),
                     static_cast<float>(-v.y * TO_JOLT));
}

/// A Jolt direction back on UT99's axes, unscaled: for normals.
Vec3 fromJoltDirection(JPH::Vec3Arg v) { return {v.GetX(), -v.GetZ(), v.GetY()}; }

Vec3 unit(const Vec3& v) {
    const double l = length(v);
    return l > 0 ? v * (1.0 / l) : Vec3{};
}

Vec3 cross(const Vec3& a, const Vec3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

/// UTA-0119 SS 4.5's Y * P * R, rows by columns, for pitch, yaw and roll.
/// The sine and cosine are core/Angles.h's, never the platform's: a mover
/// turning is simulation (UTA-0017 SS 4.6). The renderer's twin is urender's
/// Placement.cpp rotationOf; uworld may not link urender (rule 4).
using Matrix = std::array<std::array<double, 3>, 3>;

Matrix rotationOf(const std::array<std::int32_t, 3>& rotation) {
    const double cp = core::cosineOf(rotation[0]), sp = core::sineOf(rotation[0]);
    const double cy = core::cosineOf(rotation[1]), sy = core::sineOf(rotation[1]);
    const double cr = core::cosineOf(rotation[2]), sr = core::sineOf(rotation[2]);
    const Matrix yaw{{{cy, -sy, 0}, {sy, cy, 0}, {0, 0, 1}}};
    const Matrix pitch{{{cp, 0, -sp}, {0, 1, 0}, {sp, 0, cp}}};
    const Matrix roll{{{1, 0, 0}, {0, cr, sr}, {0, -sr, cr}}};
    const auto times = [](const Matrix& a, const Matrix& b) {
        Matrix m{};
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 3; ++c)
                for (int k = 0; k < 3; ++k) m[r][c] += a[r][k] * b[k][c];
        return m;
    };
    return times(times(yaw, pitch), roll);
}

/// Jolt's allocator, factory and shape types, registered once a process.
void startJolt() {
    static std::once_flag once;
    std::call_once(once, [] {
        JPH::RegisterDefaultAllocator();
        JPH::Factory::sInstance = new JPH::Factory();
        JPH::RegisterTypes();
    });
}

/// A mesh shape for `mesh`, each point placed by `place`. An empty mesh, or
/// one Jolt refuses, is an error naming `what`.
template <typename Place>
Result<JPH::RefConst<JPH::Shape>> meshShape(const CollisionMesh& mesh, bool mirrored, Place place,
                                            const std::string& what) {
    if (mesh.triangles.empty()) return fail(ErrorCode::MalformedData, what + " has no surface to collide with");
    JPH::VertexList vertices;
    vertices.reserve(mesh.points.size());
    for (const Vec3& p : mesh.points) {
        const JPH::Vec3 j = toJoltDirection(place(p));
        vertices.push_back(JPH::Float3(j.GetX(), j.GetY(), j.GetZ()));
    }
    JPH::IndexedTriangleList triangles;
    triangles.reserve(mesh.triangles.size());
    for (const auto& t : mesh.triangles)
        // A mirroring scale turns a face inside out; swapping two corners
        // turns it back (UTA-0119 SS 4.5 step 3).
        triangles.push_back(mirrored ? JPH::IndexedTriangle(t[0], t[2], t[1], 0)
                                     : JPH::IndexedTriangle(t[0], t[1], t[2], 0));
    const JPH::MeshShapeSettings settings(std::move(vertices), std::move(triangles));
    const JPH::ShapeSettings::ShapeResult made = settings.Create();
    if (made.HasError())
        return fail(ErrorCode::MalformedData, what + "'s mesh was refused: " + std::string(made.GetError()));
    return JPH::RefConst<JPH::Shape>(made.Get());
}

/// The deepest overlap deeper than OVERLAP, so a body resting SKIN off a
/// surface overlaps nothing.
class OverlapCollector final : public JPH::CollideShapeCollector {
public:
    void AddHit(const JPH::CollideShapeResult& hit) override {
        if (hit.mPenetrationDepth > OVERLAP * TO_JOLT && hit.mPenetrationDepth > depth) {
            depth = hit.mPenetrationDepth;
            axis = hit.mPenetrationAxis;
        }
    }
    float depth = 0;
    JPH::Vec3 axis = JPH::Vec3::sZero();
};

/// One mover in the world.
struct Mover {
    ubundle::CollisionTree tree;      ///< in its pivot space
    CollisionMesh mesh;               ///< the tree's
    std::array<float, 3> postScale{}; ///< its MOVR shape's
    std::array<std::int32_t, 3> rotation{};
    Matrix turn{};                    ///< rotationOf(rotation)
    Vec3 location{};
    JPH::RefConst<JPH::Shape> shape;  ///< the mesh rotated and scaled
    JPH::Mat44 place = JPH::Mat44::sIdentity(); ///< its location
    std::uint32_t exportIndex = 0;
};

/// A mover's mesh at `rotation`: UTA-0119 SS 4.5's postScale (Y P R q), with
/// its location left to the shape's placement.
Result<JPH::RefConst<JPH::Shape>> moverShape(const Mover& mover,
                                             const std::array<std::int32_t, 3>& rotation) {
    const Matrix r = rotationOf(rotation);
    const auto& s = mover.postScale;
    // r is rotationOf(rotation), not mover.turn: placeMover asks before it
    // commits the turn.
    const auto place = [&](const Vec3& q) {
        const std::array<double, 3> v{q.x, q.y, q.z};
        std::array<double, 3> out{};
        for (int row = 0; row < 3; ++row)
            out[row] = s[row] * (r[row][0] * v[0] + r[row][1] * v[1] + r[row][2] * v[2]);
        return Vec3{out[0], out[1], out[2]};
    };
    const bool mirrored = static_cast<double>(s[0]) * s[1] * s[2] < 0;
    return meshShape(mover.mesh, mirrored, place, "mover " + std::to_string(mover.exportIndex));
}

JPH::Mat44 placeAt(const Vec3& location) { return JPH::Mat44::sTranslation(toJoltDirection(location)); }

/// A world point in a mover's pivot space: UTA-0119 SS 4.5's placement undone,
/// q = R^T ((p - location) / postScale). None where a postScale is 0, which
/// flattens the mover to nothing a point can be inside.
std::optional<Vec3> intoPivot(const Mover& mover, const Vec3& p) {
    const Vec3 d = p - mover.location;
    const std::array<double, 3> v{d.x, d.y, d.z};
    std::array<double, 3> scaled{};
    for (int k = 0; k < 3; ++k) {
        if (mover.postScale[k] == 0) return std::nullopt;
        scaled[k] = v[k] / mover.postScale[k];
    }
    std::array<double, 3> q{};
    for (int col = 0; col < 3; ++col)
        for (int row = 0; row < 3; ++row) q[col] += mover.turn[row][col] * scaled[row];
    return Vec3{q[0], q[1], q[2]};
}

} // namespace

struct BodyWorld::State {
    ubundle::CollisionTree levelTree;
    JPH::RefConst<JPH::Shape> level; ///< null where the level has no surface
    std::vector<Mover> movers;

    /// Whether `p` is in solid, in the level or a mover. A body whose centre
    /// is deep in solid touches no surface, so the mesh alone cannot see it.
    [[nodiscard]] bool inSolid(const Vec3& p) const {
        if (!isEmpty(levelTree, p)) return true;
        return std::ranges::any_of(movers, [&](const Mover& m) {
            const auto q = intoPivot(m, p);
            return q && !isEmpty(m.tree, *q);
        });
    }

    /// The deepest overlap of a body at `at` with any surface. Asked before a
    /// sweep, because the cast ignores a face it moves away from, so a body
    /// half through a wall would otherwise pass for free. A face is only met
    /// from its front: a body whose centre is behind one is in solid, which
    /// inSolid answers first.
    [[nodiscard]] OverlapCollector overlap(const JPH::Shape& body, const Vec3& at) const {
        const JPH::CollideShapeSettings settings;
        OverlapCollector deepest;
        eachTarget([&](const JPH::Shape& target, JPH::Mat44Arg place) {
            JPH::CollisionDispatch::sCollideShapeVsShape(&body, &target, JPH::Vec3::sOne(), JPH::Vec3::sOne(),
                                                         placeAt(at), place, JPH::SubShapeIDCreator(),
                                                         JPH::SubShapeIDCreator(), settings, deepest);
        });
        return deepest;
    }

    /// Each shape a body collides with, with its placement.
    template <typename Visit>
    void eachTarget(Visit visit) const {
        if (level != nullptr) visit(*level, JPH::Mat44::sIdentity());
        for (const Mover& m : movers) visit(*m.shape, m.place);
    }
};

BodyWorld::BodyWorld(std::unique_ptr<State> state) noexcept : state_(std::move(state)) {}
BodyWorld::BodyWorld(BodyWorld&&) noexcept = default;
BodyWorld& BodyWorld::operator=(BodyWorld&&) noexcept = default;
BodyWorld::~BodyWorld() = default;

Result<BodyWorld> BodyWorld::build(const ubundle::Collision& collision,
                                   std::span<const ubundle::MoverShape> movers) {
    startJolt();
    auto state = std::make_unique<State>();

    state->levelTree = collision.level;
    const CollisionMesh level = collisionMesh(collision.level);
    if (!level.triangles.empty()) {
        UTA_TRY(auto shape, meshShape(level, false, [](const Vec3& p) { return p; }, "the level"));
        state->level = shape;
    }

    for (const ubundle::MoverCollision& tree : collision.movers) {
        const auto shape = std::ranges::find(movers, tree.exportIndex, &ubundle::MoverShape::exportIndex);
        if (shape == movers.end())
            return fail(ErrorCode::MalformedData,
                        "mover " + std::to_string(tree.exportIndex) + " has collision but no MOVR shape");
        Mover mover;
        mover.tree = tree.tree;
        mover.mesh = collisionMesh(tree.tree);
        mover.postScale = shape->postScale;
        mover.rotation = shape->rotation;
        mover.turn = rotationOf(shape->rotation);
        mover.location = {shape->location[0], shape->location[1], shape->location[2]};
        mover.exportIndex = tree.exportIndex;
        UTA_TRY(auto made, moverShape(mover, mover.rotation));
        mover.shape = made;
        mover.place = placeAt(mover.location);
        state->movers.push_back(std::move(mover));
    }
    return BodyWorld(std::move(state));
}

std::size_t BodyWorld::moverCount() const noexcept { return state_->movers.size(); }

Result<void> BodyWorld::placeMover(std::size_t i, const Vec3& location,
                                   const std::array<std::int32_t, 3>& rotation) {
    if (i >= state_->movers.size())
        return fail(ErrorCode::InvalidArgument, "no mover " + std::to_string(i) + "; the world holds " +
                                                    std::to_string(state_->movers.size()));
    Mover& mover = state_->movers[i];
    // A turn rebuilds the mesh: a postScale after the rotation is not a
    // rotation and a scale Jolt can place a shape by. A move only re-places it.
    if (rotation != mover.rotation) {
        UTA_TRY(auto made, moverShape(mover, rotation));
        mover.shape = made;
        mover.rotation = rotation;
        mover.turn = rotationOf(rotation);
    }
    mover.location = location;
    mover.place = placeAt(location);
    return {};
}

BodyHit BodyWorld::sweep(const BodySize& size, const Vec3& from, const Vec3& to) const {
    const Vec3 delta = to - from;
    const double moveLength = length(delta);
    JPH::CylinderShape body(static_cast<float>(size.halfHeight * TO_JOLT), static_cast<float>(size.radius * TO_JOLT),
                            static_cast<float>(RIM * TO_JOLT));
    body.SetEmbedded();

    // A body overlapping something does not move (SS 4.4). With its centre
    // in empty space, the way out is the deepest overlap's. With its centre
    // in solid there is no telling: a face knows its plane, not which side
    // of it is solid, so its push may lead deeper in.
    if (state_->inSolid(from)) return BodyHit{0, {}, true};
    const OverlapCollector stuck = state_->overlap(body, from);
    if (stuck.depth > 0) return BodyHit{0, unit(fromJoltDirection(-stuck.axis)), true};

    const JPH::ShapeCast cast(&body, JPH::Vec3::sOne(), placeAt(from), toJoltDirection(delta));
    JPH::ShapeCastSettings settings;
    settings.mActiveEdgeMovementDirection = toJoltDirection(delta);
    JPH::ClosestHitCollisionCollector<JPH::CastShapeCollector> closest;
    state_->eachTarget([&](const JPH::Shape& target, JPH::Mat44Arg place) {
        JPH::CollisionDispatch::sCastShapeVsShapeWorldSpace(cast, settings, &target, JPH::Vec3::sOne(), {}, place,
                                                           JPH::SubShapeIDCreator(), JPH::SubShapeIDCreator(),
                                                           closest);
    });
    if (!closest.HadHit()) return BodyHit{};

    const JPH::ShapeCastResult& hit = closest.mHit;
    // The penetration axis moves the surface out of the body, so the way out
    // for the body, and the surface's normal facing it, is its reverse.
    const Vec3 normal = unit(fromJoltDirection(-hit.mPenetrationAxis));

    const double contact = hit.mFraction * moveLength;
    const double facing = std::abs(dot(unit(delta), normal));
    const double travel = std::max(0.0, contact - SKIN / std::max(facing, GRAZE));
    return BodyHit{moveLength > 0 ? travel / moveLength : 0, normal, false};
}

BodyMove BodyWorld::slide(const BodySize& size, const Vec3& from, const Vec3& delta) const {
    BodyMove move{from, {}};
    Vec3 remaining = delta;
    Vec3 firstNormal{};
    for (int turn = 0;; ++turn) {
        const BodyHit hit = sweep(size, move.end, move.end + remaining);
        move.hits.push_back(hit);
        move.end = move.end + remaining * hit.fraction;
        if (hit.fraction >= 1 || hit.startsInside || turn == MAX_SLIDES) break;

        const Vec3 rest = remaining * (1 - hit.fraction);
        const Vec3 crease = cross(firstNormal, hit.normal);
        if (turn == 0 || length(crease) < 1e-6) {
            // Along the surface: the rest of the move less its part into it.
            remaining = rest - hit.normal * dot(rest, hit.normal);
            if (turn == 0) firstNormal = hit.normal;
        } else {
            // Along the crease two surfaces make: the only way left that
            // goes into neither.
            const Vec3 along = unit(crease);
            remaining = along * dot(rest, along);
        }
        if (length(remaining) < SKIN / 8) break;
    }
    return move;
}

bool BodyWorld::fits(const BodySize& size, const Vec3& at) const {
    JPH::CylinderShape body(static_cast<float>(size.halfHeight * TO_JOLT), static_cast<float>(size.radius * TO_JOLT),
                            static_cast<float>(RIM * TO_JOLT));
    body.SetEmbedded();
    return state_->overlap(body, at).depth == 0 && !state_->inSolid(at);
}

} // namespace uta::uworld

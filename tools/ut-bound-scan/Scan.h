// Render bounds that miss the geometry they guard -- UTA-0328, from UTA-0283.
//
// UT99 skips a BSP node's whole subtree -- its front, its back and its
// coplanars -- when the node's render bound leaves the view. So drawn geometry
// outside that box can vanish, and the picture keeps whatever the frame buffer
// held: the hall-of-mirrors smear GAME-0187 found on TheBoat, node 1305.
//
// Pure: it reads a Model and nothing else, so the unit tests build one in
// memory.

#pragma once

#include "upkg/Geometry.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace uta::boundscan {

/// UT99's surface flag for a polygon nothing draws. An invisible surface
/// cannot vanish, so it is not geometry a bound guards.
inline constexpr std::uint32_t PF_INVISIBLE = 0x1;

/// What is wrong with one node's render bound.
enum class Kind {
    /// A valid box that does not hold every drawn polygon under the node.
    Miss,
    /// A box whose minimum exceeds its maximum on some axis. Reported, not
    /// measured: GAME-0187 patched TheBoat's 956 of these and the picture
    /// did not change.
    Inverted,
    /// A well-formed box the file flags not valid.
    Invalid,
};

struct Finding {
    Kind kind = Kind::Miss;
    std::size_t node = 0;
    std::int32_t bound = 0;
    /// Miss only: how far, in world units, the drawn geometry under the node
    /// reaches past the box on its worst axis and side.
    double excess = 0;
    /// The file's own valid flag on the box.
    bool valid = false;
};

struct Scan {
    /// In node order.
    std::vector<Finding> findings;
    std::size_t nodes = 0;
    /// Nodes with a valid, well-formed render bound and drawn geometry under
    /// them -- the ones a Miss could be found on.
    std::size_t checked = 0;
    std::size_t misses = 0;
    std::size_t inverted = 0;
    std::size_t invalid = 0;
    /// The largest Miss excess; 0 when there is none.
    double worst = 0;
};

/// Every node reached from node 0 whose render bound misses the drawn geometry
/// under it by more than `tolerance` world units, or is inverted or flagged
/// not valid. A node with no drawn geometry under it, or no render bound, is
/// not reported. Indices outside their tables are read as absent.
[[nodiscard]] Scan scanModel(const upkg::Model& model, double tolerance);

} // namespace uta::boundscan

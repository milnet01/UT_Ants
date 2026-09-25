// What a reach spec's flags allow, and whether a pawn may take an edge --
// UTA-0085.
//
// UT99 stores a ReachSpec's `reachFlags` as a bit set of EReachSpecFlags,
// Engine/Inc/UnPath.h: the moves a pawn needs to follow the spec. A pawn may
// take the spec when it can make every one of those moves and its body fits
// the collision size the spec was built for -- APawn::calcMoveFlags and the
// supports() test in the engine's route search. NavEdge carries both raw;
// this names the bits and applies that rule, so a bot planner can tell a
// walk from a jump, a swim, a flight and a door.
//
// Bits outside the seven are reported rather than dropped: the second test
// tier counts them over a real install (tests/real/RealGraphsTest.cpp).

#pragma once

#include "unav/Graphs.h"

#include <cstdint>

namespace uta::unav {

/// EReachSpecFlags, by value.
enum ReachFlag : std::int32_t {
    R_WALK = 1,         ///< walking, or a short drop
    R_FLY = 2,          ///< flying
    R_SWIM = 4,         ///< swimming
    R_JUMP = 8,         ///< a jump
    R_DOOR = 16,        ///< a door that must be opened
    R_SPECIAL = 32,     ///< special handling: a teleporter, a lift
    R_PLAYERONLY = 64,  ///< players only, never a bot
};

/// Every bit a flag above names.
inline constexpr std::int32_t KNOWN_REACH_FLAGS = R_WALK | R_FLY | R_SWIM | R_JUMP | R_DOOR | R_SPECIAL | R_PLAYERONLY;

/// The bits of `reachFlags` no ReachFlag names. Zero on a well-formed spec.
[[nodiscard]] constexpr std::int32_t unknownReachBits(std::int32_t reachFlags) noexcept {
    return reachFlags & ~KNOWN_REACH_FLAGS;
}

/// What a pawn can do and how big it is: APawn::calcMoveFlags' inputs.
struct PawnMoves {
    bool walk = true;
    bool fly = false;
    bool swim = true;
    bool jump = true;
    bool openDoors = true;
    bool special = true;  ///< may use teleporters and lifts
    bool player = false;  ///< a player, so R_PLAYERONLY is allowed
    std::int32_t collisionRadius = 17;
    std::int32_t collisionHeight = 39;
};

/// The reach flags `pawn` can satisfy.
[[nodiscard]] constexpr std::int32_t moveFlagsOf(const PawnMoves& pawn) noexcept {
    return (pawn.walk ? R_WALK : 0) | (pawn.fly ? R_FLY : 0) | (pawn.swim ? R_SWIM : 0) | (pawn.jump ? R_JUMP : 0) |
           (pawn.openDoors ? R_DOOR : 0) | (pawn.special ? R_SPECIAL : 0) | (pawn.player ? R_PLAYERONLY : 0);
}

/// Whether `pawn` may follow `edge`: its body fits the spec's collision size,
/// and it can make every move the spec's flags name. A bit no ReachFlag names
/// is one no pawn can satisfy, so such an edge is never taken.
[[nodiscard]] constexpr bool mayTraverse(const NavEdge& edge, const PawnMoves& pawn) noexcept {
    return edge.collisionRadius >= pawn.collisionRadius && edge.collisionHeight >= pawn.collisionHeight &&
           (edge.reachFlags & ~moveFlagsOf(pawn)) == 0;
}

} // namespace uta::unav

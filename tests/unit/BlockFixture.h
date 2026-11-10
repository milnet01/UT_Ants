// Collision trees built in memory from solid blocks, for UTA-0457's cases
// (docs/specs/UTA-0457-body-collision.md SS 7). Shared by CollisionMeshTest
// and BodyCollisionTest.
//
// A WORLD IS EMPTY EXCEPT WHERE A BLOCK IS. Each block is a box, possibly
// turned about Z, whose six faces carry their real outlines, so the mesh a
// body collides with is built by uworld's own collisionMesh. The tree is a
// chain: a block's planes in turn, every node a CSG node (UTA-0111 SS 4.5's
// IsCsg), so going back means "inside this plane, try its next" and going
// front means "outside this block, try the next block".
//
// Every front link of one block leads to the SAME next block, so a node is
// reached by more than one path. A real tree reaches no node twice; this one
// trades that for a size that grows with the block count rather than its
// power, and uworld's readers walk either.

#pragma once

#include "ubundle/Bundle.h"
#include "uworld/CollisionQuery.h"

#include <array>
#include <vector>

namespace uta::test::blocks {

using uworld::Vec3;

/// A box's eight corners: bit 0 of the index picks x, bit 1 y, bit 2 z.
struct Block {
    std::array<Vec3, 8> corners;
};

/// The box from `min` to `max`, turned `yawDegrees` about Z around its
/// centre (counter-clockwise seen from above).
[[nodiscard]] Block box(const Vec3& min, const Vec3& max, double yawDegrees = 0);

/// A face that is not solid on either side: water, a portal. Its node is
/// NF_NotCsg, so a walk passes through it.
struct Sheet {
    std::array<Vec3, 4> corners;
};

/// A tree whose solid space is the union of `blocks`, with each sheet in
/// `sheets` as a non-solid face ahead of them.
[[nodiscard]] ubundle::CollisionTree solidBlocks(const std::vector<Block>& blocks,
                                                 const std::vector<Sheet>& sheets = {});

} // namespace uta::test::blocks

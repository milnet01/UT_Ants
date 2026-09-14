// The collision-tree checks, under ubake's names.
//
// UTA-0158 moved Vec3, dot, length, isEmpty, Hit, trace and traceOut to
// src/uworld/CollisionQuery.h, so a runtime target can use them. This header
// brings them back into uta::ubake, so the baker's code and tests read them as
// they did -- the same forwarding tools/ut-paths/Trace.h does for ut-paths.

#pragma once

#include "uworld/CollisionQuery.h"

namespace uta::ubake {

using uworld::dot;
using uworld::Hit;
using uworld::isEmpty;
using uworld::length;
using uworld::trace;
using uworld::traceOut;
using uworld::Vec3;

} // namespace uta::ubake

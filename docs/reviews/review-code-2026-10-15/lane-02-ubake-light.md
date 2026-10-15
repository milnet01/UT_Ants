# Lane report: baked lighting (`src/ubake`)

I found two High problems. Both break the project's rule that the baker must not use the platform maths library. The worst is that added lamps are placed with `std::sin`/`std::cos`, so the same map can bake to different bundle bytes on different compilers.

## Line counts as read (native `Read`)
FireStill.cpp 62 · FireStill.h 48 · Flames.cpp 228 · Flames.h 68 · Lamps.cpp 163 · Lamps.h 52 · LightModel.cpp 225 · LightModel.h 171 · LightProbes.cpp 442 · LightProbes.h 170 · Liquids.cpp 71 · Liquids.h 44 · MaterialLight.cpp 66 · MaterialLight.h 37 · Occlusion.cpp 172 · ShadowMask.cpp 460 · ShadowMask.h 56 · Strips.cpp 152 · Strips.h 27 · SurfaceRays.cpp 352 · SurfaceRays.h 109 · TileKind.cpp 290 · TileKind.h 80

## Already in context when I arrived
- The global `~/.claude/CLAUDE.md`. It describes the review skills and the lane machinery.
- The user-email context block.
- The review-lane agent file.
- No project `CLAUDE.md`, no memory index and no git snapshot.

I read the shared context once, as the brief told me. I did not re-read it, and I opened no test file. Every search used `exclude_glob tests/**`. The brief and the lane file did not conflict, so nothing needed resolving.

**Contract sources I opened:**
- `design.md`, the Determinism bullet.
- UTA-0049 §2 and §§8–9.
- UTA-0112 §4.10, around `hypot`.
- UTA-0256 §4.3 and §4.7.
- UTA-0263 §4.3 and §4.5.
- UTA-0326 §4.3 and §4.4.
- UTA-0162 §4.2.
- UTA-0105 §4.1 to §4.3.
- UTA-0164 §4.2 and §4.3.
- `litDirectly`, found with `find_definition`.

I did not open UTA-0277 or UTA-0338. I also did not open `Charts.h` or `ufire`, which are outside this lane.

## Critical (0)

## High (2)
- **[dim 2] `Lamps.cpp:42-43`** — `const double c = std::cos(angle); const double s = std::sin(angle);`
  - **Problem:** `turned` uses the platform maths library to rotate every vertex position, every normal and every flame base that `buildLamps` writes into the LAMP section (lines 106, 107, 114). `design.md` says: "no platform maths library in … the baker… one map, recipe and baker version to hash to one bundle on any machine". UTA-0049 §9 says glibc, LLVM's libm and the MSVC runtime are "not bit-identical". A lamp's vertices are rounded to `float`, so an ulp difference in the sine will sometimes flip a stored float. The bundle hash then differs between GCC and MSVC.
  - **Wrong side:** the code. The spec's transform (§4.3 step 2) needs only a turn about the vertical, and the project already has a sine that gives the same bits everywhere.
  - **Fix:** use `cosineOf` and `sineOf` from `LightModel.h`. Yaw is already in the 65536-to-a-turn units they take.
- **[dim 2] `Flames.cpp:40`, `:44` and `:88`** — `return std::hypot(a[0] - b[0], a[1] - b[1]);`, `std::hypot(... a[2] - b[2])`, `const double across = std::hypot(normal.x, normal.y);`
  - **Problem:** UTA-0112 §4.10 says by name that `horizontal` stays out of the baker because "it calls `std::hypot`, which this spec keeps out of the baker". UTA-0049 rules out the platform maths library for exactly this reason. `Flames.h:11` claims "THE SAME BYTES ON EVERY COMPILER". But `hypot` sets the sheet's horizontal axis `h`, which in turn sets the width, the base and the merge test `distance < min(width)/2`. Every one of those reaches FLAM.
  - **Wrong side:** the code.
  - **Fix:** use `std::sqrt` of the sum of squares. `sqrt` is correctly rounded, and `LightModel.cpp:15` already relies on that.

## Medium (2)
- **[dim 4] `ShadowMask.cpp:94-95`** — `double sineOf(std::int32_t angle) noexcept { return std::sin(angle * 2.0 * std::numbers::pi / 65536.0); }`
  - **Problem:** this is a local copy of `LightModel.h`'s `sineOf` and `cosineOf`, and it has drifted. The original was written to keep the maths library out (`LightModel.h:15-21`); this copy calls it. `LightModel.h` is included at line 9. The copy hides it because unqualified lookup stops at the unnamed namespace, so the build raises no ambiguity error.
  - **Impact:** `MOVER_PAD` (line 43) only makes a flipped `moverReach` bit less likely. The baker still breaks `design.md`'s rule, and a box edge that lands within an ulp can still flip a byte.
  - **Fix:** delete the local pair and call `uta::ubake::sineOf` and `cosineOf`. After that, `MOVER_PAD`'s stated reason no longer applies.
- **[dim 5] `Strips.cpp:122-127` and `:100-104`** — `if (auto row = rowBetween(...)) candidates.push_back(std::move(*row));`
  - **Problem:** take a group of *m* identical cylinder lights in one line with small gaps. Every pair of them is a valid candidate row, and each row stores all its members. The `longest` prune does not help, because it grows with the group's size. That makes about m²/2 rows of about m/3 indices each, and about m³ calls to `blocked`. At m=3000 that is roughly 4.5 million rows holding about 1000 indices each, around 36 GB. A downloaded map is untrusted input, so the bake can run out of memory or hang.
  - **Fix:** for each pair, keep only the members' end indices and count, not the member list. Better still, find each group's maximal collinear runs in one sorted pass, so the pairs are never enumerated.

## Low / Info
- **[dim 3] `Lamps.cpp:101`** — `lamp.light.rotation[1] += static_cast<std::int32_t>(wanted.yaw);` The yaw comes from the recipe and the rotation from an untrusted package, so this signed `int32` addition can overflow, which is undefined behaviour. The same pattern is at **`ShadowMask.cpp:132`**: `rotation[axis] += key.second[axis];` (KeyRot from the package). Fix: add in `uint32_t` and cast back.
- **[dim 2] `LightProbes.cpp:151`** — `const double within = std::cos(SUN_RADIUS);` This is the maths library in the baker again. The practical effect is tiny: it only matters when one of the 162 directions lands within an ulp of the cone's edge. Fix: write `cos(0.035)` as a literal, or use `detail::cosPolynomial(SUN_RADIUS)`.
- **[dim 5] `LightProbes.cpp:376-401`** — when `reach` is empty (a map with no NavigationPoint), the lattice walk loops over the triangle's whole bounding box, not just its plane slab. One sloped triangle 500,000 units across on all three axes is about 6×10¹⁰ iterations: the bake hangs on hostile input. Fix: when there is no reach, bound the walk by `MAX_COORDINATE`, or iterate over the plane slab.
- **INFO:** I could not see whether `charts::outlineOf` can return empty `heights`. If it can, `ShadowMask.cpp:233` dereferences `min_element` of an empty range.
- **INFO:** I did not check the UTA-0277 contract text against `TileKind.cpp`. I checked TileKind's code only for undefined behaviour, bounds and determinism, and found nothing.

## Covered by spec and looks correct (depth pass)
- **Strips:** I checked rules 1–5, the tie order and the marking against UTA-0162 §4.2. The key fields are the twelve bytes, the rotation and the four bools.
- **Flames:** the sheet flags, the merge rule, the seed being the lowest surface, and the §4.5 nearest-light rule that skips absorbed lights all match.
- **Lamps:** step 1's refusals, step 2 (yaw added to `rotation[1]`) and step 3's "drawn surface or flame" test match UTA-0256 §4.3, apart from the maths library. `switchOff` matches §4.7: it refuses strips and tied flames, and remaps flame ties.
- **ShadowMask:** the pair rule, the four sub-samples, the all-lit and all-dark handling, the occluder flags plus FakeBackdrop, the packing order and the lamp and sun indices match UTA-0326 §4.3.
- **Occlusion:** steps 4–5, the white block, the packing and rounding the height up to a multiple of 4 match UTA-0164 §§4.2–4.3.
- **Liquids:** §§4.1–4.3 match, including the ramp sampling.
- **SurfaceRays:** ties go to the lower triangle number in `first` and in `firstHas`, and the tree is built in a fixed total order.
- **Threading:** each job in LightProbes, Occlusion and ShadowMask writes only to its own slots, and every `thread_local` stack belongs to one function.
- **N/A:** observability, accessibility, localisation and privacy (none in this lane's surface). The wall-clock light pulse is not in this lane's code.

## Open questions
- `ShadowMask.cpp:43` says the libm sine with a pad is the intended design. Is that an accepted exception to `design.md`'s rule? If so, the exception is not written down anywhere I read.
- UTA-0049 §9 proposes a lint that refuses `std::sin` and `std::cos` in `src/ubake`. Four call sites (above) show it is not enforced. Is that lint planned?

## 3 items to fix first
1. **`Lamps.cpp` `turned`:** it writes maths-library results straight into LAMP vertex floats. This is the most likely way the same map bakes to different bytes on different compilers.
2. **`Flames.cpp` `std::hypot`:** a spec names `hypot` as banned from the baker, yet it sets FLAM's base, width and merges.
3. **`Strips.cpp` candidate explosion:** a hostile downloaded map can make the bake run out of memory.

**Subject line counts, as Read numbered them:** Bake.cpp 1672 · Bake.h 228 · Charts.cpp 158 · Charts.h 60 · Geometry.cpp 350 · Geometry.h 94 · Install.cpp 272 · Movers.cpp 236 · Movers.h 76 · Name.cpp 162 · Name.h 95 · TextureCache.cpp 249 · TextureCache.h 73 · Zones.cpp 125 · Zones.h 38

**In my context before I read the subject:**
- The global `~/.claude/CLAUDE.md`, which describes the review skills.
- The harness's userEmail note.
- The review-lane agent prompt.

There was no project `CLAUDE.md` and no git snapshot.

**One conflict between the brief and the agent file, and how I settled it:** the brief's OUTPUT skeleton opens at "## Critical", but the lane file decides how a return opens. So the two lists above come first.

**What I read beyond the subject:**
- The binding spec sections: UTA-0011 §4.2–4.7 and §6; UTA-0109 §4.3–4.4; UTA-0119 §4.3–4.6; UTA-0156 §4.3 and §4.5; UTA-0350 §4.2 and §6; UTA-0338 §4.4; UTA-0052 §4.6.
- `cpp.md`.
- Three cross-reference checks, each to settle a claim: `detail::resolvedRecord` (Actors.cpp) for the known trap, and the `ubundle::Zone` and `MoverShape` defaults and what their writers check.

I opened no test files, and every search excluded the test tree.

## Critical (0)

## High (0)

## Medium (3)

- **[dim 2]** `Bake.cpp:1616–1624`, `Name.cpp:69–75`. The code is `const std::string fitted = detail::fittedName(outcome.name);` … `if (request.fitBudget && !request.force && isCachedBake(fittedPath))` returns the result as `Cached`.
  - A fitted bake's bytes depend on `request.budgetBytes`, because `fitToBudget` shrinks the textures down to that budget. But `fittedName` hashes only the full bake's name.
  - So a fitted bake written under a 1 GiB budget is later served as `Cached` to a request with a 512 MiB budget. That bundle is over the smaller budget. This breaks the content-addressing rule `Name.h` quotes ("every other bake input must be covered … or the name is a lie") and UTA-0052's rule that nothing over budget is used.
  - The code is wrong.
  - Fix: fold `budgetBytes` into `fittedName`, or re-measure a cached fitted bundle's header or report against the request's budget before accepting it.

- **[dim 7]** `Name.cpp:29` — `if (const auto names = upkg::importedPackages(package); names.has_value())`.
  - When `importedPackages` fails, that package's error is dropped and its imports leave the closure without anyone being told.
  - If this happens on the map itself, the bake name covers only the map bytes, the recipe and the baker version. Changing or repairing a dependency then does not rename the bake, so a stale cached bundle is served (INV-3). The bake would still open those packages, which goes against INV-2.
  - Fix: return `Result<std::set>` and propagate the error from `closure` and `bakeName`, or refuse the name.
  - I did not check whether `importedPackages` can fail on a package that `Package::open` accepted. If it cannot, this drops to LOW.

- **[dim 2]** `Geometry.cpp:178–189` — `covered += overlapArea(flat, flatten(portal.points, portal.axis));`.
  - UTA-0109 §4.3 step 3a says portals must "cover at least 99% of its area". The code adds up each portal's overlap separately. Where two portal polygons in the chain overlap each other (for example, coplanar front and back nodes of one sheet), a node only half covered reaches 100%, and it is hidden when it should be drawn.
  - The code is wrong if overlapping portal nodes occur. I could not confirm that real maps contain them.
  - Fix: clip against the union of the portals (subtract the covered pieces one after another), or skip a portal whose polygon duplicates one already counted.

## Low / Info

- **[dim 7]** `TextureCache.cpp:230` — `Entry entry{it->path(), it->last_write_time(error), it->file_size(error)};`.
  - If `last_write_time` fails, the later successful `file_size(error)` clears the error. The entry is then kept with a wrong time (`file_time_type::min()` or unspecified) and becomes the first one trimmed.
  - Separately, at lines 227–228 the loop condition `!error` stops the whole walk at the first subdirectory that cannot be read. `total` then counts too little, and the cache can stay above its 4 GB cap.
  - Fix: use a separate `error_code` for each call, and use `directory_options::skip_permission_denied`.

- **[dim 4]** `Movers.cpp:142–148` against `226–231`.
  - The UTA-0217 finiteness guard was applied to `MainScale` and `PrePivot` but not to `Location` or `PostScale` of the same untrusted actor. `Bundle.h` says MOVR's floats are not checked.
  - So a NaN `Location` reaches `ubundle::zoneAt(rooms.map, shape.location, …)` (`Bake.cpp:1285`) and the renderer. I did not open `zoneAt`, so whether NaN is safe there is unknown.
  - Fix: treat a non-finite part as 0 for the location and 1 for the scale, as `Zones.cpp` `vectorOf` does.

- **[dim 11]** `Bake.cpp:319–332` — `std::array<long, 3> sum{}`.
  - `long` is 32 bits on Windows/MSVC. A palette with more than about 8.4 million entries would overflow the sum, which is undefined behaviour in signed arithmetic.
  - I did not check whether `readPalette` limits the entry count. If it does, this is not reachable.
  - Fix: use `std::int64_t`.

- **INFO:** I did not check whether `umat::compress`, `fs::writeFileAtomically` or `upkg::importedPackages` are safe when called from several threads at once, or how they fail. They are outside this lane.

## Covered by spec and looks correct

I read each of these and checked it against the spec:
- Install's search order, shadowing, bytewise tie-break and its absent-means-null resolver (UTA-0011 §4.2).
- The bake-name byte string, items 1–5, and leaving the map out of it (§4.4).
- The step order in `detail::bake` (§4.5, as extended).
- Variant selection and ascending id order, the masked rule (UTA-0314), the 8192 cap on textures with no picture, and every skip-not-refuse path (§4.6).
- The cache and never-degrade write path (§4.7 steps 1–4), apart from the fitted case above.
- Geometry steps 1–8, batch keys and pan rate (UTA-0109 §4.3).
- Mover selection, Scale decoding, the transform, mirror winding and zones (UTA-0119 §4.3–4.6, UTA-0156 §4.3).
- `buildZones` and `levelBrightnessOf`, including the defaults (`Zone::panSpeed{1,1}` confirmed).
- The sky refusal and its reporting in `recipeUnused` (UTA-0350 §4.2).
- The sun going into the base layer only (UTA-0338 §4.4).
- Concurrency in `bakeMaterials`: the resolver is locked, results are taken in id order, and `sites.reserve` keeps the pointers stable.

## Open questions

1. **The known trap**, "lookups by name as spelled, never lower-cased". `resolvedRecord` (Actors.cpp:269) compares `fold(record.name) == name`, and every caller here (`"bstatic"`, `"ambientbrightness"`, `"mainscale"`, …) passes lower case. That makes every lookup case-insensitive, which matches UT99's case-insensitive names. If the trap means records must be matched exactly as spelled, every lookup in Movers.cpp and Zones.cpp breaks it. I could not tell which reading is meant.
2. **Flags set after ROOM is built.** UTA-0011 §4.6 and UTA-0350 §4.2 say PF_MASKED and PF_FakeBackdrop are set "before any step reads the flags". But `buildRoomMap` (step 3) and `buildNavGraph` (step 4) run on `model` before `maskByTexture` and `skyByTexture` (step 7). If either reads `polyFlags`, the spec's statement is false. I did not open them.
3. **Cached bakes and budget.** UTA-0011 §4.7 step 2 serves an unfitted cached bake whatever the request's `budgetBytes` is. Is it intended that a bundle written under a larger budget comes back as `Cached`, not `OverBudget`, for a smaller one? If so, the spec is consistent with itself, but the never-degrade promise is weaker than UTA-0052 suggests.

## 3 items to fix first

1. **The fitted-bake name leaves out the budget** (`Bake.cpp:1616`). It is the only finding where a bundle over the caller's budget is served without any warning, so it breaks the never-degrade promise directly.
2. **Swallowed `importedPackages` failure** (`Name.cpp:29`). A name that does not cover what the bake reads means stale cached bundles, and the damage is silent and lasting.
3. **Summed portal overlap** (`Geometry.cpp:186`). If overlapping portal nodes exist, the result is wrong geometry (missing floors or walls) in the shipped bundle, and that is visible to players.

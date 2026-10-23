# close-findings: lane 04, baker bake and world content

Closes every finding in
[`lane-04-ubake-bake-content.md`](lane-04-ubake-bake-content.md) under
`UTA-0415`, and its correctness leads under `UTA-0403`. Run 2026-10-23.
Roadmap lookup: `roadmap_query ids:[UTA-0403, UTA-0415]`, both found, both
`planned`; no other bullet names these findings.

The readability fixes change no baked byte. Proof: `ut-bake` built at
`df9870f` and after the batch baked DM-ArcaneTemple, AS-Frigate, DM-Fetid
and CTF-Face; each bundle and each material light file is byte-identical.
`UTA-0403`'s light guard changes a bake only where a light's Location is not
finite, which no map baked here has; the golden bake test is unchanged, so
the baker revision stays 67, as `UTA-0367`'s mover guard did.

| # | finding | dim | sev | verified | disposition | files | was → now | must_agree |
|---|---|---|---|---|---|---|---|---|
| 1 | flame comment sits on `fireLookOf` | Q4 | MEDIUM | yes | fixed | `Bake.cpp` | comment moved onto `flameLookOf` | `Bundle.h` `FlameLook::ramp`: agrees |
| 2 | `BAKER_REVISION`'s module list incomplete | Q4 | MEDIUM | yes: links `uworld`, `urecipe`, `ufire` too | fixed | `Name.h`, UTA-0011 | names the rule: any library `src/ubake/CMakeLists.txt` links | UTA-0011 § 4.4's copy of the comment: amended |
| 3 | two `exportNamed`, different matches | Q3 | MEDIUM | yes | fixed | `Lamps.h`, `Lamps.cpp`, `Hide.cpp` | Lamps' → `exportIndexNamed`, says it matches name alone | no doc names it |
| 4 | local `classOf` hides `classOf()` | Q3 | MEDIUM | yes | fixed | `Bake.cpp` | local → `instanceClass`; comment says why `classOf()` does not fit | n/a |
| 5 | three `nameOf`; Name.h's hashes | Q3 | MEDIUM | yes | fixed | `Name.h`, `Name.cpp`, `BakeTest.cpp`, UTA-0011 | `detail::nameOf(NameInputs)` → `detail::bakeNameOf`; `textureScale`'s workaround dropped | UTA-0011 § 4.4 and INV-3, INV-4: amended. `ROADMAP.md`'s dated note: frozen |
| 6 | `makeVariant` loses the thread choosing the picture | Q2 | MEDIUM | yes | fixed | `Bake.cpp` | step 3's choice → `choosePicture` into a non-copyable `ShownPicture`; `fireClass` computed once and used three times | UTA-0105, UTA-0112, UTA-0292 name `makeVariant`: agree (it still calls the step); `tilekind.py` anchor line: untouched |
| 7 | clearance hides in the strips step | Q2 | MEDIUM | yes | fixed | `Bake.cpp` | loop → `setCameraClearance`, with its own comment; the shared `SurfaceRays` and phase said | UTA-0294: agrees |
| 8 | ancestry test written five times | Q1 | MEDIUM | yes | fixed | `Bundle.h`, `Sky.cpp`, `Zones.cpp`, `Movers.cpp`, `Lamps.cpp`, `LightProbes.cpp` | → `ubundle::isOrDescendsFrom`, which leaves `resolved` to its caller; Movers and Lamps keep theirs | the unbounded `classIndex`: lead L2 |
| 9 | `resolvedRecord` by kind wrapped six ways | Q1 | MEDIUM | yes | fixed | `Actors.h`, `Actors.cpp`, `Zones.cpp`, `Movers.cpp`, `Lamps.cpp`, `LightProbes.cpp` | → a `ValueKind` overload, saying a non-finite value is the caller's to replace; `lookUp` and Zones' `recordOf` gone | specs name `resolvedRecord`: name kept, agrees |
| 10 | `glassOf` and `detailOf` share 20 lines | Q1 | LOW | yes | fixed | `Bake.cpp` | → `greyOfNamed`; each keeps its own tail | UTA-0014 names `detailOf`: kept, agrees. `Liquids.cpp`'s luma orders a palette and stays |
| 11 | `FireStill.cpp` points at the wrong file | Q4 | LOW | yes | fixed | `FireStill.cpp` | points at `ufire/Fire.h`, which holds the provenance | n/a |
| 12 | `cubesOf` blames the added-lamp layer | Q4 | LOW | yes | fixed | `Bake.cpp` | takes the layer's name; one message | no test quotes it |
| 13 | `homeOf`'s bare 64 | Q5 | LOW | yes | fixed | `Bake.cpp` | bounded by the import table, as `walkImport` is; empty cases listed. No red run: only a crafted chain deeper than 64 behaves differently | n/a |
| 14 | sorted listing written three times | Q1 | LOW | yes | fixed | `Install.h`, `Install.cpp`, `GameTypes.cpp` | `detail::listSorted` shared; GameTypes reads a failure as empty | n/a |
| 15 | `within` defined five times | Q1 | LOW | yes | fixed | `Geometry.h`, `Flames.cpp`, `Hide.cpp`, `Collision.cpp`, `Geometry.cpp`, `TileKind.cpp` | one `within` in `Geometry.h` | corner walkers differ: out of scope |
| 16 | `unresolved` holds more than it says | Q4 | LOW | yes | fixed | `GameTypes.h` | lists every case `mapPrefixOf` returns none | n/a |
| 17 | "one a hidden brush" | Q4 | LOW | yes | fixed | `Hide.h` | "one per" | n/a |
| L1 | a light's Location is not checked finite | lead | — | yes: LITE does not refuse one (`LightSection.cpp`), Movers and Zones replace one | real, fixed under `UTA-0403` | `Actors.cpp`, `BakeActorsTest.cpp` | part not finite → 0. Red: `{ nanf, 2.0f, inff }`. Mutant `isnan` for `!isfinite`: killed | UTA-0110 says nothing on finiteness: agrees |
| L2 | `classIndex` unbounded in Zones and Movers | lead | — | yes, unreachable: every caller (`Bake.cpp`, `ut-dump`, `ut-paths` `Seeds.cpp`, tests) passes `buildActors`' output, which sets each index from its own class table; `PlacementSection.cpp` refuses a bad one on read | not a bug; precondition stated in `Zones.h` and `Movers.h` | — | — | n/a |
| L3 | only little-endian UTF-16 recognised | lead | — | yes: of the reference install's `.int` files none starts with FE FF or FF FE (census 2026-10-23) | not a bug: no content uses it; `GameTypes.h` now promises little-endian only | — | — | n/a |

## Run-level

- cited_by: `trace` on `exportNamed` and `detail::nameOf`; tree search for
  every renamed or removed name and for the old module list: hits only in
  `docs/reviews/`, UTA-0011 (amended), UTA-0014's `detailOf` (kept) and a
  dated `ROADMAP.md` note (frozen).
- swept: every `must_agree` cell above holds its verdict. Code-pair list:
  absent in this project. Asked of each fix: contradicts, breaks, stales.
- collateral: none.
- surfaced: none.
- out_of_scope: the ancestry test is also written in `tools/ut-dump/Cli.cpp`,
  `apps/ut-ants/main.cpp` and `tools/ut-paths/Seeds.cpp` (lanes 10 to 12),
  which may now call `ubundle::isOrDescendsFrom`. `Flames.cpp`'s
  `cornersBySurface` refuses a point past `MAX_COORDINATE` and `Hide.cpp`'s
  `cornersOf` does not; whether Hide needs it was not checked.
- falsified: none.

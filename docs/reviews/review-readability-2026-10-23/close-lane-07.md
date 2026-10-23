# close-findings: lane 07, upkg and umap

Closes every finding in [`lane-07-upkg-umap.md`](lane-07-upkg-umap.md)
under `UTA-0418`. Run 2026-10-23. Roadmap lookup: `UTA-0418` is the item;
the lane's correctness leads are `UTA-0405`, closed separately.

The fixes change no baked byte. Proof: `ut-bake` built at `03b2aea` and
after the batch baked DM-ArcaneTemple, AS-Frigate, DM-Fetid and CTF-Face
(revision 68, format 32); each bundle is byte-identical.

| # | finding | dim | sev | verified | disposition | files | was → now | must_agree |
|---|---|---|---|---|---|---|---|---|
| 1 | `Class.cpp` `fold` uses locale-dependent `std::tolower` | Q1 | HIGH | yes; premise check found five more `std::tolower` sites, and the sweep two more in tests | fixed | `core/Ascii.h` and every fold in `src`, `tools`, `apps`, `tests` | every local fold → `asciiLower`, `asciiFolded`, `equalsIgnoringAsciiCase` in core; ubake's `detail::fold` calls it | UTA-0011 names `detail::fold`: kept, agrees |
| 2 | INV-4 count guard hand-copied at nine sites | Q1 | MEDIUM | yes | fixed | `ReadSupport.h/.cpp`, `Geometry.cpp`, `Level.cpp`, `Texture.cpp` | nine copies → `detail::checkCount`; messages unchanged | `PackageMalformedTest` exact messages: agree |
| 3 | `OFFSET_FIELD_MIN_VERSION` claims Sound's field, Sound has its own | Q4 | MEDIUM | yes | fixed | `ReadSupport.h`, `Texture.cpp`, `Sound.cpp` | two constants → one in `ReadSupport.h` | n/a |
| 4 | Texture says UTA-0005 is not built | Q4 | MEDIUM | yes; `Texture.h` makes no such claim | fixed | `Texture.cpp` | → why name matching stays: INV-8 refuses an unmodelled subclass | `Texture.h` INV-8 line: agrees |
| 5 | reach-spec minimum says three int32s | Q4 | MEDIUM | yes | fixed | `Level.cpp` | → four int32s | `REACH_SPEC_MIN_BYTES`: agrees |
| 6 | `traceLoops` does not exist | Q4 | LOW | yes | fixed | `umap/Build.cpp` | → `traceComponent` | n/a |
| 7 | `Lattice` doc says one cell | Q4 | LOW | yes | fixed | `umap/Build.cpp` | → the sampling grid | n/a |
| 8 | "as they did" and review history in comments | Q4 | LOW | yes | fixed | `umap/Build.cpp` | → the rule in force; history left to git | n/a |
| 9 | "this file's header comment" | Q4 | LOW | yes | fixed | `Properties.cpp` | → `Properties.h`'s | n/a |
| 10 | "Step 4" names no document | Q4 | LOW | yes | fixed | `Properties.cpp` | → UTA-0003 SS 4.8 step 4 | UTA-0003 SS 4.8 item 4 is Size: agrees |
| 11 | ZoneMask comment says "as readPolys does" | Q4 | LOW | yes | fixed | `Geometry.cpp` | → the reverse of readPolys's PanU | n/a |
| 12 | name-index check copied eight times | Q1 | LOW | yes | fixed | `ReadSupport.h/.cpp`, `Class.cpp`, `Package.cpp`, `Properties.cpp`, `Geometry.cpp`, `Script.cpp`, `Sound.cpp` | two `checkNameIndex` and six inline copies → `detail::checkNameIndex`; every message now ends "outside the name table's N entries" | no test pins these messages |
| 13 | `inModel` hand-written four times | Q1 | LOW | yes | fixed | `Geometry.cpp` | four `withContext` copies → `inModel` | `PackageContentTest` "in a Model's" cases: agree |
| 14 | "bytes unread" refusal and `malformed` copied | Q1 | LOW | yes | fixed | `ReadSupport.h/.cpp` and the eight upkg readers | → `detail::expectConsumed` and `detail::malformed`; Class's "ended at P of S bytes" now reads as the others | no test pins Class's old message |
| 15 | "would blames" | spelling | — | yes | fixed | `Geometry.cpp` | → "would blame" | n/a |

## Run-level

- cited_by: tree search for `checkCount`, `checkNameIndex`,
  `reserveVertices`, `OFFSET_FIELD_MIN_VERSION`, `traceLoops`, the old
  messages and the removed fold names: two test comments naming
  `checkCount` (still true of `detail::checkCount`), ROADMAP's UTA-0216
  and RealSupport notes (frozen), `docs/reviews/` (frozen).
- swept: every `must_agree` cell above holds its verdict. Code-pair list:
  absent in this project. Asked of each fix: contradicts, breaks, stales.
- collateral: none.
- surfaced: none.
- out_of_scope: `src/ubake` keeps its own two `malformed` copies (another
  library, outside this lane). `apps/ut-ants/MapList.cpp` calls the
  locale-dependent `std::isspace`, which is not a fold. core
  `FileSystem.cpp`'s `upperAscii` and `tools/ut-paths/Cli.cpp`'s slug
  builder are left: one upper-cases, the other drops bytes too.
- falsified: none.

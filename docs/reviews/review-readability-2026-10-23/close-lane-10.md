# close-findings: lane 10, unav and ut-paths

Closes every finding in
[`lane-10-unav-ut-paths.md`](lane-10-unav-ut-paths.md) under `UTA-0421`,
and lane 04's carried-in note (`close-lane-04.md` out_of_scope): ut-paths
wrote its own class-ancestry test. Run 2026-10-23. Roadmap lookup:
`UTA-0421` exists, planned; no correctness item, as the lane filed no lead.

The fixes change no output. Proof: `ut-paths` built at `48991de` and
after the fixes ran over every work row of UT_MonsterHunt's offline split
census; both exited 0, and their output directories, standard output and
standard error are identical. `ut-bake` built before and after the
`Build.cpp` edit baked DM-ArcaneTemple, AS-Frigate, DM-Fetid and CTF-Face
from their map files; each bake exited 0 and wrote one bundle, and each
pair is byte-identical, bundle names included.

| # | finding | dim | sev | verified | disposition | files | was → now | must_agree |
|---|---|---|---|---|---|---|---|---|
| C1 | `Seeds.cpp` `descendsFrom` repeats `ubundle::isOrDescendsFrom` | lane 04 | — | yes; both test the class path, then each ancestry entry, by equality | fixed | `Seeds.cpp`, `scripts/mutations/ut-paths.py` | → the helper, by a using-declaration; its SS 4.6 reason moved to the call site; INV-10's mutation anchor follows | `Bundle.h` doc: agrees; `tests/real/RealPathSeedsTest.cpp` keeps its own copy: out_of_scope |
| 1 | `eventsOf` and `buildWiringGraph` answer one question two ways, neither naming the other | Q1 | MEDIUM | yes, all three differences | fixed (comment, as the remedy proposes; merging is a UTA-0172 non-goal) | `Seeds.cpp`, `Build.cpp` | → each names the other and the inputs on which they differ | UTA-0121 SS 3 decision 10 and UTA-0006 SS 4.3: agree |
| 2 | bare `32` for `unav::R_SPECIAL` | Q5 | MEDIUM | yes | fixed | `Seeds.cpp`, `scripts/mutations/ut-paths.py` | `& 32` → `& unav::R_SPECIAL`, with a comment naming it a teleporter's edge; INV-16's anchor follows | INV-16 text names `R_SPECIAL`: agrees |
| 3 | `nav.nodes.size()` as an unnamed "dropped" marker | Q5 | LOW | yes | fixed | `Seeds.cpp` | `position[x] < nav.nodes.size()` → `!= dropped`; equivalent, since a kept node's position is below its own index | INV-11's anchor `walkable(edge) && edge.pruned == 0 &&`: unchanged |
| 4 | `propose`'s local `nearSlope` shadows `Hops::nearSlope`; the slope detour's fallback is hidden | Q2 | LOW | yes | fixed; the lift dismissed, reason beside | `Seeds.cpp`, `scripts/mutations/ut-paths.py` | → `spotNearSlope`; the comment above it says a found path is searched again and stands where that finds nothing; INV-5's anchor follows. Lifting the lambdas to file scope not done: they share the lazy cache and six locals, so each would take a long parameter list and read no plainer | n/a |
| 5 | `buildWiringGraph` keeps the first `Tag` with no reason | Q5 | LOW | yes | fixed | `Build.cpp` | → `Actor.Tag` is one name, so a second record means a damaged list, and the first is kept | n/a |
| 6 | `runCli` doc omits two exit-1 causes | Q4 | LOW | yes; Cli.cpp returns `EXIT_FAILED` for both | fixed | `Cli.h`, `UTA-0121-bot-path-seeds.md` | → names the census and the summary | UTA-0121 SS 4.2 exit status omitted the summary case too: fixed, the spec amended to match the code (gate cancelled here: no gate) |
| 7 | usage text omits `-h` | Q1 | LOW | yes | fixed | `Cli.cpp` | `ut-paths --help` → `ut-paths --help \| -h` | no test quotes the usage text (search of `tests/`) |
| 8 | comments repeat `TOO_NEAR` and `PLACE_ACROSS` values | Q4 | LOW | yes | fixed | `Seeds.cpp`, `Walkable.h` | "within 50" → "within TOO_NEAR" twice; "within 64" → "within PLACE_ACROSS (Walkable.cpp)" | n/a |
| 9 | `letters` keeps digits too | Q3 | LOW | yes | fixed | `Cli.cpp` | → `alphanumericFolded`; its local `wantedLetters` → `wantedAlphanumeric` | callers: the two in `lookElsewhere` only |

## Run-level

- cited_by: tree search for `descendsFrom(`, `nearSlope`, `reachFlags & 32`,
  `letters(` and `ut-paths --`: the mutation catalog's three anchors,
  amended; `tests/real/RealPathSeedsTest.cpp`'s own `descendsFrom`. No spec
  names a changed symbol.
- swept: every `must_agree` cell above holds its verdict. Code-pair list:
  absent in this project. Asked of each fix: contradicts, breaks, stales.
  `scripts/mutation-probe.py ut-paths` after the fixes: 12 of 12 killed,
  the three moved anchors included.
- collateral: none.
- surfaced: none.
- out_of_scope: `tests/real/RealPathSeedsTest.cpp` writes its own
  `descendsFrom`, the same test again; a real-asset test, outside this
  lane's files. `HEIGHTS` is defined identically in `Seeds.cpp` and
  `Walkable.cpp`, which the lane noted and did not file.
- falsified: none.

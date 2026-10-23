# close-findings: lane 13, the Python and UnrealScript helpers

Closes every finding in
[`lane-13-scripts-python.md`](lane-13-scripts-python.md) under `UTA-0424`,
and its correctness leads under `UTA-0410`. Run 2026-10-23. Roadmap lookup:
`UTA-0424` and `UTA-0410` exist, both planned before this run.

The project has no Python test harness. Each red and green run below is a
scratch test or a command, outside the tree, and none is committed.

| # | finding | dim | sev | verified | disposition | files | was → now | must_agree |
|---|---|---|---|---|---|---|---|---|
| L1 | ut-compare splits ut-bake's JSON for the bundle path | lead | — | yes: a real DM-Fetid bake into a folder named `odd"dir\x` came back as `.../odd\` | fixed, `4290a85` | `ut-compare.py` | → `json.loads(...)["path"]`; a refused bake still fails with ut-bake's output. Red then green on a scratch test replaying the real report | `docs/build-and-test.md` ut-compare: agrees |
| L2 | tile-questions compares the report's hash without lowercasing | lead | — | no: ut-bake writes it with `detail::hex` (`src/ubake/Name.cpp`, digits `0123456789abcdef`), and `Bake.h` documents "64 lower-case hex digits"; the real DM-Fetid report's two hashes are lower case | dismissed | — | no edit | n/a |
| L3 | tile-questions accepts a relative HOME; C++ refuses it | lead | — | yes, and wider: HOME unset fell back to the password database. With `HOME=rel` the script read `rel/.local/share/...` and skipped a question the viewer would ask | fixed, `a38d8b8` | `tile-questions.py` | → `answers_path()` mirrors `envPath`, `None` where C++ names nothing. Red: 2 failed; three mutations killed | UTA-0277 SS 4.5: agrees |
| 1 | `skip_expression`'s opcodes are bare hex | Q4 | MEDIUM | yes | fixed, `1cee267` | `class-census.py` | → each set and branch names its token; one line names `src/upkg/Script.cpp`'s EX_ enum. Names checked against the enum by script | n/a |
| 2 | `read_class` reads with no field names | Q4 | LOW | yes | fixed, `1cee267` | `class-census.py` | → each read labelled with its UTA-0005 SS 4.3 step | UTA-0005 SS 4.3, UTA-0003 SS 4.8: agree |
| 3 | a chain past 64 counted as a "cycle" | Q1 | LOW | yes | fixed, `1cee267` | `class-census.py`, CHANGELOG | → "too deep". Census on the reference install byte-identical: no chain there passes 64 | UTA-0005 INV-6 and SS 7: agree |
| 4 | comment points at globs that are gone | Q4 | LOW | yes | fixed, `1cee267` | `class-census.py` | → says the earlier extension wins a basename clash | n/a |
| 5 | `maps_with` a second hand-kept key list, `lights` left out unsaid | Q5 | MEDIUM | yes | fixed, `fb99128` | `map-analyser.py` | → keys from `needs()` minus `NOT_IN_MAPS_WITH`; no reason was recorded for `lights` (b017f66), and the comment says so. DM-Fetid output byte-identical | docstring `mapsWith` line: fixed |
| 6 | docstring opens with version history | Q4 | LOW | yes | fixed, `fb99128` | `map-analyser.py` | → what the script reads, no versions | n/a |
| 7 | survivor reconciliation split in `main` | Q2 | LOW | yes | fixed, `4ba6c65` | `mutation-probe.py` | → `reconcile()`; equal to the old block on 20000 random cases | n/a |
| 8 | help cites CLAUDE.md for testing.md's lesson | Q4 | LOW | yes | fixed, `4ba6c65` | `mutation-probe.py` | → both mentions name `.claude/rules/testing.md` | `.claude/rules/testing.md`: agrees |
| 9 | HOW TO READ omits `NOT-UNIQUE` | Q4 | LOW | yes | fixed, `4ba6c65` | `mutation-probe.py` | → entry added | n/a |
| 10 | survivors heading and SS 4.4 banner misstate entries | Q4 | LOW | yes | fixed, `4ba6c65` | `mutations/ubundle.py` | → heading admits the UB entry; banner names INV-12 | `--coverage` byte-identical for all three subjects |
| 11 | 64-camera limit in three places | Q1 | LOW | yes | fixed, `342590b` | `ut-compare.py`, `UTACam.uc` | → `ArrayCount(Cameras)`; the array and the Python check name each other. ucc: 0 warnings | n/a |
| 12 | capture deadline hides UTACam's timings | Q4 | LOW | yes | fixed, `342590b` | `ut-compare.py` | → comment names StartSeconds, HoldSeconds, ShotSeconds | UTACam `defaultproperties`: agrees |
| 13 | future-mtime bound unexplained | Q5 | LOW | yes | fixed, `342590b` | `ut-compare.py` | → docstring says a future stamp would fail every run | n/a |
| 14 | usage is the docstring's third line | Q5 | LOW | yes | fixed, `46d8b37` | `tile-questions.py` | → `USAGE` literal; output identical | n/a |
| 15 | `Posed` named like the pose, not the pawn | Q3 | LOW | yes | fixed, `342590b` | `UTACam.uc` | → `HeldPawn`; `bPosed` comment says it is what Tick and Timer test | old name: no reader outside the file |

## Run-level

- cited_by: tree search, review records left out, for `ancestry: cycle`,
  `"cycle"`, `Posed`, `NOT-UNIQUE`, `ut-compare`, `UTACam`: hits in
  `docs/build-and-test.md`, `docs/fitting-a-map.md`, `CLAUDE.md`, and three
  specs naming `ut-compare` as a tool. None states what changed.
- swept: every `must_agree` cell above holds its verdict. Code-pair list:
  absent in this project. Asked of each fix: contradicts, breaks, stales.
  Identity runs: class-census over the reference install, map-analyser
  `--no-sky` and tile-questions on the real DM-Fetid report, mutation-probe
  `--coverage` for every subject, and each changed usage line: identical
  to HEAD's but for the intended changes.
- collateral: none.
- surfaced: none.
- out_of_scope: map-analyser stopped the whole batch on a map ut-dump
  could not read, which ut-dump reports with exit 0 and `"ok": false`;
  fixed under `UTA-0410`, `2321cc6`. `src/ubake/TextureCache.cpp` keeps a
  private copy of `detail::hex`; queued as `UTA-0432`.
- falsified: none.
- not run: a full `mutation-probe.py` run, which builds and runs every
  mutant; a full `ut-compare.py` run, which starts the original client.

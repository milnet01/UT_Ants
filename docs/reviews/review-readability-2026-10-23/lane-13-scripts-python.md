## Files read
- `scripts/class-census.py` — 484 / 484
- `scripts/map-analyser.py` — 381 / 381
- `scripts/mutation-probe.py` — 260 / 260
- `scripts/mutations/tilekind.py` — 40 / 40
- `scripts/mutations/ubundle.py` — 271 / 271
- `scripts/mutations/ut-paths.py` — 60 / 60
- `scripts/package-census.py` — 85 / 85
- `scripts/tile-questions.py` — 153 / 153
- `tools/ut-compare/UTACam/Classes/UTACam.uc` — 163 / 163
- `tools/ut-compare/ut-compare.py` — 338 / 338

To confirm findings, I also read: the project `CLAUDE.md` § Build and test, search hits in `.claude/rules/testing.md`, `src/upkg/Class.cpp`, `src/core/FileSystem.cpp`, and `src/ubundle/TileAnswers.cpp`.

## What I started with
- The global `~/.claude/CLAUDE.md`, and the review-lane agent instructions.
- A SessionStart hook message saying the Ants MCP verbs are connected.
- The userEmail context block.
- No project memory and no git snapshot.
- The shared-context brief (STEP 1), which I read before the subject files.

## Summary
These are helper programs for the developers, not part of the game. Two of them count what is inside an Unreal Tournament install, to check numbers the design documents rely on. One studies each map's sky and lighting. One deliberately breaks rules in the source code to see whether any test notices. One makes pictures so the user can say whether a texture may be shuffled. The last one runs the original game and this project's renderer from the same camera, then puts the two pictures side by side. For that it uses a small add-on written in the game's own scripting language (UTACam.uc).

## Findings

[Q4] MEDIUM `scripts/class-census.py:168` — `if op >= 0x70:` — Every bytecode opcode in `skip_expression` is a bare hex number: 0x21, 0x05, 0x0A, 0x0C, 0x1B, the 0x39–0x5F range, 0x60–0x6F and ≥0x70. Nothing names the token, and nothing says which table the numbers come from. The case at line 195 also treats the value 0xFFFF specially with no reason given. A stranger who hits `unknown opcode 0x3A` or a WRONG LENGTH tally cannot tell which instruction is involved, or whether a branch's byte count is right. The module docstring cites UTA-0005 § 4.3 for the class layout, but not for the opcodes. — Fix: add a comment per branch naming the token, plus one line naming the table the opcode values come from.
Cost: one edit — comment only.
Partly confirms `scripts/class-census.py:164:skip_expression`. The cost is the unnamed opcodes, not the length.

[Q4] LOW `scripts/class-census.py:316` — `for _ in range(3):` — `read_class` is about 30 reads in a row with no field names. The `if node != 0` (line 313) and `if package.version >= 62` (line 336) branches give no reason. The spec numbers these steps (UTA-0005 line 669 refers to "step 10's *version 62 and above*"). A stranger changing one read would have to line the code up against the spec step by step. — Fix: label each read, or each group of reads, with its UTA-0005 § 4.3 step number.
Cost: one edit — comment only.

[Q1] LOW `scripts/class-census.py:427` — `return "cycle", MAX_DEPTH` — A chain that is merely longer than 64 is counted as a "cycle". The C++ walker it re-derives reports the same case as malformed, "deeper than 64 levels" (`src/upkg/Class.cpp:276-278`). `Class.cpp:16` says "scripts/class-census.py re-derives it". So a stranger reading `ancestry: cycle` in the census output would look for a loop that does not exist. — Fix: return a distinct "too deep" end, or comment why the two cases are merged.
Cost: one edit — Searched: docs/specs and docs/standards for `walk_ancestry|read_class|skip_expression|mapsWith|maps_with`. Nothing found.
Searched: `MAX_ANCESTRY_DEPTH` in src.

[Q4] LOW `scripts/class-census.py:368` — `# By extension in EXTENSIONS' order, as the globs did, ignoring case.` — This points at glob code that is no longer there. `EXTENSIONS` is still stored as glob patterns (`"*.u"`) only so that line 361 can strip the `*` again. A stranger would go looking for the globbing, or would not realise that the order of `EXTENSIONS` decides which file wins under `setdefault`. — Fix: say directly that earlier extensions win a basename clash, and drop "as the globs did".
Cost: one edit — comment only.

[Q5] MEDIUM `scripts/map-analyser.py:349` — `maps_with = {k: 0 for k in ("skyZones", "skyKinds", "backdrop", ...` — This is a second hand-kept list of the keys `needs()` returns (lines 282-295). It leaves out `lights`, with no reason given. The docstring (line 66) says `mapsWith` counts "how many maps have each need". A new need added to `needs()` and the docstring, as versions 2 and 3 did, would be silently missing from the summary. — Fix: derive the keys from the `needs` row minus a named exclusion set, with the reason for leaving out `lights`.
Cost: one edit — Searched: docs/specs and docs/standards for `mapsWith|maps_with`. Nothing found.

[Q4] LOW `scripts/map-analyser.py:2` — `"""List what graphical features each map needs -- UTA-0347. Version 1 read` — The module summary opens with the script's version history. The output has no `version` field, so a stranger may look for one. — Fix: move the version history out of the docstring and keep the current description.
Cost: one edit — comment only.

[Q2] LOW `scripts/mutation-probe.py:229` — `# A declared survivor this run KILLED: the declaration is stale` — I could follow `main` until here. The two `--asan` adjustments to the survivor list are split: one filters `expected` at lines 224-225, the other builds `undefined` at line 235. The comment for `stale` is cut in two by `probed` and by an "Except…" comment. Someone changing how ASan-only rules are handled would likely edit one adjustment and miss the other. — Fix: pull the survivor reconciliation (lines 223-237) into one function, with both `--asan` rules side by side.
Cost: one edit — Searched: docs/specs and docs/standards (files_only, script names). No spec names `main` or these locals.
Reshapes `scripts/mutation-probe.py:162:main`.

[Q4] LOW `scripts/mutation-probe.py:18` — `reading the test. CLAUDE.md SS Build and test records it` — That section does not record this lesson, and neither does line 31's "per CLAUDE.md". The lesson lives in `.claude/rules/testing.md:17-25`. CLAUDE.md § Build and test only points onward to that file, so a stranger can recover with one hop. — Fix: point both lines at `.claude/rules/testing.md`.
Cost: one edit — comment only.
Searched: CLAUDE.md § Build and test (read) and `.claude/rules/testing.md` (grep).

[Q4] LOW `scripts/mutation-probe.py:34` — `NOT-APPLIED  the search text no longer matches.` — The HOW TO READ list in `--help` leaves out `NOT-UNIQUE`. The code returns it at line 140 and counts it as broken at line 227. — Fix: add a `NOT-UNIQUE` entry to the list.
Cost: one edit — comment only.

[Q4] LOW `scripts/mutations/ubundle.py:252` — `# Survivors that are REDUNDANCY rather than a gap. Each was chased to` — The third entry (`readBytes' own bound`, line 263) is declared as undefined behaviour, not redundancy, so the heading misstates the rule for adding one. The banner at line 94 ("INV-11") also sits above an INV-12 entry. In both cases the entries themselves set the reader right. — Fix: widen the heading to "redundancy, or UB only `--asan` can grade", and correct the banner.
Cost: one edit — comment only.

[Q1] LOW `tools/ut-compare/ut-compare.py:291` — `if not 1 <= len(cameras) <= 64:` — The 64-camera limit is written by hand in three places, with nothing linking them: here, `UTACam.uc:12` (`Cameras[64]`) and `UTACam.uc:120` (`Index >= 64`). Raising one without the others gives a confusing "the original gave 64 frames for 70 cameras" error. — Fix: use `ArrayCount(Cameras)` at UTACam.uc:120, and comment the Python check as UTACam's array size.
Cost: one edit — Searched: the files_only search of script and tool names. No spec names this limit.

[Q4] LOW `tools/ut-compare/ut-compare.py:224` — `deadline = time.time() + 90 + 10 * len(cameras)` — The per-camera allowance quietly depends on UTACam's `defaultproperties`: StartSeconds 6, then HoldSeconds 4 plus ShotSeconds 2 for each camera (`UTACam.uc:159-161`). Lengthening the hold in the `.uc` file can make a long run time out with nothing pointing at why. — Fix: add a comment naming UTACam's three timings as what 90 and 10 must cover.
Cost: one edit — comment only.

[Q5] LOW `tools/ut-compare/ut-compare.py:185` — `if p.is_file() and since < p.stat().st_mtime < time.time() + 60]` — Files with a modification time more than 60 seconds in the future are left out of the "live install changed" check, and nothing says why. — Fix: give the reason in one comment, or drop the upper bound.
Cost: one edit — comment only.

[Q5] LOW `scripts/tile-questions.py:109` — `sys.exit(__doc__.strip().splitlines()[2].strip())` — The usage message is whatever happens to be the docstring's third line. Adding a line to the docstring's opening silently changes what a wrong invocation prints. — Fix: a literal usage string, or a comment tying it to that line.
Cost: one edit — Searched: none needed; nothing outside the file reads it.

[Q3] LOW `tools/ut-compare/UTACam/Classes/UTACam.uc:21` — `var PlayerPawn Posed;` — It sits beside `bPosed` (line 18), and both are under the comment "The pose being held". But `Posed` is the pawn, while the pose is `PawnAt`, `Aim` and `Fov`. A stranger may clear `Posed` thinking it ends the hold, when `bPosed` is what `Tick` and `Timer` test. — Fix: rename it to something like `HeldPawn`.
Cost: one edit — Searched: the files_only search. UTACam appears in no spec besides its own name.

## Candidates dismissed
- `scripts/class-census.py:70:census_number` — "32 bits" is the format's integer width, not a count that can go stale.
- `scripts/map-analyser.py:92:census_number` — A dated (2026-10-22), attributed measurement that justifies `ROOM_DIRECTIONS` and `DOME_DIRECTIONS`. It agrees with both constants and with the docstring's 8 / 9–23 / 24 split.
- `scripts/map-analyser.py:102:census_number` — 3225 = 25 × (128 + 1), which is correct for the `WIDE_RADIUS` beside it. It goes stale only if that value, one token away, changes.
- `scripts/mutation-probe.py:162:main` — The length is not the problem. It reads as sequential phases. Narrowed to the Q2 finding at line 229.
- `scripts/class-census.py:164:skip_expression` — The length is not the problem. Each branch is a flat lookup. Replaced by the Q4 finding on unnamed opcodes.

## Correctness leads
- `tools/ut-compare/ut-compare.py:256-257` — `bake()` pulls the bundle path out of ut-bake's JSON report by splitting the text rather than parsing it. A path containing an escaped quote, a backslash or a `\u` escape would come back wrong. — Searched: none.
- `scripts/tile-questions.py:113` — `answered()` lowercases the stored hashes (line 61), but `q["hash"]` from the report is compared without lowercasing. If the report ever emits uppercase hex, every question would be re-asked. I did not check the report's hash casing. — Searched: none.
- `scripts/tile-questions.py:48` — Falls back to `Path.home()`, whereas the C++ `xdgDirectory` (`src/core/FileSystem.cpp:82-86, 220-226`) refuses a relative HOME. The two disagree only when HOME is relative. — Searched: `XDG_DATA_HOME`, `is_absolute` in src.

## Not checked
- I did not open the UTA-0005 § 4.3 step list, so the claim that `read_class` lines up with it step by step is inferred from the spec's "step 10" wording. I also did not check whether the spec holds the opcode table.
- Duplication between the two Python bundle section-table readers (`map-analyser.py:220` `records`, `tile-questions.py:65` `base_levels`) and `src/ubundle/Bundle.cpp`. There are two Python copies, and no comment records a fix that had to patch both, so I did not grade it. Their handling of a missing section differs by design (one returns an empty list, the other exits).
- My spec searches for fix costs covered docs/specs and docs/standards only, not `docs/design.md`.

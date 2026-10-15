**Line counts (each file read in full with native `Read`):**
- `tools/ut-bake/Cli.cpp`: 443
- `tools/ut-dump/Cli.cpp`: 1572
- `tools/ut-dump/Cli.h`: 39
- `tools/ut-paths/Cli.cpp`: 365
- `tools/ut-paths/Seeds.cpp`: 727
- `tools/ut-paths/Seeds.h`: 72

`Read` also showed one empty line after each file's last line (the `ut-dump/Cli.cpp` header said "1573 total"). I counted to the last line with content.

**Already in my context when I arrived:** the global `~/.claude/CLAUDE.md` (it mentions review skills and the Ants tools), the user's email address, and the review-lane agent file. No project `CLAUDE.md`, no memory index and no git snapshot. I also read `.claude/rules/routing.md` because it is a contract named in the tail; it carries routing history (UTA-0130, 0131, 0142, 0196).

**What I read beyond the subject:**
- UTA-0012 § 4 through § 6
- UTA-0011 § 4.8 and § 4.9
- `routing.md`
- `cpp.md`
- one slice of `src/upkg/Class.cpp`, to check whether `EffectiveProperty::origin` can be null
- one search showing `src/umat/Material.cpp` sorts `byTexture` largest first

No test files opened. No searches touched the test tree.

## Critical (0)

## High (0)

## Medium (4)

- **[dim 1] `ut-bake/Cli.cpp:295`** — `out << '[' << v[0] << ", " << v[1] << ", " << v[2] << ']';`, and `:306` `<< question.extent << ", \"clear\": " << question.clear`
  - **Problem:** the tile-question numbers are written with plain stream output, so a NaN or infinity prints as `nan` or `inf`. That breaks § 4.8's "Standard output is exactly one JSON object". The stream's default precision of 6 also loses digits on coordinates.
  - **The other tools already guard this.** `ut-dump`'s `writeLocation` (`:289–296`) and `ut-paths`' `number()` both turn non-finite values into `null` and write the shortest exact form.
  - **Not verified:** whether `at` or `normal` can be non-finite. They come from map data, which is untrusted.
  - **Fix:** use one shared finite-checking writer based on `std::format`.

- **[dim 1] `ut-paths/Cli.cpp:237–239`** — `const stdfs::path file = install / "Maps" / (row.map + ".unr"); … if (!stdfs::is_regular_file(file, ec)) {`
  - **Problem:** this looks for the map with exact case. On Linux, a census name like `DM-Foo` with `dm-foo.unr` on disk, or an install whose folder is `maps/`, is skipped.
  - **Mislabelled too:** `lookElsewhere` (`:221`) then lists the real file under `differentNames`, which the help text describes as "differs from it only in punctuation".
  - **Inconsistent with the same file:** `:338` matches a named map case-folded, "as the resolver matches a package".
  - **Fix:** find the folder and the file by folded name, the way the install resolver does.

- **[dim 11] `ut-dump/Cli.cpp:1292`, `1300`, `1359`, `1366–1369`** — `path.stem().string()`, and `:90–91` `entry->path().extension().string()` / `.stem().string()`
  - **Problem:** on MSVC, `path::string()` converts to the ANSI code page and throws on characters it cannot represent. Nothing in `runCli` catches that, so one oddly named map ends the whole library run. Where it does not throw, the map name is not UTF-8 and can fail to match during class resolution.
  - **The project says Windows is first-class.** The same file already uses `uta::fs::utf8(path)` for `file` (`:1195`), and `ut-paths` uses `ubake::detail::utf8`.
  - **Fix:** use `uta::fs::utf8(path.stem())` throughout.

- **[dim 4] Two copies of argument-to-path conversion that no longer agree:**
  - `ut-bake/Cli.cpp:314–316` uses `std::filesystem::path(*args.install)` for `--install`, the map and `--out`. Lines `:324` and `:389` use `uta::fs::pathFromUtf8` for `--recipe` and `--light-materials`. One function, two conventions.
  - `ut-dump/Cli.cpp:1470`, `1475` and `1493`, and `ut-paths/Cli.cpp:324` and `:327–328`, use the plain constructor.
  - **Effect on Windows:** if arguments arrive as UTF-8, non-ASCII paths go wrong for every argument except the two converted ones.
  - **Fix:** convert every path argument with `pathFromUtf8`. See the open question on how arguments are encoded.

## Low / Info

- **[dim 7] `ut-dump/Cli.cpp:1535–1538`** — `if (auto opened = uta::ubake::Install::open(installDir); opened.has_value()) {` and no `else`
  - **Problem:** if the install fails to open, the reason is thrown away. The run carries on with an empty resolver, and the user sees only "Engine.u not found", which is misleading.
  - **Fix:** print `opened.error().message()` to stderr.

- **[dim 1] `ut-bake/Cli.cpp:147–148`** — the `--check` / `--game-types` exclusivity test leaves out `fitBudget` and `fullBudget`.
  - **Problem:** `--check X --fit-budget` is accepted silently, even though the code's own error message says `--check` "takes an install and nothing else".
  - **Fix:** add both flags to the test.

- **[dim 9] `ut-bake/Cli.cpp:389–395`** — `std::ofstream file(uta::fs::pathFromUtf8(*args.lightMaterials), std::ios::binary);`
  - **Problem:** this writes in place, so a failure part-way leaves a truncated file. `ut-paths` uses `writeFileAtomically`.
  - **Also:** stdout has already said `"verdict": "written"` when this then exits 1. § 4.8's exit table maps 1 only to "refused or over budget".
  - **Fix:** write it atomically, and see the open question on the exit code.

- **[dim 7] `ut-bake/Cli.cpp:407`** — `umat::enforceBudget(outcome->result->budget)`
  - **Problem:** this uses `result` without checking it exists. Lines `:368` and `:371` check before every other use.
  - **Not verified:** I did not check whether `OverBudget` always carries a result.
  - **Fix:** guard it the same way.

- **[dim 2] `Seeds.cpp:420–422`** — `if (!std::isfinite(static_cast<float>(value)))`
  - **Problem:** the value is narrowed from `double` to `float` before its size is tested. `cpp.md` § Range guards calls that undefined behaviour when the value is out of range, and says to test `std::fabs(value) <= FLT_MAX` first.
  - **Low because:** in practice the inputs come from floats.
  - **Tool:** `bugprone-narrowing-conversions` is silent on an explicit cast like this.
  - **Fix:** check the magnitude, then cast.

- **[dim 3] `ut-paths/Cli.cpp:234–235`** — the plain-name guard rejects `/\:`, `.` and `..`, but not Windows device names (`CON`, `NUL`, `AUX`, …), trailing dots or spaces, or an embedded NUL byte.
  - **Problem:** the census is another project's file. On Windows, `outDir / "CON.json"` writes to a device.
  - **Fix:** extend the guard.

- **[dim 2, document side] UTA-0012 § 4.1** says "With no `Core` package found, the tool writes the warning". The code asks for `engine` (`:1543`, UTA-0265). **The document is wrong**, so this goes to `review-contract`.

- **[dim 2, document side] UTA-0011 § 4.8**'s synopsis leaves out `--light-materials`, which the code (`:55`, `:124`) and the usage text both carry (UTA-0292). **The document is out of date.**

- **INFO:** I checked one suspicion and dropped it: `kindOf` (`ut-dump:477–479`) does not test `effective.origin` for null where its siblings do. `effectiveDefaults` sets `origin` non-null on every path (`Class.cpp:306` and `:312`), so it is not a bug today.

- **INFO:** UTA-0121 governs `ut-paths`/`Seeds`, but it was not in my contract list, so I did not read it. I judged those files only against `routing.md`, `cpp.md` and the code's own comments.

## Covered by spec and looks correct

- **`ut-dump`** (against UTA-0012 § 4.1–§ 4.8 and INV-1 to INV-8): key order and keys for the failure and success cases, ascending path sort, ndjson header and `oneLine`, and `chainsUnresolved`. On that last one I traced `unresolvedChains` and `writeActorWiring`: both de-duplicate exports, skip null classes, and count the same failure ends, so the INV-8 equality holds. Also `exits`, `waypoints`, `playerStarts` and `lighting` refusals, `surfaces` range guards, and exit codes 0 and 2.
- **`ut-bake`:** the `--check`, `--game-types` and bake JSON shapes and which keys appear on which verdict; `byTexture` top-10 against `byTextureOmitted` (the list is sorted largest first, confirmed); and the exit codes.
- **`ut-paths`:** census parsing, the summary shape, exit codes, and refusal on a thrown exception. In `routing.md`, the teleporter rule (an "off" teleporter is cut only when nothing in the map fires its tag, not on `bEnabled` alone) matches `Seeds.cpp:521–542`.

## Open questions

1. How does each tool's `main` encode `argv` on Windows: UTF-8 (a manifest or `wmain`) or ANSI? This decides the severity of the dim-4 path finding. No `main` file matched my search.
2. UTA-0012 § 4.1's synopsis writes `(--install | --system)` as required. The code and the help text allow neither, with a warning. Which is meant?
3. If a bake is written and then the `--light-materials` write fails, should the exit be 1? § 4.8's table does not cover this case.
4. Does UTA-0121 say whether map lookup in `Maps/` is case-sensitive? I was not given it.

## 3 items to fix first

1. **`ut-bake` tile-question numbers (`:295`, `:306`).** One non-finite value from an untrusted map breaks the single-JSON-object contract the launcher (UTA-0016) parses. The fix is a shared writer two other tools already have.
2. **Case-sensitive map lookup in `ut-paths` (`Cli.cpp:237`).** On Linux it quietly skips real maps and mislabels them as different maps. That corrupts the census output it exists to produce.
3. **`path::string()` in `ut-dump` (`:1292` and the rest).** On a first-class target, one non-ASCII file name ends a whole-library run, which UTA-0224 says must never happen.

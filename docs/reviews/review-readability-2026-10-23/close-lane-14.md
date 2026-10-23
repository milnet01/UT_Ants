# close-findings: lane 14, the build and gate machinery

Closes every finding in
[`lane-14-build-gate.md`](lane-14-build-gate.md) under `UTA-0425`, and its
correctness leads under `UTA-0411`. Run 2026-10-23. Roadmap lookup:
`UTA-0425` and `UTA-0411` exist, both planned before this run.

Scripts have no test harness here. Each red and green run below is a
command or a scratch repository outside the tree, and none is committed.

| # | finding | dim | sev | verified | disposition | files | was → now | must_agree |
|---|---|---|---|---|---|---|---|---|
| L1 | `docs/*` makes `docs/x.utab` documentation-only | lead | — | yes: `printf 'docs/x.utab\n' \| scripts/docs-only.sh` exited 0 | fixed, `c992135` | `docs-only.sh`, `ci.sh`, CHANGELOG | → `*.utab` on the never-list; exit 1. Old and new classifier over every tracked path: 0 differ (no `.utab` is tracked) | machine hook `NEVER_DOCS` runs first, then this script: inherits the fix, no change there |
| L2 | `--help` stops mid-sentence in both scripts | lead | — | yes: `ci.sh --help` ended "is a green that"; `ci-matrix.sh --help` ended "which is what the" | fixed, `a465f4e` | `ci.sh`, `ci-matrix.sh`, CHANGELOG | → prints the comment block up to the first line of code; the old output is an exact prefix of the new | n/a |
| L3 | pre-commit tests a `/` link against the machine's root | lead | — | yes, and wider. In a scratch repository `sub/a.md` linking `/README.md` was blocked and `/usr` passed; `sub/b.md` linking `README.md` (GitHub: `sub/README.md`, absent) passed | queued: sent to the claude-config session, the hook's home | — | The hook is byte-identical to `~/.claude/skeleton/files/.githooks/pre-commit`. A proposed change, tried in the scratch repository, blocks `/usr` and `README.md` and passes `/README.md` | ci.sh's link step: names its looser twin (finding 15) |
| 1 | ci.sh comment: no docs-only push carries a `.utab` | Q4 | HIGH | yes, by L1 | fixed, `c992135` | `ci.sh` | → the comment names the never-list entry, and is now true | ci.sh `--docs` exit comment: agrees |
| 2 | `--help` prints a fixed line range | Q4 | MEDIUM | yes, by L2 | fixed, `a465f4e` | as L2 | as L2 | n/a |
| 3 | ccache key spelled out by hand in ci.yml | Q1 | MEDIUM | yes: six hand spellings | fixed, `df22786` | `ci.yml` | → "Decide the mode" emits `ccache_prefix`, `ccache_key`, `deps_prefix`, `deps_key`; every cache step reads them. The emitting block run by hand gives the old spellings | prune `PAIRS`: reads the same outputs |
| 4 | ccache settings in three places | Q1 | MEDIUM | yes | fixed, `8feb3e7` and `df22786` | `ci.yml`, `ci-matrix.sh` | → each of the three sites names the other two. One home was not made: the three run as a GitHub env file, Git Bash, and a heredoc over SSH | n/a |
| 5 | ci.yml: "the Windows leg has no compiler cache" | Q4 | MEDIUM | yes: "Install ccache" runs on Windows (UTA-0235) | fixed, `df22786` | `ci.yml` | → the timeout comment says a release tag builds cold; the Catch2 comment says MSVC is kept too | restore step `if` skips tags: agrees |
| 6 | CMakeLists.txt: mold "saves almost nothing TODAY" | Q4 | MEDIUM | yes: `src`, `tools`, `apps`, `tests` all build | fixed, `cbfc8c0` | `CMakeLists.txt` | → states what mold links today, and that ci.yml installs it (UTA-0241) | ci.yml mold comment: agrees |
| 7 | ut-bench and ut-paths sources listed in several targets | Q1 | MEDIUM | yes | queued, `UTA-0433` | — | The fix (a library per tool) touches what `UTA-0129` and `UTA-0121` specify, so it is not a comment edit | — |
| 8 | check-legs.sh parses ci-matrix.sh unannounced | Q5 | MEDIUM | yes: `check-legs.sh` greps `^run_leg `, `^ +ssh .*bash\.exe`, `./scripts/ci.sh` | fixed, `8feb3e7` | `ci-matrix.sh` | → comments at the `run_leg` lines and the `ssh` line name `check-legs.sh` and the shapes it needs. `check-legs.sh` still reads three legs | `check-legs.sh`: agrees |
| 9 | `UTA_CI_JOBS` sets build jobs only | Q3 | LOW | yes | fixed, `8feb3e7` | `ci-matrix.sh` | → header says BUILD jobs and points at `UTA_CI_TEST_JOBS`. Not renamed: a rename breaks anyone's set variable | n/a |
| 10 | empty `deps_args` means "refill" | Q5 | LOW | yes | fixed, `5031e44` | `ci.sh` | → `refill_deps`, set where the kept copy is found incomplete; true exactly when the old test was. Exercised by the GCC 14 run below with an empty `UTA_CI_DEPS_DIR` | n/a |
| 11 | ci.yml: "the same two CLAUDE.md sets" | Q4 | LOW | yes: `CLAUDE.md` sets no ccache value | fixed, `df22786` | `ci.yml` | → names `docs/build-and-test.md` and says its base is `/`, not the checkout. No reason for the difference is recorded, so none is claimed | `docs/build-and-test.md` ccache lines: agree |
| 12 | CMakeLists.txt: version checks "below" | Q4 | LOW | yes | fixed, `cbfc8c0` | `CMakeLists.txt` | → version checks above, `UTA_SANITIZE` below | n/a |
| 13 | tests/CMakeLists.txt: "the next four" | Q4 | LOW | yes: 13 files follow | fixed, `cbfc8c0` | `tests/CMakeLists.txt` | → no count; says what `RealInstallTest.cpp` keeps, from its own header | `RealInstallTest.cpp` header: agrees |
| 14 | commit-msg repeats its cap comment | Q4 | LOW | yes | dismissed here: the hook comes from `~/.claude/skeleton/files/.githooks/commit-msg`, which carries the same two comments; sent to the claude-config session | — | no edit | — |
| 15 | pre-commit's link check is a looser twin of ci.sh's | Q1 | LOW | yes, by L3 | fixed, `5031e44` | `ci.sh` | → the link step names the hook as a looser twin it does not own | n/a |
| 16 | std-includes.sh fails with a `ci:` prefix | Q3 | LOW | yes | fixed, `8feb3e7` | `std-includes.sh` | → `std-includes:`. Red: a scratch file using `std::sort` printed `ci: 1 ...`; now `std-includes: 1 ...` | no doc quotes the message |

## Run-level

- cited_by: tree search for `docs-only.sh`, `--help`, `UTA_CI_JOBS`,
  `standard function(s) used without`, `matrix.cxx || 'msvc'`: hits in
  `docs/build-and-test.md` (docs-only), `setup-hooks.sh`, `_typos.toml`
  and review records. None states what changed.
- swept: every `must_agree` cell above holds its verdict. Code-pair list:
  absent in this project. Asked of each fix: contradicts, breaks, stales.
  `check-legs.sh` after the `ci-matrix.sh` comments: same legs on both
  sides.
- collateral: none.
- surfaced: none.
- out_of_scope: ci.sh's link step read markdown inside code spans as
  links, so two lane files of this review (05, 06), each quoting a C++
  lambda `[&](const ...)`, failed the gate. Fixed under `UTA-0411`, `5031e44`:
  fenced blocks and code spans are dropped before extraction. Over every
  tracked markdown file the old extraction found 115 targets and the new
  109; the six dropped are all code quotes, and none was a link.
- falsified: none.
- not run: `ci-matrix.sh` (the push gate; the main session runs it), so
  Clang 19 and MSVC did not build this; the cache steps in `ci.yml`,
  which run only on GitHub.

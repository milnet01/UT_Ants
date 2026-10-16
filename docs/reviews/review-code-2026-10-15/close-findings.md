# close-findings — UTA-0374, 2026-10-16

Closes [`check-code.md`](check-code.md). Each site was read at the tree of
`d60da40`; where a line had drifted, the same code was read at its new line.

## Dispositions

| Finding | Verified | Disposition |
|---|---|---|
| `src/ubake/LightModel.cpp` identicalConditionAfterEarlyExit | yes: UTA-0259's early exit `if (d >= radius) return {};` makes the cylinder branch's copy unreachable | fixed: the dead check removed; no red run, nothing observable changes |
| cppcheck returnDanglingLifetime, six sites | no: each returns a pointer into `Package::exports()`, a span over the package's storage | dismissed: tool noise, `tools/audit/suppressions.md` |
| clang-analyzer null pointer, `src/ubake/Flames.cpp` | no: the point that set `low` passes the filter, so `left` and `right` are set | dismissed: suppressions.md |
| containerOutOfBounds, `tools/ut-bound-scan/Cli.cpp` | no: `first >= args.size()` returns before the loop | dismissed: suppressions.md |
| `src/ufire/Fire.cpp` widening cast, float loop, rounding | no: product already `size_t`; steps of 0.5 exact; value 0 to 255 | dismissed: suppressions.md |
| unchecked-optional-access, 15 sites | no: each guarded locally, by a size-0 fallback, or by its parser or caller | dismissed: suppressions.md, existing row widened |
| uninitMemberVarNoCtor, src and tests | no: full brace initialisers only, or a default already given | dismissed: suppressions.md |
| remaining cppcheck and clang-tidy one-offs | no: style, or deliberate in tests | dismissed: suppressions.md, one row per rule and file |
| `comparisonOfFuncReturningBoolError` class | no: Catch2's `CHECK` on a bool call | dismissed: suppressions.md |
| `bugprone-easily-swappable-parameters` class | no: signature shape only | dismissed: suppressions.md |
| ruff, bandit, semgrep | no: argument lists with no shell; Xvfb's fixed lock paths; zips of 3-vectors | dismissed: suppressions.md |
| typos | no: UT texture names, a shell array, a file name, the rendering term, a quotation of the user | dismissed: `_typos.toml` |
| `froms` | no: the plural of the local struct `From` | dismissed: `_typos.toml` |
| calibration: cppcheck `-D__has_cpp_attribute(x)=0` | yes | fixed: `tools/audit/audit-config.json` |
| calibration: FSR shaders under glslangValidator | yes | fixed: noted in suppressions.md |

## Run

- **out_of_scope, fixed:** `scripts/map-analyser.py` crashed on a nav node
  whose location `ut-dump` writes as `null`. Red: `TypeError: 'NoneType'
  object is not iterable`; green: the node is skipped.
- **swept:** `UTA-0259`'s comment above the early exit still holds
  (agrees). The `LE_CYLINDER` comment's "reaches only inside its sphere"
  still holds through the early exit (agrees). No pair list exists.
- **collateral, surfaced, falsified:** none.

# UTA-0049 — lock the cross-compiler numeric contract

**Status:** implemented (2026-09-04) — `tests/unit/NumericContractTest.cpp`
and the floating-point block in `CMakeLists.txt`.
**Kind:** test.
**Source:** ROADMAP UTA-0049 (user-decision-2026-09-04).
**Gate:** none owed, and § 12's empty log is that rather than an omission.
This is a test's own contract, authored by `write-test`, which global rule
14 excludes from `review-contract` — the exclusion follows the contract
rather than the directory it sits in. Do not read the bare "draft" status
of a neighbouring spec into this one: `UTA-0003`'s means ungated and not
to be built from, this file's work is already in the tree.

## 1. Goal

Given inputs chosen so that a fused multiply-add and a separately-rounded
multiply-then-add provably differ, and inputs chosen so that the
transforms `-ffast-math` licenses (reassociation, `x/x → 1`, `x == x →
true`, dropping the sign of zero) provably differ from IEEE-754, this
project's build computes the IEEE-754, non-fused, non-reassociated answer
— on GCC, Clang **and** MSVC — and a test proves it by comparing exact bit
patterns, never by epsilon. The CMake configuration that makes that true
is added and made to refuse a conflicting flag rather than silently lose
to one.

## 2. Problem

`docs/design.md` § "What every part does the same way", the Determinism
bullet, requires floating-point contraction and fast-math off, and no
platform maths library in the simulation or the baker — because two
first-class toolchains (GCC/Clang and MSVC) do not round the same way by
default, and ADR-0002 needs one map, recipe and baker version to hash to
one bundle on any machine.

Nothing enforces this and nothing tests it. `CMakeLists.txt`, read
2026-09-04 before this item, set no floating-point flag at all — no
`-ffp-contract`, no `/fp:`, no fast-math control on any compiler.

Everything below was computed, not guessed (Python's `struct`/`math.fma`
for the arithmetic; for the compiler-flag claims, `g++`/`clang++` compiled
to `-S` assembly or run directly), on this machine, 2026-09-04: GCC
16.2.0, Clang 22.1.8, no `-march` anywhere in this project's build.

- **Contraction defaults ON.** With `-march=native` (so an FMA instruction
  exists), `a*b+c` compiles to one `vfmadd…` on both GCC and Clang with no
  flag at all; `-ffp-contract=off` restores the separate `mulsd`/`addsd`
  pair. **At this project's actual baseline — no `-march` — neither
  compiler contracts, flag or no flag, because there is no FMA instruction
  to contract into.** That is a property of the target, not of
  `-ffp-contract`, and it means a *value-level* comparison (this project's
  own code computing `a*b+c` and comparing it against `std::fma`) is what
  actually exercises the flag, not an assembly grep — and even that
  comparison cannot distinguish the two builds at this baseline. §6 owns
  the detail.
- **The rest of the `-ffast-math` bundle defaults OFF already** (checked
  via `gcc -Q --help=optimizers`): `-fassociative-math`,
  `-ffinite-math-only`, `-freciprocal-math`, `-frounding-math`,
  `-fsignaling-nans` are all `[disabled]`, and `-fsigned-zeros`,
  `-ftrapping-math`, `-fmath-errno` are all `[enabled]`, with no flag
  passed at all. `-ffp-contract` is the one exception, defaulting to
  `fast` on GCC and `on` on Clang.
- **`-fno-fast-math` reverts the whole bundle**, including after an
  explicit `-ffast-math` or the deprecated `-Ofast` earlier on the same
  command line (last-flag-wins, verified by disassembly and by running the
  binary) — on both compilers.
- **A real CMake + Ninja build places `CMAKE_CXX_FLAGS` *before* a
  target's `add_compile_options` in the actual compiler invocation**
  (verified by building a throwaway project with
  `-DCMAKE_CXX_FLAGS=-ffast-math` and reading the generated Ninja command
  line), so flags set once at the top of this project's `CMakeLists.txt`
  win over anything later placed in `CMAKE_CXX_FLAGS` — §8 explains why
  this spec does not lean on that ordering alone.
- **MSVC's spellings could not be verified by compiling** — this machine
  has no MSVC, and `ssh wintest` (this project's Windows box) can run
  binaries but cannot build. `/fp:precise`, `/fp:contract-` and `/fp:fast`
  are taken from Microsoft's own documented behaviour ("`/fp:contract` …
  is enabled by `/fp:fast`, and disabled by `/fp:precise` and
  `/fp:strict`"), stated explicitly here as unverified-by-compilation so
  the next reader does not mistake it for a measured fact the way the
  bullets above are. **The Windows CI leg is what actually gates this**,
  not this document.

## 3. Scope decisions (agreed with the user)

### 3.1 Kind and shape — user, 2026-09-04

ROADMAP UTA-0049 (`Kind: test`, `Source: user-decision-2026-09-04`) already
names the shape: "A unit test that computes a fixed set of expressions —
fused-multiply-add bait, transcendentals, accumulation order — and asserts
an exact bit pattern, run on every matrix leg." Its own progress note
(2026-09-04) widened the scope on measurement: the contract is not merely
untested, it is unenforced, so this item also sets the CMake flags and not
only the test. "Transcendentals" in the roadmap bullet's own wording is
addressed in §9 rather than built — the same note says why: three libms
are not bit-identical and design.md forbids depending on any of them.

### 3.2 The remaining calls are mine, with reasons

`spec-format.md` §3 asks who made each preference call not put to the
user. These were not; each is reversible behind the test file or the
CMake block.

1. **Bit-pattern assertions target `double` only, not `float`.** The
   simulation and the baker are stated in `double` throughout `docs/`; the
   same transforms apply to `float` for the same reasons, and a parallel
   set of assertions would double the file without covering a different
   failure mode.
2. **The build REFUSES (configure-time `FATAL_ERROR`) rather than relying
   on flag ordering alone** when `CMAKE_CXX_FLAGS` carries a
   fast-math-enabling flag. §8 has the reasoning; the short version is
   that silently discarding a flag someone deliberately passed is a worse
   failure than refusing to configure.
3. **NaN payloads are checked structurally (exponent-all-ones,
   mantissa-nonzero), not against one pinned payload.** The contract does
   not promise GCC, Clang and MSVC produce the identical NaN payload for
   `x/x`; it promises none of them folds the result to a false, non-NaN
   answer such as `1.0`. Pinning a payload would assert something the
   contract never claimed.

## 4. Design

### 4.1 The bait values, and why each one separates the two answers

Every value below was computed with Python's `struct`/`math.fma`, not
guessed, and cross-checked by compiling and running the equivalent C++ on
this machine. Full derivations and intermediate steps live in the
write-test report for this item; the values themselves are restated in §5
against each invariant.

- **Contraction (INV-1):** `a = 0.1`, `b = 0.3`, `c = -0.03`. The double
  literal `0.03` rounds to exactly the same bit pattern as `0.1*0.3`
  computed in double, so `c` is exactly the negation of the *rounded*
  product. Separately rounded, `round(a*b) + c` is a value subtracted from
  its own exact negation — always `0.0` in IEEE-754, whatever that value's
  rounding error was. Fused, `fma(a,b,c)` rounds the multiply and add
  together in one step and returns the rounding error the separate path
  had already discarded: a small nonzero value.
- **Reassociation (INV-2):** `a = 1.0`, `b = 1e100`, `c = -1e100`. `1.0` is
  far too small to change `1e100`'s rounded representation, so `(a+b)`
  rounds to `1e100` exactly and `(a+b)+c` rounds to `0.0`. Reassociated as
  `a+(b+c)`, `b+c` cancels to exactly `0.0` first and the whole expression
  returns `1.0` instead.
- **Self-division and self-equality (INV-3, INV-4):** a quiet NaN
  (`0x7ff8000000000001`) and `0.0`, both read through a `volatile`
  intermediate so the optimiser cannot see them as compile-time constants.
  Verified by compiling and running: under `-ffast-math`, `x/x` for both
  returns `1.0` instead of NaN, and `nan == nan` returns `true` instead of
  `false`, on both GCC and Clang.
- **Signed zero (INV-5):** `0.0 - (+0.0)`, with the `+0.0` read through
  `volatile`. Verified: this is `+0.0` (all-zero bits) under strict
  arithmetic and flips to `-0.0` under `-ffast-math`, on both compilers,
  for the identical expression at `-O3`.
- **Accumulation order (INV-6):** two sequences, both crossing a
  `noinline` function boundary so the reduction loop itself is what gets
  compiled, not a compile-time-folded constant.
  - A 7-element sequence, `{1, 1e16, -1e16, 1, 1e16, -1e16, 3}`: summed
    forward it resolves to `3.0` (each leading `1.0` is absorbed without
    trace into the `1e16` beside it, since `1e16`'s representable values
    are `2.0` apart; each pair then cancels to exactly `0.0`); summed
    backward the running total is never `0` between steps, so the same
    rounding-to-a-multiple-of-2 instead loses part of the `3.0` and `1.0`
    terms as they combine with a live total, resolving to `5.0`. Both
    totals were obtained by running the reduction, not derived by hand.
  - A 64-element sequence, `1e16` then sixty-two `1.0`s then `-1e16`:
    under strict arithmetic this resolves to exactly `0.0`. This is the
    one assertion in this file confirmed to be a **live discriminator at
    this project's actual (no `-march`) build target**: compiled with
    `-ffast-math`, GCC 16.2.0 returns `32.0` and Clang 22.1.8 returns
    `48.0` for the identical loop — both wrong, and wrong *differently*.

### 4.2 The CMake enforcement

`CMakeLists.txt` sets, next to the existing MSVC block:

- **GCC/Clang:** `-ffp-contract=off` (load-bearing per §2) and
  `-fno-fast-math` (the documented master negation of the rest of the
  bundle — redundant with the defaults §2 measured, but explicit rather
  than assumed, matching this file's existing style of not trusting a
  compiler default silently).
- **MSVC:** `/fp:precise` and `/fp:contract-`, both explicit rather than
  relied on as the default (§2's unverified-by-compilation note applies).

### 4.3 The refuse guard

If `CMAKE_CXX_FLAGS` or any of `CMAKE_CXX_FLAGS_{DEBUG,RELEASE,
RELWITHDEBINFO,MINSIZEREL}` matches `-ffast-math`, `-Ofast`, `/fp:fast` or
`-ffp-contract=fast`, configuration fails with `message(FATAL_ERROR ...)`
naming the offending variable and value. §8 is the alternatives-considered
reasoning for why this exists rather than relying on flag ordering alone.

## 5. Invariants

- **INV-1** — `a*b+c`, written as ordinary separate operations, does not
  evaluate to the value a fused multiply-add would give, for inputs where
  the two provably differ (§4.1).
  *Test:* `tests/unit/NumericContractTest.cpp`, "a*b+c is not fused into a
  single rounding step".
  *Breaks when:* `-ffp-contract` reverts to `fast`/`on`, or `/fp:contract`
  is enabled. **Known limitation, not hidden:** at this project's default
  (no `-march`) build target there is no hardware FMA instruction, so this
  assertion passes with or without the fix — §2 and §8 explain why it is
  kept anyway.

- **INV-2** — `(a + b) + c` evaluates in the order written; it does not
  equal `a + (b + c)` for inputs where the two provably differ (§4.1).
  *Test:* `tests/unit/NumericContractTest.cpp`, "(a+b)+c is not
  reassociated to a+(b+c)".
  *Breaks when:* `-fassociative-math` (part of `-ffast-math`) is enabled.

- **INV-3** — `x / x` is not assumed to be `1.0`; for `x` = a quiet NaN
  and for `x` = `0.0`, both supplied so the compiler cannot see them as
  compile-time constants, the result is NaN.
  *Test:* `tests/unit/NumericContractTest.cpp`, "x/x is not folded to 1.0
  for NaN or zero".
  *Breaks when:* `-funsafe-math-optimizations` (part of `-ffast-math`) is
  enabled.

- **INV-4** — `x == x` is not assumed `true` for a NaN `x`.
  *Test:* `tests/unit/NumericContractTest.cpp`, "NaN does not compare
  equal to itself".
  *Breaks when:* `-ffinite-math-only` (part of `-ffast-math`) is enabled.

- **INV-5** — `0.0 - (+0.0)` is `+0.0` (bit pattern
  `0x0000000000000000`), never `-0.0`.
  *Test:* `tests/unit/NumericContractTest.cpp`, "subtracting positive zero
  from zero keeps the sign".
  *Breaks when:* `-fno-signed-zeros` (part of `-ffast-math`) is enabled.

- **INV-6** — a fixed sequence summed forward and the same sequence summed
  backward, both by ordinary sequential `+`, give the specific bit
  patterns their written order implies rather than a common reassociated
  answer; a long, exactly-cancelling sequence stays exactly `0.0`.
  *Test:* `tests/unit/NumericContractTest.cpp`, "forward and backward
  accumulation give the order's own answer" and "a long cancelling sum
  stays exactly zero".
  *Breaks when:* `-ffast-math` (or `-fassociative-math` alone, given a
  vectorisable loop) is enabled — §4.1's 64-element case is a measured,
  live discriminator of this at this project's actual build target.

- **INV-7** — the contract is enforced explicitly, on every compiler,
  rather than relying on any compiler's default (INV-1's own measurement
  is that GCC's and Clang's defaults disagree on exactly this), and
  configuration refuses rather than silently losing to a conflicting flag
  passed through `CMAKE_CXX_FLAGS`.
  *Test:* none automated — §7 records this as a real gap. Verified by hand
  this session: `cmake -S . -B <dir> -DCMAKE_CXX_FLAGS=-ffast-math`,
  `-DCMAKE_CXX_FLAGS=-Ofast` and `-DCMAKE_CXX_FLAGS_RELEASE=-ffast-math`
  each refuse to configure with `FATAL_ERROR`, quoting the offending
  variable; `-DCMAKE_CXX_FLAGS=-Wall` configures cleanly.
  *Breaks when:* the flags in §4.2 are removed, or the guard in §4.3 is
  removed or narrowed to miss a spelling it should catch.

## 6. Failure modes

A build that fails any of INV-1 to INV-6 does not crash or hang — it
produces a *different, silently wrong* numeric answer, which is the
failure this whole contract is written against: `ubake` hashing two
different bundles for the same (map, recipe, baker-version) triple with no
error anywhere in the pipeline (ADR-0002). Each invariant's "breaks when"
line above names the one flag responsible; there is no partial-failure
mode beyond "the wrong bit pattern comes out", because every assertion
here is a single equality on a fixed input.

INV-7's failure mode is different in kind: it is a *build-configuration*
failure, not a numeric one. If the guard in §4.3 is removed, the
consequence is not an immediate wrong answer — it is that a future
`CMAKE_CXX_FLAGS` carrying a fast-math flag would be silently accepted (or
rejected only by luck of flag ordering, §2), and INV-1 to INV-6 would then
be the only thing standing between that and a silently divergent bundle
hash. That is why §8 treats "refuse" as worth its cost even though nothing
currently exercises it automatically.

## 7. Tests

`tests/unit/NumericContractTest.cpp` joins the existing `uta_unit_tests`
executable in `tests/CMakeLists.txt`, in its existing
`catch_discover_tests(uta_unit_tests PROPERTIES LABELS "unit;fast" TIMEOUT
30)` call — no new link dependency, since the file needs only Catch2 and
the standard library.

| File | Locks |
|---|---|
| `tests/unit/NumericContractTest.cpp` | INV-1, INV-2, INV-3, INV-4, INV-5, INV-6 |

INV-7 is deliberately absent from that table: its surface is
`CMakeLists.txt` itself, not a test file, and §5 records that as a real
coverage gap rather than papering over it with a citation to something
that does not actually re-check it.

Every assertion compares `std::bit_cast` integer bit patterns, never a
floating-point equality or an epsilon — the whole point of this contract
is exactness, and an epsilon comparison would hide the one-bit
disagreement it exists to catch.

## 8. Alternatives considered (and rejected)

- **Assembly-level assertions (grep the compiled output for `vfmadd`).**
  Rejected: this project's default build target has no FMA instruction at
  all (§2), so an assembly check would trivially pass on every build,
  fixed or not, and prove nothing about the flag. A value-level check is
  the only one that asserts the actual contract (the correct, honest
  answer), even though §5 records that it too cannot distinguish the two
  builds at this baseline.
- **Building the test suite with `-march=native` to make INV-1 a live
  discriminator.** Rejected: it would change what this project actually
  ships and test a target nobody builds for, rather than testing the
  target that matters. The known limitation is recorded instead of
  engineered away.
- **Relying on flag-ordering alone (§2's measured last-flag-wins
  behaviour) instead of a configure-time refusal.** Rejected. The
  ordering is a property of how CMake happens to emit the command line on
  the generators this project uses today — not part of any interface
  contract — and a build that silently discards a flag someone
  deliberately passed is a worse failure than one that refuses to
  configure: the person who passed it does not find out it did nothing
  until the numbers stop matching across machines, which is exactly the
  failure this whole contract exists to prevent.
- **Pinning one exact NaN payload for `x/x`.** Rejected (§3.2): the
  contract does not promise payload identity across compilers, only that
  none of them treats the result as `1.0`. A structural check (exponent
  all-ones, mantissa nonzero) asserts what is actually guaranteed.
- **Asserting `sin`/`cos`/`exp`/`pow` by bit pattern, to cover the "no
  platform maths library" half of the Determinism bullet.** Rejected —
  §9 says why and how that half would be covered instead.

## 9. Out of scope

- **Transcendentals** (`sin`, `cos`, `exp`, `pow`, …). glibc, LLVM's libm
  and MSVC's UCRT are not bit-identical, and design.md's own Determinism
  bullet says the simulation and the baker must not depend on any
  platform maths library *for exactly this reason*. A bit-pattern
  assertion on a transcendental would lock in a value that is not
  actually guaranteed and would red the Windows leg the day one is
  exercised. **How that half of the contract would be covered instead**
  (not built here): a lint over `src/usim` and `src/ubake` refusing a
  direct call to `std::sin`/`std::cos`/`std::exp`/`std::pow`/… (a
  grep-based check or a clang-tidy rule banning those symbols outside a
  single project-owned polynomial/table-based implementation) — a
  build-time refusal shaped like §4.3's flag guard, not a runtime numeric
  test, because the property being enforced is "this symbol is never
  called", not "this value is exact".
- **`float` (32-bit).** §3.2 records the call and the reason.
- **MSVC verification.** §2 says why: no MSVC on this machine, and `ssh
  wintest` cannot build. The Windows CI leg is the actual gate for INV-1
  to INV-7 on that compiler.
- **`usim`/`ubake` code.** Neither exists yet. This item locks the build
  *configuration* the day they are written on top of it, rather than
  waiting for UTA-0011 to discover the configuration was never enforced.

## 10. What checks this

| Invariant | What checks it |
|---|---|
| INV-1 | `tests/unit/NumericContractTest.cpp`. **Blind spot:** does not distinguish the fixed build from the broken one at this project's default (no `-march`) baseline — §2, §5, §8. |
| INV-2 | `tests/unit/NumericContractTest.cpp` |
| INV-3 | `tests/unit/NumericContractTest.cpp` |
| INV-4 | `tests/unit/NumericContractTest.cpp` |
| INV-5 | `tests/unit/NumericContractTest.cpp` |
| INV-6 | `tests/unit/NumericContractTest.cpp` |
| INV-7 | **Nothing automated.** Verified by hand this session (§5's INV-7 test line quotes the runs). No test re-runs this check, so a later edit that weakens or removes the `FATAL_ERROR` guard is caught by nobody until someone tries it again by hand. |

## 11. Cross-doc impact

- **`docs/design.md` § "What every part does the same way", the
  Determinism bullet** — this spec is the first thing that enforces or
  tests the numeric half of that bullet; the "no platform maths library"
  half is still stated there and not yet enforced (§9).
- **`docs/decisions/ADR-0002-bake-maps-offline.md`** — "the same map,
  recipe and baker version must produce the same bundle on any machine"
  is the requirement this contract exists to keep true once `ubake`
  exists; no line of that ADR changes, but this is the first spec that
  makes the requirement checkable rather than asserted.
- **`ROADMAP.md` UTA-0049** — this spec is that item's contract.
- **No other spec cites floating-point behaviour today** — UTA-0002 and
  UTA-0003 are both non-numeric (error/logging/filesystem/jobs, and a
  package-format reader). UTA-0011 (`ubake`, not yet specced) will be the
  first consumer of INV-1 to INV-7, per §9's `usim`/`ubake` bullet.

## 12. Cold-eyes loop log

| Loop | Date | Lanes | Q1 | Q2 | Q3 | Q4 | Outcome |
|------|------|-------|----|----|----|----|---------|

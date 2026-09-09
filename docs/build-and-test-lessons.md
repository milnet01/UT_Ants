# Build and test — what each rule cost

The evidence behind `CLAUDE.md` § Build and test. Each rule there is stated
as an imperative; this is the incident that produced it.

**This is history, not instruction. `CLAUDE.md` governs.** Read this when
you want to know whether a rule still earns its place, or when you are
tempted to remove one.

## Mutate before trusting a green test

**UTA-0007.** Three of four tier-1 cases were vacuous on first writing.
Each passed. Each read correctly. Each was decided by something other than
the rule in its title — a table entry that already returned the refusal, a
range check that subsumed the guard, and a loop bound that rejected the
fixture early. None of that is visible from reading the test.

**UTA-0008.** `scripts/mutation-probe.py` asked the same question
mechanically, 57 times, and found four more of the same shape: a
string-overrun case in a section too short for the node count before it, an
unknown-id case whose payload was too short to decode either way, a
table-bound case whose bad entry sat where a different rule caught it
first, and a graph case that broke two rules at once. Every one passed.
Every one read correctly.

The probe declares its expected survivors with reasons. A new survivor
exits non-zero: some edit made a rule untested, and no other check in this
repository would have said so.

## When a mutation survives under a sanitizer, suspect the fixture

**Measured 2026-09-08 on UTA-0007's `roomAt`.** Removing its child-index
range check survived the tests under AddressSanitizer. The descent is
bounded by `nodes.size()`, and a one-node fixture exits that loop before it
ever dereferences the bad index — so the loop bound rejected the fixture
before the rule under test was reached, and ASan had nothing to see.
Padding the fixture to three nodes turned the same mutation into a reported
heap-buffer-overflow.

The sanitizer is half the answer. The fixture has to reach the code.

## A bounds check is a shape a plain test cannot grade

Remove one and the case is undefined behaviour rather than a wrong answer,
so it passes. UTA-0006 § 4.5's check was proved load-bearing under
AddressSanitizer: without it, that fixture is a heap-buffer-overflow.

This is why the ASan build directory is worth keeping around, even though
address and thread cannot share a binary.

## A path test must not compare against a raw temp path

The Windows runner's temp directory is a short 8.3 name that
`weakly_canonical` expands. So
`result->string().starts_with(dir.path().string())` compares two spellings
of one directory and fails on MSVC alone.

**Cost one red MSVC leg on 2026-09-04**; both Linux legs were green.

## Flip a roadmap item on the matrix, not the local leg

**2026-09-04.** Done the other way round once, and the item read shipped
while Windows was red.

## A spec's invariants must be in the bullet form

`spec-format.md` § 3.7 defines the bullet form and a GFM table, and nothing
else. A paragraph form (`**INV-1.** <claim>`) parses to **zero**
invariants — and `spec_lint` then returns `findings: []`,
`sections_checked: true` and `test_coverage_checked: true`, which is
indistinguishable from a clean document. `invariant_no_test` "always runs",
and ran over an empty set.

**Measured 2026-09-06.** UTA-0005 shipped as accepted, through two full
`review-contract` loops, with thirteen invariants invisible to
`spec_query`, `invariant_check` and `spec_lint` alike.

## `spec_lint` is silent about test surfaces here

It resolves test surfaces only in a `tests/features/<name>/` layout, and
this project uses `tests/unit/`. So `surfaces_checked: false` is permanent,
and `findings: []` is silence rather than a pass. Upstream ANTS-4393 /
ANTS-4679.

## `core.hooksPath` carries two values

**Corrected 2026-09-05**, measured with
`git config --show-origin --get-all core.hooksPath`. `~/.gitconfig` sets it
machine-wide to `~/.claude/githooks`; the repository value overrides it.

The section had read as though unsetting the repository value would disable
the gate. It does not — it falls back to the machine-wide hook, losing this
repository's own hooks rather than the push gate.

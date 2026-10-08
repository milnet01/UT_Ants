---
paths:
  - "tests/**"
  - "scripts/mutation-probe.py"
  - "scripts/mutations/**"
  - "docs/specs/**"
---

# Five rules this project paid for

This loads when a session reads a test, a mutation file or a spec.
[`docs/build-and-test-lessons.md`](../../docs/build-and-test-lessons.md) holds
what each one cost — read it before deciding one no longer earns its
place.

- **Mutate before trusting a green test.** A test that passes and reads
  correctly may still be graded by something other than the rule it names.
  `./scripts/mutation-probe.py <subject>` asks mechanically, and a new
  survivor exits non-zero. **Its subjects are the files in
  `scripts/mutations/`**, each mutation tagged with the invariant it
  breaks; `--coverage` names the invariants with none. A lane with no file
  there is mutated by hand, and the rule still applies there.
  Add `--asan` for a rule the Release leg cannot see, such as a bounds
  check.
- **When a mutation survives under a sanitizer, suspect the FIXTURE before
  concluding the check is unnecessary.** Ask which rule makes this fixture
  fail, and whether it is the rule you meant to test. A bounds check is the
  shape a plain test cannot grade — remove one and the case is undefined
  behaviour rather than a wrong answer, so it passes.
- **A path test must not compare against a raw temp path.** The Windows
  runner's temp directory is an 8.3 name that `weakly_canonical` expands.
  Assert the property instead: resolve under both spellings, compare the
  results.
- **Write a spec's invariants in `spec-format.md` § 3.7's bullet form.** A
  paragraph form parses to ZERO invariants, and `spec_lint` then returns
  `findings: []` with its section and coverage flags true — indistinguishable
  from a clean document. Check `spec_query` reports a non-zero
  `invariants_count` before trusting a clean lint.
- **`spec_lint` reports `surfaces_checked: false` on this project, always.**
  It resolves test surfaces only in a `tests/features/<name>/` layout and
  this project uses `tests/unit/`, so `findings: []` is SILENT about test
  surfaces rather than a pass. Read the flag before the count and check the
  `*Test:*` clauses by hand.

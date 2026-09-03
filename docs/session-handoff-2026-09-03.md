# Where this project stands — 2026-09-03

Written at the end of the first session so the next one starts from the
record. **Everything below is checkable from the repository and the
roadmap store; nothing here is the only copy of anything.**

## State

`~/.claude/workflow.md` state **5 — on an item**, with the queue built.
Discovery and design are both agreed and both gated.

## What exists

| | |
|---|---|
| `docs/discovery.md` | The problem, who it is for, **12 signs of success** (S1–S12), what this deliberately does not do. Signed off |
| `docs/design.md` | 16 parts in four groups, **19 dependency rules**, the stack. Signed off, and gated to its cap |
| `docs/decisions/` | ADR-0001 to ADR-0006. ADR-0003's serving clause is superseded by ADR-0006; ADR-0006's own origin test is corrected by design rule 15, and its Status says so |
| `docs/standards/versioning-overrides.md` | Milestones map to `0.1.0`–`0.6.0`; the `1.0` bar is S8 + S6 |
| `docs/design-review-2026-09-03.md` | The review loop log — six loops, 51 findings, all fixed |
| `ROADMAP.md` | **Rendered from the store. Do not hand-edit it** — the next `roadmap_log` write discards anything typed in |

## The one thing that is easy to get wrong

**The roadmap's source of truth is the store, not the file.** Read it with
`roadmap_query`, write it with `roadmap_log`. `ROADMAP.md` is a render,
and it is committed only so the repository has something to show.

## Next

**`UTA-0041` — the CI pipeline and the local gate.** It is next because it
protects every push after it, on a repository that is meant to go public.
Then `UTA-0002` (core) and into the package reader.

Two items were added at the end of the session and have had no design
gate of their own: **`UTA-0040`** (parallax occlusion mapping) and
**`UTA-0041`**. Neither changes a dependency rule, so neither re-arms the
`review-contract` gate on `docs/design.md` — but `UTA-0040` will add a
renderer material property, and that edit should be read against rule 3
when it lands.

## Not done, and deliberately

- **No git remote.** The repository is local. Publishing it is an
  outward-facing action nobody has authorised yet, and `UTA-0041`'s guard
  should exist before the first push rather than after it.
- **No CI, no pre-push gate wired.** That is `UTA-0041`.
- `src/` is still empty. `UTA-0002` onward fill it.

## Verifying this file rather than trusting it

```sh
cd /mnt/Games/Scripts/Linux/UT_Ants
git log --oneline                      # the session's commits
cmake -S . -B build -G Ninja && cmake --build build && ctest --test-dir build
```

The suite passes with no Unreal Tournament present. That is S7, and it is
the one claim here worth re-checking first.

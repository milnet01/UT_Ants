# `CLAUDE.md` — change history

Why a rule in `CLAUDE.md` reads the way it does, and what it read before.
Kept here rather than in the document, because that file is read in full by
every session on every turn and this is read when somebody asks.

**This is history, not instruction. Nothing here governs.** Where a line
disagrees with `CLAUDE.md`, `CLAUDE.md` is right and this file is stale.

**What stayed in `CLAUDE.md` deliberately: the measurements.** *"Cost one
red MSVC leg on 2026-09-04"*, *"Measured 2026-09-08 on UTA-0007's
`roomAt`"*, and the rest of that class are the evidence a rule exists at
all — a session that cannot see the cost will fix the rule back. Those are
not history and were not moved.

The per-run loop logs are separate again: `docs/claude-md-review-<date>.md`.

---

## § Where this project is — `Next:`

**2026-09-09.** Read *"Priority rule 1 is still clear: `UTA-0059` remains
the only open review-sourced item"*. That stopped being true the moment
`UTA-0084` was filed. The set is no longer enumerated by hand; the section
now points at the roadmap query, for the same reason `In flight:` stopped
being hand-kept on 2026-09-05.

**2026-09-09.** `Next:` moved from `UTA-0009` to `UTA-0052` on the user's
call. Both items' bodies already recorded that `UTA-0052` wants to land
first, and its `Blocked-by: ubundle` cleared when `UTA-0008` shipped.

## § Which item comes next — rule 1's set

**2026-09-09, UTA-0084's own gate, found by two lanes.** The list named
`audit-<date>`, `debt-sweep-<date>`, `code-quality-review-<date>` and
`review-code-<date>`, and omitted `review-contract-<date>` — the only token
either open review-sourced item actually carried. A conformer matching the
list literally found rule 1's set empty and went to rule 2, skipping both
`UTA-0059` and `UTA-0084`. The tokens are now examples, with the
`roadmap_query` source filter as the operative route.

**2026-09-09.** A sentence written during that same fix said
`roadmap-format.md` § 3.5.3 recognises four `Source:` values. It defines
five; `user-<date>` is the fifth and records no review. Caught by executing
the claim before the loop closed.

## § Running two sessions at once

**2026-09-08.** The section read *"departs from `workflow.md` § 1, which
allows exactly one item in flight"*. That was true when written and stopped
being so when § 1's own gate changed the rule to *per session* on
2026-09-07. The two-session cap is stricter than § 1 rather than a
departure from it, so it needs no override file.

**2026-09-08.** `docs/standards/workflow-overrides.md` carried these rules
until it was retired at its own review gate, which found the second copy
diverging from this one on every loop. `docs/reviews/workflow-overrides-loop-log.md`
is that record. This is why `CLAUDE.md` says it is the only home for the
rules and forbids restating them elsewhere.

### rule 1 — naming yourself

**2026-09-09, UTA-0084.** The holder now records its **checkout** as well
as its session name. Nothing on this machine reports which checkout a
session occupies, so the progress note is the only place that fact exists,
and rule 3 needs it to tell a second session from a first.

### rule 3 — the start-up test

**2026-09-09, UTA-0084, in two passes.**

The test was `git worktree list` and `ListAgents`. Neither counts this
project's sessions: `ListAgents` is machine-wide, and a worktree outlives
the session that made it. A session following the rule learned nothing
about who held what.

The replacement then claimed `git worktree list` showed whether the main
checkout was free. It does not — it enumerates the worktrees that exist and
reports no occupancy. All three lanes of the gate on that very fix caught
it. A session trusting it would have seen one entry, concluded main was
free, and stayed in it: the breach rule 3 opens by naming.

## § This file's own review history

**2026-09-09.** The pointer named `docs/claude-md-review-2026-09-04.md`
alone, so every run after the first was unreachable from the document.

# Workflow overrides — UT_Ants

Deltas only. Everything not named here follows `~/.claude/workflow.md`
unmodified.

## Override — at most two sessions, so at most two items

**Global rule.** `workflow.md` § 1:

> **Exactly one item is in flight per session.** A second waits. Where
> several sessions work one project from separate worktrees — which § 4
> provides for — each holds one, and the 🚧 bullets are read together.
> What this forbids is ONE session carrying two, which makes § 1's
> question unanswerable: two items drifting differently put that session
> in two states at once.

**This project.** Two sessions at most, from 2026-09-05, decided by the
user to get more work done in parallel. Each holds one item, so two
items is the total — a third session finding two already 🚧 picks
nothing up.

**The delta is the cap, and it is stricter than the global rule rather
than a departure from it.** `workflow.md` § 1 already provides for
several sessions each holding one item and sets no ceiling on how many.
One item per session is that rule followed unchanged. What this file
adds is the ceiling of two, which § 1 does not impose.

> **Corrected 2026-09-08.** This section previously quoted § 1 as
> *"Exactly one item is in flight at a time"* and presented one item per
> session as a departure from it. That quotation was accurate when this
> file was written and stopped being so two days later: § 1's own review
> gate changed the rule to *per session* on 2026-09-07, adding the
> multi-worktree provision quoted above. A reader was therefore told the
> global standard forbids something it expressly allows.

## What this override does NOT relax

- **Two sessions may not hold the same item, and no session holds two
  un-parked items.** Rule 2 of `CLAUDE.md` § Running two sessions at
  once. **A 🚧 bullet parked on `Waiting-on:` does not count against
  either limit** — `workflow.md` § 1 makes it the exception, and
  `roadmap-format.md` § 3.5.4 has the session skip it and take the next
  workable item. This project's roadmap carries no parked bullet today,
  so the carve-out is stated rather than exercised.
- **The roadmap store is keyed to the main checkout, and a worktree
  reaches it by saying so.** `CLAUDE.md` § Running two sessions at once
  states both halves: every roadmap verb passes the main checkout as
  `caller_cwd` whatever worktree you are in, and a verb given the
  worktree's own path instead falls back silently to patching that
  worktree's `ROADMAP.md`. The fallback is the failure mode, not the
  rule. A session that reads it as the rule never flips 🚧, and that
  flip is the claim the whole coordination rests on.
- **A session boundary does not end an item.** Work still under way
  stays 🚧 across it, naming its holder — `CLAUDE.md` rule 5. 📋 is for
  a deliberate abandonment only, because 📋 tells the other session by
  rule 2 that a half-built item is free. A 🚧 whose named holder is no
  longer live is abandoned and may be resumed.
- **Nothing about the gates.** Every push is gated, and an item is still
  flipped ✅ on the matrix rather than on a local leg.

## The cost, accepted

Two sessions can still edit one file from two different items, and
nothing detects that but the merge. `CLAUDE.md` rule 4 mitigates it by
assigning items that do not share a directory; it does not remove it.

## Cold-eyes loop log

Rows live in `../reviews/workflow-overrides-loop-log.md`.

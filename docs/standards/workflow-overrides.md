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
user to get more work done in parallel. Each holds one item, so at most
two items are in flight.

**The delta is the cap, and it is stricter than the global rule rather
than a departure from it.** § 1 already provides for several sessions
each holding one item and sets no ceiling on how many. One item per
session is that rule followed unchanged. What this file adds is the
ceiling of two, which § 1 does not impose.

> **Corrected 2026-09-08.** This section previously quoted § 1 as
> *"Exactly one item is in flight at a time"* and presented one item per
> session as a departure from it. That quotation was accurate when this
> file was written and stopped being so two days later: § 1's own review
> gate changed the rule to *per session* on 2026-09-07, adding the
> multi-worktree provision quoted above. A reader was therefore told the
> global standard forbids something it expressly allows.

## The mechanics are not in this file

**How a session learns the cap is reached, which item is available, what
a 🚧 parked on `Waiting-on:` means, what an abandoned 🚧 means, and what
survives a session boundary, is `CLAUDE.md` § Running two sessions at
once.** This file states the delta and stops.

**That is a repair rather than a preference.** Earlier drafts restated
those mechanics here, and each cold read found a restatement saying
something its original does not — a parked bullet reading as claimable
when § 3.5.4 says skip it, an item held by a dead session reading as
unavailable when rule 2 says resume it, and a cap on SESSIONS tested by
counting ITEMS, which passes for a third session whenever a live session
happens to hold none. One rule stated twice is two rules that will
disagree, and the second copy is the one nobody maintains.

## What this override does NOT relax

- **The roadmap store is keyed to the main checkout.** `CLAUDE.md`
  § Running two sessions at once has every roadmap verb pass the main
  checkout as `caller_cwd`, whatever worktree you are in, and records
  what `roadmap_log` does when given the worktree's own path instead. A
  session that skips that step never flips 🚧, and that flip is the
  claim the coordination in this file rests on.
- **Nothing about the gates.** This override touches none of them. The
  push gate and the matrix rule stand exactly as `CLAUDE.md` § Build and
  test states them — including its three ways a green push can have
  checked nothing, which this file neither repeats nor relaxes.

## The cost, accepted

Two sessions can still edit one file from two different items, and
nothing detects that but the merge. `CLAUDE.md` rule 4 mitigates it by
assigning items that do not share a directory; it does not remove it.

## Cold-eyes loop log

Rows live in `../reviews/workflow-overrides-loop-log.md`.

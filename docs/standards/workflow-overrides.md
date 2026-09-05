# Workflow overrides — UT_Ants

Deltas only. Everything not named here follows `~/.claude/workflow.md`
unmodified.

## Override — two items may be in flight, one per session

**Global rule.** `workflow.md` § 1:

> **Exactly one item is in flight at a time.** A second waits. Anything
> else makes § 1's question unanswerable — two items drifting differently
> put the project in two states at once.

**This project.** Two items may be in flight at once, at most one per
session, from 2026-09-05. Decided by the user to get more work done in
parallel.

## Why the standard's objection does not bite here

The rule's stated reason is that § 1's question — *where is this project?*
— becomes unanswerable, because two items put the project in two states
at once.

That is an objection to the project's state being a **scalar somebody
remembers**. It was, and the fix was to stop keeping it that way:

- **`CLAUDE.md`'s `In flight:` is no longer hand-kept.** It says to ask
  `roadmap_query status:"in-progress"`, which names *every* item in
  flight rather than one. The answer is a set, and a set of two is as
  answerable as a set of one.
- **`State:` follows from that same answer** — 4 when the set is empty, 5
  otherwise — so it cannot disagree with it.
- **The roadmap is written by a tool, not by hand**, and a session sets
  🚧 when it picks work up, minutes before doing the work. So the record
  is fresher than a note somebody meant to update.

The standard's worry was real and is what this override had to answer.
What made one-item-at-a-time load-bearing was that the record could only
hold one item; once the record is a query, the constraint it was
protecting is gone.

## What this override does NOT relax

- **Two sessions may not hold the same item.** Rule 2 of `CLAUDE.md`
  § Running two sessions at once.
- **A session still finishes what it starts, or hands it back.** An
  abandoned item returns to 📋 rather than sitting 🚧.
- **Nothing about the gates.** Every push is gated, and an item is still
  flipped ✅ on the matrix rather than on a local leg.

## The cost, accepted

Two sessions can still edit one file from two different items, and
nothing detects that but the merge. `CLAUDE.md` rule 4 mitigates it by
assigning items that do not share a directory; it does not remove it.

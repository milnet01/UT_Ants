# Workflow overrides — UT_Ants

Deltas only. Everything not named here follows `~/.claude/workflow.md`
unmodified.

## Override — items in flight per session

**Global rule.** `workflow.md` § 1: *"Exactly one item is in flight per
session. A second waits."*

**This project.** There is no limit. A session may hold several un-parked
🚧 items at once.

What `workflow.md` § 1 protects is still answerable:
- **The project's state** is `CLAUDE.md`'s formula: 5 while any 🚧 is not
  parked on `Waiting-on:`, else 4.
- **Which items are in flight** is every 🚧 that is not parked:
  `roadmap_query status:"in-progress"`.
- **Resuming a 🚧** keeps § 1's rule: a session resumes one only when it
  marked the item itself or the user names it.

**Why.** The user decided on 2026-09-25. Waiting to start a second item
while the first finishes wastes time: most often the only step left is
GitHub's CI run. Staying in one session also saves the tokens a new
session spends re-reading the project.

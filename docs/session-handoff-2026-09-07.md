# Session handoff — 2026-09-07 (superseded)

**The work this described is done.** It told the reader to close § 4.6's
residue, starting at `bounds` and then `lightmap bytes`. That happened on
2026-09-08 and `UTA-0069` shipped. The file is kept because the `UTA-0069`
ROADMAP bullet names it, and reduced to this pointer because a handoff that
still says the work is open is worse than no handoff.

Nothing here is load-bearing any more. Each thing it carried now lives where it
belongs:

| What it carried | Where it lives now |
|---|---|
| The residue, and how it closed | `docs/specs/UTA-0069-model-bsp-tables.md` § 4.6 |
| The element layouts, including `FLightMapIndex` and `FLeaf` | that spec § 4.5 |
| Why `leaves` is now returned | that spec § 4.1 |
| What INV-4 asserts, and why it is scoped | that spec § 5, INV-4 |
| The version-61 layout and its derivation | the `UTA-0072` ROADMAP bullet |
| The measurements, session by session | the `UTA-0069` ROADMAP bullet |
| A broken test citation in UTA-0004's spec | `UTA-0074` |
| What to do next | `CLAUDE.md` § Where this project is, `Next:` |

**One thing it raised was never answered and is not recorded elsewhere.**
Milestone scope for v0.1.0: eight of its open items are visual polish on a
renderer that does not exist yet — `UTA-0040`, `UTA-0044`, `UTA-0045`,
`UTA-0051`, `UTA-0052`, `UTA-0053`, `UTA-0054`, `UTA-0055`. Moving them to
0.2.0 would cut the remaining work without changing what 0.1.0 delivers.
**Proposed 2026-09-07, still open, and it needs the user.** Do not act on it
unprompted.

---
paths:
  - "tools/ut-paths/**"
  - "src/unav/**"
  - "tests/unit/Path*"
  - "docs/specs/UTA-0121*"
---

# Routing: read these before touching it

This loads when a session reads the routing code, its tests or its spec.

**No exit in the reference library is shot** (`UTA-0130`, censused
2026-09-25). `MonsterEndSB` wins by `TakeDamage` only with `TriggerType ==
TT_Shoot`, and no map uses it: every exit is player- or pawn-proximity. So
reaching the exit's cylinder is the right routing test here. The counts are on
`UTA-0130`. `ut-dump`'s `exits` carries `triggerType`,
`damageThreshold` and `bInitiallyActive`; census again before trusting this on
another library.

**Read `offWorld` as "not walkable to", never as "cannot be finished".**
Maps carrying an Assault-to-MH conversion kit put an `MHEnd` actor on the
MonsterEnd, and it touches the MonsterEnd when the final objective fires.
`UTA-0131` has the evidence.

**Triggering the MonsterEnd is the WHOLE win condition**, with no monster
count anywhere in it.

**Read `UTA-0196` before any chain work**: an earlier three-node chain
reached the exit's node but hung off a component the start cannot reach.

**Do not read UT_MonsterHunt's stored route verdicts without checking
firmness.**

**A teleporter that starts switched off still routes, unless nothing in the
map switches it on** (`UTA-0142`, UTA-0121 § 3 decision 10). The game's own
planner routes through one at round start, so do not cut its links on
`bEnabled` alone.

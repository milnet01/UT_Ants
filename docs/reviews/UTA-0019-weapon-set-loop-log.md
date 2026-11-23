# UTA-0019 — loop log

## Cold-eyes loop log

| Loop | Date | Lanes | Q1 | Q2 | Q3 | Q4 | Outcome |
|------|------|-------|----|----|----|----|---------|
| impl | 2026-11-11 | none | – | – | – | – | Building the tick amended § 4.4 (`expire`), § 4.5 (every body moves before any fires; `[set]`'s `head-height` and `self-damage`; a radius-0 projectile traced) and § 4.6 (`weaponSlot`, `weaponStep`). No reviewer dispatched: the gate is cancelled here. |
| impl | 2026-11-12 | none | – | – | – | – | The user asked for modern explosion effects: § 4.8 and § 9 point the stand-in sphere at UTA-0470. No reviewer dispatched: the gate is cancelled here. |
| impl | 2026-11-12 | none | – | – | – | – | Matching the replays amended § 4.4: `splash-from`, `body-hit` and `splash-up-max`. No reviewer dispatched: the gate is cancelled here. |
| impl | 2026-11-14 | none | – | – | – | – | Matching the replays amended § 4.9: a body that moves onto a shot is met along the shot's last move (`Projectile.Touch`). No reviewer dispatched: the gate is cancelled here. |
| impl | 2026-11-15 | none | – | – | – | – | The user widened INV-6: every mode that sleeps keeps a steady cadence (a shot more or fewer, within three ticks), and a body moved onto a shot keeps its push. No reviewer dispatched: the gate is cancelled here. |
| impl | 2026-11-15 | none | – | – | – | – | Matching the replays amended § 4.9 and INV-6: a shot launched inside a body meets it only heading in, and the replay draws a split's break tick again until as many pieces hit as the original's. No reviewer dispatched: the gate is cancelled here. |
| impl | 2026-11-16 | none | – | – | – | – | Matching the replays amended § 4.9: a falling shot turning over fast in a tick falls only after the top of its arc. No reviewer dispatched: the gate is cancelled here. |
| impl | 2026-11-16 | none | – | – | – | – | Matching the replays amended § 4.4's projectile keys: `expire-up`, how far above the shot a blast at the end of its lifetime goes off (`UT_Grenade.Timer`'s 16). No reviewer dispatched: the gate is cancelled here. |
| impl | 2026-11-16 | none | – | – | – | – | Matching the replays amended § 4.9: a falling projectile moves a tick longer than 0.05 in the engine's steps. No reviewer dispatched: the gate is cancelled here. |
| impl | 2026-11-16 | none | – | – | – | – | Matching the replays amended INV-6: a launch a tick out has its hits on a body compared as one, damage summed and the velocity after the last less the fall between. No reviewer dispatched: the gate is cancelled here. |
| impl | 2026-11-16 | none | – | – | – | – | Matching the replays amended § 4.9: a shot's hit on a body is less a thousandth of the trace from where its last tick began (`UPrimitive::LineCheck`). No reviewer dispatched: the gate is cancelled here. |
| impl | 2026-11-16 | none | – | – | – | – | Building the last two script rules amended § 4.4 and § 4.9: `spread-grow` and `spread-max` widen a held mode's cone (the Enforcer's alt), and a gel still in flight bursts 3.1 to 3.3 s after launch. No reviewer dispatched: the gate is cancelled here. |
| impl | 2026-11-17 | none | – | – | – | – | UTA-0020 amended § 4.5 with a pointer: a human body, the time scale the tick sets last, and a weapon picked on the wheel. No reviewer dispatched: the gate is cancelled here. |
| impl | 2026-11-22 | none | – | – | – | – | UTA-0468 amended § 4.4, § 4.7, § 4.8, § 4.9, § 7, § 9 and INV-6: the hammer's alt turns shots aside, the launcher locks on and fires seeking rockets, and a second Enforcer doubles the first; the Enforcer's primary joins INV-6's cadence allowance (user, 2026-11-22). No reviewer dispatched: the gate is cancelled here. |
| impl | 2026-11-23 | none | – | – | – | – | UTA-0501 amended § 4.2, § 4.5 and INV-6: a switch is a put-down then a bring-up, with the Flak Cannon's loading after, each ended on a tick, so the switch runs' two-tick allowance goes; the weapons file is version 2. No reviewer dispatched: the gate is cancelled here. |
| impl | 2026-11-23 | none | – | – | – | – | UTA-0502 amended § 4.2 (an optional `select-rate`) and § 4.5: a switch asked for while a weapon comes up cuts its select short under 0.75 of it, finishes it at 0.75 or more, and sends a loading weapon down at once, as recorded at 30 fps. No reviewer dispatched: the gate is cancelled here. |

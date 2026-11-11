# UTA-0017 — loop log

## Cold-eyes loop log

| Loop | Date | Lanes | Q1 | Q2 | Q3 | Q4 | Outcome |
|------|------|-------|----|----|----|----|---------|
| impl | 2026-11-10 | none dispatched | - | - | - | - | Fold-back from the build, no reviewer: angles moved to core/Angles.h (the baker's, reused); § 4.3 gains the fitted rules and stopSpeed, dodgeLandingKeep, dodgeDoneTime, walkableFloorZ (unconfirmed); § 4.4 step-up on any short move and the floor probe's step off a leaning wall; INV-2 replays each row from the recording, tightens to 0.15/0.15/1 and names its exemptions; § 14 narrowed to long-tick friction; § 8 records the open replay rejected. |
| impl | 2026-11-10 | none dispatched | - | - | - | - | Amendment after the user tried walking, no reviewer: § 3 item 7 and § 4.4 add the double jump as the `airJumps` rule, off by default; § 4.8 adds the eye height smoothed by UT99's `PlayerPawn.UpdateEyeHeight`, which stops stairs shaking the view. |
| impl | 2026-11-10 | none dispatched | - | - | - | - | Amendment for UTA-0018, no reviewer: § 4.8 names the `uinput` frame as the walker's input, adds the pad's Dodge press along the left stick, and the bottom face button as a jump. |
| impl | 2026-11-11 | none dispatched | - | - | - | - | Measurement, no reviewer (UTA-0459): § 4.3's walkableFloorZ row now cites ut-record's slope-0707 (walks up normal Z 0.7071, DM-Conveyor) and slope-0698 (slides off 0.6983, DM-Chasm), confirming 0.7 within that bracket; the value is unchanged. |
| impl | 2026-11-11 | none dispatched | - | - | - | - | Amendment for UTA-0019, no reviewer: § 4.2 names UTA-0019 § 4.6, which adds aim and buttons to `MoveCommand` and a push to a body. Movement with no push is unchanged. |

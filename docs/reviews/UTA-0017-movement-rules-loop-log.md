# UTA-0017 — loop log

## Cold-eyes loop log

| Loop | Date | Lanes | Q1 | Q2 | Q3 | Q4 | Outcome |
|------|------|-------|----|----|----|----|---------|
| impl | 2026-11-10 | none dispatched | - | - | - | - | Fold-back from the build, no reviewer: angles moved to core/Angles.h (the baker's, reused); § 4.3 gains the fitted rules and stopSpeed, dodgeLandingKeep, dodgeDoneTime, walkableFloorZ (unconfirmed); § 4.4 step-up on any short move and the floor probe's step off a leaning wall; INV-2 replays each row from the recording, tightens to 0.15/0.15/1 and names its exemptions; § 14 narrowed to long-tick friction; § 8 records the open replay rejected. |
| impl | 2026-11-10 | none dispatched | - | - | - | - | Amendment after the user tried walking, no reviewer: § 3 item 7 and § 4.4 add the double jump as the `airJumps` rule, off by default; § 4.8 adds the eye height smoothed by UT99's `PlayerPawn.UpdateEyeHeight`, which stops stairs shaking the view. |

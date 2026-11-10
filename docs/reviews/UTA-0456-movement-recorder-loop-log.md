# UTA-0456 — loop log

## Cold-eyes loop log

| Loop | Date | Lanes | Q1 | Q2 | Q3 | Q4 | Outcome |
|------|------|-------|----|----|----|----|---------|
| impl | 2026-11-10 | none — no reviewer dispatched; the build was the reader | - | - | - | - | Step 8 fold-back from the first build and full record. **Corrections:** the reader's type is `uta::Result`, not `core::Result`; the reader also refuses INV-4 gaps, which INV-4's test needed. **Added from use:** the joystick and render-device rows in § 4.4 (this machine's controller drifted `aStrafe` in the first calibration); the pawn zeroes mouse and turn axes; `Bots` on the PAWN line; a failure mode for a player who dies or swims mid-run (a probe run fell into a pit and hung the launch). **Settled § 14:** physics runs after `PlayerInput` in the same tick; INV-4 holds with `dt[k+1]`; K = 6000 at 30 and 120 fps. No design premise moved. |

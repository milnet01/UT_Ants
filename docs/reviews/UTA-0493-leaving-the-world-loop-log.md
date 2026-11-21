# UTA-0493 — loop log

## Cold-eyes loop log

| Loop | Date | Lanes | Q1 | Q2 | Q3 | Q4 | Outcome |
|------|------|-------|----|----|----|----|---------|
| impl | 2026-11-21 | none | - | - | - | - | Building it amended § 4.1, INV-1 and INV-2: Outside needs a map with rooms, not a zone table, since a map whose only zone is 0 would read Outside everywhere; a zone with no room is Unknown; INV-2 breaks on a field dropped, not one written unsigned, which round-trips the same. No reviewer dispatched: the gate is cancelled here. |
| impl | 2026-11-21 | none | - | - | - | - | Building it amended § 4.4, INV-8, § 6 and § 10: 11 of 20213 reference starts are Outside as placed, so INV-8 now asks that every map keep a start inside the level, and the client lifts each start clear and drops one still outside, as UT99's spawn fails there. No reviewer dispatched: the gate is cancelled here. |

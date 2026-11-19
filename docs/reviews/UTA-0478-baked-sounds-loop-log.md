# UTA-0478 — loop log

## Cold-eyes loop log

| Loop | Date | Lanes | Q1 | Q2 | Q3 | Q4 | Outcome |
|------|------|-------|----|----|----|----|---------|
| impl | 2026-11-19 | none | – | – | – | – | Building it amended § 4.3, § 4.4, § 4.6, § 10, § 13 and INV-2's clause: the report's `sounds` member is shaped as `props` is, with `why`; a sound past `SOUND_LIMIT` is skipped and reported; the client makes no `--baker-version` check on the sound bundle, which records nothing to check it against; the launcher bakes the sound set once as it starts; INV-2's limit breach is the check dropped, since the check already runs after the vector is built; § 13 carries the measured sizes. No reviewer dispatched: the gate is cancelled here. |

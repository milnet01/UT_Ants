# UTA-0112 — baked light probes — loop log

The review record for `docs/specs/UTA-0112-baked-light-probes.md`, kept outside
the spec per `~/.claude/standards/spec-format.md` § 6. `review-contract` writes
one row per loop as it closes. Rows are never back-filled and a landed row is
never edited.

## Cold-eyes loop log

| Loop | Date | Lanes | Q1 | Q2 | Q3 | Q4 | Outcome |
|------|------|-------|----|----|----|----|---------|
| 1 | 2026-09-11 | 3 | 1 | 0 | 0 | 5 | 6 verified, 0 dismissed, 6 fixed. Five untestable clauses. INV-7's sky triangle sat in solid, so sky seeding could not fail it. Nothing pinned `lightAt`'s absolute scale. A library sine in `sineOf` or `directionOf`, or a dropped polynomial term, passed INV-4, which now evaluates them in a `static_assert` and records `sineOf`'s bits. § 4.4's `LT_None` rule could not be reached, since `LITE` holds no such light. One false claim: `FGetHSV`'s body is also in a copy of Unreal Tournament 4's engine source, so the premise now reads "no source this spec draws on". All three lanes found the first three. Three open questions resolved clean: special-lit surfaces take ordinary lights in bake and renderer alike; the golden fixture's light is moot once INV-4 grades the sine; `Movers.cpp`'s `Vec3` alias is file-local. Unrunnable region stated: whether § 4.3 looks like UT99, and the bake's cost on real maps. |

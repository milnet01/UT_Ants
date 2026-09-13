# ADR-0008 — acquire dependencies by route — loop log

The review record for
`docs/decisions/ADR-0008-acquire-dependencies-by-route.md`, kept outside the
ADR. `docs/decisions/README.md` gives an ADR no loop-log section and says it
is never edited after acceptance, so rows cannot go in the document.
`review-contract` writes one row per loop as it closes. Rows are never
back-filled, and a landed row is never edited.

## Cold-eyes loop log

| Loop | Date | Lanes | Q1 | Q2 | Q3 | Q4 | Outcome |
|------|------|-------|----|----|----|----|---------|
| 1 | 2026-09-13 | 3, cold — genre pinned `adr`; packet: `docs/decisions/README.md`, ADR-0007 whole, ADR-0006's header, ADR-0002's reproducibility sentence, **S7**, `docs/design.md`'s `ubundle` row and § The stack, the new standard whole, the `find_package` and glm CMake blocks, `ci.yml`'s Vulkan steps. Unrunnable region: the runner package census and upstream repository contents, judged against ADR-0007's record | 1 | 2 | 1 | n/a | **Four verified, four fixed, none dismissed.** All four anchor in the gated span, commit 71500fb. **[Q1] glm was justified as arithmetic the baker runs**, while only `uta_urender` links it; the pin is now grounded on what ADR-0002 would require once baker arithmetic uses it. **[Q2] Two lanes found *"Two things have no mechanical check"*** against a standard whose What-checks-this table marks more; the bullet now points at that table rather than counting. **[Q2] The standard's routing question 2 had dropped ADR-0007's test** and read *a* route-3 acquisition, so a library only the SDK ships would route differently from the decision; fixed in the standard, which owns the question. **[Q3] ADR-0007's obligation that the README name the Vulkan prerequisite was carried nowhere**; the standard's § 5 now states it. **Clean-resolved open questions:** `README.md` already names the inputs and both acquisitions; ADR-0007's placement of the `find_package` binds nothing now that it is superseded; the vcpkg validation-layers port is in ADR-0007's record. **Collateral:** route 4's wording, tightened after the standard's question 5 changed. |

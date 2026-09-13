# `dependency-acquisition.md` — loop log

The review record for `docs/standards/dependency-acquisition.md`, kept
outside the document per `~/.claude/standards/documentation.md` § 9.1.
`review-contract` writes one row per loop as it closes. Rows are never
back-filled, and a landed row is never edited.

## Cold-eyes loop log

| Loop | Date | Lanes | Q1 | Q2 | Q3 | Q4 | Outcome |
|------|------|-------|----|----|----|----|---------|
| 1 | 2026-09-13 | 3, cold — genre pinned `standard`; packet: `docs/design.md` § The stack, ADR-0008, `dependencies.md` §§ 1, 4, 8 and 9, `docs/standards/README.md`, the root, `src/urender`, `src/umat` and `tests` CMake files, `ci.yml`, `scripts/ci.sh`'s docs exit and configure step, README § Building it, `third_party/bc7enc/README.md`, and `FindVulkan.cmake` excerpts. No unrunnable region for this tree | 0 | 1 | 6 | n/a | **Seven verified, seven fixed, none dismissed.** All seven anchor in the gated span, commit 71500fb, which drafted the document. **Five of the seven were on § 2's routing question.** Question 1 keyed route 3 on version matching, which the loader it exists for does not clearly meet; it now keys on being the platform's driver stack. Question 2 read *a* route-3 acquisition where the SDK and the distribution packages supply different things; it now reads *every* acquisition § 5 accepts. Question 4 left a header-only `INTERFACE` target undecided; glm's own CMake makes `glm::glm` one when its library build is off, and it now counts as linked. Question 5 contradicted its own test-only sentence and read every library target as non-runtime; route 4 is now one optional executable behind a build option of its own. **Two elsewhere:** the opening now puts toolchain libraries such as `Threads` out of scope, and § 5 names the Windows runtime DLL that `ci.yml` installs and checks in its own step. One more rule came from the ADR-0008 run and landed here, where it lives: § 5 now requires `README.md` to name the three inputs and both acquisitions. **Clean-resolved open questions:** bc7enc's licence file is present, `scripts/ci.sh` has no Vulkan probe, and the uncovered What-checks-this rows are self-assessable. **Collateral:** ADR-0008's route 4 wording, tightened to match question 5. **Out of scope, corrected in passing:** `docs/design.md`'s index prose listed the validation layers among found inputs. |

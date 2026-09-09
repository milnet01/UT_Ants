# bc7enc — vendored

The block-compression encoders `umat` uses: BC7 from `bc7enc.c`/`bc7enc.h`,
and BC1–BC5 from `rgbcx.h` (with its table header). `docs/specs/UTA-0052-texture-memory-budget.md`
§ 3 decision 4 chose it; `docs/decisions/ADR-0007-acquire-dependencies-by-route.md`
route 2 is why it is vendored rather than fetched — its build system produces
a demo executable, not a library anyone links.

## Upstream

- **Repository:** <https://github.com/richgel999/bc7enc>
- **Commit:** `f66c2e489b07138f2673a2fb3d27c1aa1d565c48` (2022-07-04)
- **Retrieved:** 2026-09-09

A vendored copy *is* its own pin, so there is no version to bump; this file
is what says which copy it is. Nothing in the build reads it — neither
`scripts/ci.sh` nor `.githooks/pre-push` mentions `third_party` — so whether
this copy has gone stale is a question somebody asks by hand. ADR-0007's
route 2 admits that in as many words.

### What landed, and its checksum

    817bfa5d30a2c4702e8c387ebb5a69398034c95905f580a6b8cd4261da901058  bc7enc.c
    6a129cd608eebf9f2796dd1ad4659f8c4fbe2a2ace371b728fc4ece92f449c90  bc7enc.h
    51aad8f8a068b782671f9277631ec721b72c0b34624c38a684ff46df8bc780c9  LICENSE
    0f71984084336191cbe5406450a072b312bea6021ed9e00c720ed00ddb75fb93  rgbcx.h
    5051cf4ce17cb1f9d011ef788b7c8d174f2270532da0514c9f394830bab72f36  rgbcx_table4.h

`bc7decomp.cpp/h` is upstream's BC7 *decoder* and is deliberately not
vendored: nothing here decodes a block.

## Licence

MIT **or** the Unlicense, at our choice — either of which this project's
GPL-3.0 `LICENSE` absorbs. The upstream text is in `LICENSE` beside this file.

**`bc7enc.h` is covered, and this was checked before the file was copied in.**
That file's `LICENSE` enumerates `rgbcx.h`, `bc7decomp.cpp/h` and `bc7enc.c`
and does not name `bc7enc.h`. The header settles it itself, on its own first
line — *"File: bc7enc.h - Richard Geldreich, Jr. - MIT license or public
domain (see end of bc7enc.c)"* — and that pointer resolves: the full dual text
sits at the end of `bc7enc.c`. So the enumeration's omission is shorthand
rather than a reservation. Recorded because a licence question on a GPL-3.0
repository is settled once and then cited, never re-derived.

## The maths, and why it does not break determinism

`bc7enc.c` includes `<math.h>`. `docs/design.md`'s Determinism bullet rules
out *"no platform maths library in the simulation or the baker"*, so the
letter of that rule is breached by this dependency and it is worth being
plain about it.

**What it actually calls is the part that matters.** Measured over the
vendored copy at the commit above:

| Function | Calls | IEEE-754 status |
|---|---|---|
| `sqrtf` | 2 | correctly rounded — mandated |
| `floor` / `floorf` | 5 | exact |
| `fabs` / `fabsf` | 4 | exact — clears a sign bit |

**No `pow`, `exp`, `log`, `sin`, `cos`, `tan`, `atan2`, `cbrt` or `hypot`** —
the family whose results genuinely differ between libm implementations.
`rgbcx.h`, which is the BC4 and BC5 path, calls `fabs` and nothing else.

That rule's own stated ground is `ADR-0002`: one map, recipe and baker version
must hash to one bundle on any machine, across two compilers. Every function
above is pinned exactly by IEEE-754, so that ground holds. **INV-6 is what
actually grades it** — a golden byte array compared on GCC, Clang and MSVC —
so if this reasoning is wrong, a leg goes red and says so.

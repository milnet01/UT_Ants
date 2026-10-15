I found two medium-severity defects, five low ones, and a contract document that no longer describes the current format. Nothing was critical or high. Below are the line counts and what was in my context, then the findings in the brief's format.

**Line counts as read** (`src/ubundle/`):

| File | Lines | File | Lines |
|---|---|---|---|
| `AddedLamps.cpp` | 100 | `LightSection.cpp` | 134 |
| `AddedSun.cpp` | 35 | `MaterialSection.cpp` | 237 |
| `BakerSection.cpp` | 32 | `MoverSection.cpp` | 69 |
| `Bundle.cpp` | 461 | `NavSection.cpp` | 94 |
| `Bundle.h` | 737 | `OcclusionSection.cpp` | 89 |
| `Codec.h` | 261 | `PaintedSkySection.cpp` | 51 |
| `CollisionSection.cpp` | 248 | `PlacementSection.cpp` | 297 |
| `FlameSection.cpp` | 79 | `RoomSection.cpp` | 189 |
| `GeometrySection.cpp` | 133 | `Sections.h` | 203 |
| `LampSection.cpp` | 98 | `ShadowMaskSection.cpp` | 192 |
| `LightProbeSection.cpp` | 111 | `Sky.cpp` | 74 |
| `SunSection.cpp` | 70 | `Sky.h` | 40 |
| `TextureSection.cpp` | 185 | `TileAnswers.cpp` | 116 |
| `TileAnswers.h` | 60 | `WiringSection.cpp` | 123 |
| `ZoneSection.cpp` | 121 | | |

**Already in my context when I started:**
- The user's global `~/.claude/CLAUDE.md`, which includes rules about review skills.
- A harness note giving the user's email address.
- This lane's agent prompt.
- No memory index and no git snapshot.

**Where I read outside the lane files:**
- **`src/urender/Frame.cpp`, lines 150–305 (`shapeOf`).** The KNOWN TRAPS line asks whether every section is sampled there, so that question put the file in play.
- **UTA-0008, §§ 4.1–4.5 and §§ 4.9–6.**
- **`cpp.md`, only Idioms through Spellings.** I did not read the rest of that file.
- **Searches for callers.** They all excluded `tests/**`.

## Critical (0)

## High (0)

## Medium (2)

- **[dim 3]** `AddedSun.cpp:20` — `light.rotation = {-sun.pitch, sun.yaw + 32768, 0};`
  - `yaw` is any i32 taken straight from the file (`SunSection.cpp:18`).
  - `validateSun` (`SunSection.cpp:41`) checks `pitch` and never checks `yaw`.
  - So a bundle with `yaw > INT32_MAX - 32768` causes signed overflow, which is undefined behaviour (UB). `applySun` runs on every loaded bundle (`apps/ut-ants/main.cpp:449`, `tools/ut-shot`, `ut-ref`, `ut-bench`), so a crafted or downloaded bundle reaches it.
  - fix: bound `yaw` in `validateSun` (for example to [0, 65536)), or do the arithmetic in `uint32_t` and convert back.
- **[dim 2]** UTA-0008 is stale against the code. **The document is the wrong side; this goes to `review-contract`.**
  - § 4.3 says `formatVersion` is `15`, INV-4 and § 6 say `14`, and `Bundle.h:73` says `28`.
  - The § 4.10 API block shows `FORMAT_VERSION = 7`, and its write order stops at `SMSK`. The code writes `BAKR` first (`Bundle.cpp:373`), which breaks the "appended, extended rather than contradicted" rule in that same clause.
  - The § 4.4 id list stops at `SMSK`.
  - The § 4.2 minimum-size table is wrong: `MaterialRecord` 6 (code: 42), `GeometryBatch` 16 (code: 24), `Zone` 4 (code: 29). It has no rows for FLAM, SMSK, LAMP, SUN, PSKY or BAKR.
  - BAKR has no spec at all: `Sections.h:21` points to "BakerSection.cpp's own header", and that header is two lines.
  - Someone building a second reader from this spec would build the wrong format.
  - fix: amend UTA-0008 §§ 4.2, 4.3, 4.4, 4.10, INV-4 and § 6; give BAKR and `readBaker` a spec home.

## Low / Info

- **[dim 2]** `TileAnswers.cpp:105` — `const auto bytes = fs::readFile(*path);` — UTA-0008 § 4.1 says "This library performs no file I/O", and `Bundle.h:12` says the same. `applyTileAnswersFile` breaks that. I can't tell which side is wrong (see Open questions). fix: move the file-reading wrapper into the apps or `core`, or amend § 4.1.
- **[dim 2]** `Bundle.cpp:381` — `if (bundle.textures) sections.push_back(Section{ID_TEXS, {}, texturesSize(*bundle.textures)});`
  - `write` never applies TEXS's own rules: format byte at most 2, dimensions and mip count, and `blocks.size() == expectedBlockBytes`. So it emits textures that `read` refuses (`TextureSection.cpp:95,105,120`).
  - That contradicts `Bundle.cpp:315` ("a bad bundle cannot be produced here") and `Codec.h:205` ("a writer never emits what the reader would refuse").
  - fix: call `validateTextureFields` and the length check on the write path, with InvalidArgument.
- **[dim 2]** `Bundle.cpp:301` — `if (descriptor.id != ID_BAKR) continue;` … `return`
  - `Bundle.h:732` says "every descriptor in `prefix` [is] checked as `read` checks them". In fact the loop stops at BAKR, and none of the table-wide rules run (order, duplicates, sections tiling the file).
  - So a BAKR whose offset points back into the header or table bytes reads those bytes as a version string. If they happen to be printable ASCII, the launcher is told a version for a file that `read` refuses.
  - fix: require `descriptor.offset >= HEADER_SIZE + sectionCount*24`, and correct the doc comment.
- **[dim 2]** `MaterialSection.cpp:123` — `look.sparks.resize(count);`
  - The count is checked against `FIRE_SPARKS_MAX` (4096) but not against the bytes left in the section. That breaks the letter of INV-2 and § 4.2 rule 1, though the allocation is capped at about 32 KB.
  - fix: refuse when `count > cursor.remaining() / 8` before the resize.
- **[dim 13]** `Sky.cpp:23` — `static_cast<char>(std::tolower(ch))` — the result depends on the program's locale. It is only safe if nothing calls `setlocale`. I did not check whether anything does. fix: fold ASCII by hand (`ch >= 'A' && ch <= 'Z' ? ch + 32 : ch`).
- **INFO:** Dimensions 8, 10, 12 and 15 do not apply. There is no shared state, and the tail names no observability, accessibility or privacy commitment.
  - Dimension 9: `write` returns a vector, so persistence belongs to the caller.
  - Dimension 17: an exact version match means every older bundle is refused as UnsupportedVersion. I did not read § 14 on migration.

## Covered by spec and looks correct

- **Framing (`decodeHeader`, `readDescriptor`, `read`'s table pass):** checked against § 4.3, § 4.4 (all five table rules), § 4.5 and INV-4, 5, 11 and 12. This includes the division-form section-count bound and the overflow-safe extent check.
- **`readVector`:** divides before it reserves.
- **`readString`, AOCC and SMSK texels, PLAC raw values:** all bounded by `readBytes`.
- **Minimum and fixed sizes:** I checked the arithmetic against each reader for LITE 73, LAMP 90, MATS 42, ZONE 29, GEOM 33 and 24, TEXS 18, FLAM 32, LPRB 84 and 72, SMSK 12 and 12, COLL 45 and 28, and MOVR 52.
- **ROOM, NAVG and WIRG validators:** match every § 4.9 bullet, including the deliberate `iZone` exception.
- **COLL walk:** terminates, and its links are checked before it runs.
- **Cross-section validators:** they cover LAMP shapes in `validateVertexZones`, the sun's slot in `validateShadowMaskAcross`, and the LPRB layer counts.
- **`applyAddedLamps`:** the SMSK renaming keeps each chart's pairs strictly ascending, and the sun's slot is renamed to `lite + joined`.
- **Write order:** matches the `Bundle.h:700` list.

## Open questions

- **File I/O in `TileAnswers`:** did UTA-0277 knowingly place `applyTileAnswersFile` in `ubundle`, against UTA-0008 § 4.1? If yes, the spec is wrong; if not, the code is.
- **Known trap (`shapeOf`):**
  - LPRB `sunSeen` is sampled.
  - PSKY is read every frame in `drawView` (`Frame.cpp:1685`), so it needs no sample.
  - LITE, where SUN and LAMP land after folding, is sampled **only by count** (`Frame.cpp:303`).
  - So if light values are uploaded behind the shape check, a bundle that differs only in sun or lamp values passes as current. I did not check the upload path; that is the urender lane's to settle.
- **Effect byte 255:** LITE and LAMP lights may carry `effect == SUN_EFFECT` (255), because LITE bytes are "not range-checked". Is the renderer safe when a crafted bundle injects a second "sun"?
- **`KNOWN_SECTIONS = 19` (`Bundle.cpp:98`):** it is kept in step with `knownId` by hand. A twentieth section without bumping it makes `readBaker` refuse valid bundles. It has not diverged yet, so I have not filed it.
- **`readBaker` on an older-format bundle:** it returns an error (UnsupportedVersion). I did not check how `MapList.cpp` treats that.

## 3 items to fix first

1. **The `yaw` overflow in `AddedSun.cpp:20`.** It is the only finding where bytes from someone else's bundle reach UB in code that runs on every map load.
2. **Make `write` check TEXS.** Right now the writer can produce a bundle its own reader refuses, which the code promises can't happen. A bad texture would only surface at load time, far from its cause.
3. **Send UTA-0008's staleness and BAKR's missing spec to `review-contract`.** The contract for a format that will soon arrive from other players doesn't describe the format, so a second reader, or a reviewer checking this one, has nothing correct to measure against.

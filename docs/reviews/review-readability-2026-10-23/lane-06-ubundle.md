## Files read
- src/ubundle/AddedLamps.cpp — 100 / 100
- src/ubundle/AddedSun.cpp — 39 / 39
- src/ubundle/BakerSection.cpp — 32 / 32
- src/ubundle/Bundle.cpp — 468 / 468
- src/ubundle/Bundle.h — 760 / 760
- src/ubundle/CMakeLists.txt — 60 / 60
- src/ubundle/Codec.h — 261 / 261
- src/ubundle/CollisionSection.cpp — 248 / 248
- src/ubundle/FlameSection.cpp — 79 / 79
- src/ubundle/GeometrySection.cpp — 150 / 150
- src/ubundle/LampSection.cpp — 98 / 98
- src/ubundle/LightProbeSection.cpp — 144 / 144
- src/ubundle/LightSection.cpp — 134 / 134
- src/ubundle/MaterialSection.cpp — 242 / 242
- src/ubundle/MoverSection.cpp — 69 / 69
- src/ubundle/NavSection.cpp — 94 / 94
- src/ubundle/OcclusionSection.cpp — 89 / 89
- src/ubundle/PaintedSkySection.cpp — 55 / 55
- src/ubundle/PlacementSection.cpp — 297 / 297
- src/ubundle/RoomSection.cpp — 189 / 189
- src/ubundle/Sections.h — 208 / 208
- src/ubundle/ShadowMaskSection.cpp — 192 / 192
- src/ubundle/Sky.cpp — 80 / 80
- src/ubundle/Sky.h — 54 / 54
- src/ubundle/SunSection.cpp — 70 / 70
- src/ubundle/TextureSection.cpp — 199 / 199
- src/ubundle/TileAnswers.cpp — 116 / 116
- src/ubundle/TileAnswers.h — 60 / 60
- src/ubundle/WiringSection.cpp — 123 / 123
- src/ubundle/ZoneSection.cpp — 121 / 121

## What I started with
- The global `~/.claude/CLAUDE.md`.
- The user-email context block.
- A SessionStart hook message saying Ants MCP is connected.
- The review-lane agent instructions.
- No project `CLAUDE.md`, no memory index and no git snapshot.
- I read the shared-context brief (STEP 1) before the lane files.

## Summary
This code reads and writes the game's own map file, `.utab`. The file holds one small header, then a table of contents, then up to nineteen labelled sections: rooms, navigation, textures, materials, geometry, lights, collision, light probes, shadows, added lamps, the sun, the painted sky and so on. Every section has its own file that turns its bytes into data and back, and checks the data is consistent before trusting it. A few helper files sit alongside:
- one folds added lamps and the sun into the lights the renderer draws;
- one finds where the sky is seen from;
- one applies the player's answers about which textures may vary across tiles.

## Findings

[Q1] HIGH `src/ubundle/LampSection.cpp:20` — `constexpr std::uint64_t MIN_LAMP = 1 + 73 + 12 + 4;` — The `73` is LITE's record size, hand-copied from `LIGHT_SIZE` in `LightSection.cpp:19`. A third copy sits in `scripts/map-analyser.py:100` (`LIGHT_SIZE, ... = 73, ...`). LITE records have grown twice already: Bundle.h's version history lists format versions 10 and 13 as LITE changes, and the UTA-0162 spec tells the editor to change `LIGHT_SIZE` in `LightSection.cpp`. The next person to add a light field will follow that pattern, bump `LIGHT_SIZE`, and leave the 73 in `MIN_LAMP` and in the analyser script stale. Nothing in `LightSection.cpp` points at either copy. The same file also re-declares `FLAME_RECORD = 32` (line 22), a second copy of `FlameSection.cpp:14`. — Fix: declare `LIGHT_SIZE` and `FLAME_RECORD` once in `Sections.h` and build `MIN_LAMP` from them; add a comment in `LightSection.cpp` naming the analyser's copy.
Cost: pinned — `docs/specs/UTA-0162-strip-lights.md` (line 83) names "`LIGHT_SIZE` in `src/ubundle/LightSection.cpp`", so moving the constant contradicts it. No ANCHORS entry covers these lines. Searched: `LIGHT_SIZE|FLAME_RECORD|MIN_LAMP` across the tree and in `docs/specs`.

[Q1] MEDIUM `src/ubundle/Bundle.cpp:98` — `constexpr std::size_t KNOWN_SECTIONS = 19;` — This number is kept equal to the id list in `knownId` (lines 89–94) by hand. The `static_assert` below it checks the prefix size, not whether the count matches the list. A stranger adding a twentieth section will add its id to `knownId`, the read dispatch and the write order, and miss this number. `readBaker` would then refuse every valid bundle that holds all twenty sections, with no test pointing at the cause. The section list is also hand-copied into the order comments at `Bundle.h:723` and `Bundle.cpp:355`, and into the header comments of `Bundle.cpp` and `Sections.h`. A review on 2026-10-15 noted the risk and did not file it (`docs/reviews/review-code-2026-10-15/lane-03-ubundle.md:95`). — Fix: build `knownId` and `KNOWN_SECTIONS` from one `constexpr` array of section ids.
Cost: one edit, provided `knownId(descriptor.id)` keeps its name and its call. `scripts/mutations/ubundle.py` pins the line `if (!knownId(descriptor.id))` and the refusal under it, so do not reshape those two lines. Searched: `knownId|KNOWN_SECTIONS` in `docs/` (found only the review record, which is not a spec).

[Q4] LOW `src/ubundle/ShadowMaskSection.cpp:105` — `// Both are GEOM's vertex count, which validateShadowMaskVertices checks;` — No function called `validateShadowMaskVertices` exists; the check is done by `validateShadowMaskAcross`, further down the same file. A stranger searching for the name finds only this comment. — Fix: rename it to `validateShadowMaskAcross` in the comment.
Cost: one edit — comment only. Searched: `validateShadowMaskVertices` across the tree (one hit, this line).

[Q4] LOW `src/ubundle/Bundle.cpp:213` — `// No post-decode validator beside this one, unlike the three` — "The three above" dates from when only ROOM, NAVG and WIRG came before TEXS. Now most arms have a validator. FLAM, LAMP and SUN also have none, and no comment beside them says why: they are checked across sections after decoding, which only `Sections.h` explains. A stranger may think those three arms are missing a check and add one. — Fix: drop "unlike the three above", and add one line at the FLAM, LAMP and SUN arms saying they are checked across sections below.
Cost: one edit — comment only.

[Q4] LOW `src/ubundle/Bundle.cpp:263` — `// Unreachable: knownId() refused every other id while the table was being validated. Named rather than folded into the WIRG arm` — WIRG is no longer the last arm; BAKR is. The reasoning still holds, but naming WIRG makes a stranger look for the wrong arm. — Fix: say "the last arm" instead of "the WIRG arm".
Cost: one edit — comment only. (`ubundle.py` replaces `knownId` lines with `ID_WIRG`, but this comment is not in its search text.)

[Q4] LOW `src/ubundle/Bundle.cpp:277` — `// UTA-0156 SS 4.2: a rule across GEOM, MOVR and ZONE, so it waits for all three.` — `validateVertexZones` also checks every LAMP shape (`ZoneSection.cpp:104-108`). `Sections.h:148-149` says the same three. A stranger changing LAMP's shape rules would not know this check also reaches LAMP. — Fix: add LAMP to both comments.
Cost: one edit — comment only.

[Q4] LOW `src/ubundle/Bundle.cpp:3` — `// Each section's own layout is its codec's -- RoomSection.cpp, ...` — The file list stops at `ShadowMaskSection.cpp`. It leaves out `LampSection.cpp`, `SunSection.cpp`, `PaintedSkySection.cpp` and `BakerSection.cpp`, all of which `read` and `write` dispatch to. A stranger would think those sections are handled somewhere else. — Fix: replace the list with "one file per section, declared in Sections.h".
Cost: one edit — comment only.

[Q4] LOW `src/ubundle/Bundle.h:76` — `/// 31 since UTA-0391 gave PSKY its haze start.` — The version history jumps from 29 to 31. Version 30 came from UTA-0387 (`docs/specs/UTA-0387-sky-seen-haze.md:68`: "`FORMAT_VERSION` becomes 30"), which added LPRB's `skySeen`. A stranger bumping the version may wonder whether 30 was skipped on purpose. — Fix: add "30 since UTA-0387 gave LPRB each probe's sky visibility".
Cost: one edit — comment only. This text is not the `FORMAT_VERSION =` line that `quarantine-guard-test.sh` pins. Searched: `30 since|version 30|FORMAT_VERSION.*30`.

[Q4] LOW `src/ubundle/Bundle.h:285` — `/// UTA-0055 SS 4.4: the spacing it was cut at, in 16 units: 1, 2, 4, 8 or` — "In 16 units" can be read as "16 UT units", not "the spacing divided by 16". The spec says `S / 16`. A stranger writing a baker could store the spacing itself. — Fix: write "the spacing divided by 16 (UTA-0055's S / 16)".
Cost: one edit — comment only. Searched: `swayCell` in the UTA-0055 spec (line 117). Confirms candidate `Bundle.h:285` as a real finding, though not as a stale count.

[Q5] LOW `src/ubundle/LampSection.cpp:76` — `UTA_CHECK(validateFlame(bundle, lamp.flames[f], 0, flame, code));` — The bare `0` means "this flame may name no light", which forces `light == -1`. The reason is only in `Bundle.h`'s `AddedLamp::flames` comment ("each `light` -1"). In `Sections.h:169` the explanation of the `lights` parameter is attached to `readFlame`, which has no such parameter, so it does not help either. — Fix: add a short comment at the call ("a lamp's flames are tied to its light at apply time, so -1 only"), and move the `Sections.h` note onto `validateFlame`.
Cost: one edit — comments only.

[Q3] LOW `src/ubundle/Sky.cpp:38` — `const auto read = [&](const std::vector<PropertyRecord>& properties) {` — This local lambda shares its name with `uta::ubundle::read`, the bundle decoder in the same namespace. A stranger reading `read(actor.properties)` will at first take it for a decode call. — Fix: rename it to something like `takeProperties`.
Cost: one edit — the lambda is local to `skyViewOf`, so no spec or catalog can name it. Searched: both mutation catalogs; neither touches `Sky.cpp`.

[Q3] LOW `src/ubundle/TileAnswers.cpp:58` — `if (const std::size_t hash = line.find('#'); hash != std::string_view::npos) line = line.substr(0, hash);` — Here `hash` holds the position of a `#` character. In this same function, `Hash` is the picture's content hash and `hash` names one at line 78. — Fix: rename it to `commentStart`.
Cost: one edit. `scripts/mutations/tilekind.py` pins `if (!conflicted.contains(hash)) out.answers`, which is the line-79 `hash`, not this one. Searched: `tilekind.py`.

[Q3] LOW `src/ubundle/AddedLamps.cpp:68` — `const auto sun = static_cast<std::uint32_t>(lite + records);` — `sun` holds the sun's old light index, while `bundle.sun` in the same branch (line 83) is the sun itself. Likewise `lite` (line 31) holds LITE's original size, not the lights. A stranger editing the renaming loop can mix these up. — Fix: rename them `oldSunIndex` and `liteSize`.
Cost: one edit. Searched: `` `lite|lite \+ `` outside src/ubundle — found only a review record and a test-local variable, no spec.

[Q3] LOW `src/ubundle/Codec.h:230` — `void putEncoded(const Result<std::vector<std::byte>>& encoded) { if (encoded) append(*encoded); else overflowed_ = true; }` — Any nested refusal is recorded as `overflowed_`, and `finish()` then reports "a count exceeds the format's u32". That is true today, because `encodeGeometry` refuses only on overflow. A stranger who adds another refusal to a nested encoder will get a misleading message. — Fix: keep the nested error and return it from `finish()`, or rename the flag to `refused_` with a neutral message.
Cost: one edit — `putEncoded` and `overflowed_` appear only in src/ubundle. Searched: `putEncoded|overflowed_` outside src/ubundle (no hits).

## Candidates dismissed
- `src/ubundle/Bundle.cpp:142:read` — The length is a flat dispatch, one arm per section, all the same shape. A stranger follows it. The real risk is the hand-kept section lists, filed under the `KNOWN_SECTIONS` finding.
- `src/ubundle/Bundle.cpp:318:write` — Same reason: a flat validate-then-emit list with a comment at each arm.
- `dup:d83735c69e60` — `FireSpark` / `ufire::Spark` / `upkg::Spark` — Three identical copies, but on purpose. The comment at `Bundle.h:208-209` names the link boundary (INV-10) that forbids sharing, and UTA-0286 §§ 4.1–4.2 prescribe both copies. The layout is UT99's fixed stored spark format, so there is no likely edit to get wrong.
- `Bundle.cpp:151` "2,863,311,531 descriptors" — This is the exact value of 0xAAAAAAAB, which the same comment gives. Arithmetic, not a count that can go stale.
- `Bundle.cpp:216` "11 says" — A spec section number ("SS 11"), not a count.
- `Bundle.h:8` "2 keeps" — "Rule 2", not a count.
- `Bundle.h:285` "16 units" — Not a stale count; filed as a wording ambiguity (Q4 above).
- `Bundle.h:552` "4096 dense maps" — A why-comment about the old limit, tied to UTA-0322.
- `Bundle.h:733` "16 bytes" — This is `HEADER_SIZE`, fixed by the format.
- `RoomSection.cpp:123` "6 records" — A spec section number ("SS 6").
- `TextureSection.cpp:92` "16 bytes" — Matches `bytesPerBlock` for every format except BC4, which is what the comment says.
- `TileAnswers.h:13` "64 hex digits" — A fixed fact of the file format; `hashOf` checks `size() != 64`.
- `WiringSection.cpp:80` "10 records" — A spec section number ("SS 10").

## Correctness leads
None found. Things I checked and found guarded:
- the `firstPair + pairCount` loops: `runWithin` runs before them;
- `fileSize - HEADER_SIZE`: `decodeHeader` refuses short input first;
- `validateFlame` reading `materials`: the bound check comes first;
- the size checks on `applyAddedLamps`' `rename` and `addLayer`: the validators guarantee them.

Searched: none beyond reading the files.

## Not checked
- `Bundle.cpp:30` cites "SS 4.5" for why `readHeader` must not bound `sectionCount`. The spec's §4.4 is the section table and §4.5 is the origin field. The citation may be deliberate (§4.5's fail-closed rule is the reason), or it may be a slip for §4.4. I did not read the spec text to settle it.
- I did not check that the hand-computed minimum sizes in the section files match their spec tables. I checked only their arithmetic against the comments beside them, and all of it adds up.
- `scripts/map-analyser.py` beyond line 100, so whether it has other hand-kept copies of sizes.

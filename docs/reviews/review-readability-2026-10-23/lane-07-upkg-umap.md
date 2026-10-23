## Files read
Line counts are as `wc -l` would give them, from native `Read`, every file read in full:
- src/umap/Build.cpp — 620 / 620
- src/umap/Build.h — 88 / 88
- src/umap/CMakeLists.txt — 45 / 45
- src/umap/Rooms.cpp — 74 / 74
- src/umap/Rooms.h — 133 / 133
- src/upkg/ByteReader.cpp — 140 / 140
- src/upkg/ByteReader.h — 58 / 58
- src/upkg/CMakeLists.txt — 33 / 33
- src/upkg/Class.cpp — 369 / 369
- src/upkg/Class.h — 176 / 176
- src/upkg/Geometry.cpp — 491 / 491
- src/upkg/Geometry.h — 212 / 212
- src/upkg/Level.cpp — 185 / 185
- src/upkg/Level.h — 69 / 69
- src/upkg/Package.cpp — 356 / 356
- src/upkg/Package.h — 140 / 140
- src/upkg/Properties.cpp — 348 / 348
- src/upkg/Properties.h — 140 / 140
- src/upkg/Script.cpp — 428 / 428
- src/upkg/Script.h — 46 / 46
- src/upkg/Sound.cpp — 79 / 79
- src/upkg/Sound.h — 36 / 36
- src/upkg/Texture.cpp — 232 / 232
- src/upkg/Texture.h — 92 / 92

## What I started with
- `~/.claude/CLAUDE.md`, the global rules.
- The account's email context block.
- A SessionStart hook message advertising the Ants MCP verbs.
- The environment block: cwd `/home/ants/.cache/lane-neutral`, which is not a git repo.
- The review-lane agent file.
- No project `CLAUDE.md`, no memory index and no git snapshot.

I did not open `review-cost.md`. My brief and the lane file did not conflict. One process note: `workspace_search` hit its rate limit once, and I used `Grep` for the rest.

## Summary
This code reads the game's original level and asset files ("packages") byte by byte. It turns them into plain structures the rest of the project can use: names, classes, scripts, textures, sounds, level geometry and bot paths. It refuses anything malformed rather than guessing. The `umap` half takes a level's own map data, works out which "room" (zone) each spot belongs to, and draws each room's outline for a map screen. It also sorts the rooms into floors. The code is heavily commented and mostly easy to follow. The main risks for a newcomer are:
- hand-copied checks that have started to drift apart;
- a few comments that now point at things that are no longer true.

## Findings

[Q1] HIGH `src/upkg/Class.cpp:31` — `std::tolower(static_cast<unsigned char>(character))));` — Case-folding names is done in four places, and they have drifted apart:
- Three copies fold by hand, because `std::tolower` depends on the machine's locale: `src/unav/Build.cpp:42-53` (the fix there cites UTA-0219), `src/ubundle/Sky.cpp:20-25` (fixed after review-code 2026-10-15) and `src/ubake/Install.cpp:65-72`.
- `Class.cpp`'s `fold` still calls `std::tolower`, and its comment ("ASCII only, which is what the format's names are") tells a stranger it is already safe.

Two past fixes have each repaired one copy and missed this one. The next person fixing a case bug will trust this comment too. — Fix: fold ASCII by hand here, as the three siblings do. Cost: one edit — Searched: `docs/` for `fold` (the only spec naming a fold is UTA-0011, which names ubake's `detail::fold`, not this one). Searched: `src` for `tolower` and for `fold`/`foldCase`/`folded` definitions. `src/urender/Tiers.cpp:21` (outside my lane) also uses `std::tolower`.

[Q1] MEDIUM `src/upkg/Geometry.cpp:74` — `Result<void> checkCount(const ByteReader& reader, std::int32_t count,` — The guard that stops a file's declared count from causing a huge allocation (INV-4) is hand-copied at nine sites:
- **Geometry.cpp:** `checkCount` :74, `reserveVertices` :50, and inline in `readPolys` :296-309.
- **Level.cpp:** :52, :120-129, :150-158.
- **Texture.cpp:** :80, :153-163, :201-212.

Each copy has its own minimum-bytes floor and its own message. UTA-0216 (`ROADMAP.md` near line 12627) had to audit these sites one by one and fixed only the Polys one. A stranger tightening the guard would edit `checkCount` and believe every table is covered. Two of the copies sit in the same file as `checkCount`. — Fix: one shared upkg helper taking count, minimum element bytes and noun, used by all nine. Cost: one edit — Searched: `docs/` for `checkCount`, `reserveVertices`, `REACH_SPEC_MIN_BYTES`: no hits. Searched: `src` for "bytes remaining can hold".

[Q4] MEDIUM `src/upkg/Texture.cpp:13` — `/// The version from which a mip's WidthOffset and a sound's NextOffset are` — The comment says this constant governs Sound's field too. It does not: `src/upkg/Sound.cpp:14` has its own `OFFSET_FIELD_MIN_VERSION = 63`. Someone who finds the boundary is wrong would edit this one and leave Sound unchanged. — Fix: share one constant in a upkg header, or drop "and a sound's NextOffset". Cost: one edit — Searched: `docs/` for `OFFSET_FIELD_MIN_VERSION`: no hits. Searched: tree for that name: only these two definitions.

[Q4] MEDIUM `src/upkg/Texture.cpp:29` — `/// resolving "is this class a Texture?" properly needs the class table, which` / `/// is UTA-0005 and is not built (SS 3.3 item 4).` — UTA-0005 is built: `readAncestry` and `resolveClass` are in `Class.h`. Someone adding support for a mod's texture subclass would believe ancestry is unavailable and add another name to the list. — Fix: say why matching by name is still chosen now that UTA-0005 exists, or drop the "not built" clause. Cost: one edit (comment only). The same claim is echoed at `Texture.h:69-72`.

[Q4] MEDIUM `src/upkg/Level.cpp:16` — `/// The smallest a reach-spec record can be: three int32s and a byte are fixed,` — The constant on line 21 is `4 + 1 + 1 + 4 + 4 + 4 + 1`, which is four int32s: distance, collisionRadius, collisionHeight and reachFlags, as read at lines 163-170. A stranger reconciling the two would "fix" the code to match the comment. That loosens the allocation guard. — Fix: "four int32s". Cost: one edit (comment only).

[Q4] LOW `src/umap/Build.cpp:67` — `` /// `traceLoops` rotates through these by index, so the order is a contract. `` — There is no `traceLoops`. The function that relies on this order is `traceComponent`, at lines 277-280 and 312-316. A stranger checking who depends on the order finds nothing. — Fix: name `traceComponent`. Cost: one edit (comment only). Searched: whole tree for `traceLoops`: only this line.

[Q4] LOW `src/umap/Build.cpp:33` — `` /// One lattice column's cell, in the grid `traceComponent` walks. `` — This is the doc comment on `Lattice`, which is the whole sampling grid (sizes, origin, spacing), not one cell. A stranger reads it as a per-cell struct. — Fix: open with "The sampling grid", then keep the paragraph on how cells are placed. Cost: one edit (comment only).

[Q4] LOW `src/umap/Build.cpp:291` — `// The box's corners in ascending index, the order a whole-lattice scan` / `// meets them in, so the rings come out as they did.` — "As they did" refers to an implementation that is gone. The rule actually in force (a fixed corner order gives a fixed ring order) is buried under history. Lines 252-255 do the same with "(review-code 2026-10-15)". — Fix: state the rule (ascending corner index keeps ring order deterministic) and move the history to the commit. Cost: one edit (comment only).

[Q4] LOW `src/upkg/Properties.cpp:115` — `/// Everything else keeps its bytes -- see this file's header comment.` — `Properties.cpp` has no header comment; the text it means is in `Properties.h:5-13`. — Fix: "see Properties.h's header comment". Cost: one edit (comment only).

[Q4] LOW `src/upkg/Properties.cpp:247` — `// Step 4 runs for a Bool too: only its VALUE bytes are skipped, never` — Nothing nearby says which document "Step 4" belongs to. The header cites UTA-0003 SS 4.8, but the reader has to guess. — Fix: name the spec section the step belongs to. Cost: one edit (comment only).

[Q4] LOW `src/upkg/Geometry.cpp:157` — `// it is read signed and reinterpreted, as readPolys does for PanU.` — `readPolys` (:345-348) does the reverse: it reads PanU unsigned and reinterprets it as signed. A stranger following the pointer finds the opposite conversion. — Fix: drop the comparison, or say "the reverse of readPolys's PanU". Cost: one edit (comment only).

[Q1] LOW `src/upkg/Properties.cpp:206` — `if (rawName < 0 || static_cast<std::size_t>(rawName) >= package.names().size()) {` — The check "a name index read from object data is inside the name table" has eight copies:
- Two different functions both named `checkNameIndex`, with different signatures: `Class.cpp:37` and `Package.cpp:56`.
- Six inline copies: `Properties.cpp:138`, `:206`, `:238`, `Geometry.cpp:333`, `Script.cpp:250` and `Sound.cpp:34`.

Someone changing the rule or its message edits one `checkNameIndex` and misses the rest. — Fix: one shared upkg helper used at all eight sites. Cost: one edit — Searched: `docs/` for `checkNameIndex`: no hits. Searched: `src` for "which is outside the name table".

[Q1] LOW `src/upkg/Geometry.cpp:122` — `return std::unexpected(` / `std::move(value).error().withContext("in a Model's " + std::string(what)));` — `inModel` (:93) exists to attach this context. It is still hand-written four times: here, at :427-428 and at :469-477 (twice). SS 4.6's file-order attribution depends on these strings. A change to the wording made in `inModel` would miss all four. — Fix: route the four through `inModel`. Cost: one edit — Searched: `docs/` for `inModel`: no hits.

[Q1] LOW `src/upkg/Texture.cpp:175` — `if (reader.remaining() != 0) {` — The "export ended with bytes unread" refusal is copied five times: `Geometry.cpp:356`, `:483`, `Sound.cpp:71`, `Texture.cpp:175`, `:224`. `Class.cpp:207` has a variant with a different message. Alongside it, the one-line `malformed()` helper is defined identically in eight upkg files (Class, Geometry, Level, Package, Properties, Script, Sound, Texture) and twice in `src/ubake`. Changing what "malformed" carries, such as an offset, would take eight edits. — Fix: one shared upkg header for `malformed` and an `expectConsumed(reader, noun)` helper. Cost: one edit — Searched: `docs/` for `malformed(`: no hits. Searched: `src` for "the layout does not match the export" and "Error malformed(".

`[tool?: spelling]` `src/upkg/Geometry.cpp:368` — "would blames".

## Candidates dismissed
- **src/upkg/Script.cpp:131 skipExpression** — It is long, but it is one flat `switch`. Every case is a few lines, grouped and labelled by its operand shape. A stranger never needs more than one case in view.
- **src/upkg/Package.cpp:150 Package::open** — A straight run in file order: header, version check, three table bound checks, then names, imports and exports. Each block is labelled. There is no point where the thread is lost.
- **src/upkg/Geometry.cpp:364 readModel** — Linear, field by field in the file's own order, and each run is commented. Its duplication is reported above as a Q1, not as a size problem.
- **src/umap/Build.cpp:502 buildRoomMap** — Four labelled stages (copy the tables, zones to rooms, sample, trace), each tied to a spec step. The depth-5 nesting is the 3-D sampling loop, which reads directly.
- **src/upkg/Properties.cpp:201 readPropertiesAt** — The decode / fall back / rewind-and-skip passage (:285-305) is dense, but every turn carries a comment giving its reason.
- **src/upkg/Class.cpp:114 readClass**, **src/upkg/Level.cpp:100 readLevel**, **src/upkg/Geometry.cpp:281 readPolys** — All straight-line field transcriptions in file order. Length alone is not a finding.
- **dup:d83735c69e60** (Spark in `Texture.h:37-45`, `Fire.h:41-49`, `Bundle.h:211-218`) — The copies are identical, not diverged, and no recorded past fix patched more than one. They are forced by link boundaries: `Bundle.h:208-209` says ubundle keeps its own copy for UTA-0008 INV-10, and upkg may link only uta_core. One weaker point: `Texture.h` does not name its two twins.
- **Build.cpp:29 / :30** — "2^30 columns" is an exponent. "4096 columns; 131072 units" is correct arithmetic (2^24 = 4096², × 32), and it is explicitly scoped to the default spacing.
- **Build.cpp:36, :171, :383, :465, :474, :478; Build.h:43, :51, :82; umap/CMakeLists.txt:5, :6; Rooms.h:23; Properties.cpp:247** — These are spec step, SS or design-rule numbers, not counts. Properties.cpp:247 is reported separately above, because its step has no named document.
- **Build.cpp:519** ("design rule 2 forbids") — a rule number, not a count.
- **Build.cpp:537, :538; Geometry.cpp:168, :216, :219, :254; Geometry.h:162, :165; Level.cpp:68, :70** — Dated corpus measurements that explain why a layout was chosen. They are records, not current-state claims that can go stale.
- **Build.h:27; Rooms.h:121** — 64 is the engine's ceiling, quoted. "300 zones … onto 44" is correct (300 mod 256 = 44).
- **Geometry.cpp:303** ("about 104 bytes") — This matches the measured `sizeof(Polygon)` = 104 recorded under UTA-0216 in ROADMAP.md.
- **Geometry.cpp:375** ("version 62 appears nowhere") — corpus measurement.
- **Geometry.cpp:406** ("SS 10 records") — a section reference.
- **Package.cpp:26** ("3 indices + i32 outer") — correct: 3 + 4 = 7.
- **Properties.cpp:148** ("16 bits") — a format fact.

## Correctness leads
- `src/upkg/Class.cpp:31` — `fold` uses the locale-dependent `std::tolower`, while sibling folds were fixed for exactly that (UTA-0219, review-code 2026-10-15). — Searched: `src` for `tolower` and for fold definitions.
- `src/upkg/Texture.cpp:117` — `isModelledTextureClass` compares class names case-sensitively, though `Class.cpp:23-24` says the engine's class names are case-insensitive. A package spelling `texture` would be refused. — Searched: none beyond Class.cpp.
- `src/upkg/Properties.cpp:217`, `:274` and `src/upkg/Script.cpp:258` — The `"None"` terminator and the `"Vector"`/`"Rotator"` struct names are matched case-sensitively, under the same case-insensitivity rule. Whether any real package spells them otherwise is unknown to me. — Searched: none.

## Not checked
- Tests were not judged.
- The `roomForZone` search came back truncated at 100 rows and I did not page it. No claim above rests on it.
- I did not open the specs to confirm the SS numbers and step numbers cited in comments, apart from the docs searches named in the costs above.
- The ROADMAP was read only around UTA-0216.
- The 104-byte `sizeof(Polygon)` figure is taken from the ROADMAP's measurement, not computed by me.

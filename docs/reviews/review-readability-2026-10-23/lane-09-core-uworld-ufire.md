## Files read
- src/CMakeLists.txt — 13 / 13
- src/core/CMakeLists.txt — 32 / 32
- src/core/Error.cpp — 46 / 46
- src/core/Error.h — 137 / 137
- src/core/FileSystem.cpp — 606 / 606
- src/core/FileSystem.h — 120 / 120
- src/core/Jobs.cpp — 206 / 206
- src/core/Jobs.h — 161 / 161
- src/core/Log.cpp — 198 / 198
- src/core/Log.h — 170 / 170
- src/core/Md5.cpp — 130 / 130
- src/core/Md5.h — 45 / 45
- src/core/Sha256.cpp — 139 / 139
- src/core/Sha256.h — 43 / 43
- src/core/Timing.cpp — 65 / 65
- src/core/Timing.h — 77 / 77
- src/ufire/CMakeLists.txt — 18 / 18
- src/ufire/Fire.cpp — 254 / 254
- src/ufire/Fire.h — 115 / 115
- src/uworld/CMakeLists.txt — 20 / 20
- src/uworld/CollisionQuery.cpp — 114 / 114
- src/uworld/CollisionQuery.h — 60 / 60

(These counts are the last numbered line from native `Read`. Each file ends in a newline.)

## What I started with
- The review-lane agent prompt.
- The global `~/.claude/CLAUDE.md`.
- A SessionStart hook note that the Ants MCP was connected.
- Environment details: working directory `/home/ants/.cache/lane-neutral`, not a git repo, today's date.
- The user's email, as an account-context reminder.

There was no project `CLAUDE.md`, no memory index and no git snapshot.

## Summary
This code is the shared groundwork for a re-creation of Unreal Tournament 99:
- It works out where settings, logs and caches are kept, and reads and writes files safely, so a crash never leaves a half-written file.
- It writes log lines, runs work on several processor cores at once, makes the two standard file fingerprints (MD5 and SHA-256), and times how long each stage of a build takes.
- The `ufire` part replays UT99's animated fire textures.
- The `uworld` part answers "is this point inside a wall?" and "where does this line first hit a wall?" against a level's collision tree.

## Findings

[Q1] MEDIUM `src/core/FileSystem.cpp:290` — `if (status.type() != std::filesystem::file_type::regular)` — `MappedFile::open` and `readFile` (`:404-410`) open with the same file-type check, and the two copies have drifted apart:
- A FIFO (a named pipe) or a device file gets InvalidArgument from `MappedFile`. `readFile` only refuses directories, so the same input fails later at `file_size` as IoFailure.
- Only `MappedFile` got the non-blocking open from review-code 2026-10-15.
- The directory messages are worded differently (`"<p> is a directory, not a file"` vs `"is a directory, not a file: <p>"`).

`FileSystem.h:57` says MappedFile "Refuses as readFile does". A stranger who trusts that, or who adds a check to one function, will miss the other. ROADMAP.md:12904-12905 (UTA-0229) records an earlier round of this drift: MappedFile "lacks readFile's size cap and regular-file check". — Fix: one shared type check called by both functions, or at least correct the header line. Cost: unsure. UTA-0002-core-foundations.md names `readFile`, and I did not read whether it pins IoFailure for a non-regular file. Searched: docs/ for `MappedFile|readFile` (hits in UTA-0002, UTA-0003).

[Q1] MEDIUM `src/core/Sha256.cpp:39` — `void Sha256::add(std::span<const std::byte> bytes) noexcept {` — Its block-buffering body is a line-for-line copy of `Md5::update` (`Md5.cpp:41-64`). A past fix had to patch both: ROADMAP.md:12910 records "Md5.cpp:49 / Sha256.cpp:47 memcpy with a null span", fixed together under UTA-0229. `Md5.h:5` says it "mirrors" Sha256, but nothing in the Sha256 files points back to Md5. A stranger fixing Sha256's buffering would miss the MD5 copy. — Fix: extract the shared block buffer, generic over each hasher's `compress`, or at minimum a comment in Sha256.cpp naming its twin. Cost: one edit. No public name changes; UTA-0121-bot-path-seeds.md:477 pins Md5's `update`/`finish`, which an extraction keeps. Searched: docs for `Md5::update|Sha256::add` (none).
Partly confirms dup:e50110eedb39.

[Q4] MEDIUM `src/core/Log.cpp:14` — `// constinit: the design sanctions exactly one global, so that one is constructed at compile time` — This is false twice over:
- `src/urender/Device.cpp:17` defines a second global category, `LogCategory logRender("urender");`, and it is not `constinit`.
- `Log.h:102` says the one sanctioned global is the `Logger`, not a category.

A stranger adding a category for their part would wrongly think it breaks the design, or must be `constinit`. — Fix: give the real reason for `constinit` (it avoids static-initialisation-order problems) and drop "exactly one global". Cost: comment only. Searched: src for `LogCategory log`.

[Q1] LOW `src/core/Log.cpp:156` — `(void)std::fprintf(stderr, "[%.*s] %.*s: %.*s\n",` — The same line format appears again at `:189` in `fileSink`. The comment on `sanitised` (`:19-20`, "fixing one would half-fix it") records a fix that had to reach both sinks. The next change to the format, such as printing the record's `time` or `thread`, which neither sink prints yet, has to be made twice. — Fix: one shared function that formats a record as a line, called by both sinks. Cost: one edit. It renames nothing, and there are no ANCHORS.

[Q1] LOW `src/ufire/Fire.h:41` — `struct Spark {` — The same 8-byte layout exists three times: `ufire::Spark`, `upkg::Spark` (`src/upkg/Texture.h:37`) and `ubundle::FireSpark` (`src/ubundle/Bundle.h:210`). Only Bundle.h names a twin and says why it stays separate. Someone reading Fire.h or Texture.h does not know the other copies, or the code converting between them, exist. Changing how one copy reads a byte, or adding a field, would leave the others unchanged without any warning. — Fix: a comment on the Fire.h and Texture.h copies naming the other two and the link rule that keeps them apart. Cost: comment only.
Confirms dup:d83735c69e60.

[Q3] LOW `src/core/Md5.h:26` — `void update(std::span<const std::byte> bytes) noexcept;` — The header says Md5 is fed "as Sha256 is" and "mirrors" it, but Sha256's method is named `add`. A stranger switching hashers, or writing code that works with both, would expect the same method name. — Fix: rename it to `add`. Cost: pinned. UTA-0121-bot-path-seeds.md:477 spells it `update(...)`, and tests/unit/CoreMd5Test.cpp:67,75 call `update`.

[Q4] LOW `src/core/FileSystem.cpp:486` — `// That is INV-7, and it is why this is not written as early returns.` — The code below does use early returns (`:509`, `:514`); each one appends `discard()` by hand. Someone adding a new failure path would expect the structure to clean up the temporary file for them, but nothing does. — Fix: say instead that every failure return must append `discard()`. Cost: comment only.

[Q4] LOW `src/core/FileSystem.h:110` — `/// Absolute inputs, empty inputs, and paths resolving outside \`root\` --` — The interface comment for `resolveUnder` leaves out two kinds of refusal that the code also makes:
- the INV-15 name refusals (a colon, a trailing dot or space, and Windows device names such as `aux.txt`), which apply even on Linux;
- a path that passes through a dangling symlink (`:587-592`).

A stranger whose Linux file `con.txt` is refused will look in the wrong place. The reason is only in the .cpp, at `:163-172`. — Fix: add one line naming INV-15's refusals and the dangling-symlink refusal. Cost: comment only.

[Q4] LOW `src/core/FileSystem.h:35` — `/// Linux: $XDG_CACHE_HOME, else ~/.cache. Windows: %LOCALAPPDATA%\UT_Ants\cache.` — `cacheDirectory` and `logDirectory` (`:38`) leave out the "under ut-ants" that the config and data comments state. The code appends `ut-ants` for all four (`FileSystem.cpp:223,227`). Someone looking for the cache on disk would look in the wrong folder. — Fix: add "each under ut-ants" to both. Cost: comment only.

[Q5] LOW `src/core/Timing.cpp:32` — `if (std::this_thread::get_id() != owner_) return;` — `adopt` silently does nothing when called off the owner thread. Its doc at `Timing.h:41-43` does not say so, although `open`'s doc (`:55`) states the same rule. A stranger merging a worker's timings from inside a job would lose them with no error. tests/unit/TimingTest.cpp:105-107 deliberately exercises this case. — Fix: add "Does nothing on any thread but the owner's" to `adopt`'s doc. Cost: comment only.

[Q5] LOW `src/ufire/Fire.cpp:214` — `if (particle.age > 0) {` — Twirl particles test their age before counting it down. Drift (`:195`) and DriftGravity (`:204`) count down first. Nothing says whether this difference copies the original. A stranger tidying the three branches into one would change the animation and the baked stills. — Fix: a one-line comment on the Twirl branch saying it matches SurrealEngine's order, if that is true. Cost: comment only.

[Q4] LOW `src/core/Error.h:59` — `/// shape this header's own example teaches` — The example it refers to (`:68-71`) chains `withContext` but never calls `.message()`, so the "taught" shape `withContext(...).message()` is not in the header. A stranger looks for an example that is not there. — Fix: drop the "own example teaches" clause and keep the code line it quotes. Cost: comment only.

[Q4] LOW `src/uworld/CollisionQuery.h:3` — `// UTA-0158 moved these here from src/ubake/CollisionQuery.h, unchanged but for` — The header opens with two moves of history (UTA-0158, then UTA-0112). What the functions are for, and how a walk works (`:10-13`), only comes after that. The .cpp header (`:1-5`) repeats the history. A stranger has to read past the history to reach the rule. — Fix: lead with the purpose and the walk rule, and leave the history to the commit messages. Cost: comment only. Searched: src for `using uworld` (confirms ubake re-exports at src/ubake/CollisionQuery.h:14-20).

## Candidates dismissed
- `src/ufire/Fire.cpp:89:Fire::step` — The function is long, but it is one flat switch where each case stands on its own, followed by a separate particle loop. I could not name a line where a reader loses the thread. Its one real problem is the Twirl finding above.
- `src/core/FileSystem.cpp:279:MappedFile::open` — Each platform half reads straight through. The real problem is the drift from `readFile`, filed under Q1.
- `src/core/FileSystem.cpp:452:writeFileAtomically` — It runs straight through and is well commented. Its one problem is the false comment filed above.
- dup:e50110eedb39, the `finish()` half — The differences (byte order, digest size) are each algorithm's own definition, and `Md5.cpp:70-71` names them. The buffering half is the confirmed finding above.
- Every census_number candidate (`Error.h:9` "§ 8", `FileSystem.h:3` "rule 15", `Md5.h:1` "RFC 1321", `ufire/CMakeLists.txt:5` "rule 2", `Fire.cpp:249` "30 frames", `Fire.h:35` "255 steps", `Fire.h:54` "255 fades", `uworld/CMakeLists.txt:2` and `:11` "rule 4", `CollisionQuery.cpp:18` "432 headers", `CollisionQuery.h:5` "rule 2", `CollisionQuery.h:38` "SS 15") — Each is a section number, a rule number, a version number or a fixed constant, not a count that can go stale. `Fire.cpp:249`'s 30 matches the code (`return 30.0`). `Fire.h:35`'s 255 follows from `age` being a `uint8_t`. `uworld/CMakeLists.txt:11`'s "three" matches the three names in its own error message.

## Correctness leads
- `src/core/FileSystem.cpp:434` — `readFile` opens with plain `fopen` after its file-type check. A FIFO swapped in between those two steps would block the open; this is the gap that review-code 2026-10-15 closed in `MappedFile::open` only (`:326-329`). — Searched: none beyond the two functions.
- `src/ufire/Fire.cpp:195` — An Emit spark with `byteD == 0` releases Drift particles whose heat never falls, so they never die. They hold their slots against `sparksLimit`, and `PRIME_STEPS`' claim that particles live "at most 255 steps" (`Fire.h:34-36`) does not hold for them. It may be faithful to UT99. — Searched: none.

## Not checked
- `src/CMakeLists.txt:1-2` says the `add_subdirectory` order is the order the parts' roadmap items landed in. `ufire`, which arrived by UTA-0286 (UTA-0176 before that), sits before `upkg`. I could not check landing order or design.md § The parts without opening documents, which are outside this review. Open question: is `ufire`'s position an exception the comment should name?
- Whether UTA-0002 pins `readFile`'s error code for a non-regular file, which decides the cost of the first finding.
- `tools/ut-paths`' re-export of the uworld functions, which `CollisionQuery.h:5-6` claims. I searched `src/` only.
- Whether Fire.cpp's spark and particle behaviour matches SurrealEngine's. I had no copy of the original.

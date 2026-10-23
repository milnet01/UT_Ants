# close-findings: lane 09, core, uworld and ufire

Closes every finding in
[`lane-09-core-uworld-ufire.md`](lane-09-core-uworld-ufire.md) under
`UTA-0420`, and its two correctness leads under `UTA-0407`. Run 2026-10-23.
Roadmap lookup: both items exist, `UTA-0407` closed first in `d652b55`.

The fixes change no baked byte. Proof: `ut-bake` built at `71c0d0f` and
after both commits baked DM-ArcaneTemple, AS-Frigate, DM-Fetid and
CTF-Face from their map files; each bake exited 0 and wrote one bundle,
and each pair is byte-identical, bundle names included.

| # | finding | dim | sev | verified | disposition | files | was → now | must_agree |
|---|---|---|---|---|---|---|---|---|
| L1 | `readFile` opens with `fopen` after its type check | lead | — | yes; nothing between `status` and `fopen` refused a swapped-in FIFO | fixed in `d652b55` | `FileSystem.cpp`, `FileSystem.h`, `CoreFileSystemTest.cpp` | → `detail::openForReading` opens `O_NONBLOCK` and refuses a non-regular descriptor; red on a held-open FIFO, then green; dropping the `S_ISREG` refusal is killed | `MappedFile::open`'s gap comment: agrees |
| L2 | an Emit spark with `byteD` 0 makes Drift sparks that never cool | lead | — | yes, and faithful: SurrealEngine's `UpdateFrame` copies the spark's `HeatDecay` into a Drift particle, then subtracts it and tests heat > 0 | fixed (comment) in `d652b55` | `Fire.h` | `PRIME_STEPS`: "particles live at most 255 steps" → age-bound ones do; a Drift with decay 0 never dies, as in SurrealEngine | `FireStill.h` `FIRE_STILL_FRAMES`: agrees |
| 1 | `MappedFile::open` and `readFile` type checks drifted | Q1 | MEDIUM | yes | fixed | `FileSystem.cpp`, `FileSystem.h`, `CoreFileSystemTest.cpp` | → one `checkReadable` and one POSIX `openRegular` serve both; `readFile` now refuses a FIFO as InvalidArgument (was IoFailure); red then green, and dropping the non-regular line is killed | `FileSystem.h` "Refuses as readFile does": now true; UTA-0002 pins no code for a non-regular file: agrees |
| 2 | `Sha256::add` copies `Md5::update`'s buffering | Q1 | MEDIUM | yes | fixed | `HashBlocks.h` (new), `Md5.*`, `Sha256.*` | → `detail::HashBlocks` holds the buffer, and both hashers pass in `compress` | RFC and FIPS vectors in `CoreMd5Test`, `CoreSha256Test`: pass; bundle names unchanged |
| 3 | `Log.cpp` "exactly one global" comment is false | Q4 | MEDIUM | yes; `urender/Device.cpp` defines `logRender` | fixed | `Log.cpp` | → `constinit` keeps it out of static initialisation order | `Log.h` `Logger` "one global": agrees |
| 4 | the log line format is written in both sinks | Q1 | LOW | yes | fixed | `Log.cpp` | → `printLine`, called by both | `CoreLogTest`: pass |
| 5 | the spark layout exists three times, unnamed twins | Q1 | LOW | yes | fixed | `Fire.h`, `upkg/Texture.h` | → each names the other two and why they stay apart | `Bundle.h` `FireSpark` comment: agrees |
| 6 | `Md5::update` vs `Sha256::add` | Q3 | LOW | yes | fixed | `Md5.*`, `CoreMd5Test.cpp`, `UTA-0121-bot-path-seeds.md` | `update` → `add`; the spec's sketch amended (gate cancelled here: no gate) | tree search for `.update(` on an Md5: none left |
| 7 | `writeFileAtomically` "not written as early returns" | Q4 | LOW | yes | fixed | `FileSystem.cpp` | → every failure return must append `discard()` | n/a |
| 8 | `resolveUnder` doc omits INV-15 and dangling-symlink refusals | Q4 | LOW | yes | fixed | `FileSystem.h` | → one line names both | UTA-0002 §4.4 declaration comment: agrees |
| 9 | cache and log directory docs omit "under ut-ants" | Q4 | LOW | yes; `xdgDirectory` appends it for all | fixed | `FileSystem.h` | → "each under ut-ants" | n/a |
| 10 | `Timing::adopt` silently ignores other threads | Q5 | LOW | yes | fixed | `Timing.h` | → says so, as `open` does | `TimingTest` off-owner case: agrees |
| 11 | Twirl tests age before counting it down | Q5 | LOW | yes; SurrealEngine does the same | fixed | `Fire.cpp` | → a comment names the order and its source | n/a |
| 12 | `Error.h` "this header's own example teaches" | Q4 | LOW | yes | fixed | `Error.h` | → the clause dropped, the code line kept | n/a |
| 13 | `CollisionQuery.h` leads with history | Q4 | LOW | yes | fixed | `uworld/CollisionQuery.h`, `.cpp` | → purpose and walk rule first; the move history left to git | UTA-0112 names `src/ubake/CollisionQuery.*`: frozen, a past move |

## Run-level

- cited_by: tree search outside `docs/reviews/` for `Md5::update`,
  `.update(` on an Md5, and both old directory messages: only the
  UTA-0121 sketch, amended. ROADMAP and CHANGELOG hits are dated records.
- swept: every `must_agree` cell above holds its verdict. Code-pair list:
  absent in this project. Asked of each fix: contradicts, breaks, stales.
- collateral: none.
- surfaced: none.
- out_of_scope: `src/CMakeLists.txt` claimed the parts are listed in the
  order they landed; `ufire` landed after `uworld` (git: 2026-10-05 against
  2026-09-14). Fixed: the comment says so. No `if(TARGET)` or cross-target
  property read depends on the order.
- falsified: none.

## Files read
- src/unav/Build.cpp — 416 / 416
- src/unav/Build.h — 42 / 42
- src/unav/CMakeLists.txt — 49 / 49
- src/unav/Graphs.cpp — 74 / 74
- src/unav/Graphs.h — 152 / 152
- src/unav/Reach.h — 71 / 71
- tools/ut-paths/CMakeLists.txt — 13 / 13
- tools/ut-paths/Cli.cpp — 395 / 395
- tools/ut-paths/Cli.h — 24 / 24
- tools/ut-paths/Seeds.cpp — 730 / 730
- tools/ut-paths/Seeds.h — 73 / 73
- tools/ut-paths/Trace.h — 34 / 34
- tools/ut-paths/Walkable.cpp — 241 / 241
- tools/ut-paths/Walkable.h — 64 / 64
- tools/ut-paths/main.cpp — 16 / 16

I read each file with native `Read`. Each count is the last line number `Read` showed. Every file ends with a newline, so a plain line count on disk will be one less.

## What I started with
- The global `~/.claude/CLAUDE.md`, with its rules 0–20.
- The review-lane agent file (this lane's instructions).
- A user-email context block.
- A SessionStart hook note saying the Ants MCP is connected.
- The environment block: working directory `/home/ants/.cache/lane-neutral`, date 2026-10-23.
- No project `CLAUDE.md`, no memory index, no git snapshot.

## Summary
The `unav` library reads one Unreal Tournament map file and builds two maps of how its things connect. The first is where a player can walk from one navigation point to the next. The second is which object sets off which other object when triggered. The `ut-paths` tool loads a map, works out where a bot-sized body can stand and walk, and searches for a walking route from the player start to each level exit. Where it finds one, it suggests points along it. It writes one JSON file per map, plus a summary of every map it handled. The list of maps comes from a separate census file.

## Findings

[Q1] MEDIUM `tools/ut-paths/Seeds.cpp:94` — `std::set<std::string> eventsOf(const ActorPlacement& actor, const ActorClass& actorClass) {`
This is a second answer to "which actor fires which `Tag`", and it disagrees with `unav::buildWiringGraph` (`src/unav/Build.cpp:291`) in three ways:
- Seeds.cpp folds case (line 98), where Build.cpp matches exactly (Build.cpp:374).
- Seeds.cpp reads Event, BumpEvent, PlayerBumpEvent, FirstHatePlayerEvent and OutEvents, where Build.cpp reads only `Event`.
- Seeds.cpp falls back to the class's default values, where Build.cpp reads the actor's own properties only (Build.cpp:318).

Each copy cites its own spec (UTA-0121 SS 3 decision 10 against UTA-0006 SS 4.3/4.4), and neither mentions the other. Someone fixing wiring in one place, for example adding OutEvents to the wiring graph, or swapping the teleporter dead-end test at Seeds.cpp:516–537 over to `unav::firedBy`, would change only one copy. The swap would quietly change which teleporters count as dead ends.
— Fix: put a comment on each copy naming the other and the inputs on which they differ.
Cost: one edit (comment only). Merging the two would be pinned: UTA-0172 line 474 lists any change to `buildWiringGraph`'s counts as a non-goal.
Searched: `(?i)playerbumpevent|outevents` and `(?i)bumpevent|"event"|fold\(record\.name\)` across *.cpp/*.h outside tests/. The only other reader is ut-dump/Cli.cpp, which prints the fields and does not match them. I also searched UTA-0121 for `wiring|buildWiringGraph|eventsOf` and found nothing.

[Q5] MEDIUM `tools/ut-paths/Seeds.cpp:543` — `&& position[edge.to] < nav.nodes.size() && !((edge.reachFlags & 32) != 0 && deadEnd[edge.from]))`
The bare `32` is `unav::R_SPECIAL`, and `unav/Reach.h` is already included at line 14. Nothing here says that a teleporter's edge is the one carrying the special flag. Someone searching for `R_SPECIAL` to learn how teleporter or lift edges are handled will not find this line. Someone renumbering or splitting the flag (Reach.h) would miss it.
— Fix: use `unav::R_SPECIAL`, with a short comment saying it marks a teleporter's edge.
Cost: pinned. `scripts/mutations/ut-paths.py` (INV-16) quotes `!((edge.reachFlags & 32) != 0 && deadEnd[edge.from]))` word for word, and INV-11 quotes `walkable(edge) && edge.pruned == 0 &&` from the same condition.

[Q5] LOW `tools/ut-paths/Seeds.cpp:509` — `std::vector<std::size_t> position(nav.nodes.size(), nav.nodes.size());`
`nav.nodes.size()` is an unnamed "this node was dropped" marker. At line 542, `position[edge.from] < nav.nodes.size()` reads like a bounds check, but it is really "this node was kept". Someone tidying it as a redundant bounds test would let dropped nodes' edges through. The comment at lines 506–508 explains the drop but not the marker.
— Fix: name the marker (for example `DROPPED`) or use `std::optional` positions.
Cost: pinned. INV-11's anchor quotes `walkable(edge) && edge.pruned == 0 &&` from line 542.

[Q2] LOW `tools/ut-paths/Seeds.cpp:633` — `const auto nearSlope = [&](std::uint32_t s) {`
`propose` defines four lambdas in the middle of the function and fills a cache (`moverOrSlope`) the first time it is needed. The reader loses the thread at line 678, `chain(hops, aroundSlopes(path, sources, goal), proposal.nodes)`. To see that the slope detour runs only for `Found` routes and quietly falls back to the original path, they have to go back through lines 630–653. The local `nearSlope` (takes a spot index) also shadows `Hops::nearSlope` (takes a segment) defined above it, so a reader jumping to the definition lands on the wrong one.
— Fix: lift `aroundSlopes` and the `why` lambda into named file-scope helpers, and rename the local `nearSlope` to say it tests one spot.
Cost: pinned. The mutation catalog quotes `        if (std::none_of(path.begin(), path.end(), nearSlope)) return path;` and `return teleporter ? NoRoute::Teleporter : mover ? ...` exactly, indentation included. UTA-0121 spec line 401 names only `propose`'s signature, which the fix does not change.

[Q5] LOW `src/unav/Build.cpp:337` — `if (!carriesTag) {`
When an actor has more than one `Tag` record, the first one wins, and no comment says why the first or whether a second is expected at all. Someone reworking this loop could easily switch it to last-wins.
— Fix: add a one-line comment giving the rule and its reason.
Cost: one edit (comment only).

[Q4] LOW `tools/ut-paths/Cli.h:17` — `/// \`args\` excludes the program name. Returns the exit code: 0 when every map was written or skipped, 1 when any was refused, 2 when the arguments were wrong.`
`runCli` also returns 1 when the census does not read (Cli.cpp:354) and when the summary is not written (Cli.cpp:387). Someone scripting on the exit code would read 1 as "a map was refused".
— Fix: add those two cases to the doc line.
Cost: one edit (comment only).

[Q1] LOW `tools/ut-paths/Cli.cpp:41` — `"       ut-paths --help\n"`
The parser also accepts `-h` (line 78), but the help text does not list it. These are two hand-kept lists of one set of options, and they no longer match.
— Fix: list `-h` in the usage text.
Cost: one edit. Searched `ut-paths --help|usage: ut-paths` across the whole tree: nothing outside Cli.cpp quotes the text.

[Q4] LOW `tools/ut-paths/Seeds.cpp:301` — `// The navigation point or node already proposed nearest \`p\`, within 50.`
This comment (and the one at line 333) repeats `TOO_NEAR`'s value instead of naming the constant, so changing `TOO_NEAR` leaves both comments wrong. `Walkable.h:59` does the same with "within 64", which is `PLACE_ACROSS`, defined in a different file (Walkable.cpp:28).
— Fix: name the constant in each comment instead of its value.
Cost: one edit (comment only).

[Q3] LOW `tools/ut-paths/Cli.cpp:186` — `std::string letters(std::string_view name) {`
It keeps digits as well as letters (line 190). A reader trusting the name would expect `MH-Foo2` and `MH-Foo3` to compare equal.
— Fix: rename it to say it keeps letters and digits (for example `alphanumericFolded`).
Cost: one edit. Its only callers are Cli.cpp lines 206 and 221. Nothing in the mutation catalog's ANCHORS mentions Cli.cpp.

## Candidates dismissed
- `src/unav/Build.cpp:291` buildWiringGraph (size): three passes (collect, index by tag, match), each opening with its own comment. I found nowhere a reader loses the thread. Its one unexplained branch is filed separately above (Build.cpp:337).
- `tools/ut-paths/Seeds.cpp:464` sceneOf (size): the work is in stages, each with a heading comment. Its real problems are line-level and filed above (lines 509 and 543).
- `tools/ut-paths/Seeds.cpp:579` propose (size): filed as Q2 at line 633, the place the thread is lost, not for its length.
- `tools/ut-paths/Walkable.cpp:118` walkGraph (size): two parallel phases, each with a comment explaining why the order stays the same as a single-threaded run. Readable as written.
- `dup:94ee2d1a3489`: the `takeValue` lambda in ut-bake/Cli.cpp and ut-paths/Cli.cpp. The two copies behave the same and differ only in the tool name printed. A search for `is given twice` found no third copy of this lambda: apps/ut-ants/Cli.cpp writes its messages inline, and urecipe is unrelated. No comment records a fix that had to touch both, so by the brief's rule it is not a finding.
- `src/unav/Build.cpp:152` "6 keeps": this is "SS 6", a section number, not a count.
- `src/unav/CMakeLists.txt:5`, `:6`, `:7` ("2 says", "6 makes", "2 through this"): these are docs/design.md rule numbers, not counts.
- `tools/ut-paths/CMakeLists.txt:1` "2 permits": also design.md rule 2.
- `tools/ut-paths/Seeds.cpp:237` "2 misses": this is `SAMPLE = 2` and `REACH = RADIUS + 2`, both defined two lines below the comment, so a reader sees the constants at once.

Noticed, and not filed: `HEIGHTS` is defined identically at Seeds.cpp:64 and Walkable.cpp:25. These are two identical copies with no record of a fix touching both, so they are below the brief's bar.

## Correctness leads
None. The things I suspected turned out to be intended:
- Build.cpp:70: `importPackageName` returns an empty string when an import's outer chain reaches a non-import. That makes the actor "not a node" with no error.
- Cli.cpp:359–360: a failure to create the `--out` directory only warns, and each map then fails at its own write.

Searched: none beyond the files themselves.

## Not checked
- I did not check that the spec references (SS numbers, INV numbers, UTA ids) in comments point at the sections they name. Only INV-10 (first PlayerStart) was confirmed, by search.
- I read only lines 90–125 of tools/ut-bake/Cli.cpp, enough to compare the duplicate pair.
- I read only lines 255–280 of UTA-0172, plus search hits elsewhere in it.
- I did not judge the maths in `placed`, `meets` or `nearSlope`.

# UTA-0057 — `upkg`: the rest of `Level`, and the ReachSpec path graph

**Status:** accepted (2026-09-06).
**Kind:** implement.
**Source:** ROADMAP UTA-0057 (user-decision-2026-09-05; narrowed to this half
by consumer-request-2026-09-06).
**Blocked by:** UTA-0003 for the container. UTA-0004 for the actor array's
*contract* — but not its code: `readLevel` was declared there and never
built, so this item is the first to implement it and inherits INV-9's test
obligation with it (§ 4.2). UTA-0005 for class ancestry, which § 4.6 needs to
identify a navigation point at all.
**Blocker for:** UTA-0006 — `unav` reads a node's `Paths` entries and cannot
resolve one without the array this item returns. UTA-0012's third query is
blocked for the same reason.

Read the part of a level record that comes after the actor list, so that the
connections between the map's navigation points can be read without running
the game.

## 1. Goal

Given a `Package` opened by UTA-0003, `readLevel` consumes a `Level` export
exactly and returns, alongside the actor references UTA-0004 already
specifies, the level's array of reach specs — the directed connections
between navigation points, each carrying the collision size it was built
for.

UTA-0004 declared `readLevel` and its return type and did not ship them: its
`docs/specs/UTA-0004-typed-level-content.md` § 4.9 records that the actor
array and the `FURL` after it decode cleanly, that "a substantial remainder
follows", and that deriving the remainder is this item's. So this item both derives that remainder and ships the reader
UTA-0004 declared.

The layout of the remainder is unknown when this is written. That is the
work, and § 4.3 is what says when it is done.

## 2. Problem

A UT99 map stores where a bot may walk as a graph, built by the editor and
saved into the level. `unav` (UTA-0006) exists to extract that graph. It
cannot, and the reason is narrow.

Each `NavigationPoint` actor carries a fixed array `Paths[16]` of integers,
and `readProperties` already returns them — they arrive as ordinary tagged
properties on the actor, like any other. But an integer is not an edge. The
integers are understood to be indices into a single level-wide array of
directed reach specs, and that array is in the part of the `Level` export
nobody here has described. Without it, `unav` can list a map's navigation
points and can say nothing about which connects to which.

### 2.1 What is known, and what is only believed

**Verified by UTA-0004, and not re-derived here.** After a `Level` export's
property list come `i32 Num`, `i32 Max`, `Num` object references, and then an
`FURL` — four compact-index-prefixed strings, a string array of options, and
`i32` port and `i32` valid. That decodes cleanly across the install's maps.
UTA-0004 § 4.9 owns both claims and this spec does not restate them.

**Not described anywhere in this project.** What follows the `FURL` — tens of
kilobytes on a stock map. `ReachSpecs` is expected to be in it.

**Believed, not verified, and the distinction is load-bearing.** That
`NavigationPoint.Paths[16]` holds indices into a level-wide reach-spec array,
and that a spec carries a start node, an end node and the collision radius
and height it was built for. This comes from the Unreal Engine 1 class
structure as the community documents it. **No one on this project has read a
reach spec**, and UTA-0004 § 4.5 records what stating an unverified layout
costs. § 4.6 is how this item settles it rather than assuming it.

### 2.2 The circumstantial evidence, measured here

The Monster Hunt server work on this machine tested the indexing claim
without reading the array, by a prediction that follows from it: if an index
names one directed spec, it appears at most once across a level's `Paths`
arrays and at most once across its `upstreamPaths` arrays.

**Its write-up lives outside this repository and has already moved once**
(`UT_MonsterHunt/ut-map-deps/paths-index-evidence.md` under
`/mnt/Games/Scripts/Linux/` as of 2026-09-06). So it is cited as provenance
only: every figure this section relies on is reproduced below with the
command that produces it, and nothing here needs that file to be found.

Re-measured here on 2026-09-06 over that project's T3D exports, rather than
taken from the report:

```sh
# per map, over <corpus>/<map>.t3d -- the corpus was at
# /mnt/Games/Scripts/Linux/UT_MonsterHunt/work/t3d/ on 2026-09-06 and has
# already moved once, so re-locate it rather than trusting this path.
grep -oE '^[[:space:]]*Paths\([0-9]+\)=-?[0-9]+'         "$f" | grep -oE '=-?[0-9]+$' | tr -d '='
grep -oE '^[[:space:]]*upstreamPaths\([0-9]+\)=-?[0-9]+' "$f" | grep -oE '=-?[0-9]+$' | tr -d '='
# the cap columns -- the anchor is load-bearing, see below
grep -cE '^[[:space:]]*Paths\(15\)='         "$f"
grep -cE '^[[:space:]]*upstreamPaths\(15\)=' "$f"
```

| map | `Paths` | distinct | `upstreamPaths` | distinct | max index | residue | at cap (P/u) |
|---|---|---|---|---|---|---|---|
| MH-AncientCavesTorus | 1202 | 1202 | 1274 | 1274 | 2816 | 72 | 0 / 0 |
| MH-Village1 | 730 | 730 | 619 | 619 | 1149 | 111 | 0 / 1 |
| MH-Dust2-BP | 6579 | 6579 | 6579 | 6579 | 14322 | 0 | 7 / 4 |

The last two columns were measured with the same run: the residue is the
difference between the two distinct counts, and the cap columns count actors
filling the sixteenth slot.

**The `^[[:space:]]*` anchor on those two commands is load-bearing.**
Unanchored, `Paths(15)=` is a substring of `upstreamPaths(15)=` and of every
other array whose name ends in `Paths`, so it counts them all: measured on
this corpus it returns 155, 68 and 298 against the anchored 0, 0 and 7.
Anyone re-deriving this table with the unanchored form will conclude the
table is wrong.

Three things this settles, and one it does not.

**Uniqueness holds without exception.** No index repeats within either array
on any map measured. A per-node value would not behave that way. This is the
strongest support the claim has.

**The array is larger than the set referenced.** MH-Dust2-BP names 6579
distinct indices with a maximum of 14322, so a reader must not assume the
indices are a dense `0..n`. Pruned specs still occupying slots is the obvious
explanation and is a guess.

**The 16-slot cap does not explain the residue.** The obvious reading is that
busy nodes overflow their fixed 16 slots. It does not survive the last two
columns: no map's capped-node count is remotely near its residue.
MH-AncientCavesTorus carries a residue of 72 with **no node at either cap**,
which alone is fatal — truncation cannot drop entries where nothing was
truncated. MH-Village1 has the largest residue at 111 and one capped node.
MH-Dust2-BP is the only map where the cap really binds and the only one with
no residue at all, which is the opposite of what the hypothesis predicts.
Recorded so this item does not spend time on a hypothesis already eliminated.

**What it does not settle** is anything about the array's position, its
element size or its field order — which is the whole of § 4.4 and § 4.5.

## 3. Scope decisions (agreed with the user)

### 3.1 This item is the `Level` half only — user, 2026-09-06

UTA-0057 carried both `Model` and the `Level` tail until 2026-09-06, when
the user adopted a request from the Monster Hunt server work to split them
and take this half first. `Model` is now UTA-0069.

The reason is what each unblocks. The `Level` tail is the only source of the
reach-spec array, and UTA-0006 is blocked on it outright. `Model` gives
`ubake` the BSP tables and gives that consumer real surface flags instead of
brush flags, which saves it a server boot rather than unblocking it.

### 3.2 The remaining calls are mine, with reasons

1. **`readLevel` returns the reach specs as the file stores them, not a
   graph.** Positional order preserved, stored indices untouched, no
   compaction and no renumbering. Three reasons. UTA-0006 owns the graph
   types and the queries over them, and a graph built here would be a second
   vocabulary for the same thing. § 2.2 shows the indices are sparse, so
   compacting would silently invalidate every `Paths` value in the level.
   And a graph has to decide what an unresolvable index means, which is a
   policy its consumer owns and this reader cannot see.

2. **The `FURL` is consumed and not returned.** UTA-0004 § 4.9 took that
   position because nothing in the roadmap reads a level's URL, and nothing
   has changed. It must still be consumed, because § 4.3 admits no partial
   read.

3. **Anything else found in the tail is consumed and not returned unless a
   roadmap item wants it.** The tail is tens of kilobytes and this item's
   consumers want one array out of it. Returning the rest would fix a shape
   for data nobody has a use for, and UTA-0004 § 4.1's argument applies: a
   vocabulary invented twice is two vocabularies.

4. **A level whose reach specs cannot be interpreted is still a refusal.**
   Not a `Level` with an empty spec array. § 4.3 is the acceptance and it
   does not bend for this array; an empty array must mean the file said so.

## 4. Design

### 4.1 Layout and the build

`src/upkg/Level.h` and `.cpp`, the home UTA-0004 § 4.1's layout table already
names for this reader. They do not exist yet:

```sh
ls src/upkg/            # ByteReader Class Geometry Package Properties Script Sound Texture
```

The link closure does not move — `uta_upkg` links `uta_core` and nothing
else, which `src/upkg/CMakeLists.txt` asserts at configure time for
UTA-0003's INV-13.

The entry point is the one UTA-0004 declared, unchanged:

```cpp
[[nodiscard]] Result<Level> readLevel(const Package&, const ExportEntry&);
```

`Level` gains one member, and conditionally a second — § 4.5's derivation
decides. The two UTA-0004 declared keep their meaning and their contract,
which is INV-9 there and is not restated here:

```cpp
struct ReachSpec {
    // Field set believed from the engine's class structure; ORDER and widths
    // are what SS 4.5 derives. The implementation names these members.
    // Its start and end node fields are returned AS STORED -- see below.
};

struct Level {
    std::vector<ObjectReference> actors;      // non-null only -- UTA-0004 INV-9
    std::uint32_t rawSlotCount = 0;           // including nulls -- UTA-0004 INV-9
    std::vector<ReachSpec> reachSpecs;        // file order, stored indexing

    // CONDITIONAL, and SS 4.5's derivation decides whether it exists: present
    // only if a ReachSpec names its nodes by raw actor-slot index rather than
    // by object reference. Absent otherwise. See below.
    // std::vector<std::uint32_t> actorSlotOfIndex;
};
```

**A spec's node fields are returned as the file stores them, and if they are
raw actor-slot indices the returned type must be able to express that.**
`actors` drops the null slots and keeps only their count, so a raw slot index
cannot be joined against it — the mapping from slot to returned position is
gone. Which form the file uses is not known here and § 4.5 derives it. If it
is an object reference, nothing more is needed. **If it is a slot index, the
reader returns the slot-to-actor mapping alongside, not merely
`rawSlotCount`.** Settling this silently is how UTA-0006 ends up with node
identities it cannot join to anything, and UTA-0006 binds to this type.

### 4.2 What this item inherits and must not restate

Three contracts already govern this reader and are cited rather than
repeated, per `spec-format.md` § 5.2:

- **Exact consumption** — UTA-0004 § 4.3 and its INV-1. A reader that does
  not end at `serialOffset + serialSize` returns `MalformedData` and never a
  partial result.
- **The actor array** — UTA-0004 INV-9. **Its contract is inherited; its
  test is not yet written.** `readLevel` never shipped, so
  `tests/unit/PackageContentTest.cpp` carries no case for it today (measured
  2026-09-06: `grep -c Level` over that file returns 0). This item is the
  first to implement the actor array, so INV-9's fixture case is owed here —
  § 7 tier 1 and § 10 carry it.
- **The supported package version range** — UTA-0003, inherited through
  `Package::open`, which refuses anything outside 61–69 before a table is
  read. This item adds no version gate.

### 4.3 The derivation method, and when it is done

The same method UTA-0004 was researched with, and the same acceptance.

The layout is right when `readLevel` consumes every `Level` export in the
reference install exactly. That check is total — it fires on every field
without anyone predicting which will be wrong — and it runs against content
this project did not write. It is not a proof of correctness: a reader can
consume the right number of bytes and assign them to the wrong fields. § 4.6
narrows that gap for the two fields the graph rests on — a spec's start and
end nodes — and closes it for nothing else. **A `ReachSpec`'s collision
radius and height are returned, are bound to by UTA-0006, and are checked by
nothing here**; § 10 grades them, and finding an independent source for them
is left to UTA-0006 rather than invented in this item.

### 4.4 The tail, and what a reader may assume about it

The reader walks the tail from the end of the `FURL` to the end of the
export. It may not assume the reach-spec array is at a fixed offset, nor
that it is the only array there; § 2.1 says only that tens of kilobytes
follow, and a derivation that hard-codes a position passes the install and
breaks on the first map built differently.

What it may assume is what UTA-0003 already guarantees: the export's byte
range is validated, so the walk has a known end, and overrunning it is a
refusal rather than undefined behaviour.

### 4.5 The reach-spec array

Its element layout is what the derivation produces. This spec deliberately
states no field order — UTA-0004 § 4.5 is the precedent, and the reason is
that a stated-but-unverified layout reads as verified to everyone
downstream.

What the implementation must record when it lands is the layout it derived,
in this section, with the evidence that fixed it. That is a Step 8 fold-back
and § 12's log is where it is announced.

### 4.6 Settling the indexing claim, rather than assuming it

This is the item's first acceptance step and it is not optional. § 2.2's
evidence is circumstantial; the array itself is the thing that settles it.

**Which actors, and which entries.** Neither is obvious and both were
measured on 2026-09-06 rather than assumed.

*Which actors:* **no export of literal class `NavigationPoint` carries a
`Paths` entry at all** — every one is a subclass. So the filter is *every
export whose class descends from `NavigationPoint`*, which needs UTA-0005's
ancestry walk. An exact name match resolves nothing.

**A name list is not a fallback, because the class set does not close.**
Measured over the whole Monster Hunt corpus on 2026-09-06 (643 T3D exports,
re-derived here rather than taken from the survey): 73 distinct classes carry
a `Paths` entry, 35 of them in exactly one map, and the distinct-class count
was still rising in the last tenth of the corpus — 72 after 580 exports, 73
after 643. A corpus still introducing classes at that point will introduce
more with the next map, so no finite list closes and ancestry is the only
approach that survives.

Two corroborations from the same run. Every `Paths`-carrying class also
exports `nextNavigationPoint`, so nothing in the corpus carries `Paths`
without being a navigation point — corpus evidence, not proof of descent,
which needs the `.u` export tables' `SuperField` links and is what UTA-0005
walks. And the survey independently hit the substring trap this section warns
about above, over-counting by millions of lines.

*Which entries:* a slot is **empty when `readProperties` returns no property
for that `arrayIndex`**. `Paths[16]` is a static array carried in the actor's
tagged property list, and `src/upkg/Properties.h`'s `Property` carries an
`arrayIndex` member — so per-slot presence is already observable, and an
unused slot is an absent property rather than a stored sentinel. **This is
not § 4.5's to derive**: that section derives the reach-spec array in the
`Level` tail, which contains nothing about an actor's property list.

The T3D exports agree as far as they can — they emit only the slots in use,
and carry no negative `Paths` value on any of the three maps — but they
cannot rule out a sentinel, because an exporter that skips unused slots and
one that never had them look identical. **So the first read of a real actor
settles it**: if every `NavigationPoint` descendant returns sixteen
properties for `Paths`, a sentinel exists and this rule is wrong. Record
which it was in the fold-back; until then, drop no entry on a guess.

Then, for every such actor, resolve each non-empty `Paths` entry against the
returned array. This is answered over the whole reference install, not over a
sample: it is the same population INV-2's test walks, and answering it on one
map would leave the shipped assertion resting on a narrower check than the
one that settled it. Two questions, and both must be answered before this
item is called done:

1. **Is every non-empty `Paths` value a valid index into the array?** If any
   is out of range, the claim is false or the layout is wrong, and the two
   are distinguishable by whether the array's own length is plausible.
2. **Does the spec at that index have the listing node as its start?** This
   is the check that separates a reader consuming the right bytes from one
   assigning them to the right fields, which § 4.3 cannot do.

**One disposition, so an out-of-range value never means two things.** Until
§ 4.6 is answered, an out-of-range value fails tier 3 and is chased as a
layout defect — the array's own length says whether that is plausible. If
§ 4.6 answers *no*, the indexing claim is false: INV-2 and INV-3 are then
**withdrawn in the fold-back**, not left standing and failing, tier 3 drops
their assertions, this item still ships the layout — the derivation is worth
having either way — and UTA-0006's blocked-by is what changes. Recording that
in the fold-back is what stops the item quietly widening.

### 4.7 Fixtures

**`tests/support/UnrealPackageBuilder` emits no `Level` export at all**
today — measured 2026-09-06, `grep -n Level` over its header and source
returns nothing. UTA-0004 § 4.10 describes what a `Level` fixture would not
cover, not one that exists. So this item writes the `Level` fixture from
scratch rather than extending one: an actor array interleaving null and
non-null slots (UTA-0004's INV-9, whose case is owed here per § 4.2), an
`FURL`, and a tail carrying a small reach-spec array with known contents at
known sparse indices, so the unit tier can exercise the reader without the
reference install. The
builder is the only way to construct the refusal cases in § 6: real content
does not supply a truncated array on demand.

## 5. Invariants

- **INV-1** — `readLevel` returns the level's reach specs in the file's own
  order, with the file's own indexing preserved: the spec at returned
  position `i` is the spec the file stores at index `i`, and no entry is
  dropped, reordered or renumbered.
  *Test:* `tests/unit/PackageContentTest.cpp` builds a level whose reach-spec
  array has distinguishable entries at known sparse indices, and asserts each
  returned position holds the entry the fixture put there.
  *Breaks when:* the reader compacts the array, or skips entries it judges
  empty or pruned — at which point every `Paths` value in every real map
  points at the wrong spec, and nothing in § 4.3 notices because the byte
  count is unchanged.

**INV-2 and INV-3 hold only if § 4.6 answers yes.** They state the indexing
claim as a contract, and § 4.6 is what settles whether that claim is true. If
it answers no, both are withdrawn in the fold-back rather than left failing.

- **INV-2** — Every non-empty `Paths` entry on an export descending from
  `NavigationPoint`, in the reference install, resolves to a position within
  the returned array.
  *Test:* `tests/real/RealInstallTest.cpp` walks each map's navigation points
  and asserts every non-empty `Paths` value is in range for that level's
  returned array, and fails outright if the number of entries it resolved is
  zero (§ 7 tier 3).
  *Breaks when:* the reader consumes the export exactly and still partitions
  the array wrongly — reading two records as one, say — which halves its
  length and puts the largest indices out of range. A wrong element size that
  also changes the byte total is caught earlier and more cheaply by UTA-0004's
  INV-1; this invariant exists for the case that one cannot see, which § 2.2
  predicts is reachable since the maximum index measured is more than twice
  the count of distinct ones.

- **INV-3** — A spec named by node A's `Paths` has A as its start node.
  *Test:* `tests/real/RealInstallTest.cpp` resolves each non-empty `Paths`
  entry and asserts the spec's start resolves to the actor that listed it.
  *Breaks when:* the start and end fields are transposed, or a neighbouring
  integer field is read as one of them — a reader that consumes its export
  exactly and is still wrong, which is the case § 4.3 explicitly cannot
  catch.

- **INV-4** — `readLevel` refuses a level whose tail it cannot interpret,
  and never returns a `Level` whose `reachSpecs` is empty because the reader
  gave up. An empty array is returned only when the file states one.
  *Test:* `tests/unit/PackageContentTest.cpp` builds a level whose reach-spec
  count exceeds the bytes remaining in the export, and asserts `readLevel`
  returns `MalformedData`; and builds a level stating a zero-length array and
  asserts that one succeeds with an empty array.
  *Breaks when:* the reader treats a short or unparseable array as "no
  paths" **and seeks to the export's end**, which is the shape UTA-0004's
  INV-1 cannot see — it consumes exactly and returns a level that looks
  unpathed. A reader that simply stops early is caught there instead. The
  cost of the invisible shape is that a map with a broken tail is
  indistinguishable from a map a designer never pathed, and `unav` reports an
  unreachable level as an empty one.

- **INV-5** — The reach specs are returned as the file's own records. No
  member of `Level` holds a resolved graph, an adjacency list, or a name
  resolved from an object reference.
  *Test:* declared reading check — `src/upkg/Level.h` is read against this
  clause; the returned type carries no resolved graph, no adjacency list and
  no name resolved from an object reference. **The § 4.1 slot-to-actor
  mapping is exempt by name**: it is the file's own indexing made usable, not
  a graph, and § 4.1 requires it on one branch of § 4.5's derivation.
  *Breaks when:* the reader grows a convenience graph, at which point UTA-0006
  binds to it, two vocabularies for one thing exist, and the lifetime problem
  UTA-0004 § 4.1 avoids by returning references unresolved comes back.

## 6. Failure modes

| Condition | Result |
|---|---|
| Tail ends before the array the header states | `MalformedData` — INV-4 |
| Array length implies a walk past the export's end | `MalformedData` — the walk's end is UTA-0003's validated range, § 4.4 |
| Export's property list unparseable | Inherited from UTA-0003's `readPropertyList` |
| Package version outside 61–69 | Refused by `Package::open` before this reader is reachable, § 4.2 |
| A `Paths` value out of range in real content | INV-2 fails — this is a finding about the layout, not content to tolerate |
| The `Paths`/`upstreamPaths` set mismatch of § 2.2 | **Not a refusal.** It is unexplained, it occurs on maps that are otherwise sound, and a reader that rejects those levels rejects most of the library |
| A navigation point with no `Paths` entry at all | **Not a refusal, and not a defect.** Five classes in the corpus carry `nextNavigationPoint` and no `Paths` — unpathed nodes in maps never rebuilt. A reader or test asserting that every navigation point is pathed is wrong about real content |

## 7. Tests

**Tier 1 — unit, always on.** `tests/unit/PackageContentTest.cpp` against the
fixture builder of § 4.7. Covers INV-1 and INV-4, both of which need content
constructed to be wrong in one named way; UTA-0004's INV-9, whose fixture
case is owed here because this item is the first to implement the actor
array (§ 4.2); and **UTA-0004's INV-1 at fixture level** — the reader ends at
`serialOffset + serialSize` on a fixture whose length the builder knows. None
of the three can be exercised by real content, which supplies no truncated
arrays and no known-wrong lengths.

**Tier 2 — declared reading check.** INV-5 has no fixture: no test can
demonstrate the absence of a member that was never added. It is checked by
reading the header, the same shape UTA-0005 used for its own
`executes nothing` clause.

**Tier 3 — real assets, off by default.** `tests/real/RealInstallTest.cpp`
under `-DUTA_REAL_ASSET_TESTS=ON`, over the install `UTA_UT_INSTALL_DIR`
names. Covers UTA-0004's INV-1 for this reader — every `Level` export
consumed exactly — and this spec's INV-2 and INV-3, which have no fixture
that could establish them: they are claims about content this project did
not write, and a fixture would only assert what the fixture builder was told.

**`Level` joins UTA-0004 § 7's zero-refusal set, and that is a decision this
item makes.** That list names `Polys`, `Model` and `Palette`, and a refusal
there fails the tier; `Level` was absent because `readLevel` did not exist.
It joins them **for the refusals this reader owns** — a tail it cannot
interpret, an array length that overruns the export, a layout that ends
anywhere but the export's end. Every map in the install has a `Level` export
and the reader has no version branch of its own (§ 4.2), so a refusal of that
kind is a reader defect until shown otherwise: zero are permitted and one
fails the tier.

**UTA-0004 § 7's two recorded-rather-failed shapes still apply**, because
neither is this reader's. A property list that does not parse never reaches
this layout at all — that layer is UTA-0003's, and § 6 keeps the row saying
so. A redundant offset contradicting its own payload is likewise an export
disagreeing with itself. Those stay recorded per export; everything else
fails.

**The tier asserts its own population, or INV-2 and INV-3 pass vacuously.**
Both quantify over the non-empty `Paths` entries found in the install, and a
reader that surfaced none — a property-reading regression, or the exact-name
class filter § 4.6 measured as resolving zero — satisfies both by checking
nothing while the tier stays green. **So the tier records how many entries it
resolved and fails on zero.** That is the assertion, and it is the only one:
§ 2.2's 8,511 across three maps is a human sanity check on the recorded
figure, not a threshold the test enforces. A magnitude floor would have to be
re-tuned every time the install grows, which is the thing § 2.2 says about
every figure here.

**This tier is the item's only oracle for real content, and it is off by
default** — an ordinary gate run proves agreement with our own fixtures.
`readLevel` is NOT in `readModel`'s position, which UTA-0004 § 7 calls "the
one reader the ordinary gate does not exercise": § 4.7 grows a fixture here,
so exact consumption, ordering and refusal are all checked on an ordinary
run. What only this tier can reach is whether the layout is right about
content nobody here wrote. § 10 grades that.

## 8. Alternatives considered (and rejected)

**Return a resolved navigation graph from `upkg`.** Rejected: UTA-0006 owns
the graph types by the roadmap's own division, and § 3.2 item 1 gives two
further reasons. It would also force this reader to decide what an
unresolvable index means, which it cannot see enough to decide.

**Wait for `unav` and derive the layout there.** Rejected: the layout is a
property of the container format, so it belongs with the other readers, and
`ut-dump` (UTA-0012) wants it without wanting a graph.

**Treat the community's documented field order as verified and ship it.**
Rejected on precedent: UTA-0004 § 2.1 measured that documentation's array
order for `Model` and found it wrong, yielding five nodes for a 450 kB
export. The same source is the origin of the `ReachSpec` field set in § 2.1,
which is why that paragraph is labelled believed rather than known.

**Reject levels whose `Paths` and `upstreamPaths` sets disagree.** Rejected:
§ 2.2 measured that on two of three maps, and § 6's last row says what
rejecting them would cost.

## 9. Out of scope

- The `Model` BSP tables — UTA-0069.
- The graph types and the queries over them — UTA-0006.
- Interpreting what a custom `NavigationPoint` subclass means — UTA-0023,
  with its ancestry from UTA-0005. **Using** that ancestry to decide whether
  an export is a navigation point at all is in scope and is § 4.6's filter;
  what it means is not.
- Returning the `FURL` or anything else in the tail — § 3.2 items 2 and 3.
- Explaining the `Paths`/`upstreamPaths` residue. This item records it,
  refuses to reject content for it, and does not claim to know its cause.

## 10. What checks this

| Invariant | What actually checks it |
|---|---|
| INV-1 | `tests/unit/PackageContentTest.cpp` against a fixture with known sparse indices. A fixture asserts what the builder was told, so this proves the reader does not reorder; it says nothing about real content |
| INV-2 | `tests/real/RealInstallTest.cpp` only, off by default. On an ordinary gate run: **nothing** |
| INV-3 | `tests/real/RealInstallTest.cpp` only, off by default. On an ordinary gate run: **nothing** |
| INV-4 | `tests/unit/PackageContentTest.cpp`, both directions |
| UTA-0004 INV-9 (inherited, its test owed here) | `tests/unit/PackageContentTest.cpp` — the sparse actor-array case, written for the first time by this item |
| UTA-0004 INV-1 (exact consumption) | `tests/unit/PackageContentTest.cpp` at fixture level on every ordinary run, and `tests/real/RealInstallTest.cpp` over the install with zero refusals permitted (§ 7 tier 3) |
| INV-5 | A declared reading check. No mechanical catcher — **nothing** stops a member being added later |
| A `ReachSpec`'s non-node fields — the collision radius and height § 1 promises | **Nothing.** INV-3 checks the start node, which pins the two node fields; no invariant reaches the rest. Transposed radius and height, or either read from a neighbouring integer, consume the same bytes and name the same nodes. UTA-0006 binds to them |
| The derived layout of the rest of the tail | **Nothing beyond exact consumption.** § 4.3 says why that is weaker than it looks, and this item returns none of that data, so a wrong reading of it is invisible until something consumes it |

**Ungraded on an ordinary gate run: INV-2, INV-3, INV-5, the `ReachSpec`
collision fields, and the rest of the derived tail.** Graded: INV-1, INV-4
and the two inherited UTA-0004 rows. That majority is this item's honest
error budget.

It is worse than UTA-0004's for a structural reason rather than a fixable
one. The invariants establishing the item's whole point — that the indices
mean what § 2.1 believes they mean — are claims about content this project
did not write, so no fixture can settle them and the tier that can is off by
default. The collision fields are worse still: they are returned, UTA-0006
binds to them, and nothing here checks them at all.

## 11. Cross-doc impact

- `docs/specs/UTA-0004-typed-level-content.md` § 4.5 says `Model` was split
  out as UTA-0057, "which carries this section as its brief". That is now
  UTA-0069. A pointer correction.
- `docs/specs/UTA-0004-typed-level-content.md` § 4.1 says "UTA-0006 and
  UTA-0009 consume only `Polys` and the actor list". UTA-0006 also consumes
  this item's reach specs; that is the reordering the roadmap records on
  2026-09-06.
- **UTA-0004 § 4.9** remains correct: the derivation of the tail is this
  item's.
- **UTA-0004 § 4.1's `struct Level` gains `reachSpecs`** (and conditionally
  the slot mapping). That document declares the type "rather than left to
  the implementation, because … four later items bind to them", so the
  declaration there must gain the member or the next item binding to `Level`
  reads a superseded one.
- **UTA-0004 § 7 tier 3's zero-refusal list gains `Level`**, per § 7 here.
  That list currently names `Polys`, `Model` and `Palette`, and it is the
  section that actually defines the tier's refusal policy — an implementer
  building the harness reads it, not this spec.
- **UTA-0004 § 10's INV-1 and INV-2 rows become false when this ships.**
  They say `readLevel` "past the actor array" has no fixture case and that
  the malform tier "does not reach … `readLevel`'s remainder at all". § 4.7
  builds that fixture, so both rows need the exception removed.
- **UTA-0006** gains a blocked-by on this item, recorded in the roadmap.
- **UTA-0012's** third day-one query depends on this item, and re-running
  § 2.2's measurement through `ut-dump` is a check on `ut-dump`.

## 12. Cold-eyes loop log

Kept outside this file per `~/.claude/standards/spec-format.md` § 6:
[`docs/reviews/UTA-0057-level-tail-and-reachspecs-loop-log.md`](../reviews/UTA-0057-level-tail-and-reachspecs-loop-log.md).

## 13. Resource cost

One reader in one existing library, no new external dependency and no change
to the link closure — UTA-0005's ancestry walk, which § 4.6's class filter
needs, is already inside `uta_upkg`.

The cost is not code volume, it is derivation: the layout is unknown, and the
only oracle for real content runs over an install of hundreds of maps.
UTA-0004 records deriving one such layout as the largest single risk in that
item — where it failed and came back as this one.

Two costs this item carries that its brief did not predict. It is the first
to implement the actor array, so UTA-0004's INV-9 fixture case is owed here
rather than inherited. And § 4.6's filter needs the ancestry walk, so the
real-asset tier depends on UTA-0005 as well as on this reader.

# UTA-0069 — `upkg`: the `Model` BSP tables

**Status:** accepted (2026-09-07). Four `review-contract` loops over two runs, each run reaching the cap for a spec; the second run gated the amendment that derived five element layouts. The loop log is `docs/reviews/UTA-0069-model-bsp-tables-loop-log.md`.
**Kind:** implement.
**Source:** ROADMAP UTA-0069 (consumer-request-2026-09-06 games-drive; split
from UTA-0057 on 2026-09-06, which keeps the `Level` tail).
**Blocked by:** UTA-0003 for the container and its validated export ranges.
UTA-0004 for `Model`'s declared struct and for § 4.3's acceptance rule, which
this item inherits rather than restates.
**Blocker for:** UTA-0007 — `umap` partitions a level on its zones, which live
in these tables and not in `Polys`. UTA-0011 (`ubake`) needs the built
surfaces. UTA-0012's package dump reaches them through the same reader.

Read the BSP tables of a `Model` export, so that a level's built geometry and
its zones can be read without running the game.

## 1. Goal

Given a `Package` opened by UTA-0003, `readModel` consumes a `Model` export
exactly and returns the tables the file stores, in the file's own order and
indexing.

UTA-0004 declared `readModel` and its return type and did not ship them. Its
§ 4.5 records what was verified — a 41-byte `UPrimitive` prefix, a run of
compact-index-prefixed arrays, an object reference to the brush's `Polys`, a
second run, two trailing `i32` — and records that the array **order**
could not be derived, calling it "the largest single risk in this item". It
could not be derived there, and the item came back as this one.

**This spec derives that order and states it, because it was measured here.**
§ 2.2 is the measurement and § 4.4 is the answer. What remains unfinished is
named in § 4.6 rather than hidden: one element layout inside the later tables
is still open, and § 4.3's acceptance is what closes it.

## 2. Problem

A `Model` is the level's built shape. `Polys` (UTA-0004 § 4.4) carries the
*brush* polygons an author drew; the `Model` carries what the editor's BSP
build produced from them. The two disagree whenever a surface was edited after
the last build, which is why this item exists at all: the Monster Hunt server
work needs `PolyFlags` on **built** surfaces, and measured 2026-09-06 by that
session, of 202 candidate surfaces flagged from brush data only 42 were
actually solid in the running game — and on `MH-Village1` it was 6 of 6 wrong.

### 2.1 What was known, and what was only believed

UTA-0004 § 4.5 verified the prefix, the presence of two array runs either side
of a `Polys` reference, and the two trailing `i32`. It did **not** establish
which array is which, nor any element layout.

It also recorded that the community documentation is measurably wrong: read in
the order that documentation implies, `DM-Deck16][.unr` yields **five nodes and
thirty-nine surfaces** for a `Model` export of over 450 kB. That figure is the
one published number this item had to overturn.

### 2.2 The derivation, measured here 2026-09-07

Method: walk a candidate layout over every `Model` export in the reference
install and count how many consume their export exactly, per UTA-0004 § 4.3.
The check is total — it fires on every field without anyone predicting which is
wrong — and it runs against content this project did not write.

Population: the 837 `.unr` packages under the reference install's `Maps`
directory (838 entries, one of which is not a package), holding **556,452** `Model` exports.

Two results, and what each one settles is not the same thing.

- Under the order of § 4.4 with only the empty-array case exercised,
  **533,685** exports (95.91%) consumed exactly and **not one** completed at
  the wrong offset. **That settles the array COUNT on each side of the `Polys`
  reference, and the position of the scalars between them — not which array is
  which.** Every table is empty there, so each costs one zero byte and any
  permutation of the eleven consumes identically.
- Every one of those 533,685 exports' `Polys` references resolved to a
  genuinely `Polys`-classed export, or to null. **Zero** resolved to anything
  else. That settles the `Polys` field's position, and nothing else's.

**An array's identity is settled by its element width**, which only the
refinement below exercises: a table read in the wrong slot consumes wrongly
*unless* the slot it swapped with has the same element width. § 10 records the
one pair where that leaves nothing checking at all.

Refining the element layouts of § 4.5 took exact consumption to **534,024**.
Deriving the zone record and four later tables on 2026-09-07 took it to **545,652**
(98.06%), with 922 completing at the wrong offset and 9,878 failing to walk.
§ 4.6 owns what is left.

**That split is the SPECIFIED reader's**, the one § 4.1 describes: it refuses a
populated `Leaves`. The derivation probe steps over `Leaves` at four bytes —
an unverified width, used only to reach the tables beyond it — and reports 1,454
and 9,346 instead. The exact count is the same either way, because no export
with a populated `Leaves` consumes exactly under either reading; what moves is
532 exports between the two residue classes. Every figure in this document is
the specified reader's unless it says otherwise.

**Those were one step when this figure was first recorded, and the document did
not say so.** 545,652 was attributed to § 4.5 alone, while reaching it in fact
required element widths for `LightBits`, `Bounds`, `LeafHulls` and `Lights`
that § 4.6 called underived and no section stated — so no reader could
reproduce the number from this document. Re-measured 2026-09-07 with **§ 4.5's
first four layouts only**, the five later ones left unstated as they were then,
it is 534,024. With § 4.5 as it stands now it is 545,652, and that is the figure
tier 3 prints.

And the published figure is overturned. Under this order `DM-Deck16][.unr`'s
464,396-byte `Model` yields **1,720 nodes and 830 surfaces**, against the five
and thirty-nine the community order produces.

## 3. Scope decisions (agreed with the user)

### 3.1 The reader transcribes, as its siblings do — inherited, not re-decided

`Geometry.h` and `Level.h` both record it, and UTA-0004 § 3.1 is where the
user made the call: `upkg` returns the file's own tables and does no geometry
work — no BSP walk, no triangulation, no winding repair. Turning tables into
triangles is `ubake`'s decision.

This item does not reopen that. It is restated here only because a BSP table is
the most tempting place to break it: a `Model` is the one export where a
convenience traversal looks like an obvious kindness, and UTA-0006's reasoning
applies unchanged — a convenience graph here becomes a second vocabulary for
the same thing, and the downstream consumer binds to it before anyone decides
it was right.

### 3.2 The remaining calls are mine, with reasons

**The tables keep the engine's names, spelled the way this codebase spells
them** — the engine's word, lowerCamel: `vectors`, `points`, `nodes`, `surfs`,
`verts`, `numSharedSides`, `zones`, `polys`, `lightMap`, `lightBits`,
`bounds`, `leafHulls`, `leaves`, `lights`, `rootOutside`, `linked`. UTA-0004 § 4.1 already declared
`polys`, `rootOutside` and `linked` that way, and `Geometry.h` and `Level.h`
spell every member so. **UTA-0007 binds to these names**, so they are a
contract rather than a convenience.

**`leaves` is named here but is not a member yet** — § 4.1 says why. The name
is reserved so that adding it later is not a rename; every other name in the
list is a member from the reader's first version.

Element structs take the engine's name without Unreal's `F`: `BspNode`,
`BspSurf`, `Vert`, `ZoneProperties`, `LightMapIndex`, `Plane`, `Box` —
`Level.h`'s `ReachSpec` is the precedent. **Three of the later tables need no
struct**: § 4.5 gives `lightBits` a `std::uint8_t` element, `leafHulls` an
`std::int32_t`, and `lights` an `ObjectReference`.

**Two engine shapes UTA-0004 never declared are declared here**, fields written
out, because *"the same shape"* is not a spelling and two builders would not
pick the same one:

```cpp
struct Plane { Vector3 normal; float w = 0; };            // FPlane
struct Box   { Vector3 min, max; bool valid = false; };   // FBox
```

`BspNode::plane` is the first, and `bounds` is a `std::vector<Box>`. `Box` is
the shape UTA-0004 § 4.1 flattened into the prefix's `boundsMin`, `boundsMax`
and `boundsValid` rather than typing — **the prefix keeps those flattened
members unchanged**; only the array element takes the struct.

**Object references are `ObjectReference`, and there are four**:
`BspSurf::texture`, `BspSurf::actor`, `ZoneProperties::zoneActor`, and every
element of `lights`. Each is returned unresolved exactly as `Polygon::texture`
already is. Every other index field is the file's own integer.

**The lowerCamel rule reaches element FIELDS, not just members.** § 4.5 spells
a field as the engine does — `ZoneActor`, `Connectivity`, `PolyFlags` — because
that is what names it in the file; the member is `zoneActor`, `connectivity`,
`polyFlags`. § 11 lets UTA-0007 bind to what is inside a zone, so the field
spellings are a contract too.

**Index fields stay as the file's own integers, and are not resolved into
pointers or references between tables.** A node names its surface by index; a
surface names its texture by object reference. Resolving either would hand the
caller a view whose lifetime it did not ask for — `Geometry.h`'s stated reason
— and would make the reader's output impossible to check byte-for-byte against
the file, which § 4.3 turns into this item's whole acceptance.

## 4. Design

### 4.1 Layout and the build

`readModel` joins the existing `Polys` reader in the files UTA-0004 § 4.1
allocated to it, and adds no library and no link edge:

```
src/upkg/Geometry.h  .cpp   Polys, Model
```

The `Model` struct extends the one UTA-0004 § 4.1 declared. Its verified
scalar fields are unchanged; the BSP tables are the `std::vector` members that
section said the implementation would add, named in § 3.2.

**The zone records ARE returned**, as `std::vector<ZoneProperties> zones`, and
so is `numSharedSides`. UTA-0007 partitions a level on its zones and they live
nowhere else, so consuming them silently would leave the consumer that needs
them unable to ask. `Level.h` is the precedent for returning out of a tail
rather than against it: it returns its `reachSpecs` and consumes the rest.
§ 4.5 derives `ZoneProperties`, so the member has a complete element type.

**`leaves` is NOT returned.** A `std::vector` needs a complete element type,
and § 4.6 leaves that layout underived — so returning it would mean either
stating a layout nothing has verified, which § 4.6 forbids, or shipping an
empty element struct that reads as finished. The user ruled on it (2026-09-07),
having been asked the same question about `zones` and answered it the other
way, by deriving. The asymmetry is deliberate: `zones` was derivable in one
pass, where `Leaves` waits on § 4.6's residues.

**Nor is it stepped over when it is populated.** An empty `Leaves` costs one
zero byte and is consumed like any other empty table; a non-empty one is
`MalformedData` and fails the tier-3 run, which is § 6's row for a table this
reader does not yet describe. Stepping over it would need the very width § 4.6
withholds. So the omission narrows what this reader RETURNS without widening
what it ACCEPTS.

**No stated consumer is blocked by it.** UTA-0007 partitions a level on its
**zones**; UTA-0011 bakes the built surfaces; UTA-0012 dumps a package through
this reader. None of the three names leaves, and only the third reaches every
table — so a dump is short by this one until § 4.6 closes, and by nothing else.
**When it closes, `leaves` becomes a returned member** — § 3.2 reserves the
name so that adding it is not a rename.

### 4.2 What this item inherits and must not restate

- **The version gate.** `Package::open` refuses a `packageVersion` outside
  61–69 before any table is read (UTA-0004 § 4.2). No reader repeats it.
- **The acceptance rule.** UTA-0004 § 4.3: a reader that does not end exactly
  at `serialOffset + serialSize` returns `MalformedData`, and never a partial
  result.
- **The bounds-checked cursor.** `ByteReader` owns every bounds check
  (UTA-0003 § 4.2), so a count read from the file cannot walk off the export.
- **The property-list prefix.** `readPropertyList` yields `nativeOffset`, and
  the walk below starts there — the same entry every sibling reader uses.

### 4.3 The derivation method, and when it is done

The method of § 2.2, and UTA-0004 § 4.3's acceptance.

The layout is right when `readModel` consumes every `Model` export in the
reference install exactly. It is not a proof of correctness: a reader can
consume the right number of bytes and assign them to the wrong fields. One
independent check narrows that gap, and closes it for nothing else: the `Polys`
reference must resolve to a `Polys`-classed export **or be null** (§ 2.2
measured this at 100% of walked exports, counting null as passing), which is
INV-5 — and § 6 makes null legitimate, so a tier-3 assertion that refuses it
fails content this reader must accept. § 2.2's Deck16 node and surface counts
are a derivation observation and not a second check — no bound is stated that
they could fail, and no invariant carries them.

**The residue is the work.** § 2.2 reached 98.06%; § 4.6 names what is left,
and INV-4 states the bar: every export, no tolerance.

### 4.4 The derived order

Measured 2026-09-07 (§ 2.2). After the property list, at `nativeOffset`:

```
FBox      BoundingBox        FVector min, FVector max, u8 valid   (25 bytes)
FSphere   BoundingSphere     FVector, float                       (16 bytes)
  index-prefixed array   Vectors      FVector, 12 bytes each
  index-prefixed array   Points       FVector, 12 bytes each
  index-prefixed array   Nodes        FBspNode, variable width
  index-prefixed array   Surfs        FBspSurf, variable width
  index-prefixed array   Verts        FVert, variable width
i32       NumSharedSides
i32       NumZones           followed by NumZones zone records
index     Polys              an object reference
  index-prefixed array   LightMap     FLightMapIndex, variable width
  index-prefixed array   LightBits
  index-prefixed array   Bounds
  index-prefixed array   LeafHulls
  index-prefixed array   Leaves
  index-prefixed array   Lights
i32       RootOutside
i32       Linked
```

The 41-byte prefix, the `Polys` reference and the two trailing `i32` are
UTA-0004 § 4.5's verified facts, and they sit where it said. What this section
adds is **which array is which, and that `NumSharedSides` and `NumZones` are
raw `i32` between the two runs** — the part that was unknown, and the part the
community order gets wrong.

**One of those verified facts is superseded here.** UTA-0004 § 4.5 records "a
second, shorter run of arrays". The second run holds **six** arrays against the
first run's five, so it is longer in count — and on a brush model, where every
table costs one zero byte, longer in bytes too. "Shorter" holds only of a
populated level model, whose geometry sits in the first run's `Nodes`, `Surfs`
and `Verts`. An implementer reading "shorter" as a
count reads four arrays after `Polys` and misassigns every field from there.
§ 11 records the supersession.

Every array is prefixed by a compact index giving its element count, so an
empty table costs one zero byte. A brush model — every table empty — is
therefore **68 payload bytes plus the width of its `Polys` index**, which
`ByteReader::readIndex` gives as one to five bytes depending on the export
slot.

**Do not assert 70.** Measured 2026-09-07 over the install, `Model` payloads
under 90 bytes fall in four buckets — 65 bytes (198), 69 (18), 70 (411,969),
71 (121,665). The last three are the one-, two- and three-byte index widths;
65 is § 4.6's unexplained class, which 68-plus-an-index cannot produce. A
fixture pinned at 70 rejects valid brush models, and one pinned to 69–71
rejects the 65-byte class.

**That threshold selects on size, not on every table being empty**, so these
buckets are not a partition of § 2.2's exactly-consuming count and the two
totals are not expected to reconcile.

### 4.5 The element layouts that are settled

Each was confirmed the same way: the failure count at that table collapsed when
the layout was corrected, and no earlier table's count moved.

**`FBspNode`** — `FPlane` (16 bytes; the member is `plane`, § 3.2's `Plane`),
`ZoneMask` (a 64-bit mask; `ByteReader`
offers `readI64` and no `readU64`, so read it as `i64` and cast, as `readPolys`
does for `PanU`/`PanV`), `NodeFlags` (`u8`),
then seven compact indices (`iVertPool`, `iSurf`, `iFront`, `iBack`, `iPlane`,
`iCollisionBound`, `iRenderBound`), then `iZone[2]` and `NumVertices` as three
bytes, then **`iLeaf[2]` as two raw `i32`**.

That last field is the one that matters. Read as compact indices it failed
22,536 exports; read as raw `i32` it failed **3**. Nothing else in the walk
changed.

**`FBspSurf`** — index `Texture`, `u32 PolyFlags`, six indices (`pBase`,
`vNormal`, `vTextureU`, `vTextureV`, `iLightMap`, `iBrushPoly`), `i16 PanU`,
`i16 PanV`, index `Actor`. One export in the install fails at this table.

**`FVert`** — index `pVertex`, index `iSide`.

**`FLightMapIndex`** — index `DataOffset`, **index `iLightActors`**, `FVector
Pan`, `float UScale`, `float VScale`, `i32 UClamp`, `i32 VClamp`. Both
readings were re-measured 2026-09-07 **under the layouts this section now
states**, so they reproduce against it: `iLightActors` as a raw `i32` fails
5,195 exports at this table and takes exact consumption to 535,360; as a
compact index, 98 and 545,652. Reading `UClamp` and `VClamp` as compact indices
is refuted the same way — 535,291 exact, 96.20%. This table is
genuinely exercised rather than merely silent: 10,361 of the exactly-consuming
exports carry a non-empty `LightMap`.

The five below were derived on 2026-09-07, in the file order § 4.6 requires.
**Each *fell from* figure is the failure count at that table with that table
unstated and every table before it stated** — the state the derivation was
actually in on reaching it. That is why some exceed the count of exports
arriving with a non-empty table: an implausible count refuses there too.

**`FZoneProperties`** — index `ZoneActor`, `i64 Connectivity`, `i64
Visibility`. Seventeen bytes where `ZoneActor` is null, wider as that index
widens. It was derived **without assuming a layout**: the span between
`NumZones` and the following `Polys` reference was swept, and a span accepted
only where the index there resolved to a `Polys`-classed export and the
`LightMap` count after it was sane. The method self-checks where the answer is
known in advance — on zero-zone exports the span must be 0, and was.

**Its field ORDER is settled by content, not by width**, which matters because
a fixed field and a variable one summing correctly cannot be told apart by byte
count. Down consecutive records the middle eight bytes read 1, 2, 4, 8, 0x10,
0x20, 0x40, 0x80: a mask whose set bit tracks the record's own ordinal. That
identifies `Connectivity` as a zone mask and fixes the leading field as what
precedes it. Implementing the layout took the zone-record failure class to
**zero**.

**`LightBits`** — one byte per element. 15,251 exports carry a non-empty
`LightBits`, so with no stated layout every one of them stops there; at one
byte per element the failures at that table are 1,830.

**`Bounds`** — `FBox`, 25 bytes, the prefix's own shape. Failures at it fell
from 18,022 to 5,121. It carries the largest residue, so it was confirmed a
**second time by an independent method**: sweeping its element width against
the end of the payload put 11,541 exports at 25 bytes, against 643 at the next
candidate.

**`LeafHulls`** — four bytes per element. Failures at it fell from 13,010 to
1,196, and exact consumption is 538,456 against 534,542 for a compact index —
which is what separates the two candidates, since both are plausible widths.

**`Lights`** — one compact index per element, an object reference. As a compact
index its own failures are 334 and exact consumption 545,652; as a raw four-byte
read, 6,916 and 538,456.

### 4.6 What is not yet derived

**One element layout is left: `Leaves`.** It is genuinely populated rather
than an artefact of misalignment — 680 exports reach it with a non-empty count
**under the stepping probe, which is the only configuration that can reach past
it at all**,
and those counts are small and plausible, the large majority declaring sixteen
or fewer. But no shape lands. Sweeping *k* compact indices plus *f* fixed bytes
against the end of the payload, for *k* up to four and *f* up to thirty-two,
the best candidate accounts for 35 of the 680. **The apparent runners-up are
aliases rather than corroboration**: an index whose value is zero occupies one
byte, so "two indices plus nine bytes" and "three indices plus eight bytes" are
one shape counted twice, which is why they score identically. Do not read that
pair as agreement.

By the cascade rule below, that puts the misalignment UPSTREAM of `Leaves`, in
the residues named next, so it cannot be settled until those close.

**9,878 exports where the walk stops**, at a position § 4.4 places: `Bounds`
5,121, `LightBits` 1,830, `Leaves` 1,197, `LeafHulls` 1,196, `Vectors` 215,
`Lights` 201, `LightMap` 98, `Points` 15, `Nodes` 3, `Surfs` 1, and 1 at the
trailing `i32`. **All but the 1,197 sit at a table § 4.5 settles** — `Leaves`
is the gap above, refused rather than walked, on its own footing. In each the declared count is implausible, so the
cursor was already misaligned when it arrived: **the wrong width is upstream of
the table that reports the error, not at it.** Those 234 belong to the version
class below.

**922 exports that complete at the wrong offset.** The walk consumed a
plausible number of bytes and still landed wrong, so no table reports an error
and only the end offset says anything. A field *inside* an otherwise-correct
table is mis-sized.

**One class is now scoped rather than unexplained.** Every one of the 198
all-empty exports weighing 65 payload bytes sits in a single package — the
corpus's only one at version 61 — and every `Model` export in that package
fails. No other version has a short payload class. So the branch is real, it is
version 61, and it costs 0.04% of the corpus. **Its layout is not simply four
bytes shorter**: a dumped export reads `FBox`, then twelve zero bytes, then
twelve bytes decoding as six compact indices, then `NumSharedSides` and
`NumZones`, then the two trailing `i32` — 25 + 12 + 12 + 8 + 8, the 65. Dropping
`FSphere`'s `W` for a 37-byte prefix was measured and changed nothing.

Outside that package the failures are **content-dependent rather than
version-dependent**, which is the useful discriminator: within the version
holding the bulk of the corpus, a little over half of the real-content models
parse and the rest do not, and no version rule separates them.

These cascade — a wrong element width makes every later count garbage — so they
are derived in file order, each measured by § 4.3's check before the next.
**That ordering is the method, not an observation**: fixing them out of order
attributes one table's failures to another, which is what makes a residue look
irreducible when it is not.

**No layout for `Leaves` is stated here**, deliberately, and UTA-0004 § 4.5 is
the precedent: a stated-but-unverified layout reads as verified to everyone
downstream. What is stated is where it sits (§ 4.4) and how to know when it is
right (§ 4.3). § 4.1 says what the reader does in the meantime.

### 4.7 Fixtures

**Tier 1 is owed now, not once § 4.6 closes.** § 4.4 and § 4.5 state every
field a fixture needs for the settled tables, and an empty table costs one zero
byte — so a `Model` with populated `Nodes`, `Surfs` and `Verts`, zero zones and
every later table empty is encodable today. **§ 4.5 now settles the zone
record, `LightBits`, `Bounds`, `LeafHulls` and `Lights`, so a fixture may
populate those too.** Only `Leaves` waits, and UTA-0004 § 7's rule that a
builder cannot encode a withheld layout reaches it alone.

Four cases in the builder of UTA-0004 § 4.10, one per branch needing
constructed content:

- **INV-1** — a `Model` with tables at known counts, whose total length the
  builder knows.
- **INV-2, an oversized count** — the same, with a node count declaring far
  more than the file holds.
- **INV-2, a negative count** — a second fixture declaring one. INV-2's test
  clause names both and § 6 gives each its own failure row, so one fixture
  leaves half the invariant unexercised.
- **INV-3** — a `Model` whose nodes and surfs are **distinguishable per
  index**. Known counts do not give this, which is why it is its own case: a
  reader that compacts or reorders a table passes a count check.

## 5. Invariants

- **INV-1** — `readModel` consumes a `Model` export exactly, ending at
  `serialOffset + serialSize`, or returns `MalformedData` with no partial
  result.
  *Test:* `tests/unit/PackageContentTest.cpp` on the fixture of § 4.7, whose
  length the builder knows; and `tests/real/RealInstallTest.cpp` over the
  reference install (§ 7 tier 3).
  *Breaks when:* an element width is wrong or a table is read in the wrong
  position — at which point the cursor lands somewhere other than the export's
  end. This is UTA-0004 § 4.3's rule applied to this reader, and it is the only
  check that fires on a field nobody predicted would be wrong.

- **INV-2** — A `Model` whose declared table count is negative, or would walk
  past the export's end, is refused with `MalformedData`, and no allocation is
  sized from that count before it is checked.
  *Test:* `tests/unit/PackageMalformedTest.cpp` builds a `Model`
  declaring a node count far larger than the file, and a second declaring a
  negative one — `ByteReader::readIndex` takes the sign from bit 7 of the first
  byte, so it is encodable — and asserts an `Error` naming the count check.
  *Breaks when:* the reader reserves from the file's own count — the
  four-byte edit that asks for gigabytes, which is UTA-0004 INV-4's reason and
  applies unchanged to every count in § 4.4: eleven arrays and the zone record
  run.

- **INV-3** — The tables are returned in the file's own order and indexing:
  the element at returned position `i` is the element the file stores at index
  `i`, and nothing is dropped, reordered or renumbered.
  *Test:* `tests/unit/PackageContentTest.cpp` builds a `Model` whose nodes and
  surfaces are distinguishable at known indices, and asserts each returned
  position holds what the fixture put there.
  *Breaks when:* the reader compacts a table or skips elements it judges
  unused — at which point every `iSurf`, `iVertPool` and `iZone` cross-index in
  the model points at the wrong row, and INV-1 does not notice because the byte
  count is unchanged.

- **INV-4** — `readModel` consumes **every** `Model` export in the reference
  install exactly. Zero refusals.
  *Test:* `tests/real/RealInstallTest.cpp`, which already requires exactly this
  of `Polys`, `Palette` and `Level` (§ 7 tier 3).
  *Breaks when:* any table's element layout is wrong — § 2.2 measured a single
  wrong field costing 4% of the install and another 0.8%.
  **This is the item's acceptance, not a progress measure.** UTA-0004 § 7 puts
  `Model` in its zero-refusal set, and the ROADMAP bullet states the same
  acceptance. § 2.2's 98.06% is how far the derivation has got, not a floor
  anyone may ship against: a rate written into the contract would freeze this
  item's unfinished work into a permanent tolerance. § 4.6 is the work, and
  this invariant is what says when it is done.

- **INV-5** — A `Model`'s `Polys` reference resolves to a `Polys`-classed
  export, or is null.
  *Test:* `tests/real/RealInstallTest.cpp` resolves it for every `Model` it
  consumed and **fails on any reference resolving to a class other than
  `Polys`**; null is permitted. § 2.2 measured zero of the first, so the
  assertion sits at zero rather than at a rate — a test that merely tallied
  could not fail, which is the shape UTA-0004 § 7 records having removed once
  already.
  *Breaks when:* the field is read at the wrong offset. This is the one check
  that distinguishes a correct field assignment from a byte count that merely
  adds up, which is the gap § 4.3 says INV-1 cannot close.

## 6. Failure modes

| Condition | Result |
|---|---|
| Export ends before a table's own count prefix can be read | `MalformedData` — INV-1 |
| A table count implies a walk past the export's end | `MalformedData` — INV-2, checked before any reserve |
| A table count is negative | `MalformedData` — INV-2 |
| Export's property list unparseable | Inherited from UTA-0003's `readPropertyList` |
| Package version outside 61–69 | Refused by `Package::open` before this reader is reachable — § 4.2 |
| A `Model` export with no serialised data | An empty `Model`, as `readPolys` does for a sizeless `Polys`. UTA-0003 INV-7 makes a sizeless export ordinary. Measured 2026-09-07: the install's maps hold **no** such export, so this row is reachable only by content outside it |
| A `Polys` reference that is null | **Not a refusal.** `ObjectReference` makes null a legitimate value and a `Model` need not own brush polygons. § 2.2 did not separate null from resolved, so this rests on the type rather than on a measurement |
| A table this reader does not yet describe (§ 4.6) | `MalformedData`, and the tier-3 run FAILS. Until § 4.6 closes this reader does not satisfy INV-4 and the item is not done — the honest state, and why INV-4 states no tolerance. A reader that guessed the layout instead is what UTA-0004 § 4.5 forbids |

## 7. Tests

**Tier 1 — unit, always on.** `tests/unit/PackageContentTest.cpp` and
`tests/unit/PackageMalformedTest.cpp` against the fixtures of § 4.7.
Covers INV-1 at fixture level, INV-2 and INV-3 — all three need content
constructed to be wrong in one named way, which real content does not supply.
**Owed now, for the settled tables**: § 4.7 says why, and what waits.

**Tier 3 — real assets, off by default.** `tests/real/RealInstallTest.cpp`
under `-DUTA_REAL_ASSET_TESTS=ON` over `UTA_UT_INSTALL_DIR`. Covers INV-1 at
install scale, INV-4 and INV-5.

**This tier prints the figures § 2.2 asserts.** `~/.claude/skills/write-spec/references/drafting-rules.md`
prefers a test that prints a spec's numbers over a command recorded beside
them, and this spec's argument rests entirely on measurement. The
exact-consumption count, the population and the `Polys` class tally become an
output rather than a transcription. **The count is a diagnostic while § 4.6 is
open, never a bar** — INV-4 is the bar, and it admits no tolerance.

**Tier 3 is off by default**, so until § 4.7's cases land `readModel` is the
one reader the ordinary gate does not exercise — which UTA-0004 § 7 already
records. That is why tier 3 must FAIL on a refusal rather than record one: a
reader with a wrong field order refuses nearly everything, and a tier that
tallied refusals instead would be incapable of failing. UTA-0004 § 7 gives that
outcome as its own reason for the zero-refusal rule, and INV-4 keeps it.

## 8. Alternatives considered (and rejected)

**Take the community order and ship it.** Rejected on evidence: § 2.1's five
nodes and thirty-nine surfaces for a 450 kB export, and § 2.2's 1,720 and 830
under the derived order. It was never a live option; it is recorded because it
is the option a reader without § 2.2 in front of them would reach for.

**State the full layout from the engine's published serialiser and verify
later.** Rejected. It is the failure UTA-0004 § 4.5 names — a
stated-but-unverified layout reads as verified downstream — and § 4.5 above
shows why it would have been wrong twice, on `iLeaf` and on `iLightActors`,
both of which read plausibly either way and are settled only by measurement.

**Return the tables resolved into each other — a node holding its surface
rather than its index.** Rejected under § 3.2: it makes the output
uncheckable against the file, which is this item's whole acceptance, and it
takes a lifetime decision away from the caller.

**Derive every element layout before writing any of the reader.** Rejected:
the layouts cascade (§ 4.6), so the only instrument that distinguishes them is
a running walk. The reader is the derivation tool.

## 9. Out of scope

- Any geometry work — BSP traversal, triangulation, winding repair (§ 3.1).
  `ubake` owns it.
- Partitioning a level into rooms, and answering which room a point is in.
  That is UTA-0007, which consumes what this returns.
- The `Level` tail and the reach-spec array — UTA-0057, shipped.
- Resolving object references to names or objects (§ 3.2).
- Lightmap *data* interpretation. This item returns the tables' bytes and
  indices; what a lightmap means is a renderer question, and no roadmap item
  needs it yet.

## 10. What checks this

| Claim | What checks it |
|---|---|
| The order of § 4.4, for arrays of DIFFERING element width | INV-1 and INV-4, tier 3, over the whole install |
| Which of `Vectors` and `Points` is which | `nothing`. Both are arrays of 12-byte `FVector`, so the two are byte-identical under transposition and no consumption check can ever separate them. § 4.6 does not close this — it is open on its own footing, and UTA-0007 binds to the names |
| The `Polys` field's position specifically | INV-5 — the one check that separates a correct assignment from an adding-up byte count |
| The element layouts of § 4.5 | INV-1 and INV-4 only. Each was settled by the residue moving, and nothing checks a *named field* inside a node or surf against an independent source |
| The 922 exports of § 4.6 that complete at the wrong offset | INV-4, which fails on them. Nothing localises them to a table, and § 4.6 says so |
| The tables' order and indexing (INV-3) | Tier 1 fixture, once § 4.7 exists. `nothing` until then |
| Refusal before allocation (INV-2) | Tier 1 fixture. That the refusal *precedes* the allocation is not observable from outside — the same gap UTA-0004 records for its INV-4 |
| The residue of § 4.6 | `nothing`, by construction — it is the open work, and INV-4 is what says when it is closed |
| That a node's `iZone` names the zone a renderer would agree with | `nothing`. No independent oracle exists offline, and inventing one is UTA-0007's problem rather than this reader's |
| § 2.2's figures | Tier 3 prints them (§ 7) |

## 11. Cross-doc impact

- **UTA-0004 § 4.5** is this item's brief and says the layout could not be
  derived there. Its narrative stays as written — it is a true record of that
  item — but **one of its verified facts is superseded**: the second run of
  arrays is LONGER in count, six against five, and on a brush model longer in
  bytes as well. § 4.4 owns the correction. **Its § 4.5 already points here;
  its § 14 does not** — that section is a dated record of what shipped on
  2026-09-05, before the split, so it routes `readModel` to UTA-0057 and is
  left as written.
- **UTA-0004 § 4.10 and § 7 are superseded on fixtures**, for the settled
  tables only. That item's builder "does not build a `Model`" and its § 7 gives
  `readModel` no fixture case until the layout is derived. § 4.4 and § 4.5
  derive it, so § 4.7 owes tier 1 now — and § 4.5 settles the zone record too,
  so a fixture may populate it. Only a fixture exercising `Leaves` still waits,
  and there UTA-0004's rule stands unchanged.
- **UTA-0004 § 7's zero-refusal set is NOT amended.** `Model` stays in it and
  INV-4 states the same rule rather than a weaker one. UTA-0057 § 7 amended
  that list for `Level`; this item has no equivalent need, because its residue
  is unfinished derivation rather than content disagreeing with itself.
- **UTA-0007** binds to the member names § 4.4 and § 4.5 state, and this is the
  first document to state them. **`ZoneProperties`'s fields are now stated**
  (§ 4.5, derived 2026-09-07), so that item may bind to `zones` and to what is
  inside it. It does not name leaves, the one member § 4.1 withholds.
- **`src/upkg/Geometry.h`**'s header comment cites UTA-0004 § 4.4 and § 4.5;
  it gains this spec once the reader lands.
- **ROADMAP UTA-0069** carries the acceptance in one line; this spec is the
  contract behind it. UTA-0011 and UTA-0012 are its stated consumers.
- **No standard is touched**, and no override is added.

## 12. Cold-eyes loop log

Kept outside this file per `~/.claude/standards/spec-format.md` § 6:
[`docs/reviews/UTA-0069-model-bsp-tables-loop-log.md`](../reviews/UTA-0069-model-bsp-tables-loop-log.md).

## 13. Resource cost

One reader in an existing file, no new dependency and no change to the link
closure.

The cost is derivation, and § 2.2 has already spent most of it: the order is
settled and nine element layouts with it. What remains (§ 4.6) is bounded —
`Leaves`, the 922 exports that end at the wrong offset, and the version-61
prefix branch, each measured by the same total check.

The standing cost is the oracle. `readModel` has no tier-1 case until § 4.7,
so every claim here rests on a tier that is off by default and needs an
install of hundreds of maps. That is the same position UTA-0004 left it in, and
this item does not improve it — it only makes the tier able to pass.

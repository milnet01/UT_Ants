# UTA-0069 — `upkg`: the `Model` BSP tables

**Status:** draft (2026-09-07).
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
second shorter run, two trailing `i32` — and records that the array **order**
could not be derived, calling it "the largest single risk in this item". It
could not be derived there, and the item came back as this one.

**This spec derives that order and states it, because it was measured here.**
§ 2.2 is the measurement and § 4.4 is the answer. What remains unfinished is
named in § 4.6 rather than hidden: several element layouts inside the later
tables are still open, and § 4.3's acceptance is what closes them.

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

Population: the 838 `.unr` packages under the reference install's `Maps`
directory, holding **556,452** `Model` exports.

Two results settle the order.

- Under the order of § 4.4 with only the empty-array case exercised,
  **533,685** exports (95.91%) consumed exactly and **not one** completed at
  the wrong offset. A wrong order does not produce a zero there.
- Every one of those 533,685 exports' `Polys` references resolved to a
  genuinely `Polys`-classed export, or to null. **Zero** resolved to anything
  else. The field's position is therefore not a coincidence of byte counts.

Refining the element layouts of § 4.5 took exact consumption to **545,652**
(98.06%), with 1,155 completing at the wrong offset and 9,645 failing to walk.
§ 4.6 owns that residue.

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

**The tables are returned under the engine's own member names.** UTA-0004 § 4.1
states that the implementation names these members and that **UTA-0007 binds to
them**, so the names are a contract rather than a convenience. Inventing
friendlier ones would leave `umap` binding to a vocabulary no other document
uses.

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
section said the implementation would add.

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
consume the right number of bytes and assign them to the wrong fields. Two
independent checks narrow that gap, and close it for nothing else — the `Polys`
reference must resolve to a `Polys`-classed export (§ 2.2 measured this at
100% of walked exports), and the node and surface counts must be plausible for
the level's size (§ 2.2 measured Deck16).

**The residue is the work.** § 2.2 reached 98.06%; § 4.6 names what is left and
§ 5's INV-4 states the floor this must clear.

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

Every array is prefixed by a compact index giving its element count, so an
empty table costs one zero byte. That is what makes a brush model — every table
empty — decode to exactly 70 payload bytes, and it is the case § 2.2's first
measurement rests on.

### 4.5 The element layouts that are settled

Each was confirmed the same way: the failure count at that table collapsed when
the layout was corrected, and no earlier table's count moved.

**`FBspNode`** — `FPlane` (16 bytes), `ZoneMask` (`u64`), `NodeFlags` (`u8`),
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
Pan`, `float UScale`, `float VScale`, `i32 UClamp`, `i32 VClamp`. Reading
`iLightActors` as a raw `i32` failed 4,408 exports; as a compact index, 32.

### 4.6 What is not yet derived

The residue of § 2.2, by the table the walk stops at: the zone record after
`NumZones`, and the element layouts of `LightBits`, `Bounds`, `LeafHulls`,
`Leaves` and `Lights`.

These cascade — a wrong element width in `LightBits` makes every later count
garbage — so they are derived in file order, each one measured by the rate of
§ 4.3 before moving to the next. **That ordering is the method, not an
observation**: fixing them out of order attributes one table's failures to
another, which is what makes a residue look irreducible when it is not.

**No layout for these is stated here**, deliberately, and UTA-0004 § 4.5 is the
precedent: a stated-but-unverified layout reads as verified to everyone
downstream. What is stated is where they sit (§ 4.4) and how to know when each
is right (§ 4.3).

### 4.7 Fixtures

The fixture builder of UTA-0004 § 4.10 gains a `Model` case once § 4.6 closes.
Until then it cannot encode one — UTA-0004 § 7 records that a builder cannot
encode a layout the spec withholds, and that is why `readModel` is the one
reader with no tier-1 case today.

The fixture cases INV-1 and INV-2 need are a `Model` with tables at known
counts, and the same truncated one byte short.

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

- **INV-2** — A `Model` whose declared table count would walk past the export's
  end is refused with `MalformedData`, and no allocation is sized from that
  count before it is checked.
  *Test:* `tests/unit/PackageMalformedTest.cpp` builds a `Model`
  declaring a node count far larger than the file and asserts an `Error` naming
  the count check.
  *Breaks when:* the reader reserves from the file's own count — the
  four-byte edit that asks for gigabytes, which is UTA-0004 INV-4's reason and
  applies unchanged to six more tables here.

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

- **INV-4** — `readModel` consumes at least 99% of the `Model` exports in the
  reference install exactly.
  *Test:* `tests/real/RealInstallTest.cpp` counts exports consumed exactly and
  fails below the floor, and fails outright if it consumed none (§ 7 tier 3).
  *Breaks when:* a table's element layout is wrong. A defect of that kind does
  not cost a few tenths of a percent — § 2.2 measured a single wrong field
  costing 4% and another costing 0.8%. The gap between the floor and what a
  defect produces is what makes this a check rather than a tolerance, and a
  rate does not go stale as the library grows.

- **INV-5** — A `Model`'s `Polys` reference resolves to a `Polys`-classed
  export, or is null.
  *Test:* `tests/real/RealInstallTest.cpp` resolves it for every `Model` it
  consumed and counts the classes.
  *Breaks when:* the field is read at the wrong offset. This is the one check
  that distinguishes a correct field assignment from a byte count that merely
  adds up, which is the gap § 4.3 says INV-1 cannot close.

## 6. Failure modes

| Condition | Result |
|---|---|
| Export ends before a table the file's own count declares | `MalformedData` — INV-1 |
| A table count implies a walk past the export's end | `MalformedData` — INV-2, checked before any reserve |
| A table count is negative | `MalformedData` — INV-2 |
| Export's property list unparseable | Inherited from UTA-0003's `readPropertyList` |
| Package version outside 61–69 | Refused by `Package::open` before this reader is reachable — § 4.2 |
| A `Model` export with no serialised data | An empty `Model`, as `readPolys` does for a sizeless `Polys`. UTA-0003 INV-7 makes a sizeless export ordinary, and § 2.2 found such exports in the install |
| A `Polys` reference that is null | **Not a refusal.** § 2.2 measured null references in real content; a `Model` need not own brush polygons |
| A table this reader does not yet describe (§ 4.6) | `MalformedData`, and the export is counted against INV-4's rate rather than crashing the walk. Until § 4.6 closes, that is the honest result: the reader does not know the layout, and a reader that guesses is what UTA-0004 § 4.5 forbids |

## 7. Tests

**Tier 1 — unit, always on.** `tests/unit/PackageContentTest.cpp` and
`tests/unit/PackageMalformedTest.cpp` against the fixture of § 4.7.
Covers INV-1 at fixture level, INV-2 and INV-3 — all three need content
constructed to be wrong in one named way, which real content does not supply.
**Owed once § 4.6 closes**, and not before: § 4.7 says why.

**Tier 3 — real assets, off by default.** `tests/real/RealInstallTest.cpp`
under `-DUTA_REAL_ASSET_TESTS=ON` over `UTA_UT_INSTALL_DIR`. Covers INV-1 at
install scale, INV-4 and INV-5.

**This tier prints the figures § 2.2 asserts.** `~/.claude/skills/write-spec/references/drafting-rules.md`
prefers a test that prints a spec's numbers over a command recorded beside
them, and this spec's argument rests entirely on measurement. The rate, the
population and the `Polys` class tally become an output rather than a
transcription.

**Tier 3 is `readModel`'s only check until tier 1 exists**, which UTA-0004 § 7
already records, and it is off by default — so `readModel` is the one reader
the ordinary gate does not exercise. That is the reason INV-4 is a floor and
not a boolean: a reader with a wrong field order refuses nearly everything, and
a tier that recorded refusals rather than failing on them would be incapable of
failing.

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
| The order of § 4.4 | INV-1 and INV-4, tier 3, over the whole install |
| The `Polys` field's position specifically | INV-5 — the one check that separates a correct assignment from an adding-up byte count |
| The element layouts of § 4.5 | INV-1 and INV-4 only. Each was settled by the rate moving, and nothing checks a *named field* inside a node or surf against an independent source |
| The tables' order and indexing (INV-3) | Tier 1 fixture, once § 4.7 exists. `nothing` until then |
| Refusal before allocation (INV-2) | Tier 1 fixture. That the refusal *precedes* the allocation is not observable from outside — the same gap UTA-0004 records for its INV-4 |
| The residue of § 4.6 | `nothing`, by construction — it is the open work, and INV-4's floor is what says when it is closed |
| That a node's `iZone` names the zone a renderer would agree with | `nothing`. No independent oracle exists offline, and inventing one is UTA-0007's problem rather than this reader's |
| § 2.2's figures | Tier 3 prints them (§ 7) |

## 11. Cross-doc impact

- **UTA-0004 § 4.5** is this item's brief and says the layout could not be
  derived there. It stays as written — it is a true record of that item. Its
  § 14 already points here.
- **UTA-0007** binds to the member names of § 4.4 and § 4.5. This is the first
  document to state them, so that item's zone partition has something to name.
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
settled and four element layouts with it. What remains (§ 4.6) is bounded — six
tables, each measured by the same total check, each independent once the one
before it is right.

The standing cost is the oracle. `readModel` has no tier-1 case until § 4.7,
so every claim here rests on a tier that is off by default and needs an
install of hundreds of maps. That is the same position UTA-0004 left it in, and
this item does not improve it — it only makes the tier able to pass.

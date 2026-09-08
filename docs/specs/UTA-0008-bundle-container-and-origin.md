# UTA-0008 — `ubundle`: the `.utab` container, its origin field and its version

**Status:** spec draft (2026-09-08).
**Kind:** implement.
**Source:** ROADMAP UTA-0008 (design-2026-09-03).

**Blocked by:** UTA-0006 and UTA-0007 — this serialises the model types they
own.
**Blocker for:** UTA-0011 (`ubake` has nothing to write a bake into),
UTA-0013 (the quarantine guard's third check reads the origin field), and
UTA-0016 (`ut-ants` loads a bundle).

**Layman:** our own file format for a finished level — the box everything a
baked map contains is packed into, and the one field that records whether any
of it came out of somebody's copy of Unreal Tournament.

## 1. Goal

A baked level can be written to a single `.utab` file and read back with every
value bit-identical, on GCC, Clang and MSVC alike. Any tool — including a git
hook that must not link the engine — can read the first sixteen bytes of that
file and learn whether its content derives from an Unreal Tournament install.
A file that has been truncated, corrupted or hand-edited is refused with a
reason, never parsed into a structure the runtime then walks off the end of.

## 2. Problem

Three subsystems have finished halves that cannot meet.

1. **`umap` and `unav` produce models nothing can store.**
   `uta::umap::RoomMap`, `uta::unav::NavGraph` and `uta::unav::WiringGraph`
   are built at bake time and read at runtime, and there is no bake time and
   runtime to be either side of — nothing writes them to a file.
   `docs/specs/UTA-0007-room-partition-and-lookup.md` § 9 lists *"Serialising
   `RoomMap`"* as out of its own scope and names `ubundle` as the owner;
   `docs/specs/UTA-0006-navigation-and-wiring-graphs.md`'s header block names
   UTA-0008 as the item it blocks.

2. **A lifetime rule already exists for a consumer that does not.** The
   header comment on `src/unav/Graphs.h` says a graph owns its strings and
   outlives the `Package` it was built from, *"because `ubundle` serialises it
   after that package may be closed"* — and `src/umap/Rooms.h` says the same
   of a `RoomMap` and its `Model`. Both types already pay for a requirement
   nothing yet imposes.

3. **The quarantine guard cannot be finished.** `docs/design.md` rule 15's
   third check is *"no tracked `.utab` outside `content/` whose origin is not
   `authored`"*, and `docs/specs/UTA-0002-core-foundations.md` § 8 records
   that check as waiting on the origin field. Until the field exists, the
   guard enforces two of its three rules, and the one it cannot enforce is
   the one standing between this repository and publishing Epic's content.

## 3. Scope decisions (agreed with the user)

1. **This item defines the envelope *and* the sections for the model types
   that exist today** — `RoomMap`, `NavGraph` and `WiringGraph`. Sections for
   content nobody produces yet (geometry, materials, collision, lights, baked
   indirect light, entity placements, and everything a character bundle
   holds) are defined by the items that produce them. **User, 2026-09-08.**
   The alternatives offered were an envelope-only item and specifying the
   whole format at once; § 8 records why each lost.

2. **Nothing is compressed in version 1, and each section carries a
   compression byte anyway.** **User, 2026-09-08.** The byte is *not* a
   forward-compatibility device — § 14 explains why version gating makes that
   impossible — and it is kept on a narrower ground the user's decision still
   supports: it makes compression a per-section property, so the choice to
   compress large geometry and leave a small graph raw is already expressible
   when compression arrives.

3. **One format version covers both the framing and the section payloads.**
   Mine, following `docs/design.md` rule 17, which assigns *both* kinds of
   change to *"a bundle-format version bump"*: adding a room attribute is
   `umap`'s change, reframing a section is `ubundle`'s, and each bumps the
   same number. § 8 records the per-section version that lost to it.

4. **`Derived` is the zero value of the origin enum, not `Authored`.** Mine,
   following `docs/design.md` rule 15's own fail-closed reasoning: *"an
   unrecognised package withheld costs a player a map, and an unrecognised
   package sent is the breach ADR-0006 § Decision forbids."* A byte that has
   been zeroed by a partial write therefore reads as the restrictive value,
   and the failure it produces is a bundle wrongly withheld rather than one
   wrongly published.

## 4. Design

### 4.1 What `ubundle` is, and what it may link

A new static library `uta_ubundle` from `src/ubundle/`, holding
`Bundle.h`/`Bundle.cpp`. It links `uta_core` for `Result`/`Error`,
`uta_umap` for `RoomMap`, and `uta_unav` for the two graphs — and nothing
else.

It must **not** link `uta_upkg`. `docs/design.md` rule 2 keeps the package
reader out of every runtime target, and `ut-ants` links this library in order
to load a bundle (UTA-0016). It must also link `uta_umap` and `uta_unav`
rather than `uta_umap_build` and `uta_unav_build`: those two are the bake-side
libraries, and `src/umap/CMakeLists.txt` and `src/unav/CMakeLists.txt` each
already assert that they reach `uta_upkg`.

This library performs no file I/O. `read` takes a span of bytes the caller
owns and `write` returns a vector; opening and writing files is `uta::fs`'s,
which is what keeps this library testable without a filesystem.

`src/CMakeLists.txt` gains `add_subdirectory(ubundle)` after `umap`, matching
that file's stated rule that the order is the order items land in.

### 4.2 Primitive encoding

Every multi-byte value is **little-endian**, on every platform. All three
supported compilers target little-endian hardware, and fixing it in the format
rather than following the host is what stops a big-endian port silently
producing files nothing else can read.

| Type | Encoding |
|---|---|
| `u8`, `u16`, `u32`, `u64` | unsigned, little-endian |
| `i32` | two's complement, little-endian |
| `f32` | IEEE-754 `binary32`, little-endian, moved through `std::bit_cast<std::uint32_t>` |
| `string` | `u32` byte length, then exactly that many bytes of UTF-8; no terminator |
| `vector<T>` | `u32` element count, then that many encodings of `T` |

Three rules govern every read, and they are the whole of this format's
robustness argument.

1. **No count or length read from the file sizes an allocation before it has
   been checked against the bytes actually remaining in its section.** For a
   fixed-width element of `S` bytes the check is `count <= remaining / S`; for
   a variable-width element it is `count <= remaining / minimumEncodedSize`,
   the smallest number of bytes one element can occupy. Division, never
   multiplication — `count * S` is the overflow this rule exists to avoid.
   This is `UTA-0003`'s INV-2 restated for this reader; a four-byte edit to a
   count field otherwise asks the allocator for gigabytes.

2. **Every read is bounded by its section's span**, never by the file. A
   section that claims more bytes than it was given fails inside its own span
   rather than reading a neighbour's.

3. **Nothing is `memcpy`d from or into a struct.** Every field is encoded
   individually, in the order its table below gives. A struct copy would
   write whatever the compiler left in the padding, which makes the output
   non-deterministic between compilers and leaks whatever was on the stack.

The minimum encoded sizes each element type contributes, derived from the
layouts in §§ 4.6–4.8:

| Element | Minimum bytes |
|---|---|
| `Point2` | 8 (fixed) |
| `Point3` | 12 (fixed) |
| `Footprint` | 8 |
| `Room` | 20 |
| `RoomMap::Node` | 34 (fixed) |
| `NavNode` | 16 |
| `NavEdge` | 25 (fixed) |
| `WiringNode` | 24 |
| `WiringEdge` | 12 |
| `DanglingEvent` | 8 |

### 4.3 The header

Sixteen bytes, at offset 0.

| Offset | Size | Field | Value |
|---|---|---|---|
| 0 | 4 | `magic` | the bytes `U`, `T`, `A`, `B` — `0x55 0x54 0x41 0x42` |
| 4 | 4 | `formatVersion` | `u32`, `1` in this version |
| 8 | 1 | `origin` | `u8`, § 4.5 |
| 9 | 1 | `kind` | `u8`, `0` = map, `1` = character |
| 10 | 2 | `reserved` | `u16`, must be `0` |
| 12 | 4 | `sectionCount` | `u32` |

The magic is spelled in ASCII so a hex dump of a bundle names itself, which is
what `ut-dump` and a bug report both need first.

`kind` is present now, with no character sections defined, because
`docs/design.md` § The bundle makes one container serve a baked map and an
authored character, and adding the field later would bump the format version.
`docs/standards/versioning-overrides.md` § Breaking surfaces states what that
costs: *"A change here invalidates every cached bake — expected, and it must be
announced, because on a big rotation it is a long wait."* One byte now is
cheaper than that wait.

The section table begins immediately at offset 16. There is no table-offset
field, because a field whose value is always 16 is a field that can be wrong.

### 4.4 The section table

`sectionCount` descriptors of 24 bytes each, starting at offset 16.

| Offset | Size | Field | Value |
|---|---|---|---|
| 0 | 4 | `id` | four bytes, § 4.6–4.8 |
| 4 | 8 | `offset` | `u64`, from the start of the file |
| 12 | 8 | `size` | `u64`, payload bytes |
| 20 | 1 | `compression` | `u8`, `0` = none; version 1 defines no other value |
| 21 | 3 | `reserved` | three bytes, must be `0` |

`id` is a **byte sequence and not an integer**, so there is no endianness to
get wrong and a hex dump reads it left to right.

Five rules on the table, all checked before any payload is read:

- `16 + 24 * sectionCount` must not exceed the file size — checked before the
  table itself is read, by the § 4.2 division rule.
- Descriptors are in **ascending `offset` order**. This makes the overlap
  check below a single linear pass, and it makes `write`'s output ordering
  fixed rather than incidental.
- No id appears twice.
- Every section's `[offset, offset + size)` lies within the file, computed so
  that `offset + size` cannot overflow, and begins at or after
  `16 + 24 * sectionCount`.
- No two sections overlap.

A section whose id this version does not define is **refused**, not skipped.
Under § 4.3's exact version match there is no such thing as a file from a
later version that this reader should tolerate, so an unknown id means a
corrupt or hand-edited file.

Sections are individually optional: a map bundle carries no character
sections, and a bundle baked from a level with no navigation points carries no
`NAVG`. Absence is not an error, and it is distinct from a present-but-empty
section — an empty `NavGraph` says the level was examined and had none.

### 4.5 The origin field

```cpp
enum class Origin : std::uint8_t {
    Derived  = 0,  ///< something out of somebody's UT install contributed
    Authored = 1,  ///< nothing did
};

/// The most restrictive of two origins. Derived wins.
[[nodiscard]] constexpr Origin combine(Origin a, Origin b) noexcept;
```

Any byte other than `0` or `1` in the header's `origin` field is
`MalformedData`. It is never defaulted to a value — a file whose origin cannot
be read has no origin, and the distinction between *unreadable* and *derived*
is what lets a caller log the difference even though both fail closed.

`combine` lives here, and not in each tool that writes a bundle, because
`docs/design.md` rule 15 says the field is *"inherited from the most
restrictive input"* and two implementations of "most restrictive" are two
implementations that will one day disagree. `ubake` and `ued` decide **what
the inputs are**; this decides what their combination is.

`readHeader` exists as a separate entry point for one caller: the quarantine
guard of UTA-0013, which runs inside a git hook over every tracked `.utab` and
must answer one question without decoding a level. It reads the first sixteen
bytes and stops.

**The guard's failure direction is fixed here rather than left to it.** Any
result other than a successfully read header carrying `Origin::Authored` means
*not authored* — a short file, a bad magic, an unsupported version, an
unrecognised origin byte, an I/O error. Rule 15 permits a tracked `.utab`
outside `content/` only when its origin *is* `authored`, so the refusal path
and the `derived` path lead to the same place, and a guard that treats an
unreadable file as publishable has inverted the rule it enforces.

### 4.6 The `ROOM` section — `uta::umap::RoomMap`

Id `R`, `O`, `O`, `M`. Payload, in this order:

```
rooms        vector<Room>
bands        vector<f32>
roomForZone  vector<u32>
nodes        vector<Node>
leafZone     vector<u8>
```

`Room`:

```
zoneIndex  u32
parts      vector<Footprint>
minZ       f32
maxZ       f32
floors     vector<u16>
```

`Footprint`:

```
outer  vector<Point2>
holes  vector<vector<Point2>>
```

`Point2` is `x` then `y`, each `f32`. `Point3` is `x`, `y`, `z`.

`Node`, matching the nested `RoomMap::Node` in `src/umap/Rooms.h`:

```
normal    Point3
w         f32
iFront    i32
iBack     i32
iLeaf[0]  i32
iLeaf[1]  i32
iZone[0]  u8
iZone[1]  u8
```

**The order of the four pairs above is the whole hazard of this section, and
it is written out element by element for that reason.** `iFront` precedes
`iBack`; `iLeaf[0]` and `iZone[0]` are the **back** side, index 1 the front,
per the engine's own convention as recorded on `RoomMap::Node::iLeaf` in
`src/umap/Rooms.h`. UTA-0078 was exactly this defect one layer down — `upkg`
read a BSP node's front and back children the wrong way round — and
`docs/specs/UTA-0069-model-bsp-tables.md` § 4.5 records why nothing caught it:
a parse-success check cannot see two adjacent same-width fields swapped.
§ 5's INV-6 and INV-7 are this section's answer to that.

Rooms with no footprint are encoded like any other, with `parts` empty;
UTA-0007 § 4.4 produces them and `uui` draws them as empty.

### 4.7 The `NAVG` section — `uta::unav::NavGraph`

Id `N`, `A`, `V`, `G`. Payload:

```
nodes               vector<NavNode>
edges               vector<NavEdge>
discardedEndpoints  u32
```

`NavNode`:

```
exportIndex  u32
className    string
firstEdge    u32
edgeCount    u32
```

`NavEdge`:

```
from             u32
to               u32
distance         i32
collisionRadius  i32
collisionHeight  i32
reachFlags       i32
pruned           u8
```

`collisionRadius` precedes `collisionHeight`, and both are `i32` — the second
same-width adjacent pair in this format, and INV-6 covers it with the first.

`pruned` is stored as the file's own byte rather than a bit, because
`src/unav/Graphs.h` records that nothing has measured it to be only ever 0 or
1, and UTA-0006 § 4.5's reasoning is that `ubundle` fixing a one-bit field for
a byte of unmeasured range is how a value gets silently lost.

### 4.8 The `WIRG` section — `uta::unav::WiringGraph`

Id `W`, `I`, `R`, `G`. Payload:

```
nodes     vector<WiringNode>
edges     vector<WiringEdge>
incoming  vector<WiringEdge>
dangling  vector<DanglingEvent>
```

`WiringNode`:

```
exportIndex     u32
tag             string
firstOutgoing   u32
outgoingCount   u32
firstIncoming   u32
incomingCount   u32
```

`WiringEdge` is `from` `u32`, `to` `u32`, `event` `string`.
`DanglingEvent` is `from` `u32`, `event` `string`.

`incoming` holds the same edges as `edges` in a second order, and is written
out in full rather than rebuilt on load. Whether that second order earns its
storage is UTA-0006 § 14's open question, and it is `unav`'s to answer:
`docs/design.md` rule 17 gives this library the bytes of those types and not
their meaning, so deciding the field is redundant would be deciding something
that is not ours. § 15 records what changes here if `unav` later drops it.

### 4.9 Validation after decoding

A section that decodes without running out of bytes can still describe a
structure whose own spec forbids it — a node run reaching past the edge
vector, a zone mapping to a room that does not exist. **Every check below runs
before `read` returns, and a failure is `MalformedData`.**

The alternative is validating in the accessors, and `UTA-0003`'s INV-6
records what that costs: a caller that indexes a table directly gets an
unchecked value. `uta::unav::edgesFrom` returns a `std::span` built from
`firstEdge` and `edgeCount`, so a run reaching past `edges` is undefined
behaviour in the consumer and not a wrong answer this library can be blamed
for later.

**`ROOM`**, against `docs/specs/UTA-0007-room-partition-and-lookup.md`:

- every `roomForZone[i]` is `NO_ROOM` or less than `rooms.size()`;
- `roomForZone` is either **empty** or has `NO_ROOM` at index 0. Empty is a
  real state and not a defect: UTA-0007 § 6 records two maps in the install
  whose zone table is empty, and says of them that *"`roomForZone` is empty"*
  and that the result is a valid `RoomMap` with no rooms. Where it is
  non-empty, § 4.2 there makes index 0 present and always `NO_ROOM`. A rule
  requiring it non-empty would refuse a bundle those two maps legitimately
  produce;
- every `Room::zoneIndex` is non-zero (§ 4.3 there);
- every `Room::floors` is non-empty, and every entry is less than
  `bands.size()`;
- `bands` is in ascending order;
- every `leafZone[i]` is `ZONE_REFUSED` or less than `roomForZone.size()`;
- every `Node::iFront`, `iBack` is `INDEX_NONE` or a valid index into `nodes`;
- every `Node::iLeaf[k]` is `INDEX_NONE` or a valid index into `leafZone`.

**`NAVG`**, against `docs/specs/UTA-0006-navigation-and-wiring-graphs.md`:

- `nodes` is in strictly ascending `exportIndex` order — `nodeOf` relies on
  it, and an unsorted table makes it return another actor's edges rather than
  fail;
- for every node, `firstEdge + edgeCount <= edges.size()`, computed so it
  cannot overflow;
- every edge in node `i`'s run has `from == i`, which is what
  `src/unav/Graphs.h` means by *"grouped by `from`: one contiguous run per
  node"*;
- every edge's `from` and `to` are less than `nodes.size()`.

**`WIRG`**: the same four, applied to `edges` grouped by `from` through
`firstOutgoing`/`outgoingCount` and to `incoming` grouped by `to` through
`firstIncoming`/`incomingCount`; plus `incoming.size() == edges.size()`, and
every `DanglingEvent::from` less than `nodes.size()`.

That size equality is weaker than the real invariant, which is that `incoming`
is a permutation of `edges`. § 10 records it as partial rather than claiming
the check it is not.

### 4.10 The API

```cpp
namespace uta::ubundle {

inline constexpr std::uint32_t FORMAT_VERSION = 1;

enum class BundleKind : std::uint8_t { Map = 0, Character = 1 };

struct BundleHeader {
    std::uint32_t formatVersion = FORMAT_VERSION;
    Origin       origin = Origin::Derived;
    BundleKind   kind = BundleKind::Map;
};

/// A bundle's contents. A section absent from the file is an empty optional,
/// which is distinct from a present but empty one.
struct Bundle {
    BundleHeader                       header;
    std::optional<umap::RoomMap>       rooms;
    std::optional<unav::NavGraph>      nav;
    std::optional<unav::WiringGraph>   wiring;
};

/// Total: every input returns. Never throws, never reads outside `bytes`.
[[nodiscard]] Result<Bundle> read(std::span<const std::byte> bytes);

[[nodiscard]] Result<std::vector<std::byte>> write(const Bundle& bundle);

/// The header alone, from the first 16 bytes -- UTA-0013's entry point.
[[nodiscard]] Result<BundleHeader> readHeader(std::span<const std::byte> bytes);

}  // namespace uta::ubundle
```

`write` emits sections in the fixed order `ROOM`, `NAVG`, `WIRG`, omitting
absent ones. Fixed rather than incidental because `docs/design.md` § Close
calls requires a bundle no other tool wrote to be *"named by the hash of its
own contents"*, and a hash over an incidentally-ordered file names one world
two things.

## 5. Invariants

- **INV-1** — `read` and `readHeader` are total: for any input bytes they
  return a `Result` and never throw, terminate, or read outside the caller's
  span.
  *Test:* `tests/unit/BundleMalformedTest.cpp`, over a corpus of truncations
  at every byte offset of a valid bundle, and of headers whose counts, offsets
  and sizes lie. No arrow: the surface does not exist yet.
  *Breaks when:* a length or offset from the file is used before it is
  checked. **The fixture isolates this rule and not the allocator's:** each
  case is a *short* file whose declared extents exceed it, so the only rule
  that can reject it is the § 4.2 bounds check — a build with that check
  removed reads past the span rather than failing some earlier test.

- **INV-2** — No allocation is sized by a value read from the file before
  that value has been checked against the bytes remaining in its section.
  *Test:* `tests/unit/BundleMalformedTest.cpp` decodes a bundle whose `rooms`
  count is `0xFFFFFFFF` in a section of a few dozen bytes, and asserts an
  `Error` rather than an allocation. No arrow: the surface does not exist yet.
  *Breaks when:* a vector is reserved from the count before the count is
  validated, or the check multiplies rather than divides and overflows.

- **INV-3** — A `Bundle` returned by a successful `read` satisfies every
  structural rule § 4.9 lists. No span built from a returned node's run
  reaches past its vector, and no index stored in one of its tables is out of
  range for the table it names.
  *Test:* `tests/unit/BundleMalformedTest.cpp`, one case per bullet in § 4.9,
  each a bundle that decodes cleanly and violates exactly that rule. No arrow:
  the surface does not exist yet.
  *Breaks when:* validation is deferred to the accessors — `edgesFrom` then
  builds a span past the end of `edges` for a run the file declared and
  nothing checked.

- **INV-4** — A header whose `magic` is not `U`,`T`,`A`,`B` is
  `MalformedData`; one whose `formatVersion` is not `1` is
  `UnsupportedVersion`. Neither is read further.
  *Test:* `tests/unit/BundleFormatTest.cpp`. No arrow: the surface does not
  exist yet.
  *Breaks when:* the version is checked after the section table is read, or
  only a lower bound is checked — a later version's table then parses as
  garbage rather than being refused.

- **INV-5** — An `origin` byte other than `0` or `1` is `MalformedData`, and
  is never defaulted to either value.
  *Test:* `tests/unit/BundleFormatTest.cpp` reads headers carrying `2` and
  `0xFF` and asserts the error. No arrow: the surface does not exist yet.
  *Breaks when:* the byte is cast to the enum without a range check, which
  makes an unreadable origin indistinguishable from a declared one — and under
  § 4.5 the guard must be able to fail closed on the difference.

- **INV-6** — A hand-authored golden byte array, in which every field of a
  `Node`, a `NavEdge` and a `WiringNode` holds a value distinct from every
  other field's, decodes to exactly those values, field by field.
  *Test:* `tests/unit/BundleFormatTest.cpp`, asserting each field against its
  own literal. No arrow: the surface does not exist yet.
  *Breaks when:* two adjacent same-width fields are swapped in the reader —
  `iFront` with `iBack`, `iLeaf[0]` with `iLeaf[1]`, `iZone[0]` with
  `iZone[1]`, `collisionRadius` with `collisionHeight`, `minZ` with `maxZ`, a
  `Point2`'s `x` with its `y`. **A round-trip test cannot break this and that
  is why the fixture is authored by hand:** a swap present in both the writer
  and the reader round-trips perfectly. The golden bytes are written from
  § 4.6–4.8 rather than produced by `write`, so they are an independent
  statement of the layout. This is the UTA-0078 class, recorded in
  `docs/specs/UTA-0069-model-bsp-tables.md` § 4.5.

- **INV-7** — `write` applied to the structure INV-6 decodes produces exactly
  INV-6's golden bytes.
  *Test:* `tests/unit/BundleFormatTest.cpp`. No arrow: the surface does not
  exist yet.
  *Breaks when:* the same swap exists in the writer. **The fixture isolates
  the writer specifically:** INV-6 grades the reader against the golden bytes
  and this grades the writer against them, so neither can be satisfied by a
  compensating error in the other.

- **INV-8** — `write` is deterministic: the same `Bundle` produces
  byte-identical output, within a run and across the three compilers.
  *Test:* `tests/unit/BundleFormatTest.cpp` encodes one bundle twice and
  compares; the cross-compiler half is the CI matrix running the same
  assertion. No arrow: the surface does not exist yet.
  *Breaks when:* a struct is `memcpy`d, so padding bytes reach the file; or a
  container with unspecified iteration order is written in that order.
  Determinism is what lets `docs/design.md` § Close calls name a bundle by the
  hash of its contents.

- **INV-9** — Every `f32` written is recovered bit-identically, including
  negative zero, both infinities, a quiet NaN and a subnormal.
  *Test:* `tests/unit/BundleFormatTest.cpp`, comparing
  `std::bit_cast<std::uint32_t>` of each value before and after. No arrow: the
  surface does not exist yet.
  *Breaks when:* a float is converted through a wider type, or compared with
  `==` instead of by bit pattern — under which `-0.0` reads as preserved when
  it has been replaced by `+0.0`, the case
  `docs/specs/UTA-0049-numeric-contract.md`'s INV-5 exists for.

- **INV-10** — `uta_ubundle`'s link entries are exactly `uta_core`,
  `uta_umap` and `uta_unav`.
  *Test:* `src/ubundle/CMakeLists.txt`, a configure-time property assertion in
  the form `src/umap/CMakeLists.txt` already uses for UTA-0007's INV-5.
  *Breaks when:* a convenience dependency on `uta_upkg`, `uta_umap_build` or
  `uta_unav_build` is added — which is how `docs/design.md` rule 2's boundary
  stops being checkable, since `ut-ants` links this library.

- **INV-11** — Reading a bundle that declares a section id this version does
  not define, or two sections with the same id, or two whose byte ranges
  overlap, is `MalformedData`.
  *Test:* `tests/unit/BundleMalformedTest.cpp`, one case each. No arrow: the
  surface does not exist yet.
  *Breaks when:* the table is trusted and each section is decoded from its own
  descriptor without a pass over the whole table first. Overlapping sections
  otherwise decode without error and one of them is wrong.

## 6. Failure modes

| When | What happens |
|---|---|
| Fewer than 16 bytes | `MalformedData`; `readHeader` fails the same way |
| `magic` wrong | `MalformedData`, before anything else is read (INV-4) |
| `formatVersion` != 1 | `UnsupportedVersion`, before the table is read (INV-4) |
| `origin` not 0 or 1 | `MalformedData` (INV-5); callers treat it as not authored (§ 4.5) |
| `kind` not 0 or 1 | `MalformedData` — an undefined kind names sections this version cannot know |
| Either `reserved` non-zero | `MalformedData`; it is the only thing that makes a reserved field a contract |
| `sectionCount` overruns the file | `MalformedData`, checked before the table is read |
| Table not in ascending offset order | `MalformedData` |
| Section extent outside the file, or overlapping | `MalformedData` (INV-11) |
| Unknown or duplicated section id | `MalformedData` (INV-11) |
| `compression` non-zero | `UnsupportedVersion` — the byte is defined and its value is not |
| A count exceeds its section's remaining bytes | `MalformedData`, before allocating (INV-2) |
| A section ends with bytes unread | `MalformedData` — trailing bytes mean the layout was misread, not that there is slack |
| A decoded structure violates § 4.9 | `MalformedData` (INV-3) |
| `write` given a `Bundle` whose structures violate § 4.9 | `InvalidArgument` — refused rather than written, so a bad bundle cannot be produced and then blamed on the reader |

## 7. Tests

Both files join the existing `uta_unit_tests` executable in
`tests/CMakeLists.txt` with the `unit;fast` label, as UTA-0005's tests do. The
executable gains `uta_ubundle` as a link library.

| File | Locks |
|---|---|
| `tests/unit/BundleFormatTest.cpp` | INV-4, INV-5, INV-6, INV-7, INV-8, INV-9 |
| `tests/unit/BundleMalformedTest.cpp` | INV-1, INV-2, INV-3, INV-11 |
| `src/ubundle/CMakeLists.txt` | INV-10, at configure time |

**Every test is to be seen failing against pre-fix code before it is
believed.** This project's `CLAUDE.md` § Build and test records the reason
directly: on UTA-0007 three of four tier-1 cases passed on first writing for
the wrong reason, each decided by a rule other than the one it named. INV-6
and INV-7 are the cases most exposed to it — a golden fixture whose values are
not all distinct passes under a swap — so the fixture's values are asserted
distinct as part of writing it.

**No test name contains a comma.** Catch2 treats one as a filter separator, so
a name carrying one silently matches nothing when run by name.

`BundleMalformedTest.cpp` is also the file worth running under
AddressSanitizer, per `CLAUDE.md` § Build and test's separate build directory:
INV-1 and INV-2 are bounds properties, and removing the check they name
produces undefined behaviour rather than a wrong answer, which a plain test
cannot grade.

## 8. Alternatives considered (and rejected)

- **Envelope only — define the framing and no section payloads.** Offered to
  the user 2026-09-08 and rejected by them. It matches the roadmap headline's
  wording most literally, and it leaves *"serialising `RoomMap`"* — which
  UTA-0007 § 9 has already assigned to `ubundle` — belonging to no roadmap
  item at all.

- **Define every section the format will ever carry.** Offered and rejected in
  the same exchange. Geometry, materials, lights and entity placements have no
  producer yet — `umat` is UTA-0009 and UTA-0010, the baker is UTA-0011 — so
  their layouts would be invented against nothing and rewritten on contact.

- **A version per section, instead of one for the file.** Rejected because
  `docs/design.md` rule 17 assigns both kinds of change to *"a bundle-format
  version bump"*: a room attribute is `umap`'s change and reframing a section
  is `ubundle`'s, and rule 17 gives them the same number. A second version
  space would also make which combinations of section versions anyone has
  tested a real question, for a format whose files are regenerated by
  re-baking.

- **Reuse `upkg`'s `ByteReader`.** Rejected on `docs/design.md` rule 2:
  `ut-ants` links this library, so it cannot reach `uta_upkg`. The two
  encodings share nothing beyond little-endian integers — `upkg` decodes UE1's
  compact indices and this format has none — so the duplication is a few
  fixed-width reads, and the two are free to change independently.

- **Rebuild `WiringGraph::incoming` on load instead of storing it.** It would
  halve that section. Rejected because deciding the field is redundant is
  deciding what the type means, which `docs/design.md` rule 17 gives to
  `unav`; UTA-0006 § 14 already holds the question.

- **A per-section checksum.** Rejected as inert. Under § 14's exact version
  match a reader refuses any file it was not built for, so a reserved field
  buys no forward compatibility, and integrity against accidental corruption
  is not needed while bundles do not travel (`docs/design.md` rule 15). § 15
  records when it becomes real. Note that a checksum would in any case not be
  a defence against a hostile file — that is INV-1 and INV-2's job, and a
  sender who corrupts a bundle can recompute a checksum over it.

## 9. Out of scope

- **Baking a level into a bundle** — `ubake`, UTA-0011.
- **Sections for geometry, materials, collision, lights, baked indirect light
  and entity placements** — each defined by the item that produces it;
  UTA-0009 and UTA-0010 for materials, UTA-0011 for the rest.
- **Character sections** — mesh, skeleton, skins and attachment points; the
  `kind` byte reserves the distinction and nothing more. Deferred; not yet
  queued.
- **The quarantine guard itself** — UTA-0013. This provides `readHeader` and
  fixes the failure direction; the guard is that item's.
- **Naming a bundle file** — `docs/design.md` § Close calls gives the rule to
  `ubake` and `ued`, which share a content-tool version this library does not
  see.
- **File I/O** — `uta::fs`, UTA-0002.

## 10. What checks this

| Rule | What catches a breach |
|---|---|
| INV-1 | `tests/unit/BundleMalformedTest.cpp`, strengthened by an AddressSanitizer run per § 7. `scripts/ci.sh`'s sanitizer leg is ThreadSanitizer, which finds races and not out-of-span reads, so the ASan run is by hand |
| INV-2 | `tests/unit/BundleMalformedTest.cpp`. **Partial:** the test bounds the failure, not the ordering — that the error arrives *before* the allocation is not observable from outside |
| INV-3 | `tests/unit/BundleMalformedTest.cpp`, one case per § 4.9 bullet |
| INV-4 | `tests/unit/BundleFormatTest.cpp` |
| INV-5 | `tests/unit/BundleFormatTest.cpp` |
| INV-6 | `tests/unit/BundleFormatTest.cpp`, against hand-authored bytes |
| INV-7 | `tests/unit/BundleFormatTest.cpp`, against the same bytes |
| INV-8 | `tests/unit/BundleFormatTest.cpp` for the within-run half; the CI matrix for the cross-compiler half |
| INV-9 | `tests/unit/BundleFormatTest.cpp` |
| INV-10 | `src/ubundle/CMakeLists.txt`, at configure time |
| INV-11 | `tests/unit/BundleMalformedTest.cpp` |
| `incoming` is a permutation of `edges` (§ 4.9) | **Partial:** only the size equality is checked. A permutation check is `O(E log E)` on every load and the wrong half of the trade while `unav` still holds UTA-0006 § 14's question about whether the field survives |
| The format version is one of the baker's inputs | **nothing** — `ubake` does not exist; `docs/design.md` § Close calls states it and UTA-0011 owns it |
| A `.utab` outside `content/` is `authored` | **nothing** — this provides `readHeader` and fixes § 4.5's failure direction; the check itself is UTA-0013's and is tracked there |

## 11. Cross-doc impact

- `CHANGELOG.md` — an `### Added` entry for the bundle format.
- `README.md` — no change; the format is not user-facing until `ut-bake` is.
- `docs/specs/UTA-0006-navigation-and-wiring-graphs.md` and
  `docs/specs/UTA-0007-room-partition-and-lookup.md` — no edit needed. Each
  already names `ubundle` as the owner of its types' bytes, and this document
  is what that pointer now resolves to.
- `docs/design.md` — no change. Rules 15 and 17 are implemented here, not
  amended.
- `CLAUDE.md` — no change.

## 12. Cold-eyes loop log

Rows live in `../reviews/UTA-0008-bundle-container-and-origin-loop-log.md`.

## 13. Resource cost

`read` holds the caller's bytes and the structures it decodes from them, so
peak memory is roughly twice the file size for the duration of the call. There
is no cache, no eviction and no state held between calls.

The bound on that allocation is the file itself: § 4.2's division rule means
no count can license more elements than its section has bytes to hold, so a
file of *N* bytes cannot cause more than *O(N)* allocation. There is no
separate size cap, and none is needed — the caller chose to read the file.

No new external dependency. Nothing is added to the build beyond one static
library and two test files.

## 14. Migration / compatibility

This is a new format; there is no old data.

**A reader accepts `formatVersion == 1` and nothing else.** It does not accept
a range. Two things make an exact match right here and now, and one makes it
wrong later.

Right now: a bundle is a bake output, and
`docs/standards/versioning-overrides.md` § Breaking surfaces already treats a
format change as invalidating every cached bake, so there is nothing a
tolerant reader would save. And `docs/design.md` § Close calls makes the
format version one of the baker's own inputs, so a version change renames
every bundle anyway.

Later: an **authored** bundle is distributed and cannot be re-baked by whoever
holds it — `docs/design.md` rule 15 makes it the one thing that travels whole,
and `docs/standards/versioning-overrides.md` cuts **S6** at `0.6.0`. Exact
version matching means a format bump orphans every authored bundle in
circulation. § 15 carries this; nothing in `0.1.0` decides it.

This is also why § 3's compression byte is not described as a
forward-compatibility device. A version-2 file is refused by a version-1
reader whatever its section descriptors say, so no reserved field can make one
readable. The byte earns its place by making compression a per-section
property rather than a whole-file one.

## 15. Open questions

- **How a distributed authored bundle survives a format bump.** Exact version
  matching is correct for a bake cache and wrong for a file somebody else
  holds. The answer is a reader that accepts a range, a converter, or a rule
  that authored bundles are re-exported — and it is due before `0.6.0` ships
  **S6**, not before `0.1.0`. Not yet queued as a roadmap item.

- **Integrity checking, when bundles begin to travel.** § 8 rejects a checksum
  as inert today. It stops being inert at `0.4.0`'s content download and
  `0.6.0`'s authored bundles, where a file arrives over a network. The
  distinction to keep is that a checksum answers accidental corruption and
  never a hostile sender; INV-1 and INV-2 are what answer the second.

- **Whether `WiringGraph::incoming` is stored at all.** UTA-0006 § 14 holds
  the question. If `unav` drops the field, § 4.8 loses a vector and § 4.9
  loses two of its checks — a subtraction from this format, not a
  redesign of it.

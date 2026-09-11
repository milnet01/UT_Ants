# UTA-0110 — `ubake`: write the level's lights and actor placements into the bundle

**Status:** accepted (2026-09-11), at the review's cap; amended for UTA-0124
(§ 4.5 step 1, INV-4) and accepted again (2026-09-11), at that review's cap.
**Kind:** implement.
**Source:** ROADMAP UTA-0110 (user-request-2026-09-10, split out of
UTA-0011).

**Blocked by:** UTA-0011, shipped.
**Blocker for:** UTA-0014, which turns the lights into dynamic lights;
UTA-0023, which resolves each placed actor onto a class of ours; UTA-0119,
which bakes each mover's shape where this places it.
**Pairs with:** UTA-0104 (the map standard), UTA-0100 (the cost of
`effectiveDefaults`).

**Layman:** Carry each level's lamps, and where everything in it stands,
into the baked map.

## 1. Goal

A bake writes every actor the level places, and every light among them,
into two new sections. `PLAC` holds each placed actor's own settings and a
table of the classes they belong to, each with its ancestry and its merged
defaults. `LITE` holds each light's position and UT99's own light numbers,
already resolved from the actor and its class. The renderer lights a level
from `LITE`, and actor resolution works from `PLAC`, without either reading
a UT file.

## 2. Problem

1. **The bundle carries no actor.** `ubundle::Bundle` holds rooms, two
   graphs, textures, materials and geometry. `docs/design.md` § The bundle
   has a map carry *"lights"* and *"entity placements"* too.
2. **UTA-0014 has no light to draw.** Its body: *"The level's own light
   actors -- position, colour, brightness, radius, flicker -- become real
   dynamic lights"*.
3. **UTA-0023 needs each actor's class, ancestry and defaults in the
   bundle.** Its body: *"the bundle stores each placed actor's class name,
   ancestry and defaults"*, because resolution happens when an actor spawns,
   not at bake time. `ADR-0004` is why.
4. **`upkg` has no call that resolves an actor's class.** An actor's class
   is usually an import — `Engine.Light`, not a class of the map — and
   `upkg::readAncestry` starts from a class export. `unav`'s
   `descendsFromNavigationPoint` does the step itself, and its comment says
   *"`upkg` exposes no call for that resolution"*.

## 3. Scope decisions (agreed with the user)

1. **Lights keep UT99's own numbers.** *User, 2026-09-11.* The twelve byte
   fields and four bools of `Actor.uc`'s lighting group, resolved from the
   actor and its class. The renderer (UTA-0014) turns them into colour, as
   it does surface flags.
2. **Movers' shapes are UTA-0119's.** *User, 2026-09-11.* This item records
   where each mover stands and its settings, as it does any actor.
3. **Every placed actor is written, whatever its class.** Mine. Choosing
   which classes matter is resolution, which is UTA-0023's; the bake
   transcribes.
4. **A class is written once, and its actors refer to it.** Mine. A map
   places many actors of few classes — `unav`'s own comment on the same walk
   says *"thousands of actors of a few dozen classes"* — and a merged default
   set runs to hundreds of properties, so a copy per actor repeats one fact
   many times.
5. **An actor is keyed by its slot in the map's export table**, as `NAVG`
   and `WIRG` key their nodes (`unav::NavNode::exportIndex`). Mine. So a
   consumer joins a light, a placement and a navigation node on one number.
6. **A reference to another object is written as a text path.** Mine. A
   package's reference numbers mean nothing outside that package.

## 4. Design

### 4.1 The files

| File | Holds |
|---|---|
| `src/upkg/Class.h/.cpp` | `resolveClass` (§ 4.2) |
| `src/ubundle/Bundle.h` | the `PLAC` and `LITE` types, `Bundle::placements` and `Bundle::lights`, `FORMAT_VERSION` (§ 4.3, § 4.4) |
| `src/ubundle/Sections.h`, `PlacementSection.cpp`, `LightSection.cpp` | the two codecs and their validation |
| `src/ubake/Actors.h/.cpp` | `buildActors` (§ 4.5, § 4.6) |
| `src/ubake/Bake.cpp`, `src/ubake/Name.h` | the bake's new step, `BAKER_REVISION` (§ 4.7) |

`PlacementSection.cpp` and `LightSection.cpp` join `uta_ubundle`, and
`Actors.cpp` joins `uta_ubake`. No target or link changes.

### 4.2 Resolving an actor's class

```cpp
namespace uta::upkg {

/// Where a class reference leads, and what it is called either way.
struct ClassSite {
    std::string package;      ///< folded; the map's own name for a class it exports
    std::string name;         ///< the class's name as spelled where it is referenced
    ResolvedClass resolved;   ///< both null when the class was not found
    AncestryEnd end = AncestryEnd::Root;  ///< why `resolved` is null; Root when it is not
};

/// An export of `package` is that export. An import names its outermost
/// package; that name is folded, handed to `resolver`, and the class export
/// of that package whose name matches, compared case-insensitively, is it.
/// A package the resolver does not supply leaves `resolved` null with `end`
/// PackageMissing; a package holding no such class, with ClassMissing.
/// Neither is an error.
[[nodiscard]] Result<ClassSite> resolveClass(const Package& package,
                                             std::string_view packageName,
                                             ObjectReference classReference,
                                             const PackageResolver& resolver);

}  // namespace uta::upkg
```

`packageName` is the map's folded stem, used for a class the map exports.
The fold and the case-insensitive match are UTA-0005 § 4.6's rules. A class
export is one with a null class reference and serialised data, since
`readClass` refuses one with none.

**`unav` keeps its own step.** `descendsFromNavigationPoint` matches class
names exactly, so moving it onto `resolveClass` changes which actors are
navigation nodes. That is a behaviour change to `NAVG` and belongs to its
own change (§ 9).

### 4.3 A property value in a bundle

```cpp
namespace uta::ubundle {

enum class ValueKind : std::uint8_t {
    Byte = 0, Int = 1, Bool = 2, Float = 3, Object = 4, Class = 5,
    Name = 6, String = 7, Vector = 8, Rotator = 9, Raw = 10,
};

/// A value upkg carries through undecoded: a struct, an array, a map.
struct RawValue {
    std::uint8_t type = 0;           ///< upkg::PropertyType's value
    std::string structName;          ///< empty unless type is Struct
    std::vector<std::byte> bytes;
};

struct PropertyRecord {
    std::string name;                ///< as spelled where it was read
    std::uint32_t arrayIndex = 0;
    ValueKind kind = ValueKind::Byte;
    std::variant<std::uint8_t, std::int32_t, bool, float, std::string,
                 std::array<float, 3>, std::array<std::int32_t, 3>, RawValue> value;
};

}  // namespace uta::ubundle
```

| `kind` | `value` holds | Encoded as |
|---|---|---|
| `Byte` | `std::uint8_t` | `u8` |
| `Int` | `std::int32_t` | `i32` |
| `Bool` | `bool` | `u8`, `0` or `1` |
| `Float` | `float` | `f32` |
| `Object`, `Class` | `std::string`: the path of what it names, empty for null | `string` |
| `Name` | `std::string`: the name as spelled | `string` |
| `String` | `std::string` | `string` |
| `Vector` | `std::array<float, 3>`: x, y, z | three `f32` |
| `Rotator` | `std::array<std::int32_t, 3>`: pitch, yaw, roll | three `i32` |
| `Raw` | `RawValue` | `u8` type, `string` struct name, `vector<u8>` bytes |

A record is `string` name, `u32` array index, `u8` kind, then the value.
**Minimum encoded size: 10 bytes** — an empty name, the index, the kind and
a one-byte value.

**A `Raw` value's bytes are copied as read.** An object or name index inside
them stays relative to the package it came from and is not rewritten;
decoding them is UTA-0005 § 3.2's filed work.

**An object path** is the names from the outermost package down to the
object, joined by `.` and folded: `botpack.pulsegun` for an import, and
`<package>.<outers>.<name>` for an export, `<package>` being the folded name
of the package that holds it. That is the map's stem for the map, and for any
other package the name the resolver was asked for: `upkg::Package` carries
no name of its own, so `buildActors` records each package's name as its
resolver returns it. A class's path in § 4.4 takes the same form. It is
`umat::materialId`'s path form without the variant suffix (UTA-0009 § 4.6).

Both paths refuse, `read` as `MalformedData` and `write` as
`InvalidArgument`: a `kind` above `10`; a `Raw` type outside `1` to `15`,
the values `upkg::PropertyType` defines. The range is stated here rather
than read from `upkg`, which `ubundle` may not include (UTA-0008 INV-10).

**Two rules have one path each.** `read` alone refuses a `Bool` byte other
than `0` or `1`: a `bool` in memory holds no other value. `write` alone
refuses a `value` not holding the alternative its `kind` names: `read`
decodes the value from its kind, so it never produces one.

### 4.4 The `PLAC` and `LITE` sections

```cpp
namespace uta::ubundle {

inline constexpr std::uint32_t FORMAT_VERSION = 5;  // 5 since UTA-0110

enum class AncestryEnd : std::uint8_t { Root = 0, PackageMissing = 1, ClassMissing = 2 };

/// One class the level's actors belong to.
struct ActorClass {
    std::string path;                      ///< "<package>.<class>", folded -- its identity
    bool resolved = false;                 ///< the class itself was found
    std::vector<std::string> ancestry;     ///< its parents' paths, nearest first
    AncestryEnd end = AncestryEnd::Root;
    std::string missing;                   ///< § 4.5's rule; empty on Root
    std::vector<PropertyRecord> defaults;  ///< effective, merged up the chain
};

/// One placed actor.
struct ActorPlacement {
    std::uint32_t exportIndex = 0;         ///< its slot in the map's export table
    std::string path;                      ///< its own object path, § 4.3's form
    std::uint32_t classIndex = 0;          ///< into Placements::classes
    std::vector<PropertyRecord> properties;///< its own list, in file order
};

struct Placements {
    std::vector<ActorClass> classes;       ///< strictly ascending by path, bytewise
    std::vector<ActorPlacement> actors;    ///< strictly ascending by exportIndex
};

/// One light, resolved.
struct Light {
    std::uint32_t exportIndex = 0;
    std::array<float, 3> location{};
    std::array<std::int32_t, 3> rotation{};  ///< pitch, yaw, roll
    std::uint8_t type = 0, effect = 0, brightness = 0, hue = 0, saturation = 0,
                 radius = 0, period = 0, phase = 0, cone = 0,
                 volumeBrightness = 0, volumeRadius = 0, volumeFog = 0;
    bool specialLit = false, actorShadows = false, corona = false, lensFlare = false;
};

struct Bundle {
    // ... the existing members, then:
    std::optional<Placements> placements;
    std::optional<std::vector<Light>> lights;  ///< strictly ascending by exportIndex
};

}  // namespace uta::ubundle
```

**`PLAC`** is the bytes `P`, `L`, `A`, `C`. Its payload is `vector<ActorClass>`
then `vector<ActorPlacement>`. An `ActorClass` is its `path` as `string`,
`resolved` as `u8`, `ancestry` as `vector<string>`, `end` as `u8`, `missing`
as `string`, `defaults` as `vector<PropertyRecord>`. An `ActorPlacement` is
`exportIndex` as `u32`, `path` as `string`, `classIndex` as `u32`, then
`properties`. Minimum sizes: `ActorClass` 18, `ActorPlacement` 16.

**`LITE`** is the bytes `L`, `I`, `T`, `E`. Its payload is `vector<Light>`.
A `Light` is `exportIndex` as `u32`, `location` as three `f32`, `rotation`
as three `i32`, the twelve bytes in the order declared above as `u8`, then
the four bools as `u8`: 44 bytes, fixed.

**Validation**, `MalformedData` on `read` and `InvalidArgument` on `write`:

- `PLAC`: classes strictly ascending by `path`; actors strictly ascending by
  `exportIndex`; every `classIndex` less than `classes.size()`; an `end` byte
  above `2`; `missing` empty exactly when `end` is `Root`; `resolved` false
  only when `end` is not `Root`; on `read` alone, a `resolved` byte other
  than `0` or `1`; § 4.3's rules on every record.
- `LITE`: lights strictly ascending by `exportIndex`; on `read` alone, a bool
  byte other than `0` or `1`.

**The light bytes are not validated.** They are UT99's numbers, and a value
past the last `ELightType` is the renderer's to handle, as an unknown surface
flag is. `ubundle` does not check that a light's `exportIndex` has a
placement; the baker guarantees it (INV-9).

**`write` emits them last**: `ROOM`, `NAVG`, `WIRG`, `TEXS`, `MATS`, `GEOM`,
`PLAC`, `LITE`. **`FORMAT_VERSION` becomes `5`.**

### 4.5 Building the placements

```cpp
namespace uta::ubake {

struct Actors {
    ubundle::Placements placements;
    std::vector<ubundle::Light> lights;
};

/// The level's placed actors, their classes and their lights.
[[nodiscard]] Result<Actors> buildActors(const upkg::Package& map, std::string_view mapName,
                                         const upkg::Level& level,
                                         const upkg::PackageResolver& resolver);

}  // namespace uta::ubake
```

**For each entry of `level.actors`**, which holds the non-null slots only:

1. The reference must be an export of the map, or the bake is refused with
   `MalformedData` naming the slot. `Level::actors` comes from export data,
   which `Package::open` did not validate. **A slot naming an export an
   earlier slot named is skipped, so the actor is placed once** (UTA-0124).
   UT99's own maps carry such slots, CTF-November and DM-Grinder among them,
   and a placement keyed by its export carries nothing a second slot could
   add. **`BAKER_REVISION` does not move for it:** every map with a repeated
   slot was refused before, and a refused bake writes nothing a bake name
   could find, so no bundle written before changes.
2. Its own properties, by `upkg::readProperties`, each written as a
   `PropertyRecord` in file order. A refusal from `readProperties` refuses
   the bake, naming the actor.
3. Its class, by `resolveClass`.

**For each distinct class**, keyed by its folded path:

1. **Resolved:** `resolved` is true. `readAncestry`, then `effectiveDefaults`
   over it. `ancestry` is the chain's paths after the class itself, and `end`
   is `Ancestry::end`, so a class whose parent is missing says so here. The
   defaults are written sorted by folded name, then array index.
2. **Not resolved:** `resolved` is false, `ancestry` and `defaults` are
   empty, and `end` is `ClassSite::end`.

**`missing` is the folded package name on `PackageMissing`, and the class
name as spelled on `ClassMissing`** — from `Ancestry::missingPackage` or
`missingClass` on the resolved branch, and from `ClassSite` on the other.
It is empty on `Root`.

`effectiveDefaults` runs once per class, not per actor. UTA-0100 measured it
at about a millisecond per class, which is its *"not worth changing"*
branch.

An `Object` or `Class` value's path is built against the package it was read
from — the map for an actor's own list, `EffectiveProperty::origin` for a
default. A `Name` value is that package's name text.

### 4.6 Resolving a light

**An actor is a light when its resolved `LightType` is not `0`**
(`LT_None`). Any actor can light, not only a `Light`.

**Each field resolves in this order**: the actor's own property of that
name, at array index `0` and of the field's type; else its class's effective
default of that name and type; else `Actor.uc`'s default, which is `0` or
false for every one of the sixteen. `location` resolves the same way from
`Location`, and `rotation` from `Rotation`, both zero by default. A property
of the right name and the wrong type is passed over. Names are compared
folded.

The sixteen, with the property each reads:

| Field | Property | Type |
|---|---|---|
| `type` | `LightType` | byte |
| `effect` | `LightEffect` | byte |
| `brightness`, `hue`, `saturation` | `LightBrightness`, `LightHue`, `LightSaturation` | byte |
| `radius`, `period`, `phase`, `cone` | `LightRadius`, `LightPeriod`, `LightPhase`, `LightCone` | byte |
| `volumeBrightness`, `volumeRadius`, `volumeFog` | `VolumeBrightness`, `VolumeRadius`, `VolumeFog` | byte |
| `specialLit`, `actorShadows`, `corona`, `lensFlare` | `bSpecialLit`, `bActorShadows`, `bCorona`, `bLensFlare` | bool |

Source for the declarations and `Actor`'s zero defaults:
<https://github.com/Slipyx/UT99/blob/master/Engine/Actor.uc>, UT's 469b
scripts. `Light.uc` in the same tree sets `LightType=LT_Steady` and
`LightBrightness=64`, among others, which is what a class default carries.

### 4.7 The bake's step

`detail::bake` gains one step, after `GEOM`:

> **`PLAC` and `LITE`** are `buildActors` over the level. Its refusal refuses
> the bake, naming the map.

UTA-0119 § 4.6 has since moved this step ahead of the materials, because the
movers are found from it.

Both sections are written, and empty where the level places nothing.
**`BAKER_REVISION` becomes `3`**, and UTA-0011 INV-5's golden value is
recorded again under it.

### 4.8 As built (2026-09-11)

- **A slot naming a class export refuses the bake**, with `MalformedData`
  naming the slot, as a slot that is not an export does; § 4.5 does not name
  it. A slot naming an export a slot before it named refused the bake too,
  after the review's second loop asked about it, until UTA-0124 measured
  UT99's own maps carrying such slots; § 4.5 step 1 now skips it.
- **A property's object reference that leads outside its package's tables
  refuses the bake.** § 4.3 defines a path only for a reference that
  resolves, and `Package::open` does not validate property data.
- **`resolveClass` was written before INV-3's cases**, so those were not seen
  failing first. Mutation stands in: comparing class names exactly, passing
  the package name unfolded, and giving both unresolved cases one `end` each
  fail INV-3.
- **The builder sorts placements and lights by export index**, since a level
  lists its actors in its own order. No invariant's fixture differs from
  export order, so mutation could remove either sort with every test passing.
  A case in `tests/unit/BakeActorsTest.cpp` now hands the builder a level in
  reverse order.
- **The golden bake's case asserts it places one actor and one light**, so a
  bake that stopped writing either fails there rather than being re-recorded.
- **Mutation, by hand:** each mutation § 7 listed on that date was killed by the
  invariant naming it, and one mutation per INV-2 rule by that rule's case.
  UTA-0124 added the last two, which run with its change.
- **`buildActors` walks an export's outer chain itself**, beside `Bake.cpp`'s
  `exportPath`. Two copies of that walk now exist.
- **The real-asset case compiles and has not been run.** Its figures are for
  whoever runs it against an install.

## 5. Invariants

- **INV-1** — `PLAC` and `LITE` round-trip through `ubundle::write` and
  `ubundle::read`, every value kind and float bit included, and `write`
  emits them after `GEOM`, `PLAC` first.
  *Test:* `tests/unit/BundleActorsTest.cpp`, one record of every kind, a
  `Raw` struct value, a `-0.0` light location, in a bundle that also carries
  `GEOM`.
  *Breaks when:* a kind is encoded with another's width, or a section is
  emitted out of order.

- **INV-2** — `read` refuses with `MalformedData`, and `write` with
  `InvalidArgument`: two classes of one path; classes out of order; two
  actors of one slot; two actors out of order; a `classIndex` equal to
  `classes.size()`; an `end` byte of `3`; `missing` empty on
  `PackageMissing`; `missing` set on `Root`; `resolved` false on `Root`; a
  `kind` byte of `11`; a `Raw` type byte of `0`, and one of `16`; two lights
  of one slot; lights out of order. `read` alone refuses a `Bool` value
  byte, a `resolved` byte and a light bool byte of `2`. `write` alone refuses
  a `value` whose alternative is not its `kind`'s.
  *Test:* `tests/unit/BundleActorsTest.cpp`, one case per rule on each path
  the rule names, each fixture breaking that rule alone.
  *Breaks when:* a two-path rule is checked on one path only, or an unknown
  byte is read as a known value.

- **INV-3** — `resolveClass` finds a class the map exports; finds an
  imported class in the package the resolver supplies, whatever the case of
  the package's or the class's name; and returns an unresolved site whose
  `end` is `PackageMissing` where the resolver supplies no package, and
  `ClassMissing` where the package holds no such class.
  *Test:* `tests/unit/PackageClassTest.cpp`, four cases, each asserting `end`.
  *Breaks when:* the package name reaches the resolver unfolded, the class
  name is compared exactly, or the two unresolved cases share one `end`.

- **INV-4** — Every export `Level::actors` names gives one placement, however
  many slots name it, keyed by its slot in the map's export table, carrying
  its own object path and its own properties in file order.
  *Test:* `tests/unit/BakeActorsTest.cpp`, a map whose two actors, of
  different classes, sit at export indices other than their positions in
  `Level::actors`, the second carrying two properties. The test asserts each
  placement's export index exactly. A second case names one actor in two
  slots, around another's, and asserts two placements and one light each.
  *Breaks when:* the key is the position in `Level::actors`, the path is not
  the actor's own, the actor's list is reordered, or a repeated slot refuses
  the bake or places its actor twice.

- **INV-5** — Actors of one class share one class entry, which carries the
  class's parents nearest first and its defaults merged over them.
  *Test:* `tests/unit/BakeActorsTest.cpp`: two actors of a map-exported class
  whose parent, in another package, has a parent of its own. The parent sets
  a default the child overrides and one it does not; the grandparent sets
  one neither overrides.
  *Breaks when:* a class is written per actor, the chain is written root
  first, or a default is the parent's where the child overrides it.

- **INV-6** — An actor whose class's package the resolver does not supply
  gets a class entry with `resolved` false, `end` `PackageMissing` and
  `missing` the folded package name. One whose package holds no such class
  gets `resolved` false, `ClassMissing` and the class name. Both have no
  ancestry and no defaults. A class that resolves while its parent does not
  gets `resolved` true, with its parent's `end` and `missing`. The bake is
  not refused.
  *Test:* `tests/unit/BakeActorsTest.cpp`: an actor of `NoSuchPkg.Thing`; one
  of `ActorPkg.NoSuchThing` where `ActorPkg` is installed; and one of a
  map-exported class whose parent is `NoSuchPkg.Base`.
  *Breaks when:* the bake is refused, two of the three entries are written
  alike, or an entry names nothing.

- **INV-7** — An actor is a light exactly when its resolved `LightType` is not
  `0`, and each of its sixteen fields is its own value, else its class's
  default, else zero.
  *Test:* `tests/unit/BakeActorsTest.cpp`: an actor of a class whose default
  `LightType` is `1` and `LightBrightness` `64`, overriding `LightHue` and
  `LightBrightness`; an actor of a class with no light defaults, setting
  `LightType` itself; and one setting nothing. The first two are lights with
  those values and the rest zero; the third is not a light.
  *Breaks when:* only the actor's own list is read, only the class's
  defaults are, or a class name decides what is a light.

- **INV-8** — An object value is written as the folded path of what it
  names, an import's from its outermost package and an export's from the
  package that holds it; a null reference as an empty string; a name as
  spelled.
  *Test:* `tests/unit/BakeActorsTest.cpp`: an actor carrying an imported
  texture, a map-exported texture in a group, a null object and a `Tag`;
  and a class default, inherited from a class in `ActorPkg`, naming an
  export of `ActorPkg`, which must read `actorpkg.<its name>`.
  *Breaks when:* a path stops at the immediate outer, keeps the file's
  reference number, or gives another package's export the map's stem.

- **INV-9** — Every light's `exportIndex` is a placement's, and an actor slot
  naming an import, or an export past the table, refuses the bake with
  `MalformedData` naming the slot.
  *Test:* `tests/unit/BakeActorsTest.cpp`.
  *Breaks when:* a light is written for an actor that has no placement, or a
  bad slot is read unchecked.

## 6. Failure modes

| When | What happens |
|---|---|
| An actor slot is not an export of the map | The bake is refused, naming the slot |
| An actor slot names an export an earlier slot named | The slot is skipped; the actor is placed once |
| An actor's properties do not read | The bake is refused, naming the actor |
| An actor's class's package is not installed | Its class entry says `PackageMissing` and names it; the bake goes on |
| The package opens without the class | `ClassMissing`, naming the class |
| `readAncestry` refuses — a cycle, a chain too deep | The bake is refused |
| A light property carries another type | It is passed over, and the next source is used |
| The level places nothing | `PLAC` and `LITE` are written empty |

## 7. Tests

**Unit, on every CI leg:** `tests/unit/BundleActorsTest.cpp` for INV-1 and
INV-2; `tests/unit/PackageClassTest.cpp` for INV-3;
`tests/unit/BakeActorsTest.cpp` for INV-4, INV-5, INV-6, INV-7, INV-8 and
INV-9. Each is seen failing before the code it locks exists.

**The fixtures grow.** `MapBuilder` in `tests/unit/BakeFixture.h` gains an
actor carrying a property list, and `classPackage` a class with a parent and
defaults. UTA-0011 INV-5's golden bake then covers `PLAC` and `LITE`.

**Real-asset tier, local only:** a new file, `tests/real/RealActorsTest.cpp`,
runs `buildActors` over every map and prints actors placed, distinct classes,
classes ending `PackageMissing` and `ClassMissing`, lights, maps refused by
reason, and the maps with a repeated slot and how many each skips. It asserts
that every light's `exportIndex` has a placement.

**Mutation, by hand** (`CLAUDE.md` § Build and test): read only the actor's
own list for a light; read only the class's defaults; write a class per
actor; write the chain root first; key an actor by its position in
`Level::actors`; compare a class name exactly; stop an object path at the
immediate outer; read a `Bool` byte of `2` as true; refuse a repeated slot;
place a repeated slot's actor twice. Each must be killed by the invariant that
names it.

## 8. Alternatives considered (and rejected)

- **Colours converted at bake time.** The user chose UT99's numbers (§ 3
  decision 1): a change to how they become colour would otherwise re-bake
  every map.
- **Only the classes this engine will implement.** Which classes matter is
  resolution, UTA-0023's, which runs at spawn so a better answer needs no
  re-bake (ADR-0004).
- **A class's own defaults and its chain, merged at spawn.** Every consumer
  would repeat UTA-0005's merge, and the merge needs the packages the bundle
  exists to replace.
- **Lights left inside `PLAC` for the renderer to resolve.** The renderer
  would carry the ancestry merge too. `LITE` is the resolved view, and
  `PLAC` stays the source it was resolved from.
- **Location and rotation as fields of every placement.** They are already
  in the actor's properties or its class's defaults; a field beside them is
  a second copy that can disagree. `LITE` resolves them for the one consumer
  that needs them now.
- **Moving `unav` onto `resolveClass` here.** It changes which actors are
  navigation nodes (§ 4.2); a `NAVG` change belongs to its own item.

## 9. Out of scope

- What each actor becomes in the game — tracked by UTA-0023.
- Turning light numbers into colour, and drawing lights — tracked by
  UTA-0014, which draws with UTA-0112 § 4.3's model.
- Movers' shapes — tracked by UTA-0119.
- `unav`'s class match moving to `resolveClass`, with its case rule —
  deferred; not yet queued.
- Decoding struct, array and map values — UTA-0005 § 3.2's call, unchanged.
- The project's own standard for what a map carries — tracked by UTA-0104.

## 10. What checks this

| Rule | What catches a breach |
|---|---|
| INV-1, INV-2 | `tests/unit/BundleActorsTest.cpp`, a unit test |
| INV-3 | `tests/unit/PackageClassTest.cpp`, a unit test |
| INV-4, INV-5, INV-6, INV-7, INV-8, INV-9 | `tests/unit/BakeActorsTest.cpp`, a unit test |
| `BAKER_REVISION` covering the actors | **Partial:** `tests/unit/BakeGoldenTest.cpp`, a golden-hash test, catches what its fixture places |
| The sixteen light fields being UT99's | **nothing** — the tests use the names the code uses, and the names rest on the cited script |
| Every light having a placement on real maps | **Partial:** `tests/real/RealActorsTest.cpp`, a real-asset test; no CI leg runs it |

## 11. Cross-doc impact

- `docs/specs/UTA-0008-bundle-container-and-origin.md`, in the same change
  as the code: § 4.2's minimum-size table gains `PropertyRecord`,
  `ActorClass`, `ActorPlacement` and `Light`; § 4.3 and § 4.4 gain version
  `5`, `PLAC` and `LITE`; § 4.10's API and order clause gain both; INV-4 is
  annotated with version `5`.
- `docs/specs/UTA-0011-map-baker.md` — § 4.5 gains the step, § 4.3's
  `BAKER_REVISION` moves to `3`. Recorded when built.
- `docs/specs/UTA-0005-class-tables-and-ancestry.md` — a pointer to
  `resolveClass`, the call its § 4.6 rules now have.
- `CHANGELOG.md` — an `### Added` entry, and a `### Changed` entry for format
  version `5`.
- ROADMAP UTA-0100 — a note that the bake calls `effectiveDefaults` per class.

## 12. Cold-eyes loop log

Rows live in `../reviews/UTA-0110-lights-and-placements-loop-log.md`.

## 13. Resource cost

- No new target and no new dependency.
- A class's merged defaults are written once however many actors it has.
- `effectiveDefaults` once per distinct class; the real-asset case prints
  the total time.

## 14. Migration / compatibility

**No `.utab` exists that version `5` orphans**: `0.1.0` has not been cut. A
version-`4` file is refused and baked over (UTA-0011 § 4.7).

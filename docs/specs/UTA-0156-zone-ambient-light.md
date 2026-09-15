# UTA-0156 — zone ambient light

**Status:** spec draft (2026-09-15). No review unless the user asks, by standing instruction.
**Kind:** fix.
**Source:** ROADMAP UTA-0156 (user-request-2026-09-14).

**Layman:** each area of a map gets the background light its author set for
it, so dark corners are as bright as in the original game.

## 1. Goal

A bake writes each zone's `AmbientBrightness`, `AmbientHue` and
`AmbientSaturation` into a new `ZONE` section, and tags every drawn vertex with
its zone. The renderer adds that zone's ambient light to every lit surface, so
a map whose author set ambient light draws as its author lit it.

## 2. Problem

1. `ZoneInfo`'s three ambient properties are in no bundle section.
   `docs/specs/UTA-0014-vulkan-draw-path.md` § 2 consequence 2 records it, and
   its § Out of scope defers them. The renderer may not read a package, so it
   has nothing to apply.
2. Most maps set them. `ut-ants-uta0156/ambient-census/` runs `ubake::buildActors`
   over every map in the reference install and resolves the three properties on
   each actor descending from `ZoneInfo` (`build/ambient-census <install>`, output
   in `census.txt`). About half the maps set a non-zero `AmbientBrightness` on
   at least one zone, always on the actor itself.
3. UE1 applies ambient per zone, and a zone with no `ZoneInfo` uses the level's
   `LevelInfo`. `ULevel::GetZoneActor` in the 469 SDK's `Engine/Inc/UnLevel.h`
   returns `Model->Zones[iZone].ZoneActor`, else `GetLevelInfo()`.
   SurrealEngine's `LightmapBuilder::SetAmbientLight` seeds every lightmap texel
   with that actor's ambient colour before adding the lights, and its own
   comment marks the scale as unverified.
4. Baked probes cannot carry it alone. `shaders/probes.glsl::indirectAt` gives
   zero where no corner of the lattice cell has a probe, so ambient put only in
   probes would leave those cells dark.

## 3. Scope decisions (agreed with the user)

- **Cheapest method that still looks modern, first iteration** — the user,
  2026-09-14. So ambient is a flat term per zone, with no bounce.
- **How it looks is decided by measurement, never by asking the user to
  compare** — the user's standing instruction. § 7's measurement is where.
- **Zone ambient stays in UTA-0156 rather than being split** — this spec's
  call, from § 2 consequence 2.
- **The bundle carries UT99's bytes and the renderer converts them** — this
  spec's call. UTA-0014 § 3 decision 5 writes the light model once, in GLSL,
  and `LITE` already carries a light's bytes the same way.

## 4. Design

### 4.1 The `ZONE` section — `ubundle`

```cpp
namespace uta::ubundle {

inline constexpr std::uint32_t FORMAT_VERSION = 11; // 11 since UTA-0156
inline constexpr std::size_t ZONE_LIMIT = 64;       ///< umap::ZONE_CEILING

/// One zone's ambient light, as its ZoneInfo (or the LevelInfo) sets it.
struct ZoneAmbient {
    std::uint8_t brightness = 0;
    std::uint8_t hue = 0;
    std::uint8_t saturation = 0;
};

struct Bundle {
    // ... every existing member, unchanged, then:
    /// Index i is the source Model's zone i -- UTA-0156.
    std::optional<std::vector<ZoneAmbient>> zones;
};

}  // namespace uta::ubundle
```

On the wire: a `u32` count, then each entry's `brightness`, `hue` and
`saturation` as `u8`. `write` emits `ZONE` after `LPRB`, extending the fixed
order `ubundle::write` documents. The codec lives in a new
`src/ubundle/ZoneSection.cpp`, declared in `Sections.h`.

**Validation**, `MalformedData` on `read` and `InvalidArgument` on `write`: a
count of `0`, or above `ZONE_LIMIT`.

### 4.2 A vertex's zone — `ubundle`

```cpp
struct GeometryVertex {
    // ... position, normal, u, v, unchanged, then:
    std::uint8_t zone = 0;                  ///< an index into ZONE
    std::array<std::uint8_t, 3> reserved{}; ///< always zero
};
```

`reserved` makes the padding explicit, so the vertex bytes the renderer uploads
and hashes are defined. On the wire a vertex is UTA-0109 § 4.2's fields, then
`zone` as one `u8`; `reserved` is not written and reads as zero.

**Validation**, same codes, over the level's `Geometry` and every
`MoverShape::geometry`:

- a vertex `zone` not below `zones->size()` when `ZONE` is present;
- a non-zero vertex `zone` when `ZONE` is absent;
- on `write` only, a non-zero `reserved` byte.

### 4.3 The bake — `ubake`

A new `src/ubake/Zones.{h,cpp}`:

```cpp
namespace uta::ubake {

/// One entry per zone of `model`, and one when it has none.
[[nodiscard]] std::vector<ubundle::ZoneAmbient> buildZones(const upkg::Model& model,
                                                          const ubundle::Placements& placements);

}  // namespace uta::ubake
```

**An entry's actor.** Zone `i`'s `zoneActor`, when it is an export reference
naming a placement's `exportIndex`. Otherwise the level's `LevelInfo`: the
lowest-`exportIndex` placement whose class path is `engine.levelinfo` or whose
ancestry contains it. With neither, the entry is zero. A model with no zones
gets one entry, taken from the `LevelInfo`.

**An entry's values.** `ambientbrightness`, `ambienthue` and
`ambientsaturation`, each resolved as a byte by `detail::resolvedRecord`: the
actor's own record, else its class's default, else `0`.

**Where it runs.** `bake` calls it after step 5's `buildActors`, and writes the
result as `Bundle::zones`.

**A level vertex's zone.** `buildGeometry` gains the zone count and gives every
vertex of node `n` the value `n.iZone[1]` — the zone on the plane's front, the
side the surface faces (`umap::RoomMap::Node`'s convention) — or `0` where that
is not below the count.

**A mover vertex's zone.** `bake`'s step 9 gives every vertex of a
`MoverShape` the `zoneIndex` of the room `umap::roomAt` finds at its placed
`location`, or `0` where it finds `NO_ROOM` or the index is not below the count.

`ubake` static-asserts `ubundle::ZONE_LIMIT == umap::ZONE_CEILING`.
`BAKER_REVISION` becomes `14`.

### 4.4 The renderer — `urender`

`ShaderTypes.h` and `types.glsl` gain:

```cpp
enum Binding : std::uint32_t {
    // ... FRAME through SHADOW_FACES, unchanged, then:
    ZONES = 10,
    SHADOW_ATLAS = 11,
    TEXTURES = 12, ///< last: it is the variable-count binding
};

struct Zone {                   // std430, 16 bytes, offsets asserted
    std::uint32_t brightness;   // 0
    std::uint32_t hue;          // 4
    std::uint32_t saturation;   // 8
    std::uint32_t reserved;     // 12
};
```

- **Upload.** One `Zone` per `ZONE` entry. A bundle without `ZONE` uploads one
  zero entry, so binding `10` is never empty. `ZONES` sits with the other
  storage buffers, which `Pipelines::create` lays out as one run from `FRAME`.
- **Vertex input.** The scene pipeline gains attribute `3`, `R8_UINT`, at
  `offsetof(GeometryVertex, zone)`. `scene.vert` passes it to `scene.frag` as a
  `flat` `uint`.
- **The ambient term**, in `shaders/light.glsl` beside `lightAt`:

  ```glsl
  const float AMBIENT_SCALE = 2.5; // set by § 7's measurement

  vec3 zoneAmbient(Zone zone) {
      return lightColour(zone.hue, zone.saturation)
           * (float(zone.brightness) / 255.0 * AMBIENT_SCALE);
  }
  ```

  It is UTA-0112 § 4.3's colour and intensity with no falloff, incidence, spot,
  shadow or flicker.
- **Shading.** A lit surface shows `base × (direct + indirect + ambient)`. A
  `PF_Unlit` or `PF_FakeBackdrop` surface is unchanged.

The shadow pipeline does not read `zone`.

## 5. Invariants

- **INV-1** — `ZONE` round-trips entries with § 4.1's bytes, and refuses a
  count of `0` and of `65` on both `read` and `write`.
  *Test:* `tests/unit/BundleZonesTest.cpp`, new.
  *Breaks when:* the three bytes are written in another order; either count is
  accepted.

- **INV-2** — a vertex's `zone` round-trips in `GEOM` and in a `MOVR` shape.
  `read` and `write` refuse a zone equal to the `ZONE` count, a non-zero zone
  with `ZONE` absent, and, on `write`, a non-zero `reserved` byte.
  *Test:* `tests/unit/BundleGeometryTest.cpp` and
  `tests/unit/BundleMoversTest.cpp`, extended.
  *Breaks when:* the mover geometry is not checked; the bound is `ZONE_LIMIT`
  rather than the section's count.

- **INV-3** — `buildZones` over a Model with three zones: zone 1 names a
  `ZoneInfo` placement setting `AmbientBrightness` `40`; zone 2 names a
  placement setting nothing, whose class default brightness is `7`; zone 0
  names no actor, and the `LevelInfo` sets `90`. The entries' brightness is
  `90`, `40` and `7`. With the `LevelInfo` removed, entry 0 is zero; a Model
  with no zones gives one entry of `90`.
  *Test:* `tests/unit/BakeZonesTest.cpp`, new.
  *Breaks when:* a null `zoneActor` gives zero instead of the `LevelInfo`'s
  value; class defaults are ignored; entries shift by one.

- **INV-4** — `buildGeometry` gives the vertices of a node whose `iZone` is
  `{2, 3}` zone `3`, and those of a node whose `iZone[1]` is not below the
  zone count zone `0`.
  *Test:* `tests/unit/BakeGeometryTest.cpp`, extended.
  *Breaks when:* `iZone[0]` is used, or an out-of-range zone is written.

- **INV-5** — a mover placed inside a room gets that room's `zoneIndex` on
  every vertex; one placed where `roomAt` finds `NO_ROOM` gets `0`.
  *Test:* `tests/unit/BakeMoversTest.cpp`, extended.
  *Breaks when:* the mover's pivot-space origin is looked up instead of its
  placed `location`.

- **INV-6** — with no lights and no probes, a lit surface of base colour white
  in a zone of brightness `40`, hue `0` and saturation `255` draws
  `AMBIENT_SCALE × 40 / 255` linear under `linearOutput`. At brightness `0` it
  draws `0`. A `PF_Unlit` surface in the same zone draws its base colour.
  *Test:* `tests/device/RenderLightingTest.cpp`, a new case.
  *Breaks when:* ambient is not added; it is added to an unlit surface; the
  vertex attribute or binding `10` is miswired, so every surface reads zone 0.

- **INV-7** — `gpu::Zone`'s offsets and the `Binding` numbers match
  `types.glsl` and `scene_bindings.glsl`.
  *Test:* the `static_assert`s in `src/urender/ShaderTypes.h`, which
  UTA-0014 INV-9 already holds for every shared struct.
  *Breaks when:* a field or binding moves in one file only.

- **INV-8** — the golden bake is re-recorded under `BAKER_REVISION` `14`.
  *Test:* `tests/unit/BakeGoldenTest.cpp`.
  *Breaks when:* what the baker writes changes with no bump, or the bump lands
  without re-recording.

## 6. Failure modes

- **A node's front side is not its surface's side** on some map. Its surface
  takes the zone behind it. § 7's probe measures this before the code.
- **A mover that travels between zones** keeps its starting zone's ambient.
  Accepted for the first iteration.
- **`AMBIENT_SCALE` is wrong.** Zones with ambient draw uniformly too bright or
  too dark. § 7's measurement sets it.
- **A `zoneActor` naming an actor that is not a `ZoneInfo`** has its values
  read anyway, as `GetZoneActor` would.
- **Meshes drawn later** (UTA-0159) get no ambient from this item.
- **A bundle from before this item** is refused by its format version (§ 13).

## 7. Tests

All carry the `unit` label but INV-6, which carries `device`.

- INV-1 — `tests/unit/BundleZonesTest.cpp`, new.
- INV-2 — `tests/unit/BundleGeometryTest.cpp` and
  `tests/unit/BundleMoversTest.cpp`, extended.
- INV-3 — `tests/unit/BakeZonesTest.cpp`, new.
- INV-4 — `tests/unit/BakeGeometryTest.cpp`, extended.
- INV-5 — `tests/unit/BakeMoversTest.cpp`, extended.
- INV-6 — `tests/device/RenderLightingTest.cpp`, extended.
- INV-7 — `src/urender/ShaderTypes.h`'s `static_assert`s.
- INV-8 — `tests/unit/BakeGoldenTest.cpp`, re-recorded.

Each extended test is seen to fail against the code before this item.

**Before the code: probe the front-side rule.** A scratch program beside
`ut-ants-uta0156/ambient-census/` compares, for every drawn node of every map
in the reference install, `iZone[1]` against the zone `roomAt` finds one unit
in front of the polygon's centroid along its surface normal. Where they
disagree on more than a stray node per map, § 4.3 switches to that descent and
this spec is amended first.

**Measurement, not asserted.** After the code lands:

1. Capture AS-Frigate with `ut-ants-uta0156/capture-original.sh`. Its
   `LevelInfo` sets ambient, per `census.txt`.
2. Bake it, and run `ut-ants-uta0156/compare.py` against those frames.
3. Sweep `AMBIENT_SCALE` shader-only, with `EXPOSURE` held. Keep `1.0` unless
   another value lowers the block RMS.
4. Re-run DM-Deck16][. Its zones set no ambient, so its block RMS must not
   move.

## 8. Alternatives considered (and rejected)

- **Ambient in the light probes only.** Lost: § 2 consequence 4.
- **Bake each vertex's ambient as linear RGB.** Lost: `AMBIENT_SCALE` would be
  fixed at bake time, so § 7's sweep would need a bake per value.
- **Put the ambient table in `ROOM`.** Lost: `umap::RoomMap` answers which
  room a point is in, and lighting data does not belong to it.
- **Walk `ROOM`'s tree per pixel for the zone.** Lost: a tree descent per
  fragment, for a value that is fixed per surface.
- **Split `GEOM` batches by zone.** Lost: it breaks UTA-0109's batch order and
  multiplies draws.
- **Descend from each polygon's centroid at bake.** Kept in reserve: a lookup
  per node for what `iZone[1]` already states. § 7's probe decides.

## 9. Out of scope

- Ambient on meshes — tracked by UTA-0159.
- An actor's own `AmbientGlow` — deferred; not yet queued.
- Ambient light bouncing into the probes — deferred; not yet queued.
- Zone fog — tracked by UTA-0015, which renames `ZoneAmbient` to `Zone`, adds
  its fog flag at format `12`, and moves `zoneAt` to `ubundle`.
- Sky zones — tracked by UTA-0163.

## 10. What checks this

| Rule | What catches a breach |
|------|----------------------|
| INV-1 | `tests/unit/BundleZonesTest.cpp` |
| INV-2 | `tests/unit/BundleGeometryTest.cpp`, `tests/unit/BundleMoversTest.cpp` |
| INV-3 | `tests/unit/BakeZonesTest.cpp` |
| INV-4 | `tests/unit/BakeGeometryTest.cpp` |
| INV-5 | `tests/unit/BakeMoversTest.cpp` |
| INV-6 | `tests/device/RenderLightingTest.cpp` |
| INV-7 | `src/urender/ShaderTypes.h`'s `static_assert`s |
| INV-8 | `tests/unit/BakeGoldenTest.cpp` |
| The front-side rule holds on real maps | **nothing** — § 7's probe is run by hand, once |
| How ambient looks against the original | **nothing** — § 7's measurement is run by hand |

## 11. Cross-doc impact

- `docs/specs/UTA-0008-bundle-container-and-origin.md` — the section order.
- `docs/specs/UTA-0109-map-geometry.md` § 4.2 — the vertex record.
- `docs/specs/UTA-0014-vulkan-draw-path.md` § 2 and § Out of scope — zone
  ambient is no longer deferred; the bindings.
- `docs/specs/UTA-0112-baked-light-probes.md` § Out of scope — the same.
- `CLAUDE.md` § Where this project is — the bundle format version.
- `CHANGELOG.md`.

## 12. Cold-eyes loop log

Rows live in `../reviews/UTA-0156-zone-ambient-light-loop-log.md`.

## 13. Migration / compatibility

`FORMAT_VERSION` becomes `11`. `ubundle::read` refuses any other version, so
every map baked before this item must be baked again.

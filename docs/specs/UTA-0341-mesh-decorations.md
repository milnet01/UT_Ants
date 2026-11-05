<!-- ants-spec-format: 1 -->
# UTA-0341 — mesh decorations: draw the props a map places as models

**Status:** accepted (2026-11-05), unreviewed. Contract reviews are cancelled
for this project (`CLAUDE.md` § Contract reviews); no review runs unless the
user asks.
**Kind:** feature.
**Source:** ROADMAP UTA-0341 (in-session-2026-10-09).
**Builds on:** UTA-0110 (`docs/specs/UTA-0110-lights-and-placements.md`):
PLAC's actors and class defaults say which props a map places. UTA-0119
(`docs/specs/UTA-0119-mover-shapes.md`): a shape drawn as a mover. UTA-0256
(`docs/specs/UTA-0256-added-lamps.md`): a recipe lamp's fitting, which may
name a mesh decoration and is owed its drawing here.

**Layman:** pipes, barrels, lamps and other props that a map places as 3D
models are drawn, lit by the room's lights.

## 1. Goal

Every prop a map places as a model is drawn where UT99 draws it, in its own
textures, lit by the lights and bounce light around it. A recipe lamp whose
fitting names such a prop draws that model with the lamp.

## 2. Problem

UT99 draws two kinds of scenery: the level's brushes, which the bake turns
into GEOM (UTA-0109), and actors drawn as a mesh. Nothing in this project
reads a mesh, so the second kind is missing. UTA-0320 found it on DM-Fetid,
whose ceiling pipe is a `Botpack.Pipe` actor. UTA-0256 already accepts a
recipe lamp naming such an actor as its fitting, and checks it, but draws
nothing (`isMeshDecoration`, `src/ubake/Lamps.cpp`).

**Census, 2026-11-05**: a scratch program over every map in the reference
install's `Maps` folder (1611), reading each map's actors through
`buildActors` and `detail::resolvedRecord`. 1235 maps place at least one
decoration drawn as a mesh, 138,399 in all, naming 1556 distinct meshes;
467 of them have `Mesh` None. One map's actors did not read. The most
placed meshes are UnrealShare's `WoodenBoxM` (108 faces), `SeaweedM` (48)
and Botpack's `lightboxM` (8). The most props on one map: MH-NigutarV1
3291, MH-Vale_Evelyn 2975, MH-GiranTownSBFix2 2945. DM-Fetid places 20.
Summing stock meshes' face counts: MH-Vale_Evelyn's props are some 333,000
triangles, MH-NigutarV1's some 98,000.

## 3. Scope decisions

1. **Cheap methods that still look modern** (user, 2026-09-14, `CLAUDE.md`
   § Standing facts).
2. **Taken by this spec, 2026-11-05**, each recorded here because the
   roadmap body left them open:
   - **Decorations only.** A placed actor descending from
     `engine.decoration`. Pickups and weapons are UTA-0019's, monsters and
     players UTA-0159's: each moves, animates or is picked up, which a
     baked shape cannot do.
   - **Still.** Each prop is drawn in one pose, the frame its
     `AnimSequence` and `AnimFrame` name. Animated props (UTA-0055's flags
     and banners) stand still until an item animates them.
   - **Lit as a mover is lit.** Through the clustered lights and the
     probes, with no baked charts (§ 8).
   - **Drawn at every tier.** A prop is the map's own content, as a brush
     is.
   - **Blocks nothing.** Collision for props is UTA-0377's.

## 4. Design

### 4.1 Which actors are props

The baker draws a placed actor when all of these hold, each property read
through `detail::resolvedRecord` (own value, then class defaults, then
UT99's zero):

1. its class resolves and descends from `engine.decoration`;
2. `DrawType` is 2, `DT_Mesh`;
3. `bHidden` is false;
4. `Mesh` names a `LodMesh` export that reads (§ 4.2). The lookup passes
   over a class export of the same name: SpVale's `SPWheel` is both.

An actor failing item 4 is skipped and named in the bake report's
`skippedProps`, with the reason. It never refuses the map.

### 4.2 Reading a LodMesh

A new reader, `upkg::readLodMesh` (`src/upkg/Mesh.h`), reads a `LodMesh`
export, or a `Mesh` (UTA-0450, below). It refuses any other class with a
named reason, and refuses an export it does not consume to the last byte.

The layout, after the export's property list:

1. `BoundingBox` (6 floats and a byte) and `BoundingSphere` (4 floats).
2. `Verts`: a lazy array (an int32 skip offset, then a compact-index count)
   of uint32. Each packs X in bits 0 to 10, Y in 11 to 21 and Z in 22 to
   31, each a signed two's-complement field.
3. `Tris`, a lazy array of 20-byte records; `AnimSeqs`, each a name, a
   group name, an int32 start frame, an int32 frame count, a notify array
   (each a float and a name) and a float rate; `Connects`, a lazy array of
   8-byte records; a second bounding box and sphere; `VertLinks`, a lazy
   array of int32; `Textures`, an array of object references;
   `BoundingBoxes` (25 bytes each) and `BoundingSpheres` (16 bytes each).
4. `FrameVerts` and `AnimFrames`, int32; `ANDFlags` and `ORFlags`, uint32;
   `Scale` and `Origin`, 3 floats each; `RotOrigin`, 3 int32; `CurPoly` and
   `CurVertex`, int32; `TextureLOD`, an array of float.
5. The LOD tables: `CollapsePointThus` and `FaceLevel`, arrays of uint16;
   `Faces`, each three uint16 wedge indices and a uint16 material index;
   `CollapseWedgeThus`, an array of uint16; `Wedges`, each a uint16 vertex
   index and two bytes U and V; `Materials`, each a uint32 PolyFlags and an
   int32 texture index; `SpecialFaces`, as `Faces`.
6. Nine 32-bit values, then `RemapAnchors`, an array of uint16.

`Verts` holds `FrameVerts` times `AnimFrames` entries, frame by frame.

**Measured 2026-11-05**: a scratch parser of this layout consumed every
`LodMesh` export of `System/Botpack.u`, `UnrealShare.u` and `UnrealI.u`
(537) to its last byte, and in each `Verts` held `FrameVerts` times
`AnimFrames` entries. Decoding `Botpack.pipeM`'s 62 vertices gives exactly
its stored bounding box. `Tris` was empty in all 537.

A LodMesh reads its triangles from `Faces`. Only full detail is drawn: the
LOD tables beyond `Faces` and `Wedges` are stepped over.

**The older `Mesh` class** (UTA-0450, amended 2026-11-05) is items 1 to 4
alone: it ends after `TextureLOD`, and keeps its triangles in `Tris`. Each
`Tris` record is three uint16 vertex indices, three U and V byte pairs, a
uint32 PolyFlags and an int32 texture slot. The reader turns them into the
same faces, wedges and materials: one wedge per distinct vertex and
texture coordinate, one material per distinct PolyFlags and slot, in
first-seen order. Measured 2026-11-05: this layout consumed every `Mesh`
export of the install's `System` packages (751) to its last byte; six,
smoke and spark effects, hold no triangles.

### 4.3 Placing a prop

The frame drawn is frame 0, unless `AnimSequence` names one of the mesh's
`AnimSeqs`. Then it is that sequence's start frame plus
`floor(AnimFrame * frame count)`, clamped to the sequence.

Each vertex `V` of that frame goes to the world as UT99's `UMesh::GetFrame`
places it, through UTA-0119's rotation order (`Y * P * R`, as `rotationOf`
in `src/urender/Placement.cpp` builds it), with the baker's portable sine
(`sineOfUnits`, `src/ubake/LightModel.h`) so every compiler bakes the same
bytes:

```
world = Location + PrePivot
      + R(Rotation) * R(RotOrigin) * ((V - Origin) * Scale * DrawScale)
```

`*` on two vectors is per component. Where `Scale * DrawScale` has an odd
number of negative components, the mirror flips each triangle's winding
back.

A face's three wedges give its corners and their texture coordinates,
`u = U / 256`, `v = V / 256`. Each corner's normal is the area-weighted sum
of the frame's face normals at its vertex, turned and normalised as the
positions are. A face's own normal, `(b - a) x (c - a)`, points out of the
mesh: measured on `barrel2M` (140 of 140 faces outward from its centre) and
`pipebendM` (180 of 180). The baker writes each triangle in GEOM's winding
for that outward normal.

Each vertex's `zone` is the zone holding the actor's `Location`.

### 4.4 Textures and flags

A face's material `m` takes its picture from, in order: `MultiSkins[i]`,
`Skin`, the mesh's `Textures[i]`, then the actor's `Texture`, where `i` is
`m`'s texture index. The first non-null is used; with none, the face is
dropped.

The MATS id is `umat::materialId` of that texture, as a BSP surface's is,
so a picture both use is one material. The bake resolves a texture named by
path through a new `resolveTextureByPath` beside `detail::resolveTexture`,
then makes it with `makeVariant` as a surface's.

A skin no level surface wears is enlarged at most `PROP_UPSCALE`, 2, each
way, against a surface's 4 (`ubundle::MAX_UPSCALE_FACTOR`). Measured
2026-11-05: at 4, MH-Vale_Evelyn's prop skins took its texture working set
from 711,698,376 bytes to 1,180,221,888, past the 1 GiB budget, so the map
would no longer open without the player asking it to shrink. A skin a
surface also wears keeps the surface's size.

A face's polyFlags are its material's `PolyFlags`, plus:

- `Style` 3 (`STY_Masked`) adds `PF_MASKED`; 4 (`STY_Translucent`) adds
  `PF_TRANSLUCENT`; 5 (`STY_Modulated`) adds `PF_MODULATED`;
- `bUnlit` adds `PF_UNLIT`.

The renderer treats these flags as it treats a BSP surface's.

### 4.5 The bundle

A new section, `PROP`, written after `VEGE`, holds one `Geometry` in world
space, under GEOM's rules (UTA-0109): every placed prop's triangles,
batched by material and polyFlags. A map with no prop writes no `PROP`.
`FORMAT_VERSION` becomes 36 and `BAKER_REVISION` 77; 78 once props of the
older `Mesh` class are drawn (UTA-0450). Each prop's
vertices are one per wedge, so faces sharing a corner share its vertex.

A recipe lamp whose fitting names a mesh decoration (UTA-0256 § 4.1) gains
a copy of that prop's triangles in its `AddedLamp::shape`, placed by § 4.3
and then moved as the lamp's light is moved (UTA-0256 § 4.3). The prop
itself stays in `PROP` where the map placed it.

### 4.6 Drawing

At load, `ubundle::applyProps` appends `PROP` to MOVR as one mover standing
still at the origin, as `applyAddedLamps` appends a lamp's shape, under
the export index `PROPS_EXPORT`. Every loader calls it beside `applySun`:
the viewer, `ut-shot`, `ut-ref` and `ut-bench`. The renderer draws it as
it draws that shape: no new pass, no new binding. Its vertex normals light
it per pixel. `shapeOf` already samples every MOVR shape, so a changed
`PROP` re-uploads.

## 5. Invariants

- **INV-1** — The layout reads a real LodMesh. `readLodMesh` on
  `Botpack.u`'s `pipeM` gives 62 vertices a frame, one frame, and every
  vertex within the mesh's stored bounding box, with at least one vertex on
  each of its six faces.
  *Test:* `tests/real/RealMeshTest.cpp`, "UTA-0341 INV-1", in the
  real-asset tier, beside a case reading every stock LodMesh (537).
  *Breaks when:* a field of § 4.2 is read at the wrong width, or the vertex
  packing is decoded in the wrong order.
- **INV-2** — The reader refuses what it cannot consume. A LodMesh export
  with one byte appended, or one cut short, is refused, as is a `Texture`
  export handed to it.
  *Test:* `tests/unit/LodMeshTest.cpp`, "UTA-0341 INV-2".
  *Breaks when:* the end-of-export check is dropped.
- **INV-3** — Only decorations drawn as a mesh are props. A fixture level
  placing a decoration with `DrawType` 2, one with `DrawType` 1, one hidden,
  and a non-decoration with `DrawType` 2 bakes a `PROP` holding the first
  alone.
  *Test:* `tests/unit/BakePropsTest.cpp`, "UTA-0341 INV-3".
  *Breaks when:* any of § 4.1's tests is skipped.
- **INV-4** — A prop stands where UT99 places it. A fixture mesh placed with
  a location, a rotation, a `DrawScale` and a `PrePivot` lands each vertex
  where `tests/support/FCoordsPort.h` composes UT99's coordinates for the
  same actor, within 0.01 units.
  *Test:* `tests/unit/BakePropsTest.cpp`, "UTA-0341 INV-4".
  *Breaks when:* the rotation order, `RotOrigin`, `Origin`, `Scale` or
  `PrePivot` is dropped or misapplied.
- **INV-5** — A mirrored prop still faces out. The INV-4 fixture with one
  negative `Scale` component writes each triangle with the winding whose
  normal points away from the mesh's centre.
  *Test:* `tests/unit/BakePropsTest.cpp`, "UTA-0341 INV-5".
  *Breaks when:* the winding flip of § 4.3 is skipped.
- **INV-6** — The skin rule. With `MultiSkins[0]`, `Skin` and the mesh's own
  texture all set, a face of material 0 wears `MultiSkins[0]`; with it
  cleared, `Skin`; with both cleared, the mesh's.
  *Test:* `tests/unit/BakePropsTest.cpp`, "UTA-0341 INV-6".
  *Breaks when:* § 4.4's order changes.
- **INV-7** — `Style` and `bUnlit` reach the flags. The INV-3 prop with
  `Style` 4 and `bUnlit` writes batches carrying `PF_TRANSLUCENT` and
  `PF_UNLIT`.
  *Test:* `tests/unit/BakePropsTest.cpp`, "UTA-0341 INV-7".
  *Breaks when:* either is not read.
- **INV-8** — A prop that does not read never refuses the map. The INV-3
  fixture whose prop names a missing mesh bakes, writes no `PROP`, and names
  the actor in `skippedProps`.
  *Test:* `tests/unit/BakePropsTest.cpp`, "UTA-0341 INV-8".
  *Breaks when:* the reader's refusal reaches the bake's result.
- **INV-9** — `PROP` reads back what was written and refuses what GEOM
  refuses.
  *Test:* `tests/unit/PropSectionTest.cpp`, "UTA-0341 INV-9".
  *Breaks when:* the section's codec skips GEOM's checks.
- **INV-10** — A recipe lamp draws its mesh fitting. UTA-0256 INV-14's
  fixture lamp naming a mesh decoration bakes an `AddedLamp::shape` holding
  that mesh's triangles, moved with the lamp, while `PROP` keeps the prop
  where the map placed it. UTA-0256 INV-14's byte-equality clause is
  withdrawn by this spec.
  *Test:* `tests/unit/BakeLampsTest.cpp`, "UTA-0341 INV-10".
  *Breaks when:* the fitting's mesh is dropped, or not moved with the lamp.
- **INV-11** — Props draw. A fixture level with one prop in front of the
  camera draws differently with its `PROP` than without it, at Low and at
  High.
  *Test:* `tests/device/RenderPropsTest.cpp`, "UTA-0341 INV-11".
  *Breaks when:* `applyProps` is not called, or the shape is culled.
- **INV-12** — A changed `PROP` re-uploads. Moving every vertex of one
  bundle's `PROP` in place, between two draws by one renderer, changes the
  image.
  *Test:* `tests/device/RenderPropsTest.cpp`, "UTA-0341 INV-12", which edits
  one bundle in place as `RenderSunTest.cpp` does.
  *Breaks when:* `shapeOf` stops sampling MOVR's shapes.

- **INV-13** — A skin only props wear is enlarged at most `PROP_UPSCALE`.
  In the INV-3 fixture, the cube's skin is enlarged no more than twice its
  size, while the floor's picture, which a surface wears, is enlarged
  further.
  *Test:* `tests/unit/BakePropsTest.cpp`, "UTA-0341 INV-13".
  *Breaks when:* the cap is not applied, or applied to surfaces too.

- **INV-14** — A `Mesh` reads as a LodMesh does, and draws. The fixture
  cube written as a `Mesh` reads to the same vertices and, face by face,
  the same corners as the cube written as a `LodMesh`; padded by a byte it
  is refused. A prop wearing it bakes the cube's twelve triangles. Every
  `Mesh` of `System/SPVale.u` (24) reads.
  *Test:* `tests/unit/LodMeshTest.cpp` and `tests/unit/BakePropsTest.cpp`,
  "UTA-0341 INV-14"; `tests/real/RealMeshTest.cpp`, "UTA-0341 INV-14", in
  the real-asset tier.
  *Breaks when:* `Tris` is stepped over, a `Mesh` is read on into a
  LodMesh's tables, or the prop lookup refuses the class.

## 6. Failure modes

- **A mesh in a package the install lacks, or in a layout § 4.2 does not
  consume.** The prop is skipped and named (§ 4.1).
- **A prop that animates in UT99** stands still in its first pose.
- **No shadow from a prop.** Props cast no shadow in this iteration: they
  are in no ray set and in no shadow map. A ceiling pipe throws none.
  Recorded as § 9's.
- **Smooth shading across hard edges.** One normal per vertex smooths a
  barrel's rim into its lid. UT99 shades meshes per vertex the same way.
- **Texture coordinates off by half a texel** if UT99 centres them
  differently from `U / 256`. Checked by eye in § 13.

## 7. Tests

INV-1 reads the reference install (the real-asset tier,
`docs/build-and-test.md`). INV-2 reads, pads and cuts a fixture export, a
cube `tests/unit/BakeFixture.h`'s new `cubeMesh` and `addLodMesh` build, so
it runs without the install. INV-3 to INV-8 bake fixture levels placing
that cube (`tests/unit/BakePropsTest.cpp`), as does INV-13. INV-9 is the codec's.
INV-10 extends `BakeLampsTest.cpp`. INV-11 and INV-12 are device tests, run
on Mesa's CPU driver and the GPU under the validation layer and sync
validation. Every existing device test draws a map with no `PROP` and must
pass unchanged.

## 8. Alternatives considered (and rejected)

- **Bake props into GEOM, with charts.** They would take baked shadows and
  occlusion, as brushes do. But SMSK and AOCC chart planar polygons, and a
  mesh is many small curved triangles: a chart each would crowd the atlas,
  and a pipe would shade facet by facet. Rejected for a first iteration.
- **Instanced meshes with a transform per instance.** Less memory for a
  mesh placed many times: MH-Vale_Evelyn's some 333,000 prop triangles
  (§ 2) are a few hundred distinct meshes. World-space copies need no new
  pass, binding or shader, which is why the first iteration takes them.
  § 13 measures what they cost; instancing is the next step if it is too
  much.
- **Read the mesh at load time.** No format change, but the renderer would
  need the install's packages, which the bundle exists to make unneeded.
  Rejected.

## 9. Out of scope

- Animating a prop; pickups, weapons, monsters and players (UTA-0019,
  UTA-0159).
- Shadows cast by props, and their collision (UTA-0377).
- `SkeletalMesh` exports; version-61 meshes. (The `Mesh` class was here
  until UTA-0450 brought it in, § 4.2.)
- Level-of-detail reduction.

## 10. What checks this

| Claim | Check |
|---|---|
| INV-1 to INV-14 | the tests in § 7 |
| A prop looks as UT99 draws it | nothing automatic; § 13's views beside a capture of the original |
| Existing maps without props unchanged | every existing device test |

## 11. Cross-doc impact

- `docs/specs/UTA-0256-added-lamps.md` INV-14: its clause that a lamp with a
  mesh fitting bakes a bundle byte-equal to one without is withdrawn
  (INV-10 here). Its refusals stand.
- `docs/specs/UTA-0055-swaying-cloth-and-water.md` defers mesh flags and
  banners here; this spec draws them still (§ 3).
- `CLAUDE.md` § Standing facts names the bundle format and baker revision;
  they become 36 and 77 when this ships.

## 12. Cold-eyes loop log

`docs/reviews/UTA-0341-mesh-decorations-loop-log.md`. No review runs: the
gate is cancelled for this project.

## 13. Resource cost

To be measured when built: `ut-bench frame` at High, 2560x1440, on DM-Fetid
and MH-Vale_Evelyn (§ 2's most prop triangles), props on against off in
one job, and each bundle's PROP size.

**Measured 2026-11-05** on the RX 6600 (RADV), `ut-bench frame --tier high
--size 2560x1440`, each map's bundle against a copy with PROP removed, in
one job; `ut-bench` gave no busy-machine warning. Frame medians, ms:

| Map | Prop triangles, vertices | Still, off → on | Moving, off → on |
|---|---|---|---|
| DM-Fetid (views on its props) | 2108, 1726 | 3.774 → 4.304 | 3.806 → 3.944 |
| MH-Vale_Evelyn | 317,661, 220,664 | 6.745 → 6.702 | 6.788 → 6.687 |

Vale's difference is inside run-to-run noise; Fetid's views fill the
screen with props. Texture working set with `PROP_UPSCALE` (§ 4.4): Fetid
52,901,792 bytes, Vale 835,273,152, both under the budget. `ut-shot` views
at High show Fetid's pipes, bends, tube lights and barrels in place, a bend
meeting its straight pipe cleanly, and Vale's boulders, moss, ferns and
lanterns. Not yet compared against a capture of the original.
Refute by re-running the same command. Props draw inside the opaque and translucent passes, so the cost
shows there. Recorded here with the date, with `ut-shot` views of DM-Fetid's
pipes beside the original's.

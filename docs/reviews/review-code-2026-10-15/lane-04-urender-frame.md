I found 1 medium and 2 low defects in the per-frame path. The medium one can write past the end of two GPU buffers. Nothing rated critical or high.

**Line counts of the subject files, as Read numbered them:**

| File | Lines |
|---|---|
| `Fog.h` | 55 |
| `Frame.cpp` | 1961 (Read said 1962 in total; probably a trailing newline) |
| `Probes.cpp` | 50 |
| `Renderer.h` | 227 |
| `Sky.cpp` | 41 |
| `Sky.h` | 56 |
| `Tiers.h` | 138 |
| `Zones.cpp` | 23 |
| `Zones.h` | 22 |

**Already in my context when I arrived:**
- The global `/home/ants/.claude/CLAUDE.md`. It describes rule 14, the review skills and the Ants verbs.
- The `review-lane` agent prompt.
- The user's email address.
- Environment details, including the date 2026-10-15.

There was no project `CLAUDE.md`, no memory index and no git snapshot.

**What I read, and one tool fallback:**
- The shared context once, and each lane file once with native Read.
- Contract sections: UTA-0015 §§4.3–6, UTA-0051 §§4.1–6, UTA-0215 §§4.2–6, UTA-0014 §4.3 and §6.
- `cpp.md` §§ Idioms, Range guards and Spellings.
- A batched spec read spilled to a file I cannot open without Bash. I read the same sections again with native Read; nothing in that spill was used.
- To check claims, I also read outside the lane: the light-list helpers in `Lights.cpp` (`drawsDirectly`, `directLights`, `drawnIndices`, the start of `drawnLights`), `useOf` and `transitionImage` in `Resources.cpp`, and search hits for `Buffer::upload`, `Materials.cpp` and `Pipelines.cpp`.
- I passed `tests/**` as `exclude_glob` on every search and opened no test file.
- My brief and the review-lane rules did not conflict.

## Critical (0)

## High (0)

## Medium (1)
- [dim 2] `Frame.cpp:144-145` — `/// What it cannot see: an edit in place that changes only unsampled bytes. The lights and the movers' transforms are read every frame and need no upload.`
  - **The comment's claim is false for lights.** `shapeOf` hashes no light data at all, only `shape.lights = bundle.lights->size()` (line 303). It also does not sample `bundle.rooms`.
  - **But `upload` builds several things from the lights, sized or mapped once:**
    - the `lights` buffer, sized `drawnLights(bundle, 0.0).size() + 1` (798-800);
    - `shadowFaces`, sized `lightCount * 6 + 6` (849);
    - `lightZones`, found from each light's `location` and `bundle.rooms` (802-805);
    - the SMSK pair-to-light mapping from `drawnIndices` (737).
  - **Failure scenario.** A bundle is reloaded into the same storage with the same counts. This is the exact case lines 137-140 say the sample exists to catch. Suppose one light's `drawsDirectly` answer flips: brightness 0 to non-zero, `specialLit`, strip absorbed, or type.
    - `drawnLights` then returns more entries than the buffer holds.
    - Line 1782, `std::memcpy(impl.lights.mapped(), frameLights.data(), frameLights.size() * sizeof(gpu::Light))`, writes past the mapped allocation.
    - Line 1784 can do the same into `shadowFaces`, since `plan.faces` grows with `directLights`.
    - Separately, moving a light (or changing ROOM) leaves `lightZones` stale. `volumeLights` then picks the wrong glowing set, against UTA-0015 §4.4 ("`upload` caches each drawn light's zone").
  - **Fix:**
    - Fold each light's `drawsDirectly` result and `location`, plus the ends of `bundle.rooms`, into `shapeOf`.
    - Bound both memcpys by the buffer's capacity, returning an error rather than overrunning.

## Low / Info
- [dim 4] `Frame.cpp:1299-1301` — `static_cast<float>(std::fmod(static_cast<double>(rate) * static_cast<double>(cameraPanSpeed[axis]) * lastLightSeconds, 1.0))` — no `isfinite` guard.
  - The other two readers of the same clock have one: the flame clock (1670) and the wobble clock (1478–1480).
  - A non-finite `pinLightSeconds` value therefore pushes a NaN pan to every panning batch, while flames and the wobble read 0.
  - Fix: apply the same `std::isfinite(lastLightSeconds) ? … : 0` guard.
- [dim 2] `Frame.cpp:1630` — `scale = std::clamp(*impl.config.fixedRenderScale, settings.minimumRenderScale, 1.0);`
  - `std::clamp` returns a NaN unchanged, so `regionOf` (1635) does `static_cast<std::uint32_t>(std::ceil(NaN * full))`, which is undefined behaviour.
  - `Renderer.h:75-76` promises the value is "clamped to the tier's floor and to 1". `create` checks `hazeScale` for this (1524) but not `fixedRenderScale`.
  - Fix: refuse a non-finite `fixedRenderScale` in `create` with InvalidArgument.
- INFO — I did not read UTA-0014 §4.8 (shadow maps) or §4.10/§4.11, so the shadow pass and output stage were checked against code comments and the other specs only.
- Dimensions with nothing found (searched this lane):
  - 3 (security): bounds checks are present at 709, 739, 744, 1745, `Zones.cpp:13-17` and `Probes.cpp:36`.
  - 5, 7 and 8: nothing found.
- Dimensions N/A for this lane:
  - 9 and 17: no persistence in this lane.
  - 10, 12 and 15: no contract named in the tail.
  - 13 and 16: no user-facing strings or locale handling here.

## Covered by spec and looks correct
- **Tiers** (`Tiers.h`): `settingsOf` matches UTA-0051 INV-1. The feature table has VolumetricFog from Medium (UTA-0015 INV-9). Caustics and WaterMotes are from Medium (UTA-0215 §§4.5, 4.8).
- **Dynamic resolution**: the region uses `max(1, ceil(s·W))`. Sky faces are drawn at scale 1. The measurement excludes present. Velocity and Emission readback are refused below scale 1 (UTA-0051 §4.4, INV-8).
- **Fog**:
  - Below Medium the volumes are 1×1×1, cleared to (0,0,0,1), and nothing is dispatched.
  - The stages run between culling and the forward pass.
  - The flashlight is appended after the plan, and `LIGHTS` is sized +1.
  - `historyWeight` is 1 after a cut, on upload and after sky faces.
- **Fog barriers**, checked against the Vulkan synchronisation rules:
  - scatter → integrate and copy;
  - copy → next scatter;
  - integrate → fragment;
  - haze copy → scatter;
  - atlas → compute. This one chains through `useOf`'s fragment-test stages, which overlap.
- **Descriptor pool**: its counts match the sets allocated (11 sets; samplers = textureCount + 15; 3 storage images).
- **Underwater**: `flashFog`'s `viewFog` and water flag only when the camera's zone is water, and 800 specks gated on water and tier (UTA-0215 §§4.2, 4.8).
- **Probes, zones, sky**:
  - The probe table always has a free slot, so its linear probing ends.
  - An empty probes upload is padded to 16 bytes (`Resources.cpp:134`).
  - `zonesSeeingSky` is bounds-checked.
  - The sky-face field of view gives a 90° central square in both orientations.

## Open questions
- If the sky zone is itself water, `recordFrame` draws motes into the sky faces (1358 does not test `skyFace`). Is that intended?
- The flame and mote pipelines are drawn without `vkCmdSetFrontFace` or `vkCmdSetDepthBias` in that pass. They inherit whatever the previous pass set. Whether they declare those as dynamic state is in `Pipelines.cpp`, which is outside this lane.

## 3 items to fix first
1. **The Medium finding (lights not in the fingerprint).** It is the only out-of-bounds write into mapped memory, and the in-code comment hides it.
2. **`fixedRenderScale` NaN.** It is undefined behaviour from a public `Config` field that the header promises is clamped.
3. **The pan clock's missing guard.** It is a guard already fixed at two sibling sites and one-line to bring into line.

# Fitting a map that looks wrong

What MH-()mG-TheBoat-V2mini-BP taught, as a method for the next map that
draws too dark, too bright or wrong outdoors. Each step names the roadmap
item that holds its evidence. The recipe keys are specified in
[`specs/UTA-0113-recipe-format.md`](specs/UTA-0113-recipe-format.md),
[`specs/UTA-0338-baked-sun.md`](specs/UTA-0338-baked-sun.md) (`[sun]`),
[`specs/UTA-0350-painted-skies.md`](specs/UTA-0350-painted-skies.md)
(`[sky]`) and [`specs/UTA-0256-added-lamps.md`](specs/UTA-0256-added-lamps.md)
(`[lamp]`). The commands are in [`build-and-test.md`](build-and-test.md).

## 1. Diagnose before fixing

- **Read how the map is lit before blaming the code.** `ut-dump
  --surface-list` shows each texture's flags, and `ut-dump --lighting`
  shows which surfaces no light reaches. A dark map is usually one the
  original lit by zone ambient, which this renderer withdrew on purpose.
  TheBoat's black frames were bisected as a regression before this was
  seen (UTA-0350, UTA-0382).
- **Check whether the map has a real sky.** `scripts/map-analyser.py`
  reports `paintedSky` (the sky is only painted on walls) and `paintedSun`.
  A sky with no `PF_FakeBackdrop` surface and no SkyZoneInfo is a closed
  box: sun and sky light never get in until a `[sky]` opens it (UTA-0350).
- **Split the light before judging it.** `ut-shot --light-terms` separates
  direct from indirect light, and a build with one feature switched off
  settles which one is at fault. TheBoat's bleached sand was blamed on the
  haze until a haze-off build showed the same (UTA-0383).
- **A sky that looks like a box: rule out parallax first.** A shot moved
  sideways and one only turned show whether the sky sits at infinity.
  TheBoat's did; the box came from the sea ending in a straight edge under a
  pale wall, and from flat clouds (UTA-0389).
- **Test zone-based effects on single-zone maps.** TheBoat is one zone, so
  a test by zone could not tell the hold from the deck (UTA-0387), and a
  BSP node with zone 0 on both sides was skipped by the haze (UTA-0390).
- **A stray shape: check the original at the same camera.** `ut-compare
  --capture` showed TheBoat's stone triangle in the sea there too. It was a
  pit a brush cut through the sea floor, found by casting the camera ray to
  the water and listing nearby surfaces with `ut-dump --surface-list`
  (UTA-0396).

## 2. Fix in this order

1. **Sky.** A painted-only sky gets `[sky] textures =` naming the painted
   textures. Recipe colours are screen colours; the bake divides them by the
   screen's exposure (UTA-0383).
2. **Sun.** A clear or lightly clouded sky gets a `[sun]`; only an overcast
   sky goes without (UTA-0346). With no painted sun, the direction is a
   pick: TheBoat's is warm white at about 50 degrees.
3. **Rooms the sun cannot reach.** Decided room by room, against the
   original, never by a blanket rule or a brightness gain (`CLAUDE.md`
   § Standing facts). TheBoat's holds got `[lamp]`s copying the map's own
   cabin light, wearing its lantern as their fitting (UTA-0344).
4. **Haze.** Only where the default washes out distant geometry, with
   `[sky] haze` (UTA-0391). TheBoat keeps the default.
5. **The map's own faults.** Only at the user's word. A brush that cuts a
   hole in a surface gets a `[hide]`, which covers the hole as that surface
   (UTA-0396). `ut-dump --brushes` names its brush and polygon.

## 3. Measure every change

- **A recipe change can change nothing.** TheBoat's first sun moved no
  pixel, which is how the closed sky box was found (UTA-0346). Compare the
  light before and after at fixed cameras.
- **Compare against the original, not by eye.** `ut-compare --capture`
  gives mean grey against the original at the same camera, and `ut-ref`
  tells accurate light from a fault (UTA-0344).
- **Re-shoot every other recipe map after a shared change.** UTA-0383's sky
  fix for TheBoat also dimmed TheFortress (UTA-0398).
- **Re-bake after the baker or format moves** (`CLAUDE.md` § Standing
  facts). A new bundle section must also be sampled in the fingerprint
  test's `shapeOf`, or GitHub's matrix fails (UTA-0350).
- **A bake is named by the baker's revision and the recipe, not its code.**
  Changing baker code again under the same revision makes `ut-bake` report
  "already baked" and keep the old probes. Delete that bake before
  re-measuring (UTA-0389).

## 4. What stays open on TheBoat

`roadmap_query query:"TheBoat" status:"active"` lists it.

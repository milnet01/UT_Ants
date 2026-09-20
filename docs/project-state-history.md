# Project state history

What `CLAUDE.md`'s § Where this project is used to carry: the running
chronology of what shipped, what each item left behind, and the corrections
this project made to its own record.

**Moved here 2026-09-20.** That section had grown to about two fifths of
`CLAUDE.md`, which is read on every prompt, and almost all of it was history a
session needs only when it goes looking. `CLAUDE.md` keeps the facts that
change what a session does *now*; everything else is here.

**`ROADMAP.md` is the authority, not this file.** Each item's own bullet
carries its measurements, its spec and its shipped record, and the roadmap is
generated from a store. This is the digest as `CLAUDE.md` held it, kept because
it reads as a narrative the per-item bullets do not. Where the two disagree,
believe the roadmap. Nothing here is maintained: it is a record of what was
true when it was written.

## What shipped, newest first

`UTA-0192` shipped 2026-09-20, green on the matrix: the output stage runs the
Khronos PBR Neutral curve's shoulder with its TOE removed. The toe subtracted
up to 0.04 from every channel and UT99 has no such step, so ours darkened every
dim surface the original left alone. Measured, the toe is the whole of the
mismatch and the shoulder is free — pooled RMS 34.85 with it and 32.49 without,
where UT99's own clip scores 32.58 and that clip placed UNDER the toe scores
35.06, worse than shipping. `EXPOSURE` 6.16 to 5.03 and `AMBIENT_SCALE` 1 to
0.75 followed, refitted on UTA-0187's own linear renders with no re-render.
`DISPLAY_LIGHT_POWER` stays 1.6. The baker did not change, so no bundle went
stale. Added `tests/device/RenderOutputStageTest.cpp`, the output stage's first
grader — until it, every device test read back before the post chain and
nothing graded exposure or the tone map at all. Amended `UTA-0014` SS 4.10,
which had named an exposure four refits stale. Harness at `ut-ants-uta0192`.

`UTA-0126` — why eight maps still do not route — shipped 2026-09-20. All eight
have a named cause and none is undiagnosed. Two were ours and are `UTA-0133`,
shipped: § 4.7 offered its fallback goal wherever the network reached the node
NEAREST the exit, never checking that node is AT the exit, so the chain bridged
to a substitute. It keys on a navigation point that TOUCHES the exit now. Over
the partitioned corpus 17 maps moved: five gained something real, twelve
stopped claiming a route they never had.

Five are map content, confirmed in-engine by UT_MonsterHunt 2026-09-20 — all
NOROUTE, the pawn walking and the network answering, the exit alone
unreachable. They are write-offs. The sixth is MH-NivenSB.

Two repairs outlived that investigation and carry its measurements: `UTA-0195`
(MH-Skaarj_ReactorTest-v1's exit sits 43 units from the network and still does
not route — something refuses a link with no distance to cross) and `UTA-0196`
(MH-NivenSB needs a chain anchored at PathNodeSeed8's end, not the single
bridge node, which gets its spec and closes the gap by only 68 of 2852 units).

`UTA-0187` shipped 2026-09-20, green on the matrix: light meets texture on
display values at `DISPLAY_LIGHT_POWER` 1.6, `AMBIENT_SCALE` 1 and `EXPOSURE`
6.16, and ubake takes UT99's own falloff at baker revision 21. `post.frag`
applied no ramp — the measured `raw^1.65` scored worse at every point. No spec,
by the user's decision. **`UTA-0192` later took the toe out of that tone map
and moved both constants; see its own record.**

`UTA-0164` shipped 2026-09-19, green on the matrix: baked ambient occlusion at
every tier, bundle format 14, spec `docs/specs/UTA-0164-ambient-occlusion.md`.

`UTA-0186` (black holes in a mansion map's walls) is parked on `Waiting-on:`
the holes being seen again: it did not reproduce on the current build.
`UTA-0190` adds P in the viewer, which writes the exact camera into the map's
notes, which is how the next sighting gets pinned down. Both are open; the
roadmap carries their current state and this line will not be kept up to date.

`UTA-0178` shipped 2026-09-18, green on the matrix: the haze is refitted on all
three reference maps, and AS-Frigate's sky has its own cause; `UTA-0187` covers
AS-Frigate's sky. `UTA-0188` (AS-Frigate's water drawn dark) follows
`UTA-0187`; the session placed 0164, 0187 and 0188, 2026-09-18.

`UTA-0182` (a wall near a grazing lamp shadowed itself) and `UTA-0185`
(surfaces on the probe grid lost their bounce light) shipped 2026-09-17, green
on the matrix. `UTA-0183` shipped the same day: the controller's Options button
closes a map. `UTA-0179` shipped the same day: the launcher lists only playable
maps, by UT99's MapPrefix rule. Then `UTA-0177` (no magenta for a skipped
texture), `UTA-0180` (break up the tiled look of textures), `UTA-0181` (a local
folder of replacement textures) and `UTA-0184` (a modern look for the launcher,
keeping its large text). The user filed 0182 before 0180 and left the rest of
the placement to the session, 2026-09-17: defects first, so the tiling look is
measured on clean frames.

`UTA-0173` shipped 2026-09-17, green on the matrix: ut-dump counts only monster
factories. `UTA-0163` (the real sky) shipped the same day; the sky shows
bright, and why is `UTA-0178`. `UTA-0175` (Medium and up shadow at 32 units in
an 8192 atlas), `UTA-0176` (fire textures bake as flames, found by class; baker
revision 19) and `UTA-0174` (the camera kept clear of every nearby surface)
shipped 2026-09-17, green on the matrix. After them `UTA-0157` (painted details
such as ceiling lights become real geometry), `UTA-0172` (ut-dump's per-actor
event wiring), then `UTA-0142`'s trigger census — the user's order, 2026-09-17,
after flying AS-Frigate in the launcher.

`UTA-0170` shipped 2026-09-17, green on the matrix: `ut-ants <install>` alone
opens a map launcher with a notes file per map. `UTA-0169` shipped the same
day: a light of brightness 0 with no fog volume is no longer drawn. `UTA-0167`
(fly the camera with a gamepad) and `UTA-0168` (non-solid brushes cast no
shadow, baker revision 18, `EXPOSURE` 5.4) shipped first, both green on the
matrix.

`UTA-0165` shipped 2026-09-17, green on the matrix: every light carries its
level's `LevelInfo.Brightness`, which UT99 applies and we never read
(`UTA-0156` SS 4.5, bundle format 13), and `EXPOSURE` 5.5, `AMBIENT_SCALE` 0.5
and the fog were refitted against the original game. UT99's own point falloff
was decoded and measured worse here, so SurrealEngine's stays; the reason sits
beside `ubake::falloff`. The captures, bakes and sweep scripts are at
`ut-ants-uta0156`.

`UTA-0166` shipped 2026-09-16 on the matrix: a shadow tile is sized from its
light's reach rather than its size on screen, so the plan no longer changes
with the camera and every light keeps a tile. That amended `UTA-0014` SS 4.8
and `UTA-0051`'s shadow-planning paragraph, and forced `UTA-0015` SS 7's sweeps
to be re-run, since every fog constant had been fitted while only a fraction of
the lights scattered.

`UTA-0015` (volumetric fog, light shafts and the flashlight) shipped
2026-09-15, green on the matrix, spec `docs/specs/UTA-0015-volumetric-fog.md`;
its look was set by measurement against the original game.

`UTA-0157` (light fixtures) is parked for the user's review over real matches:
its cheap tricks have both landed — parallax (`UTA-0040`) and `UTA-0053`'s
emissive bloom, shipped 2026-09-15 green on the matrix — and no measurement can
say whether real housings are still needed. The rest of `UTA-0053` stays
deferred. How it looks is left to research and measurement, to be reviewed
later over real matches.

`UTA-0156` (brightness) shipped 2026-09-15, green on the matrix: cylinder
lights fade at the edge of their reach, `EXPOSURE` is 3.2, and zone ambient
light is carried in a `ZONE` section and drawn at a measured `AMBIENT_SCALE` of
2.5 (spec `docs/specs/UTA-0156-zone-ambient-light.md`). `UTA-0165` then took
UT99's own `FGetHSV` brightness curve into the light model, and later
`LevelInfo.Brightness`, refitting both to the values above.

`UTA-0162` (strip lights) shipped 2026-09-14, green on the matrix: rows of
lights bake into one segment. `UTA-0155` shipped the same day: the bake reads a
palette from another package, and bakes a procedural texture as a still of its
SourceTexture. `UTA-0153`, `UTA-0154` (AMD FSR 1), `UTA-0158` (the camera stops
at walls) and `UTA-0040` (parallax occlusion, spec
`docs/specs/UTA-0040-parallax-occlusion.md`) shipped the same day.

`UTA-0051` shipped 2026-09-14: quality tiers and dynamic resolution, spec
`docs/specs/UTA-0051-quality-tiers.md`. `UTA-0016` shipped the same day:
`ut-ants <install> <bundle>` flies a camera through a baked map. Its presenting
path is checked by hand with `--frames N --validation` on a baked map. SDL3
went through `docs/standards/dependency-acquisition.md` § 2 by hand on the way,
landed on route 1, and found nothing in § 2 to fix. `UTA-0103` shipped the same
day. `UTA-0136` shipped 2026-09-13, and UT_MonsterHunt's calibration of it is
on `UTA-0085`.

`UTA-0014` shipped 2026-09-12, green on the matrix; its spec is
`docs/specs/UTA-0014-vulkan-draw-path.md`. The draw path is graded headlessly
on Mesa's software driver, so `scripts/ci.sh` selects test labels per platform
and both CI legs install Vulkan packages; the presenting path is run by hand,
with the probe at `/mnt/Games/Scripts/Linux/ut-ants-present-probe-uta0014/`.
(`CLAUDE.md` § Build and test carries that rule, which is why it is only
recorded here.)

`UTA-0133` shipped 2026-09-12 and is ✅. Its gate is worth reading before the
next amendment to any spec: `review-contract` ran before the code, per rule 14,
and found the amendment ITSELF wrong — keyed on two different navigation points
at once. Writing the code first would have built the defect it was removing.

**Shipped 2026-09-12**, all on the matrix: `UTA-0133`'s fallback keying and
`UTA-0134` (answered in-engine by UT_MonsterHunt, nothing to change),
`UTA-0127`'s off-world mark, `UTA-0128`'s control group, `UTA-0087`,
`UTA-0081`, `UTA-0071` and `UTA-0077`.

## Investigations and what they found

`UTA-0077` ran the real-asset tier on Windows for the first time, on a STOCK
install — 96 maps, zero `MH-`. The readers came back clean on a corpus they
were not derived from: 96 of 96 maps parsed, 640,209 of 640,213 assertions. The
three failures are `UTA-0132`, not readers. The route is written up at
`ut-ants-windows-uta0077` and is four commands, so re-running it on any install
is cheap. One Defender exclusion for `C:\uta-test` was added on the Windows
machine; that item carries the command to undo it.

`UTA-0128` found MHEndPlace's rule SCATTERED against author-placed exits — a
median 7837 units off, within 4000 units on 30% of 471 maps, and beaten by the
node nearest the PlayerStart on a third of them. The transcription reproduces
both of UT_MonsterHunt's reference points exactly, so that is a finding about
the rule and not about our control group. They accept it and have deployed
MHEndPlace as an explicit stopgap. Probes and per-map output are kept outside
the repository, at `ut-paths-output-uta0128`.

Filed 2026-09-12 from UT_MonsterHunt's answer to `UTA-0134`: `UTA-0135`, the
exit's VERTICAL window, which is unmeasured and which MH-NivenSB's correction
rests on; `UTA-0136`, serialising the nav graph's reach-spec flags from
ut-dump; and `UTA-0137`, a skipped census row not saying whether the map was
renamed or de-duplicated away — the two look identical from the name, and
resolving one by punctuation reads a DIFFERENT map. Corrected by UT_MonsterHunt
before anything was built on it.

**Filed 2026-09-12 and open at the time:** `UTA-0129` the benchmark tool,
`UTA-0130` exits that are SHOT rather than walked into, `UTA-0131` the hunt for
whatever tool parked 58 MonsterEnds outside the world, and `UTA-0132` the
real-asset tier assuming the reference install.

## Corrections this file made to its own record

**The MH-BoomDockBridge_V0 firmness case, corrected 2026-09-20.** This file
said UT_MonsterHunt's store had that map as a provisional ROUTE that a fresh
engine run contradicted, and credited their firmness marking with catching it.
They corrected it, owning the store, and closed it as their GAME-0157: the
store was never wrong — it held NOROUTE, firm, from the engine, throughout.
What printed the contradicting ROUTE was the run's CONSOLE OUTPUT. Their file
stage prints its own guess, their engine stage correctly skips a map that
already has a firm verdict, and nothing then printed the measured answer. They
fixed it by having the guess name the stored answer beside it. So the hazard
was never a bad verdict reaching a reader of their store; it was a person
watching a run scroll past. Not independently verified by us — we cannot read
their store. **The firmness caution in `CLAUDE.md` still stands; this case is
simply not its evidence.**

**`UTA-0126`'s eight are not `MonsterEndSB`** — censused 2026-09-12, all eight
`monsterhunt.monsterend` with `TriggerType` 0. This file previously said two of
them were and had never been checked. The corpus is still uncensused, which is
what `UTA-0130` is for.

**`UTA-0142`'s re-check was asked for twice.** This file said the item was
waiting on UT_MonsterHunt's in-game re-check until 2026-09-20, when it had been
ANSWERED on 2026-09-17 (their GAME-0120: 1 real misroute, 16 enabled later, 6
no effect, 1 unmeasured). A session read the stale line and asked them to re-run
work they had already delivered.

## Facts that have since been superseded

**`ZoneInfo`'s ambient values are no longer outside the bundle.** This file
carried, from `UTA-0014`, that they were in **no** bundle section (`rg -c
Ambient src/` exiting 1) and that applying them needed `ubake` to write them and
a format version bump first. `UTA-0156` did exactly that: zone ambient light is
carried in a `ZONE` section. The line was still in `CLAUDE.md` when this file
was split out of it.

# UT_Ants — Discovery

> **Purpose — so that later, anyone can tell whether the thing being
> built is still the thing that was wanted.**

Not a kick-off document. This is what everything is checked against for
the life of the project, which is why the signs of success below have to
be things you could actually observe.

**This document is a gate.** Design does not start until it is agreed —
`~/.claude/workflow.md` § 2. It passes when a stranger could read it and
say whether a given feature serves it.

**Status:** drafted 2026-09-03, awaiting sign-off.

The request this was drawn from is kept verbatim at
[docs/request-2026-09-03.md](request-2026-09-03.md).

## The problem

Unreal Tournament (1999) is still being played, and its community map
library is still growing — one Monster Hunt server on this machine
carries 610 of them, most made by people who have never met each other.
The maps are alive. The engine is not.

Three specific hurts follow from that.

**The visuals cannot be fixed from inside.** UT99 bakes its lighting
into flat lightmaps at build time and draws 8-bit palettised textures.
There are no moving shadows, no material depth, no fog volumes, no light
shafts. Epic never released the engine source and delisted the game in
January 2023, so no patch will ever change this — the ceiling is fixed
at 1999.

**The bots cannot play Monster Hunt.** MH maps are built around switches
that open doors, plates that must be held while others pass, and counters
that need three levers pulled. UT99's bots understand none of it: they
walk into the closed door and stay there. So a Monster Hunt server with
fewer than a full team of humans stalls, and the fix today is a human
babysitting the bots through every puzzle.

**The map library is trapped.** Those 610 maps only run on an engine
nobody can change, in a game nobody can legally buy any more. Every year
that passes, running them gets harder rather than easier.

## Who it is for

- **A person who runs a Monster Hunt server** and wants the rotation they
  already have to keep working — the same maps, the same custom monsters —
  while looking like a game made this decade.
- **A person who still plays UT99** and would move to something better
  looking, but only if dodging, hammer-jumping and shock-comboing feel
  exactly the way their hands already expect.
- **A person who makes UT content** — maps, monsters, player characters —
  and wants their work to reach other players without asking anyone's
  permission or shipping anyone a zip file.

The first of those is the author, and saying so is useful: this project
has one live server it must eventually be good enough to replace.

## Signs it is working

Each is written so it could be observed by using the thing, not by
reading a commit log.

- **S1** — You open one of your own UT maps and recognise it instantly,
  while shadows move as you move, surfaces have real depth, and light
  shafts cut through the fog.
- **S2** — A UT99 player runs, dodges, hammer-jumps and shock-combos on
  muscle memory alone, and nothing surprises their hands.
- **S3** — A Monster Hunt map pulled from the existing 610 loads with its
  monsters in place — including custom ones nobody wrote code for here.
- **S4** — Bots clear a door puzzle with no human present: they find the
  switch, press it, go through, and where a plate must be held, one of
  them stays behind and holds it.
- **S5** — A player who has never seen a map joins a server and is
  playing on it within a minute, having taken everything they needed from
  the host — on any map the client had warning of, which on a rotation is
  every map after the first. A map nobody has prepared costs a wait, and
  the player is told so rather than left guessing.
- **S6** — Someone other than the author builds a map and a player
  character, hosts them, and other players see both correctly without
  installing anything by hand.
- **S7** — A stranger clones the public repository with no Unreal
  Tournament on their machine, and the build and the test suite both
  pass.
- **S8** — The live Monster Hunt server runs on this instead of UT99, and
  nobody wants to switch back.
- **S9** — A full Monster Hunt round is played on a PS4 controller
  without touching keyboard or mouse, and changing weapon never means
  scrolling through them one at a time.
- **S10** — A server operator turns the super weapons on for one map in
  the rotation and off for the next, without restarting the server and
  without editing either map.
- **S11** — A run of platforms can be crossed in first person without
  falling off for want of being able to see your own feet — and a UT99
  player checking **S2** on the same server notices no difference in how
  running, dodging and jumping feel.

S8 is deliberately the last one, and it is the bar this project's `1.0`
is measured against — see `docs/standards/versioning-overrides.md`.

- **S12** — Opening the level map mid-game shows where you have been,
  where your team has been, and which parts of the level nobody has
  reached yet — and on a Deathmatch server it tells you nothing about
  an opponent.

The S11 sign has two halves on purpose. First-person platforming is
bad in most shooters because the player cannot see where they will land,
and Metroid Prime is the game that solved it. Taking that solution must
not cost **S2**, which is why the sign is written so that failing either
half fails the sign.

## What it deliberately does not do

- **No single-player campaign.** No story, no ladder, no cutscenes.
- **No modes beyond Deathmatch, Team Deathmatch and Monster Hunt** before
  `1.0`. Not Capture the Flag, not Assault, not Domination. They are
  plausible afterwards and nothing in the design should block them, but
  building them is not this.
- **It does not talk to UT99 servers or clients.** Matching a 1999 wire
  protocol byte for byte would constrain every other decision here. The
  question may be reopened later; it is closed for `1.0`.
- **It does not run UnrealScript.** No bytecode interpreter. A custom
  class is understood by reading what it descends from and what its
  settings are, and is then played by our own equivalent — so a map using
  genuinely exotic scripted behaviour degrades rather than breaks.
- **It does not redistribute Epic's content.** Players bring their own
  copy of Unreal Tournament; the game reads it locally and refuses to
  start without it. Nothing Epic owns is committed, published, or sent
  over the network.
- **It is not a general-purpose engine.** It exists to play UT-derived
  content well. A decision that would serve some other game and cost this
  one is the wrong decision here.
- **It is not a 3D modelling tool.** Characters and models are made in
  Blender or its equivalents; this project imports them and sets them up.
- **No ray tracing required.** The visual target is reachable on the
  hardware that exists — dynamic lights, shadow maps, baked bounce light,
  volumetrics. Ray tracing is not a goal and is not a fallback plan.

# ADR-0001: Build a new engine rather than host the game in Godot or Unreal 5

- **Status:** Accepted
- **Date:** 2026-09-03

## Context

Unreal Tournament's engine source was never released, so nothing can be
forked. The project therefore needs an engine, and there were three ways
to get one: write it, host the game inside Godot 4, or host it inside
Unreal Engine 5.

Three constraints shaped the choice.

The strongest requirement in discovery is **S2** — a UT99 player's hands
must not be surprised. That lives entirely in the movement code: run
speed, air control, dodge impulse, step-up height, the exact shape of the
collision cylinder against the world. A host engine's character
controller does not reproduce it, so the movement code has to be written
by hand whichever route is taken.

**S6** requires a map editor that produces content other players can
download and run. Neither host engine's editor can be shipped inside
another program, so the editor has to be written by hand too.

The author has done this before. `DOOM_Ants` is a from-scratch Vulkan
renderer over a 1990s game on this same machine and this same GPU, which
makes the hardest part of the "write it" option a known quantity rather
than an estimate.

## Decision

Write the engine, in C++ with Vulkan.

## Consequences

Everything is ours: no host engine's assumptions to work around, no
licensing question about shipping converted content inside somebody
else's runtime, and the map format can be designed for the editor rather
than the editor bent around a format.

The cost is real and large. Renderer, physics, audio, networking,
asset pipeline and editor are all now this project's problem, and each is
months rather than weeks. Nothing about this decision is recoverable
cheaply — by the time it is obviously wrong, there will be too much
engine to move.

What has to be true: the milestones must stay ordered so that each one is
provable on its own, because a from-scratch engine has no borrowed
scaffolding to make a half-built subsystem look like it works.

The two rejected options remain reasonable for a different project. The
argument against them is specific to a remake whose whole point is that
the movement and the tools are exactly right, not general.

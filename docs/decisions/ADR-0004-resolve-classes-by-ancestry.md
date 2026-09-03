# ADR-0004: Understand custom actors by ancestry and defaults rather than by running UnrealScript

- **Status:** Accepted
- **Date:** 2026-09-03

## Context

Every weapon, monster, pickup and rule in Unreal Tournament is compiled
UnrealScript inside a `.u` package. **S3** requires that a Monster Hunt
map from the existing 610 loads with its monsters in place, and most of
those maps ship their own monster classes.

Reproducing that faithfully means writing an UnrealScript virtual
machine: the bytecode interpreter, the object model, the native function
surface the script layer calls into, and the replication semantics built
on top. It is the single largest piece of work available in this project,
and it would constrain the engine's own design to match a 1999 object
model.

But a `.u` package holds two separable things. The **bytecode**, which
needs a VM. And a **class table with default properties** — the parent
class, and the authored values for health, speed, damage, mesh, skin,
scale, sounds — which is a readable table needing no VM at all.

Most custom Monster Hunt monsters are a stock monster with different
numbers.

## Decision

Read the class table. For any actor a map places, walk its ancestry until
a class this engine implements natively is reached, spawn that, and apply
the default properties read from the package.

A hand-maintained override list can map specific notorious classes to
better equivalents than ancestry alone would choose.

No bytecode is executed, and no UnrealScript VM is written.

## Consequences

Monster Hunt maps populate, mutators and custom weapons come through the
same mechanism, and the engine's own object model is free of 1999.

What this gives up is exactness. A custom monster whose interest is its
*behaviour* rather than its numbers — a scripted boss with phases, a
monster that teleports on a timer — will appear and fight as its nearest
ancestor, which is not what its author wrote. Those maps degrade rather
than break, and the difference is visible to anyone who knows the map.

What has to be true: the fallback must be legible. When ancestry
resolution lands on something distant, the game must say so in its log
and the map's recipe must be able to record a better answer, or the same
disappointment gets rediscovered by every player independently.

Adding a VM later is not closed off, but it is not planned. If it ever
happens it supersedes this ADR rather than extending it.

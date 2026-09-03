# ADR-0005: Our own network protocol, not wire compatibility with UT99

- **Status:** Accepted
- **Date:** 2026-09-03

## Context

A remake could speak Unreal Tournament's own network protocol, letting
its client join real UT99 servers and real UT99 clients join ours.

That protocol replicates UnrealScript objects. Matching it byte for byte
would require the object model ADR-0004 deliberately declines to build,
and would fix the engine's internal representation of every replicated
thing to a 1999 shape — the opposite of what a from-scratch engine is
for.

Discovery names the immediate need as local network play, with the
author's own Monster Hunt server as the eventual target. Nothing in the
signs of success requires talking to an unmodified UT99 anywhere.

## Decision

Define our own protocol: UDP, authoritative server, client-side
prediction and reconciliation for the local player, snapshot
interpolation for everything else. Content transfer — recipes, community
maps, character packages — rides alongside it against a manifest of
fingerprints.

## Consequences

The netcode can be built for how this engine actually represents the
world, and prediction can be right rather than bolted on, which matters
because **S2** is unachievable over a network without it.

The cost is that this game and Unreal Tournament are separate
populations. A server runs one or the other. For a project whose target
is a single server the author controls that is acceptable; for a project
hoping to join an existing ecosystem it would not be.

What has to be true: client and server must run the same simulation code
and the same game rules, from the same source, or prediction diverges and
the fidelity this whole project is built for is lost at the one moment it
is most visible.

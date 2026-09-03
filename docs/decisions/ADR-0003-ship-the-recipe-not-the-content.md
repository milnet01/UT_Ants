# ADR-0003: Distribute recipes and code; never Epic's content, in the repository or over the network

- **Status:** Accepted
- **Date:** 2026-09-03

## Context

This is a public GitHub repository, and the game downloads content from
whichever server a player joins. Both are distribution.

Unreal Tournament was delisted by Epic in January 2023 and is not
officially available. Being free of charge for a period did not license
anyone else to redistribute it, and a re-lit, re-textured version of one
of Epic's maps is a derivative work of that map — improving something
does not transfer its authorship.

Community-made maps are a different matter: they are their authors' work,
and UT's own culture has always been that servers hand them to players on
join. That is how the 610-map rotation this project must support already
operates.

The problem is that a baked bundle (ADR-0002) blurs the line. It is our
geometry format, our materials and our lighting — over Epic's level
design.

## Decision

One quarantine directory, one rule:

> Nothing under `content/` is committed, published, or served to another
> player — and anything derived from a file under `content/` is itself
> under `content/`.

`content/` holds the player's own Unreal Tournament install, every bundle
baked from it, and everything downloaded from a host. It is gitignored as
a directory, and the Unreal asset extensions are gitignored across the
whole tree as a second line.

What travels instead is the **recipe**: our material assignments, fog
volumes, light-shaft placement, friendly map names and bot hints. The
recipe is entirely our own work. A joining player receives the recipe and
any community-authored map, and bakes against their own copy of the game.

## Consequences

The repository is publishable, and a stranger can clone and test it with
no Unreal Tournament present. Servers can distribute community content as
they always have. Nothing Epic owns moves.

The cost is a bake on first encounter with a map, rather than a download
and play. Bundles cannot be shared between players even though they are
identical, which wastes work that a less careful project would not waste.
And the rule has to be enforced mechanically — a guard in the pre-push
hook and in CI — because a single careless `git add` undoes it
permanently in a public history.

What has to be true: the game must refuse to start without a valid
Unreal Tournament install and say plainly why, or players will conclude
it is broken rather than that it is waiting for something.

This is a considered engineering position, not legal advice.

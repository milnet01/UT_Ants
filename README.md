# UT_Ants

A new engine for Unreal Tournament (1999) maps. It takes the maps you
already own, rebuilds them with modern lighting and materials, and plays
them — with bots that can finally get through Monster Hunt's door puzzles.

## Why

Unreal Tournament is still played, and its community still makes maps.
The engine is stuck in 1999, though:

- **Flat lighting.** Shadows are painted on when a map is built, so
  nothing casts a moving shadow.
- **Bots that cannot play Monster Hunt.** They walk into a closed door
  and stay there, because they do not understand switches and levers.
- **No way to fix it.** Epic never released the source code and no
  longer sells the game.

UT_Ants keeps the maps and replaces the engine.
[docs/discovery.md](docs/discovery.md) lists what "working" means as things
you could see for yourself — for example, opening one of your own maps and
recognising it instantly, but with moving shadows and real surface depth.

## How it works

There are two halves.

1. **The baker** reads a map from *your* copy of Unreal Tournament and
   converts it into a **bundle**: one file holding the level, upgraded
   textures, and the routes bots use to get around.
2. **The game** loads bundles and plays them. It never reads Unreal
   Tournament's own files directly.

**Nothing from Epic is copied into this project or sent over the
network.** You bring your own copy of the game, and everything made from
it stays on your machine.

## Status

Early — **there is nothing to play yet.** What exists so far is the
foundation the baker and the game will stand on:

- **Reading Unreal Tournament's files** — maps, textures, sounds, the
  game's class definitions, and the paths bots follow.
- **Understanding a level** — which room a spot belongs to (for an
  in-game map screen), where bots can walk, and which switch opens which
  door.
- **The bundle file format** that the baker writes and the game will read.
- **Texture compression and a memory budget**, so upgraded textures fit
  on the graphics card. In progress.
- **Shared plumbing** — error reporting, logging, file handling and
  running work on several processor cores at once.
- **The baker, `ut-bake`** — turns one of your maps into a bundle, and
  checks that a folder really holds Unreal Tournament. A bake holds the
  room map, the bot paths and the materials so far.

`ut-dump`, a developer tool for looking inside a map file, is partly
built.

Next come the rest of the baker — the level's shape, its lights and its
collision — and the renderer, which draws the result.
[ROADMAP.md](ROADMAP.md) is the up-to-date list; this section is a summary
of it.

## Building it

You do not need Unreal Tournament to build this or to run its tests.

You need CMake, Ninja, and a C++ compiler recent enough for C++23: GCC 14,
Clang 19, or Visual Studio 2022 version 17.10 — or anything newer.

You also need **Vulkan 1.3 or newer** for the renderer: its headers, its loader
and the `glslc` shader compiler. On Windows, install the LunarG Vulkan SDK. On
Linux, either that SDK or your distribution's packages — on Ubuntu,
`libvulkan-dev` and `glslc`. The Vulkan validation layers are worth having while
working on the renderer; nothing checks that they are installed.

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release   # set up a build folder
cmake --build build                                        # compile everything
ctest --test-dir build -L unit                             # run the automated tests
ctest --test-dir build -L device                           # run the renderer's tests
```

The renderer's tests draw real pictures, so they need a Vulkan driver. A machine
with no graphics card can use Mesa's software driver — on Ubuntu,
`mesa-vulkan-drivers`. Without a driver those tests fail rather than skip.

`./scripts/ci.sh` runs every check the online build runs, on your own
machine.

**Testing against a real install is optional.** Add
`-DUTA_REAL_ASSET_TESTS=ON -DUTA_UT_INSTALL_DIR=<your Unreal Tournament folder>`
to the first command to add a second set of tests that reads real map
files.

## Using it

The baker, `ut-bake`, is the first program you can run. It is built with
everything else, into `build/tools/ut-bake/`.

```sh
ut-bake --check "<your Unreal Tournament folder>"      # is this a usable install?
ut-bake --install "<your Unreal Tournament folder>" --out bakes "<a map file>"
```

The first command says whether the folder holds Unreal Tournament, and what
is missing if not. The second bakes one map into the `bakes` folder. The file
is named from everything it was baked from, so baking the same map again finds
the first bake instead of repeating it; add `--force` to bake it anyway. Each
command prints one line of JSON saying what happened.

Nothing plays a baked map yet — that is the game itself, `ut-ants`.

## Documentation

| | |
|---|---|
| [ROADMAP.md](ROADMAP.md) | What is planned, in progress and shipped |
| [CHANGELOG.md](CHANGELOG.md) | What shipped, when |
| [docs/discovery.md](docs/discovery.md) | What this is for, and how we would know it works |
| [docs/design.md](docs/design.md) | The shape — the parts, and what may touch what |
| [docs/decisions/](docs/decisions/) | Why a close call went the way it did |
| [docs/specs/](docs/specs/) | The contract for one feature, where one was needed |

## License

[GPL-3.0](LICENSE). The texture compressor in
[third_party/bc7enc/](third_party/bc7enc/) is someone else's work under the
MIT licence or the Unlicense; its README says which copy it is and where it
came from.

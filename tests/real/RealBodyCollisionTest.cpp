// UTA-0457's real-asset case, INV-9 of docs/specs/UTA-0457-body-collision.md:
// over UTA-0456's recordings on DM-Deck16][, our body fits wherever the
// original's did, moves freely where it moved freely, and finds the wall the
// original slid along. It PRINTS the original's floor hover and its gap from
// that wall at each rate, the figures that spec's SS 2 rests on.
//
// The recordings are read from the directory UTA_MOVEMENT_RECORDINGS names,
// one ut-record run, through readMovementRecording; with no such directory the
// case fails, never skips. Only rows summary.tsv marks `ok` are read: a rate
// the original missed is not pooled (UTA-0456 INV-7).
//
// THE LEVEL ONLY, NO MOVERS. A mover's MOVR shape needs the whole bake's
// materials; the four runs here ride no mover, and leaving one out can only
// make a recorded move freer, so the first two checks lose nothing that a
// mover in these runs' way would have shown.

#include "core/FileSystem.h"
#include "ubake/Collision.h"
#include "ubake/Name.h"
#include "upkg/Geometry.h"
#include "upkg/Level.h"
#include "upkg/Package.h"
#include "uworld/BodyCollision.h"

#include "support/MovementRecording.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#ifndef UTA_UT_INSTALL_DIR
#error "UTA_UT_INSTALL_DIR must be defined for the real-asset tier"
#endif

namespace {

namespace fs = std::filesystem;
using uta::uworld::BodySize;
using uta::uworld::BodyWorld;
using uta::uworld::SKIN;
using uta::uworld::Vec3;

/// SS 2: the original slides at most this far off a wall at any rate it held.
/// Measured 2026-11-10: about 0.8 at 30 fps once settled and 0.24 at 85, and up
/// to 1.57 on the slide's first ticks at 30. A wall missing from the mesh would
/// leave our sweep 5 units of free travel.
constexpr double MAX_WALL_GAP = 2;

const std::set<std::string> RUNS{"stand", "wall-angle", "stair", "ledge"};

uta::ubundle::CollisionTree levelTreeOf(const fs::path& mapPath) {
    const auto bytes = uta::fs::readFile(mapPath);
    REQUIRE(bytes.has_value());
    const auto map = uta::upkg::Package::open(*bytes);
    REQUIRE(map.has_value());
    const auto exports = map->exports();
    const uta::upkg::ExportEntry* levelExport = nullptr;
    for (const auto& entry : exports)
        if (entry.objectClass.kind() != uta::upkg::ObjectReferenceKind::Null
            && uta::ubake::detail::fold(std::string(map->objectName(entry.objectClass).value_or(""))) == "level")
            levelExport = &entry;
    REQUIRE(levelExport != nullptr);
    const auto level = uta::upkg::readLevel(*map, *levelExport);
    REQUIRE(level.has_value());
    REQUIRE(level->model.kind() == uta::upkg::ObjectReferenceKind::Export);
    const auto model = uta::upkg::readModel(*map, exports[level->model.index()]);
    REQUIRE(model.has_value());
    auto tree = uta::ubake::buildCollision(*model);
    REQUIRE(tree.has_value());
    return std::move(*tree);
}

struct Held {
    std::string run, rate;
};

/// summary.tsv's rows marked ok, for the runs this case reads.
std::vector<Held> heldRates(const fs::path& directory) {
    std::ifstream summary(directory / "summary.tsv");
    REQUIRE(summary.good());
    std::vector<Held> held;
    std::string line;
    std::getline(summary, line); // the header
    while (std::getline(summary, line)) {
        std::istringstream cells(line);
        std::vector<std::string> row;
        for (std::string cell; std::getline(cells, cell, '\t');) row.push_back(cell);
        if (row.size() >= 11 && row[10] == "ok" && RUNS.contains(row[0])) held.push_back({row[0], row[1]});
    }
    return held;
}

Vec3 at(const uta::test::RecordedTick& tick) { return {tick.location[0], tick.location[1], tick.location[2]}; }

} // namespace

TEST_CASE("UTA-0457 INV-9: our body fits and moves where the original did, on DM-Deck16][",
          "[real][body-collision]") {
    const char* recordings = std::getenv("UTA_MOVEMENT_RECORDINGS");
    INFO("UTA_MOVEMENT_RECORDINGS names a ut-record run's directory");
    REQUIRE(recordings != nullptr);
    const fs::path directory(recordings);

    uta::ubundle::Collision collision;
    collision.level = levelTreeOf(fs::path(UTA_UT_INSTALL_DIR) / "Maps" / "DM-Deck16][.unr");
    auto built = BodyWorld::build(collision, {});
    REQUIRE(built.has_value());
    const BodyWorld& world = *built;

    const auto held = heldRates(directory);
    REQUIRE(held.size() >= RUNS.size());
    std::size_t rows = 0, freeMoves = 0, wallTicks = 0;
    for (const Held& h : held) {
        std::ifstream file(directory / (h.run + "-" + h.rate + ".utarec"));
        REQUIRE(file.good());
        const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        const auto recording = uta::test::readMovementRecording(text);
        REQUIRE(recording.has_value());
        const BodySize body{recording->pawn.at("CollisionRadius"), recording->pawn.at("CollisionHeight")};
        const auto& ticks = recording->ticks;

        double hover = 0, gapSum = 0;
        std::size_t gaps = 0;
        for (std::size_t k = 0; k < ticks.size(); ++k) {
            INFO(h.run << " at " << h.rate << " fps, tick " << ticks[k].tick);
            ++rows;
            // First: the body fits where the original stood.
            CHECK(world.fits(body, at(ticks[k])));

            if (k + 1 < ticks.size()) {
                // Second: a move the original made with nothing in its way,
                // level on the ground or falling throughout, is free.
                const bool level = ticks[k].physics == "PHYS_Walking" && ticks[k + 1].physics == "PHYS_Walking"
                                   && std::abs(ticks[k].location[2] - ticks[k + 1].location[2]) < 1e-3;
                const bool falling = ticks[k].physics == "PHYS_Falling" && ticks[k + 1].physics == "PHYS_Falling";
                if (level || falling) {
                    ++freeMoves;
                    CHECK(world.sweep(body, at(ticks[k]), at(ticks[k + 1])).fraction == 1);
                }
            }

            if (ticks[k].physics == "PHYS_Walking") {
                const auto floor = uta::uworld::trace(collision.level, at(ticks[k]), at(ticks[k]) + Vec3{0, 0, -200});
                hover = floor.fraction * 200 - body.halfHeight;
            }

            // Third: wall-angle's slide along the wall facing -Y, where the
            // original runs at full speed along -X with no Y left (SS 2).
            if (h.run == "wall-angle" && std::abs(ticks[k].velocity[1]) < 1 && ticks[k].velocity[0] < -300) {
                ++wallTicks;
                const Vec3 from = at(ticks[k]);
                const auto hit = world.sweep(body, from, from + Vec3{0, 5, 0});
                CHECK(hit.fraction < 1);
                // Our body stops SKIN off the wall, so the original's gap is
                // our travel and the skin.
                const double gap = hit.fraction * 5 + SKIN;
                CHECK(gap <= MAX_WALL_GAP);
                gapSum += gap;
                ++gaps;
            }
        }
        std::cout << "UTA-0457 " << h.run << " " << h.rate << " fps: walking centre " << hover
                  << " above its half-height over the floor";
        if (gaps > 0) std::cout << "; sliding " << gapSum / static_cast<double>(gaps) << " off the wall";
        std::cout << "\n";
    }
    std::cout << "UTA-0457: " << rows << " rows fit, " << freeMoves << " free moves swept, " << wallTicks
              << " wall-slide ticks with the wall within " << MAX_WALL_GAP << "\n";
    CHECK(wallTicks > 0);
}

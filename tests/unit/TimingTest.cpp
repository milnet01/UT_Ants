// UTA-0129's phase times -- docs/specs/UTA-0129-benchmark-tool.md SS 4.1,
// INV-1 and INV-2.
//
// The clock is stepped by hand, so no case here measures a duration.
//
// NO TEST NAME CONTAINS A COMMA. Catch2 treats one as a filter separator.

#include "core/Jobs.h"
#include "core/Timing.h"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <vector>

using uta::Phase;
using uta::PhaseScope;
using uta::PhaseTimes;

TEST_CASE("UTA-0129 INV-1: a phase holds its scope's time and a child sits under its parent", "[core][timing]") {
    double now = 100;
    PhaseTimes times([&now] { return now; });
    {
        const PhaseScope outer(&times, "bake");
        now += 1; // in bake, before any child
        {
            const PhaseScope child(&times, "rooms");
            now += 2;
        }
        {
            const PhaseScope child(&times, "materials");
            now += 4;
        }
        {
            const PhaseScope again(&times, "rooms");
            now += 8;
        }
        // Still open, so not listed yet; its closed children are.
        const std::vector<Phase> inside = times.phases();
        REQUIRE(inside.size() == 2);
        CHECK(inside[0].name == "rooms");
        CHECK(inside[1].name == "materials");
    }
    {
        // The same name with nothing open around it is another phase.
        const PhaseScope top(&times, "rooms");
        now += 16;
    }

    const std::vector<Phase> phases = times.phases();
    REQUIRE(phases.size() == 4);
    // In the order each was first opened, not the order each closed.
    CHECK(phases[0].name == "bake");
    CHECK(phases[0].depth == 0);
    CHECK(phases[0].calls == 1);
    CHECK(phases[0].seconds == 15); // its children's time is in it
    CHECK(phases[1].name == "rooms");
    CHECK(phases[1].depth == 1);
    CHECK(phases[1].calls == 2);
    CHECK(phases[1].seconds == 10); // 2 and 8: one row, both times
    CHECK(phases[2].name == "materials");
    CHECK(phases[2].depth == 1);
    CHECK(phases[2].seconds == 4);
    CHECK(phases[3].name == "rooms");
    CHECK(phases[3].depth == 0);
    CHECK(phases[3].seconds == 16);
}

TEST_CASE("UTA-0129 SS 4.1: adopted phases go under the open phase and after what is recorded", "[core][timing]") {
    double now = 0;
    PhaseTimes times([&now] { return now; });
    const std::vector<Phase> child = {{"level", 0, 3, 1}, {"lights", 1, 2, 5}};
    {
        const PhaseScope first(&times, "name");
        now += 1;
    }
    {
        const PhaseScope outer(&times, "bake");
        times.adopt(child);
        now += 3;
    }
    {
        const PhaseScope last(&times, "encode");
        now += 1;
    }
    const std::vector<Phase> phases = times.phases();
    REQUIRE(phases.size() == 5);
    CHECK(phases[0].name == "name");
    CHECK(phases[1].name == "bake");
    CHECK(phases[2].name == "level");
    CHECK(phases[2].depth == 1);
    CHECK(phases[2].seconds == 3);
    CHECK(phases[3].name == "lights");
    CHECK(phases[3].depth == 2);
    CHECK(phases[3].calls == 5);
    CHECK(phases[4].name == "encode");
}

TEST_CASE("UTA-0129 INV-2: a scope on another thread or with no times records nothing", "[core][timing]") {
    double now = 0;
    PhaseTimes times([&now] { return now; });
    {
        const PhaseScope outer(&times, "materials");
        uta::JobSystem jobs(3);
        const std::size_t threw = jobs.parallelFor(64, [&times](std::size_t) {
            const PhaseScope inside(&times, "one-texture");
            times.adopt({{"smuggled", 0, 1, 1}});
        });
        CHECK(threw == 0);
        now += 5;
    }
    const std::vector<Phase> phases = times.phases();
    REQUIRE(phases.size() == 1);
    CHECK(phases[0].name == "materials");
    CHECK(phases[0].seconds == 5);
    CHECK(phases[0].calls == 1);

    const PhaseScope none(nullptr, "nothing"); // and its destructor must not touch anything
    CHECK(times.phases().size() == 1);
}

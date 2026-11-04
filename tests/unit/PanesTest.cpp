// UTA-0272: which materials are glass panes, by their texture's name.
//
// The names are real ones from the census recorded on UTA-0272, as
// umat::materialId spells them: folded to lower case, package first.
//
// NO TEST NAME CONTAINS A COMMA. Catch2 treats one as a filter separator.

#include "ubake/Panes.h"

#include <catch2/catch_test_macros.hpp>

using uta::ubake::isGlassPane;

TEST_CASE("UTA-0272: the census's glass names are panes", "[ubake][pane]") {
    for (const char* id : {"genfx.glass.glass2", "skycity.base.specglas", "utcrystal.glass.sglass",
                           "dm-fixture.tglass_r", "deck16.panels.window8", "texpkg.misc.nwindow4",
                           "metalmys.glass.sglasw6", "demglass.demglass1", "genfx.glass.glass2#masked"})
        CHECK(isGlassPane(id));
}

TEST_CASE("UTA-0272: names that are not glass are not panes", "[ubake][pane]") {
    for (const char* id : {"genfx.crystal.crystal", "genfx.misc.cryst2", "genfx.fire.ancflame2",
                           "liquids.water.cwater4", "skybox.sky.skyblu3", "decayeds.panels.drtpanelbase", ""})
        CHECK_FALSE(isGlassPane(id));
}

TEST_CASE("UTA-0272: only the texture's own name is read, never its package or group", "[ubake][pane]") {
    // A package or group named for glass holds frames and floors too.
    CHECK_FALSE(isGlassPane("glassworks.floors.metalfloor"));
    CHECK_FALSE(isGlassPane("texpkg.window.frame1"));
    CHECK(isGlassPane("texpkg.window.window1"));
    // The suffix is not part of the name: "#masked" cannot hide or make one.
    CHECK_FALSE(isGlassPane("texpkg.frame#window"));
}

// UTA-0014 INV-2 -- docs/specs/UTA-0014-vulkan-draw-path.md.
//
// No header of uta_urender that another subsystem may include declares, takes
// or returns a Vulkan type, so a translation unit including every such header
// compiles with no Vulkan headers.
//
// THIS FILE IS A TARGET OF ITS OWN, uta_render_header_isolation_test, which
// links uta_urender and NOT Vulkan::Vulkan. In uta_unit_tests it could never
// fail: INV-3's fixtures give that binary the Vulkan include path.
//
// TWO GRADERS, because one is not enough on Linux. On the MSVC leg the SDK's
// headers are on no default path, so a public header including them does not
// compile here. On Linux the distribution puts them in /usr/include, where an
// include would succeed -- so the #error below refuses the Vulkan header's own
// include guard instead, which it defines on every platform.

#include "urender/Renderer.h"

#ifdef VULKAN_CORE_H_
#error "a public urender header included the Vulkan headers -- UTA-0014 INV-2"
#endif

#include <catch2/catch_test_macros.hpp>

TEST_CASE("INV-2: urender's public header compiles with no Vulkan headers", "[render]") {
    // The fields most likely to invite a Vulkan type, named here so a change
    // to either shows up as a compile error in this target too.
    uta::urender::Config config;
    CHECK(config.surface == 0u);
    CHECK(uta::urender::Renderer::Target::Colour != uta::urender::Renderer::Target::Velocity);
}

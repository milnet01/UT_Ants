// The materials' staging buffer -- review-code 2026-10-15 (UTA-0365).
// vkCmdCopyBufferToImage needs each bufferOffset to be a multiple of the
// format's block size, 16 bytes for BC5 and BC7. A BC4 map with a full mip
// chain is 8 bytes past a multiple of 16, so a plain running total started
// the next BC7 map misaligned.

#include "urender/Materials.h"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <vector>

TEST_CASE("UTA-0365: every texture starts its staging run on a 16-byte boundary", "[urender][materials]") {
    // A BC4 4x4 map with its 2x2 and 1x1 levels is three 8-byte blocks, then
    // a BC7 map, then an RGBA8 fire picture of 3x1 texels, then one more.
    const std::vector<std::size_t> sizes{24, 16, 12, 16};
    const uta::urender::StagingLayout layout = uta::urender::stagingLayout(sizes);
    REQUIRE(layout.starts.size() == sizes.size());
    CHECK(layout.starts == std::vector<VkDeviceSize>{0, 32, 48, 64});
    CHECK(layout.total == 80);
    for (std::size_t i = 0; i < sizes.size(); ++i) {
        CAPTURE(i);
        CHECK(layout.starts[i] % 16 == 0);
        if (i + 1 < sizes.size()) CHECK(layout.starts[i] + sizes[i] <= layout.starts[i + 1]);
    }
}

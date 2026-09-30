// umat -- the flame list (UTA-0263 SS 4.1), beside the curated library's
// table, and keyed the same way.
//
// EVERY ENTRY IS A `flame` LINE OF tests/real/flame-labels.txt, and INV-3 checks
// the two agree over the reference install. Edit the labels first, then this.
//
// AN EDIT HERE CHANGES WHAT A BAKE WRITES, so it bumps ubake's BAKER_REVISION;
// tests/unit/FlameLibraryTest.cpp pins this table's digest to say so.

#include "umat/Flames.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <span>

namespace uta::umat {

namespace {

constexpr auto TABLE = std::to_array<FlameEntry>({
    {0x082f620e969dfa16ULL, "greatfire.ancsconc"},
    {0x0935c59586eceaf6ULL, "nalifx.fireplace"},
    {0x09fd26d244829138ULL, "hubeffects.smallfireh2"},
    {0x0d94bc67bac6ffd8ULL, "fireeng.torch1"},
    {0x143618b2df42ff1cULL, "nalifx.urn"},
    {0x1aa30f000d8e7116ULL, "greatfire.anctoast"},
    {0x1b8a3f525aca88ddULL, "chizraefx.lightning6"},
    {0x1c85aaf5f5bf4471ULL, "greatfire.ancflame2"},
    {0x2544bbf56d6a8eacULL, "nalifx.torches3"},
    {0x285c110c1eebd0c5ULL, "nalifx.torches4"},
    {0x32a617955bd38770ULL, "fireeng.jwfire1"},
    {0x34d42d0256e46081ULL, "greatfire.ancpurp"},
    {0x3dfe2a0508a22e42ULL, "mh-thefifthvortexv31-lift.emeraldfire1"},
    {0x543f2be626a928fdULL, "indus4.bluefire"},
    {0x6f85e54a89781c03ULL, "nalifx.urn-purp"},
    {0x785fd3d143988d1aULL, "greatfire.ancbutt"},
    {0x864682e2c63d4a47ULL, "greatfire.anchot"},
    {0x86be14078aa2983cULL, "nalifx.flames1"},
    {0x984d19cfee6d8096ULL, "indus4.redfire"},
    {0x9f26fbf614f58b65ULL, "fireeng.fire1"},
    {0xa1437f6d996156aeULL, "greatfire.ancflame3"},
    {0xaaaf550a917c904bULL, "greatfire.ancflame4"},
    {0xb53a0bdc81cb866dULL, "uttech1.donfire"},
    {0xb9a2897f0a3a7f2aULL, "greatfire.anclargeblu"},
    {0xba18ea6cfa0e7a85ULL, "nalifx.torches2"},
    {0xc2191fefe01af238ULL, "nalifx.urn2"},
    {0xce04010364d771a0ULL, "greatfire.anchoriz"},
    {0xd07fdf8ec9cb9501ULL, "hubeffects.smallfireh3"},
    {0xd1c8dab2e5dc9e8aULL, "greatfire.ancflame1"},
    {0xd57257c593f0f08aULL, "mh-hauntedhalloween.pumpkinflame"},
    {0xdeb051c95e4afe54ULL, "hubeffects.torchfire"},
    {0xfc3111a0e868655cULL, "ty_dxeffects.flame_b"},
});

constexpr bool sortedAndUnique() {
    for (std::size_t i = 1; i < TABLE.size(); ++i)
        if (TABLE[i - 1].fingerprint >= TABLE[i].fingerprint) return false;
    return true;
}
static_assert(sortedAndUnique(), "the flame list must be sorted by fingerprint with none twice");

} // namespace

std::span<const FlameEntry> flameLibrary() noexcept { return TABLE; }

bool isFlame(std::uint64_t fingerprint) noexcept {
    return std::ranges::binary_search(TABLE, fingerprint, {}, &FlameEntry::fingerprint);
}

} // namespace uta::umat

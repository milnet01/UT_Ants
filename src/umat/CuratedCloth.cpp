// umat -- the cloth list (UTA-0055 SS 4.6), beside the flame list, and keyed
// the same way.
//
// EVERY ENTRY IS A LINE OF tests/real/cloth-labels.txt, and
// tests/real/RealClothTest.cpp checks the two agree over the reference
// install. Edit the labels first, then this.
//
// AN EDIT HERE CHANGES WHAT A BAKE WRITES, so it bumps ubake's BAKER_REVISION;
// tests/unit/ClothLibraryTest.cpp pins this table's digest to say so.

#include "umat/Cloth.h"
#include "umat/Fingerprint.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <span>

namespace uta::umat {

namespace {

constexpr auto TABLE = std::to_array<ClothEntry>({
    {0x015cdd572823b2f4ULL, "trenchesfx.nziflg3"},
    {0x0198a3676f2e2ac0ULL, "botpack.jflag13"},
    {0x07eeb0c534f9e32bULL, "mh-um-castlearrggh.tapestwar"},
    {0x08714ea95aa3ef5aULL, "mh-girantownsbfix2.duepicflag3"},
    {0x1db6247cdadfa512ULL, "hourpitores_ut.pitoresflag1"},
    {0x21c009718bfbb5cfULL, "hourpitores_ut.pitoresdecotent1"},
    {0x27760027ccc05a96ULL, "mh-approachv0.bannerne2"},
    {0x2a593063544f5153ULL, "botpack.jflag11"},
    {0x2b8b281931468f3aULL, "mh-salmagundi.myflag"},
    {0x354c227bf892532bULL, "trenchesfx.nflagb"},
    {0x3dd613824a0783acULL, "mh-um-castlearrggh.tapestking"},
    {0x42e247f2dd54be79ULL, "skycity.sawning2"},
    {0x4e6d58d76cd0b769ULL, "mh-fnb-allgoodthingsv0.rapbanner500-v"},
    {0x527f16320a6f73abULL, "hourpitores_ut.pitoresflag1b"},
    {0x5e7dbef8c2bee171ULL, "hh3_vendetta.banner002"},
    {0x6cd7de23fdfb7581ULL, "genin.rug-d"},
    {0x6d119d80ae23a759ULL, "tyunr2.ty_flag"},
    {0x762225c54c59f35dULL, "hh3_vendetta.banner006"},
    {0x7f1c28e503667ce9ULL, "dmorbit.curtains"},
    {0x96fef08751091f2dULL, "mh-approachv0.bannerne1"},
    {0x9caec5513d443287ULL, "kasarultextures.tapestry"},
    {0xb62dde79ce000f7cULL, "dm-ctc-eldora.hourindus_decotent_i"},
    {0xbd1eb9a353b25b08ULL, "mh-fnb-allgoodthingsv0.rapbanner600-v"},
    {0xbe191f212e8e4b12ULL, "mh-mhmvillage-fixed.sail"},
    {0xc54abc428d4644f7ULL, "indus6.sail1a"},
    {0xc7f7bd8dcea7be80ULL, "genfx.rug-d"},
    {0xc98306d05d571010ULL, "mh-um-castlearrggh.tapesthunt"},
    {0xca5c6bb41b7640cfULL, "indus6.sail1"},
    {0xdefbb7cdb2f70df8ULL, "kasarultextures.tapestry1"},
    {0xe0e432099e9c9abaULL, "indus6.rbedhang"},
    {0xe1f9eb9dd0124e5fULL, "ddayfx.naziflag"},
    {0xe96cbbd6f951425cULL, "mh-cmc-partycrashers.flags"},
});

static_assert(detail::sortedByFingerprint(TABLE), "the cloth list must be sorted by fingerprint with none twice");

} // namespace

std::span<const ClothEntry> clothLibrary() noexcept { return TABLE; }

bool isCloth(std::uint64_t fingerprint) noexcept {
    return std::ranges::binary_search(TABLE, fingerprint, {}, &ClothEntry::fingerprint);
}

} // namespace uta::umat

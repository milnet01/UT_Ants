// UTA-0056 INV-10: the flak-announcer scenario meets SS 4.7's three targets
// at default settings, on the install's own sounds
// (docs/specs/UTA-0056-audio-mixer.md). The scenario is ut-mix's, so what
// this grades is what the tool lets a person hear.

#include "ut-mix/Scenario.h"

#include "RealSupport.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>

TEST_CASE("UTA-0056 INV-10: speech stays clear over the flak cannon at default settings", "[real][uaudio]") {
    const auto rendered = uta::tools::mix::renderFlakAnnouncer(std::filesystem::path(UTA_UT_INSTALL_DIR), {});
    if (!rendered) FAIL(rendered.error().message());
    const uta::tools::mix::Figures& f = rendered->figures;
    WARN("speech margin " << f.speechMarginDb << " dB; limiter " << f.limiterReductionDb << " dB; impact voices "
                          << f.mostImpactVoices << "; mix " << f.mixSeconds * 1000 << " ms for "
                          << f.soundSeconds << " s");
    CHECK(f.speechMarginDb >= 6.0);
    CHECK(f.limiterReductionDb <= 6.0);
    CHECK(f.mostImpactVoices <= 4);
}

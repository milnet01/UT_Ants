// ut-mix -- UTA-0056 SS 4.9. Renders the flak-announcer scenario from an
// install's sounds to a 16-bit stereo WAV at 48000 Hz, and prints SS 4.7's
// three figures, so the mix can be heard before the game plays anything.
// The output is derived from the install: write it under content/ or
// scratch, never into the repository.
//
//   ut-mix --install <UT dir> [--duck on|off] [--duck-db <dB>] --out <file.wav>

#include "ut-mix/Scenario.h"

#include "common/Options.h"
#include "core/FileSystem.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace {

constexpr std::string_view USAGE =
    "usage: ut-mix --install <UT dir> [--duck on|off] [--duck-db <dB>] --out <file.wav>\n";

void putLe(std::vector<std::byte>& out, std::uint32_t value, int width) {
    for (int i = 0; i < width; ++i) out.push_back(static_cast<std::byte>(value >> (8 * i) & 0xffu));
}

void putTag(std::vector<std::byte>& out, std::string_view tag) {
    for (const char c : tag) out.push_back(static_cast<std::byte>(c));
}

/// Interleaved stereo floats as a 16-bit PCM WAV.
std::vector<std::byte> wavOf(const std::vector<float>& frames) {
    constexpr std::uint32_t RATE = uta::uaudio::Mixer::RATE;
    const auto dataBytes = static_cast<std::uint32_t>(frames.size() * 2);
    std::vector<std::byte> out;
    putTag(out, "RIFF");
    putLe(out, 36 + dataBytes, 4);
    putTag(out, "WAVEfmt ");
    putLe(out, 16, 4);
    putLe(out, 1, 2); // PCM
    putLe(out, 2, 2);
    putLe(out, RATE, 4);
    putLe(out, RATE * 4, 4);
    putLe(out, 4, 2);
    putLe(out, 16, 2);
    putTag(out, "data");
    putLe(out, dataBytes, 4);
    for (const float sample : frames) {
        const auto value = static_cast<std::int16_t>(std::lround(std::clamp(sample, -1.0f, 1.0f) * 32767.0f));
        putLe(out, static_cast<std::uint16_t>(value), 2);
    }
    return out;
}

const char* verdict(bool met) { return met ? "met" : "MISSED"; }

} // namespace

int main(int argc, char** argv) {
    const std::vector<std::string_view> args(argv + 1, argv + argc);
    uta::tools::OptionValues values("ut-mix", args, std::cerr);
    std::string install, out;
    uta::uaudio::Settings settings;
    for (std::size_t at = 0; at < args.size(); ++at) {
        const std::string_view arg = args[at];
        std::optional<std::string_view> value;
        if (arg == "--install" && (value = values.take(at))) {
            install = *value;
        } else if (arg == "--out" && (value = values.take(at))) {
            out = *value;
        } else if (arg == "--duck" && (value = values.take(at)) && (*value == "on" || *value == "off")) {
            settings.duck = *value == "on";
        } else if (arg == "--duck-db" && (value = values.take(at))) {
            float db = 0;
            const auto [end, error] = std::from_chars(value->data(), value->data() + value->size(), db);
            if (error != std::errc{} || end != value->data() + value->size() || db > 0 || db < -24) {
                std::cerr << "ut-mix: --duck-db takes a number from -24 to 0\n";
                return 2;
            }
            settings.duckDb = db;
        } else {
            if (arg == "--duck" && value) std::cerr << "ut-mix: --duck takes on or off\n";
            else if (arg != "--install" && arg != "--out" && arg != "--duck" && arg != "--duck-db")
                std::cerr << "ut-mix: unknown argument " << arg << "\n" << USAGE;
            return 2;
        }
    }
    if (install.empty() || out.empty()) {
        std::cerr << USAGE;
        return 2;
    }

    const auto rendered = uta::tools::mix::renderFlakAnnouncer(install, settings);
    if (!rendered) {
        std::cerr << "ut-mix: " << rendered.error().message() << "\n";
        return 1;
    }
    if (auto written = uta::fs::writeFileAtomically(out, wavOf(rendered->frames)); !written) {
        std::cerr << "ut-mix: " << out << ": " << written.error().message() << "\n";
        return 1;
    }
    const uta::tools::mix::Figures& f = rendered->figures;
    std::cout << "wrote " << out << "\n"
              << "speech margin:     " << f.speechMarginDb << " dB (target >= 6: "
              << verdict(f.speechMarginDb >= 6) << ")\n"
              << "limiter reduction: " << f.limiterReductionDb << " dB (target <= 6: "
              << verdict(f.limiterReductionDb <= 6) << ")\n"
              << "most impact voices: " << f.mostImpactVoices << " (target <= 4: " << verdict(f.mostImpactVoices <= 4)
              << ")\n"
              << "mix time:          " << f.mixSeconds * 1000 << " ms for " << f.soundSeconds << " s of sound\n";
    return 0;
}

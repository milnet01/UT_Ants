#include "ut-mix/Scenario.h"

#include "core/Ascii.h"
#include "core/FileSystem.h"
#include "uaudio/Wav.h"
#include "upkg/Package.h"
#include "upkg/Sound.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace uta::tools::mix {
namespace {

using uaudio::Clip;
using uaudio::Group;
using uaudio::Mixer;

constexpr double RUN_SECONDS = 4.0;
constexpr double REFIRE_SECONDS = 0.77; // [fire flak-cannon primary] refire
constexpr int CHUNKS = 8;               // [fire flak-cannon primary] count
constexpr double CHUNK_STEP_SECONDS = 0.015;
constexpr double SPEECH_AT_SECONDS = 1.0;
constexpr double ATTACK_SECONDS = 0.05; // the duck's, SS 4.4

std::size_t frameAt(double seconds) { return static_cast<std::size_t>(std::llround(seconds * Mixer::RATE)); }

/// `sound`, a Sound export of `package` in `install`'s Sounds/, decoded.
Result<Clip> loadSound(const std::filesystem::path& install, std::string_view package, std::string_view sound) {
    const std::string where = std::string(package) + "'s " + std::string(sound);
    auto bytes = fs::readFile(install / "Sounds" / package);
    if (!bytes) return std::unexpected(bytes.error().withContext(std::string(package)));
    auto opened = upkg::Package::open(*bytes);
    if (!opened) return std::unexpected(opened.error().withContext(std::string(package)));
    for (const upkg::ExportEntry& entry : opened->exports()) {
        const auto name = opened->name(entry.objectName);
        const auto type = opened->objectName(entry.objectClass);
        if (!name || !type || !equalsIgnoringAsciiCase(*name, sound) || *type != "Sound") continue;
        auto stored = upkg::readSound(*opened, entry);
        if (!stored) return std::unexpected(stored.error().withContext(where));
        auto clip = uaudio::decodeWav(stored->data);
        if (!clip) return std::unexpected(clip.error().withContext(where));
        return clip;
    }
    return fail(ErrorCode::NotFound, std::string(package) + " has no sound " + std::string(sound));
}

double rms(std::span<const float> samples) {
    double sum = 0;
    for (const float s : samples) sum += static_cast<double>(s) * s;
    return samples.empty() ? 0 : std::sqrt(sum / static_cast<double>(samples.size()));
}

struct Event {
    std::size_t frame;
    const Clip* clip;
    Group group;
};

} // namespace

Result<Rendered> renderFlakAnnouncer(const std::filesystem::path& install, const uaudio::Settings& settings) {
    UTA_TRY(const Clip fire, loadSound(install, "UnrealShare.uax", "shot1"));
    UTA_TRY(const Clip impact, loadSound(install, "UnrealShare.uax", "Chunkhit1"));
    UTA_TRY(const Clip speech, loadSound(install, "Announcer.uax", "cd5min"));

    std::vector<Event> events;
    for (double shot = 0; shot < RUN_SECONDS; shot += REFIRE_SECONDS) {
        events.push_back({frameAt(shot), &fire, Group::Weapons});
        for (int chunk = 1; chunk <= CHUNKS; ++chunk)
            events.push_back({frameAt(shot + chunk * CHUNK_STEP_SECONDS), &impact, Group::Weapons});
    }
    const std::size_t speechStart = frameAt(SPEECH_AT_SECONDS);
    const auto speechFrames =
        static_cast<std::size_t>(std::ceil(static_cast<double>(speech.frames()) * Mixer::RATE / speech.rate));
    events.push_back({speechStart, &speech, Group::Speech});
    std::ranges::stable_sort(events, {}, &Event::frame);

    const std::size_t total = std::max(frameAt(RUN_SECONDS), speechStart + speechFrames);
    Rendered out;
    out.frames.assign(total * 2, 0.0f);
    std::array<std::vector<float>, uaudio::GROUP_COUNT> tapped;
    uaudio::GroupTaps taps;
    for (std::size_t g = 0; g < uaudio::GROUP_COUNT; ++g) {
        tapped[g].assign(total * 2, 0.0f);
        taps.buffers[g] = tapped[g];
    }

    Mixer mixer;
    mixer.setSettings(settings);
    std::chrono::steady_clock::duration mixing{};
    const auto mixTo = [&](std::size_t from, std::size_t to) {
        if (to <= from) return;
        uaudio::GroupTaps slice;
        for (std::size_t g = 0; g < uaudio::GROUP_COUNT; ++g)
            slice.buffers[g] = taps.buffers[g].subspan(from * 2, (to - from) * 2);
        const auto began = std::chrono::steady_clock::now();
        mixer.mix(std::span(out.frames).subspan(from * 2, (to - from) * 2), &slice);
        mixing += std::chrono::steady_clock::now() - began;
    };

    // Voices start only at a mix's first frame, so the most impact voices at
    // any frame is the most just after a request.
    std::size_t done = 0;
    for (const Event& event : events) {
        mixTo(done, event.frame);
        done = std::max(done, event.frame);
        mixer.play(*event.clip, {.group = event.group});
        out.figures.mostImpactVoices = std::max(out.figures.mostImpactVoices, mixer.voicesOf(impact));
    }
    mixTo(done, total);

    // SS 4.7 result 1: over the line, after the duck's attack.
    const std::size_t from = (speechStart + frameAt(ATTACK_SECONDS)) * 2;
    const std::size_t to = (speechStart + speechFrames) * 2;
    std::vector<float> others(to - from);
    for (std::size_t i = from; i < to; ++i)
        others[i - from] = tapped[static_cast<std::size_t>(Group::Music)][i] +
                           tapped[static_cast<std::size_t>(Group::World)][i] +
                           tapped[static_cast<std::size_t>(Group::Weapons)][i];
    const double speechRms = rms(std::span(tapped[static_cast<std::size_t>(Group::Speech)]).subspan(from, to - from));
    out.figures.speechMarginDb = 20.0 * std::log10(speechRms / rms(others));
    out.figures.limiterReductionDb = mixer.stats().limiterReductionDb;
    out.figures.mixSeconds = std::chrono::duration<double>(mixing).count();
    out.figures.soundSeconds = static_cast<double>(total) / Mixer::RATE;
    return out;
}

} // namespace uta::tools::mix

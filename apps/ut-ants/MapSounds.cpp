#include "MapSounds.h"

#include "core/Ascii.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace uta::client {
namespace {

// UT99's Actor defaults, used only where neither the actor nor its class
// family stores the property: spec SS 14 item 2.
constexpr std::uint8_t DEFAULT_RADIUS = 32;
constexpr std::uint8_t DEFAULT_VOLUME = 128;
constexpr std::uint8_t DEFAULT_PITCH = 64;

/// The sound properties one list holds, each null where it holds none.
struct Sound {
    const std::string* path = nullptr;
    const std::array<float, 3>* location = nullptr;
    const std::uint8_t* radius = nullptr;
    const std::uint8_t* volume = nullptr;
    const std::uint8_t* pitch = nullptr;

    /// `over`'s values where it has them, else these.
    [[nodiscard]] Sound under(const Sound& over) const noexcept {
        return {over.path ? over.path : path, over.location ? over.location : location,
                over.radius ? over.radius : radius, over.volume ? over.volume : volume,
                over.pitch ? over.pitch : pitch};
    }
};

Sound soundOf(const std::vector<ubundle::PropertyRecord>& properties) {
    Sound sound;
    for (const ubundle::PropertyRecord& property : properties) {
        if (property.arrayIndex != 0) continue;
        const std::string name = asciiFolded(property.name); // stored as spelled in the map
        if (name == "ambientsound") {
            if (property.kind == ubundle::ValueKind::Object) sound.path = std::get_if<std::string>(&property.value);
        } else if (name == "location") {
            sound.location = std::get_if<std::array<float, 3>>(&property.value);
        } else if (name == "soundradius") {
            sound.radius = std::get_if<std::uint8_t>(&property.value);
        } else if (name == "soundvolume") {
            sound.volume = std::get_if<std::uint8_t>(&property.value);
        } else if (name == "soundpitch") {
            sound.pitch = std::get_if<std::uint8_t>(&property.value);
        }
    }
    return sound;
}

} // namespace

EmitterScan ambientEmitters(const ubundle::Placements& placements, const uaudio::SoundBank& bank) {
    EmitterScan scan;
    for (const ubundle::ActorPlacement& actor : placements.actors) {
        if (actor.classIndex >= placements.classes.size()) continue;
        const Sound sound = soundOf(placements.classes[actor.classIndex].defaults).under(soundOf(actor.properties));
        if (sound.path == nullptr || sound.path->empty()) continue;
        const std::uint8_t volume = sound.volume ? *sound.volume : DEFAULT_VOLUME;
        if (volume == 0) continue;
        const uaudio::Clip* const clip = bank.find(*sound.path);
        if (clip == nullptr) {
            ++scan.missing;
            continue;
        }
        if (sound.radius == nullptr || sound.volume == nullptr || sound.pitch == nullptr) ++scan.guessed;
        scan.emitters.push_back(
            {.position = sound.location ? *sound.location : std::array<float, 3>{},
             .clip = clip,
             .radius = RADIUS_UNITS * (static_cast<float>(sound.radius ? *sound.radius : DEFAULT_RADIUS) + 1.0f),
             .volume = static_cast<float>(volume) / 255.0f,
             .pitch = static_cast<float>(sound.pitch ? *sound.pitch : DEFAULT_PITCH) / 64.0f});
    }
    return scan;
}

uaudio::Listener listenerAt(std::array<float, 3> location, std::array<std::int32_t, 3> rotation) noexcept {
    // As FlyCamera's strafe: the view looks along UT's rotated +X, and its
    // right is a quarter turn of yaw on from that, level whatever the pitch.
    const double yaw = static_cast<double>(rotation[1]) * 2.0 * std::numbers::pi / 65536.0;
    return {.position = location,
            .right = {static_cast<float>(-std::sin(yaw)), static_cast<float>(std::cos(yaw)), 0.0f}};
}

MapSounds::MapSounds(std::vector<Emitter> emitters) : emitters_(std::move(emitters)), voices_(emitters_.size()) {
    heard_.reserve(emitters_.size());
}

void MapSounds::update(const uaudio::Listener& listener, uaudio::Mixer& mixer) {
    heard_.clear();
    for (std::size_t i = 0; i < emitters_.size(); ++i) {
        const Emitter& emitter = emitters_[i];
        const uaudio::Placed placed = uaudio::place(listener, emitter.position, emitter.radius);
        const float gain = placed.gain * emitter.volume;
        if (gain > 0.0f) heard_.push_back({i, gain, placed.pan, voices_[i].valid() ? gain * KEEP : gain});
    }
    // The loudest first; the emitter's order settles a tie, so the choice
    // does not depend on the sort.
    const auto louder = [](const Heard& a, const Heard& b) {
        return a.rank != b.rank ? a.rank > b.rank : a.emitter < b.emitter;
    };
    const std::size_t wanted = std::min(VOICES, heard_.size());
    std::partial_sort(heard_.begin(), heard_.begin() + static_cast<std::ptrdiff_t>(wanted), heard_.end(), louder);

    // Walk the emitters once, with the wanted ones back in emitter order.
    std::sort(heard_.begin(), heard_.begin() + static_cast<std::ptrdiff_t>(wanted),
              [](const Heard& a, const Heard& b) { return a.emitter < b.emitter; });
    std::size_t next = 0;
    for (std::size_t i = 0; i < emitters_.size(); ++i) {
        uaudio::VoiceId& voice = voices_[i];
        if (next < wanted && heard_[next].emitter == i) {
            const Heard& heard = heard_[next++];
            if (!voice.valid()) {
                voice = mixer.startLoop(*emitters_[i].clip, {.group = uaudio::Group::World,
                                                             .gain = heard.gain,
                                                             .pan = heard.pan,
                                                             .pitch = emitters_[i].pitch});
            } else if (!mixer.setLoop(voice, heard.gain, heard.pan)) {
                voice = {}; // the mixer stole it; it may rejoin next frame
            }
        } else if (voice.valid()) {
            // Out of earshot, or crowded out by louder ones.
            mixer.stop(voice);
            voice = {};
        }
    }
}

} // namespace uta::client

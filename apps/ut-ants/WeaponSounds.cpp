// Weapons heard -- docs/specs/UTA-0467-weapon-sounds.md SS 4.5.

#include "WeaponSounds.h"

#include <algorithm>
#include <array>
#include <string>
#include <string_view>

namespace uta::client {

namespace {

std::array<float, 3> placeOf(const ugame::Vec3& at) {
    return {static_cast<float>(at.x), static_cast<float>(at.y), static_cast<float>(at.z)};
}

const ugame::FireMode& modeOf(const ugame::Weapon& weapon, int which) {
    return which == 0 ? weapon.primary : *weapon.alt;
}

} // namespace

WeaponSounds::WeaponSounds(const ugame::WeaponSet& set, const uaudio::SoundBank& bank) : set_(set), bank_(bank) {
    const std::vector<std::string> named = ugame::soundPaths(set);
    missing_ = static_cast<std::size_t>(
        std::ranges::count_if(named, [&bank](const std::string& path) { return bank.find(path) == nullptr; }));
}

void WeaponSounds::playAt(std::string_view path, const ugame::Vec3& at, const uaudio::Listener& listener,
                          uaudio::Mixer& mixer) const {
    if (path.empty()) return;
    const uaudio::Clip* const clip = bank_.find(path);
    if (clip == nullptr) return;
    const uaudio::Placed placed = uaudio::place(listener, placeOf(at), WEAPON_SOUND_RADIUS);
    // Out of earshot takes no voice from a sound that can be heard.
    if (placed.gain <= 0.0f) return;
    mixer.play(*clip, {.group = uaudio::Group::Weapons, .gain = placed.gain, .pan = placed.pan});
}

void WeaponSounds::update(const uaudio::Listener& listener, std::span<const ugame::Cue> cues,
                          std::span<const ugame::Body> bodies, uaudio::Mixer& mixer) {
    for (const ugame::Cue& cue : cues) playAt(ugame::soundOf(set_, cue), cue.at, listener, mixer);

    // A body that is gone takes its loop with it.
    for (std::size_t i = bodies.size(); i < loops_.size(); ++i)
        if (loops_[i].voice.valid()) mixer.stop(loops_[i].voice);
    loops_.resize(bodies.size());

    for (std::size_t i = 0; i < bodies.size(); ++i) {
        const ugame::Body& body = bodies[i];
        Loop& loop = loops_[i];
        const int now = ugame::loopingMode(set_, body);
        if (loop.mode >= 0 && (now != loop.mode || body.weapon != loop.weapon)) {
            if (loop.voice.valid()) mixer.stop(loop.voice);
            // The barrel spins down in the hand that held it: not for a
            // weapon being put away, and not for the dead.
            if (body.alive() && body.pending < 0 && body.weapon == loop.weapon)
                playAt(modeOf(set_.weapons[static_cast<std::size_t>(loop.weapon)], loop.mode).endSound,
                       body.move.location, listener, mixer);
            loop = {};
        }
        if (now < 0) continue;
        loop.weapon = body.weapon;
        loop.mode = now;
        const ugame::FireMode& mode = modeOf(set_.weapons[static_cast<std::size_t>(body.weapon)], now);
        const uaudio::Clip* const clip = bank_.find(mode.loopSound);
        if (clip == nullptr) continue;
        const uaudio::Placed placed = uaudio::place(listener, placeOf(body.move.location), WEAPON_SOUND_RADIUS);
        if (placed.gain <= 0.0f) {
            if (loop.voice.valid()) mixer.stop(loop.voice);
            loop.voice = {};
        } else if (!loop.voice.valid()) {
            loop.voice = mixer.startLoop(*clip, {.group = uaudio::Group::Weapons, .gain = placed.gain, .pan = placed.pan});
        } else if (!mixer.setLoop(loop.voice, placed.gain, placed.pan)) {
            loop.voice = {}; // the mixer stole it; it starts again next frame
        }
    }
}

} // namespace uta::client

// Weapons heard: a shot, a switch, a held mode's loop, an explosion --
// docs/specs/UTA-0467-weapon-sounds.md SS 4.5.
//
// The game says what happened (ugame::Cue) and which mode loops
// (ugame::loopingMode); the weapon set names the sound. This plays each
// through the mixer, placed where it happened. No SDL here: the unit tests
// drive it against a mixer with no device.
#pragma once

#include "uaudio/Mixer.h"
#include "uaudio/SoundBank.h"
#include "uaudio/Spatial.h"
#include "ugame/Game.h"

#include <cstddef>
#include <span>
#include <vector>

namespace uta::client {

/// World units a weapon's sound carries. A recollection of the engine's
/// value for a PlaySound given no radius, not a reading of it: spec SS 15
/// item 1.
inline constexpr float WEAPON_SOUND_RADIUS = 1600.0f;

class WeaponSounds {
public:
    /// Both must outlive this: it keeps the set to read and clips the bank owns.
    WeaponSounds(const ugame::WeaponSet& set, const uaudio::SoundBank& bank);

    /// Plays `cues`, a frame's worth, and keeps each body's loop: started
    /// when its mode begins to sound, moved with the body, stopped when it
    /// ends, with the mode's end-sound where the same weapon is still in
    /// hand. The caller excludes the mixer's mix() (Audio::withMixer). Once
    /// a rendered frame.
    void update(const uaudio::Listener& listener, std::span<const ugame::Cue> cues,
                std::span<const ugame::Body> bodies, uaudio::Mixer& mixer);

    /// Sound paths the set names that the bank does not hold: each is silent.
    [[nodiscard]] std::size_t missing() const noexcept { return missing_; }

private:
    /// A body's loop: the weapon and mode that sound, and its voice. The
    /// voice is invalid while the loop is out of earshot or its clip is
    /// missing; the weapon and mode still say a loop is on.
    struct Loop {
        int weapon = -1;
        int mode = -1;
        uaudio::VoiceId voice;
    };

    void playAt(std::string_view path, const ugame::Vec3& at, const uaudio::Listener& listener,
                uaudio::Mixer& mixer) const;

    const ugame::WeaponSet& set_;
    const uaudio::SoundBank& bank_;
    std::size_t missing_ = 0;
    std::vector<Loop> loops_; ///< per body
};

} // namespace uta::client

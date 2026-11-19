// A map's ambient sounds, placed in the world --
// docs/specs/UTA-0480-map-sounds.md SS 4.3 and 4.4.
//
// ambientEmitters reads each actor's AmbientSound and its radius, volume and
// pitch from the bundle's placements. MapSounds then keeps the loudest of
// them playing as mixer loops, moved and stopped as the listener moves.
// No SDL here: the unit tests drive it against a mixer with no device.
#pragma once

#include "uaudio/Mixer.h"
#include "uaudio/SoundBank.h"
#include "uaudio/Spatial.h"
#include "ubundle/Bundle.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace uta::client {

struct Emitter {
    std::array<float, 3> position{};
    const uaudio::Clip* clip = nullptr;
    float radius = 0.0f; ///< world units
    float volume = 0.0f; ///< 0 to 1
    float pitch = 1.0f;
};

/// World units of earshot for each step of an actor's SoundRadius. A
/// recollection of UT99's driver, not a reading of it: spec SS 14 item 1.
inline constexpr float RADIUS_UNITS = 25.0f;

struct EmitterScan {
    std::vector<Emitter> emitters;
    /// Actors naming a sound the bank does not hold.
    std::size_t missing = 0;
    /// Emitters whose radius, volume or pitch is stored by neither the actor
    /// nor its class family, so a built-in value stood in.
    std::size_t guessed = 0;
};

/// Every actor with an AmbientSound the bank holds and a volume above
/// nothing. A property is the actor's own, else its class family's default.
[[nodiscard]] EmitterScan ambientEmitters(const ubundle::Placements& placements, const uaudio::SoundBank& bank);

/// The listener at a camera's place, facing its yaw (65536 to a turn).
[[nodiscard]] uaudio::Listener listenerAt(std::array<float, 3> location, std::array<std::int32_t, 3> rotation) noexcept;

class MapSounds {
public:
    /// Ambient loops at once, of the mixer's 64 voices.
    static constexpr std::size_t VOICES = 16;
    /// How much louder a silent emitter must be to take a playing one's
    /// voice, so two near-equal hums do not swap every frame.
    static constexpr float KEEP = 1.1f;

    explicit MapSounds(std::vector<Emitter> emitters);

    /// Start, move and stop voices for a listener here. The caller excludes
    /// the mixer's mix() (Audio::withMixer). Once a rendered frame.
    void update(const uaudio::Listener& listener, uaudio::Mixer& mixer);

    [[nodiscard]] std::size_t emitterCount() const noexcept { return emitters_.size(); }

private:
    struct Heard {
        std::size_t emitter = 0;
        float gain = 0.0f;
        float pan = 0.0f;
        float rank = 0.0f; ///< gain, raised by KEEP for one already playing
    };
    std::vector<Emitter> emitters_;
    std::vector<uaudio::VoiceId> voices_; ///< per emitter; invalid when silent
    std::vector<Heard> heard_;            ///< each update's scratch, kept to allocate once
};

} // namespace uta::client

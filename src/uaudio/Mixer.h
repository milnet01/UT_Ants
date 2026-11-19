// The mixer -- docs/specs/UTA-0056-audio-mixer.md SS 4.3 to 4.6.
//
// Mixes any number of play requests into one stereo stream at 48000 Hz,
// float, interleaved left-right. Two mechanisms, one per defect UTA-0056
// names:
//
//   - Speech ducks the three groups below it by a fixed dip (SS 4.4), so the
//     announcer stays audible over gunfire.
//   - Voice limits (SS 4.5) collapse near-simultaneous requests of one clip,
//     cap each clip's voices and the total, so a burst of flak shrapnel is one
//     hit rather than dozens summed past full scale.
//
// A peak limiter (SS 4.6) is the guarantee that no sample leaves outside
// [-1, 1]; the voice limits do the real work.
//
// No SDL here: the client owns the device and calls mix() from its callback
// (SS 4.8). NOT THREAD-SAFE: the caller excludes mix() while it plays, stops
// or changes settings. mix() allocates nothing and takes no lock.
//
// LIFETIME: a voice points at its Clip. The caller keeps every clip it plays
// alive until stopAll(), or until the voice ends.

#pragma once

#include "uaudio/Wav.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace uta::uaudio {

/// The four groups, LOWEST priority first: a voice is stolen from the lowest
/// group, and speech ducks the other three.
enum class Group : std::uint8_t { Music, World, Weapons, Speech };
inline constexpr std::size_t GROUP_COUNT = 4;

/// The player's settings. SS 9: kept in memory with these defaults until the
/// settings menu (UTA-0120) saves them.
struct Settings {
    bool duck = true;
    /// The dip, in dB; clamped to -24..0.
    float duckDb = -12.0f;
    float master = 1.0f;
    /// Indexed by Group.
    std::array<float, GROUP_COUNT> groupVolume{1.0f, 1.0f, 1.0f, 1.0f};
};

struct PlayRequest {
    Group group = Group::World;
    float gain = 1.0f;
    /// -1 left to 1 right.
    float pan = 0.0f;
    /// Within the group: the lowest is stolen first.
    int priority = 0;
};

/// A voice that plays its clip round and round until stop() --
/// docs/specs/UTA-0480-map-sounds.md SS 4.1. It starts from silence, and
/// every change of its gain or pan is a ramp across one Mixer::BLOCK.
struct LoopRequest {
    Group group = Group::World;
    float gain = 0.0f;
    /// -1 left to 1 right.
    float pan = 0.0f;
    /// The clip's rate is multiplied by it; clamped to 0.5..2.
    float pitch = 1.0f;
    int priority = 0;
};

/// A playing voice. Zero is no voice: a dropped request returns it.
struct VoiceId {
    std::uint32_t value = 0;
    [[nodiscard]] bool valid() const noexcept { return value != 0; }
};

struct Stats {
    /// Each group's duck gain now, linear, indexed by Group. Speech's is 1.
    std::array<float, GROUP_COUNT> duckGain{};
    /// The limiter's deepest gain reduction since the last stats(), in dB:
    /// 0 when it never acted.
    float limiterReductionDb = 0.0f;
    /// Requests dropped since the mixer was made: no voice could be stolen.
    std::uint64_t dropped = 0;
};

/// Four caller-owned buffers, indexed by Group, each as long as mix()'s
/// output. Each receives that group's own share of the output, after its
/// volume, duck and the master volume and before the limiter. An empty span
/// is skipped.
struct GroupTaps {
    std::array<std::span<float>, GROUP_COUNT> buffers;
};

class Mixer {
public:
    static constexpr std::uint32_t RATE = 48000;
    static constexpr std::size_t MAX_VOICES = 64;
    /// Output frames mixed per pass, and the length of a loop's ramp.
    static constexpr std::size_t BLOCK = 256;

    void setSettings(const Settings& settings);
    [[nodiscard]] const Settings& settings() const noexcept { return settings_; }

    /// Start `clip` at the next mix(), after SS 4.5's three checks. An invalid
    /// id when the request was dropped.
    VoiceId play(const Clip& clip, PlayRequest request);
    /// Start `clip` as a loop. It skips SS 4.5's collapse and per-clip cap --
    /// its caller budgets it -- and counts toward MAX_VOICES, where it is
    /// stolen like any voice. An invalid id when no voice could be had.
    VoiceId startLoop(const Clip& clip, LoopRequest request);
    /// A playing loop's gain and pan, ramped in from the next mix(). False:
    /// no such loop, as after it was stolen.
    bool setLoop(VoiceId voice, float gain, float pan) noexcept;
    /// Stop one voice. A loop ramps out across one BLOCK and then frees its
    /// slot; a one-shot stops at once, as a stolen voice does. No such
    /// voice: nothing happens.
    void stop(VoiceId voice) noexcept;
    void stopAll() noexcept;

    /// Fill interleaved left-right frames; `out.size()` must be even. With
    /// `taps`, each non-empty buffer must be as long as `out`.
    void mix(std::span<float> out, const GroupTaps* taps = nullptr) noexcept;

    /// The readouts; resets the limiter's deepest reduction.
    [[nodiscard]] Stats stats() noexcept;
    /// How many voices of `clip` are playing.
    [[nodiscard]] int voicesOf(const Clip& clip) const noexcept;

private:
    struct Voice {
        const Clip* clip = nullptr; ///< null: the slot is free
        double position = 0;        ///< in the clip's frames
        double step = 0;            ///< clip frames per output frame
        float gain = 1;
        float left = 0;             ///< the pan law's gains
        float right = 0;
        Group group = Group::World;
        int priority = 0;
        std::uint64_t start = 0;    ///< the output frame it started at
        std::uint64_t order = 0;    ///< start order, for "oldest"
        std::uint32_t id = 0;
        // A loop alone uses the rest: each side's gain now, where it is
        // heading, its step a frame and the frames the ramp has left.
        bool loop = false;
        bool stopping = false;      ///< the slot frees when the ramp ends
        float leftNow = 0;
        float rightNow = 0;
        float leftStep = 0;
        float rightStep = 0;
        std::uint32_t ramp = 0;
    };
    Voice* collapseTarget(const Clip& clip) noexcept;
    Voice* slotFor(const Clip& clip, Group group) noexcept;
    Voice* freeOrStolen(Group group) noexcept;
    Voice* loopOf(VoiceId voice) noexcept;
    /// Render a loop: as render(), wrapping at the clip's end and ramping.
    void renderLoop(Voice& voice, float* into, std::size_t frames) noexcept;
    /// Render `voice` into `into` (interleaved, `frames` long), adding;
    /// returns the frames it played before it ended.
    std::size_t render(Voice& voice, float* into, std::size_t frames) noexcept;
    void mixBlock(float* out, std::size_t frames, const GroupTaps* taps, std::size_t tapAt) noexcept;

    Settings settings_;
    std::array<Voice, MAX_VOICES> voices_{};
    std::array<std::array<float, BLOCK * 2>, GROUP_COUNT> scratch_{};
    /// Per group, in dB: 0 is no dip.
    std::array<float, GROUP_COUNT> duckDb_{};
    float limiterGain_ = 1.0f;
    float limiterStep_ = 0.0f;
    float limiterDeepest_ = 1.0f;
    std::uint64_t frame_ = 0;
    std::uint64_t nextOrder_ = 1;
    std::uint32_t nextId_ = 1;
    std::uint64_t dropped_ = 0;
};

} // namespace uta::uaudio

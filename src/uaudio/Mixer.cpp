#include "uaudio/Mixer.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <tuple>

namespace uta::uaudio {
namespace {

constexpr std::uint64_t framesIn(double milliseconds) {
    return static_cast<std::uint64_t>(milliseconds * Mixer::RATE / 1000.0);
}

// SS 4.5 item 1: a request within this long of a voice of the same clip
// joins it.
constexpr std::uint64_t COLLAPSE_FRAMES = framesIn(40);
// SS 4.4: the duck's attack and release.
constexpr float ATTACK_FRAMES = static_cast<float>(framesIn(50));
constexpr float RELEASE_FRAMES = static_cast<float>(framesIn(500));
// SS 4.6: the limiter's return to unity.
constexpr float LIMITER_RELEASE_FRAMES = static_cast<float>(framesIn(100));

constexpr float MAX_COLLAPSED_GAIN = 2.0f;
constexpr int MAX_CLIP_CAP = static_cast<int>(Mixer::MAX_VOICES);

float gainOf(float db) noexcept { return std::pow(10.0f, db / 20.0f); }

std::size_t indexOf(Group group) noexcept { return static_cast<std::size_t>(group); }

struct Sides {
    float left = 0;
    float right = 0;
};

// Constant power: the centre gives each side cos(pi/4).
Sides panned(float pan) noexcept {
    const float angle = (std::clamp(pan, -1.0f, 1.0f) + 1.0f) * std::numbers::pi_v<float> / 4.0f;
    return {std::cos(angle), std::sin(angle)};
}

} // namespace

void Mixer::setSettings(const Settings& settings) {
    settings_ = settings;
    settings_.duckDb = std::clamp(settings_.duckDb, -24.0f, 0.0f);
}

Mixer::Voice* Mixer::collapseTarget(const Clip& clip) noexcept {
    Voice* newest = nullptr;
    for (Voice& voice : voices_) {
        if (voice.clip != &clip || voice.loop || frame_ - voice.start > COLLAPSE_FRAMES) continue;
        if (newest == nullptr || voice.order > newest->order) newest = &voice;
    }
    return newest;
}

// SS 4.5 items 2 and 3: the clip's oldest voice when the clip is at its cap,
// else a free slot, else the voice the total cap steals; null to drop.
Mixer::Voice* Mixer::slotFor(const Clip& clip, Group group) noexcept {
    const int cap = std::clamp(clip.voiceCap, 1, MAX_CLIP_CAP);
    Voice* oldestOfClip = nullptr;
    int ofClip = 0;
    for (Voice& voice : voices_) {
        if (voice.clip != &clip || voice.loop) continue;
        ++ofClip;
        if (oldestOfClip == nullptr || voice.order < oldestOfClip->order) oldestOfClip = &voice;
    }
    if (ofClip >= cap) return oldestOfClip;
    return freeOrStolen(group);
}

// SS 4.5 item 3: a free slot, else the voice the total cap steals; null to
// drop.
Mixer::Voice* Mixer::freeOrStolen(Group group) noexcept {
    Voice* victim = nullptr;
    for (Voice& voice : voices_) {
        if (voice.clip == nullptr) return &voice;
        if (voice.group > group) continue; // never from a higher group
        const auto rank = [](const Voice& v) { return std::tuple(v.group, v.priority, v.order); };
        if (victim == nullptr || rank(voice) < rank(*victim)) victim = &voice;
    }
    return victim;
}

VoiceId Mixer::play(const Clip& clip, PlayRequest request) {
    if (clip.frames() == 0) return {};
    if (Voice* const joined = collapseTarget(clip)) {
        joined->gain = std::min(MAX_COLLAPSED_GAIN, std::sqrt(joined->gain * joined->gain + request.gain * request.gain));
        return VoiceId{joined->id};
    }
    Voice* const slot = slotFor(clip, request.group);
    if (slot == nullptr) {
        ++dropped_;
        return {};
    }
    const Sides sides = panned(request.pan);
    *slot = Voice{.clip = &clip,
                  .position = 0,
                  .step = static_cast<double>(clip.rate) / RATE,
                  .gain = request.gain,
                  .left = sides.left,
                  .right = sides.right,
                  .group = request.group,
                  .priority = request.priority,
                  .start = frame_,
                  .order = nextOrder_++,
                  .id = nextId_++};
    if (nextId_ == 0) nextId_ = 1; // zero is no voice
    return VoiceId{slot->id};
}

VoiceId Mixer::startLoop(const Clip& clip, LoopRequest request) {
    if (clip.frames() == 0) return {};
    Voice* const slot = freeOrStolen(request.group);
    if (slot == nullptr) {
        ++dropped_;
        return {};
    }
    *slot = Voice{.clip = &clip,
                  .position = 0,
                  .step = static_cast<double>(clip.rate) / RATE * std::clamp(request.pitch, 0.5f, 2.0f),
                  .group = request.group,
                  .priority = request.priority,
                  .start = frame_,
                  .order = nextOrder_++,
                  .id = nextId_++,
                  .loop = true};
    if (nextId_ == 0) nextId_ = 1; // zero is no voice
    const VoiceId id{slot->id};
    setLoop(id, request.gain, request.pan);
    return id;
}

Mixer::Voice* Mixer::loopOf(VoiceId voice) noexcept {
    if (!voice.valid()) return nullptr;
    for (Voice& candidate : voices_)
        if (candidate.clip != nullptr && candidate.loop && candidate.id == voice.value) return &candidate;
    return nullptr;
}

bool Mixer::setLoop(VoiceId voice, float gain, float pan) noexcept {
    Voice* const loop = loopOf(voice);
    if (loop == nullptr || loop->stopping) return false;
    const Sides sides = panned(pan);
    loop->gain = gain;
    loop->left = sides.left;
    loop->right = sides.right;
    // One BLOCK from wherever the last ramp had got to.
    loop->leftStep = (sides.left * gain - loop->leftNow) / static_cast<float>(BLOCK);
    loop->rightStep = (sides.right * gain - loop->rightNow) / static_cast<float>(BLOCK);
    loop->ramp = static_cast<std::uint32_t>(BLOCK);
    return true;
}

void Mixer::stop(VoiceId voice) noexcept {
    if (!voice.valid()) return;
    for (Voice& candidate : voices_) {
        if (candidate.clip == nullptr || candidate.id != voice.value) continue;
        if (!candidate.loop) {
            candidate.clip = nullptr;
        } else if (!candidate.stopping) {
            setLoop(voice, 0.0f, 0.0f);
            // Keep the sides it had: a stop fades where the sound is.
            candidate.leftStep = -candidate.leftNow / static_cast<float>(BLOCK);
            candidate.rightStep = -candidate.rightNow / static_cast<float>(BLOCK);
            candidate.stopping = true;
        }
        return;
    }
}

void Mixer::stopAll() noexcept {
    for (Voice& voice : voices_) voice.clip = nullptr;
}

std::size_t Mixer::render(Voice& voice, float* into, std::size_t frames) noexcept {
    const Clip& clip = *voice.clip;
    const float* const samples = clip.samples.data();
    const std::size_t length = clip.frames();
    const bool stereo = clip.channels == 2;
    std::size_t played = 0;
    for (; played < frames && voice.position < static_cast<double>(length); ++played) {
        const auto at = static_cast<std::size_t>(voice.position);
        const std::size_t next = at + 1 < length ? at + 1 : at;
        const auto t = static_cast<float>(voice.position - static_cast<double>(at));
        float left = 0;
        float right = 0;
        if (stereo) {
            left = samples[at * 2] + (samples[next * 2] - samples[at * 2]) * t;
            right = samples[at * 2 + 1] + (samples[next * 2 + 1] - samples[at * 2 + 1]) * t;
        } else {
            left = right = samples[at] + (samples[next] - samples[at]) * t;
        }
        into[played * 2] += left * voice.left * voice.gain;
        into[played * 2 + 1] += right * voice.right * voice.gain;
        voice.position += voice.step;
    }
    if (voice.position >= static_cast<double>(length)) voice.clip = nullptr;
    return played;
}

void Mixer::renderLoop(Voice& voice, float* into, std::size_t frames) noexcept {
    const Clip& clip = *voice.clip;
    const float* const samples = clip.samples.data();
    const std::size_t length = clip.frames();
    const auto end = static_cast<double>(length);
    const bool stereo = clip.channels == 2;
    for (std::size_t i = 0; i < frames; ++i) {
        if (voice.ramp > 0) {
            voice.leftNow += voice.leftStep;
            voice.rightNow += voice.rightStep;
            if (--voice.ramp == 0) {
                if (voice.stopping) {
                    voice.clip = nullptr;
                    return;
                }
                // Land on the target exactly, whatever the steps rounded to.
                voice.leftNow = voice.left * voice.gain;
                voice.rightNow = voice.right * voice.gain;
            }
        }
        const auto at = static_cast<std::size_t>(voice.position);
        // The frame after the last is the first: the loop has no seam.
        const std::size_t next = at + 1 < length ? at + 1 : 0;
        const auto t = static_cast<float>(voice.position - static_cast<double>(at));
        float left = 0;
        float right = 0;
        if (stereo) {
            left = samples[at * 2] + (samples[next * 2] - samples[at * 2]) * t;
            right = samples[at * 2 + 1] + (samples[next * 2 + 1] - samples[at * 2 + 1]) * t;
        } else {
            left = right = samples[at] + (samples[next] - samples[at]) * t;
        }
        into[i * 2] += left * voice.leftNow;
        into[i * 2 + 1] += right * voice.rightNow;
        voice.position += voice.step;
        while (voice.position >= end) voice.position -= end;
    }
}

void Mixer::mixBlock(float* out, std::size_t frames, const GroupTaps* taps, std::size_t tapAt) noexcept {
    for (auto& buffer : scratch_) std::fill_n(buffer.data(), frames * 2, 0.0f);

    // Voices start only at a block's first frame, so speech plays from there
    // until its longest voice ends.
    std::size_t speechUntil = 0;
    for (Voice& voice : voices_) {
        if (voice.clip == nullptr) continue;
        const Group group = voice.group;
        if (voice.loop) {
            renderLoop(voice, scratch_[indexOf(group)].data(), frames);
            if (group == Group::Speech) speechUntil = frames;
            continue;
        }
        const std::size_t played = render(voice, scratch_[indexOf(group)].data(), frames);
        if (group == Group::Speech) speechUntil = std::max(speechUntil, played);
    }

    const float target = settings_.duckDb;
    const float attack = -target / ATTACK_FRAMES;
    const float release = -target / RELEASE_FRAMES;
    for (std::size_t i = 0; i < frames; ++i) {
        // SS 4.4: linear in dB, per frame, on the three groups below speech.
        const bool speaking = i < speechUntil;
        std::array<float, GROUP_COUNT> factor{};
        for (std::size_t g = 0; g < GROUP_COUNT; ++g) {
            float& dip = duckDb_[g];
            if (g == indexOf(Group::Speech) || !settings_.duck || release == 0) {
                dip = 0;
            } else if (speaking) {
                dip = dip > target ? std::max(target, dip - attack) : std::min(target, dip + release);
            } else {
                dip = std::min(0.0f, dip + release);
            }
            factor[g] = settings_.groupVolume[g] * (dip == 0 ? 1.0f : gainOf(dip)) * settings_.master;
        }

        float left = 0;
        float right = 0;
        for (std::size_t g = 0; g < GROUP_COUNT; ++g) {
            const float groupLeft = scratch_[g][i * 2] * factor[g];
            const float groupRight = scratch_[g][i * 2 + 1] * factor[g];
            left += groupLeft;
            right += groupRight;
            if (taps != nullptr && !taps->buffers[g].empty()) {
                taps->buffers[g][(tapAt + i) * 2] = groupLeft;
                taps->buffers[g][(tapAt + i) * 2 + 1] = groupRight;
            }
        }

        // SS 4.6. One gain for both channels of a frame. Just under 1 / peak,
        // so the product cannot round past full scale.
        const float peak = std::max(std::abs(left), std::abs(right));
        if (peak * limiterGain_ > 1.0f) {
            limiterGain_ = std::nextafter(1.0f / peak, 0.0f);
            limiterStep_ = (1.0f - limiterGain_) / LIMITER_RELEASE_FRAMES;
            limiterDeepest_ = std::min(limiterDeepest_, limiterGain_);
        }
        out[i * 2] = left * limiterGain_;
        out[i * 2 + 1] = right * limiterGain_;
        limiterGain_ = std::min(1.0f, limiterGain_ + limiterStep_);
    }
    frame_ += frames;
}

void Mixer::mix(std::span<float> out, const GroupTaps* taps) noexcept {
    const std::size_t frames = out.size() / 2;
    for (std::size_t done = 0; done < frames;) {
        const std::size_t count = std::min(BLOCK, frames - done);
        mixBlock(out.data() + done * 2, count, taps, done);
        done += count;
    }
}

Stats Mixer::stats() noexcept {
    Stats stats;
    for (std::size_t g = 0; g < GROUP_COUNT; ++g) stats.duckGain[g] = gainOf(duckDb_[g]);
    stats.limiterReductionDb = std::max(0.0f, -20.0f * std::log10(limiterDeepest_));
    stats.dropped = dropped_;
    limiterDeepest_ = limiterGain_;
    return stats;
}

int Mixer::voicesOf(const Clip& clip) const noexcept {
    return static_cast<int>(std::ranges::count_if(voices_, [&](const Voice& v) { return v.clip == &clip; }));
}

} // namespace uta::uaudio

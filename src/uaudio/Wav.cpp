#include "uaudio/Wav.h"

#include <cstring>
#include <optional>
#include <string>
#include <string_view>

namespace uta::uaudio {
namespace {

constexpr std::uint16_t FORMAT_PCM = 1;
constexpr std::uint32_t MIN_RATE = 4000;
constexpr std::uint32_t MAX_RATE = 96000;

std::uint32_t readU32(std::span<const std::byte> bytes, std::size_t at) {
    return static_cast<std::uint32_t>(bytes[at]) | static_cast<std::uint32_t>(bytes[at + 1]) << 8 |
           static_cast<std::uint32_t>(bytes[at + 2]) << 16 | static_cast<std::uint32_t>(bytes[at + 3]) << 24;
}

std::uint16_t readU16(std::span<const std::byte> bytes, std::size_t at) {
    return static_cast<std::uint16_t>(static_cast<unsigned>(bytes[at]) | static_cast<unsigned>(bytes[at + 1]) << 8);
}

bool tagIs(std::span<const std::byte> bytes, std::size_t at, std::string_view tag) {
    return std::memcmp(bytes.data() + at, tag.data(), 4) == 0;
}

std::unexpected<Error> malformed(std::string why) {
    return fail(ErrorCode::MalformedData, "a WAV sound " + std::move(why));
}

struct Format {
    std::uint16_t tag = 0;
    std::uint16_t channels = 0;
    std::uint32_t rate = 0;
    std::uint16_t bits = 0;
};

} // namespace

Result<Clip> decodeWav(std::span<const std::byte> bytes) {
    if (bytes.size() < 12 || !tagIs(bytes, 0, "RIFF") || !tagIs(bytes, 8, "WAVE"))
        return malformed("does not start with a RIFF WAVE header");

    std::optional<Format> format;
    std::optional<std::span<const std::byte>> data;
    // Each chunk is an id, a size and its bytes, padded to an even length. The
    // last chunk's pad byte may be missing; nothing after it is read.
    for (std::size_t at = 12; at + 8 <= bytes.size();) {
        const std::uint32_t size = readU32(bytes, at + 4);
        const std::size_t body = at + 8;
        if (size > bytes.size() - body)
            return malformed("has a chunk of " + std::to_string(size) + " bytes running past its end");
        if (tagIs(bytes, at, "fmt ")) {
            if (size < 16) return malformed("has a fmt chunk of only " + std::to_string(size) + " bytes");
            format = Format{readU16(bytes, body), readU16(bytes, body + 2), readU32(bytes, body + 4),
                            readU16(bytes, body + 14)};
        } else if (tagIs(bytes, at, "data")) {
            data = bytes.subspan(body, size);
        }
        at = body + size + (size & 1u);
    }
    if (!format) return malformed("has no fmt chunk");
    if (!data) return malformed("has no data chunk");
    if (format->tag != FORMAT_PCM)
        return fail(ErrorCode::UnsupportedVersion,
                    "a WAV sound in format tag " + std::to_string(format->tag) + " is not PCM; only tag 1 plays");
    if (format->channels != 1 && format->channels != 2)
        return malformed("has " + std::to_string(format->channels) + " channels; only 1 or 2 play");
    if (format->bits != 8 && format->bits != 16)
        return malformed("has " + std::to_string(format->bits) + "-bit samples; only 8 or 16 play");
    if (format->rate < MIN_RATE || format->rate > MAX_RATE)
        return malformed("plays at " + std::to_string(format->rate) + " Hz, outside 4000 to 96000");

    Clip clip;
    clip.rate = format->rate;
    clip.channels = format->channels;
    const std::size_t width = format->bits / 8u;
    const std::size_t count = data->size() / (width * clip.channels) * clip.channels;
    clip.samples.resize(count);
    for (std::size_t i = 0; i < count; ++i) {
        if (width == 1) {
            clip.samples[i] = (static_cast<float>(std::to_integer<unsigned>((*data)[i])) - 128.0f) / 128.0f;
        } else {
            const auto value = static_cast<std::int16_t>(readU16(*data, i * 2));
            clip.samples[i] = static_cast<float>(value) / 32768.0f;
        }
    }
    return clip;
}

} // namespace uta::uaudio

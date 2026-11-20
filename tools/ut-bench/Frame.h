// ut-bench's frame workload -- docs/specs/UTA-0129-benchmark-tool.md SS 4.4.
//
// Draws a baked map on the surfaceless path from given cameras, still and then
// moving between them, and says how long a frame took. The time is the
// renderer's own: submit and wait, with no present.
//
// Compiled into ut-bench, the unit tests and the device tests. Only the device
// tests draw a frame; the unit tests grade the arguments and the arithmetic.

#pragma once

#include "Cli.h"

#include <ostream>
#include <span>
#include <string_view>
#include <vector>

namespace uta::bench {

/// `args` are what follows the word `frame`. Returns the exit code: 0 when
/// every frame drew, 1 when the bundle, the cameras or the renderer failed, 2
/// when the arguments were wrong.
[[nodiscard]] int runFrame(std::span<const std::string_view> args, std::ostream& out, std::ostream& err,
                           const BuildInfo& build);

namespace detail {

struct FrameSpread {
    double min = 0, p25 = 0, median = 0, p75 = 0, p99 = 0, max = 0;
};

/// The smallest, the median, the largest and the 25th, 75th and 99th
/// percentiles of `milliseconds`. A percentile is nearest-rank: for the
/// 99th, the value at position ceil(0.99 n), counting from 1. Zeros when
/// there are none.
[[nodiscard]] FrameSpread frameSpreadOf(std::vector<double> milliseconds);

/// UTA-0491: a still view draws one frame over and over, so a frame that took
/// over this many times the view's median was held up by something else: a
/// program sharing the GPU, or a busy processor.
inline constexpr double DISTURBED_OVER_MEDIAN = 2.0;

struct Calm {
    std::size_t disturbed = 0; ///< frames over DISTURBED_OVER_MEDIAN medians
    FrameSpread spread;        ///< the spread of the frames that were not
};

/// The frames of one still view, sorted into disturbed and calm. A frame at
/// exactly DISTURBED_OVER_MEDIAN medians is calm.
[[nodiscard]] Calm calmOf(const std::vector<double>& milliseconds);

} // namespace detail

} // namespace uta::bench

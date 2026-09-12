// Running one compute shader on a device -- the shape UTA-0014's INV-6 and
// INV-7 grade shader arithmetic with: the real shader, dispatched over a
// table of cases, its results read back and compared on the CPU.
//
// Like every device fixture, a missing device FAILS the test (INV-5).

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace uta::test::render {

/// Dispatch `spirv`, whose local size is 16, over `invocations` elements.
/// `inputs` are bound as storage buffers 0 to n-1 and a zeroed output of
/// `outputBytes` at binding n; the output is returned.
[[nodiscard]] std::vector<std::byte> runCompute(std::span<const std::uint32_t> spirv,
                                                const std::vector<std::span<const std::byte>>& inputs,
                                                std::size_t outputBytes, std::uint32_t invocations);

} // namespace uta::test::render

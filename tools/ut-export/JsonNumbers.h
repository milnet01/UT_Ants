// Numbers as ut-export writes them into JSON -- UTA-0476. Shortest text that
// reads back to the same value, so a float written from a float stays short.

#pragma once

#include <array>
#include <charconv>
#include <cstddef>
#include <string>

namespace uta::tools::exporter {

template <typename T>
std::string number(T value) {
    char buf[32];
    const auto [end, error] = std::to_chars(buf, buf + sizeof buf, value);
    return error == std::errc{} ? std::string(buf, end) : std::string("0");
}

template <typename T, std::size_t N>
std::string list(const std::array<T, N>& values) {
    std::string out = "[";
    for (std::size_t i = 0; i < N; ++i) out += (i ? "," : "") + number(values[i]);
    return out + "]";
}

} // namespace uta::tools::exporter

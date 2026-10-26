// The option-value reader the C++ tools share -- UTA-0431.
//
// Header-only, like Json.h. One rule for every tool that uses it: the
// argument after a value-taking option is its value. An option given twice
// is refused, and so is one with nothing after it; each refusal names the
// tool and the option.

#pragma once

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <optional>
#include <ostream>
#include <span>
#include <string_view>
#include <system_error>
#include <vector>

namespace uta::tools {

class OptionValues {
public:
    /// `tool` opens each refusal written on `err`: "ut-bake: --recipe is given twice".
    OptionValues(std::string_view tool, std::span<const std::string_view> args, std::ostream& err)
        : tool_(tool), args_(args), err_(err) {}

    /// The value of the option at `args[at]`, moving `at` onto it; nullopt
    /// after saying on `err` why there is none.
    [[nodiscard]] std::optional<std::string_view> take(std::size_t& at) {
        const std::string_view option = args_[at];
        if (std::ranges::find(taken_, option) != taken_.end()) {
            err_ << tool_ << ": " << option << " is given twice\n";
            return std::nullopt;
        }
        if (at + 1 >= args_.size()) {
            err_ << tool_ << ": " << option << " needs a value\n";
            return std::nullopt;
        }
        taken_.push_back(option);
        return args_[++at];
    }

    /// take's value as a whole number of at least `least`, into `into`;
    /// false after saying on `err` what was wrong.
    [[nodiscard]] bool takeWhole(std::size_t& at, unsigned& into, unsigned least) {
        const std::string_view option = args_[at];
        const std::optional<std::string_view> text = take(at);
        if (!text) return false;
        unsigned read = 0;
        const auto [end, error] = std::from_chars(text->data(), text->data() + text->size(), read);
        if (error != std::errc{} || end != text->data() + text->size() || read < least) {
            err_ << tool_ << ": " << option << " takes a whole number of at least " << least << "\n";
            return false;
        }
        into = read;
        return true;
    }

private:
    std::string_view tool_;
    std::span<const std::string_view> args_;
    std::ostream& err_;
    std::vector<std::string_view> taken_; ///< the value-taking options read so far
};

} // namespace uta::tools

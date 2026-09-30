// Named, nested phases and how long each took --
// docs/specs/UTA-0129-benchmark-tool.md SS 4.1.
//
// What a benchmark needs from the code it times: where the time went, not
// only how much there was. A PhaseTimes is owned by the thread that made it.
// Work a phase hands to the job system counts toward the phase that waited for
// it, as wall-clock time; a scope opened on any other thread records nothing.

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace uta {

/// One named phase, as PhaseTimes recorded it.
struct Phase {
    std::string name;        ///< lower-case words joined by '-'
    unsigned depth = 0;      ///< 0 for a phase opened with none open
    double seconds = 0;      ///< every call's elapsed time, summed; its children's included
    std::uint64_t calls = 0;
};

class PhaseTimes {
public:
    using Clock = std::function<double()>; ///< seconds, never decreasing

    PhaseTimes(); ///< reads std::chrono::steady_clock
    explicit PhaseTimes(Clock clock);

    /// Every phase closed so far, in the order each was first opened. A phase
    /// is its name and the phases open around it: opened again, it adds to its
    /// row and makes no second one.
    [[nodiscard]] std::vector<Phase> phases() const;

    /// Add `child`'s phases under the phase now open, each one deeper by this
    /// one's open depth, after the phases already recorded.
    void adopt(const std::vector<Phase>& child);

private:
    friend class PhaseScope;

    struct Node {
        Phase phase;
        std::size_t parent = 0; ///< an index into nodes_, or ROOT, or ADOPTED
        double started = 0;
        bool closed = false;
    };

    /// False, recording nothing, on any thread but the owner's.
    bool open(std::string_view name);
    void close();

    Clock clock_;
    std::thread::id owner_;
    std::vector<Node> nodes_;       ///< in first-open order
    std::vector<std::size_t> open_; ///< the open phases, outermost first
};

/// Opens `name` in `times` and closes it when destroyed. `times` may be null.
class PhaseScope {
public:
    PhaseScope(PhaseTimes* times, std::string_view name);
    ~PhaseScope();
    PhaseScope(const PhaseScope&) = delete;
    PhaseScope& operator=(const PhaseScope&) = delete;

private:
    PhaseTimes* times_; ///< null when this scope recorded nothing
};

} // namespace uta

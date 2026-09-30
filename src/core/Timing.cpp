// Named, nested phases and how long each took -- Timing.h.

#include "core/Timing.h"

#include <chrono>
#include <utility>

namespace uta {
namespace {

constexpr std::size_t ROOT = ~std::size_t{0};
/// An adopted row's parent: no later scope is ever opened under one.
constexpr std::size_t ADOPTED = ROOT - 1;

} // namespace

PhaseTimes::PhaseTimes()
    : PhaseTimes([] {
          return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
      }) {}

PhaseTimes::PhaseTimes(Clock clock) : clock_(std::move(clock)), owner_(std::this_thread::get_id()) {}

std::vector<Phase> PhaseTimes::phases() const {
    std::vector<Phase> closed;
    for (const Node& node : nodes_)
        if (node.closed) closed.push_back(node.phase);
    return closed;
}

void PhaseTimes::adopt(const std::vector<Phase>& child) {
    if (std::this_thread::get_id() != owner_) return;
    const auto deeper = static_cast<unsigned>(open_.size());
    for (const Phase& phase : child)
        nodes_.push_back(Node{{phase.name, phase.depth + deeper, phase.seconds, phase.calls}, ADOPTED, 0, true});
}

bool PhaseTimes::open(std::string_view name) {
    if (std::this_thread::get_id() != owner_) return false;
    const std::size_t parent = open_.empty() ? ROOT : open_.back();
    std::size_t at = 0;
    while (at < nodes_.size() && !(nodes_[at].parent == parent && nodes_[at].phase.name == name)) ++at;
    if (at == nodes_.size())
        nodes_.push_back(Node{{std::string(name), static_cast<unsigned>(open_.size()), 0, 0}, parent, 0, false});
    nodes_[at].started = clock_();
    open_.push_back(at);
    return true;
}

void PhaseTimes::close() {
    Node& node = nodes_[open_.back()];
    open_.pop_back();
    node.phase.seconds += clock_() - node.started;
    ++node.phase.calls;
    node.closed = true;
}

PhaseScope::PhaseScope(PhaseTimes* times, std::string_view name)
    : times_(times != nullptr && times->open(name) ? times : nullptr) {}

PhaseScope::~PhaseScope() {
    if (times_ != nullptr) times_->close();
}

} // namespace uta

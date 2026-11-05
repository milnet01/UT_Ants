// The PROP section: the props a map places as models, in world space --
// docs/specs/UTA-0341-mesh-decorations.md SS 4.5 and SS 4.6. It is a
// Geometry under GEOM's rules, so GEOM's codec reads and writes it; this file
// names its refusals and hands it to the drawing as a mover standing still.

#include "ubundle/Bundle.h"
#include "ubundle/Sections.h"

#include <string>
#include <utility>

namespace uta::ubundle {
namespace detail {

Result<void> validateProps(const Geometry& props, ErrorCode code) {
    const Result<void> checked = validateGeometry(props, code);
    if (!checked.has_value()) return fail(code, "PROP: " + std::string(checked.error().message()));
    return {};
}

} // namespace detail

void applyProps(Bundle& bundle) {
    if (!bundle.props) return;
    if (!bundle.props->indices.empty()) {
        if (!bundle.movers) bundle.movers.emplace();
        bundle.movers->push_back(MoverShape{PROPS_EXPORT, {}, {}, {1, 1, 1}, std::move(*bundle.props)});
    }
    bundle.props.reset();
}

} // namespace uta::ubundle

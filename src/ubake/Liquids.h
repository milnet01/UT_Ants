// A liquid texture's look, as MATS carries it --
// docs/specs/UTA-0105-shader-liquids.md SS 4.1 to SS 4.3.
//
// BAKE-SIDE ONLY, as the rest of uta_ubake.
//
// THE SETTINGS ARE CARRIED, NOT INTERPRETED. Each byte is the texture's own
// property, or its class's default; urender's liquid.glsl turns them into
// motion, so a re-fit of its constants never needs a re-bake (SS 3).

#pragma once

#include "ubundle/Bundle.h"
#include "upkg/Texture.h"

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string_view>

namespace uta::ubake {

/// SS 4.1: the kind a texture class gives, from its folded name -- `wettexture`,
/// `icetexture` or `wavetexture`. Any other class, a subclass of these among
/// them, gives none.
[[nodiscard]] std::optional<ubundle::LiquidKind> liquidKindOf(std::string_view foldedClassName) noexcept;

/// SS 4.2: one byte setting by folded property name -- the texture's own, else
/// its class's default, else empty.
using LiquidSetting = std::function<std::optional<std::uint8_t>(std::string_view folded)>;

/// SS 4.2: the look of a `kind` texture `width` by `height` texels, its
/// settings from `setting`. A setting neither the texture nor its class sets
/// is 0, as UnrealScript leaves it. `palette` gives a Wave its ramp (SS 4.3).
/// Empty when a side is 0 or past ubundle::LIQUID_SIZE_MAX, which MATS refuses.
[[nodiscard]] std::optional<ubundle::LiquidLook> liquidLookOf(ubundle::LiquidKind kind, const LiquidSetting& setting,
                                                              std::uint32_t width, std::uint32_t height,
                                                              const upkg::Palette& palette);

/// SS 4.3: eight of `palette`'s colours at evenly spaced positions in luma
/// order, darkest first, as linear RGB. All zero for an empty palette.
[[nodiscard]] std::array<std::array<float, 3>, 8> liquidRampOf(const upkg::Palette& palette);

} // namespace uta::ubake

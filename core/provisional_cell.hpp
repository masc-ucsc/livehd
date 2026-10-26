// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <string_view>

namespace livehd {

// LiveHD-generated provisional cells (todo/livehd/2d-domplace.html): body-less
// netlist declarations whose models live in a separate bundle, never in the
// netlist. A consumer that cannot model one (simulation, STA) refuses it by
// this reserved name prefix without depending on the generator.
inline constexpr std::string_view kProvisionalDominoPrefix = "__lhd_domino_";

[[nodiscard]] constexpr bool is_provisional_domino_cell(std::string_view name) {
  return name.starts_with(kProvisionalDominoPrefix);
}

}  // namespace livehd

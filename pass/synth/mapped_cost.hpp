// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <optional>

#include "tmap.hpp"

namespace livehd::synth {
struct Mapped_cost {
  uint64_t gates                = 0;
  double   area                 = 0;
  double   maximum_region_delay = 0;      // ranking diagnostic, not whole-design STA
  bool     delay_ps             = false;  // false: unit-delay logic levels (untimed map), not ps
};
// Weight definition-region rows by actual emitted Sub instantiation counts.
// Absorbed/unreachable definitions contribute zero; shared definitions are
// counted for every physical copy. Refuse hierarchy cycles and count overflow.
std::optional<Mapped_cost> mapped_cost(const Mapped_design& design, const std::function<bool()>& admission = {});
}  // namespace livehd::synth

// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <stdexcept>

#include "inou/lefdef/physical.hpp"

namespace livehd::placement {
// Distinguish a supported but too-small crossing from unsupported technology.
struct Pg_via_no_fit : std::invalid_argument {
  using std::invalid_argument::invalid_argument;
};
struct Pg_via_request {
  std::string            lower, upper;
  // Available centered metal rectangles at this stack level, in DEF DBU.
  std::array<int64_t, 2> lower_size{}, upper_size{};
  bool                   lower_internal = false, upper_internal = false;
  // Immutable contacts (macro pins): keep the complete landing pad within the
  // supplied centered rectangle and retain minimum enclosure on that side.
  bool                   lower_fixed = false, upper_fixed = false;
};
struct Pg_via_array {
  physical::Via              via;
  std::array<std::string, 3> layers;
  int64_t                    rows = 0, columns = 0;
};
// Construct one adjacent-layer PG connection from LEF generate rules, with
// fixed-via array fallback. This is geometry construction, not a DRC verdict.
// The supported cut-spacing table subset is DEFAULT plus all-default entries;
// other tables fail explicitly. Full LEF58 routing-oracle checks are separate.
Pg_via_array make_pg_via(const physical::Technology&, int64_t dbu, const Pg_via_request&);
}  // namespace livehd::placement

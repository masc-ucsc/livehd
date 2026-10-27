// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <cstdint>
#include <map>
#include <string>

#include "hhds/graph.hpp"
#include "inou/lefdef/physical.hpp"

namespace livehd::placement {
struct Options;
struct Physical_size {
  uint64_t cells    = 0;
  double   area_um2 = 0;
};
// Standard-cell inventory: fixed macro area cannot subsidize logic growth.
// Reset and domino rail tree buffers (reset_tree.hpp) are structural fanout
// restructuring, not growth, and stay out of it.
// Replacements apply to every occurrence of the named definition, including
// nested/shared colors. Master overrides are private resize proposals.
using Master_changes = std::map<uint64_t, std::string>;
Physical_size physical_size(hhds::Graph& source, const physical::Technology& technology, hhds::Graph* replacement = nullptr,
                            const Master_changes& masters = {});
struct Growth_budget {
  Physical_size baseline;
  double        fraction = .03;
  uint64_t      rejected = 0;
  bool          allows(const Physical_size& candidate) const;
};
void        initialize_growth_budget(hhds::Graph& source, Options& options);
bool        growth_admissible(hhds::Graph& source, const Options& options, hhds::Graph* replacement = nullptr,
                              const Master_changes& masters = {});
void        require_growth_budget(hhds::Graph& source, const Options& options);
std::string growth_budget_report(hhds::Graph& source, const Options& options);
}  // namespace livehd::placement

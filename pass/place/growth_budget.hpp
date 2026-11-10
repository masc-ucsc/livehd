// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <cstdint>
#include <map>
#include <set>
#include <string>

#include "hhds/graph.hpp"
#include "inou/lefdef/physical.hpp"

namespace livehd::placement {
struct Options;
struct Physical_size {
  uint64_t cells          = 0;
  double   area_um2       = 0;
  uint64_t tree_cells     = 0;  // reset/rail tree buffers, outside cells and area_um2
  double   tree_um2       = 0;
  uint64_t constant_cells = 0;  // reporting subset of cells, still fully charged
  double   constant_um2   = 0;
};
// Standard-cell inventory: fixed macro area cannot subsidize logic growth.
// Reset and domino rail tree buffers (reset_tree.hpp) are structural fanout
// restructuring, not growth: they count no cell, and their area is tallied
// apart so that only what upsizing adds above the master they were minted
// with (Growth_budget::tree_master_um2) is charged.
// Replacements apply to every occurrence of the named definition, including
// nested/shared colors. Master overrides are private resize proposals.
using Master_changes = std::map<uint64_t, std::string>;
Physical_size physical_size(hhds::Graph& source, const physical::Technology& technology, hhds::Graph* replacement = nullptr,
                            const Master_changes& masters = {}, const std::set<std::string>* constant_masters = nullptr);
struct Growth_budget {
  Physical_size baseline;
  double        fraction        = .03;
  uint64_t      rejected        = 0;
  double        tree_master_um2 = 0;  // the area of the master trees mint (placement sets it; 0: unknown)
  // The area charged against the budget: the cells plus tree-buffer growth.
  double        charged_area(const Physical_size& candidate) const;
  // `share` scales the allowance: gate sizing is admitted against sizing_growth_share of it, so it cannot spend the
  // whole budget before buffer trees reach the high-fanout nets (eyeriss: 0.07 um2 left for a 271-load net).
  bool          allows(const Physical_size& candidate, double share = 1.0) const;
};
// The part of the growth allowance gate sizing may use; buffer insertion may use all of it.
inline constexpr double sizing_growth_share = 0.6;
void        initialize_growth_budget(hhds::Graph& source, Options& options);
bool        growth_admissible(hhds::Graph& source, const Options& options, hhds::Graph* replacement = nullptr,
                              const Master_changes& masters = {});
void        require_growth_budget(hhds::Graph& source, const Options& options);
std::string growth_budget_report(hhds::Graph& source, const Options& options);
}  // namespace livehd::placement

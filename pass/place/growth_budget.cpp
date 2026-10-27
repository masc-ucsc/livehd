// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "growth_budget.hpp"

#include <cmath>
#include <format>
#include <set>
#include <stdexcept>

#include "place.hpp"
#include "placement_technology.hpp"
#include "reset_tree.hpp"

namespace livehd::placement {
Physical_size physical_size(hhds::Graph& source, const physical::Technology& technology, hhds::Graph* replacement,
                            const Master_changes& masters) {
  std::map<std::string, Physical_size> memo;
  std::set<std::string>                active;
  const auto                           visit = [&](auto&& self, hhds::Graph& original) -> Physical_size {
    const std::string name(original.get_name());
    if (const auto found = memo.find(name); found != memo.end()) {
      return found->second;
    }
    if (!active.insert(name).second) {
      throw std::runtime_error("recursive hierarchy in physical growth inventory");
    }
    auto&         graph = replacement && replacement->get_name() == name ? *replacement : original;
    Physical_size result;
    for (auto node : graph.body().nodes()) {
      const auto io = node.get_subnode_io();
      if (!io || is_tree_buffer(node.get_name())) {
        continue;  // reset/rail tree buffers are structural, not growth
      }
      std::string master(io->get_name());
      if (&graph == replacement) {
        if (const auto changed = masters.find(node.get_debug_nid()); changed != masters.end()) {
          master = changed->second;
        }
      }
      if (const auto found = technology.macros.find(master); found != technology.macros.end()) {
        if (found->second.klass.starts_with("CORE")) {
          ++result.cells;
          result.area_um2 += found->second.width * found->second.height;
        }
      } else if (const auto child = io->get_graph(); child && master == io->get_name()) {
        const auto nested  = self(self, *child);
        result.cells      += nested.cells;
        result.area_um2   += nested.area_um2;
      } else {
        throw std::runtime_error("physical growth inventory has no LEF master: " + master);
      }
    }
    active.erase(name);
    memo.emplace(name, result);
    return result;
  };
  return visit(visit, source);
}
bool Growth_budget::allows(const Physical_size& candidate) const {
  return candidate.cells <= std::floor(baseline.cells * (1 + fraction) + 1e-9) && std::isfinite(candidate.area_um2)
         && candidate.area_um2 <= baseline.area_um2 * (1 + fraction) + 1e-9;
}
void initialize_growth_budget(hhds::Graph& source, Options& options) {
  options.growth_budget           = std::make_shared<Growth_budget>();
  options.growth_budget->baseline = physical_size(source, options.technology().lef);
}
bool growth_admissible(hhds::Graph& source, const Options& options, hhds::Graph* replacement, const Master_changes& masters) {
  if (!options.growth_budget) {
    return true;
  }
  const bool allowed = options.growth_budget->allows(physical_size(source, options.technology().lef, replacement, masters));
  options.growth_budget->rejected += !allowed;
  return allowed;
}
void require_growth_budget(hhds::Graph& source, const Options& options) {
  if (!growth_admissible(source, options)) {
    throw std::runtime_error("physical cell/area growth budget exceeded");
  }
}
std::string growth_budget_report(hhds::Graph& source, const Options& options) {
  if (!options.growth_budget) {
    return "null";
  }
  const auto  now    = physical_size(source, options.technology().lef);
  const auto& budget = *options.growth_budget;
  return std::format(
      R"({{"basis":"synthesized_standard_cells","fraction":{},"baseline_cells":{},"baseline_area_um2":{},"cells":{},"area_um2":{},"within_budget":{},"rejected_proposals":{}}})",
      budget.fraction,
      budget.baseline.cells,
      budget.baseline.area_um2,
      now.cells,
      now.area_um2,
      budget.allows(now),
      budget.rejected);
}
}  // namespace livehd::placement

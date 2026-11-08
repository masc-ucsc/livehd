// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "fp_repetition_graph.hpp"

#include <functional>
#include <limits>
#include <map>
#include <optional>
#include <stdexcept>

#include "graph/node_util.hpp"
#include "memory_proxy.hpp"
#include "pass/liberty/liberty_dff.hpp"

namespace livehd::placement {
namespace gu = graph_util;
Fp_rep_design fp_repetition_design(hhds::Graph& top, const physical::Technology& tech, const std::string& liberty) {
  const auto            sequential = liberty::sequential_cell_names(liberty);
  std::optional<double> flop_area;  // memory proxies and unmapped registers only
  const auto            smallest_flop = [&] {
    if (!flop_area) {
      flop_area = liberty::smallest_flop_area(liberty).value_or(0);
    }
    return *flop_area;
  };
  Fp_rep_design                       design;
  std::map<hhds::Gid, size_t>         ids;
  std::function<size_t(hhds::Graph&)> add = [&](hhds::Graph& g) -> size_t {
    if (auto found = ids.find(g.get_gid()); found != ids.end()) {
      return found->second;
    }
    const auto id = design.modules.size();
    ids.emplace(g.get_gid(), id);
    design.modules.push_back({std::string(g.get_name()), {}, {}});
    std::vector<Fp_rep_leaf>  leaves;
    std::vector<Fp_rep_child> children;
    for (auto n : g.body().nodes()) {
      const auto op = gu::type_op_of(n);
      if (op == Ntype_op::Memory) {
        const auto area = smallest_flop() > 0 ? memory_proxy_shape(n, smallest_flop()).area : 0;
        leaves.push_back({gu::logical_hier_name(gu::default_instance_name(n)), area, true});
        continue;
      }
      if (gu::is_type_register(n)) {
        leaves.push_back({gu::logical_hier_name(gu::default_instance_name(n)), smallest_flop(), false});
        continue;
      }
      if (op != Ntype_op::Sub || n.is_loop_subnode()) {
        continue;
      }
      const auto io = n.get_subnode_io();
      if (!io) {
        continue;
      }
      const auto master = std::string(io->get_name());
      if (const auto macro = tech.macros.find(master); macro != tech.macros.end()) {
        const double area = macro->second.width * macro->second.height;
        if (sequential.contains(master)) {
          leaves.push_back({gu::logical_hier_name(gu::default_instance_name(n)), area, false});
        } else if (fp_rep_memory_class(macro->second.klass)) {
          leaves.push_back({gu::logical_hier_name(gu::default_instance_name(n)), area, true});
        }
        continue;
      }
      if (auto child = n.get_subnode_graph()) {
        // Unnamed generated wrappers are transparent, as in flatten.
        auto name = gu::has_name(n) ? gu::logical_hier_name(gu::default_instance_name(n), true) : std::string{};
        children.push_back({std::move(name), add(*child)});
      }
    }
    design.modules[id].leaves   = std::move(leaves);
    design.modules[id].children = std::move(children);
    return id;
  };
  design.top = add(top);
  return design;
}
}  // namespace livehd::placement

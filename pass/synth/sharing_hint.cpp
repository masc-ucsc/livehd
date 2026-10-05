// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "sharing_hint.hpp"

#include <algorithm>
#include <format>
#include <limits>
#include <set>

#include "absl/container/flat_hash_map.h"
#include "node_util.hpp"

namespace livehd::synth {
namespace gu = graph_util;
Sharing_hint_result preserve_shared_cones(hhds::Graph& graph, uint32_t minimum_fanout,
                                          const std::function<bool(std::string_view)>& admission) {
  Sharing_hint_result result;
  if (!minimum_fanout) {
    result.completed = true;
    return result;
  }
  if (minimum_fanout < 2) {
    return result;
  }
  uint64_t   visits   = 0;
  const auto admitted = [&](std::string_view stage) { return !admission || ((visits++ % 1024) != 0) || admission(stage); };
  struct Row {
    hhds::Node_class             node;
    std::vector<hhds::Pin_class> inputs;
    uint64_t                     fanout = 0;
    bool                         scalar = false;
  };
  std::vector<Row>                              rows;
  absl::flat_hash_map<hhds::Node_class, size_t> index;
  int32_t                                       color = 0;
  for (auto node : graph.body().nodes(hhds::Node_order::forward)) {
    if (!admitted("sharing-hint-scan")) {
      return result;
    }
    Row row{node, {}, 0, true};
    for (auto output : node.out_sorted_pins()) {
      row.scalar &= gu::bits_of(output) == 1;
    }
    for (auto sink : node.inp_sorted_pins()) {
      for (auto driver : sink.get_driver_pins()) {
        row.inputs.push_back(driver);
      }
    }
    color = std::max(color, gu::node_color_of(node));
    index.emplace(node, rows.size());
    rows.push_back(std::move(row));
  }
  if (color == std::numeric_limits<int32_t>::max()) {
    return result;
  }
  for (const auto& row : rows) {
    for (auto driver : row.inputs) {
      const auto it = index.find(driver.get_master_node());
      if (it != index.end()) {
        ++rows[it->second].fanout;
      }
    }
  }
  if (admission && !admission("sharing-hint-cone")) {
    return result;
  }
  std::set<size_t> selected;
  for (size_t root = 0; root < rows.size(); ++root) {
    if (rows[root].fanout < minimum_fanout) {
      continue;
    }
    ++result.candidates;
    if (!rows[root].scalar) {
      ++result.scalar_rejections;
      continue;
    }
    const auto op = gu::type_op_of(rows[root].node);
    if (op != Ntype_op::And && op != Ntype_op::Or && op != Ntype_op::Xor) {
      continue;
    }
    std::vector<size_t>          pending{root};
    std::set<size_t>             cone;
    std::vector<hhds::Pin_class> leaves;
    bool                         valid = true;
    std::string                  rejection;
    while (!pending.empty()) {
      if (!admitted("sharing-hint-cone")) {
        return result;
      }
      const auto i = pending.back();
      pending.pop_back();
      if (!cone.insert(i).second) {
        continue;
      }
      const auto kind        = gu::type_op_of(rows[i].node);
      const bool fixed_shift = (kind == Ntype_op::SRA || kind == Ntype_op::SHL) && rows[i].node.get_sink_pin(1).has_driver()
                               && rows[i].node.get_sink_pin(1).get_driver_pin().is_const()
                               && gu::const_of(rows[i].node.get_sink_pin(1).get_driver_pin()).is_integer()
                               && !gu::const_of(rows[i].node.get_sink_pin(1).get_driver_pin()).has_unknowns()
                               && !gu::const_of(rows[i].node.get_sink_pin(1).get_driver_pin()).is_negative();
      const bool selector    = fixed_shift || (kind == Ntype_op::Get_mask && gu::bit_range(rows[i].node).has_value());
      if (cone.size() > 128 || (!rows[i].scalar && !selector)
          || (kind != Ntype_op::And && kind != Ntype_op::Or && kind != Ntype_op::Xor && kind != Ntype_op::Not && !selector)) {
        valid     = false;
        rejection = std::format("kind={} scalar={} cone_nodes={}", Ntype::get_name(kind), rows[i].scalar, cone.size());
        break;
      }
      for (auto driver : rows[i].inputs) {
        if (driver.is_const()) {
          continue;
        }
        if (gu::is_graph_input_pin(driver) || gu::type_op_of(driver.get_master_node()) == Ntype_op::Flop) {
          if (std::find(leaves.begin(), leaves.end(), driver) == leaves.end()) {
            leaves.push_back(driver);
          }
          if (leaves.size() > 6) {
            valid     = false;
            rejection = "external source pins >6";
          }
        } else {
          const auto it = index.find(driver.get_master_node());
          if (it == index.end()) {
            valid     = false;
            rejection = "driver outside body";
          } else {
            pending.push_back(it->second);
          }
        }
      }
      if (!valid) {
        break;
      }
    }
    if (valid) {
      ++result.roots;
      selected.insert(cone.begin(), cone.end());
    } else {
      ++result.closure_rejections;
      if (result.rejection_examples.size() < 4) {
        result.rejection_examples.push_back(std::move(rejection));
      }
    }
  }
  // Admission finishes before the first tag write. The graph belongs to the
  // provider's private preparation; its owner discards it on later refusal.
  for (auto i : selected) {
    gu::set_color(rows[i].node, color + 1);
  }
  result.nodes     = selected.size();
  result.completed = true;
  return result;
}
}  // namespace livehd::synth

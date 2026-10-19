// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "register_merge.hpp"

#include <algorithm>
#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "node_util.hpp"

namespace livehd::synth {

namespace {

namespace gu = livehd::graph_util;

// What the register is, as a string: every operand (a constant by value, a signal by the identity
// of its driver pin), the width and the sign. Empty when the register is not a plain one.
std::string register_key(const hhds::Node_class& node) {
  const auto q = node.get_driver_pin(0);
  if (q.is_invalid() || gu::bits_of(q) == 0) {
    return {};
  }
  std::string key = std::to_string(gu::bits_of(q)) + (gu::is_unsign(q) ? "u" : "s");
  for (auto sink : node.inp_sorted_pins()) {
    const auto drivers = sink.get_driver_pins();
    if (drivers.size() != 1) {
      return {};
    }
    key += '|' + std::to_string(sink.get_port_id()) + '=';
    if (drivers[0].is_const()) {
      key += 'c' + gu::const_of(drivers[0]).to_pyrope();
    } else {
      key += 'p' + std::to_string(drivers[0].get_class_index().value) + ':' + std::to_string(drivers[0].get_port_id());
    }
  }
  return key;
}

}  // namespace

std::optional<Register_merge_stats> merge_equal_registers(std::span<const std::shared_ptr<hhds::Graph>> definitions,
                                                          Preparation_budget*                           budget) {
  Register_merge_stats stats;
  constexpr uint64_t   kMaxRounds = 16;
  for (bool again = true; again && stats.rounds < kMaxRounds;) {
    again = false;
    ++stats.rounds;
    for (const auto& graph : definitions) {
      if (!graph) {
        continue;
      }
      absl::flat_hash_map<std::string, hhds::Node_class> survivors;
      std::vector<std::pair<hhds::Node_class, hhds::Node_class>> merges;  // duplicate, survivor
      for (auto node : graph->body().nodes()) {
        if (!admit_preparation(budget, "register-merge-scan")) {
          return std::nullopt;
        }
        if (gu::type_op_of(node) != Ntype_op::Flop) {
          continue;
        }
        auto key = register_key(node);
        if (key.empty()) {
          continue;
        }
        const auto [it, added] = survivors.emplace(std::move(key), node);
        if (!added) {
          merges.emplace_back(node, it->second);
        }
      }
      for (const auto& [duplicate, survivor] : merges) {
        if (!admit_preparation(budget, "register-merge")) {
          return std::nullopt;
        }
        const auto dup_q = duplicate.get_driver_pin(0);
        const auto sur_q = survivor.get_driver_pin(0);
        std::vector<hhds::Pin_class> readers;
        for (auto edge : dup_q.out_edges()) {
          readers.push_back(edge.sink);
        }
        for (const auto& sink : readers) {
          sink.del_sink(dup_q);
          sur_q.connect_sink(sink);
        }
        ++stats.registers;
        stats.bits += static_cast<uint64_t>(gu::bits_of(dup_q));
        gu::drop_drivers(duplicate);
        duplicate.del_node();
        again = true;
      }
    }
  }
  return stats;
}

}  // namespace livehd::synth

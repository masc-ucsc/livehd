// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "metrics.hpp"

#include <algorithm>
#include <format>
#include <map>
#include <set>

namespace livehd::synth {
std::string source_metrics_json(const Logic_network& source) {
  std::vector<bool> has_source(source.nodes.size()), reachable(source.nodes.size());
  for (Id id = 0; id < source.nodes.size(); ++id) {
    const auto& node = source.nodes[id];
    has_source[id]   = node.source || std::any_of(node.inputs.begin(), node.inputs.end(), [&](Id in) { return has_source[in]; });
  }
  uint64_t     direct = 0, constant = 0;
  std::set<Id> unique, logic;
  for (auto root : source.outputs) {
    reachable[root] = true;
    unique.insert(root);
    if (source.nodes[root].source) {
      ++direct;
    } else if (!has_source[root]) {
      ++constant;
    } else {
      logic.insert(root);
    }
  }
  uint64_t sources = 0, functions = 0;
  for (size_t id = source.nodes.size(); id-- > 0;) {
    if (!reachable[id]) {
      continue;
    }
    const auto& node = source.nodes[id];
    if (node.source) {
      ++sources;
    } else {
      ++functions;
    }
    for (auto in : node.inputs) {
      reachable[in] = true;
    }
  }
  return std::format(
      R"({{"outputs":{},"unique_outputs":{},"direct_source_outputs":{},"source_free_outputs":{},"logic_outputs":{},"unique_logic_outputs":{},"reachable_source_nodes":{},"reachable_function_nodes":{}}})",
      source.outputs.size(),
      unique.size(),
      direct,
      constant,
      source.outputs.size() - direct - constant,
      logic.size(),
      sources,
      functions);
}
std::string network_metrics_json(const Unate_network& network) {
  std::vector<uint64_t>  reads(network.nodes.size());
  std::vector<bool>      reachable(network.nodes.size());
  std::map<Id, unsigned> polarities;
  uint64_t               functions = 0, inverters = 0, shared = 0, function_reads = 0, literals = 0, ports = 0, used = 0;
  for (auto output : network.outputs) {
    ++reads[output];
    reachable[output] = true;
  }
  for (const auto& node : network.nodes) {
    for (auto port : node.ports) {
      ++reads[port];
    }
  }
  for (size_t id = network.nodes.size(); id-- > 0;) {
    const auto& node = network.nodes[id];
    if (reachable[id]) {
      for (auto port : node.ports) {
        reachable[port] = true;
      }
    }
    if (node.kind == Node_kind::source_inverter) {
      ++inverters;
    }
    if (node.kind != Node_kind::function) {
      continue;
    }
    ++functions;
    used           += reachable[id];
    shared         += reads[id] > 1;
    function_reads += reads[id];
    ports          += node.ports.size();
    for (const auto& term : node.terms) {
      literals += term.size();
    }
    polarities[node.origin] |= node.negative ? 2 : 1;
  }
  const auto twins = std::count_if(polarities.begin(), polarities.end(), [](const auto& item) { return item.second == 3; });
  return std::format(
      R"({{"function_producers":{},"source_inverters":{},"twin_pairs":{},"shared_function_producers":{},"function_reads":{},"endpoint_reads":{},"literal_occurrences":{},"function_ports":{},"reachable_function_producers":{},"unused_function_producers":{}}})",
      functions,
      inverters,
      twins,
      shared,
      function_reads,
      network.outputs.size(),
      literals,
      ports,
      used,
      functions - used);
}
}  // namespace livehd::synth

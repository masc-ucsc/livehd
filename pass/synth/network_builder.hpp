// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <algorithm>
#include <map>

#include "unate.hpp"

namespace livehd::synth::detail {
class Network_builder {
  const Logic_network&              source;
  const Recipe&                     recipe;
  Budget&                           budget;
  std::map<std::pair<Id, bool>, Id> producers;

public:
  Unate_network network;
  Network_builder(const Logic_network& s, const Recipe& r, Budget& b) : source(s), recipe(r), budget(b) {}
  Id input(Id origin, bool negative) {
    const auto key = std::pair{origin, negative};
    if (const auto it = producers.find(key); it != producers.end()) {
      return it->second;
    }
    Unate_node node;
    node.origin   = origin;
    node.negative = negative;
    if (negative) {
      node.kind = Node_kind::source_inverter;
      node.ports.push_back(input(origin, false));
      ++network.source_inverters;
    }
    const auto id = static_cast<Id>(network.nodes.size());
    network.nodes.push_back(std::move(node));
    producers.emplace(key, id);
    return id;
  }
  bool append(Id origin, bool negative, const std::vector<Id>& inputs, const Truth_table& table, const Form& form,
              const std::optional<Truth_table>& care = {}, const std::vector<Id>& sources = {}, bool functional = false) {
    Unate_node node;
    node.kind           = Node_kind::function;
    node.origin         = origin;
    node.negative       = negative;
    node.logical_inputs = node.witness_inputs = inputs;
    node.completion                           = table;
    node.care                                 = care;
    node.image_sources                        = sources;
    node.functional                           = functional;
    node.level                                = 1;
    std::map<std::pair<uint32_t, bool>, uint32_t> port_indices;
    for (uint32_t j = 0; j < inputs.size(); ++j) {
      for (uint32_t rail = 0; rail < 2; ++rail) {
        if (!(((rail ? form.negative : form.positive) >> j) & 1)) {
          continue;
        }
        if (!budget.spend()) {
          return false;
        }
        const auto key = std::pair{inputs[j], rail != 0};
        if (inputs[j] < source.nodes.size() && source.nodes[inputs[j]].source) {
          input(inputs[j], rail != 0);
        }
        const auto it = producers.find(key);
        if (it == producers.end()) {
          return false;
        }
        port_indices[{j, rail != 0}] = node.ports.size();
        node.ports.push_back(it->second);
        node.level = std::max(node.level, network.nodes[it->second].level + 1);
      }
    }
    for (const auto& cube : form.cubes) {
      if (!budget.spend(inputs.size() + 1)) {
        return false;
      }
      std::vector<uint32_t> term;
      for (uint32_t j = 0; j < inputs.size(); ++j) {
        if ((cube.care >> j) & 1) {
          term.push_back(port_indices.at({j, ((cube.ones >> j) & 1) == 0}));
        }
      }
      node.terms.push_back(std::move(term));
    }
    if (node.level > level_limit(recipe)) {
      return false;
    }
    network.depth                 = std::max(network.depth, node.level);
    network.max_support           = std::max(network.max_support, static_cast<uint32_t>(inputs.size()));
    network.max_literals          = std::max(network.max_literals, form.literals);
    network.max_series            = std::max(network.max_series, form.series);
    producers[{origin, negative}] = network.nodes.size();
    network.nodes.push_back(std::move(node));
    return true;
  }
  void outputs() {
    for (auto id : source.outputs) {
      if (source.nodes[id].source) {
        input(id, false);
      }
      network.outputs.push_back(producers.at({id, false}));
    }
  }
};
}  // namespace livehd::synth::detail

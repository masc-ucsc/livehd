// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "mapped_cost.hpp"

#include <cmath>
#include <limits>
#include <map>
#include <set>

#include "node_util.hpp"

namespace livehd::synth {
std::optional<Mapped_cost> mapped_cost(const Mapped_design& design, const std::function<bool()>& admission) {
  if (!design.top) {
    return {};
  }
  std::map<std::string, std::map<std::string, uint64_t>> children;
  std::vector<std::shared_ptr<hhds::Graph>>              pending{design.top};
  std::set<std::string>                                  visited;
  uint64_t                                               visits = 0;
  while (!pending.empty()) {
    auto graph = std::move(pending.back());
    pending.pop_back();
    const std::string name(graph->get_name());
    if (!visited.insert(name).second) {
      continue;
    }
    if (admission && !admission()) {
      return {};
    }
    auto& kids = children[name];
    for (auto node : graph->body().nodes()) {
      if (++visits > 2000000 || (admission && visits % 1024 == 0 && !admission())) {
        return {};
      }
      if (graph_util::type_op_of(node) != Ntype_op::Sub || graph_util::is_property_marker(node)) {
        continue;  // a marker (fproperty/lgassert/lgundef) has no hardware cost
      }
      auto io = node.get_subnode_io();
      if (!io) {
        return {};
      }
      const std::string child(io->get_name());
      ++kids[child];
      if (auto body = node.get_subnode_graph()) {
        pending.push_back(std::move(body));
      }
    }
  }
  std::map<std::string, uint32_t> indegree;
  for (const auto& [name, kids] : children) {
    indegree.try_emplace(name, 0);
    for (const auto& [child, count] : kids) {
      ++indegree[child];
    }
  }
  std::vector<std::string> ready;
  for (const auto& [name, count] : indegree) {
    if (!count) {
      ready.push_back(name);
    }
  }
  std::map<std::string, uint64_t> instances{
      {std::string(design.top->get_name()), 1}
  };
  uint64_t definitions = 0;
  while (!ready.empty()) {
    auto name = std::move(ready.back());
    ready.pop_back();
    ++definitions;
    const auto current = instances[name];
    const auto found   = children.find(name);
    if (found == children.end()) {
      continue;
    }
    for (const auto& [child, count] : found->second) {
      if (current && count > (std::numeric_limits<uint64_t>::max() - instances[child]) / current) {
        return {};
      }
      instances[child] += current * count;
      if (!--indegree[child]) {
        ready.push_back(child);
      }
    }
  }
  if (definitions != indegree.size()) {
    return {};
  }
  Mapped_cost result;
  result.delay_ps = design.delay_ps;
  std::set<std::string> priced;
  for (const auto& row : design.regions) {
    const auto count = instances[row.module];
    if (!count) {
      continue;
    }
    if (!priced.insert(row.module).second || row.gates < 0 || !std::isfinite(row.area) || row.area < 0 || !std::isfinite(row.delay)
        || row.div_blackbox) {
      return {};
    }
    const auto gates = static_cast<uint64_t>(row.gates);
    if (gates && count > (std::numeric_limits<uint64_t>::max() - result.gates) / gates) {
      return {};
    }
    result.gates += count * gates;
    result.area  += static_cast<double>(count) * row.area;
    if (count && std::isfinite(row.delay) && row.delay > result.maximum_region_delay) {
      result.maximum_region_delay = row.delay;
    }
  }
  if (!std::isfinite(result.area)) {
    return {};
  }
  return result;
}
}  // namespace livehd::synth

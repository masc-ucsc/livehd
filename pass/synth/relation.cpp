// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include <algorithm>
#include <array>
#include <bit>
#include <limits>
#include <map>
#include <set>

#include "unate.hpp"

namespace livehd::synth {
namespace {
struct Node_limit {};
struct Work_limit {};

// Reduced ordered BDDs over original source IDs. Unique nodes and exact ITE
// preserve correlations. Neither an allocation limit nor a work interruption
// can return a partial relation as a proof.
class Relation_bdd {
  struct Node {
    Id variable, low, high;
  };
  using Key                    = std::array<Id, 3>;
  static constexpr Id terminal = std::numeric_limits<Id>::max();
  std::vector<Node>   nodes{
      {terminal, 0, 0},
      {terminal, 1, 1}
  };
  std::map<Key, Id> unique, memo;
  uint32_t          limit;
  Budget&           budget;

  void spend(uint64_t amount = 1) {
    if (!budget.spend(amount)) {
      throw Work_limit{};
    }
  }
  Id make(Id variable, Id low, Id high) {
    spend(std::bit_width(unique.size()) + 1);
    if (low == high) {
      return low;
    }
    const Key key{variable, low, high};
    if (const auto it = unique.find(key); it != unique.end()) {
      return it->second;
    }
    if (nodes.size() - 2 >= limit) {
      throw Node_limit{};
    }
    const auto id = static_cast<Id>(nodes.size());
    nodes.push_back({variable, low, high});
    unique.emplace(key, id);
    return id;
  }

public:
  Relation_bdd(uint32_t bound, Budget& work) : limit(bound), budget(work) {}
  Id variable(Id id) { return make(id, 0, 1); }
  Id ite(Id condition, Id yes, Id no) {
    spend();
    if (condition == 0) {
      return no;
    }
    if (condition == 1 || yes == no) {
      return yes;
    }
    if (yes == 1 && no == 0) {
      return condition;
    }
    const Key key{condition, yes, no};
    spend(std::bit_width(memo.size()) + 1);
    if (const auto it = memo.find(key); it != memo.end()) {
      return it->second;
    }
    const auto top    = std::min({nodes[condition].variable, nodes[yes].variable, nodes[no].variable});
    const auto branch = [&](Id id, bool high) {
      const auto& node = nodes[id];
      return node.variable == top ? (high ? node.high : node.low) : id;
    };
    // No references into nodes survive a recursive call or vector growth.
    const auto low    = ite(branch(condition, false), branch(yes, false), branch(no, false));
    const auto high   = ite(branch(condition, true), branch(yes, true), branch(no, true));
    const auto result = make(top, low, high);
    // Apply-cache memory is bounded independently of recursive work.
    if (memo.size() >= uint64_t(limit) * 4) {
      memo.clear();
    }
    memo.emplace(key, result);
    return result;
  }
  Id compose(const Logic_node& node, const std::vector<Id>& values, uint32_t input = 0, uint32_t assignment = 0) {
    spend();
    if (input == node.inputs.size()) {
      return node.table.get(assignment) ? 1 : 0;
    }
    const auto low  = compose(node, values, input + 1, assignment);
    const auto high = compose(node, values, input + 1, assignment | (uint32_t{1} << input));
    return ite(values[node.inputs[input]], high, low);
  }
  void relation(const Logic_network& source, const std::vector<Id>& cone, const std::vector<Id>& leaves, std::optional<Id> root,
                Dependency_result& result) {
    std::vector<Id> values(source.nodes.size());
    for (auto id : result.sources) {
      values[id] = variable(id);
    }
    for (auto id : cone) {
      values[id] = compose(source.nodes[id], values);
    }
    // At most 2^12 divisor tuples, regardless of original source count.
    for (uint32_t tuple = 0; tuple < (uint32_t{1} << leaves.size()); ++tuple) {
      Id block = 1;
      for (uint32_t j = 0; j < leaves.size(); ++j) {
        block = ((tuple >> j) & 1) ? ite(values[leaves[j]], block, 0) : ite(values[leaves[j]], 0, block);
      }
      if (!block) {
        continue;
      }
      result.care.set(tuple, true);
      if (root) {
        const auto yes = ite(values[*root], block, 0);
        const auto no  = ite(values[*root], 0, block);
        if (yes && no) {
          result.status = Dependency_status::refuted;
          return;
        }
        result.table.set(tuple, yes != 0);
      }
    }
    result.status = Dependency_status::proven;
  }
};
Dependency_result source_relation(const Logic_network& source, std::optional<Id> root, const std::vector<Id>& leaves,
                                  uint32_t source_limit, Budget& budget, uint32_t symbolic_nodes) {
  Dependency_result result;
  if (!budget.spend(source.nodes.size() + leaves.size() + 1)) {
    return result;
  }
  if (!source.valid() || leaves.size() > max_logical_inputs || source_limit > max_logical_inputs || symbolic_nodes > max_symbolic_nodes
      || (root && *root >= source.nodes.size())) {
    result.status = Dependency_status::invalid;
    return result;
  }
  std::set<Id> unique;
  for (auto leaf : leaves) {
    if (leaf >= source.nodes.size() || (root && leaf >= *root) || !unique.insert(leaf).second) {
      result.status = Dependency_status::invalid;
      return result;
    }
  }
  std::vector<bool> seen(source.nodes.size());
  std::vector<Id>   pending(leaves), cone;
  if (root) {
    pending.push_back(*root);
  }
  while (!pending.empty()) {
    const auto id = pending.back();
    pending.pop_back();
    if (!budget.spend()) {
      return result;
    }
    if (seen[id]) {
      continue;
    }
    seen[id] = true;
    if (source.nodes[id].source) {
      result.sources.push_back(id);
      if (result.sources.size() > (symbolic_nodes ? max_symbolic_sources : source_limit)) {
        result.status = Dependency_status::unsupported;
        return result;  // No sampled absence is ever turned into a don't-care.
      }
    } else {
      cone.push_back(id);
      pending.insert(pending.end(), source.nodes[id].inputs.begin(), source.nodes[id].inputs.end());
    }
  }
  if (!budget.spend((cone.size() + result.sources.size()) * (std::bit_width(cone.size() + result.sources.size()) + 1))) {
    return result;
  }
  std::sort(cone.begin(), cone.end());
  std::sort(result.sources.begin(), result.sources.end());
  result.care  = Truth_table(static_cast<uint32_t>(leaves.size()));
  result.table = Truth_table(static_cast<uint32_t>(leaves.size()));
  if (result.sources.size() > source_limit) {
    result.symbolic = true;
    try {
      Relation_bdd bdd(symbolic_nodes, budget);
      bdd.relation(source, cone, leaves, root, result);
    } catch (const Node_limit&) {
      result.status = Dependency_status::unsupported;
    } catch (const Work_limit&) {
      result.status = Dependency_status::exhausted;
    }
    return result;
  }
  std::vector<bool> values(source.nodes.size());
  for (uint32_t x = 0; x < (uint32_t{1} << result.sources.size()); ++x) {
    if (!budget.spend(result.sources.size() + leaves.size() + 1)) {
      return result;
    }
    for (uint32_t j = 0; j < result.sources.size(); ++j) {
      values[result.sources[j]] = ((x >> j) & 1) != 0;
    }
    for (auto id : cone) {
      const auto& node = source.nodes[id];
      if (!budget.spend(node.inputs.size() + 1)) {
        return result;
      }
      uint32_t index = 0;
      for (uint32_t j = 0; j < node.inputs.size(); ++j) {
        index |= uint32_t(values[node.inputs[j]]) << j;
      }
      values[id] = node.table.get(index);
    }
    uint32_t tuple = 0;
    for (uint32_t j = 0; j < leaves.size(); ++j) {
      tuple |= uint32_t(values[leaves[j]]) << j;
    }
    if (root) {
      const auto value = values[*root];
      if (result.care.get(tuple) && result.table.get(tuple) != value) {
        result.status = Dependency_status::refuted;
        return result;  // Two original-source assignments disagree in one divisor block.
      }
      result.table.set(tuple, value);
    }
    result.care.set(tuple, true);
  }
  result.status = Dependency_status::proven;
  return result;
}
}  // namespace

Image_result exact_image(const Logic_network& source, const std::vector<Id>& leaves, uint32_t source_limit, Budget& budget,
                         uint32_t symbolic_nodes) {
  auto       relation = source_relation(source, {}, leaves, source_limit, budget, symbolic_nodes);
  const auto status   = relation.status == Dependency_status::proven        ? Status::feasible
                        : relation.status == Dependency_status::invalid     ? Status::invalid
                        : relation.status == Dependency_status::unsupported ? Status::unsupported
                                                                            : Status::search_exhausted;
  return {status, std::move(relation.care), std::move(relation.sources), relation.symbolic};
}

Dependency_result exact_dependency(const Logic_network& source, Id root, const std::vector<Id>& divisors, uint32_t source_limit,
                                   Budget& budget, uint32_t symbolic_nodes) {
  return source_relation(source, root, divisors, source_limit, budget, symbolic_nodes);
}

}  // namespace livehd::synth

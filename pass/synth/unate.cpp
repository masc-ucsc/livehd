// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "unate.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <functional>
#include <limits>
#include <map>
#include <numeric>
#include <set>
#include <stdexcept>
#include <tuple>
#include <utility>

namespace livehd::synth {
namespace {
constexpr Id       absent     = std::numeric_limits<Id>::max();

bool valid_table(const Truth_table& t) {
  if (t.inputs > max_logical_inputs || t.words.size() != ((uint32_t{1} << t.inputs) + 63) / 64) {
    return false;
  }
  const auto bits = uint32_t{1} << t.inputs;
  return bits >= 64 || (t.words.back() >> bits) == 0;
}

bool covers(const Cube& c, uint32_t v) { return (v & c.care) == c.ones; }

// Independent of the cover builder. A cut must separate its root from every
// original source; no image don't-cares or sampled assumptions are introduced.
// Per-thread scratch for cut_table: it runs once per enumerated cut, so
// allocating (and zeroing) node-count arrays per call made cut enumeration and
// verify() quadratic in the region size. Generation stamps replace the clears.
struct Cut_scratch {
  std::vector<uint32_t> leaf_stamp, seen_stamp;
  std::vector<uint8_t>  values;
  uint32_t              generation = 0;
  void                  prepare(size_t nodes) {
    if (leaf_stamp.size() < nodes) {
      leaf_stamp.resize(nodes, 0);
      seen_stamp.resize(nodes, 0);
      values.resize(nodes, 0);
    }
    if (++generation == 0) {
      std::fill(leaf_stamp.begin(), leaf_stamp.end(), 0);
      std::fill(seen_stamp.begin(), seen_stamp.end(), 0);
      generation = 1;
    }
  }
};

bool cut_table(const Logic_network& g, Id root, const std::vector<Id>& leaves, Truth_table& result, Budget& budget,
               uint32_t* cone_size = nullptr) {
  if (leaves.size() > max_logical_inputs || root >= g.nodes.size() || !budget.spend(leaves.size() + 1)) {
    return false;
  }
  thread_local Cut_scratch scratch;
  scratch.prepare(g.nodes.size());
  const auto gen = scratch.generation;
  for (auto leaf : leaves) {
    if (leaf >= root || scratch.leaf_stamp[leaf] == gen) {
      return false;
    }
    scratch.leaf_stamp[leaf] = gen;
  }
  std::vector<Id> pending{root}, cone;
  while (!pending.empty()) {
    const auto id = pending.back();
    pending.pop_back();
    if (!budget.spend()) {
      return false;
    }
    if (scratch.leaf_stamp[id] == gen || scratch.seen_stamp[id] == gen) {
      continue;
    }
    scratch.seen_stamp[id] = gen;
    if (g.nodes[id].source) {
      return false;
    }
    cone.push_back(id);
    for (auto in : g.nodes[id].inputs) {
      pending.push_back(in);
    }
  }
  std::sort(cone.begin(), cone.end());
  if (cone_size) {
    *cone_size = static_cast<uint32_t>(cone.size());
  }
  result       = Truth_table(static_cast<uint32_t>(leaves.size()));
  auto& values = scratch.values;
  for (uint32_t x = 0; x < (uint32_t{1} << leaves.size()); ++x) {
    if (!budget.spend(cone.size() + leaves.size() + 1)) {
      return false;
    }
    for (uint32_t j = 0; j < leaves.size(); ++j) {
      values[leaves[j]] = ((x >> j) & 1) != 0;
    }
    for (auto id : cone) {
      uint32_t    index = 0;
      const auto& n     = g.nodes[id];
      for (uint32_t j = 0; j < n.inputs.size(); ++j) {
        index |= uint32_t(values[n.inputs[j]]) << j;
      }
      values[id] = n.table.get(index);
    }
    result.set(x, values[root]);
  }
  return true;
}

struct Choice {
  std::vector<Id>            leaves;
  std::vector<Id>            witness_leaves;
  Truth_table                table;
  Form                       form;
  uint32_t                   level     = 0;
  double                     area_flow = 0;
  bool                       valid     = false;
  std::optional<Truth_table> care;
  std::vector<Id>            image_sources;
  bool                       functional = false;
};
using Choices      = std::vector<std::array<Choice, 2>>;
using Alternatives = std::vector<std::array<std::vector<Choice>, 2>>;
using Cut          = std::vector<Id>;

bool trim(std::vector<Cut>& cuts, uint32_t limit, const Choices& choices, const std::vector<uint32_t>& refs, Budget& budget) {
  // Charge bounded ranking work before allocating/sorting. Enumeration and
  // ranking share the same budget as form construction and witness checking.
  if (!budget.spend(uint64_t(cuts.size()) * (std::bit_width(cuts.size()) + 1) * (max_logical_inputs + 1) * 3)) {
    return false;
  }
  std::sort(cuts.begin(), cuts.end(), [](const Cut& a, const Cut& b) {
    return a.size() != b.size() ? a.size() < b.size() : a < b;
  });
  cuts.erase(std::unique(cuts.begin(), cuts.end()), cuts.end());
  if (cuts.size() <= limit) {
    return true;
  }
  struct Score {
    uint32_t depth = 0;
    double   area  = 0;
  };
  std::vector<Score> scores(cuts.size());
  for (size_t i = 0; i < cuts.size(); ++i) {
    for (auto leaf : cuts[i]) {
      uint32_t depth = std::numeric_limits<uint32_t>::max();
      double   area  = std::numeric_limits<double>::infinity();
      for (const auto& rail : choices[leaf]) {
        if (rail.valid) {
          depth = std::min(depth, rail.level);
          area  = std::min(area, rail.area_flow);
        }
      }
      scores[i].depth  = std::max(scores[i].depth, depth);
      scores[i].area  += area / std::max(uint32_t{1}, refs[leaf]);
    }
  }
  // Estimates use the cheaper available rail; actual form rail demand is not
  // known until the cut table is built. These ranks never certify feasibility.
  // Lexicographic cut order breaks every tie independently of hash iteration.
  std::array<std::vector<size_t>, 3> order;
  for (auto& rank : order) {
    rank.resize(cuts.size());
    std::iota(rank.begin(), rank.end(), 0);
  }
  std::sort(order[1].begin(), order[1].end(), [&](size_t a, size_t b) {
    if (scores[a].depth != scores[b].depth) {
      return scores[a].depth < scores[b].depth;
    }
    if (scores[a].area != scores[b].area) {
      return scores[a].area < scores[b].area;
    }
    return a < b;
  });
  std::sort(order[2].begin(), order[2].end(), [&](size_t a, size_t b) {
    if (scores[a].area != scores[b].area) {
      return scores[a].area < scores[b].area;
    }
    if (scores[a].depth != scores[b].depth) {
      return scores[a].depth < scores[b].depth;
    }
    return a < b;
  });
  std::vector<bool>     selected(cuts.size());
  std::array<size_t, 3> cursor{};
  std::vector<Cut>      retained;
  retained.reserve(limit);
  for (size_t turn = 0; retained.size() < limit; ++turn) {
    const auto rank = turn % order.size();
    auto&      pos  = cursor[rank];
    while (selected[order[rank][pos]]) {
      ++pos;
    }
    const auto index = order[rank][pos++];
    selected[index]  = true;
    retained.push_back(std::move(cuts[index]));
  }
  cuts = std::move(retained);
  return true;
}

// Iterate support shrinking to fixed point. Dropping an independent variable
// is an exact identity, not a structural source-count rejection.
void shrink(Cut& leaves, Truth_table& t, Budget& budget) {
  for (uint32_t j = 0; j < t.inputs;) {
    bool independent = true;
    for (uint32_t x = 0; x < (uint32_t{1} << t.inputs); ++x) {
      if (!budget.spend()) {
        return;
      }
      if (t.get(x) != t.get(x ^ (uint32_t{1} << j))) {
        independent = false;
        break;
      }
    }
    if (!independent) {
      ++j;
      continue;
    }
    Truth_table small(t.inputs - 1);
    for (uint32_t x = 0; x < (uint32_t{1} << small.inputs); ++x) {
      const auto low = x & ((uint32_t{1} << j) - 1);
      small.set(x, t.get(low | ((x ^ low) << 1)));
    }
    t = std::move(small);
    leaves.erase(leaves.begin() + j);
  }
}

bool emit_network(const Logic_network& source, const Choices& choices, Unate_network& net, Budget& budget) {
  if (!budget.spend(source.nodes.size())) {
    return false;
  }
  std::vector<std::array<bool, 2>> needed(source.nodes.size());
  std::vector<std::pair<Id, bool>> pending;
  for (auto out : source.outputs) {
    pending.emplace_back(out, false);
  }
  while (!pending.empty()) {
    auto [id, neg] = pending.back();
    pending.pop_back();
    if (!budget.spend()) {
      return false;
    }
    if (needed[id][neg]) {
      continue;
    }
    needed[id][neg] = true;
    if (source.nodes[id].source) {
      needed[id][0] = true;
      continue;
    }
    const auto& c = choices[id][neg];
    if (!c.valid) {
      return false;
    }
    for (uint32_t j = 0; j < c.leaves.size(); ++j) {
      if ((c.form.positive >> j) & 1) {
        pending.emplace_back(c.leaves[j], false);
      }
      if ((c.form.negative >> j) & 1) {
        pending.emplace_back(c.leaves[j], true);
      }
    }
  }
  std::vector<std::array<Id, 2>> ids(source.nodes.size(), {absent, absent});
  for (Id id = 0; id < source.nodes.size(); ++id) {
    for (uint32_t neg = 0; neg < 2; ++neg) {
      if (!needed[id][neg]) {
        continue;
      }
      Unate_node node;
      node.origin   = id;
      node.negative = neg != 0;
      if (source.nodes[id].source) {
        node.kind = neg ? Node_kind::source_inverter : Node_kind::source;
        if (neg) {
          node.ports.push_back(ids[id][0]);
          ++net.source_inverters;
        }
      } else {
        const auto& c       = choices[id][neg];
        node.kind           = Node_kind::function;
        node.logical_inputs = c.leaves;
        node.witness_inputs = c.witness_leaves;
        node.completion     = c.table;
        node.care           = c.care;
        node.image_sources  = c.image_sources;
        node.functional     = c.functional;
        node.level          = 1;
        if (!budget.spend(uint64_t(c.form.literals) + c.leaves.size() + 1)) {
          return false;
        }
        std::map<std::pair<uint32_t, bool>, uint32_t> port_index;
        for (uint32_t j = 0; j < c.leaves.size(); ++j) {
          for (uint32_t sign = 0; sign < 2; ++sign) {
            if (((sign ? c.form.negative : c.form.positive) >> j) & 1) {
              port_index[{j, sign != 0}] = static_cast<uint32_t>(node.ports.size());
              auto driver                = ids[c.leaves[j]][sign];
              if (driver == absent) {
                return false;
              }
              node.ports.push_back(driver);
              node.level = std::max(node.level, net.nodes[driver].level + 1);
            }
          }
        }
        for (const auto& cube : c.form.cubes) {
          std::vector<uint32_t> term;
          for (uint32_t j = 0; j < c.leaves.size(); ++j) {
            if ((cube.care >> j) & 1) {
              term.push_back(port_index.at({j, ((cube.ones >> j) & 1) == 0}));
            }
          }
          node.terms.push_back(std::move(term));
        }
        net.depth        = std::max(net.depth, node.level);
        net.max_support  = std::max(net.max_support, static_cast<uint32_t>(c.leaves.size()));
        net.max_literals = std::max(net.max_literals, c.form.literals);
        net.max_series   = std::max(net.max_series, c.form.series);
      }
      ids[id][neg] = static_cast<Id>(net.nodes.size());
      net.nodes.push_back(std::move(node));
    }
  }
  for (auto out : source.outputs) {
    net.outputs.push_back(ids[out][0]);
  }
  return true;
}

// The objective, lexicographic: unate functions (each demanded rail once, so a
// twin counts), total literal occurrences, total ports. A source rail is a
// free input (A and !A are both available), so a source inverter costs
// nothing here. An exact structural recount, not Liberty area.
std::array<uint64_t, 3> shared_cost(const Unate_network& network) {
  std::array<uint64_t, 3> cost{};
  for (const auto& node : network.nodes) {
    if (node.kind != Node_kind::function) {
      continue;
    }
    ++cost[0];
    cost[2] += node.ports.size();
    for (const auto& term : node.terms) {
      cost[1] += term.size();
    }
  }
  return cost;
}

void recover(const Logic_network& source, Choices& choices, const Alternatives& alternatives, uint32_t rounds, Attempt& result,
             Budget& budget) {
  auto cost = shared_cost(result.network);
  for (uint32_t round = 0; round < rounds; ++round) {
    if (!budget.spend(source.nodes.size())) {
      break;
    }
    std::vector<std::array<bool, 2>> used(source.nodes.size());
    const auto&                      incumbent = result.recovered ? *result.recovered : result.network;
    for (const auto& node : incumbent.nodes) {
      used[node.origin][node.negative] = true;
    }
    bool changed = false;
    // Outputs first. Newly demanded internal producers can be recovered in the
    // next bounded sweep. Ties retain the incumbent deterministically.
    for (size_t pos = source.nodes.size(); pos > 0 && !budget.exhausted; --pos) {
      const auto id = pos - 1;
      for (uint32_t rail = 0; rail < 2 && !budget.exhausted; ++rail) {
        if (!used[id][rail] || source.nodes[id].source) {
          continue;
        }
        for (const auto& replacement : alternatives[id][rail]) {
          if (!budget.spend()) {
            break;
          }
          auto& selected = choices[id][rail];
          if (replacement.leaves == selected.leaves && replacement.table == selected.table
              && replacement.form.cubes == selected.form.cubes) {
            continue;
          }
          ++result.recovery_checks;
          auto saved = std::move(selected);
          selected   = replacement;
          Unate_network trial;
          const bool    emitted = emit_network(source, choices, trial, budget);
          if (!emitted && !budget.exhausted) {
            result.status = Status::invalid;
            result.reason = "recovery dependency closure could not be emitted";
            return;
          }
          if (emitted && trial.depth <= level_limit(result.recipe) && shared_cost(trial) < cost) {
            if (verify(source, trial, result.recipe, budget)) {
              cost             = shared_cost(trial);
              result.recovered = std::move(trial);
              ++result.recovery_improvements;
              changed = true;
              continue;
            }
            if (!budget.exhausted) {
              result.status = Status::invalid;
              result.reason = "shared-area recovery witness mismatch";
              return;
            }
          }
          selected = std::move(saved);
          if (budget.exhausted) {
            break;
          }
        }
      }
    }
    if (!changed) {
      break;
    }
  }
  result.recovery_exhausted = budget.exhausted;
}

bool same_choice(const Choice& a, const Choice& b) {
  return a.leaves == b.leaves && a.table == b.table && a.form.cubes == b.form.cubes;
}

void recover_joint(const Logic_network& source, Choices& choices, const Alternatives& alternatives, uint32_t limit,
                   uint32_t window_limit, Attempt& result, Budget& budget) {
  if (!limit) {
    result.joint_reason = "disabled";
    return;
  }
  result.joint_reason = "work_or_resource_exhausted";
  if (!budget.spend(source.nodes.size())) {
    result.joint_exhausted = true;
    return;
  }
  struct Slot {
    Id                         origin;
    uint32_t                   rail;
    Choice                     incumbent;
    std::vector<const Choice*> other;
  };
  std::vector<Slot>                    slots;
  std::vector<std::array<bool, 2>>     seen(source.nodes.size());
  std::vector<std::pair<Id, uint32_t>> pending;
  for (auto output : source.outputs) {
    pending.emplace_back(output, 0);
  }
  uint64_t combinations = 1;
  while (!pending.empty()) {
    const auto [id, rail] = pending.back();
    pending.pop_back();
    if (!budget.spend()) {
      result.joint_exhausted = true;
      return;
    }
    if (seen[id][rail] || source.nodes[id].source) {
      continue;
    }
    seen[id][rail] = true;
    Slot       slot{id, rail, choices[id][rail], {}};
    // Follow ALL retained alternatives, including producers unused by the
    // incumbent. Otherwise an "exact" sweep could silently freeze new rails.
    const auto demand = [&](const Choice& choice) {
      if (!budget.spend(choice.leaves.size() + 1)) {
        return false;
      }
      for (uint32_t j = 0; j < choice.leaves.size(); ++j) {
        if ((choice.form.positive >> j) & 1) {
          pending.emplace_back(choice.leaves[j], 0);
        }
        if ((choice.form.negative >> j) & 1) {
          pending.emplace_back(choice.leaves[j], 1);
        }
      }
      return true;
    };
    if (!demand(slot.incumbent)) {
      result.joint_exhausted = true;
      return;
    }
    for (const auto& candidate : alternatives[id][rail]) {
      bool duplicate = false;
      for (size_t i = 0; i <= slot.other.size(); ++i) {
        if (!budget.spend(candidate.leaves.size() + candidate.table.words.size() + candidate.form.cubes.size() + 1)) {
          result.joint_exhausted = true;
          return;
        }
        if (same_choice(candidate, i == 0 ? slot.incumbent : *slot.other[i - 1])) {
          duplicate = true;
          break;
        }
      }
      if (!duplicate) {
        slot.other.push_back(&candidate);
        if (!demand(candidate)) {
          result.joint_exhausted = true;
          return;
        }
      }
    }
    if (!slot.other.empty()) {
      const auto radix = slot.other.size() + 1;
      if (combinations > limit / radix) {
        if (!window_limit) {
          result.joint_reason = "candidate_limit";
          return;  // No truncated enumeration or optimality claim.
        }
        combinations = uint64_t(limit) + 1;  // Saturate; enumerate bounded windows instead.
      } else {
        combinations *= radix;
      }
      slots.push_back(std::move(slot));
    }
  }
  std::sort(slots.begin(), slots.end(), [](const Slot& a, const Slot& b) {
    return a.origin != b.origin ? a.origin > b.origin : a.rail < b.rail;
  });
  auto       cost      = shared_cost(result.recovered ? *result.recovered : result.network);
  // Enumerate a complete window while freezing all outside choices at the
  // current verified incumbent. Every trial rebuilds the WHOLE shared closure;
  // a local gate count would miss newly demanded twins and shared producers.
  const auto enumerate = [&](const std::vector<size_t>& window, uint64_t count) {
    std::vector<Choice> saved;
    for (auto index : window) {
      const auto& slot = slots[index];
      if (!budget.spend(slot.incumbent.leaves.size() + slot.incumbent.table.words.size() + slot.incumbent.form.cubes.size() + 1)) {
        return false;
      }
      saved.push_back(choices[slot.origin][slot.rail]);
    }
    result.joint_combinations += count;
    std::optional<uint64_t> best;
    uint64_t                checked = 0;
    for (uint64_t combination = 0; combination < count; ++combination) {
      auto digits = combination;
      for (auto index : window) {
        const auto& slot      = slots[index];
        const auto  digit     = digits % (slot.other.size() + 1);
        digits               /= slot.other.size() + 1;
        const auto& selected  = digit == 0 ? slot.incumbent : *slot.other[digit - 1];
        if (!budget.spend(selected.leaves.size() + selected.table.words.size() + selected.form.cubes.size() + 1)) {
          break;
        }
        choices[slot.origin][slot.rail] = selected;
      }
      Unate_network trial;
      if (!emit_network(source, choices, trial, budget)) {
        if (!budget.exhausted) {
          result.status = Status::invalid;
          result.reason = "joint recovery dependency closure could not be emitted";
        }
        break;
      }
      ++checked;
      ++result.joint_checks;
      if (trial.depth <= level_limit(result.recipe) && shared_cost(trial) < cost) {
        if (!verify(source, trial, result.recipe, budget)) {
          if (!budget.exhausted) {
            result.status = Status::invalid;
            result.reason = "joint recovery witness mismatch";
          }
          break;
        }
        cost                   = shared_cost(trial);
        result.joint_recovered = std::move(trial);
        ++result.joint_improvements;
        best = combination;
      }
    }
    // Incomplete enumeration may retain only an already verified improvement.
    // Restoring choices also keeps later windows consistent with that network.
    auto digits = best.value_or(0);
    for (size_t i = 0; i < window.size(); ++i) {
      const auto& slot = slots[window[i]];
      if (best) {
        const auto digit                 = digits % (slot.other.size() + 1);
        digits                          /= slot.other.size() + 1;
        choices[slot.origin][slot.rail]  = digit == 0 ? slot.incumbent : *slot.other[digit - 1];
      } else {
        choices[slot.origin][slot.rail] = std::move(saved[i]);
      }
    }
    return !budget.exhausted && result.status != Status::invalid && checked == count;
  };

  if (combinations <= limit) {
    std::vector<size_t> all(slots.size());
    std::iota(all.begin(), all.end(), 0);
    result.joint_complete = enumerate(all, combinations);
    result.joint_reason   = result.joint_complete ? "enumerated_candidate_optimum" : "work_or_resource_exhausted";
  } else {
    result.joint_scope = "bounded_windows";
    // Group related decisions by potentially shared rails, including currently
    // unused alternatives. The producer's own identity connects direct readers;
    // source-positive rails are free and do not create sharing opportunities.
    std::vector<std::set<uint64_t>> resources(slots.size());
    for (size_t i = 0; i < slots.size() && !budget.exhausted; ++i) {
      const auto& slot = slots[i];
      resources[i].insert(uint64_t(slot.origin) * 2 + slot.rail);
      for (size_t j = 0; j <= slot.other.size(); ++j) {
        const auto& choice = j ? *slot.other[j - 1] : slot.incumbent;
        if (!budget.spend((choice.leaves.size() + 1) * 2)) {
          break;
        }
        for (uint32_t leaf = 0; leaf < choice.leaves.size(); ++leaf) {
          for (uint32_t rail = 0; rail < 2; ++rail) {
            if (((rail ? choice.form.negative : choice.form.positive) >> leaf) & 1) {
              if (rail || !source.nodes[choice.leaves[leaf]].source) {
                resources[i].insert(uint64_t(choice.leaves[leaf]) * 2 + rail);
              }
            }
          }
        }
      }
    }
    std::set<std::vector<size_t>> visited;
    result.joint_reason = "window_sweep_completed";
    for (size_t seed = 0; seed < slots.size() && !budget.exhausted && result.status != Status::invalid; ++seed) {
      if (result.joint_windows >= window_limit) {
        result.joint_reason = "window_limit";
        break;
      }
      uint64_t count = slots[seed].other.size() + 1;
      if (count > limit) {
        continue;  // Never truncate the alternatives of an admitted variable.
      }
      if (!budget.spend(slots.size() * (std::bit_width(slots.size()) + 1))) {
        break;
      }
      std::vector<std::pair<size_t, size_t>> ranks;
      for (size_t other = 0; other < slots.size(); ++other) {
        if (other == seed) {
          continue;
        }
        if (!budget.spend(resources[seed].size() + resources[other].size() + 1)) {
          break;
        }
        size_t shared = 0;
        auto   a = resources[seed].begin(), b = resources[other].begin();
        while (a != resources[seed].end() && b != resources[other].end()) {
          if (*a == *b) {
            ++shared;
            ++a;
            ++b;
          } else if (*a < *b) {
            ++a;
          } else {
            ++b;
          }
        }
        if (shared) {
          ranks.emplace_back(shared, other);
        }
      }
      if (budget.exhausted) {
        break;
      }
      std::sort(ranks.begin(), ranks.end(), [](const auto& a, const auto& b) {
        return a.first != b.first ? a.first > b.first : a.second < b.second;
      });
      std::vector<size_t> window{seed};
      for (auto [shared, other] : ranks) {
        (void)shared;
        const auto radix = slots[other].other.size() + 1;
        if (count <= limit / radix) {
          count *= radix;
          window.push_back(other);
        }
      }
      std::sort(window.begin(), window.end());
      if (!visited.insert(window).second) {
        continue;
      }
      ++result.joint_windows;
      if (enumerate(window, count)) {
        ++result.joint_windows_complete;
      } else {
        break;
      }
    }
    if (budget.exhausted) {
      result.joint_reason = "work_or_resource_exhausted";
    }
  }
  result.joint_exhausted = budget.exhausted;
  if (result.status == Status::invalid) {
    result.joint_reason = "invalid_witness";
  }
}

std::vector<Cut> divisor_proposals(const std::vector<Id>& pool, const Cut& leaves, uint32_t support, uint32_t limit,
                                   Budget& budget) {
  std::set<Cut>    seen;
  std::vector<Cut> proposals;
  const auto       add = [&](Cut divisors) {
    if (!budget.spend((divisors.size() + 1) * (std::bit_width(divisors.size()) + 1))) {
      return;
    }
    if (proposals.size() >= limit || divisors.size() > support) {
      return;
    }
    std::sort(divisors.begin(), divisors.end());
    if (std::adjacent_find(divisors.begin(), divisors.end()) == divisors.end() && seen.insert(divisors).second) {
      proposals.push_back(std::move(divisors));
    }
  };
  add({});  // A constant is a valid dependency on the empty divisor set.
  for (auto id : pool) {
    add({id});
  }
  for (size_t i = 0; i < pool.size(); ++i) {
    for (size_t j = i + 1; j < pool.size(); ++j) {
      add({pool[i], pool[j]});
    }
  }
  // Larger existing supports can exchange one variable for a paid divisor;
  // exact support shrinking may then discard multiple redundant variables.
  for (size_t j = 0; j < leaves.size(); ++j) {
    for (auto id : pool) {
      auto replaced = leaves;
      replaced[j]   = id;
      add(std::move(replaced));
    }
  }
  return proposals;
}

Choice dependency_choice(const Logic_network& source, const Choices& choices, Id root, bool negative, const Cut& divisors,
                         uint32_t source_limit, uint32_t symbolic_nodes, Attempt& result, Budget& budget, bool cover = false,
                         const std::vector<uint32_t>* refs = nullptr) {
  auto& queries = cover ? result.cover_queries : result.divisor_queries;
  auto& refuted = cover ? result.cover_refuted : result.divisor_refuted;
  auto& limited = cover ? result.cover_limited : result.divisor_limited;
  ++queries;
  auto relation = exact_dependency(source, root, divisors, source_limit, budget, symbolic_nodes);
  (cover ? result.cover_symbolic : result.divisor_symbolic) += relation.symbolic;
  if (relation.status == Dependency_status::refuted) {
    ++refuted;
    return Choice();
  }
  if (relation.status == Dependency_status::unsupported) {
    if (relation.symbolic) {
      ++(cover ? result.cover_symbolic_limited : result.divisor_symbolic_limited);
    } else {
      ++limited;
    }
    return Choice();
  }
  if (relation.status == Dependency_status::exhausted) {
    return Choice();
  }
  if (relation.status != Dependency_status::proven) {
    result.status = Status::invalid;
    result.reason = "invalid paid-divisor dependency query";
    return Choice();
  }
  auto       t       = negative ? relation.table.complement() : relation.table;
  const auto partial = make_form(t, relation.care, result.recipe.literals, result.recipe.series, budget);
  if (partial.status == Status::invalid) {
    result.status = Status::invalid;
    result.reason = "paid-divisor completion failed verification";
    return Choice();
  }
  if (partial.status != Status::feasible) {
    return Choice();
  }
  t = Truth_table(t.inputs);
  for (uint32_t x = 0; x < (uint32_t{1} << t.inputs); ++x) {
    if (!budget.spend(partial.cubes.size() + 1)) {
      return Choice();
    }
    t.set(x, std::any_of(partial.cubes.begin(), partial.cubes.end(), [&](const Cube& c) { return covers(c, x); }));
  }
  auto selected = divisors;
  shrink(selected, t, budget);
  auto form = make_form(t, result.recipe.literals, result.recipe.series, budget);
  if (form.status == Status::invalid) {
    result.status = Status::invalid;
    result.reason = "paid-divisor total form failed verification";
    return Choice();
  }
  if (form.status != Status::feasible) {
    return Choice();
  }
  bool supplied = true;
  for (uint32_t j = 0; j < selected.size(); ++j) {
    supplied &= !((form.positive >> j) & 1) || choices[selected[j]][0].valid;
    supplied &= !((form.negative >> j) & 1) || choices[selected[j]][1].valid;
  }
  if (!supplied) {
    return Choice();
  }
  uint32_t level = 1;
  double   area  = 1;
  for (uint32_t j = 0; j < selected.size(); ++j) {
    for (uint32_t rail = 0; rail < 2; ++rail) {
      if (((rail ? form.negative : form.positive) >> j) & 1) {
        const auto& child  = choices[selected[j]][rail];
        level              = std::max(level, child.level + 1);
        area              += child.area_flow / (refs ? std::max(uint32_t{1}, (*refs)[selected[j]]) : 1);
      }
    }
  }
  return {selected,
          divisors,
          std::move(t),
          std::move(form),
          level,
          area,
          true,
          std::move(relation.care),
          std::move(relation.sources),
          true};
}

void cover_outputs(const Logic_network& source, Choices& choices, Alternatives& alternatives, const std::vector<uint32_t>& refs,
                   uint32_t limit, uint32_t source_limit, uint32_t symbolic_nodes, Attempt& result, Budget& budget) {
  if (!limit || !source_limit) {
    result.cover_reason = "disabled";
    return;
  }
  std::set<Id> missing;
  for (auto root : source.outputs) {
    if (!choices[root][0].valid) {
      missing.insert(root);
    }
  }
  if (missing.empty()) {
    result.cover_reason = "not_needed";
    return;
  }
  for (auto root : missing) {
    if (budget.exhausted || result.status == Status::invalid) {
      break;
    }
    std::vector<Id> pool;
    // A preceding producer with a shallower realizable rail can supply an
    // output at this recipe's depth. Original-source queries, not graph reach,
    // decide whether these producers actually determine that output.
    for (Id id = root; id > 0 && pool.size() < 8;) {
      --id;
      if (!budget.spend()) {
        break;
      }
      if ((choices[id][0].valid && choices[id][0].level < level_limit(result.recipe))
          || (choices[id][1].valid && choices[id][1].level < level_limit(result.recipe))) {
        pool.push_back(id);
      }
    }
    const auto proposals = divisor_proposals(pool, source.nodes[root].inputs, result.recipe.support, limit, budget);
    for (const auto& divisors : proposals) {
      if (budget.exhausted || result.status == Status::invalid) {
        break;
      }
      auto candidate
          = dependency_choice(source, choices, root, false, divisors, source_limit, symbolic_nodes, result, budget, true, &refs);
      if (!candidate.valid || candidate.level > level_limit(result.recipe)) {
        continue;
      }
      auto& best = choices[root][0];
      if (!best.valid || candidate.level < best.level || (candidate.level == best.level && candidate.area_flow < best.area_flow)) {
        best = candidate;
      }
      if (!alternatives.empty()) {
        alternatives[root][0].push_back(std::move(candidate));
      }
    }
    result.cover_roots += choices[root][0].valid;
  }
  result.cover_exhausted = budget.exhausted;
  result.cover_reason    = result.status == Status::invalid       ? "invalid_witness"
                           : budget.exhausted                     ? "work_or_resource_exhausted"
                           : result.cover_roots == missing.size() ? "covered_by_existing_divisors"
                                                                  : "bounded_dependency_search_incomplete";
}

// Re-express demanded functions using already paid producers, even outside
// their structural fan-in. Each proposal is an independently proved functional
// dependency; no simulation signature or truth-table hash authorizes reuse.
void recover_divisors(const Logic_network& source, Choices& choices, uint32_t limit, uint32_t source_limit, uint32_t symbolic_nodes,
                      Attempt& result, Budget& budget) {
  if (!limit || !source_limit) {
    result.divisor_reason = "disabled";
    return;
  }
  result.divisor_reason = "work_or_resource_exhausted";
  if (!budget.spend(source.nodes.size())) {
    result.divisor_exhausted = true;
    return;
  }
  const auto& seed = result.joint_recovered ? *result.joint_recovered : result.recovered ? *result.recovered : result.network;
  auto        cost = shared_cost(seed);
  std::set<Id, std::greater<Id>>                                   paid;
  std::set<std::pair<Id, bool>, std::greater<std::pair<Id, bool>>> roots;
  for (const auto& node : seed.nodes) {
    paid.insert(node.origin);
    if (node.kind == Node_kind::function) {
      roots.emplace(node.origin, node.negative);
    }
  }
  for (const auto& [root, negative] : roots) {
    if (budget.exhausted || result.status == Status::invalid) {
      break;
    }
    std::vector<Id> pool;
    for (auto id : paid) {
      if (!budget.spend()) {
        break;
      }
      if (id < root) {
        pool.push_back(id);
        if (pool.size() == 8) {
          break;
        }
      }
    }
    const auto proposals = divisor_proposals(pool, choices[root][negative].leaves, result.recipe.support, limit, budget);
    for (const auto& divisors : proposals) {
      if (budget.exhausted || result.status == Status::invalid) {
        break;
      }
      auto candidate = dependency_choice(source, choices, root, negative, divisors, source_limit, symbolic_nodes, result, budget);
      if (!candidate.valid) {
        continue;
      }
      auto saved              = choices[root][negative];
      choices[root][negative] = std::move(candidate);
      Unate_network trial;
      const bool    emitted = emit_network(source, choices, trial, budget);
      bool          keep    = false;
      if (emitted) {
        ++result.divisor_checks;
        if (trial.depth <= level_limit(result.recipe) && shared_cost(trial) < cost) {
          if (verify(source, trial, result.recipe, budget)) {
            cost                     = shared_cost(trial);
            result.divisor_recovered = std::move(trial);
            ++result.divisor_improvements;
            keep = true;
          } else if (!budget.exhausted) {
            result.status = Status::invalid;
            result.reason = "paid-divisor dependency witness mismatch";
          }
        }
      } else if (!budget.exhausted) {
        result.status = Status::invalid;
        result.reason = "paid-divisor dependency closure could not be emitted";
      }
      if (!keep) {
        choices[root][negative] = std::move(saved);
      }
    }
  }
  result.divisor_exhausted = budget.exhausted;
  result.divisor_reason    = result.status == Status::invalid ? "invalid_witness"
                             : budget.exhausted               ? "work_or_resource_exhausted"
                             : result.divisor_queries         ? "bounded_paid_divisor_sweep"
                                                              : "no_eligible_divisors";
}

// The node's own fan-in as its cut: always formable for an imported source (a
// 2-input AND, or an XOR only when every recipe admits its 4-literal form), so
// it guarantees every node both rails. Search work does not pay for
// it; it is what an exhausted search falls back to. Fills only unset rails.
bool fanin_choice(const Logic_network& source, Id id, const Recipe& recipe, Choices& choices, const std::vector<uint32_t>& refs,
                  Attempt& result) {
  Budget local{std::numeric_limits<uint64_t>::max() / 4};
  Cut    leaves = source.nodes[id].inputs;
  std::sort(leaves.begin(), leaves.end());
  leaves.erase(std::unique(leaves.begin(), leaves.end()), leaves.end());
  Truth_table table;
  if (leaves.size() > recipe.support || !cut_table(source, id, leaves, table, local)) {
    result.status = Status::invalid;
    result.reason = "a node's own fan-in does not form a cut within the support limit";
    return false;
  }
  for (uint32_t sign = 0; sign < 2; ++sign) {
    if (choices[id][sign].valid) {
      continue;
    }
    auto selected = leaves;
    auto t        = sign ? table.complement() : table;
    shrink(selected, t, local);
    auto form = make_form(t, recipe.literals, recipe.series, local);
    if (form.status != Status::feasible) {
      result.status = Status::invalid;
      result.reason = "a node's own fan-in function exceeds the literal/series limits";
      return false;
    }
    uint32_t level = 1;
    double   area  = 1;
    for (uint32_t j = 0; j < selected.size(); ++j) {
      for (uint32_t rail = 0; rail < 2; ++rail) {
        if (((rail ? form.negative : form.positive) >> j) & 1) {
          const auto& child = choices[selected[j]][rail];
          if (!child.valid) {
            result.status = Status::invalid;
            result.reason = "fan-in rail missing in topological order";
            return false;
          }
          level  = std::max(level, child.level + 1);
          area  += child.area_flow / std::max(uint32_t{1}, refs[selected[j]]);
        }
      }
    }
    if (level > level_limit(recipe)) {
      continue;  // a depth-capped recipe leaves this rail to the residue covers
    }
    choices[id][sign] = Choice{selected, leaves, std::move(t), std::move(form), level, area, true, {}, {}, false};
    ++result.fanin_fallbacks;
  }
  return true;
}

Attempt attempt(const Logic_network& source, const Recipe& recipe, uint32_t cut_limit, uint32_t recovery_rounds,
                uint32_t joint_limit, uint32_t joint_windows, uint32_t image_inputs, uint32_t divisor_limit, uint32_t cover_limit,
                uint32_t symbolic_nodes, Budget& budget) {
  Attempt result;
  result.recipe = recipe;
  // Unbounded depth: every cone must decompose (fan-in cuts back up the search).
  const bool                    complete = recipe.levels == 0;
  Choices                       choices(source.nodes.size());
  Alternatives                  alternatives(recovery_rounds || joint_limit ? source.nodes.size() : 0);
  std::vector<std::vector<Cut>> cuts(source.nodes.size());
  std::vector<uint32_t>         refs(source.nodes.size(), 0);
  for (const auto& node : source.nodes) {
    for (auto in : node.inputs) {
      ++refs[in];
    }
  }
  for (auto out : source.outputs) {
    ++refs[out];
  }
  for (Id id = 0; id < source.nodes.size(); ++id) {
    const auto& node = source.nodes[id];
    if (node.source) {
      choices[id][0].valid = choices[id][1].valid = true;
      cuts[id]                                    = {{id}};
      continue;
    }
    if (budget.exhausted || !budget.spend()) {
      // Out of search work. With unbounded depth every remaining node takes its
      // own fan-in cut, so the cone still decomposes (with more, smaller
      // functions). A depth-capped recipe stops here and leaves the residue to
      // the divisor/encoding/reshaping covers. A time or memory refusal is
      // global and always stops the attempt.
      if (budget.resource_exhausted || !complete) {
        break;
      }
      cuts[id] = {{id}};
      if (!fanin_choice(source, id, recipe, choices, refs, result)) {
        return result;
      }
      continue;
    }
    std::vector<Cut> merged(1);
    for (auto in : node.inputs) {
      std::vector<Cut> next;
      for (const auto& a : merged) {
        for (const auto& b : cuts[in]) {
          if (!budget.spend(a.size() + b.size() + 1)) {
            break;
          }
          Cut u;
          std::set_union(a.begin(), a.end(), b.begin(), b.end(), std::back_inserter(u));
          if (u.size() <= recipe.support) {
            next.push_back(std::move(u));
          }
        }
        if (budget.exhausted) {
          break;
        }
      }
      if (!trim(next, cut_limit, choices, refs, budget)) {
        break;
      }
      merged = std::move(next);
      if (budget.exhausted) {
        break;
      }
    }
    cuts[id] = merged;
    // Keep the self cut for parents even if no form at this node is feasible.
    cuts[id].push_back({id});
    for (auto leaves : merged) {
      const auto  witness_leaves = leaves;
      Truth_table table;
      if (!cut_table(source, id, leaves, table, budget)) {
        if (!budget.exhausted) {
          result.status = Status::invalid;
          result.reason = "enumerated cut does not separate its source cone";
          return result;
        }
        break;
      }
      std::optional<Image_result> image;
      if (image_inputs
          && std::any_of(witness_leaves.begin(), witness_leaves.end(), [&](Id leaf) { return !source.nodes[leaf].source; })) {
        ++result.image_queries;
        auto proof             = exact_image(source, witness_leaves, image_inputs, budget, symbolic_nodes);
        result.image_symbolic += proof.symbolic;
        if (proof.status == Status::feasible) {
          ++result.image_proven;
          if (proof.care != Truth_table(proof.care.inputs, true)) {
            image = std::move(proof);
          }
        } else if (proof.status == Status::unsupported) {
          ++result.image_limited;
        } else if (proof.status == Status::invalid) {
          result.status = Status::invalid;
          result.reason = "invalid image query";
          return result;
        }
      }
      for (uint32_t mode = 0; mode < (image ? 2U : 1U) && !budget.exhausted; ++mode) {
        for (uint32_t sign = 0; sign < 2 && !budget.exhausted; ++sign) {
          auto selected_leaves = witness_leaves;
          auto t               = sign ? table.complement() : table;
          if (mode) {
            const auto partial = make_form(t, image->care, recipe.literals, recipe.series, budget);
            if (partial.status == Status::invalid) {
              result.status = Status::invalid;
              result.reason = "care-guided form failed verification";
              return result;
            }
            if (partial.status != Status::feasible) {
              continue;
            }
            // Freeze this form as a TOTAL completion, including off-image
            // assignments. Later rail demand and tmap see precisely this table.
            t = Truth_table(t.inputs);
            for (uint32_t x = 0; x < (uint32_t{1} << t.inputs); ++x) {
              if (!budget.spend(partial.cubes.size() + 1)) {
                break;
              }
              t.set(x, std::any_of(partial.cubes.begin(), partial.cubes.end(), [&](const Cube& c) { return covers(c, x); }));
            }
          }
          shrink(selected_leaves, t, budget);
          auto form = make_form(t, recipe.literals, recipe.series, budget);
          if (form.status == Status::invalid) {
            result.status = Status::invalid;
            result.reason = "constructed function failed verification";
            return result;
          }
          if (form.status != Status::feasible) {
            continue;
          }
          uint32_t level = 1;
          double   area  = 1;
          bool     valid = true;
          for (uint32_t j = 0; j < selected_leaves.size(); ++j) {
            for (uint32_t rail = 0; rail < 2; ++rail) {
              if (((rail ? form.negative : form.positive) >> j) & 1) {
                const auto& child  = choices[selected_leaves[j]][rail];
                valid              = valid && child.valid;
                level              = std::max(level, child.level + 1);
                area              += child.area_flow / std::max(uint32_t{1}, refs[selected_leaves[j]]);
              }
            }
          }
          auto& best = choices[id][sign];
          if (valid && level <= level_limit(recipe)) {
            Choice candidate{selected_leaves, witness_leaves, std::move(t), std::move(form), level, area, true, {}, {}, false};
            if (mode) {
              candidate.care          = image->care;
              candidate.image_sources = image->sources;
              ++result.image_candidates;
            }
            // The objective is the fewest unate functions: area flow (the
            // shared function count estimate) decides, depth only breaks ties.
            if (!best.valid || area < best.area_flow || (area == best.area_flow && level < best.level)) {
              best = candidate;
            }
            if (recovery_rounds || joint_limit) {
              alternatives[id][sign].push_back(std::move(candidate));
            }
          }
        }
      }
    }
    // Whatever the enumeration left unset (a rail no retained cut could form,
    // or work ran out mid-node) takes the node's own fan-in cut.
    if (complete && !budget.resource_exhausted && (!choices[id][0].valid || !choices[id][1].valid)
        && !fanin_choice(source, id, recipe, choices, refs, result)) {
      return result;
    }
    if (budget.resource_exhausted || (!complete && budget.exhausted)) {
      break;
    }
  }
  if (!budget.exhausted) {
    cover_outputs(source, choices, alternatives, refs, cover_limit, image_inputs, symbolic_nodes, result, budget);
    if (result.status == Status::invalid) {
      return result;
    }
  }
  for (auto out : source.outputs) {
    result.covered_outputs += choices[out][0].valid;
  }
  // Emitting and verifying the selected network is not search: it gets its
  // own work allowance so an exhausted search still yields its network. Only
  // a time/memory refusal (the shared admission) can stop it.
  Budget final_budget{std::numeric_limits<uint64_t>::max() / 4};
  final_budget.admission = budget.admission;
  // A depth-capped recipe keeps the old contract: an exhausted search yields no
  // network, and emission/verification share the search budget.
  auto& emit_budget = complete ? final_budget : budget;
  if ((complete || !budget.exhausted) && !budget.resource_exhausted && result.covered_outputs == source.outputs.size()
      && emit_network(source, choices, result.network, emit_budget)) {
    if (verify(source, result.network, recipe, emit_budget)) {
      result.status = Status::feasible;
      result.reason = "verified decomposition";
      if (recovery_rounds) {
        recover(source, choices, alternatives, recovery_rounds, result, budget);
      }
      if (result.status != Status::invalid) {
        recover_joint(source, choices, alternatives, joint_limit, joint_windows, result, budget);
      }
      if (result.status != Status::invalid) {
        recover_divisors(source, choices, divisor_limit, image_inputs, symbolic_nodes, result, budget);
      }
      return result;
    }
    if (!emit_budget.exhausted) {
      result.status = Status::invalid;
      result.reason = "decomposition witness mismatch";
      return result;
    }
  }
  if (final_budget.resource_exhausted) {
    // A time/memory refusal while emitting is the same global stop as one
    // during search: optimize() must not start the next recipe.
    budget.exhausted = budget.resource_exhausted = true;
  }
  result.network = {};
  result.reason  = budget.resource_exhausted ? "resource budget exhausted"
                   : budget.exhausted        ? "work budget exhausted"
                                             : "bounded cut/divisor/form search found no complete cover";
  return result;
}
}  // namespace

Truth_table::Truth_table(uint32_t n, bool value) : inputs(n) {
  if (n > max_logical_inputs) {
    throw std::invalid_argument("unate truth table exceeds supported 12 inputs");
  }
  const uint32_t bits = uint32_t{1} << n;
  words.assign((bits + 63) / 64, value ? ~uint64_t{0} : 0);
  if (value && bits < 64) {
    words.back() &= (uint64_t{1} << bits) - 1;
  }
}
bool Truth_table::get(uint32_t x) const { return ((words.at(x / 64) >> (x % 64)) & 1) != 0; }
void Truth_table::set(uint32_t x, bool value) {
  auto&      w    = words.at(x / 64);
  const auto mask = uint64_t{1} << (x % 64);
  w               = value ? w | mask : w & ~mask;
}
Truth_table Truth_table::complement() const {
  Truth_table result(inputs, true);
  for (size_t i = 0; i < words.size(); ++i) {
    result.words[i] ^= words[i];
  }
  return result;
}
const char* status_name(Status status) {
  switch (status) {
    case Status::feasible        : return "feasible";
    case Status::search_exhausted: return "search_exhausted";
    case Status::unsupported     : return "unsupported";
    case Status::invalid         : return "invalid";
  }
  return "invalid";
}
bool Budget::spend(uint64_t amount) {
  if (exhausted) {
    return false;
  }
  if (admission && (checkpoint_work == 0 || amount >= checkpoint_work)) {
    if (!admission()) {
      resource_exhausted = exhausted = true;
      return false;
    }
    checkpoint_work = admission_interval;
  }
  checkpoint_work -= std::min(checkpoint_work, amount);
  if (exhausted || amount > remaining) {
    exhausted = true;
    return false;
  }
  remaining -= amount;
  return true;
}

Form make_form(const Truth_table& table, uint32_t max_literals, uint32_t max_series, Budget& budget) {
  if (!valid_table(table)) {
    Form result;
    result.status = Status::invalid;
    return result;
  }
  return make_form(table, Truth_table(table.inputs, true), max_literals, max_series, budget);
}

Form make_form(const Truth_table& table, const Truth_table& care_mask, uint32_t max_literals, uint32_t max_series, Budget& budget) {
  Form result;
  if (!valid_table(table) || !valid_table(care_mask) || table.inputs != care_mask.inputs) {
    result.status = Status::invalid;
    return result;
  }
  // Enumerate primes by merging adjacent cubes. This is exhaustive only when
  // the shared work budget permits completion. No depth-infeasibility claim is
  // made here: selection and the later decomposition are heuristic.
  using Key = std::pair<uint32_t, uint32_t>;
  std::set<Key>  layer, primes;
  const uint32_t count = uint32_t{1} << table.inputs;
  for (uint32_t x = 0; x < count; ++x) {
    if (!budget.spend()) {
      return result;
    }
    if (table.get(x) || !care_mask.get(x)) {
      layer.emplace(count - 1, x);
    }
  }
  while (!layer.empty()) {
    std::set<Key> next;
    for (auto [care, ones] : layer) {
      bool merged = false;
      for (uint32_t j = 0; j < table.inputs; ++j) {
        if (!budget.spend()) {
          return result;
        }
        const auto bit = uint32_t{1} << j;
        if ((care & bit) && layer.contains({care, ones ^ bit})) {
          merged = true;
          next.emplace(care ^ bit, ones & ~bit);
        }
      }
      if (!merged && static_cast<uint32_t>(std::popcount(care)) <= max_series) {
        primes.emplace(care, ones);
      }
    }
    layer = std::move(next);
  }
  std::vector<uint32_t> uncovered;
  for (uint32_t x = 0; x < count; ++x) {
    if (table.get(x) && care_mask.get(x)) {
      uncovered.push_back(x);
    }
  }
  while (!uncovered.empty()) {
    Cube     best;
    uint32_t best_gain = 0;
    for (auto [care, ones] : primes) {
      uint32_t gain = 0;
      for (auto x : uncovered) {
        if (!budget.spend()) {
          return result;
        }
        gain += covers({care, ones}, x);
      }
      if (gain > best_gain || (gain == best_gain && std::popcount(care) < std::popcount(best.care))) {
        best      = {care, ones};
        best_gain = gain;
      }
    }
    if (best_gain == 0) {
      return result;
    }
    result.cubes.push_back(best);
    result.literals += std::popcount(best.care);
    result.series    = std::max(result.series, static_cast<uint32_t>(std::popcount(best.care)));
    result.positive |= best.ones;
    result.negative |= best.care & ~best.ones;
    if (result.literals > max_literals) {
      return result;
    }
    std::erase_if(uncovered, [&](uint32_t x) { return covers(best, x); });
  }
  if (check_form(table, care_mask, result, budget)) {
    result.status = Status::feasible;
  } else if (!budget.exhausted) {
    result.status = Status::invalid;
  }
  return result;
}

namespace {
bool contains(const Cube& big, const Cube& small) {  // small's literals are a subset of big's
  return (big.care & small.care) == small.care && (big.ones & small.care) == small.ones;
}
Cube strip(const Cube& c, const Cube& d) { return {c.care & ~d.care, c.ones & ~d.care}; }
bool same(const Cube& a, const Cube& b) { return a.care == b.care && a.ones == b.ones; }

// The literal (var, polarity) in the most cubes; count 0 when none repeats.
std::tuple<uint32_t, uint32_t, bool> frequent_literal(const std::vector<Cube>& cubes) {
  uint32_t best_count = 0, best_var = 0;
  bool     best_one   = false;
  for (uint32_t j = 0; j < 32; ++j) {
    uint32_t pos = 0, neg = 0;
    for (const auto& c : cubes) {
      if ((c.care >> j) & 1) {
        ((c.ones >> j) & 1 ? pos : neg) += 1;
      }
    }
    if (pos > best_count) {
      best_count = pos, best_var = j, best_one = true;
    }
    if (neg > best_count) {
      best_count = neg, best_var = j, best_one = false;
    }
  }
  return {best_count > 1 ? best_count : 0, best_var, best_one};
}

uint32_t sop_literals(const std::vector<Cube>& cubes) {
  uint32_t sum = 0;
  for (const auto& c : cubes) {
    sum += std::popcount(c.care);
  }
  return sum;
}

// Algebraic division F / D: the cubes q with q*d in F for every d in D.
std::vector<Cube> divide(const std::vector<Cube>& f, const std::vector<Cube>& d) {
  std::vector<Cube> quotient;
  bool              first = true;
  for (const auto& dc : d) {
    std::vector<Cube> part;
    for (const auto& fc : f) {
      if (contains(fc, dc)) {
        part.push_back(strip(fc, dc));
      }
    }
    if (first) {
      quotient = std::move(part);
      first    = false;
    } else {
      std::erase_if(quotient, [&](const Cube& q) {
        return std::none_of(part.begin(), part.end(), [&](const Cube& p) { return same(p, q); });
      });
    }
  }
  return quotient;
}

uint32_t literal_factor(const std::vector<Cube>& cubes);
uint32_t kernel_factor(const std::vector<Cube>& cubes);
uint32_t literal_factor_bound(const std::vector<Cube>& cubes) { return literal_factor(cubes); }

// f = l * Q + R on the most frequent literal.
uint32_t literal_factor(const std::vector<Cube>& cubes) {
  if (cubes.size() <= 1) {
    return cubes.empty() ? 0 : static_cast<uint32_t>(std::popcount(cubes.front().care));
  }
  const auto [count, var, one] = frequent_literal(cubes);
  if (count == 0) {
    return sop_literals(cubes);
  }
  std::vector<Cube> quotient, rest;
  const Cube        lit{uint32_t{1} << var, one ? uint32_t{1} << var : 0};
  for (const auto& c : cubes) {
    (contains(c, lit) ? quotient : rest).push_back(contains(c, lit) ? strip(c, lit) : c);
  }
  return 1 + kernel_factor(quotient) + kernel_factor(rest);
}

// f = Q * K + R with K a kernel of f (divide by frequent literals until no
// literal repeats), recursively -- the better of that and literal_factor.
uint32_t kernel_factor(const std::vector<Cube>& cubes) {
  if (cubes.empty()) {
    return 0;
  }
  if (std::any_of(cubes.begin(), cubes.end(), [](const Cube& c) { return c.care == 0; })) {
    return 0;  // contains the constant-1 term
  }
  if (cubes.size() == 1) {
    return static_cast<uint32_t>(std::popcount(cubes.front().care));
  }
  auto kernel = cubes;
  while (true) {
    const auto [count, var, one] = frequent_literal(kernel);
    if (count == 0) {
      break;
    }
    const Cube        lit{uint32_t{1} << var, one ? uint32_t{1} << var : 0};
    std::vector<Cube> next;
    for (const auto& c : kernel) {
      if (contains(c, lit)) {
        next.push_back(strip(c, lit));
      }
    }
    kernel = std::move(next);
  }
  uint32_t best = literal_factor_bound(cubes);
  if (kernel.size() >= 2 && kernel.size() < cubes.size()) {
    const auto quotient = divide(cubes, kernel);
    if (!quotient.empty()) {
      std::vector<Cube> rest;
      for (const auto& fc : cubes) {
        const bool covered = std::any_of(quotient.begin(), quotient.end(), [&](const Cube& q) {
          return std::any_of(kernel.begin(), kernel.end(), [&](const Cube& k) {
            const Cube prod{q.care | k.care, q.ones | k.ones};
            return same(prod, fc);
          });
        });
        if (!covered) {
          rest.push_back(fc);
        }
      }
      best = std::min(best, kernel_factor(quotient) + kernel_factor(kernel) + kernel_factor(rest));
    }
  }
  return best;
}
}  // namespace

uint32_t factor_literals(const std::vector<Cube>& cubes) { return kernel_factor(cubes); }

namespace {
// Minterm sets of an at most 8-input function.
struct Minterms {
  std::array<uint64_t, 4> w{};
  Minterms operator&(const Minterms& o) const {
    Minterms r;
    for (size_t i = 0; i < 4; ++i) {
      r.w[i] = w[i] & o.w[i];
    }
    return r;
  }
  Minterms operator|(const Minterms& o) const {
    Minterms r;
    for (size_t i = 0; i < 4; ++i) {
      r.w[i] = w[i] | o.w[i];
    }
    return r;
  }
  Minterms operator~() const {
    Minterms r;
    for (size_t i = 0; i < 4; ++i) {
      r.w[i] = ~w[i];
    }
    return r;
  }
  bool     operator==(const Minterms&) const = default;
  bool     any() const { return (w[0] | w[1] | w[2] | w[3]) != 0; }
  uint32_t count() const {
    return static_cast<uint32_t>(std::popcount(w[0]) + std::popcount(w[1]) + std::popcount(w[2]) + std::popcount(w[3]));
  }
  bool get(uint32_t x) const { return ((w[x / 64] >> (x % 64)) & 1) != 0; }
  void set(uint32_t x) { w[x / 64] |= uint64_t{1} << (x % 64); }
};
}  // namespace

Form exact_form(const Truth_table& table, uint32_t max_literals, uint32_t max_series, bool factored, Budget& budget) {
  if (!valid_table(table) || table.inputs > 8) {
    auto form = make_form(table, factored ? 4096 : max_literals, max_series, budget);
    if (form.status == Status::feasible) {
      form.factored = factor_literals(form.cubes);
      if ((factored ? form.factored : form.literals) > max_literals) {
        form.status = Status::search_exhausted;
      }
    }
    return form;
  }
  Form           result;
  const uint32_t n     = table.inputs;
  const uint32_t count = uint32_t{1} << n;
  Minterms       full, on;
  for (uint32_t x = 0; x < count; ++x) {
    full.set(x);
    if (table.get(x)) {
      on.set(x);
    }
  }
  std::array<Minterms, 8> var{};
  for (uint32_t j = 0; j < n; ++j) {
    for (uint32_t x = 0; x < count; ++x) {
      if ((x >> j) & 1) {
        var[j].set(x);
      }
    }
  }
  const auto cube_mask = [&](uint32_t care, uint32_t ones) {
    Minterms m = full;
    for (uint32_t j = 0; j < n; ++j) {
      if ((care >> j) & 1) {
        m = m & (((ones >> j) & 1) ? var[j] : ~var[j]);
      }
    }
    return m & full;
  };
  const auto finish = [&](std::vector<Cube> cubes) {
    result.cubes = std::move(cubes);
    for (const auto& c : result.cubes) {
      result.literals += std::popcount(c.care);
      result.series    = std::max(result.series, static_cast<uint32_t>(std::popcount(c.care)));
      result.positive |= c.ones;
      result.negative |= c.care & ~c.ones;
    }
    result.factored = factor_literals(result.cubes);
    const bool fits = (factored ? result.factored : result.literals) <= max_literals && result.series <= max_series;
    result.status   = fits && check_form(table, result, budget) ? Status::feasible : Status::search_exhausted;
    return result;
  };
  if (!on.any()) {
    return finish({});
  }
  if (on == full) {
    return finish({Cube{0, 0}});
  }
  // Every implicant of at most max_series literals, then the primes among them.
  struct Prime {
    uint32_t care, ones, literals;
    Minterms mask;
  };
  std::vector<Prime> primes;
  const Minterms     off = ~on & full;
  for (uint32_t care = 1; care < count; ++care) {
    const auto lits = static_cast<uint32_t>(std::popcount(care));
    if (lits > max_series || !budget.spend(uint64_t{1} << lits)) {
      if (budget.exhausted) {
        return result;
      }
      continue;
    }
    for (uint32_t ones = care;; ones = (ones - 1) & care) {
      const auto mask = cube_mask(care, ones);
      if (mask.any() && !(mask & off).any()) {
        bool prime = true;
        for (uint32_t j = 0; j < n && prime; ++j) {
          if ((care >> j) & 1) {
            const auto bigger = cube_mask(care & ~(uint32_t{1} << j), ones & ~(uint32_t{1} << j));
            prime             = (bigger & off).any();
          }
        }
        if (prime) {
          primes.push_back({care, ones, lits, mask});
        }
      }
      if (ones == 0) {
        break;
      }
    }
  }
  Minterms reachable;
  for (const auto& p : primes) {
    reachable = reachable | p.mask;
  }
  if (!((reachable & on) == on)) {
    return result;  // some minterm needs a product longer than max_series
  }
  std::sort(primes.begin(), primes.end(), [](const Prime& a, const Prime& b) {
    return a.literals != b.literals ? a.literals < b.literals : a.mask.count() > b.mask.count();
  });
  // Branch and bound on the uncovered minterm with the fewest covering primes.
  std::vector<std::vector<uint32_t>> covering(count);
  for (uint32_t i = 0; i < primes.size(); ++i) {
    for (uint32_t x = 0; x < count; ++x) {
      if (primes[i].mask.get(x)) {
        covering[x].push_back(i);
      }
    }
  }
  std::vector<uint32_t> chosen, best;
  uint64_t              best_cost = std::numeric_limits<uint64_t>::max();
  uint64_t              nodes     = 0;
  const auto            score     = [](uint64_t literals, uint64_t cubes) { return (literals << 16) | cubes; };
  {
    // Greedy seed (most new minterms, then fewest literals): an upper bound,
    // and the answer if the node limit stops the exact search.
    Minterms covered;
    uint64_t literals = 0;
    while (!((covered & on) == on)) {
      uint32_t pick = 0, gain = 0;
      for (uint32_t i = 0; i < primes.size(); ++i) {
        const auto g = (primes[i].mask & on & ~covered).count();
        if (g > gain) {
          pick = i, gain = g;
        }
      }
      best.push_back(pick);
      covered   = covered | primes[pick].mask;
      literals += primes[pick].literals;
    }
    best_cost = score(literals, best.size());
  }
  std::function<void(const Minterms&, uint64_t)> search = [&](const Minterms& covered, uint64_t literals) {
    if (++nodes > 20000 || !budget.spend()) {
      return;
    }
    if ((covered & on) == on) {
      if (score(literals, chosen.size()) < best_cost) {
        best_cost = score(literals, chosen.size());
        best      = chosen;
      }
      return;
    }
    const auto open = on & ~covered;
    uint32_t   pick = count;
    for (uint32_t x = 0; x < count; ++x) {
      if (open.get(x) && (pick == count || covering[x].size() < covering[pick].size())) {
        pick = x;
      }
    }
    for (auto i : covering[pick]) {
      if (score(literals + primes[i].literals, chosen.size() + 1) >= best_cost) {
        continue;
      }
      chosen.push_back(i);
      search(covered | primes[i].mask, literals + primes[i].literals);
      chosen.pop_back();
    }
  };
  search(Minterms{}, 0);
  if (best.empty()) {
    return result;
  }
  std::vector<Cube> cubes;
  for (auto i : best) {
    cubes.push_back({primes[i].care, primes[i].ones});
  }
  return finish(std::move(cubes));
}

bool check_form(const Truth_table& table, const Form& form, Budget& budget) {
  return valid_table(table) && check_form(table, Truth_table(table.inputs, true), form, budget);
}

bool check_form(const Truth_table& table, const Truth_table& care_mask, const Form& form, Budget& budget) {
  if (!valid_table(table) || !valid_table(care_mask) || table.inputs != care_mask.inputs) {
    return false;
  }
  const auto count    = uint32_t{1} << table.inputs;
  uint32_t   literals = 0, series = 0, positive = 0, negative = 0;
  for (const auto& c : form.cubes) {
    if ((c.ones & ~c.care) || c.care >= count) {
      return false;
    }
    literals += std::popcount(c.care);
    series    = std::max(series, static_cast<uint32_t>(std::popcount(c.care)));
    positive |= c.ones;
    negative |= c.care & ~c.ones;
  }
  if (literals != form.literals || series != form.series || positive != form.positive || negative != form.negative) {
    return false;
  }
  for (uint32_t x = 0; x < count; ++x) {
    bool y = false;
    for (const auto& c : form.cubes) {
      if (!budget.spend()) {
        return false;
      }
      y |= covers(c, x);
    }
    if (care_mask.get(x) && y != table.get(x)) {
      return false;
    }
  }
  return true;
}

Id Logic_network::add_source() {
  nodes.push_back({true, {}, Truth_table{}});
  return static_cast<Id>(nodes.size() - 1);
}
Id Logic_network::add_function(std::vector<Id> inputs, Truth_table table) {
  if (inputs.size() != table.inputs || !valid_table(table)
      || std::any_of(inputs.begin(), inputs.end(), [&](Id id) { return id >= nodes.size(); })) {
    throw std::invalid_argument("invalid unate source function");
  }
  nodes.push_back({false, std::move(inputs), std::move(table)});
  return static_cast<Id>(nodes.size() - 1);
}
bool Logic_network::valid() const {
  for (Id id = 0; id < nodes.size(); ++id) {
    const auto& n = nodes[id];
    if (n.source) {
      if (!n.inputs.empty()) {
        return false;
      }
    } else if (!valid_table(n.table) || n.inputs.size() != n.table.inputs
               || std::any_of(n.inputs.begin(), n.inputs.end(), [&](Id in) { return in >= id; })) {
      return false;
    }
  }
  return std::all_of(outputs.begin(), outputs.end(), [&](Id id) { return id < nodes.size(); });
}

namespace {
// A gate of a candidate's local network. A leaf is a source-network node or,
// encoded as local_ref(j), the output of local gate j (an earlier one).
struct Local_gate {
  std::vector<Id> leaves;
  Form            form;
  bool            complemented = false;
  Truth_table     table;  // canonical (value 0 at the all-zero input)
};
constexpr Id local_ref(uint32_t j) { return std::numeric_limits<Id>::max() - 1 - j; }
constexpr bool is_local(Id id) { return id >= std::numeric_limits<Id>::max() - 16 && id != std::numeric_limits<Id>::max(); }
constexpr uint32_t local_index(Id id) { return std::numeric_limits<Id>::max() - 1 - id; }

struct Split_candidate {
  std::vector<Id>         cut;     // the source-network leaves the candidate reads
  std::vector<Id>         leaves;  // the root form's leaf order (may hold local refs)
  Form                    form;
  bool                    complemented = false;
  uint32_t                cost = 0, volume = 0;
  bool                    valid = false;
  // A decomposition: G(H(B), rest) with locals = {H}, or, when G is not one
  // gate, G2(G1(B'), rest') over (rest, h) with locals = {H, G1}.
  std::vector<Local_gate> locals;
  bool                    shared = false;  // locals[0] matches an H an earlier node proposed
};
}  // namespace

Split_result split_cones(const Logic_network& source, const Recipe& recipe, const Search_options& options) {
  Split_result result;
  if (!source.valid()) {
    result.status = Status::invalid;
    result.reason = "invalid source network";
    return result;
  }
  if (source.nodes.size() > options.max_nodes || options.cuts_per_node == 0 || options.cuts_per_node > 256 || recipe.support < 2
      || recipe.support > max_logical_inputs || recipe.literals < 2 || recipe.series < 2) {
    result.status = Status::unsupported;
    result.reason = "unsupported split options";
    return result;
  }
  // A cone count is capped here: 4 means "more than three gates".
  constexpr uint32_t more = 4;
  const auto         n    = source.nodes.size();
  // Wide cuts are only decomposed as G(H(B), rest): |rest| + 1 <= support
  // bounds them, and each column pattern must fit one machine word.
  const uint32_t wide = options.split_cut > recipe.support && recipe.support <= 7
                            ? std::min<uint32_t>({options.split_cut, 2 * recipe.support - 1, max_logical_inputs})
                            : recipe.support;
  Budget         budget{options.work};
  budget.admission = options.admission;
  std::vector<std::vector<Cut>> cuts(n);
  std::vector<uint8_t>          constant(n, 0);  // no leaves: folds into its consumers' cuts
  std::vector<uint32_t>         cost(n, 0);      // fewest gates (tree count, capped) building the node
  std::vector<Split_candidate>  by_cost(n), by_volume(n), by_volume1(n);
  const auto                    internal = [&](Id u) { return !source.nodes[u].source && !constant[u]; };
  const auto                    below    = [&](const Cut& c) {
    uint32_t sum = 0;
    for (auto u : c) {
      sum = internal(u) ? std::min(more, sum + cost[u]) : sum;
    }
    return sum;
  };
  const auto rank = [](const Split_candidate& c, bool volume_first) {
    const int64_t v = -static_cast<int64_t>(c.volume);
    const int64_t s = c.shared ? 0 : 1;
    int64_t       l = c.form.literals;
    for (const auto& g : c.locals) {
      l += g.form.literals;
    }
    return volume_first ? std::tuple(v, static_cast<int64_t>(c.cost), s, l, c.complemented)
                        : std::tuple(static_cast<int64_t>(c.cost), s, v, l, c.complemented);
  };
  const auto gate_form = [&](const Truth_table& t, Budget& b) {
    if (options.split_exact) {
      return exact_form(t, recipe.literals, recipe.series, options.split_factor, b);
    }
    auto form = make_form(t, options.split_factor ? 4096 : recipe.literals, recipe.series, b);
    if (form.status == Status::feasible) {
      form.factored = factor_literals(form.cubes);
      if ((options.split_factor ? form.factored : form.literals) > recipe.literals) {
        form.status = Status::search_exhausted;
      }
    }
    return form;
  };
  // The cheaper feasible rail of `t` (a gate may drive either polarity, since
  // every consumer takes its inputs in either polarity).
  const auto best_rail = [&](const Truth_table& t, Budget& b, bool& complemented) -> std::optional<Form> {
    std::optional<Form> best;
    for (int rail = 0; rail < 2; ++rail) {
      auto form = gate_form(rail ? t.complement() : t, b);
      if (form.status == Status::invalid) {
        return std::nullopt;
      }
      if (form.status == Status::feasible && (!best || form.literals < best->literals)) {
        best         = std::move(form);
        complemented = rail == 1;
      }
    }
    return best;
  };
  const auto offer = [&](Id id, Split_candidate c) {
    if (!by_cost[id].valid || rank(c, false) < rank(by_cost[id], false)) {
      by_cost[id] = c;
    }
    if (c.locals.empty() && (!by_volume1[id].valid || rank(c, true) < rank(by_volume1[id], true))) {
      by_volume1[id] = c;
    }
    if (!by_volume[id].valid || rank(c, true) < rank(by_volume[id], true)) {
      by_volume[id] = std::move(c);
    }
  };
  // Bound-set functions proposed so far, for sharing: (bound, h table words).
  std::set<std::pair<std::vector<Id>, std::vector<uint64_t>>> proposed;
  bool                                                        invalid = false;
  const auto consider = [&](Id id, Cut leaves, Truth_table table, uint32_t volume, Budget& b) {
    shrink(leaves, table, b);
    if (leaves.size() > recipe.support) {
      return;
    }
    for (int rail = 0; rail < 2; ++rail) {
      auto form = gate_form(rail ? table.complement() : table, b);
      if (form.status == Status::invalid) {
        invalid = true;
        return;
      }
      if (form.status != Status::feasible) {
        continue;
      }
      Split_candidate c;
      c.cut          = leaves;  // what the function depends on
      c.leaves       = leaves;
      c.form         = std::move(form);
      c.complemented = rail == 1;
      c.cost         = std::min(more, (leaves.empty() ? 0U : 1U) + below(leaves));
      c.volume       = volume;
      c.valid        = true;
      offer(id, std::move(c));
    }
  };
  // One-bit bound-set split of a table: t(x) = g(h(x|B), x|R) exists iff the
  // decomposition chart over bound positions B has at most two distinct
  // columns. h is the column class (canonical: h = 0 on B's all-zero input);
  // g reads R in order, then h as its last input.
  struct Split_tables {
    Truth_table           h, g;
    std::vector<uint32_t> bpos, rpos;
  };
  const auto bound_split = [](const Truth_table& t, uint32_t bmask) -> std::optional<Split_tables> {
    const uint32_t k = t.inputs;
    Split_tables   out;
    for (uint32_t j = 0; j < k; ++j) {
      ((bmask >> j) & 1 ? out.bpos : out.rpos).push_back(j);
    }
    const auto nb = static_cast<uint32_t>(out.bpos.size()), nr = static_cast<uint32_t>(out.rpos.size());
    if (nr > 6) {
      return std::nullopt;
    }
    const auto scatter = [](uint32_t value, const std::vector<uint32_t>& pos) {
      uint32_t x = 0;
      for (uint32_t i = 0; i < pos.size(); ++i) {
        x |= ((value >> i) & 1) << pos[i];
      }
      return x;
    };
    std::vector<uint32_t> xr(uint32_t{1} << nr);
    for (uint32_t r = 0; r < xr.size(); ++r) {
      xr[r] = scatter(r, out.rpos);
    }
    uint64_t              p0 = 0, p1 = 0;
    bool                  has1 = false;
    std::vector<uint64_t> column(uint32_t{1} << nb);
    for (uint32_t beta = 0; beta < column.size(); ++beta) {
      const auto xb = scatter(beta, out.bpos);
      uint64_t   p  = 0;
      for (uint32_t r = 0; r < xr.size(); ++r) {
        p |= uint64_t{t.get(xb | xr[r])} << r;
      }
      column[beta] = p;
      if (beta == 0) {
        p0 = p;
      } else if (p != p0) {
        if (has1 && p != p1) {
          return std::nullopt;
        }
        p1 = p, has1 = true;
      }
    }
    if (!has1) {
      return std::nullopt;  // t does not depend on B
    }
    out.h = Truth_table(nb);
    for (uint32_t beta = 0; beta < column.size(); ++beta) {
      out.h.set(beta, column[beta] != p0);
    }
    out.g = Truth_table(nr + 1);
    for (uint32_t r = 0; r < xr.size(); ++r) {
      out.g.set(r, (p0 >> r) & 1);
      out.g.set(r | (uint32_t{1} << nr), (p1 >> r) & 1);
    }
    return out;
  };
  // The used leaves of a form over `leaves` (a column-constant input is not one).
  const auto used = [](const Form& f, const std::vector<Id>& leaves, std::vector<Id>& into) {
    for (uint32_t j = 0; j < leaves.size(); ++j) {
      if (((f.positive | f.negative) >> j) & 1 && !is_local(leaves[j])) {
        into.push_back(leaves[j]);
      }
    }
  };
  // F(cut) = G(H(B), cut \ B); when G is not one gate, G itself splits as
  // G2(G1(B'), rest') -- "the second part is two unate functions". H comes
  // first after the leaves, over the bound set B, so cones that share B can
  // share it.
  const auto decompose = [&](Id id, const Cut& cut, const Truth_table& table, uint32_t volume, Budget& b) {
    const auto k = static_cast<uint32_t>(cut.size());
    if (k < 3 || k > wide) {
      return;
    }
    Split_candidate best;
    const auto      key = [](const Split_candidate& x) {
      int64_t lits = x.form.literals;
      for (const auto& g : x.locals) {
        lits += g.form.literals;
      }
      return std::tuple(x.locals.size(), x.shared ? 0 : 1, -static_cast<int64_t>(x.locals.front().leaves.size()), lits);
    };
    for (uint32_t bmask = 1; bmask < (uint32_t{1} << k); ++bmask) {
      const auto nb = static_cast<uint32_t>(std::popcount(bmask));
      if (nb < 2 || nb > recipe.support || k - nb + 1 > recipe.support + 1 || !b.spend(uint64_t{1} << k)) {
        if (b.exhausted) {
          break;
        }
        continue;
      }
      auto split = bound_split(table, bmask);
      if (!split) {
        continue;
      }
      bool h_comp = false;
      auto h_form = best_rail(split->h, b, h_comp);
      if (!h_form) {
        continue;
      }
      Local_gate h;
      for (auto j : split->bpos) {
        h.leaves.push_back(cut[j]);
      }
      h.form         = std::move(*h_form);
      h.complemented = h_comp;
      h.table        = split->h;
      std::vector<Id> g_leaves;
      for (auto j : split->rpos) {
        g_leaves.push_back(cut[j]);
      }
      g_leaves.push_back(local_ref(0));
      Split_candidate c;
      c.volume = volume;
      c.valid  = true;
      c.shared = options.split_share && proposed.contains({h.leaves, h.table.words});
      bool g_comp = false;
      if (g_leaves.size() <= recipe.support) {
        if (auto g_form = best_rail(split->g, b, g_comp)) {
          c.leaves       = g_leaves;
          c.form         = std::move(*g_form);
          c.complemented = g_comp;
          c.locals       = {h};
        }
      }
      if (c.locals.empty() && g_leaves.size() <= recipe.support + 1) {
        // G2(G1(B'), rest') over G's inputs (rest, then h).
        const auto gk = split->g.inputs;
        for (uint32_t gmask = 1; gmask < (uint32_t{1} << gk) && c.locals.empty(); ++gmask) {
          const auto gb = static_cast<uint32_t>(std::popcount(gmask));
          if (gb < 2 || gb > recipe.support || gk - gb + 1 > recipe.support || !b.spend(uint64_t{1} << gk)) {
            if (b.exhausted) {
              break;
            }
            continue;
          }
          auto second = bound_split(split->g, gmask);
          if (!second) {
            continue;
          }
          bool g1_comp = false, g2_comp = false;
          auto g1_form = best_rail(second->h, b, g1_comp);
          if (!g1_form) {
            continue;
          }
          auto g2_form = best_rail(second->g, b, g2_comp);
          if (!g2_form) {
            continue;
          }
          Local_gate g1;
          for (auto j : second->bpos) {
            g1.leaves.push_back(g_leaves[j]);
          }
          g1.form         = std::move(*g1_form);
          g1.complemented = g1_comp;
          g1.table        = second->h;
          c.leaves.clear();
          for (auto j : second->rpos) {
            c.leaves.push_back(g_leaves[j]);
          }
          c.leaves.push_back(local_ref(1));
          c.form         = std::move(*g2_form);
          c.complemented = g2_comp;
          c.locals       = {h, std::move(g1)};
        }
      }
      if (c.locals.empty()) {
        continue;
      }
      used(c.form, c.leaves, c.cut);
      for (const auto& g : c.locals) {
        used(g.form, g.leaves, c.cut);
      }
      std::sort(c.cut.begin(), c.cut.end());
      c.cut.erase(std::unique(c.cut.begin(), c.cut.end()), c.cut.end());
      c.cost = std::min<uint32_t>(more, static_cast<uint32_t>(1 + c.locals.size()) + below(c.cut));
      if (!best.valid || key(c) < key(best)) {
        best = std::move(c);
      }
    }
    if (best.valid) {
      offer(id, std::move(best));
    }
  };
  for (Id id = 0; id < n; ++id) {
    const auto& node = source.nodes[id];
    if (node.source) {
      cuts[id] = {{id}};
      continue;
    }
    std::vector<Cut> merged(1);
    bool             searched = !budget.exhausted && budget.spend();
    if (budget.resource_exhausted) {
      result.reason = "resource budget exhausted";
      return result;
    }
    for (auto in : node.inputs) {
      if (!searched) {
        break;
      }
      std::vector<Cut> next;
      for (const auto& a : merged) {
        for (const auto& b : cuts[in]) {
          if (!budget.spend(a.size() + b.size() + 1)) {
            break;
          }
          Cut u;
          std::set_union(a.begin(), a.end(), b.begin(), b.end(), std::back_inserter(u));
          if (u.size() <= wide) {
            next.push_back(std::move(u));
          }
        }
      }
      std::sort(next.begin(), next.end());
      next.erase(std::unique(next.begin(), next.end()), next.end());
      // Keep gate-sized cuts (fewest gates below, then smallest) and, apart,
      // the widest cheap cuts a decomposition may cover.
      std::vector<Cut> small, large;
      for (auto& c : next) {
        (c.size() <= recipe.support ? small : large).push_back(std::move(c));
      }
      std::stable_sort(small.begin(), small.end(), [&](const Cut& a, const Cut& b) {
        const auto ca = below(a), cb = below(b);
        return ca != cb ? ca < cb : a.size() < b.size();
      });
      std::stable_sort(large.begin(), large.end(), [&](const Cut& a, const Cut& b) {
        const auto ca = below(a), cb = below(b);
        return ca != cb ? ca < cb : a.size() > b.size();
      });
      small.resize(std::min<size_t>(small.size(), options.cuts_per_node));
      large.resize(std::min<size_t>(large.size(), options.cuts_per_node / 2));
      next = std::move(small);
      next.insert(next.end(), std::make_move_iterator(large.begin()), std::make_move_iterator(large.end()));
      merged   = std::move(next);
      searched = !budget.exhausted;
    }
    Cut fanin;
    for (auto in : node.inputs) {
      if (!constant[in]) {
        fanin.push_back(in);
      }
    }
    std::sort(fanin.begin(), fanin.end());
    fanin.erase(std::unique(fanin.begin(), fanin.end()), fanin.end());
    if (!searched) {
      // Out of work: the node's own fan-in, on an unbounded local budget.
      merged = {fanin};
      ++result.fanin_fallbacks;
      if (budget.resource_exhausted) {
        result.reason = "resource budget exhausted";
        return result;
      }
    } else if (!node.inputs.empty() && std::find(merged.begin(), merged.end(), fanin) == merged.end()) {
      merged.push_back(fanin);  // the node's own fan-in always stays a candidate
    }
    Budget local{std::numeric_limits<uint64_t>::max() / 4};
    auto&  b = searched ? budget : local;
    std::vector<const Cut*> wide_cuts;  // decomposition candidates, tabled on demand
    for (const auto& leaves : merged) {
      if (leaves.size() > recipe.support) {
        wide_cuts.push_back(&leaves);
        continue;
      }
      Truth_table table;
      uint32_t    volume = 0;
      if (!cut_table(source, id, leaves, table, b, &volume)) {
        if (b.exhausted) {
          break;
        }
        result.status = Status::invalid;
        result.reason = "non-separating split cut";
        return result;
      }
      consider(id, leaves, table, volume, b);
      if (invalid) {
        result.status = Status::invalid;
        result.reason = "invalid split form";
        return result;
      }
    }
    // Two gates over a wide cut can only help where one gate over a narrow
    // cut costs 3 or more: try the cheapest wide cuts, cheapest first.
    const uint32_t current = by_cost[id].valid ? by_cost[id].cost : more;
    if (searched && current >= 3 && wide > recipe.support) {
      uint32_t tried = 0;
      for (const auto* leaves : wide_cuts) {
        if (below(*leaves) + 2 >= current || tried >= 4) {
          continue;
        }
        ++tried;
        Truth_table table;
        uint32_t    volume = 0;
        if (!cut_table(source, id, *leaves, table, b, &volume)) {
          if (b.exhausted) {
            break;
          }
          result.status = Status::invalid;
          result.reason = "non-separating split cut";
          return result;
        }
        decompose(id, *leaves, table, volume, b);
      }
    }
    if (budget.resource_exhausted) {
      result.reason = "resource budget exhausted";
      return result;
    }
    if (node.inputs.empty()) {
      // A 0-input constant folds into every consumer's cut. A node that is
      // only LOGICALLY constant keeps its cuts: its cone still reaches sources.
      constant[id] = 1;
      cuts[id]     = {Cut{}};
      continue;
    }
    cost[id] = by_cost[id].valid ? by_cost[id].cost : more;
    if (by_cost[id].valid && !by_cost[id].locals.empty()) {
      proposed.insert({by_cost[id].locals.front().leaves, by_cost[id].locals.front().table.words});
    }
    cuts[id] = std::move(merged);
    cuts[id].push_back({id});
  }

  // Keep every gate of a cone of at most three; then, for a larger cone, its
  // root and one gate under it (a decomposed root is already two gates), all
  // the widest available.
  std::vector<int8_t> kept(n, -1);  // -1 none, 0 by_cost, 1 by_volume, 2 by_volume1
  const auto          chosen = [&](Id v) -> const Split_candidate& {
    return kept[v] == 1 ? by_volume[v] : kept[v] == 2 ? by_volume1[v] : by_cost[v];
  };
  std::vector<Id> work;
  for (auto out : source.outputs) {
    if (constant[out]) {
      kept[out] = 0;  // a tied output still needs its constant cell
      continue;
    }
    if (source.nodes[out].source || cost[out] >= more || !by_cost[out].valid) {
      continue;
    }
    work.assign(1, out);
    while (!work.empty()) {
      const auto v = work.back();
      work.pop_back();
      if (!internal(v) || kept[v] >= 0) {
        continue;
      }
      kept[v] = 0;
      for (auto u : by_cost[v].cut) {
        work.push_back(u);
      }
    }
  }
  std::vector<uint8_t> remainder(n, 0);
  for (auto out : source.outputs) {
    if (source.nodes[out].source || constant[out] || cost[out] < more) {
      continue;
    }
    if (kept[out] < 0) {
      if (!by_volume[out].valid) {
        remainder[out] = 1;  // no gate at all builds this root
        continue;
      }
      kept[out] = 1;
    }
    if (!chosen(out).locals.empty()) {
      continue;  // already two or three gates
    }
    Id       second = absent;
    uint32_t widest = 0;
    for (auto u : chosen(out).cut) {
      if (internal(u) && kept[u] < 0 && by_volume1[u].valid && (second == absent || by_volume1[u].volume > widest)) {
        second = u;
        widest = by_volume1[u].volume;
      }
    }
    if (second != absent) {
      kept[second] = 2;
    }
  }
  std::map<std::pair<std::vector<Id>, std::vector<uint64_t>>, Id> synthetic;
  for (Id v = 0; v < n; ++v) {
    if (kept[v] < 0) {
      continue;
    }
    const auto& c = chosen(v);
    for (auto u : c.cut) {
      if (internal(u) && kept[u] < 0) {
        remainder[u] = 1;
      }
    }
    // Local gates before their first consumer (their leaves are cut leaves of
    // v or earlier locals); an identical local gate is shared.
    std::vector<Id> local_ids;
    const auto      resolve = [&](std::vector<Id> leaves) {
      for (auto& l : leaves) {
        l = is_local(l) ? local_ids.at(local_index(l)) : l;
      }
      return leaves;
    };
    for (const auto& g : c.locals) {
      auto leaves      = resolve(g.leaves);
      auto [it, fresh] = synthetic.try_emplace({leaves, g.table.words}, static_cast<Id>(n + synthetic.size()));
      if (fresh) {
        result.gates.push_back({it->second, leaves, g.form, g.complemented, 0});
        ++result.bound_gates;
      } else {
        ++result.shared_bound_gates;
      }
      local_ids.push_back(it->second);
    }
    result.gates.push_back({v, resolve(c.leaves), c.form, c.complemented, c.volume});
  }
  result.synthetic = static_cast<uint32_t>(synthetic.size());
  // The remainder is computed from the sources alone: every node under a
  // remainder output, gates included, is its logic.
  std::vector<uint8_t> seen(n, 0);
  for (Id v = 0; v < n; ++v) {
    if (!remainder[v]) {
      continue;
    }
    result.remainder_outputs.push_back(v);
    work.assign(1, v);
    while (!work.empty()) {
      const auto u = work.back();
      work.pop_back();
      if (seen[u] || source.nodes[u].source) {
        continue;
      }
      seen[u] = 1;
      ++result.remainder_nodes;
      for (auto in : source.nodes[u].inputs) {
        work.push_back(in);
      }
    }
  }
  for (auto out : source.outputs) {
    const uint8_t gates = source.nodes[out].source || constant[out] ? 0 : static_cast<uint8_t>(std::min(cost[out], more));
    result.cone_gates.push_back(gates);
    ++(gates == 0 ? result.cones_wire : gates <= 2 ? result.cones_2 : gates == 3 ? result.cones_3 : result.cones_more);
  }
  result.work   = options.work - budget.remaining;
  result.status = Status::feasible;
  return result;
}

Search_result optimize(const Logic_network& source, const Search_options& options) {
  Search_result result;
  if (!source.valid()) {
    result.status = Status::invalid;
    return result;
  }
  if (source.nodes.size() > options.max_nodes || options.cuts_per_node == 0 || options.cuts_per_node > 256
      || options.recipes.empty() || options.recipes.size() > 32 || options.recovery_rounds > 8 || options.joint_limit > 65536
      || options.joint_windows > 256 || options.image_inputs > max_logical_inputs || options.divisor_limit > 4096
      || options.cover_limit > 4096 || options.symbolic_nodes > max_symbolic_nodes || options.encoding_limit > 4096
      || options.encoding_pair_limit > 4096 || options.encoding_code_limit == 0 || options.encoding_code_limit > 4096
      || options.reshape_limit > 4096) {
    result.status = Status::unsupported;
    return result;
  }
  // The deterministic work budget is SLICED per recipe rather than shared as one
  // pool. With one pool the first expensive recipe drains it and the `break`
  // below skips recipes 2..N entirely -- so the four-recipe portfolio silently
  // degenerated to one on exactly the regions where the later, cheaper recipes
  // are most likely to fit. Unspent slice carries forward, so the TOTAL work
  // (and therefore the runtime) is unchanged; only its distribution is.
  const uint64_t slice = options.work / options.recipes.size();
  uint64_t       carry = options.work - slice * options.recipes.size();
  Budget         budget{0};
  budget.admission = options.admission;
  for (const auto& recipe : options.recipes) {
    const auto recipe_index = result.attempts.size();
    budget.remaining += slice + carry;
    carry              = 0;
    budget.exhausted   = false;
    if (options.observe_recipe) {
      options.observe_recipe(recipe_index, true);
    }
    const auto before = budget.remaining;
    Attempt    current;
    if (recipe.support > max_logical_inputs || recipe.support < 2 || recipe.literals < 2 || recipe.series < 2) {
      current.recipe = recipe;
      current.status = Status::unsupported;
      current.reason = "supported limits: 2..12 inputs, at least 2 literals and series 2 (levels 0 = unbounded)";
    } else {
      current = attempt(source,
                        recipe,
                        options.cuts_per_node,
                        options.recovery_rounds,
                        options.joint_limit,
                        options.joint_windows,
                        options.image_inputs,
                        options.divisor_limit,
                        options.cover_limit,
                        options.symbolic_nodes,
                        budget);
    }
    if (current.status == Status::search_exhausted && !budget.exhausted) {
      encode_residue(source, options.encoding_limit, current, budget, options.encoding_pair_limit, options.encoding_code_limit);
    }
    if (current.status == Status::search_exhausted && !budget.exhausted) {
      reshape_residue(source, options.reshape_limit, options.symbolic_nodes, current, budget);
    }
    current.work = before - budget.remaining;
    if (current.status == Status::feasible) {
      result.status = Status::feasible;
    }
    result.attempts.push_back(std::move(current));
    if (options.observe_recipe) {
      options.observe_recipe(recipe_index, false);
    }
    if (result.attempts.back().status == Status::invalid) {
      result.status = Status::invalid;
      break;  // A mismatch is an optimizer defect, not a relaxation trigger.
    }
    if (budget.resource_exhausted) {
      break;  // a memory/time refusal is global; a spent recipe slice is not
    }
    carry = budget.remaining;  // an unspent slice funds the next recipe
  }
  return result;
}

bool verify(const Logic_network& source, const Unate_network& net, const Recipe& recipe, Budget& budget) {
  if (!source.valid() || net.outputs.size() != source.outputs.size()) {
    return false;
  }
  Logic_network extended;
  const bool    extended_model = !net.encodings.empty() || std::any_of(net.nodes.begin(), net.nodes.end(), [](const auto& node) {
    return node.functional
           && std::any_of(node.witness_inputs.begin(), node.witness_inputs.end(), [&](Id id) { return id >= node.origin; });
  });
  if (extended_model) {
    if (net.encodings.size() > max_encoder_nodes || !budget.spend(source.nodes.size() + net.encodings.size())) {
      return false;
    }
    extended = source;
    for (const auto& encoding : net.encodings) {
      std::set<Id> unique;
      if (encoding.source || !valid_table(encoding.table) || encoding.inputs.size() != encoding.table.inputs
          || encoding.inputs.size() > recipe.support) {
        return false;
      }
      for (auto in : encoding.inputs) {
        if (!budget.spend() || in >= extended.nodes.size() || (in < source.nodes.size() && !source.nodes[in].source)
            || !unique.insert(in).second) {
          return false;
        }
      }
      extended.add_function(encoding.inputs, encoding.table);
    }
  }
  const auto&                   model = extended_model ? extended : source;
  std::set<std::pair<Id, bool>> identities;
  uint32_t                      depth = 0, support = 0, literals = 0, series = 0, inverters = 0;
  for (Id id = 0; id < net.nodes.size(); ++id) {
    if (!budget.spend()) {
      return false;
    }
    const auto& n = net.nodes[id];
    if (n.origin >= model.nodes.size() || !identities.emplace(n.origin, n.negative).second
        || std::any_of(n.ports.begin(), n.ports.end(), [&](Id in) { return in >= id; })) {
      return false;
    }
    if (n.kind == Node_kind::source || n.kind == Node_kind::source_inverter) {
      if (!model.nodes[n.origin].source || n.level != 0 || !n.terms.empty() || !n.logical_inputs.empty()
          || !n.witness_inputs.empty() || n.care || !n.image_sources.empty() || n.functional) {
        return false;
      }
      if (n.kind == Node_kind::source) {
        if (n.negative || !n.ports.empty()) {
          return false;
        }
      } else {
        if (!n.negative || n.ports.size() != 1 || net.nodes[n.ports[0]].origin != n.origin
            || net.nodes[n.ports[0]].kind != Node_kind::source) {
          return false;
        }
        ++inverters;
      }
      continue;
    }
    if (n.kind != Node_kind::function || model.nodes[n.origin].source || n.logical_inputs.size() > recipe.support) {
      return false;
    }
    Truth_table expected;
    if (n.functional) {
      Id         query_root = n.origin;
      const bool copied     = extended_model;
      if (copied) {
        // Encoders have appended identities. Duplicate this original root only
        // for the query so all divisors precede it; its logic stays unchanged.
        const auto definition = extended.nodes[n.origin];
        query_root            = extended.add_function(definition.inputs, definition.table);
      }
      const auto dependency = exact_dependency(model, query_root, n.witness_inputs, max_logical_inputs, budget, max_symbolic_nodes);
      if (copied) {
        extended.nodes.pop_back();
      }
      if (dependency.status != Dependency_status::proven || !n.care || dependency.care != *n.care
          || dependency.sources != n.image_sources) {
        return false;
      }
      expected = dependency.table;
    } else {
      if (!cut_table(model, n.origin, n.witness_inputs, expected, budget)) {
        return false;
      }
    }
    if (n.negative) {
      expected = expected.complement();
    }
    if (n.care && !n.functional) {
      const auto image = exact_image(model, n.witness_inputs, max_logical_inputs, budget, max_symbolic_nodes);
      if (image.status != Status::feasible || image.care != *n.care || image.sources != n.image_sources) {
        return false;
      }
    } else if (!n.care && !n.image_sources.empty()) {
      return false;
    }
    if (!valid_table(n.completion) || n.completion.inputs != n.logical_inputs.size()) {
      return false;
    }
    std::vector<uint32_t> projection;
    std::set<Id>          logical;
    for (auto in : n.logical_inputs) {
      auto it = std::find(n.witness_inputs.begin(), n.witness_inputs.end(), in);
      if (it == n.witness_inputs.end() || !logical.insert(in).second) {
        return false;
      }
      projection.push_back(static_cast<uint32_t>(it - n.witness_inputs.begin()));
    }
    // Prove support shrinking and the chosen completion on the independently
    // established image, or on every cut assignment for a total-care witness.
    for (uint32_t x = 0; x < (uint32_t{1} << expected.inputs); ++x) {
      if (!budget.spend(projection.size() + 1)) {
        return false;
      }
      uint32_t projected = 0;
      for (uint32_t j = 0; j < projection.size(); ++j) {
        projected |= ((x >> projection[j]) & 1) << j;
      }
      if ((!n.care || n.care->get(x)) && expected.get(x) != n.completion.get(projected)) {
        return false;
      }
    }
    Form                                   form;
    uint32_t                               level = 1;
    std::vector<std::pair<uint32_t, bool>> variables;
    std::set<Id>                           ports;
    for (auto port : n.ports) {
      const auto& producer = net.nodes[port];
      auto        it       = std::find(n.logical_inputs.begin(), n.logical_inputs.end(), producer.origin);
      if (it == n.logical_inputs.end() || !ports.insert(port).second) {
        return false;
      }
      variables.emplace_back(static_cast<uint32_t>(it - n.logical_inputs.begin()), producer.negative);
      level = std::max(level, producer.level + 1);
    }
    for (const auto& term : n.terms) {
      Cube               cube;
      std::set<uint32_t> used;
      for (auto port : term) {
        if (!budget.spend() || port >= variables.size() || !used.insert(port).second) {
          return false;
        }
        const auto [v, negative] = variables[port];
        const auto bit           = uint32_t{1} << v;
        if (cube.care & bit) {
          return false;  // both rails in a product: not a valid implicant
        }
        cube.care |= bit;
        if (!negative) {
          cube.ones |= bit;
        }
      }
      form.cubes.push_back(cube);
      form.literals += std::popcount(cube.care);
      form.series    = std::max(form.series, static_cast<uint32_t>(std::popcount(cube.care)));
      form.positive |= cube.ones;
      form.negative |= cube.care & ~cube.ones;
    }
    if (level != n.level || level > level_limit(recipe) || form.literals > recipe.literals || form.series > recipe.series
        || !check_form(n.completion, form, budget)) {
      return false;
    }
    depth    = std::max(depth, level);
    support  = std::max(support, static_cast<uint32_t>(n.logical_inputs.size()));
    literals = std::max(literals, form.literals);
    series   = std::max(series, form.series);
  }
  for (uint32_t j = 0; j < net.outputs.size(); ++j) {
    if (net.outputs[j] >= net.nodes.size()) {
      return false;
    }
    const auto& n = net.nodes[net.outputs[j]];
    if (n.origin != source.outputs[j] || n.negative) {
      return false;
    }
  }
  return depth == net.depth && support == net.max_support && literals == net.max_literals && series == net.max_series
         && inverters == net.source_inverters;
}
}  // namespace livehd::synth

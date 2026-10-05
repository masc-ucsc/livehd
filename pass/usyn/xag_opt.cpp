// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "xag_opt.hpp"

#include <algorithm>
#include <tuple>

#include "npn4.hpp"
#include "xag_representations.hpp"

namespace livehd::usyn {
namespace {
std::optional<double> cone_cost(const Xag& graph, Xsignal root, std::span<const Id> basis, const Choice_options& options,
                                Budget& work, const Native_cost_model* model) {
  auto cone = collect_subwindow(graph, root, basis, {8, 1024}, work);
  if (cone.status != Status::feasible) {
    return {};
  }
  if (model) {
    if (!work.spend(1 + basis.size() + cone.interior.size())) {
      return {};
    }
    Xag                   local;
    std::map<Id, Xsignal> mapped{
        {0, local.constant(false)}
    };
    for (auto leaf : basis) {
      mapped.emplace(leaf, local.input(std::to_string(leaf)));
    }
    const auto translated = [&](Xsignal signal) {
      const auto value = mapped.at(signal.id);
      return signal.inverted ? ~value : value;
    };
    for (auto id : cone.interior) {
      const auto& node = graph.node(id);
      if (node.kind != Xag::Kind::and_gate && node.kind != Xag::Kind::xor_gate) {
        return {};
      }
      const auto a = translated(node.inputs[0]), b = translated(node.inputs[1]);
      mapped.emplace(id, node.kind == Xag::Kind::and_gate ? local.land(a, b) : local.lxor(a, b));
    }
    const std::array outputs{translated(root)};
    const auto       cost = estimate_native_cost(local, outputs, *model, work, options.gate_objective);
    if (cost.status != Status::feasible) {
      return {};
    }
    return options.gate_objective ? static_cast<double>(cost.gates) : cost.area;
  }
  uint64_t cost = 0;
  for (auto id : cone.interior) {
    const auto kind  = graph.node(id).kind;
    cost            += kind == Xag::Kind::and_gate ? options.and_cost : kind == Xag::Kind::xor_gate ? options.xor_cost : 0;
  }
  return static_cast<double>(cost);
}
Native_estimate proxy(const Choice_extraction& graph, const Choice_options& options, Budget& work) {
  Native_estimate result;
  if (!work.spend(graph.graph.size() + graph.outputs.size())) {
    result.status = Status::search_exhausted;
    return result;
  }
  for (Id id = 0; id < graph.graph.size(); ++id) {
    const auto kind  = graph.graph.node(id).kind;
    result.gates    += kind == Xag::Kind::and_gate ? options.and_cost : kind == Xag::Kind::xor_gate ? options.xor_cost : 0;
  }
  for (auto output : graph.outputs) {
    result.depth = std::max(result.depth, graph.graph.node(output.id).level);
  }
  result.area   = static_cast<double>(result.gates);
  result.status = Status::feasible;
  return result;
}
bool lower(const Native_estimate& a, const Native_estimate& b, bool gates) {
  if (a.status != Status::feasible) {
    return false;
  }
  if (b.status != Status::feasible) {
    return true;
  }
  return gates ? std::tie(a.gates, a.area, a.depth) < std::tie(b.gates, b.area, b.depth)
               : std::tie(a.area, a.depth, a.gates) < std::tie(b.area, b.depth, b.gates);
}
}  // namespace

Choice_result optimize_choices(const Xag& source, std::span<const Xsignal> outputs, const Choice_options& options, Budget& work,
                               const Native_cost_model* model) {
  Choice_result result;
  if (!options.classes || options.classes > 1024 || !options.and_cost || !options.xor_cost) {
    return result;
  }
  const auto estimate = [&](const Choice_extraction& network, Budget& budget) {
    return model ? estimate_native_cost(network.graph, network.outputs, *model, budget, options.gate_objective)
                 : proxy(network, options, budget);
  };
  // Structural incumbent first. Trials never replace it until a complete
  // extraction, global pricing and every protected output's depth guard pass.
  auto incumbent = extract_choices(source, {}, {}, outputs, work, options.max_nodes);
  if (incumbent.status != Status::feasible) {
    result.status = incumbent.status;
    return result;
  }
  result.status        = Status::feasible;
  result.report.before = result.report.after  = estimate(incumbent, work);
  result.report.limited                      |= result.report.before.limited;
  if (result.report.before.status != Status::feasible) {
    result.report.limited = true;
    result.network        = std::move(incumbent);
    return result;
  }
  Xag                     scratch        = incumbent.graph;
  const auto              original_nodes = scratch.size();
  std::vector<Xag_choice> classes;
  std::vector<uint32_t>   area, depth;
  std::vector<bool>       live(original_nodes);
  for (auto output : incumbent.outputs) {
    live[output.id] = true;
  }
  for (size_t end = original_nodes; end > 0; --end) {
    const auto  id   = static_cast<Id>(end - 1);
    const auto& node = scratch.node(id);
    if (live[id] && (node.kind == Xag::Kind::and_gate || node.kind == Xag::Kind::xor_gate)) {
      live[node.inputs[0].id] = live[node.inputs[1].id] = true;
    }
  }
  auto search = work.slice(10000000, 1, 64 * original_nodes + outputs.size() + 4096);
  for (Id id = 1; id < original_nodes && classes.size() < options.classes; ++id) {
    if (!live[id] || scratch.node(id).kind == Xag::Kind::source) {
      continue;
    }
    auto local  = search.slice(100000, 1, 4096);
    auto window = grow_window(scratch, {id, false}, {8, 64}, local);
    if (window.status != Status::feasible || window.leaves.empty()) {
      search.absorb(local);
      result.report.limited = true;
      continue;
    }
    auto function = window_function(scratch, window, local);
    auto choice   = make_choice(scratch, {id, false}, window.leaves, local);
    if (function.status != Status::feasible || !choice) {
      search.absorb(local);
      result.report.limited = true;
      continue;
    }
    std::vector<Xsignal> inputs;
    for (auto leaf : window.leaves) {
      inputs.push_back({leaf, false});
    }
    auto representation_work = local.slice(40000, 1, 20000);
    auto representations     = represent_function(scratch, function.table, inputs, representation_work, options.max_nodes);
    local.absorb(representation_work);
    result.report.bdd_nodes  += representations.bdd_nodes;
    result.report.sop_cubes  += representations.sop_cubes;
    result.report.dsd_blocks += representations.dsd_blocks;
    result.report.rejected   += representations.rejected;
    result.report.limited    |= representations.limited;
    std::vector<Xsignal> candidates;
    for (const auto& candidate : representations.candidates) {
      candidates.push_back(candidate.signal);
    }
    if (window.leaves.size() <= 4) {
      std::array<Xsignal, 4> leaves{};
      std::copy(inputs.begin(), inputs.end(), leaves.begin());
      uint16_t truth = 0;
      for (uint32_t pattern = 0; pattern < 16; ++pattern) {
        truth |= function.table.get(pattern & ((1U << inputs.size()) - 1)) << pattern;
      }
      auto npn = npn4_candidates(scratch, truth, leaves, local, options.max_nodes);
      candidates.insert(candidates.end(), npn.signals.begin(), npn.signals.end());
    }
    struct Rank {
      Xsignal  signal;
      double   cost;
      uint32_t level;
    };
    std::vector<Rank> ranking;
    auto              old_cost = cone_cost(scratch, {id, false}, window.leaves, options, local, model);
    if (old_cost) {
      ranking.push_back({
          {id, false},
          *old_cost,
          scratch.node(id).level
      });
    }
    for (auto candidate : candidates) {
      ++result.report.candidates;
      auto cost = cone_cost(scratch, candidate, window.leaves, options, local, model);
      if (cost && std::none_of(ranking.begin(), ranking.end(), [&](const auto& rank) { return rank.signal == candidate; })) {
        ranking.push_back({candidate, *cost, scratch.node(candidate.id).level});
      }
    }
    if (ranking.size() > 1) {
      auto area_rank = ranking, depth_rank = ranking;
      std::stable_sort(area_rank.begin(), area_rank.end(), [](const auto& a, const auto& b) {
        return std::tie(a.cost, a.level) < std::tie(b.cost, b.level);
      });
      std::stable_sort(depth_rank.begin(), depth_rank.end(), [](const auto& a, const auto& b) {
        return std::tie(a.level, a.cost) < std::tie(b.level, b.cost);
      });
      for (const auto candidate : {area_rank.front().signal, depth_rank.front().signal}) {
        const auto status = retain_choice(scratch, *choice, candidate, local);
        if (status != Status::feasible) {
          result.report.limited   = true;
          result.report.rejected += status == Status::invalid;
        }
      }
      if (choice->members().size() > 1) {
        const auto find = [&](Xsignal candidate) {
          const auto& members = choice->members();
          const auto  it      = std::find(members.begin(), members.end(), candidate);
          return it == members.end() ? 0U : static_cast<uint32_t>(it - members.begin());
        };
        area.push_back(find(area_rank.front().signal));
        depth.push_back(find(depth_rank.front().signal));
        result.report.retained += choice->members().size();
        classes.push_back(std::move(*choice));
      }
    }
    search.absorb(local);
    if (search.resource_exhausted || !search.available()) {
      result.report.limited = true;
      break;
    }
  }
  result.report.classes       = classes.size();
  result.report.scratch_nodes = scratch.size();
  if (classes.size() == options.classes) {
    result.report.limited = true;
  }
  work.absorb(search);
  const auto            snapshot_outputs = incumbent.outputs;
  auto                  selected         = std::move(incumbent);
  std::vector<uint32_t> best_selection(classes.size());
  const auto            consider_selection = [&](const std::vector<uint32_t>& selection) {
    auto trial_work = work.slice(10000000, 1, 8 * scratch.size() + outputs.size() + 32);
    auto trial      = extract_choices(scratch, classes, selection, snapshot_outputs, trial_work, options.max_nodes);
    work.absorb(trial_work);
    ++result.report.extractions;
    if (trial.status != Status::feasible) {
      result.report.limited = true;
      if (trial.status == Status::invalid) {
        ++result.report.cycles;
      }
      return;
    }
    auto       pricing_work  = work.slice(10000000, 1, 32);
    const auto cost          = estimate(trial, pricing_work);
    result.report.limited   |= cost.limited;
    work.absorb(pricing_work);
    bool depth_ok = trial.outputs.size() == selected.outputs.size();
    for (size_t i = 0; depth_ok && i < trial.outputs.size(); ++i) {
      depth_ok &= trial.graph.node(trial.outputs[i].id).level <= source.node(outputs[i].id).level;
    }
    if (cost.status != Status::feasible) {
      result.report.limited = true;
    }
    if (depth_ok && lower(cost, result.report.after, options.gate_objective)) {
      selected            = std::move(trial);
      best_selection      = selection;
      result.report.after = cost;
      ++result.report.selections;
    }
  };
  if (!work.resource_exhausted && !classes.empty()) {
    consider_selection(area);
    if (depth != area) {
      consider_selection(depth);
    }
    // Local prices miss cross-output sharing. Reprice at most eight single
    // class changes against the complete currently selected network, starting
    // at consumers so an unnecessary replacement can restore useful sharing.
    uint32_t recovery = 0;
    for (size_t end = classes.size(); end > 0 && recovery < 8; --end) {
      const auto i = end - 1;
      for (uint32_t member = 0; member < classes[i].members().size() && recovery < 8; ++member) {
        if (member == best_selection[i]) {
          continue;
        }
        auto trial_selection = best_selection;
        trial_selection[i]   = member;
        consider_selection(trial_selection);
        ++recovery;
        if (work.resource_exhausted) {
          break;
        }
      }
      if (work.resource_exhausted) {
        result.report.limited = true;
        break;
      }
    }
  } else if (work.resource_exhausted) {
    result.report.limited = true;
  }
  result.network = std::move(selected);
  return result;
}
}  // namespace livehd::usyn

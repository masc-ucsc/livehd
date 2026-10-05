// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "cmos_cleanup.hpp"

#include "sop_factor.hpp"

namespace livehd::usyn {
Cmos_cleanup_result clean_cmos_expansion(const Xag_region& source, const Residual_options& options, Budget& search, bool sop_tree,
                                         bool multi_rep, const Native_cost_model* cost_model, bool gate_objective) {
  Cmos_cleanup_result result;
  if (source.status != Status::feasible) {
    result.reason = "invalid CMOS expansion";
    return result;
  }
  if (!search.spend(source.inputs.size() + source.state.size() + source.outputs.size())) {
    result.status = Status::search_exhausted;
    result.reason = "CMOS cleanup admission budget";
    return result;
  }
  std::vector<Residual_output> roots;
  for (const auto& output : source.outputs) {
    roots.push_back({output.signal, false});
  }
  for (const auto& state : source.state) {
    roots.push_back({state.d, false});
  }
  auto optimized = optimize_residual(source.graph, roots, options, search);
  result.status  = optimized.status;
  result.reason  = std::move(optimized.reason);
  result.report  = std::move(optimized.report);
  if (!optimized.network) {
    return result;
  }
  auto& network = *optimized.network;
  if (multi_rep) {
    std::vector<Xsignal> outputs;
    for (const auto& output : network.outputs) {
      outputs.push_back(output.signal);
    }
    Choice_options choice_options;
    choice_options.max_nodes      = options.max_nodes;
    choice_options.and_cost       = options.and_cost;
    choice_options.xor_cost       = options.xor_cost;
    choice_options.gate_objective = gate_objective;
    auto trial_work               = search.slice(options.stage_work, 1, 16 * network.graph.size() + 32);
    auto trial                    = optimize_choices(network.graph, outputs, choice_options, trial_work, cost_model);
    search.absorb(trial_work);
    result.choices = std::move(trial.report);
    if (trial.status == Status::invalid) {
      result.status = Status::invalid;
      result.reason = "multi-representation choice validation";
      return result;
    }
    if (trial.network) {
      for (size_t i = 0; i < network.outputs.size(); ++i) {
        network.outputs[i].signal = trial.network->outputs[i];
      }
      network.graph            = std::move(trial.network->graph);
      result.report.cost_after = 0;
      for (Id id = 0; id < network.graph.size(); ++id) {
        const auto kind           = network.graph.node(id).kind;
        result.report.cost_after += kind == Xag::Kind::and_gate   ? options.and_cost
                                    : kind == Xag::Kind::xor_gate ? options.xor_cost
                                                                  : 0;
      }
    }
    if (result.choices.limited || trial.status != Status::feasible) {
      result.report.exhausted = true;
      result.report.limits.push_back("multi-representation choice bound");
    }
  }
  Xag_region region;
  const auto port = [&](Xsignal old) -> std::optional<Xsignal> {
    if (old.inverted || old.id >= source.graph.size() || source.graph.node(old.id).kind != Xag::Kind::source) {
      return {};
    }
    const auto index = source.graph.node(old.id).source_index;
    if (index >= network.graph.input_names().size()) {
      return {};
    }
    // Rebuilds emit every original source first, in its original order.
    const Xsignal signal{index + 1, false};
    if (network.graph.node(signal.id).kind != Xag::Kind::source || network.graph.node(signal.id).source_index != index) {
      return {};
    }
    return signal;
  };
  for (auto input : source.inputs) {
    const auto signal = port(input);
    if (!signal) {
      result.status = Status::invalid;
      result.reason = "invalid CMOS cleanup input correspondence";
      return result;
    }
    region.inputs.push_back(*signal);
  }
  for (size_t i = 0; i < source.state.size(); ++i) {
    const auto& state = source.state[i];
    const auto  q     = port(state.q);
    if (!q) {
      result.status = Status::invalid;
      result.reason = "invalid CMOS cleanup state correspondence";
      return result;
    }
    region.state.push_back({state.name, state.init, *q, network.outputs[source.outputs.size() + i].signal});
  }
  for (size_t i = 0; i < source.outputs.size(); ++i) {
    region.outputs.push_back({source.outputs[i].name, network.outputs[i].signal});
  }
  if (sop_tree) {
    std::vector<Xsignal> outputs;
    for (const auto& output : network.outputs) {
      outputs.push_back(output.signal);
    }
    auto trial_work = search.slice(options.stage_work, 1, 4 * network.graph.size() + outputs.size() + 32);
    auto trial      = factor_sop(network.graph, outputs, trial_work, options.max_nodes);
    search.absorb(trial_work);
    result.report.sop_roots     = trial.roots;
    result.report.sop_cofactors = trial.cofactors;
    if (trial.status == Status::feasible) {
      // An explicitly selected complete alternative, rather than a claim of
      // native proxy improvement. The original expanded model remains owned by
      // the caller. Mapping ablations judge this candidate's actual gate count.
      for (size_t i = 0; i < region.outputs.size(); ++i) {
        region.outputs[i].signal = trial.outputs[i];
      }
      for (size_t i = 0; i < region.state.size(); ++i) {
        region.state[i].d = trial.outputs[region.outputs.size() + i];
      }
      network.graph = std::move(trial.graph);
      std::vector<bool> live(network.graph.size());
      for (auto output : trial.outputs) {
        live[output.id] = true;
      }
      result.report.cost_after = 0;
      if (!search.spend(network.graph.size() + trial.outputs.size())) {
        result.status            = Status::search_exhausted;
        result.report.cost_after = result.report.cost_before;
        result.report.sop_roots  = 0;
        result.report.exhausted  = true;
        result.report.limits.push_back("SOP choice pricing budget");
        result.reason = "SOP choice pricing budget";
        return result;
      }
      for (size_t end = network.graph.size(); end > 0; --end) {
        const auto  id   = static_cast<Id>(end - 1);
        const auto& node = network.graph.node(id);
        if (live[id] && (node.kind == Xag::Kind::and_gate || node.kind == Xag::Kind::xor_gate)) {
          result.report.cost_after += node.kind == Xag::Kind::and_gate ? options.and_cost : options.xor_cost;
          live[node.inputs[0].id] = live[node.inputs[1].id] = true;
        }
      }
    }
    if (trial.limited || trial.status != Status::feasible) {
      result.report.exhausted = true;
      result.report.limits.push_back("SOP choice bound");
    }
  }
  region.graph  = std::move(network.graph);
  // original_nodes referred to the import snapshot, not this expansion. Raw
  // source/control Lids remain authoritative in the owner's Source_state_table.
  region.status = Status::feasible;
  result.region = std::move(region);
  return result;
}
}  // namespace livehd::usyn

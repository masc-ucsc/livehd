// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "endpoint_lnet.hpp"

namespace livehd::usyn {

Lnet_result expand_cmos(const Xag_region& region, std::span<const Cmos_endpoint> endpoints, const Endpoint_options& options,
                        Budget& work, uint32_t max_nodes) {
  Lnet_result result;
  if (region.status != Status::feasible || endpoints.size() > region.state.size() || max_nodes == 0) {
    result.reason = "invalid CMOS expansion request";
    return result;
  }
  if (endpoints.empty()) {
    return export_lnet(region, work, max_nodes);
  }
  const auto refuse = [&] {
    result.status = Status::search_exhausted;
    result.reason = "CMOS expansion budget";
    return std::move(result);
  };
  if (region.graph.size() > max_nodes || region.inputs.size() > max_nodes || region.state.size() > max_nodes
      || region.outputs.size() > max_nodes
      || !work.spend(region.graph.size() + region.inputs.size() + region.state.size() + region.outputs.size())) {
    return refuse();
  }
  std::vector<bool> selected(region.state.size(), false);
  for (const auto& endpoint : endpoints) {
    if (!work.spend()) {
      return refuse();
    }
    const auto index = endpoint.state_index;
    if (index >= region.state.size() || selected[index] || endpoint.solution.name != region.state[index].name
        || endpoint.solution.root != region.state[index].d) {
      result.reason = "endpoint does not identify a unique original state";
      return result;
    }
    if (!validate_endpoint(region.graph, endpoint.solution, options, work)) {
      if (work.exhausted) {
        return refuse();
      }
      result.reason = "invalid endpoint function or phase structure";
      return result;
    }
    selected[index] = true;
  }

  // Appending to a private hashed XAG permits ordinary CMOS sharing across
  // proposed DOMINO boundaries. The selected artifact remains authoritative;
  // this expansion deliberately imposes no mapping boundary at a cell output.
  Xag_region lowered;
  lowered.graph   = region.graph;
  lowered.inputs  = region.inputs;
  lowered.state   = region.state;
  lowered.outputs = region.outputs;
  lowered.status  = Status::feasible;
  for (const auto& endpoint : endpoints) {
    std::vector<Xsignal> cells;
    for (const auto& cell : endpoint.solution.cells) {
      const auto&          function = *cell.function;
      std::vector<Xsignal> inputs;
      for (size_t i = 0; i < function.inputs.size(); ++i) {
        inputs.push_back(cell.producers[i] < 0 ? Xsignal{function.inputs[i], false} : cells[cell.producers[i]]);
      }
      std::vector<Xsignal> formula;
      formula.reserve(function.formula.nodes.size());
      for (const auto& node : function.formula.nodes) {
        if (!work.spend() || lowered.graph.size() >= max_nodes) {
          return refuse();
        }
        Xsignal signal;
        switch (node.kind) {
          case Gate_formula::Kind::constant: signal = lowered.graph.constant(node.inverted); break;
          case Gate_formula::Kind::literal:
            signal = inputs[node.variable];
            if (node.inverted) {
              signal = ~signal;
            }
            break;
          case Gate_formula::Kind::series  : signal = lowered.graph.land(formula[node.left], formula[node.right]); break;
          case Gate_formula::Kind::parallel: signal = lowered.graph.lor(formula[node.left], formula[node.right]); break;
        }
        formula.push_back(signal);
      }
      cells.push_back(function.formula.output_inverted ? ~formula.back() : formula.back());
    }
    lowered.state[endpoint.state_index].d = cells.back();
  }
  return export_lnet(lowered, work, max_nodes);
}

}  // namespace livehd::usyn

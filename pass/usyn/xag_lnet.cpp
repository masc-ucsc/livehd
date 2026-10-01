// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "xag_lnet.hpp"

#include <functional>

namespace livehd::usyn {

Xag_region import_lnet(const synth::Lnet& net, Budget& work, uint32_t max_nodes) {
  Xag_region out;
  if (max_nodes == 0) {
    out.reason = "invalid XAG node budget";
    return out;
  }
  if (net.size() > max_nodes) {
    out.status = Status::search_exhausted;
    out.reason = "source node budget";
    return out;
  }
  out.original_nodes.resize(net.size());
  std::vector<bool> assigned(net.size());
  const auto        source = [&](synth::Lid id, const std::string& name) {
    if (id >= net.size() || assigned[id] || net.kind(id) != synth::Lnet::Kind::source) {
      return false;
    }
    out.original_nodes[id] = out.graph.input(name);
    assigned[id]           = true;
    return true;
  };
  for (const auto& pi : net.inputs()) {
    if (!work.spend()) {
      out.status = Status::search_exhausted;
      out.reason = "source import budget";
      return out;
    }
    if (!source(pi.node, pi.name)) {
      out.reason = "invalid Lnet input";
      return out;
    }
    out.inputs.push_back(out.original_nodes[pi.node]);
  }
  for (const auto& latch : net.latches()) {
    if (!work.spend()) {
      out.status = Status::search_exhausted;
      out.reason = "state import budget";
      return out;
    }
    if (!source(latch.q, latch.name) || latch.d >= net.size()) {
      out.reason = "invalid Lnet state boundary";
      return out;
    }
    out.state.push_back({latch.name, latch.init, out.original_nodes[latch.q], {}});
  }
  for (synth::Lid id = 0; id < net.size(); ++id) {
    if (!work.spend() || out.graph.size() > max_nodes) {
      out.status = Status::search_exhausted;
      out.reason = "XAG construction budget";
      return out;
    }
    if (net.kind(id) == synth::Lnet::Kind::constant) {
      out.original_nodes[id] = out.graph.constant(net.eval(id, 0));
    } else if (net.kind(id) == synth::Lnet::Kind::source) {
      if (!assigned[id]) {
        out.reason = "Lnet source has no boundary identity";
        return out;
      }
    } else {
      for (auto in : net.fanins(id)) {
        if (in >= id || !assigned[in]) {
          out.reason = "Lnet is not topologically ordered";
          return out;
        }
      }
      // Local Shannon construction is bounded by Lnet's eight-input table,
      // never by the potentially huge support of the surrounding cone.
      bool       admitted = true;
      const auto build    = [&](auto&& self, uint32_t variable, uint32_t assignment) -> Xsignal {
        if (!work.spend() || out.graph.size() >= max_nodes) {
          admitted = false;
          return {};
        }
        if (variable == net.fanin_count(id)) {
          return out.graph.constant(net.eval(id, assignment));
        }
        const auto lo = self(self, variable + 1, assignment);
        if (!admitted) {
          return {};
        }
        const auto hi = self(self, variable + 1, assignment | (1U << variable));
        if (!admitted) {
          return {};
        }
        return out.graph.mux(out.original_nodes[net.fanin(id, variable)], hi, lo);
      };
      out.original_nodes[id] = build(build, 0, 0);
      if (!admitted || out.graph.size() > max_nodes) {
        out.status = Status::search_exhausted;
        out.reason = "XAG construction budget";
        return out;
      }
    }
    assigned[id] = true;
  }
  for (size_t i = 0; i < out.state.size(); ++i) {
    out.state[i].d = out.original_nodes[net.latches()[i].d];
  }
  for (const auto& po : net.outputs()) {
    if (po.node >= net.size()) {
      out.reason = "invalid Lnet output";
      return out;
    }
    out.outputs.push_back({po.name, out.original_nodes[po.node]});
  }
  out.status = Status::feasible;
  return out;
}

Lnet_result export_lnet(const Xag_region& region, Budget& work, uint32_t max_nodes) {
  using synth::Lid;
  using synth::Lnet;
  Lnet_result result;
  const auto& graph = region.graph;
  if (region.status != Status::feasible || max_nodes == 0) {
    result.reason = "invalid XAG export request";
    return result;
  }
  const auto refuse = [&] {
    result.status = Status::search_exhausted;
    result.reason = "logical emission budget";
    return std::move(result);
  };
  if (graph.size() > max_nodes || region.inputs.size() > max_nodes || region.state.size() > max_nodes
      || region.outputs.size() > max_nodes || !work.spend(graph.size())) {
    return refuse();
  }
  Lnet             net;
  std::vector<Lid> mapped(graph.size(), Lnet::kNone), negative(graph.size(), Lnet::kNone);
  mapped[0]         = Lnet::kConst0;
  const auto source = [&](Xsignal signal, const std::string& name, bool latch, char init) {
    if (!work.spend() || net.size() >= max_nodes) {
      result.status = Status::search_exhausted;
      return false;
    }
    if (signal.inverted || signal.id >= graph.size() || mapped[signal.id] != Lnet::kNone
        || graph.node(signal.id).kind != Xag::Kind::source) {
      return false;
    }
    mapped[signal.id] = latch ? net.latch(net.add_latch(name, init)).q : net.add_input(name);
    return true;
  };
  for (auto pi : region.inputs) {
    if (pi.id >= graph.size() || graph.node(pi.id).kind != Xag::Kind::source
        || !source(pi, graph.input_names()[graph.node(pi.id).source_index], false, 'x')) {
      result.reason = "invalid or unadmitted input boundary";
      return result;
    }
  }
  for (const auto& state : region.state) {
    if ((state.init != '0' && state.init != '1' && state.init != 'x') || !source(state.q, state.name, true, state.init)) {
      result.reason = "invalid or unadmitted state boundary";
      return result;
    }
  }
  if (region.inputs.size() + region.state.size() != graph.input_names().size()) {
    result.reason = "unidentified XAG sources";
    return result;
  }

  std::vector<bool> live(graph.size(), false);
  std::vector<Id>   todo;
  for (const auto& po : region.outputs) {
    todo.push_back(po.signal.id);
  }
  for (const auto& state : region.state) {
    todo.push_back(state.d.id);
  }
  while (!todo.empty()) {
    if (!work.spend()) {
      return refuse();
    }
    const auto id = todo.back();
    todo.pop_back();
    if (id >= graph.size()) {
      result.reason = "invalid XAG output boundary";
      return result;
    }
    if (live[id]) {
      continue;
    }
    live[id]         = true;
    const auto& node = graph.node(id);
    if (node.kind == Xag::Kind::and_gate || node.kind == Xag::Kind::xor_gate) {
      todo.push_back(node.inputs[0].id);
      todo.push_back(node.inputs[1].id);
    }
  }
  for (Id id = 1; id < graph.size(); ++id) {
    if (!work.spend()) {
      return refuse();
    }
    if (!live[id] || mapped[id] != Lnet::kNone) {
      continue;
    }
    if (net.size() >= max_nodes) {
      return refuse();
    }
    const auto&        node = graph.node(id);
    const auto         a = node.inputs[0], b = node.inputs[1];
    // Xag guarantees topological node IDs; all source nodes were identified
    // above. Preserve complements in the LUT, not as per-edge NOT nodes.
    const auto         va   = a.inverted ? ~Lnet::kVar0 : Lnet::kVar0;
    constexpr uint64_t var1 = 0xCCCCCCCCCCCCCCCCULL;
    const auto         vb   = b.inverted ? ~var1 : var1;
    mapped[id]              = net.add_lut({mapped[a.id], mapped[b.id]}, node.kind == Xag::Kind::and_gate ? va & vb : va ^ vb);
  }
  bool       admitted = true;
  const auto output   = [&](Xsignal signal) -> Lid {
    if (!work.spend()) {
      admitted = false;
      return Lnet::kConst0;
    }
    if (!signal.inverted) {
      return mapped[signal.id];
    }
    if (negative[signal.id] == Lnet::kNone) {
      if (net.size() >= max_nodes) {
        admitted = false;
        return Lnet::kConst0;
      }
      negative[signal.id] = signal.id == 0 ? net.add_constant(true) : net.add_lut({mapped[signal.id]}, Lnet::kNot);
    }
    return negative[signal.id];
  };
  for (uint32_t i = 0; i < region.state.size(); ++i) {
    const auto d = output(region.state[i].d);
    if (!admitted) {
      return refuse();
    }
    net.set_latch_input(i, d);
  }
  for (const auto& po : region.outputs) {
    const auto signal = output(po.signal);
    if (!admitted) {
      return refuse();
    }
    net.add_output(signal, po.name);
  }
  result.net    = std::move(net);
  result.status = Status::feasible;
  return result;
}

}  // namespace livehd::usyn

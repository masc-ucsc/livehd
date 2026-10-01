// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "satopt_muxtree.hpp"

#include <algorithm>

#include "cprop_muxctx.hpp"
#include "satopt_detail.hpp"

namespace livehd::satopt {
namespace {
using namespace detail;
using Path = std::vector<std::pair<Pin, bool>>;

bool boundary(Node n) {
  return gu::has_color(n) || gu::has_runtime_check(n) || gu::has_name(n) || !gu::pin_name_of(n.get_driver_pin(0)).empty();
}

std::vector<Pin> inputs(Node node, Meter& meter) {
  std::vector<Pin> pins;
  for (auto sink : node.inp_sorted_pins()) {
    if (!meter.work(1) || sink.get_port_id() != pins.size() || sink.get_driver_pin().is_invalid()) {
      return {};
    }
    const auto pin = sink.get_driver_pin();
    const auto op  = gu::type_op_of(pin.get_master_node());
    if (op == Ntype_op::Latch || op == Ntype_op::Flop || (pin.is_const() && gu::const_of(pin).has_unknowns())) {
      return {};
    }
    pins.push_back(pin);
  }
  return pins;
}

Path context(Node node, Meter& meter) {
  Path                      path;
  absl::flat_hash_set<Node> seen;
  // Each step has one parent; four facts and four ancestors bound both work
  // and premise size. A dropped conjunct weakens the premise conservatively.
  for (int depth = 0; depth < 4 && path.size() < 4; ++depth) {
    if (!meter.work(1) || boundary(node) || !seen.insert(node).second) {
      break;
    }
    auto edges = node.out_edges();
    auto it    = edges.begin();
    if (it == edges.end()) {
      break;
    }
    const auto sink = (*it).sink;
    if (++it != edges.end()) {
      break;
    }
    auto       parent = sink.get_master_node();
    const auto op     = gu::type_op_of(parent);
    if (gu::has_color(parent) || gu::has_runtime_check(parent) || (op != Ntype_op::Mux && op != Ntype_op::Hotmux)) {
      break;
    }
    const auto   pins = inputs(parent, meter);
    const size_t pid  = sink.get_port_id();
    if (op == Ntype_op::Mux) {
      if (pins.size() != 3 || pid == 0) {
        break;
      }
      path.emplace_back(pins[0], pid == 2);
    } else {
      if (pins.size() < 2 || !livehd::muxctx::exclusive(parent)) {
        break;
      }
      if (pid % 2) {
        path.emplace_back(pins[pid - 1], true);
      } else if (pid + 1 == pins.size()) {
        for (size_t i = 0; i + 1 < pins.size() && path.size() < 4; i += 2) {
          path.emplace_back(pins[i], false);
        }
      } else {
        break;
      }
    }
    node = parent;
  }
  return path;
}

std::optional<bool> nominate(Word_sim& sim, Pin target, const Path& path, Meter& meter) {
  const auto                            before = sim.work();
  const auto*                           values = sim.values(target);
  std::vector<const std::vector<Dlop>*> conditions;
  for (const auto& [pin, active] : path) {
    (void)active;
    conditions.push_back(sim.values(pin));
  }
  if (!meter.work(sim.work() - before) || !values
      || std::any_of(conditions.begin(), conditions.end(), [](auto p) { return p == nullptr; })) {
    return {};
  }
  std::optional<bool> candidate;
  for (size_t column = 0; column < values->size(); ++column) {
    if (!meter.work(path.size() + 1)) {
      return {};
    }
    bool matches = true;
    for (size_t i = 0; i < path.size(); ++i) {
      const auto& v  = (*conditions[i])[column];
      matches       &= !v.has_unknowns() && (!v.is_known_zero() == path[i].second);
    }
    if (!matches) {
      continue;
    }
    const auto& v = (*values)[column];
    if (v.has_unknowns()) {
      return {};
    }
    const bool truth = !v.is_known_zero();
    if (candidate && *candidate != truth) {
      return {};
    }
    candidate = truth;
  }
  // No matching samples is no nomination, even for an unreachable context.
  return candidate;
}
}  // namespace

void simplify_mux_contexts(const std::vector<std::shared_ptr<hhds::Graph>>& graphs, Stage_report& report, Meter& meter) {
  using namespace detail;
  for (const auto& graph : graphs) {
    if (!graph || !meter.work(1)) {
      continue;
    }
    std::vector<std::pair<Node, size_t>> targets;
    for (auto node : graph->body().nodes()) {
      if (!meter.work(1)) {
        break;
      }
      const auto op = gu::type_op_of(node);
      if (boundary(node) || (op != Ntype_op::Mux && op != Ntype_op::Hotmux)) {
        continue;
      }
      const auto pins = inputs(node, meter);
      if (op == Ntype_op::Mux && pins.size() == 3) {
        targets.emplace_back(node, 0);
      }
      if (op == Ntype_op::Hotmux && pins.size() >= 2 && livehd::muxctx::exclusive(node)) {
        for (size_t i = 0; i + 1 < pins.size(); i += 2) {
          targets.emplace_back(node, i);
        }
      }
    }
    auto sim = std::make_unique<Word_sim>(
        Word_sim::Options{.samples = meter.budget().samples, .descend = true, .reject_unstamped = true});
    for (auto [node, pid] : targets) {
      if (!meter.work(1)) {
        break;
      }
      const auto pins = inputs(node, meter);
      if (pid >= pins.size() || pins[pid].is_const()) {
        continue;
      }
      const auto path = context(node, meter);
      if (path.empty()) {
        continue;
      }
      ++report.candidates;
      const auto candidate = nominate(*sim, pins[pid], path, meter);
      if (!candidate) {
        ++report.sim_rejects;
        continue;
      }
      if (gu::type_op_of(node) == Ntype_op::Hotmux && *candidate) {
        bool removable_control = false;
        for (size_t i = 0; i + 1 < pins.size(); i += 2) {
          removable_control |= i != pid && (!pins[i].is_const() || !gu::const_of(pins[i]).is_known_zero());
        }
        if (!removable_control) {
          ++report.sim_rejects;
          continue;
        }
      }
      if (!meter.query()) {
        ++report.budget_skips;
        break;
      }
      // A fresh prover after every mutation: neither terms nor contextual
      // premises can survive a graph edit or leak to a sibling context.
      formal::Prover prover(graph.get(), prove_options(meter.budget(), true, true));
      const auto     result = prover.truth_when(pins[pid], *candidate, path);
      const bool     within = meter.work(prover.work());
      ++report.queries;
      if (result.verdict == formal::Verdict::Proven) {
        ++report.proven;
        if (!within) {
          ++report.budget_skips;
          break;
        }
        if (gu::type_op_of(node) == Ntype_op::Hotmux) {
          gu::set_proven(node, gu::kFormalOnehot);
        }
        const auto tie = [&](size_t index, bool truth) {
          const auto sink = node.get_sink_pin(index);
          pins[index].del_sink(sink);
          gu::create_const(*graph, *Dlop::create_integer(truth)).connect_sink(sink);
        };
        if (gu::type_op_of(node) == Ntype_op::Mux || !*candidate) {
          tie(pid, *candidate);
          ++report.applied;
        } else {
          // A contextual true control is not globally true. Keep it intact,
          // and disable the other proven-exclusive controls under this path.
          // Only disabling controls retains the GLOBAL exclusivity proof.
          for (size_t i = 0; i + 1 < pins.size(); i += 2) {
            if (i != pid && (!pins[i].is_const() || !gu::const_of(pins[i]).is_known_zero())) {
              tie(i, false);
              ++report.applied;
            }
          }
        }
        sim = std::make_unique<Word_sim>(
            Word_sim::Options{.samples = meter.budget().samples, .descend = true, .reject_unstamped = true});
      } else if (result.verdict == formal::Verdict::Refuted) {
        ++report.refuted;
        const auto before = sim->work();
        sim->add_model(result.model);
        meter.work(sim->work() - before);
      } else {
        ++report.unknown;
      }
    }
  }
}
}  // namespace livehd::satopt

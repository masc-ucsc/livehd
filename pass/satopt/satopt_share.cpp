// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "satopt_share.hpp"

#include "cprop_opshare.hpp"
#include "satopt_detail.hpp"

namespace livehd::satopt {
namespace {
using namespace detail;
struct Candidate {
  Node node;
  Pin  control;
  bool active;
  int  operand_width;
};

std::optional<Candidate> candidate(Node node, Meter& meter) {
  const auto op    = gu::type_op_of(node);
  const bool shift = op == Ntype_op::SHL || op == Ntype_op::SRA;
  if (op != Ntype_op::Mult && op != Ntype_op::Div && op != Ntype_op::Rem && !shift) {
    return {};
  }
  const auto pin = node.get_driver_pin(0);
  if (gu::bits_of(pin) < (shift ? 8 : 4) || gu::has_color(node) || gu::has_runtime_check(node) || gu::has_name(node)
      || !gu::pin_name_of(pin).empty()) {
    return {};
  }
  auto edges = node.out_edges();
  auto it    = edges.begin();
  if (it == edges.end()) {
    return {};
  }
  const auto sink = (*it).sink;
  // This is the complete all-use audit, not a selected convenient consumer.
  // Multiple, named/opaque, graph-output and check uses reject the candidate.
  if (++it != edges.end() || sink.get_port_id() == 0 || sink.get_port_id() > 2) {
    return {};
  }
  const auto parent = sink.get_master_node();
  if (gu::type_op_of(parent) != Ntype_op::Mux || gu::has_color(parent) || gu::has_runtime_check(parent)) {
    return {};
  }
  size_t count = 0;
  Pin    control;
  for (auto s : parent.inp_sorted_pins()) {
    if (!meter.work(1) || s.get_port_id() != count++) {
      return {};
    }
    const auto p = s.get_driver_pin();
    if (p.is_invalid()) {
      return {};
    }
    const auto kind = gu::type_op_of(p.get_master_node());
    if (kind == Ntype_op::Latch || kind == Ntype_op::Flop) {
      return {};
    }
    if (s.get_port_id() == 0) {
      control = p;
    }
  }
  if (count != 3 || control.is_const()) {
    return {};
  }
  int max_width = 0;
  for (auto s : node.inp_sorted_pins()) {
    if (!meter.work(1)) {
      return {};
    }
    const auto p = s.get_driver_pin();
    if (p.is_invalid() || (!p.is_const() && gu::bits_of(p) <= 0)) {
      return {};
    }
    if (shift && s.get_port_id() == 1 && p.is_const()) {
      return {};
    }
    max_width = std::max(max_width, width(p));
  }
  return Candidate{node, control, sink.get_port_id() == 2, max_width};
}

bool independent(const Candidate& a, const Candidate& b, Meter& meter) {
  const auto       ap = a.node.get_driver_pin(0), bp = b.node.get_driver_pin(0);
  std::vector<Pin> pending{a.control, b.control};
  for (auto node : {a.node, b.node}) {
    for (auto s : node.inp_sorted_pins()) {
      pending.push_back(s.get_driver_pin());
    }
  }
  absl::flat_hash_set<Pin> seen;
  while (!pending.empty()) {
    const auto p = pending.back();
    pending.pop_back();
    if (!meter.work(1) || p == ap || p == bp) {
      return false;
    }
    if (p.is_const() || cut(p) || !seen.insert(p).second) {
      continue;
    }
    if (seen.size() > 256) {
      return false;
    }
    for (auto s : p.get_master_node().inp_sorted_pins()) {
      if (!meter.work(1)) {
        return false;
      }
      auto d = s.get_driver_pin();
      if (d.is_invalid()) {
        return false;
      }
      pending.push_back(d);
    }
  }
  return true;
}

bool no_sample_overlap(Word_sim& sim, const Candidate& a, const Candidate& b, Meter& meter) {
  const auto  before = sim.work();
  const auto* av     = sim.values(a.control);
  const auto* bv     = sim.values(b.control);
  if (!meter.work(sim.work() - before) || !av || !bv) {
    return false;
  }
  for (size_t i = 0; i < av->size(); ++i) {
    if (!meter.work(1) || (*av)[i].has_unknowns() || (*bv)[i].has_unknowns()) {
      return false;
    }
    if ((!(*av)[i].is_known_zero() == a.active) && (!(*bv)[i].is_known_zero() == b.active)) {
      return false;
    }
  }
  return true;
}
}  // namespace

void share_operators(const std::vector<std::shared_ptr<hhds::Graph>>& graphs, Stage_report& report, Meter& meter) {
  using namespace detail;
  for (const auto& graph : graphs) {
    if (!graph || !meter.work(1)) {
      continue;
    }
    // Stable graph-order buckets, with at most 32 previous candidates tried
    // per new candidate. No overlapping shared-driver buckets or N^2 search.
    using Key = std::tuple<Ntype_op, int, bool>;
    std::map<Key, std::vector<Candidate>> buckets;
    for (auto node : graph->body().nodes()) {
      if (!meter.work(1)) {
        break;
      }
      if (auto c = candidate(node, meter)) {
        const auto p = node.get_driver_pin(0);
        buckets[{gu::type_op_of(node), gu::bits_of(p), gu::is_unsign(p)}].push_back(*c);
      }
    }
    auto                      sim = std::make_unique<Word_sim>(sim_options(meter.budget(), true, true));
    absl::flat_hash_set<Node> consumed;
    for (const auto& [key, bucket] : buckets) {
      (void)key;
      for (size_t i = 1; i < bucket.size() && !meter.exhausted(); ++i) {
        if (consumed.contains(bucket[i].node)) {
          continue;
        }
        for (size_t j = i > 32 ? i - 32 : 0; j < i; ++j) {
          if (!meter.work(1)) {
            break;
          }
          if (consumed.contains(bucket[j].node)) {
            continue;
          }
          const auto& a = bucket[j];
          const auto& b = bucket[i];
          ++report.candidates;
          if (std::max(a.operand_width, b.operand_width) > 2 * std::min(a.operand_width, b.operand_width)
              || !independent(a, b, meter)) {
            ++report.sim_rejects;
            continue;
          }
          // Use the exact cprop descriptor, guards, matching and width policy
          // before spending a proof query; the dry run performs no mutation.
          size_t     work       = 4096;
          const bool compatible = livehd::share_exclusive_operators(*graph, a.node, b.node, a.control, a.active, work, false);
          if (!meter.work(4096 - work)) {
            break;
          }
          if (!compatible || !no_sample_overlap(*sim, a, b, meter)) {
            ++report.sim_rejects;
            continue;
          }
          if (!meter.query()) {
            ++report.budget_skips;
            break;
          }
          formal::Prover prover(graph.get(), prove_options(meter.budget(), true, true));
          const auto     result = prover.truth_when(b.control,
                                                    !b.active,
                                                    {
                                                        {a.control, a.active}
          });
          const bool     within = meter.work(prover.work());
          ++report.queries;
          if (result.verdict == formal::Verdict::Proven) {
            ++report.proven;
            if (!within || !meter.work(4096 - work)) {
              ++report.budget_skips;
              break;
            }
            work = 4096;
            if (livehd::share_exclusive_operators(*graph, a.node, b.node, a.control, a.active, work, true)) {
              consumed.insert(a.node);
              consumed.insert(b.node);
              ++report.applied;
              ++report.nodes_removed;
              sim = std::make_unique<Word_sim>(sim_options(meter.budget(), true, true));
              break;
            }
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
  }
}
}  // namespace livehd::satopt

// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "lib_cell_prep.hpp"

#include <format>
#include <string>
#include <vector>

#include "absl/container/flat_hash_set.h"
#include "cell.hpp"
#include "inline_sub.hpp"
#include "latch_contract.hpp"
#include "node_util.hpp"

namespace livehd::lec {

namespace gu = livehd::graph_util;
namespace lc = livehd::latch_contract;

// box_node_key lives in encode.cpp (one definition, shared identity).
std::string box_node_key(const hhds::Occurrence_node& n);

namespace {

// A cell the phase schedule may see through on a latch enable: combinational
// (no state, no memory, no instance, no clock operator -- the "pure" test of
// proof_prep inline_clock_lib_cells) AND a single product term, i.e. no Or.
// gensim writes a model as a sum of products, so this admits BUF/INV/AND/NAND/
// NOR/ANDN (an And of literals, maybe inverted) and keeps OR/XOR/AOI opaque.
// The restriction is deliberate: the schedule models an And-form gate
// (`clk & en`, De Morgan spellings included) with the guard's own polarity,
// but its Or-form gate (`clk | ~en`) only for an inverted HELD-latch operand;
// exposing an arbitrary Or would feed it shapes whose guard polarity it does
// not model. An opaque cell keeps the old, conservative data-latch reading.
bool is_exposable_gate_model(hhds::Graph* model) {
  if (model == nullptr) {
    return false;
  }
  for (auto node : model->body().nodes()) {
    const auto op = gu::type_op_of(node);
    if (gu::is_type_register(node) || op == Ntype_op::Memory || op == Ntype_op::Sub || op == Ntype_op::Clock_cell
        || op == Ntype_op::Or) {
      return false;
    }
  }
  return true;
}

// Splice every listed Sub of `g`'s body from its --lib model. Collected by class
// index and resolved against ONE snapshot of the body: a splice creates and
// deletes nodes, but it never renumbers an untouched node, so the remaining
// handles stay valid (the same batch shape proof_prep's clock-cone splice uses).
// The inliner binds the model by port NAME through the instance's own IO; one it
// refuses stops the batch with `res.error` set, never a silent skip.
void splice_batch(hhds::Graph* g, const absl::flat_hash_set<hhds::Class_index>& subs, const Lib_cell_models& sub_lib,
                  Lib_cell_prep_result& res) {
  std::vector<hhds::Node_class> todo;
  for (auto n : g->body().nodes()) {
    if (gu::type_op_of(n) == Ntype_op::Sub && subs.contains(n.get_class_index())) {
      todo.push_back(n);
    }
  }
  for (const auto& inst : todo) {
    auto it = sub_lib.find(inst.get_subnode_gid());
    if (it == sub_lib.end() || it->second == nullptr) {
      continue;  // unreachable: the walk only records cells with a model
    }
    auto* model = it->second;  // combinational (is_exposable_gate_model): no state to name
    if (!gu::inline_sub_instance(g, inst, "pass.lec", model)) {
      const auto why = gu::sub_def_port_mismatch(inst, model);
      res.error = std::format("lec: cannot splice the latch-enable --lib cell instance '{}' (cell '{}') from its --lib model: {}",
                              gu::default_instance_name(inst),
                              model->get_name(),
                              why.empty() ? std::string{"the inliner refused it (see the diagnostic above)"} : why);
      return;
    }
    ++res.spliced;
  }
}

}  // namespace

Lib_cell_prep_result inline_clock_latch_enable_cells(const Lib_cell_models& sub_lib, hhds::Graph* top,
                                                     const std::function<bool(const hhds::Graph*)>& is_boxed) {
  Lib_cell_prep_result res;
  if (top == nullptr || sub_lib.empty()) {
    return res;
  }
  absl::flat_hash_set<hhds::Gid> exposable;
  for (const auto& [gid, model] : sub_lib) {
    if (is_exposable_gate_model(model)) {
      exposable.insert(gid);
    }
  }
  if (exposable.empty()) {
    return res;
  }
  // FAST REJECT, like plan_phases: no latch anywhere, nothing to expose (and
  // no flattened occurrence walk to pay for).
  {
    bool any_latch = false;
    for (auto n : top->grouped_hierarchy().nodes()) {
      if (gu::type_op_of(n) == Ntype_op::Latch) {
        any_latch = true;
        break;
      }
    }
    if (!any_latch) {
      return res;
    }
  }
  // The planner's own opacity: a boxed def is one unit, never descended.
  ankerl::unordered_dense::set<hhds::Gid> opaque_subs;
  if (is_boxed) {
    for (auto sn : top->grouped_hierarchy().nodes()) {
      if (gu::type_op_of(sn) != Ntype_op::Sub) {
        continue;
      }
      if (auto def = sn.get_subnode_graph(); def && is_boxed(def.get())) {
        opaque_subs.insert(sn.get_subnode_gid());
      }
    }
  }
  const ankerl::unordered_dense::set<hhds::Gid>* opaque = opaque_subs.empty() ? nullptr : &opaque_subs;

  std::vector<hhds::Occurrence_pin> enables;
  for (auto node : top->occurrences(opaque).nodes(hhds::Node_order::forward)) {
    if (gu::type_op_of(node) == Ntype_op::Latch) {
      enables.push_back(lc::sink_driver_hier(node, "enable"));
    }
  }
  if (enables.empty()) {
    return res;
  }
  // The planner's clock notion (phase_sched.cpp plan_phases).
  const lc::Design_clocks clocks(top, /*hier=*/true, opaque);

  // node key -> 0 no / 1 yes / 2 on the stack. A cell is recorded when ITS
  // output reaches a clock; every operand is explored (no short-circuit) so a
  // clock-dependent cell behind another operand is found too.
  absl::flat_hash_map<std::string, uint8_t>                                 memo;
  absl::flat_hash_map<hhds::Graph*, absl::flat_hash_set<hhds::Class_index>> cells;
  auto reaches = [&](auto&& self, const hhds::Occurrence_pin& p, int depth) -> bool {
    if (p.is_invalid() || p.is_const()) {
      return false;
    }
    if (gu::is_graph_input_pin(p)) {
      return clocks.is_clock(p);
    }
    const auto n  = p.get_master_node();
    const auto op = gu::type_op_of(n);
    if (op == Ntype_op::Clock_cell) {
      return true;
    }
    if (gu::is_type_register(n) || op == Ntype_op::Memory || depth > 256) {
      return false;  // state is a data boundary; an absurdly deep cone stays as it was
    }
    const bool is_cell = op == Ntype_op::Sub;
    if (is_cell) {
      // Only a body-less --lib cell (the splice binds its model by port name);
      // a design def with a body is descended by the occurrence walk itself,
      // and anything else stays an opaque boundary.
      if (!exposable.contains(n.get_subnode_gid()) || n.get_subnode_graph() || n.is_loop_subnode()
          || sub_lib.contains(n.get_graph()->get_gid())) {
        return false;
      }
    }
    const auto key = box_node_key(n);
    if (auto m = memo.find(key); m != memo.end()) {
      return m->second == 1;
    }
    memo[key] = 2;
    bool dep  = false;
    for (auto sink : n.inp_sorted_pins()) {
      for (auto driver : sink.get_driver_pins()) {
        dep = self(self, driver, depth + 1) || dep;
      }
    }
    memo[key] = dep ? 1 : 0;
    if (dep && is_cell) {
      cells[n.get_graph()].insert(n.get_class_index());
    }
    return dep;
  };
  for (const auto& en : enables) {
    (void)reaches(reaches, en, 0);
  }
  for (const auto& [g, subs] : cells) {
    splice_batch(g, subs, sub_lib, res);
    if (!res.error.empty()) {
      break;
    }
  }
  return res;
}

}  // namespace livehd::lec

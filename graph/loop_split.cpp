// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "loop_split.hpp"

#include <vector>

#include "absl/container/flat_hash_map.h"
#include "hlop/dlop.hpp"
#include "node_util.hpp"

namespace livehd::graph_util {

namespace {

// Walk back through pure pass-throughs so a carry wired via a width coercion or
// an identity mask still matches the shapes below. Bounded: a coercion chain is
// a handful of nodes, and the guard keeps a malformed cycle from spinning.
//
// Same rule as semdiff's identity_get_mask_input: the mask selects exactly `a`'s
// declared width, either as the explicit full-width value or as the `-1`
// "every bit" sentinel tolg emits for a coercion, and the result is not
// narrower than `a` (a full-width mask on a truncating Get_mask is NOT an
// identity).
hhds::Pin_class skip_identities(hhds::Pin_class d) {
  for (int guard = 0; guard < 32 && !d.is_invalid(); ++guard) {
    auto n = d.get_master_node();
    if (n.is_invalid() || type_op_of(n) != Ntype_op::Get_mask) {
      return d;
    }
    auto a    = get_driver_of_sink_name(n, "a");
    auto mask = get_driver_of_sink_name(n, "mask");
    if (a.is_invalid() || mask.is_invalid() || !is_const_pin(mask)) {
      return d;
    }
    const auto bits     = bits_of(a);
    const auto out_bits = bits_of(d);
    if (bits <= 0 || (out_bits > 0 && out_bits < bits)) {
      return d;
    }
    const auto value    = hydrate_const(mask);
    const bool all_bits = value.is_just_i64() && value.to_just_i64() == -1;
    if (!all_bits) {
      auto full = Dlop::get_mask_value(bits);
      if (!full || !value.is_known_eq(*full)) {
        return d;
      }
    }
    d = a;
  }
  return d;
}

// Within ONE iteration, state is the only scheduling boundary: a flop's output
// does not depend on its input in the same cycle. A Sub is NOT a boundary -- a
// pure-comb call is seen through, conservatively (every input feeds every
// output), which is the same model graph/split_selfref.hpp's
// comb_pin_depends_on uses. Treating a Sub as opaque here would let
// `mask = f_sub(carry)` pass as carry-independent.
bool is_state_boundary(const hhds::Node_class& n) {
  const auto op = type_op_of(n);
  return is_type_flop(n) || op == Ntype_op::Memory || op == Ntype_op::Latch;
}

// Does `start`'s backward cone reach ANY pin in `targets`? Compared per PIN, not
// per node: every graph input shares the singleton INPUT node, so a node-level
// test would report that the invariant and the index "depend on the carry" and
// every shape below would fall through to `induction`.
//
// Iterative on purpose: a loop body is an arbitrary comb cone (an inlined
// callee, a per-bit ripple), so a recursive walk would be bounded only by the
// stack.
bool cone_reaches_any(const hhds::Pin_class& start, const absl::flat_hash_set<hhds::Pin_class>& targets) {
  if (start.is_invalid() || targets.empty()) {
    return false;
  }
  absl::flat_hash_set<hhds::Node_class> visited;
  std::vector<hhds::Pin_class>          work{start};
  while (!work.empty()) {
    auto d = work.back();
    work.pop_back();
    if (d.is_invalid()) {
      continue;
    }
    if (targets.contains(d)) {
      return true;
    }
    auto n = d.get_master_node();
    if (n.is_invalid() || is_builtin_node(n)) {
      continue;  // some other graph input / constant: not a target
    }
    if (!visited.insert(n).second || is_state_boundary(n)) {
      continue;
    }
    for (const auto& e : n.inp_edges()) {
      work.push_back(e.driver);
    }
  }
  return false;
}

// Collect the backward cone of `start`, stopping at the body inputs and at
// state. These are the nodes that carry recurrence state.
void collect_cone(const hhds::Pin_class& start, absl::flat_hash_set<hhds::Node_class>& out) {
  std::vector<hhds::Pin_class> work{start};
  while (!work.empty()) {
    auto d = work.back();
    work.pop_back();
    if (d.is_invalid()) {
      continue;
    }
    auto n = d.get_master_node();
    if (n.is_invalid() || is_builtin_node(n) || !out.insert(n).second || is_state_boundary(n)) {
      continue;
    }
    for (const auto& e : n.inp_edges()) {
      work.push_back(e.driver);
    }
  }
}

// The declared INPUT pin for a body port, or an invalid pin when the port is
// not declared. Declared IO pins are materialized with the body, so this never
// allocates -- the analysis must not write to the graph it describes.
hhds::Pin_class declared_input_pin(const hhds::Graph& body, hhds::Port_id port) {
  auto io = body.get_io();
  if (!io || !io->has_input_with_port_id(port)) {
    return {};
  }
  return body.get_input_node().get_driver_pin(port);
}

}  // namespace

bool is_associative_op(Ntype_op op) {
  switch (op) {
    case Ntype_op::Sum:
    case Ntype_op::Mult:
    case Ntype_op::And:
    case Ntype_op::Or:
    case Ntype_op::Xor : return true;
    default            : return false;
  }
}

bool is_idempotent_op(Ntype_op op) {
  // op(x, x) == x. Sum and Xor are associative but NOT idempotent, which is
  // exactly why `sum += x` is count-SENSITIVE while `any |= x` is not.
  switch (op) {
    case Ntype_op::And:
    case Ntype_op::Or : return true;
    default           : return false;
  }
}

Loop_split classify_loop(const hhds::Node_class& loop_sub) {
  Loop_split out;
  if (loop_sub.is_invalid() || type_op_of(loop_sub) != Ntype_op::Sub || !loop_sub.is_loop_subnode()) {
    return out;
  }
  auto body = loop_sub.get_subnode_graph();
  if (!body) {
    return out;  // body-less black box: nothing to classify
  }
  hhds::Subnode_group group{loop_sub};
  if (!group.is_loop()) {
    return out;
  }
  const auto loop = group.loop();
  if (!loop.has_value()) {
    return out;
  }
  out.valid = true;

  for (auto n : body->body().nodes(hhds::Node_order::forward)) {
    if (!n.is_invalid() && !is_builtin_node(n)) {
      ++out.body_nodes;
    }
  }

  auto out_node = body->get_output_node();

  // Every carry-in pin. A value that depends on ANY of them is ordered by the
  // iteration -- including through a SIBLING carry: `l[f(cnt)] = v; cnt += 1`
  // writes lane r where an earlier lane's counter says, which is a recurrence
  // even though `l`'s own carry never feeds the position.
  absl::flat_hash_set<hhds::Pin_class> carry_ins;
  for (const auto& carry : group.carries()) {
    auto p = declared_input_pin(*body, carry.input_port());
    if (!p.is_invalid()) {
      carry_ins.insert(p);
    }
  }
  const auto depends_on_a_carry = [&](const hhds::Pin_class& p) { return cone_reaches_any(p, carry_ins); };

  absl::flat_hash_set<hhds::Pin_class> index_pins;
  if (loop->index_input.has_value()) {
    auto p = declared_input_pin(*body, *loop->index_input);
    if (!p.is_invalid()) {
      index_pins.insert(p);
    }
  }

  for (const auto& carry : group.carries()) {
    Carry_class cc;
    cc.in_port  = carry.input_port();
    cc.out_port = carry.output_port();

    // carry_in is a driver on the body's INPUT node; carry_out is whatever
    // drives the body's OUTPUT node at the carry's port.
    auto carry_in = declared_input_pin(*body, cc.in_port);

    hhds::Pin_class carry_out;
    for (const auto& e : out_node.inp_edges()) {
      if (e.sink.get_port_id() == cc.out_port) {
        carry_out = e.driver;
        break;
      }
    }
    if (carry_in.is_invalid() || carry_out.is_invalid()) {
      out.carries.push_back(cc);  // undeclared / undriven: stays `induction`, the safe answer
      continue;
    }

    auto       head   = skip_identities(carry_out);
    auto       head_n = head.get_master_node();
    const auto op     = head_n.is_invalid() ? Ntype_op::Invalid : type_op_of(head_n);

    if (op == Ntype_op::Set_mask) {
      // acc = set_mask(acc, mask(index), value): the per-lane write. Parallel iff
      // the WINDOW is chosen by the loop INDEX and independently of every carry
      // -- otherwise where lane r writes depends on what earlier lanes wrote,
      // which is a real recurrence. A constant window (`l#[1] = ...` in every
      // lane) is the last-writer recurrence, not a per-lane slice. The remaining
      // question (do two lanes' windows overlap?) is a value question this
      // cannot settle; flag it for the consumer.
      auto       base           = get_driver_of_sink_name(head_n, "a");
      auto       mask           = get_driver_of_sink_name(head_n, "mask");
      auto       value          = get_driver_of_sink_name(head_n, "value");
      const bool base_is_carry  = !base.is_invalid() && skip_identities(base) == carry_in;
      const bool mask_is_free   = !mask.is_invalid() && !depends_on_a_carry(mask);
      const bool value_is_free  = value.is_invalid() || !depends_on_a_carry(value);
      const bool mask_via_index = !mask.is_invalid() && cone_reaches_any(mask, index_pins);
      if (base_is_carry && mask_is_free && value_is_free && mask_via_index) {
        cc.kind                 = Carry_kind::disjoint_slice;
        cc.needs_disjoint_proof = true;
      }
    } else if (is_associative_op(op)) {
      // acc = op(acc, X) with X independent of every carry. Only the FOLD sink
      // (`as`, pid 0) re-associates: Sum's `bs` subtracts, so `X - acc` on it is
      // an alternating recurrence, not a reduction. Exactly one fold operand
      // must be the carry and the rest must be carry-free.
      int  carry_operands = 0;
      int  free_operands  = 0;
      bool foreign        = false;
      for (const auto& e : head_n.inp_edges()) {
        if (e.sink.get_port_id() != 0) {
          foreign = true;  // an operand on a non-fold sink (Sum/Mult `bs`)
        } else if (skip_identities(e.driver) == carry_in) {
          ++carry_operands;
        } else if (!depends_on_a_carry(e.driver)) {
          ++free_operands;
        } else {
          foreign = true;  // an operand that USES a carry without being it
        }
      }
      if (!foreign && carry_operands == 1 && free_operands >= 1) {
        cc.kind       = Carry_kind::assoc_reduction;
        cc.reduce_op  = op;
        cc.idempotent = is_idempotent_op(op);
      }
    }

    if (cc.kind == Carry_kind::induction) {
      collect_cone(carry_out, out.induction_nodes);
    }
    out.carries.push_back(cc);
  }
  return out;
}

}  // namespace livehd::graph_util

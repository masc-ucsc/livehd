//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

#include "absl/container/inlined_vector.h"
#include "hhds/graph.hpp"
#include "hlop/dlop.hpp"
#include "node_util.hpp"  // graph:graph — livehd::graph_util::* helpers
#include "pass.hpp"

class Cprop {
public:
  // A node's operand list: its SINK PINS, snapshotted.
  //
  // SNAPSHOT deliberately, not for convenience. Every method below rewires the
  // graph while walking this list (connect_driver, del_sink, set_type_op,
  // del_node), and a lazy pin view is a view over the live edge storage those
  // calls mutate. That is precisely why the old spelling, inp_edges(),
  // materialized; inp_pins_snapshot() is the same guarantee in pin form.
  //
  // One driver per sink pin (graph/cell.hpp), so a SINK PIN *is* an operand:
  // element `i` is the i-th operand's sink pin, `p.get_driver_pin()` is the
  // value feeding it, and `p.get_port_id()` is its port. Ports are ascending,
  // which is what the positional readers (inp_edges_ordered[0], [1], [2]) and
  // is_two_arm_mux depend on.
  using Inp_pins = absl::InlinedVector<hhds::Pin_class, 8>;

private:
  static inline std::atomic<int> trace_module_cnt = 0;

protected:
  hhds::Graph* current_graph = nullptr;

  void collapse_forward_same_op(hhds::Node_class& node, Inp_pins& inp_edges_ordered);
  void collapse_forward_sum(hhds::Node_class& node, Inp_pins& inp_edges_ordered);
  void collapse_forward_always_pin0(hhds::Node_class& node, Inp_pins& inp_edges_ordered);
  // Reconnect node's consumers to new_dpin and delete node. Returns false
  // (graph untouched) when forwarding would lose a parallel operand — a caller
  // that created new_dpin's node must then clean up the orphan.
  bool collapse_forward_for_pin(hhds::Node_class& node, hhds::Pin_class new_dpin);

  bool try_constant_prop(hhds::Node_class& node, Inp_pins& inp_edges_ordered);
  void try_collapse_forward(hhds::Node_class& node, Inp_pins& inp_edges_ordered);

  void replace_part_inputs_const(hhds::Node_class& node, Inp_pins& inp_edges_ordered);
  void replace_all_inputs_const(hhds::Node_class& node, Inp_pins& inp_edges_ordered);
  void replace_node(hhds::Node_class& node, const Dlop& result);
  void replace_node(hhds::Node_class& node, const spool_ptr<Dlop>& result) { replace_node(node, *result); }
  void replace_logic_node(hhds::Node_class& node, const Dlop& result);
  void replace_logic_node(hhds::Node_class& node, const spool_ptr<Dlop>& result) { replace_logic_node(node, *result); }

  bool            scalar_mux(hhds::Node_class& node, Inp_pins& inp_edges_ordered);
  void            scalar_sext(hhds::Node_class& node, Inp_pins& inp_edges_ordered);
  // EQ(EQ(x,0),0) -> x / EQ(b,1) -> b boolean-chain folds. true = node deleted.
  bool            scalar_eq(hhds::Node_class& node, Inp_pins& inp_edges_ordered);
  // 0/1 hygiene on Xor/And/Or: x^1 -> EQ(x,0), complementary literals,
  // absorption, duplicate operands. true = node deleted or rewired (the caller
  // re-reads its type and operands).
  bool            scalar_bool(hhds::Node_class& node, Inp_pins& inp_edges_ordered);
  // Constant shift-of-shift composition (SRA/SHL chains). true = node rewired
  // in place (caller must re-read input edges).
  bool            scalar_shift(hhds::Node_class& node, Inp_pins& inp_edges_ordered);
  // Or of constant-shifted copies of ONE 0/1 source ({N{bit}} replication) ->
  // Mux(bit, 0, mask). true = node deleted.
  bool            try_broadcast_or(hhds::Node_class& node, Inp_pins& inp_edges_ordered);
  hhds::Pin_class try_find_single_driver_pin(hhds::Node_class& node, int64_t pos);
  bool            scalar_get_mask(hhds::Node_class& node);
  bool            scalar_set_mask(hhds::Node_class& node);
  // Constant slice of a packed (Or-of-shifted-disjoint-fields) wire -> the one
  // operand that drives it. true = node deleted (folded to a constant); false
  // may still have rewired the node in place.
  bool            scalar_get_mask_packed(hhds::Node_class& node, const Dlop& mask_const);
  // Multi-bit slice straddling several Set_mask/Concat lanes -> one Concat of
  // the covering pieces. true = node retyped (no longer a Get_mask).
  bool            gather_straddling_slice(hhds::Node_class& node);

  void bwd_del_node(hhds::Node_class& node);

  // A latch already carries its write condition on `enable`.  Remove the
  // redundant hold arm from `din = enable ? data : Q` before the ordinary
  // scalar sweep, so downstream passes see the canonical `din = data` form.
  void canonicalize_latch_hold(const hhds::Node_class& latch);
  void canonicalize_flop_hold(const hhds::Node_class& flop);
  // din only matters while `enable` holds: resolve din's private Mux/Or/
  // Set_mask spine under the facts an Or-of-literals enable implies (the
  // per-lane hold Mux of `if (rst) q[k] <= i; else if (en) q[k] <= d;`).
  void canonicalize_flop_enable(const hhds::Node_class& flop);

  // Retype And(x, 2^n-1) [binary, one const] into the value-identical
  // Get_mask(x, 2^n-1) so every low-mask truncation shares ONE shape.
  void canonicalize_and_mask(hhds::Node_class& node);
  // Hash-cons identical pure combinational nodes (same op, same input pins).
  void cse_pass(const std::vector<hhds::Node_class>& order);
  // Expected-linear mux sharing over disjoint, single-consumer regions.
  void mux_share_pass();
  // One round of bit-slice vectorization: runs of 1-bit Mux(s, x[j], y[j+d])
  // over consecutive j become one Mux(s, x[j0..], y[j0+d..]). true = changed.
  bool vectorize_bit_muxes();
  // And/Or whose operands are one 1-bit expression over single-bit slices at
  // shifted positions -> one word expression tested under a mask. true = changed.
  bool vectorize_bit_reductions();
  // Merge adjacent Concat lanes that are contiguous slices of one source.
  // true = node rewritten (it may have been forwarded and deleted).
  bool merge_concat_slices(hhds::Node_class& node);
  void scalar_node(hhds::Node_class& node);

public:
  Cprop() = default;

  // Rewrites unlimited-precision values without consulting width/sign hints.
  void do_trans(const std::shared_ptr<hhds::Graph>& g);
};

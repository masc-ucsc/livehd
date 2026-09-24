//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
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

  bool scalar_mux(hhds::Node_class& node, Inp_pins& inp_edges_ordered, bool factor_packs = true);
  void scalar_sext(hhds::Node_class& node, Inp_pins& inp_edges_ordered);
  // EQ(EQ(x,0),0) -> x / EQ(b,1) -> b boolean-chain folds. true = node deleted.
  bool scalar_eq(hhds::Node_class& node, Inp_pins& inp_edges_ordered);
  // 0/1 hygiene on Xor/And/Or: x^1 -> EQ(x,0), complementary literals,
  // absorption, duplicate operands. true = node deleted or rewired (the caller
  // re-reads its type and operands).
  bool scalar_bool(hhds::Node_class& node, Inp_pins& inp_edges_ordered);
  // Constant shift-of-shift composition (SRA/SHL chains). true = node rewired
  // in place (caller must re-read input edges).
  bool scalar_shift(hhds::Node_class& node, Inp_pins& inp_edges_ordered);
  // Or of constant-shifted copies of ONE 0/1 source ({N{bit}} replication) ->
  // Mux(bit, 0, mask). true = node deleted.
  bool try_broadcast_or(hhds::Node_class& node, Inp_pins& inp_edges_ordered);
  bool indexed_get_mask(hhds::Node_class& node, hhds::Pin_class source, int lo, int hi);
  bool scalar_get_mask(hhds::Node_class& node);
  bool scalar_set_mask(hhds::Node_class& node);
  // Constant slice of a packed (Or-of-shifted-disjoint-fields) wire -> the one
  // operand that drives it. true = node deleted (folded to a constant); false
  // may still have rewired the node in place.
  bool scalar_get_mask_packed(hhds::Node_class& node, const Dlop& mask_const);
  // Multi-bit slice straddling several Set_mask/Concat lanes -> one Concat of
  // the covering pieces. true = node retyped (no longer a Get_mask).

  void bwd_del_node(hhds::Node_class& node);

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
  void remember_node(const hhds::Node_class& node);
  bool canonicalize_pack(hhds::Node_class& node);
  // Low-lane narrowing (cprop_lowlane.cpp): an operation reading a value
  // spelled Or(Shl(H, k), L) runs on the H parts, k bits narrower, and
  // re-attaches the low part. true = node replaced.
  bool low_lane(hhds::Node_class& node);
  bool low_lanes_ = false;
  void normalize_emitted(hhds::Node_class& node);
  void scalar_node(hhds::Node_class& node);

public:
  Cprop() = default;
  // `low_lanes`: also narrow operations over known low lanes. It creates cells
  // with no width stamp, so only callers that run bitwidth afterwards set it.
  explicit Cprop(bool low_lanes) : low_lanes_(low_lanes) {}
  // Rewrites unlimited-precision values without consulting width/sign hints.
  void do_trans(const std::shared_ptr<hhds::Graph>& g);
};

namespace livehd {
// The k of a value spelled as a low-lane form (Or(Shl(H,k),L), Shl(H,k),
// And(x,-2^k), or a two-lane Concat; cprop_lowlane.cpp): its low k bits are
// structurally known. 0 for any other value.
int low_lane_bits(const hhds::Pin_class& pin);
// Disjoint mux regions: ordinary cprop leaves destination-Q regions intact;
// enableopt exclusively owns their feedback-to-enable transformation.
void share_mux_regions(hhds::Graph& graph, bool state_context,
                       const std::function<bool(const hhds::Pin_class&)>& boolean_fact = {});
}  // namespace livehd

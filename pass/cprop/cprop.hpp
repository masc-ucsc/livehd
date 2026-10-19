//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include "absl/container/flat_hash_set.h"
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
  // P7: replace node by a constant fold result. A result with '?' bits keeps
  // the node (a new constant would split an operand literal); a fully known
  // result becomes a constant.
  void replace_node(hhds::Node_class& node, const Dlop& result);
  void replace_node(hhds::Node_class& node, const spool_ptr<Dlop>& result) { replace_node(node, *result); }
  void replace_logic_node(hhds::Node_class& node, const Dlop& result);
  void replace_logic_node(hhds::Node_class& node, const spool_ptr<Dlop>& result) { replace_logic_node(node, *result); }
  // Q2: a '?' that the node itself owns (x/0, x%0), not one taken from an
  // operand literal. Creating it is one new literal, not a split.
  void replace_node_fresh_unknown(hhds::Node_class& node, const Dlop& result);
  // Redirect node's consumers to an existing constant pin (a fold whose result
  // is exactly one operand literal forwards that literal's pin) and retire it.
  void forward_to_const_pin(hhds::Node_class& node, hhds::Pin_class dpin);
  // P7 follow-up: a '?'-dropping op (And/Or/Get_mask/Set_mask/SRA/Sext/Mux/
  // Ror/Rxor/Popcount) over a small all-constant cone whose
  // intermediate values hold '?' bits (kept as nodes, see replace_node) folds
  // when its three-valued value is FULLY KNOWN. true = node replaced.
  bool fold_known_cone(hhds::Node_class& node, const Inp_pins& inp_edges_ordered);
  // Driver pins whose cone reaches a non-constant leaf: fold_known_cone stops
  // there instead of re-walking it from every root. Cleared per cse_pass sweep.
  absl::flat_hash_set<hhds::Class_index> cone_dead;

  bool scalar_mux(hhds::Node_class& node, Inp_pins& inp_edges_ordered, bool factor_packs = true);
  void scalar_sext(hhds::Node_class& node, Inp_pins& inp_edges_ordered);
  // EQ(EQ(x,0),0) -> x / EQ(b,1) -> b boolean-chain folds. true = node deleted.
  bool scalar_eq(hhds::Node_class& node, Inp_pins& inp_edges_ordered);
  // EQ(x,x) -> 1, LT(x,x) / GT(x,x) -> 0 for one driver pin x. true = node deleted.
  bool scalar_same_operand_compare(hhds::Node_class& node, Inp_pins& inp_edges_ordered);
  // Not(Not(x)) -> x. true = node deleted.
  bool scalar_not(hhds::Node_class& node, Inp_pins& inp_edges_ordered);
  // A Div/Rem with one operand disconnected (P4): a-only -> a, b-only -> 0.
  // true = handled (node deleted or kept by a refused forward).
  bool collapse_single_input_divrem(hhds::Node_class& node, Inp_pins& inp_edges_ordered);
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
  bool scalar_get_mask_packed(hhds::Node_class& node);
  // Multi-bit slice straddling several Set_mask/Concat lanes -> one Concat of
  // the covering pieces. true = node retyped (no longer a Get_mask).

  void bwd_del_node(hhds::Node_class& node);

  // Retype And(x, 2^n-1) [binary, one const] into the value-identical
  // Get_mask(x, 0, n) so every low-mask truncation shares ONE shape.
  void canonicalize_and_mask(hhds::Node_class& node);
  // Hash-cons identical pure combinational nodes (same op, same input pins).
  void cse_pass(const std::vector<hhds::Node_class>& order);
  // Expected-linear mux sharing over disjoint, single-consumer regions.
  void mux_share_pass();
  // Factor private, same-shape operators out of binary/index mux arms.
  // Fresh operand muxes have no width hints; bitwidth infers them afterwards.
  void mux_op_share_pass();
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
  // The constructor's `low_lanes` opt-in doubles as "bitwidth runs after this
  // sweep": rewrites that mint cells with no width stamp (low-lane narrowing,
  // the accumulation guard in scalar_mux) run only then. Bitwidth_rewrite
  // reuses scalar_mux AFTER inference converged, so it must never see one.
  [[nodiscard]] bool may_emit_unstamped() const { return low_lanes_; }
  void normalize_emitted(hhds::Node_class& node);
  void scalar_node(hhds::Node_class& node);

public:
  // The cells a rewrite may touch; see set_guard_scope.
  using Node_scope = std::function<bool(const hhds::Node_class&)>;

private:
  Node_scope guard_scope_;

public:
  Cprop() = default;
  // `low_lanes`: also narrow operations over known low lanes (and, inside
  // set_guard_scope, move accumulation guards onto their terms). Both create
  // cells with no width stamp, so only callers that run bitwidth afterwards
  // set it.
  explicit Cprop(bool low_lanes) : low_lanes_(low_lanes) {}
  // The accumulation-guard rewrite in scalar_mux (`mux(s, a, a OP x)` ->
  // `a OP mux(s, id, x)`) is a ROLLED-LOOP rule: it turns a loop body's
  // guarded update into an associative reduction of its carry, which is what
  // the loop consumers (loop_split's assoc_reduction, synthesis' parallel-data
  // extraction and chain merge) exploit. It runs only on the Muxes and update
  // cells `scope` admits, and only with `low_lanes`. Empty (the default):
  // nowhere, so straight-line code -- including a hand-unrolled
  // `if c { acc |= x }` run -- compiles exactly as it did before rolled loops
  // existed. pass.cprop admits every cell of a lifted loop body (a callee of a
  // compact-loop Sub); synthesis' loop cleanup admits the cells an expansion
  // minted.
  void set_guard_scope(Node_scope scope) { guard_scope_ = std::move(scope); }
  [[nodiscard]] static Node_scope whole_graph() {
    return [](const hhds::Node_class&) { return true; };
  }
  // Rewrites unlimited-precision values without consulting width/sign hints.
  void do_trans(const std::shared_ptr<hhds::Graph>& g);
};

namespace livehd {
// The k of a value spelled as a low-lane form (Or(Shl(H,k),L), Shl(H,k),
// And(x,-2^k), or a two-lane Concat; cprop_lowlane.cpp): its low k bits are
// structurally known. 0 for any other value.
int  low_lane_bits(const hhds::Pin_class& pin);
// Disjoint mux regions: ordinary cprop leaves destination-Q regions intact;
// enableopt exclusively owns their feedback-to-enable transformation.
void share_mux_regions(hhds::Graph& graph, bool state_context,
                       const std::function<bool(const hhds::Pin_class&)>& boolean_fact = {});
}  // namespace livehd

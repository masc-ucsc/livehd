//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

#include "hhds/graph.hpp"
#include "hlop/dlop.hpp"
#include "node_util.hpp"  // graph:graph — livehd::graph_util::* helpers
#include "pass.hpp"

class Cprop {
private:
  static inline std::atomic<int> trace_module_cnt = 0;

protected:
  hhds::Graph* current_graph = nullptr;

  void collapse_forward_same_op(hhds::Node_class& node, livehd::graph_util::Edge_vec& inp_edges_ordered);
  void collapse_forward_sum(hhds::Node_class& node, livehd::graph_util::Edge_vec& inp_edges_ordered);
  void collapse_forward_always_pin0(hhds::Node_class& node, livehd::graph_util::Edge_vec& inp_edges_ordered);
  // Reconnect node's consumers to new_dpin and delete node. Returns false
  // (graph untouched) when a consumer width disagrees with new_dpin's — a caller
  // that created new_dpin's node must then clean up the orphan.
  bool collapse_forward_for_pin(hhds::Node_class& node, hhds::Pin_class new_dpin);

  bool try_constant_prop(hhds::Node_class& node, livehd::graph_util::Edge_vec& inp_edges_ordered);
  void try_collapse_forward(hhds::Node_class& node, livehd::graph_util::Edge_vec& inp_edges_ordered);

  void replace_part_inputs_const(hhds::Node_class& node, livehd::graph_util::Edge_vec& inp_edges_ordered);
  void replace_all_inputs_const(hhds::Node_class& node, livehd::graph_util::Edge_vec& inp_edges_ordered);
  void replace_node(hhds::Node_class& node, const Dlop& result);
  void replace_node(hhds::Node_class& node, const spool_ptr<Dlop>& result) { replace_node(node, *result); }
  void replace_logic_node(hhds::Node_class& node, const Dlop& result);
  void replace_logic_node(hhds::Node_class& node, const spool_ptr<Dlop>& result) { replace_logic_node(node, *result); }

  bool            scalar_mux(hhds::Node_class& node, livehd::graph_util::Edge_vec& inp_edges_ordered);
  void            scalar_sext(hhds::Node_class& node, livehd::graph_util::Edge_vec& inp_edges_ordered);
  // EQ(EQ(x,0),0) -> x / EQ(b,1) -> b boolean-chain folds. true = node deleted.
  bool            scalar_eq(hhds::Node_class& node, livehd::graph_util::Edge_vec& inp_edges_ordered);
  // Constant shift-of-shift composition (SRA/SHL chains). true = node rewired
  // in place (caller must re-read input edges).
  bool            scalar_shift(hhds::Node_class& node, livehd::graph_util::Edge_vec& inp_edges_ordered);
  // Or of constant-shifted copies of ONE 0/1 source ({N{bit}} replication) ->
  // Mux(bit, 0, mask). true = node deleted.
  bool            try_broadcast_or(hhds::Node_class& node, livehd::graph_util::Edge_vec& inp_edges_ordered);
  hhds::Pin_class try_find_single_driver_pin(hhds::Node_class& node, int64_t pos);
  bool            scalar_get_mask(hhds::Node_class& node);
  bool            scalar_set_mask(hhds::Node_class& node);
  // Constant slice of a packed (Or-of-shifted-disjoint-fields) wire -> the one
  // operand that drives it. true = node deleted (folded to a constant); false
  // may still have rewired the node in place.
  bool            scalar_get_mask_packed(hhds::Node_class& node, const Dlop& mask_const);

  void bwd_del_node(hhds::Node_class& node);

  // A latch already carries its write condition on `enable`.  Remove the
  // redundant hold arm from `din = enable ? data : Q` before the ordinary
  // scalar sweep, so downstream passes see the canonical `din = data` form.
  void canonicalize_latch_holds(hhds::Graph* g);

  // Retype And(x, 2^n-1) [binary, one const] into the value-identical
  // Get_mask(x, 2^n-1) so every low-mask truncation shares ONE shape.
  void canonicalize_and_masks(hhds::Graph* g);
  // Hash-cons identical pure combinational nodes (same op, same input pins).
  void cse_pass(hhds::Graph* g);
  void scalar_pass(hhds::Graph* g);

public:
  Cprop() = default;

  void do_trans(const std::shared_ptr<hhds::Graph>& g);
};

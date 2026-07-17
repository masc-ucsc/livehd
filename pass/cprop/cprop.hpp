//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "hhds/graph.hpp"
#include "hlop/dlop.hpp"
#include "node_util.hpp"  // graph:graph — livehd::graph_util::* helpers
#include "pass.hpp"

class Cprop {
private:
  static inline int trace_module_cnt = 0;

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
  hhds::Pin_class try_find_single_driver_pin(hhds::Node_class& node, int64_t pos);
  bool            scalar_get_mask(hhds::Node_class& node);
  // Constant slice of a packed (Or-of-shifted-disjoint-fields) wire -> the one
  // operand that drives it. true = node deleted (folded to a constant); false
  // may still have rewired the node in place.
  bool            scalar_get_mask_packed(hhds::Node_class& node, const Dlop& mask_const);

  void bwd_del_node(hhds::Node_class& node);

  void scalar_pass(hhds::Graph* g);

public:
  Cprop() = default;

  void do_trans(const std::shared_ptr<hhds::Graph>& g);
};

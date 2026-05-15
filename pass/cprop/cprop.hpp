//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "const.hpp"
#include "hhds/graph.hpp"
#include "node_util.hpp"  // graph:graph — livehd::graph_util::* helpers
#include "pass.hpp"

class Cprop {
private:
  bool              hier;
  static inline int trace_module_cnt = 0;

protected:
  hhds::Graph* current_graph = nullptr;

  void collapse_forward_same_op(hhds::Node_class& node, std::vector<hhds::Edge_class>& inp_edges_ordered);
  void collapse_forward_sum(hhds::Node_class& node, std::vector<hhds::Edge_class>& inp_edges_ordered);
  void collapse_forward_always_pin0(hhds::Node_class& node, std::vector<hhds::Edge_class>& inp_edges_ordered);
  void collapse_forward_for_pin(hhds::Node_class& node, hhds::Pin_class new_dpin);

  bool try_constant_prop(hhds::Node_class& node, std::vector<hhds::Edge_class>& inp_edges_ordered);
  void try_collapse_forward(hhds::Node_class& node, std::vector<hhds::Edge_class>& inp_edges_ordered);

  void replace_part_inputs_const(hhds::Node_class& node, std::vector<hhds::Edge_class>& inp_edges_ordered);
  void replace_all_inputs_const(hhds::Node_class& node, std::vector<hhds::Edge_class>& inp_edges_ordered);
  void replace_node(hhds::Node_class& node, const Const& result);
  void replace_node(hhds::Node_class& node, const spool_ptr<Dlop>& result) { replace_node(node, *result); }
  void replace_logic_node(hhds::Node_class& node, const Const& result);
  void replace_logic_node(hhds::Node_class& node, const spool_ptr<Dlop>& result) { replace_logic_node(node, *result); }

  bool            scalar_mux(hhds::Node_class& node, std::vector<hhds::Edge_class>& inp_edges_ordered);
  void            scalar_sext(hhds::Node_class& node, std::vector<hhds::Edge_class>& inp_edges_ordered);
  hhds::Pin_class try_find_single_driver_pin(hhds::Node_class& node, int64_t pos);
  bool            scalar_get_mask(hhds::Node_class& node);

  void bwd_del_node(hhds::Node_class& node);

  void scalar_pass(hhds::Graph* g);

public:
  Cprop(bool _hier);

  void do_trans(const std::shared_ptr<hhds::Graph>& g);
};

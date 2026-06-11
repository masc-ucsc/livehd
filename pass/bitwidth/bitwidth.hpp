//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <memory>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "bitwidth_range.hpp"
#include "hhds/graph.hpp"
#include "node_util.hpp"  // graph:graph — livehd::graph_util::* helpers
#include "pass.hpp"

class Bitwidth {
private:
  static inline int trace_module_cnt = 0;

protected:
  int  max_iterations;
  bool hier;
  bool discovered_some_backward_nodes_try_again;

  enum class Attr { Set_other, Set_ubits, Set_sbits, Set_max, Set_min, Set_dp_assign };

  static Attr get_key_attr(std::string_view key);

  bool                                                 not_finished;
  absl::flat_hash_map<hhds::Class_index, Bitwidth_range> bwmap;  // keyed by driver pin's class_index

  // Nodes added by attr_set processing (Get_mask insertions) that must be
  // re-visited after the forward iterator finishes the current pass.
  std::vector<hhds::Node_class> pending_added_nodes;

  hhds::Graph* current_graph = nullptr;

  void set_bw_1bit(hhds::Pin_class dpin);
  void set_bits_sign(hhds::Pin_class& dpin, const Bitwidth_range& bw);
  void adjust_bw(hhds::Pin_class dpin, const Bitwidth_range& bw);

  void process_const(hhds::Node_class& node);
  void process_not(hhds::Node_class& node, std::vector<hhds::Edge_class>& inp_edges);
  void process_flop(hhds::Node_class& node);
  void process_memory(hhds::Node_class& node);
  void process_mux(hhds::Node_class& node, std::vector<hhds::Edge_class>& inp_edges);
  void process_hotmux(hhds::Node_class& node, std::vector<hhds::Edge_class>& inp_edges);
  void process_sra(hhds::Node_class& node, std::vector<hhds::Edge_class>& inp_edges);
  void process_shl(hhds::Node_class& node, std::vector<hhds::Edge_class>& inp_edges);
  void process_sum(hhds::Node_class& node, std::vector<hhds::Edge_class>& inp_edges);
  void process_mult(hhds::Node_class& node, std::vector<hhds::Edge_class>& inp_edges);
  void process_get_mask(hhds::Node_class& node);
  void process_set_mask(hhds::Node_class& node);
  void process_sext(hhds::Node_class& node, std::vector<hhds::Edge_class>& inp_edges);
  void process_comparator(hhds::Node_class& node);
  void process_bit_or(hhds::Node_class& node, std::vector<hhds::Edge_class>& inp_edges);
  void process_bit_xor(hhds::Node_class& node, std::vector<hhds::Edge_class>& inp_edges);
  void process_bit_and(hhds::Node_class& node, std::vector<hhds::Edge_class>& inp_edges);
  void process_assignment_or(hhds::Node_class& node, std::vector<hhds::Edge_class>& inp_edges);
  void process_ror(hhds::Node_class& node, std::vector<hhds::Edge_class>& inp_edges);
  void process_attr_set_dp_assign(hhds::Node_class& node);
  void process_attr_set_bw(hhds::Node_class& node, Bitwidth::Attr attr);
  void process_attr_set(hhds::Node_class& node);
  void insert_tposs_nodes(hhds::Node_class& node_attr, int32_t ubits);

  void debug_unconstrained_msg(hhds::Node_class& node, hhds::Pin_class& d_dpin);
  void try_delete_attr_node(hhds::Node_class& node);
  void set_subgraph_boundary_bw(hhds::Node_class& node);
  void dump(hhds::Graph* g);

  // After the iteration budget is spent, warn (via diag) for every driver pin
  // whose bits/sign could not be bounded.
  void report_unbounded(hhds::Graph* g);

  void bw_pass(hhds::Graph* g);

public:
  Bitwidth(bool hier, int max_iterations);
  void do_trans(const std::shared_ptr<hhds::Graph>& g);
  bool is_finished() const { return !not_finished; }
};

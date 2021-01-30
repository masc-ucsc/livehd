//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "lconst.hpp"
#include "lgtuple.hpp"
#include "node.hpp"
#include "pass.hpp"

class Cprop {
private:
  bool hier;
  bool at_gioc;
  bool tuple_issues;
protected:

  absl::flat_hash_map<Node::Compact, std::shared_ptr<Lgtuple>> node2tuple;  // node to the most up-to-dated tuple chain
  absl::flat_hash_map<std::string_view, Node_pin> oname2dpin;

  void collapse_forward_same_op(Node &node, XEdge_iterator &inp_edges_ordered);
  void collapse_forward_sum(Node &node, XEdge_iterator &inp_edges_ordered);
  void collapse_forward_always_pin0(Node &node, XEdge_iterator &inp_edges_ordered);
  void collapse_forward_for_pin(Node &node, Node_pin &new_dpin);

  bool try_constant_prop(Node &node, XEdge_iterator &inp_edges_ordered);
  void try_collapse_forward(Node &node, XEdge_iterator &inp_edges_ordered);

  void replace_part_inputs_const(Node &node, XEdge_iterator &inp_edges_ordered);
  void replace_all_inputs_const(Node &node, XEdge_iterator &inp_edges_ordered);
  void replace_node(Node &node, const Lconst &result);
  void replace_logic_node(Node &node, const Lconst &result, const Lconst &result_reduced);

  void try_connect_tuple_to_sub(Node_pin &dollar_spin, std::shared_ptr<Lgtuple> tup, Node &sub_node, Node &tup_node);
  void try_connect_lgcpp(Node &node);
  void try_connect_sub_inputs(Node &node);

  void process_subgraph(Node &node);

  // Attributes method
  bool process_attr_get(Node &node);
  void process_attr_q_pin(Node &node, Node_pin &parent_dpin);


  // Tuple methods
  std::shared_ptr<Lgtuple const> find_lgtuple(Node_pin up_dpin);
  void process_tuple_add(Node &node);
  bool process_tuple_get(Node &node);
  void process_mux(Node &node);

  // io construction
  void try_create_graph_output(Node &node, std::shared_ptr<Lgtuple> tup);

  // Delete node and all the previous nodes feeding this one if single user
  void bwd_del_node(Node &node);

public:
  Cprop (bool _hier, bool _gioc);
  static std::tuple<std::string_view, std::string_view, int> get_tuple_name_key(Node &node);
  void dump_node2tuples() const;
  // Entry point
  void do_trans(LGraph *orig);
  bool has_tuple_issues() const { return tuple_issues; }
};

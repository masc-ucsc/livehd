//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <tuple>

#include "lconst.hpp"
#include "lgtuple.hpp"
#include "node.hpp"
#include "pass.hpp"

class Cprop {
private:
  bool hier;
  bool at_gioc;
  bool tuple_issues;

  inline static Node_pin invalid_pin; // just for speed

protected:
  absl::flat_hash_map<Node::Compact, std::shared_ptr<Lgtuple const>> node2tuple;  // node to the most up-to-dated tuple chain
  absl::flat_hash_map<std::string_view, Node_pin>                    oname2dpin;
  absl::flat_hash_map<std::string, Node_pin>                         reg_name2qpin;
  absl::flat_hash_map<std::string, std::pair<std::string, Node_pin>> reg_attr_map;
  absl::flat_hash_map<std::string, std::vector<Node_pin>>            reg_name2sink_pins;
  absl::flat_hash_set<Node::Compact>                                 dont_touch;

  std::tuple<Node_pin, std::shared_ptr<Lgtuple const> > get_value(const Node &node) const;

  void collapse_forward_same_op(Node &node, XEdge_iterator &inp_edges_ordered);
  void collapse_forward_sum(Node &node, XEdge_iterator &inp_edges_ordered);
  void collapse_forward_always_pin0(Node &node, XEdge_iterator &inp_edges_ordered);
  void collapse_forward_for_pin(Node &node, Node_pin &new_dpin);

  bool try_constant_prop(Node &node, XEdge_iterator &inp_edges_ordered);
  void try_collapse_forward(Node &node, XEdge_iterator &inp_edges_ordered);

  void replace_part_inputs_const(Node &node, XEdge_iterator &inp_edges_ordered);
  void replace_all_inputs_const(Node &node, XEdge_iterator &inp_edges_ordered);
  void replace_node(Node &node, const Lconst &result);
  void replace_logic_node(Node &node, const Lconst &result);

  void try_connect_tuple_to_sub(Node_pin &dollar_spin, std::shared_ptr<Lgtuple const> tup, Node &sub_node, Node &tup_node);
  void try_connect_lgcpp(Node &node);
  void try_connect_sub_inputs(Node &node);

  void process_subgraph(Node &node, XEdge_iterator &inp_edges_ordered);

  // Tuple methods
  std::shared_ptr<Lgtuple const> find_lgtuple(Node_pin up_dpin) const;
  std::shared_ptr<Lgtuple const> find_lgtuple(Node up_node) const;

  void  process_attr_get(Node &node);
  void  process_attr_set(Node &node);
  void  process_tuple_add(Node &node);
  Node_pin  expand_data_and_attributes(Node &node, const std::string &key_name, XEdge_iterator &pending_out_edges, std::shared_ptr<Lgtuple const> node_tup);
  bool  process_tuple_get(Node &node);

  bool  process_mux(Node &node, XEdge_iterator &inp_edges_ordered);
  void  process_sext(Node &node, XEdge_iterator &inp_edges_ordered);

  // io construction
  void try_create_graph_output(Node &node, std::shared_ptr<Lgtuple> tup);

  // Delete node and all the previous nodes feeding this one if single user
  void bwd_del_node(Node &node);

  void process_flop(Node &node);

  std::tuple<std::string, std::string> get_tuple_name_key(Node &node) const;

public:
  Cprop(bool _hier, bool _gioc);

  void dump_node2tuples() const;

  void do_trans(Lgraph *orig);
  bool has_tuple_issues() const { return tuple_issues; }
};

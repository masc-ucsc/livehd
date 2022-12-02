//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <tuple>

#include "lconst.hpp"
#include "lgtuple.hpp"
#include "node.hpp"
#include "pass.hpp"

class Cprop {
private:
  bool              hier;
  bool              tuple_issues;
  bool              tuple_found;  // set during scalar_pass
  static inline int trace_module_cnt = 0;

  inline static Node_pin invalid_pin;  // just for speed

  void connect_clock_pin_if_needed(Node &node);
  void connect_reset_pin_if_needed(Node &node);

protected:
  absl::flat_hash_map<Node::Compact, std::shared_ptr<Lgtuple const>> node2tuple;  // node to the most up-to-dated tuple chain
  absl::flat_hash_set<Node::Compact>                                 tuple_done;

  std::tuple<Node_pin, std::shared_ptr<Lgtuple const>> get_value(const Node &node) const;

  void add_pin_with_check(const std::shared_ptr<Lgtuple> &tup, std::string_view key, Node_pin &pin);

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

  void try_connect_tuple_to_sub(const std::shared_ptr<Lgtuple const> &tup, Node &sub_node, Node &tup_node);
  void try_connect_lgcpp(const Node &node);

  // Tuple methods
  std::shared_ptr<Lgtuple const> find_lgtuple(const Node_pin &up_dpin) const;
  std::shared_ptr<Lgtuple const> find_lgtuple(const Node &up_node) const;

  void reconnect_memory_port(Node &node, size_t n_ports, std::string_view field, std::shared_ptr<Lgtuple const> tup);
  void reconnect_memory_port_const(Node &node, std::string_view field, std::shared_ptr<Lgtuple const> tup);
  void reconnect_memory(Node &node, std::shared_ptr<Lgtuple const> tup);
  void reconnect_sub_as_cell(Node &node, Ntype_op cell_ntype);
  void reconnect_tuple_sub(Node &node);
  void reconnect_tuple_add(Node &node);
  void reconnect_tuple_get(Node &node);
  bool handle_runtime_index(Node &tg_node, const Node &field_node, const std::shared_ptr<Lgtuple const> &parent_tup);
  bool is_runtime_index_case(const std::shared_ptr<Lgtuple const> &node_tup);

  Node_pin expand_data_and_attributes(Node &node, std::string_view key_name, XEdge_iterator &pending_out_edges,
                                      const std::shared_ptr<Lgtuple const> &node_tup);

  // handle tuple issues but allowed to "mutate" the node
  void tuple_shl_mut(Node &node);
  void tuple_mux_mut(Node &node);
  void tuple_flop_mut(Node &node);
  void tuple_get_mask_mut(Node &node);

  // handle tuple issues but not allowed to "mutate"
  void tuple_subgraph(const Node &node);
  void tuple_tuple_add(const Node &node);
  bool tuple_tuple_get(Node &node);
  void tuple_attr_set(const Node &node);

  bool     scalar_mux(Node &node, XEdge_iterator &inp_edges_ordered);
  void     scalar_sext(Node &node, XEdge_iterator &inp_edges_ordered);
  Node_pin try_find_single_driver_pin(Node &node, int pos);
  bool     scalar_get_mask(Node &node);

  // io construction
  void try_create_graph_output(Node &node, const std::shared_ptr<Lgtuple const> &tup);

  // Delete node and all the previous nodes feeding this one if single user
  void bwd_del_node(Node &node);

  std::tuple<std::string, std::string> get_tuple_name_key(const Node &node) const;

  void scalar_pass(Lgraph *orig);
  void tuple_pass(Lgraph *orig);
  void clean_io(Lgraph *orig);

public:
  Cprop(bool _hier);

  void dump_node2tuples() const;

  void do_trans(Lgraph *orig);
  bool has_tuple_issues() const { return tuple_issues; }
};

//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#pragma once

#include "lconst.hpp"
#include "node.hpp"
#include "pass.hpp"
#include "lgtuple.hpp"

class Pass_cprop : public Pass {
private:
protected:
  absl::flat_hash_map<Node::Compact, std::shared_ptr<Lgtuple>> node2tuple; //node to most up-to-dated tuple chain

  static void optimize(Eprp_var &var);
  void collapse_forward_same_op(Node &node, XEdge_iterator &inp_edges_ordered);
  void collapse_forward_sum(Node &node, XEdge_iterator &inp_edges_ordered);
  void collapse_forward_always_pin0(Node &node, XEdge_iterator &inp_edges_ordered);
  void collapse_forward_for_pin(Node &node, Node_pin &new_dpin);
  void try_collapse_forward(Node &node, XEdge_iterator &inp_edges_ordered);
  void replace_part_inputs_const(Node &node, XEdge_iterator &inp_edges_ordered);
  void replace_all_inputs_const(Node &node, XEdge_iterator &inp_edges_ordered);
  void replace_node(Node &node, const Lconst &result);
	void replace_logic_node(Node &node, const Lconst &result, const Lconst &result_reduced);

  void process_subgraph(Node &node);

	void process_tuple_q_pin(Node &node, Node_pin &parent_dpin);
  void merge_to_tuple(std::shared_ptr<Lgtuple> tup, Node &node, Node
      &parent_node, Node_pin &parent_dpin, int key_pos, std::string_view
      key_name, Node_pin &val_dpin);
  bool process_tuples(Node &node, XEdge_iterator &inp_edges_ordered);
  void trans(LGraph *orig);

public:
  Pass_cprop(const Eprp_var &var);

  static void setup();
};


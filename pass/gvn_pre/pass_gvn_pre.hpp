//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#ifndef PASS_gvn_pre_H
#define PASS_gvn_pre_H

#include "options.hpp"
#include "pass.hpp"

#include <string>

class Pass_gvn_pre_options_pack : public Options_pack {
public:
};

class Node_pin_Plus : public Node_pin {
public:
  Node_pin_Plus(Index_ID _nid, Port_ID _pid, bool _input)
      : Node_pin(_nid, _pid, _input) {
  }

  Node_pin_Plus(Node_pin _node_pin)
      : Node_pin(_node_pin.get_nid(), _node_pin.get_pid(), false) {
  }

  Node_pin_Plus()
      : Node_pin(-1, -1, false) {
  }

  bool operator<(const Node_pin &rhs) const {
    if(this->get_nid() < rhs.get_nid()) {
      return 1;
    } else if(this->get_nid() > rhs.get_nid())
      return 0;
    else { // equal
      if(this->get_pid() < rhs.get_pid())
        return 1;
      else
        return 0;
    }
  }
  bool operator==(const Node_pin &rhs) const {
    if(this->get_nid() != rhs.get_nid()) {
      return 0;
    } else { // equal nid
      return (this->get_pid() == rhs.get_pid());
    }
  }

  void print_info() {
    fmt::print("      nid is {}, pid is {}, input is {}\n", get_nid(), get_pid(), input);
    return;
  }
};

typedef std::vector<Node_pin_Plus> Node_pin_Vec;
class Expression_Node {
public:
  enum task_enum { build_sets_enum, insertion_enum, elimination_enum };
  Node_Type_Op node_type_op;
  //  Node_pin_Plus node_pin;
  Node_pin_Vec node_pin_vec;
  Expression_Node(
      // Node_pin_Plus _node_pin= Node_pin_Plus(-1, -1, false),
      Node_Type_Op _node_type_op = Invalid_Op, Node_pin_Vec _node_pin_vec = Node_pin_Vec())
      : node_type_op(_node_type_op)
      , node_pin_vec(_node_pin_vec)
  //    ,node_pin(_node_pin)
  {
    for(auto &node_pin : node_pin_vec) {
      node_pin.reset_as_output();
    }
  }
  bool operator<(const Expression_Node &rhs) const {
    if(this->node_type_op == rhs.node_type_op) {
      if(this->node_pin_vec.size() == rhs.node_pin_vec.size()) {
        int i = 0;
        for(const auto &node_pin : node_pin_vec) {
          if(node_pin == rhs.node_pin_vec[i]) {
            // continue search
          } else
            return (node_pin < rhs.node_pin_vec[i]);
          i++;
        }
        return 0; // because they are equal
      } else
        return (this->node_pin_vec.size() < rhs.node_pin_vec.size());
    } else
      return (this->node_type_op < rhs.node_type_op);
  }

  void print_info() {
    fmt::print("  For this enode: node type op is {} \n", node_type_op);
    fmt::print("    Printing node pins: \n");
    for(auto &node_pin : node_pin_vec) {
      node_pin.print_info();
    }
  }
};
typedef std::map<Expression_Node, Node_pin_Plus>
    ExpLeaderMap; // Expression leader map; key:expression is led by leader:operand(has specific operation type)
typedef std::map<Node_pin_Plus, Node_pin_Plus>
    OpLeaderMap; // Operand leader map; key:(invalid type) operand is led by leader:operand(has specific operation type)
class Pass_gvn_pre : public Pass {
private:
protected:
  std::string gvn_pre_type;

  Pass_gvn_pre_options_pack opack;

public:
  Pass_gvn_pre();
  ExpLeaderMap                              exp_leader_map;
  OpLeaderMap                               op_leader_map;
  typedef std::map<Index_ID, Node_pin_Plus> Index_Replace_Map;
  Index_Replace_Map                         index_id_to_replace;

  void          build_sets(LGraph *g);
  void          insertion(LGraph *g);
  void          elimination(LGraph *g);
  void          result_graph(LGraph *g);
  void          transform(LGraph *orig) final;
  void          traverse(LGraph *g, int round);
  Node_pin_Plus lookup_op_leader(Node_pin_Plus new_opnode);
  Node_pin_Plus lookup_exp_leader(Index_ID idx, Expression_Node new_exp_enode);
};

#endif

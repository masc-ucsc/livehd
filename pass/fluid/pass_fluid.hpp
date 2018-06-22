//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#ifndef PASS_fluid_H
#define PASS_fluid_H

// FIXME: names are not to standard (no camel-case)

#include "options.hpp"
#include "pass.hpp"

#include <string>

class Pass_fluid_options_pack : public Options_pack {
public:
};

class Node_Pin_P : public Node_Pin {
public:
  Node_Pin_P(Index_ID _nid, Port_ID _pid, bool _input)
      : Node_Pin(_nid, _pid, _input) {
  }

  Node_Pin_P(Node_Pin _node_pin)
      : Node_Pin(_node_pin.get_nid(), _node_pin.get_pid(), _node_pin.is_input()) {
  }

  Node_Pin_P()
      : Node_Pin(-1, -1, false) {
  }

  bool operator<(const Node_Pin &rhs) const {
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
  bool operator==(const Node_Pin &rhs) const {
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
typedef std::vector<Node_Pin_P> Node_Pin_Vec;
typedef std::vector<Index_ID>   Flop_Indx_Vec;

typedef std::map<Index_ID, Flop_Indx_Vec> IndexFlopMap;    // Each Key is a node idx which locates the accumulated Flops pass through that node idx
typedef std::map<Index_ID, bool>          IndexHasFlopMap; // Each Key is a node idx which indicates whether there is any accumulated Flops pass through that node idx
typedef std::vector<Index_ID>             IOFlopVec;       // Each Key is a node idx which indicates whether the node is a flop
typedef std::map<Index_ID, Index_ID>      FlopToVqMap;     // Each Key is a node idx that locates the node id which contains its vq or sin

class Pass_fluid : public Pass {
private:
protected:
  std::string fluid_type;

  Pass_fluid_options_pack opack;

public:
  Pass_fluid();
  IndexFlopMap    join_index_flop_map;
  IndexHasFlopMap join_index_has_flop_map;
  IOFlopVec       in_flop_vec;
  IOFlopVec       out_flop_vec;
  IndexFlopMap    fork_index_flop_map;
  IndexHasFlopMap fork_index_has_flop_map;
  FlopToVqMap     flop_to_vq_map;
  FlopToVqMap     flop_to_sin_map;

  // clk=0, din=1, vdin=2, sout=3, rst=4, rst_value=5
  // q=0, vq=1, sin=2.
  // Left: <-sin
  // Rigt: sout<-
  Port_ID ffvin  = 2;
  Port_ID ffsout = 3;
  Port_ID ffvq   = 1;
  Port_ID ffsin  = 2;

  void find_join(LGraph *g);
  void find_fork(LGraph *g);
  void add_fork_deadlock(LGraph *g);
  void add_fork(LGraph *g);
  void add_join_deadlock(LGraph *g);
  void add_join(LGraph *g);
  void result_graph(LGraph *g);
  void transform(LGraph *orig) final;
  void traverse(LGraph *g, int round);
};

#endif

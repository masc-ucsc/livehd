//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "lgedgeiter.hpp"
#include "lgbench.hpp"
#include "lgraph.hpp"

#include "pass_bitwidth.hpp"

#include <string>
#include <vector>
#include <algorithm>
#include <math.h>

Pass_bitwidth::Pass_bitwidth() {
}


//NOTE:  This class stores information for each node.
//         Needs to be readjusted so that it focuses
//         on outputs of nodes and not nodes(?).
//FIXME: Need to change for Implicit/Explicit discussion.
class node_properties {
public://Change this later. For now makes accessing stuff easier.
  bool used;//Indicates whether this node has been touched yet or not.
  bool is_signed;
  int calculated_max, calculated_min;
  int hardset_max, hardset_min;
  bool is_hardset_max, is_hardset_min;
  bool drop;//for dropping bits; ignore for now, told to wait for Sheng
//public:
  node_properties();
};

node_properties::node_properties () {
  used = false;
  is_signed = false;
  calculated_max = 0;
  calculated_min = 0;
  hardset_max = 0;
  hardset_min = 0;
  is_hardset_max = false;
  is_hardset_min = false;
  drop = false;
}


//------------------------------------------------------------------
//Helper Functions (Forward Traversal)
//NOTE: These functions give ranges (<min,max>) during the initial
//        forward traversal, wherever possible.

void to_graphio(const LGraph *g, Index_ID idx, node_properties arr[50]) {
  fmt::print("I found an IO");
  if(g->is_graph_input(idx)) {
    fmt::print(" (${})", g->get_graph_input_name(idx));
  } else if (g->is_graph_output(idx)) {
    fmt::print(" (%{})", g->get_graph_output_name(idx));
  }

  int size = g->get_bits(idx);
  arr[idx].calculated_min = 0;
  arr[idx].calculated_max = pow(2,size) - 1;
  arr[idx].used = true;
}

void to_and(const LGraph *g, Index_ID idx, node_properties arr[50]) {
  int input_node = 0;
  fmt::print("I found an AND ... Inp. Nodes:");
  int size[2];
  int tmp = 0;
  for(const auto &p : g->inp_edges(idx)) {
    input_node = p.get_idx();
    fmt::print(" {}/b{}", input_node, g->get_bits(input_node));
    size[tmp] = g->get_bits(input_node);
    tmp++;
  }

  arr[idx].calculated_min = 0;
  arr[idx].calculated_max = pow(2,g->get_bits(idx)) - 1;
  arr[idx].used = true;

  /*if(size[0] == size[1]) {
    fmt::print(" ...SAME");
  } else if (size[0] > size[1]) {
    fmt::print(" ...DIFF: reduce size of first node");
  } else {
    fmt::print(" ...DIFF: reduce size of second node");
  }*/
}

void to_sum(const LGraph *g, Index_ID idx, node_properties arr[50]) {
  fmt::print("I found a SUM ... Inp. Nodes:");
  int n_idx[2];
  int tmp = 0;
  for(const auto &p : g->inp_edges(idx)) {
    n_idx[tmp] = p.get_idx();
    fmt::print(" {}/b{}", n_idx[tmp], g->get_bits(p.get_idx()));
    tmp++;
  }

  //Set SUM node's min/max values.
  arr[idx].calculated_max = arr[n_idx[0]].calculated_max + arr[n_idx[1]].calculated_max;
  arr[idx].calculated_min = arr[n_idx[0]].calculated_min + arr[n_idx[1]].calculated_min;
  arr[idx].used = true;
  //fmt::print("\tNode min: {}, max: {}", arr[idx].calculated_min, arr[idx].calculated_max);
}

void to_pick(const LGraph *g, Index_ID idx, node_properties arr[50]) {
  //FIXME: This is incorrect, but it works for now.
  fmt::print("I found a PICK ... Inp. Nodes:");
  int nodes[2];
  int tmp = 0;
  for(const auto &p : g->inp_edges(idx)) {
    nodes[tmp] = p.get_idx();
    fmt::print(" {}/b{}", p.get_idx(), g->get_bits(p.get_idx()));
    tmp++;
  }

  arr[idx].calculated_min = 0;
  arr[idx].calculated_max = arr[nodes[0]].calculated_max;
  arr[idx].used = true;
  //fmt::print("\tNode min: {}, max: {}", arr[idx].calculated_min, arr[idx].calculated_max);
}

void to_const(const LGraph *g, Index_ID idx, node_properties arr[50]) {
  //Constant value's min/max values are just what it's assigned to. Cannot vary.
  int val = g->node_value_get(idx);
  fmt::print("I found a U32CONST ({})", val);

  arr[idx].calculated_min = val;
  arr[idx].calculated_max = val;
  arr[idx].used = true;
}

void to_not(const LGraph *g, Index_ID idx, node_properties arr[50]) {
  //FIXME: This is wrong and needs to change. Will depend on signage. For now, fine.
  fmt::print("I found a NOT");
  int inp_idx;
  for(const auto &p : g->inp_edges(idx)) {
    inp_idx = p.get_idx();
    fmt::print(" {}/b{} ... Inp. Node:", inp_idx, g->get_bits(p.get_idx()));
  }
  arr[idx].calculated_max = arr[inp_idx].calculated_max;
  arr[idx].calculated_min = arr[inp_idx].calculated_min;
  arr[idx].used = true;
  //fmt::print("... {} ... {}", arr[idx].calculated_min, arr[idx].calculated_max);
}

//------------------------------------------------------------------
//MIT Algorithm


void Pass_bitwidth::trans(LGraph *orig) {
  LGBench b;

  fmt::print ("\n");
  //std::vector<node_properties> testVector;
  node_properties arr[50];//The size of this is arbitrary. We use 50 for now so we don't have to worry about size for trivial cases. Elements are indexed by their node #.


  //First forward pass.
  for(auto idx : orig->forward()) {
    const auto op = orig->node_type_get(idx);
    fmt::print("Node {}:\t\t", idx);

    switch(op.op) {
      case GraphIO_Op:
         to_graphio(orig, idx, arr);
        break;
      case And_Op:
        to_and(orig, idx, arr);
        break;
      case Or_Op:
        fmt::print("I found an OR");
        break;
      case Xor_Op:
        fmt::print("I found an XOR");
        break;
      case LessThan_Op:
        fmt::print("I found a L.T.");
        break;
      case GreaterThan_Op:
        fmt::print("I found a G.T.");
        break;
      case LessEqualThan_Op:
        fmt::print("I found a L.E.T.");
        break;
      case GreaterEqualThan_Op:
        fmt::print("I found a G.E.T.");
        break;
      case Equals_Op:
        fmt::print("I found an EQUALS");
        break;
      case Not_Op:
        to_not(orig, idx, arr);
        break;
      case ShiftLeft_Op:
        fmt::print("I found a SHIFT_LEFT");
        break;
      case ShiftRight_Op:
        fmt::print("I found a SHIFT_RIGHT");
        break;
      case Mux_Op:
        fmt::print("I found a MUX");
        break;
      case Sum_Op:
        to_sum(orig, idx, arr);
        break;
      case Join_Op:
        fmt::print("I found a JOIN");
        break;
      case Flop_Op:
        fmt::print("I found a FLOP");
        break;
      case SubGraph_Op:
        fmt::print("I found a SUBGRAPH");
        break;
      case U32Const_Op:
        to_const(orig, idx, arr);
        break;
      case Pick_Op:
        to_pick(orig, idx, arr);
        break;
      case Latch_Op:
        fmt::print("I found a LATCH");
        break;
      case StrConst_Op:
        fmt::print("I found a STRING");
        break;
      default:
        fmt::print("FIXME! op not found");
    }

    fmt::print("\n");
  }

  //Print table after first forward pass.
  fmt::print("\nTable after forward traversal:\n\tN_ID \t C_MIN \t C_MAX\n");
  for(int i = 1; i < 50; i++) {
    if(arr[i].used == true)
      fmt::print("\t{} \t {} \t {}\n", i, arr[i].calculated_min, arr[i].calculated_max);
  }
  //fmt::print("{} \t {} \t {}\n", 12, arr[12].calculated_min, arr[12].calculated_max);
  fmt::print("\n\n");


  //Start doing backwards pass.
  for(auto b_idx : orig->backward()) {
    fmt::print("Node {}:\t\t", b_idx);

    const auto op = orig->node_type_get(b_idx);
    //These if-else will be refactored into subroutines later. For now, keep
    //  like this for ease.
    if (op.op == Sum_Op) {
      fmt::print("I found a SUM!");
      int input[2];
      int temp = 0;
      int output;
      for(const auto &i : orig->inp_edges(b_idx)) {
        fmt::print(" {}i", i.get_idx());
        input[temp] = i.get_idx();
        temp++;
      }
      for(const auto &o : orig->out_edges(b_idx)) {
        fmt::print(" {}o", o.get_idx());
        output = o.get_idx();
      }
      //----------------
      int min_i1 = arr[input[0]].calculated_min;
      int min_i2 = arr[input[1]].calculated_min;
      int min_o  = arr[output].calculated_min;
      int max_i1 = arr[input[0]].calculated_max;
      int max_i2 = arr[input[1]].calculated_max;
      int max_o  = arr[output].calculated_max;

      //fmt::print("\t{},{},{}, {} . {} . {} . {} . {} . {}",input[0],input[1],output, min_i1, min_i2, min_o, max_i1, max_i2, max_o);


      //Readjusting min/max of inputs/outputs, if need be.
      arr[output].calculated_max =   std::min(max_o, max_i1 + max_i2);
      arr[output].calculated_min =   std::max(min_o, min_i1 + min_i2);
      arr[input[0]].calculated_max = std::min(max_i1, max_o - min_i2);
      arr[input[0]].calculated_min = std::max(min_i1, min_o - max_i2);
      arr[input[1]].calculated_max = std::min(max_i2, max_o - min_i1);
      arr[input[1]].calculated_min = std::max(min_i2, min_o - max_i1);
      //----------------
    } else if (op.op == And_Op) {
      fmt::print("I found an AND!");
      int input[2];
      int temp = 0;
      int output;
      for(const auto &i : orig->inp_edges(b_idx)) {
        fmt::print(" {}i", i.get_idx());
        input[temp] = i.get_idx();
        temp++;
      }
      for(const auto &o : orig->out_edges(b_idx)) {
        fmt::print(" {}o", o.get_idx());
        output = o.get_idx();
      }
      ///---------------
      int min_o  = arr[output].calculated_min;
      int max_o  = arr[output].calculated_max;

      int min_inp_bitwidth = std::min(orig->get_bits(input[0]), orig->get_bits(input[1]));
      if(arr[output].is_signed == false) {
        fmt::print("\tNot signed");
        //Range is <min_o, max_o> INTERSECT <0, 2^n - 1>
        arr[output].calculated_min = std::max(min_o, 0);
        arr[output].calculated_max = std::min(max_o, (int)(pow(2,min_inp_bitwidth))-1);
      } else {
        fmt::print("\tIs signed");
        //Range is <min_o, max_o> INTERSECT <-2^(n-1), 2^(n-1) - 1>
        arr[output].calculated_min = std::max(min_o, (int)(pow(-2,min_inp_bitwidth-1)));
        arr[output].calculated_max = std::min(max_o, (int)(pow(2,min_inp_bitwidth-1))-1);
      }
      //fmt::print("\t{} ... {}", arr[output].calculated_min, arr[output].calculated_max);
    } else if (op.op == Pick_Op) {
      //This will need to change. For now just pass <min,max> of PICK to its outputs.
      fmt::print("I found an PICK!");
      int output;
      for(const auto &o : orig->out_edges(b_idx)) {
        fmt::print(" {}o", o.get_idx());
        output = o.get_idx();
      }
      arr[output].calculated_min = arr[b_idx].calculated_min;
      arr[output].calculated_max = arr[b_idx].calculated_max;

    }
    fmt::print("\n");
  }

  //Table after first backwards pass.
  fmt::print("\nTable after backwards traversal:\n\tN_ID \t C_MIN \t C_MAX\n");
  for(int i = 1; i < 50; i++) {
    if(arr[i].used == true)
      fmt::print("\t{} \t {} \t {}\n", i, arr[i].calculated_min, arr[i].calculated_max);
  }
  fmt::print("\n");


  b.sample("pass_bitwidth");
}

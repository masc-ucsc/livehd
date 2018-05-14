
#include <string>
#include <time.h>

#include "lgedgeiter.hpp"
#include "lgraph.hpp"
#include "pass_gvn_pre.hpp"

Pass_gvn_pre::Pass_gvn_pre()
    : Pass("gvn_pre") {
}

// OPs that should be processed differently:
// IO
// Join: if there is only one operand, it can be removed; or we can add the result enode to be led by the right hand operand
// XOR: Need to lookup/create: ~a & b | a & ~b
// MUX: complicated...
Node_Pin_Plus Pass_gvn_pre::lookup_op_leader(Node_Pin_Plus new_opnode) {
  Node_Pin_Plus leader_opnode = Node_Pin_Plus(-1, -1, false);
  // lookup leader in op_leader_map for the operand which has Invalid_Op
  if(op_leader_map.find(new_opnode) != op_leader_map.end()) {
    leader_opnode = op_leader_map[new_opnode];
  } else {
    fmt::print("Error: No existing leader for operand.\n");
    assert(0);
    leader_opnode = new_opnode;
  }
  return (leader_opnode);
}

Node_Pin_Plus Pass_gvn_pre::lookup_exp_leader(Index_ID idx, Expression_Node new_exp_enode) {
  Node_Pin_Plus result_opnode = Node_Pin_Plus(idx, 0, false);
  // lookup expression in exp_leader_map
  if(exp_leader_map.count(new_exp_enode)) {
    // add the left hand result operand to OpLeaderMap; it's led by the found leader; no new expression added
    op_leader_map[result_opnode] = exp_leader_map[new_exp_enode];
    result_opnode                = exp_leader_map[new_exp_enode];
  } else {
    // the result becomes leader
    // add both expression and result to maps; they all point to the result enode
    // add expression to expression map
    exp_leader_map[new_exp_enode] = result_opnode;
    op_leader_map[result_opnode]  = result_opnode;
  }
  return (result_opnode);
}

void Pass_gvn_pre::build_sets(LGraph *g) {
  for(auto idx : g->forward()) {
    switch(g->node_type_get(idx).op) {
    // combining simple Commutative ops:
    case And_Op:
    case Or_Op:
    case Xor_Op: // there is no efficient way to store the sum of all products of an Xor, so treat it as a simple commutative op
    {
      bool         unsuported = false;
      Node_Pin_Vec new_exp_vec;
      for(const auto &in_edge : g->inp_edges(idx)) {
        // for each operand
        Node_Pin_Plus new_opnode = in_edge.get_out_pin();
        if(op_leader_map.find(new_opnode) == op_leader_map.end()) {
          unsuported = true;
          break;
        }
        Node_Pin_Plus leader_opnode = lookup_op_leader(new_opnode);
        // add operand leader
        new_exp_vec.push_back(leader_opnode);
      }
      if(!unsuported) {
        // full expression
        // Sort vec to achieve the Commutative property
        sort(new_exp_vec.begin(), new_exp_vec.end());
        Expression_Node new_exp_enode(g->node_type_get(idx).op, new_exp_vec);
        Node_Pin_Plus   result_opnode          = lookup_exp_leader(idx, new_exp_enode);
        Node_Pin_Plus   original_result_opnode = Node_Pin_Plus(idx, 0, false);
        if(result_opnode != original_result_opnode) {
          index_id_to_replace[idx] = result_opnode;
        }
      }
      break;
    }
    // Join: assert 1 operand
    case Join_Op: {
      Node_Pin_Plus result_opnode = Node_Pin_Plus(idx, 0, false);
      int           i             = 1;
      bool          unsuported    = false;
      Node_Pin_Plus leader_opnode(1, 0, false);
      for(const auto &in_edge : g->inp_edges(idx)) {
        if(i > 1) {
          unsuported = true;
          break;
        } else {
          // for each operand
          Node_Pin_Plus new_opnode = in_edge.get_out_pin();
          if(op_leader_map.find(new_opnode) == op_leader_map.end()) {
            unsuported = true;
            break;
          }
          leader_opnode = lookup_op_leader(new_opnode);
        }
        i++;
      }
      if(!unsuported) {
        // map result node to the leader node too
        op_leader_map[result_opnode] = leader_opnode;
        index_id_to_replace[idx]     = leader_opnode;
      }
      // for Join skip the expression lookup
      break;
    }

    // Not: assert 1 operand
    case Not_Op: {
      Node_Pin_Vec new_exp_vec;
      int          i = 1;
      for(const auto &in_edge : g->inp_edges(idx)) {
        if(i > 1) {
          console->error("Error: This Not Op node has more than 1 input.\n");
          assert(0);
        } else {
          // for each operand
          Node_Pin_Plus new_opnode    = in_edge.get_out_pin();
          Node_Pin_Plus leader_opnode = lookup_op_leader(new_opnode);
          // add operand leader
          new_exp_vec.push_back(leader_opnode);
        }
        i++;
      }
      // full expression
      // skip sort vec for Not Op
      Expression_Node new_exp_enode(g->node_type_get(idx).op, new_exp_vec);
      Node_Pin_Plus   result_opnode          = lookup_exp_leader(idx, new_exp_enode);
      Node_Pin_Plus   original_result_opnode = Node_Pin_Plus(idx, 0, false);
      if(result_opnode != original_result_opnode) {
        index_id_to_replace[idx] = result_opnode;
      }
      break;
    }

    // IO
    case GraphIO_Op: {
      fmt::print("GraphIO_Op.\n");
      Node_Pin_Plus new_pin(idx, 0, false);
      /*
        // getting output node pin (especially pid)
        for(const auto &out_edge:g->out_edges(idx)) {
          // weird behavior here
          // TODO: ask why out_edge.get_inp_pin() cannot get the correct nid of this IO node
          Node_Pin_Plus new_pin_for_pid = out_edge.get_inp_pin();
          new_pin = Node_Pin_Plus(idx, new_pin_for_pid.get_pid(), false);
          assert(idx > 0);
          assert(new_pin.get_nid() > 0);
          // one edge is enough to get the info
          break;
        }
        */
      // make exception for IO, make the op type as invalid for general
      //Expression_Node new_enode(g->node_type_get(idx).op, new_pin_vec);
      if(op_leader_map.count(new_pin)) {
        console->error("Error: this IO was enmaped.\n");
        new_pin.print_info();
        assert(0);
      } else {
        op_leader_map[new_pin] = new_pin;
        assert(new_pin.get_nid() > 0);
      }
    } break;

    case Invalid_Op:
    case Sum_Op:
    case Mult_Op:
    case Flop_Op:
    case LessThan_Op:
    case GreaterThan_Op:
    case Equals_Op:
    case Mux_Op:
    case SubGraph_Op:
    case U32Const_Op:
    case StrConst_Op:
      break;
    default:
      console->warn("Unsuported OP {}.\n", g->node_type_get(idx).get_name());
    }
  }
}

void Pass_gvn_pre::insertion(LGraph *g) {
  for(auto idx : g->forward()) {
    if(index_id_to_replace.count(idx)) {
      // this node should be replaced
      // Insert new edges
      Node_Pin_Plus leader_node = index_id_to_replace[idx];
      leader_node.reset_as_output();
      for(const auto &out_edge : g->out_edges(idx)) {
        Node_Pin src_np = Node_Pin(leader_node.get_nid(), leader_node.get_pid(), false);
        Node_Pin dst_np = Node_Pin(out_edge.get_idx(), out_edge.get_out_pin().get_pid(), true);
        g->add_edge(src_np, dst_np);
      }
    }
  }
}

void Pass_gvn_pre::elimination(LGraph *g) {
  for(auto idx : g->forward()) {
    if(index_id_to_replace.count(idx)) {
      // this node should be replaced
      // Delete all edges
      bool deleted;
      do {
        deleted = false;
        for(const auto &out_edge : g->out_edges(idx)) {
          g->del_edge(out_edge);
          deleted = true;
        }
      } while(deleted);

      // Delete all inp edges
      do {
        deleted = false;
        for(const auto &inp_edge : g->inp_edges(idx)) {
          g->del_edge(inp_edge);
          deleted = true;
        }
      } while(deleted);
    }
  }
}

void Pass_gvn_pre::result_graph(LGraph *g) {
  fmt::print("\nResult graph: \n");
  for(auto idx : g->forward()) {
    fmt::print("\n*****Visiting idx:{}\n", idx);
    for(const auto &out_edge : g->out_edges(idx)) {
      fmt::print("Printing out edge: \n");
      fmt::print("nid{} pid{} \n", out_edge.get_idx(), out_edge.get_inp_pin().get_pid());
    }
    for(const auto &inp_edge : g->inp_edges(idx)) {
      fmt::print("Printting in edge: \n");
      fmt::print("nid{} pid{} \n", inp_edge.get_idx(), inp_edge.get_out_pin().get_pid());
    }
  }
}

void Pass_gvn_pre::traverse(LGraph *g, int round) {
  build_sets(g);
  insertion(g);
  elimination(g);
  g->sync();
  //result_graph(g);
}
void Pass_gvn_pre::transform(LGraph *g) {
  traverse(g, 1);
}

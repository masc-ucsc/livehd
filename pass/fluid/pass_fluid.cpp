//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <string>
#include <time.h>

#include "lgedgeiter.hpp"
#include "lgraph.hpp"
#include "pass_fluid.hpp"

#define PRINT_INFO

Pass_fluid::Pass_fluid()
    : Pass("fluid") {
}

/*
  Node_Pin:
    Index_ID    nid:Index_Bits;
    Port_ID     pid:Port_Bits;
    bool        input;
*/

void Pass_fluid::find_join(LGraph *g) {
  fmt::print("\n**Find Join: **\n");
  for(auto idx : g->forward()) {
    fmt::print("\n*****Visiting idx:{}\n", idx);
    fmt::print("{}\n", g->node_type_get(idx).get_name());
    for(const auto &inp_edge : g->inp_edges(idx)) {
      Node_Pin_P new_node_pin = inp_edge.get_out_pin();
      Index_ID   inp_idx      = new_node_pin.get_nid();
      if(g->node_type_get(inp_idx).op == SFlop_Op) {
#ifdef PRINT_INFO
        fmt::print("Has a flop as its input.\n");
        new_node_pin.print_info();
#endif
        if(find(join_index_flop_map[idx].begin(), join_index_flop_map[idx].end(), inp_idx) == join_index_flop_map[idx].end()) {
          // not found in current vec
          join_index_has_flop_map[idx] = true;
          join_index_flop_map[idx].push_back(inp_idx);
        } else {
#ifdef PRINT_INFO
          fmt::print("Found repetitive idx {}.\n", inp_idx);
#endif
        }
      } else if(join_index_has_flop_map[inp_idx]) {
        // This input idx has flops
        for(auto flop_idx : join_index_flop_map[inp_idx]) {
          if(find(join_index_flop_map[idx].begin(), join_index_flop_map[idx].end(), flop_idx) == join_index_flop_map[idx].end()) {
            join_index_has_flop_map[idx] = true;
            join_index_flop_map[idx].push_back(flop_idx);
          } else {
#ifdef PRINT_INFO
            fmt::print("Found repetitive idx {}.\n", flop_idx);
#endif
          }
        }
      }
    } // end of for loop
    if(g->node_type_get(idx).op == SFlop_Op) {
      // the current node is flop
      if(join_index_has_flop_map[idx]) {
        for(auto flop : join_index_flop_map[idx]) {
#ifdef PRINT_INFO
          fmt::print("This flop joins flop: {}.\n", flop);
#endif
        }
      } else {
        // this node is an in flop since it doesn't join any flops
        in_flop_vec.push_back(idx);
      }
    }
  }
}

void Pass_fluid::find_fork(LGraph *g) {
  fmt::print("\n**Find Fork: **\n");
  for(auto idx : g->backward()) {
    fmt::print("\n*****Visiting idx:{}\n", idx);
    fmt::print("{}\n", g->node_type_get(idx).get_name());
    for(const auto &out_edge : g->out_edges(idx)) {
      Node_Pin_P new_node_pin = out_edge.get_inp_pin();
      Index_ID   out_idx      = new_node_pin.get_nid();
      if(g->node_type_get(out_idx).op == SFlop_Op) {
        Port_ID flop_pid = out_edge.get_inp_pin().get_pid();
#ifdef PRINT_INFO
        fmt::print("Has a flop as its output.\n");
        new_node_pin.print_info();
        fmt::print("The flop pid is {}\n", flop_pid);
#endif
        if(flop_pid == 0) {
          fmt::print("This pin is a clk pin, ignore it.\n");
        } else {
          if(find(fork_index_flop_map[idx].begin(), fork_index_flop_map[idx].end(), out_idx) == fork_index_flop_map[idx].end()) {
            // not found in current vec
            fork_index_has_flop_map[idx] = true;
            fork_index_flop_map[idx].push_back(out_idx);
          } else {
#ifdef PRINT_INFO
            fmt::print("Found repetitive idx {}.\n", out_idx);
#endif
          }
        }
      } else if(fork_index_has_flop_map[out_idx]) // TODO: confirm if a unmapped key will return false
      {
        // This output idx has flops
        for(auto flop_idx : fork_index_flop_map[out_idx]) {
          if(find(fork_index_flop_map[idx].begin(), fork_index_flop_map[idx].end(), flop_idx) == fork_index_flop_map[idx].end()) {
            fork_index_has_flop_map[idx] = true;
            fork_index_flop_map[idx].push_back(flop_idx);
          } else {
#ifdef PRINT_INFO
            fmt::print("Found repetitive idx {}.\n", flop_idx);
#endif
          }
        }
      }
    } // end of for loop

    if(g->node_type_get(idx).op == SFlop_Op) {
      // the current node is flop
      if(fork_index_has_flop_map[idx]) {
        for(auto flop : fork_index_flop_map[idx]) {
#ifdef PRINT_INFO
          fmt::print("This flop forks flop: {}.\n", flop);
#endif
        }
      } else {
        // this node is an out flop since it doesn't fork any flops
        fmt::print("This flop is an Out Flop.\n");
        out_flop_vec.push_back(idx);
      }
    }
  }
}

void Pass_fluid::add_fork(LGraph *g) {
  // add v and s first
  fmt::print("\n**Add Fork: **\n");

  // add vi
  std::string vi_name = "vi";
  Index_ID    vi_nid  = g->create_node().get_nid();
  // TODO: ask how to add pid to IO
  // Port_ID vi_pid = vi_nid;
  Port_ID vi_pid = 0;
  fmt::print("the nid of vi is {}\n", vi_nid);
  g->node_type_set(vi_nid, GraphIO_Op);
  g->add_graph_input(vi_name.c_str(), vi_nid);
  Node_Pin global_vi(vi_nid, vi_pid, false);

  // add so
  std::string so_name = "so";
  Index_ID    so_nid  = g->create_node().get_nid();
  // TODO: ask how to add pid to IO
  //  Port_ID so_pid = so_nid;
  Port_ID so_pid = 0;
  fmt::print("the nid of so is {}\n", so_nid);
  g->node_type_set(so_nid, GraphIO_Op);
  g->add_graph_input(so_name.c_str(), so_nid);
  Node_Pin global_so(so_nid, so_pid, false);

  // add vo
  std::string vo_name = "vo";
  Index_ID    vo_nid  = g->create_node().get_nid();
  // TODO: ask how to add pid to IO
  //  Port_ID vo_pid = vo_nid;
  Port_ID vo_pid = 0;
  fmt::print("the nid of vo is {}\n", vo_nid);
  g->node_type_set(vo_nid, GraphIO_Op);
  g->add_graph_output(vo_name.c_str(), vo_nid);
  Node_Pin global_vo(vo_nid, vo_pid, true);

  // add si
  std::string si_name = "si";
  Index_ID    si_nid  = g->create_node().get_nid();
  // TODO: ask how to add pid to IO
  //  Port_ID si_pid = si_nid;
  Port_ID si_pid = 0;
  fmt::print("the nid of si is {}\n", si_nid);
  g->node_type_set(si_nid, GraphIO_Op);
  g->add_graph_output(si_name.c_str(), si_nid);
  Node_Pin global_si(si_nid, si_pid, true);

  for(auto idx : g->forward()) {
    fmt::print("\n*****Visiting idx:{}\n", idx);
    fmt::print("{}\n", g->node_type_get(idx).get_name());
    for(const auto &out_edge : g->out_edges(idx)) {
      fmt::print("Printing edge from nid{} pid{} to nid{} pid{}.\n", out_edge.get_self_nid(), out_edge.get_out_pin().get_pid(), out_edge.get_idx(), out_edge.get_inp_pin().get_pid());
    }

    if(g->node_type_get(idx).op == SFlop_Op) {
      // change the node type to fflop
      g->node_type_set(idx, FFlop_Op);

      // add in flops connections
      if(find(in_flop_vec.begin(), in_flop_vec.end(), idx) != in_flop_vec.end()) {
        // this flop is an In Flop
        fmt::print("This flop is an In Flop.\n");
        // connect vin
        Node_Pin vin_pin(idx, ffvin, true);
        g->add_edge(global_vi, vin_pin);

        // connect si
        Node_Pin si_pin(idx, ffsin, false);
        g->add_edge(si_pin, global_si);
      }

      // add out flops connections
      if(find(out_flop_vec.begin(), out_flop_vec.end(), idx) != out_flop_vec.end()) {
        // this flop is an Out Flop
        fmt::print("This flop is an Out Flop.\n");
        // connect vq
        Node_Pin vq_pin(idx, ffvq, false);
        g->add_edge(vq_pin, global_vo);

        // connect sout
        Node_Pin sout_pin(idx, ffsout, true);
        g->add_edge(global_so, sout_pin);
      }
      if(fork_index_has_flop_map[idx]) {
        Node_Pin sout_pin(idx, ffsout, true);
        Node_Pin vq_pin(idx, ffvq, false);
        if(fork_index_flop_map[idx].size() >= 2) {
          bool join_after_fork = false;
          // Add Fork
          fmt::print("Add fork at idx={}.\n", idx);

          Flop_Indx_Vec local_fork_index_flop_vec;
          local_fork_index_flop_vec = fork_index_flop_map[idx];
          size_t   flop_count       = fork_index_flop_map[idx].size();
          Index_ID fork_nid;
          Index_ID forked_fflop_nid[flop_count];

          // detect join after fork
          for(int i = 0; i < flop_count; i++) {
            fork_nid = local_fork_index_flop_vec.back();
            local_fork_index_flop_vec.pop_back();
            // check if the output flop needs to join
            if(join_index_flop_map[fork_nid].size() >= 2) {
              // add forked fflop
              join_after_fork = true;
              fmt::print("Join after fork detected at nid={} to nid={}.\n", idx, fork_nid);
              forked_fflop_nid[i] = g->create_node().get_nid();
              g->node_type_set(forked_fflop_nid[i], FFlop_Op);
            }
          }

          if(join_after_fork) {
            // replicate the logic from idx to forked fflops
            for(int i = 0; i < flop_count; i++) {
              fmt::print("replicate logic from {} to {}.\n", idx, forked_fflop_nid[i]);
              // this node should be replaced
              // Insert new edges
              for(const auto &out_edge : g->out_edges(idx)) {
                Node_Pin src = Node_Pin(forked_fflop_nid[i], 0, false);
                Node_Pin dst = Node_Pin(out_edge.get_idx(), out_edge.get_inp_pin().get_pid(), true);
                g->add_edge(src, dst);
              }
            }
            // Delete all out edges from idx
            bool deleted;
            do {
              deleted = false;
              for(const auto &out_edge : g->out_edges(idx)) {
                fmt::print("Printing out edge: \n");
                fmt::print("nid{} pid{} \n", out_edge.get_idx(), out_edge.get_inp_pin().get_pid());

                fmt::print("Deleting out edge: \n");
                g->del_edge(out_edge);
                deleted = true;
              }
            } while(deleted);
          }

          // Or gate
          Index_ID or_nid = g->create_node().get_nid();
          g->node_type_set(or_nid, Or_Op);
          Node_Pin or_inp(or_nid, 0, true);
          Node_Pin or_onp(or_nid, 0, false);
          local_fork_index_flop_vec = fork_index_flop_map[idx];

          for(int i = 0; i < flop_count; i++) {
            fmt::print("Add #{} input to OR.\n", i);
            fork_nid = local_fork_index_flop_vec.back();
            local_fork_index_flop_vec.pop_back();
            Node_Pin fork_np = Node_Pin(fork_nid, ffsin, false);
            // check if the output flop needs to join
            if(join_index_flop_map[fork_nid].size() >= 2) {
              // add forked fflop
              // TODO add clk to new fflops
              Node_Pin forked_fflop_sin(forked_fflop_nid[i], ffsin, false);
              g->add_edge(forked_fflop_sin, or_inp);
            } else {
              g->add_edge(fork_np, or_inp);
            }
          }
          g->add_edge(or_onp, sout_pin);

          // Not gate
          Index_ID not_nid = g->create_node().get_nid();
          g->node_type_set(not_nid, Not_Op);
          Node_Pin not_inp(not_nid, 0, true);
          g->add_edge(or_onp, not_inp);
          Node_Pin not_onp(not_nid, 0, false);

          // And gate
          Index_ID and_nid = g->create_node().get_nid();
          g->node_type_set(and_nid, And_Op);
          Node_Pin and_inp(and_nid, 0, true);
          Node_Pin and_onp(and_nid, 0, false);
          g->add_edge(not_onp, and_inp);
          g->add_edge(vq_pin, and_inp);
          local_fork_index_flop_vec = fork_index_flop_map[idx];
          for(int i = 0; i < flop_count; i++) {
            fmt::print("Add #{} output to AND.\n", i);
            fork_nid = local_fork_index_flop_vec.back();
            local_fork_index_flop_vec.pop_back();
            // check if the output flop needs to join
            if(join_index_flop_map[fork_nid].size() >= 2) {
              Node_Pin forked_fflop_vin(forked_fflop_nid[i], ffvin, true);
              g->add_edge(and_onp, forked_fflop_vin);
            } else {
              Node_Pin fork_vin_np = Node_Pin(fork_nid, ffvin, true);
              g->add_edge(and_onp, fork_vin_np);
            }
          }

          if(join_after_fork) {
            // map them to the join map and fork map (TODO may be not?)
            for(int i = 0; i < flop_count; i++) {
            }
          }
          flop_to_vq_map[idx]  = and_nid;
          flop_to_sin_map[idx] = or_nid;
        }      // end of if (fork_index_flop_map[idx].size() >= 2)
        else { // only one out fflop; no need to fork, just connect
          Index_ID fork_nid    = fork_index_flop_map[idx].back();
          Node_Pin fork_sin_np = Node_Pin(fork_nid, ffsin, false);
          g->add_edge(fork_sin_np, sout_pin);
          Node_Pin fork_vin_np = Node_Pin(fork_nid, ffvin, true);
          g->add_edge(vq_pin, fork_vin_np);
          flop_to_vq_map[idx]  = idx;
          flop_to_sin_map[idx] = idx;
        }
      } // end of if (fork_index_has_flop_map[idx])
    }   // end of if == Flop
  }

  fmt::print("\n*****Second round.\n");
  for(auto idx : g->forward()) {
    fmt::print("\n*****Visiting idx:{}\n", idx);
    fmt::print("{}\n", g->node_type_get(idx).get_name());

    for(const auto &out_edge : g->out_edges(idx)) {
      fmt::print("Printing edge from nid{} pid{} to nid{} pid{}.\n", out_edge.get_self_nid(), out_edge.get_out_pin().get_pid(), out_edge.get_idx(), out_edge.get_inp_pin().get_pid());
    }
  }

  /*
  for(auto idx:g->backward()) {
    fmt::print("\n*****Visiting idx:{}\n",idx);
    fmt::print("{}\n", op_type_map[g->node_type_get(idx).op]);
  */
}

void Pass_fluid::add_fork_deadlock(LGraph *g) {
  // add v and s first
  fmt::print("\n**Add Fork: **\n");

  // add vi
  std::string vi_name = "vi";
  Index_ID    vi_nid  = g->create_node().get_nid();
  // TODO: ask how to add pid to IO
  // Port_ID vi_pid = vi_nid;
  Port_ID vi_pid = 0;
  fmt::print("the nid of vi is {}\n", vi_nid);
  g->node_type_set(vi_nid, GraphIO_Op);
  g->add_graph_input(vi_name.c_str(), vi_nid);
  Node_Pin global_vi(vi_nid, vi_pid, false);

  // add so
  std::string so_name = "so";
  Index_ID    so_nid  = g->create_node().get_nid();
  // TODO: ask how to add pid to IO
  //  Port_ID so_pid = so_nid;
  Port_ID so_pid = 0;
  fmt::print("the nid of so is {}\n", so_nid);
  g->node_type_set(so_nid, GraphIO_Op);
  g->add_graph_input(so_name.c_str(), so_nid);
  Node_Pin global_so(so_nid, so_pid, false);

  // add vo
  std::string vo_name = "vo";
  Index_ID    vo_nid  = g->create_node().get_nid();
  // TODO: ask how to add pid to IO
  //  Port_ID vo_pid = vo_nid;
  Port_ID vo_pid = 0;
  fmt::print("the nid of vo is {}\n", vo_nid);
  g->node_type_set(vo_nid, GraphIO_Op);
  g->add_graph_output(vo_name.c_str(), vo_nid);
  Node_Pin global_vo(vo_nid, vo_pid, true);

  // add si
  std::string si_name = "si";
  Index_ID    si_nid  = g->create_node().get_nid();
  // TODO: ask how to add pid to IO
  //  Port_ID si_pid = si_nid;
  Port_ID si_pid = 0;
  fmt::print("the nid of si is {}\n", si_nid);
  g->node_type_set(si_nid, GraphIO_Op);
  g->add_graph_output(si_name.c_str(), si_nid);
  Node_Pin global_si(si_nid, si_pid, true);

  for(auto idx : g->forward()) {
    fmt::print("\n*****Visiting idx:{}\n", idx);
    fmt::print("{}\n", g->node_type_get(idx).get_name());
    for(const auto &out_edge : g->out_edges(idx)) {
      fmt::print("Printing edge from nid{} pid{} to nid{} pid{}.\n", out_edge.get_self_nid(), out_edge.get_out_pin().get_pid(), out_edge.get_idx(), out_edge.get_inp_pin().get_pid());
    }

    if(g->node_type_get(idx).op == SFlop_Op) {
      // change the node type to fflop
      g->node_type_set(idx, FFlop_Op);
      if(find(in_flop_vec.begin(), in_flop_vec.end(), idx) != in_flop_vec.end()) {
        // this flop is an In Flop
        fmt::print("This flop is an In Flop.\n");
        // connect vin
        Node_Pin vin_pin(idx, ffvin, true);
        g->add_edge(global_vi, vin_pin);

        // connect si
        Node_Pin si_pin(idx, ffsin, false);
        g->add_edge(si_pin, global_si);
      }
      // add out flops connections
      if(find(out_flop_vec.begin(), out_flop_vec.end(), idx) != out_flop_vec.end()) {
        // this flop is an Out Flop
        fmt::print("This flop is an Out Flop.\n");
        // connect vq
        Node_Pin vq_pin(idx, ffvq, false);
        g->add_edge(vq_pin, global_vo);

        // connect sout
        Node_Pin sout_pin(idx, ffsout, true);
        g->add_edge(global_so, sout_pin);
      }
      if(fork_index_has_flop_map[idx]) {
        Node_Pin sout_pin(idx, ffsout, true);
        Node_Pin vq_pin(idx, ffvq, false);
        if(fork_index_flop_map[idx].size() >= 2) {
          // Add Fork
          fmt::print("Add fork at idx={}.\n", idx);

          Flop_Indx_Vec local_fork_index_flop_vec;
          local_fork_index_flop_vec = fork_index_flop_map[idx];
          // Or gate
          Index_ID or_nid = g->create_node().get_nid();
          g->node_type_set(or_nid, Or_Op);
          Node_Pin or_inp(or_nid, 0, true);
          Index_ID fork_nid;
          size_t   flop_count = fork_index_flop_map[idx].size();
          for(int i = 0; i < flop_count; i++) {
            fmt::print("Add #{} input to OR.\n", i);
            fork_nid = local_fork_index_flop_vec.back();
            local_fork_index_flop_vec.pop_back();
            Node_Pin fork_np = Node_Pin(fork_nid, ffsin, false);
            g->add_edge(fork_np, or_inp);
          }
          Node_Pin or_onp(or_nid, 0, false);
          g->add_edge(or_onp, sout_pin);

          // Not gate
          Index_ID not_nid = g->create_node().get_nid();
          g->node_type_set(not_nid, Not_Op);
          Node_Pin not_inp(not_nid, 0, true);
          g->add_edge(or_onp, not_inp);
          Node_Pin not_onp(not_nid, 0, false);

          // And gate
          Index_ID and_nid = g->create_node().get_nid();
          g->node_type_set(and_nid, And_Op);
          Node_Pin and_inp(and_nid, 0, true);
          Node_Pin and_onp(and_nid, 0, false);
          g->add_edge(not_onp, and_inp);
          g->add_edge(vq_pin, and_inp);
          local_fork_index_flop_vec = fork_index_flop_map[idx];
          for(int i = 0; i < flop_count; i++) {
            fmt::print("Add #{} output to AND.\n", i);
            fork_nid = local_fork_index_flop_vec.back();
            local_fork_index_flop_vec.pop_back();
            Node_Pin fork_vin_np = Node_Pin(fork_nid, ffvin, true);
            g->add_edge(and_onp, fork_vin_np);
          }
          flop_to_vq_map[idx]  = and_nid;
          flop_to_sin_map[idx] = or_nid;
        }      // end of if (fork_index_flop_map[idx].size() >= 2)
        else { // only one out fflop; no need to fork, just connect
          Index_ID fork_nid    = fork_index_flop_map[idx].back();
          Node_Pin fork_sin_np = Node_Pin(fork_nid, ffsin, false);
          g->add_edge(fork_sin_np, sout_pin);
          Node_Pin fork_vin_np = Node_Pin(fork_nid, ffvin, true);
          g->add_edge(vq_pin, fork_vin_np);
          flop_to_vq_map[idx]  = idx;
          flop_to_sin_map[idx] = idx;
        }
      } // end of if (fork_index_has_flop_map[idx])
    }   // end of if == Flop
  }

  fmt::print("\n*****Second round.\n");
  for(auto idx : g->forward()) {
    fmt::print("\n*****Visiting idx:{}\n", idx);
    fmt::print("{}\n", g->node_type_get(idx).get_name());

    for(const auto &out_edge : g->out_edges(idx)) {
      fmt::print("Printing edge from nid{} pid{} to nid{} pid{}.\n", out_edge.get_self_nid(), out_edge.get_out_pin().get_pid(), out_edge.get_idx(), out_edge.get_inp_pin().get_pid());
    }
  }

  /*
  for(auto idx:g->backward()) {
    fmt::print("\n*****Visiting idx:{}\n",idx);
    fmt::print("{}\n", op_type_map[g->node_type_get(idx).op]);
  */
}

/*
 Port_ID ffvin = 2;
  Port_ID ffsout = 3;
  Port_ID ffvq = 1;
  Port_ID ffsin = 2;
*/

void Pass_fluid::add_join(LGraph *g) {
}

void Pass_fluid::add_join_deadlock(LGraph *g) {
  fmt::print("\n**Add Join (possible dead lock): **\n");
  for(auto idx : g->forward()) {
    fmt::print("\n*****Visiting idx:{}\n", idx);
    fmt::print("{}\n", g->node_type_get(idx).get_name());
    if(g->node_type_get(idx).op == FFlop_Op) {
      if(join_index_has_flop_map[idx]) {
        size_t flop_count = join_index_flop_map[idx].size();
        if(join_index_flop_map[idx].size() >= 2) {
          /* TODO: ask why this doesn't work
          for(const auto &inp_edge:g->inp_edges(idx)) {
            Node_Pin_P new_node_pin = inp_edge.get_inp_pin();
            new_node_pin.print_info();
            Port_ID new_pid2= new_node_pin.get_pid();
            fmt::print("new_pid2 is {}.\n", new_pid2);
          */
          Flop_Indx_Vec local_join_index_flop_vec;
          local_join_index_flop_vec = join_index_flop_map[idx];
          Index_ID join_vq_nid, flop_nid, join_sout_nid;

          // And gate to vout
          Index_ID and_vout_nid = g->create_node().get_nid();
          g->node_type_set(and_vout_nid, And_Op);
          Node_Pin and_vout_inp(and_vout_nid, 0, true);
          Node_Pin and_vout_onp(and_vout_nid, 0, false);
          Node_Pin vin_np(idx, ffvin, true);
          //          g->add_edge(and_vout_onp, vin_np);

          // Not gate to vout
          Index_ID not_nid = g->create_node().get_nid();
          g->node_type_set(not_nid, Not_Op);
          Node_Pin not_inp(not_nid, 0, true);
          Node_Pin not_onp(not_nid, 0, false);
          g->add_edge(and_vout_onp, not_inp);

          // Or gate
          Index_ID or_nid = g->create_node().get_nid();
          g->node_type_set(or_nid, Or_Op);
          Node_Pin or_inp(or_nid, 0, true);
          Node_Pin or_onp(or_nid, 0, false);
          Node_Pin sin_np(idx, ffsin, false);
          g->add_edge(sin_np, or_inp);
          g->add_edge(not_onp, or_inp);

          // And gate to souts of upstream
          Index_ID              and_sout_nid[flop_count];
          std::vector<Node_Pin> and_sout_inp;
          for(int i = 0; i < flop_count; i++) {
            and_sout_nid[i] = g->create_node().get_nid();
            g->node_type_set(and_sout_nid[i], And_Op);
            and_sout_inp.push_back(Node_Pin(and_sout_nid[i], 0, true));
          }

          for(int i = 0; i < flop_count; i++) {

            fmt::print("Add #{} input to ANDtoVout.\n", i);
            assert(local_join_index_flop_vec.size() > 0);
            flop_nid = local_join_index_flop_vec.back();
            local_join_index_flop_vec.pop_back();
            join_vq_nid = flop_to_vq_map[flop_nid];
            // add edge to souts
            join_sout_nid = flop_to_sin_map[flop_nid];
            Node_Pin and_sout_onp(and_sout_nid[i], 0, false);
            g->add_edge(or_onp, and_sout_inp[i]);

            if(g->node_type_get(join_sout_nid).op == FFlop_Op) {
              // add edge fron and_sout_port to sout
              Node_Pin join_sout_np = Node_Pin(join_sout_nid, ffsout, true);
              g->add_edge(and_sout_onp, join_sout_np);
              // delete the edge from this Fflop.sin to this Fflop.sout
              for(const auto &out_edge : g->out_edges(idx)) {
                if(out_edge.get_idx() == flop_nid) {
                  if(out_edge.get_out_pin().get_pid() == ffsin && out_edge.get_inp_pin().get_pid() == ffsout) {
                    g->del_edge(out_edge);
                    break;
                  }
                }
              }
            } else if(g->node_type_get(join_sout_nid).op == Or_Op) {
              // add edge to sout
              Node_Pin join_sout_np = Node_Pin(join_sout_nid, 0, true);
              g->add_edge(and_sout_onp, join_sout_np);
              // delete the edge from this Fflop.sin to the Or gate of the upstream fflop
              for(const auto &out_edge : g->out_edges(idx)) {
                if(out_edge.get_idx() == join_sout_nid) {
                  g->del_edge(out_edge);
                  break;
                }
              }
            } else {
              fmt::print("Error: Invalid Operator to join.\n");
              assert(0);
            }

            // add edge to vout
            if(g->node_type_get(join_vq_nid).op == FFlop_Op) {
              // add edge to vout
              Node_Pin join_vq_np = Node_Pin(join_vq_nid, ffvq, false);
              g->add_edge(join_vq_np, and_vout_inp);
              // delete the edge from Fflop.vq to this Fflop.vin
              for(const auto &out_edge : g->out_edges(join_vq_nid)) {
                if(out_edge.get_idx() == idx) {
                  if(out_edge.get_out_pin().get_pid() == ffvq && out_edge.get_inp_pin().get_pid() == ffvin) {
                    g->del_edge(out_edge);
                    break;
                  }
                }
              }
              // add edge to or gate
              g->add_edge(join_vq_np, and_sout_inp[i]);
            } else if(g->node_type_get(join_vq_nid).op == And_Op) {
              // add edge to vout
              Node_Pin join_vq_np = Node_Pin(join_vq_nid, 0, false);
              g->add_edge(join_vq_np, and_vout_inp);
              // delete the edge from AND to this Fflop
              for(const auto &out_edge : g->out_edges(join_vq_nid)) {
                if(out_edge.get_idx() == idx) {
                  g->del_edge(out_edge);
                  break;
                }
              }
              // add edge to or gate
              g->add_edge(join_vq_np, and_sout_inp[i]);
            } else {
              fmt::print("Error: Invalid Operator to join.\n");
              assert(0);
            }
          } // end of for flop_count
          g->add_edge(and_vout_onp, vin_np);
        }
      }
    }
  }
}

void Pass_fluid::traverse(LGraph *g,
                          int     round) {
  std::cout << "RTP 2" << std::endl;
  find_join(g);
  std::cout << "RTP 3" << std::endl;
  find_fork(g);
  std::cout << "RTP 4" << std::endl;
  add_fork_deadlock(g);
  std::cout << "RTP 5" << std::endl;
  add_join_deadlock(g);
  std::cout << "RTP 6" << std::endl;
  //result_graph(g);
}
void Pass_fluid::transform(LGraph *g) {
  traverse(g, 1);
}

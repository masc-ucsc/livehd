//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <string>
#include <time.h>

#include "lgedgeiter.hpp"
#include "lgraph.hpp"
#include "pass_vectorize.hpp"

Pass_vectorize::Pass_vectorize()
    : Pass("vectorize") {
}

void Pass_vectorize::collapse_reset(LGraph *g) {

  Index_ID reset_idx = 0;
  Port_ID  reset_pid = 0;

  if(g->is_graph_input("reset")) {
    reset_idx = g->get_graph_input("reset").get_nid();
    reset_pid = g->get_graph_input("reset").get_pid();
  }
  if(reset_idx == 0) {
    if(g->is_graph_input("rst")) {
      reset_idx = g->get_graph_input("rst").get_nid();
      reset_pid = g->get_graph_input("rst").get_pid();
    }
  }

  if(reset_idx == 0)
    return;

  for(const auto &c1 : g->out_edges(reset_idx)) {
    const auto op = g->node_type_get(c1.get_idx());
    if(op.op != Mux_Op)
      continue;
    if(c1.get_inp_pin().get_pid() != Node_Type::get(Mux_Op).get_input_match("S"))
      continue; // Control in mux

    Index_ID mux_idx  = c1.get_idx();
    Index_ID flop_idx = 0;

    for(const auto &c2 : g->out_edges(c1.get_idx())) {
      const auto op = g->node_type_get(c2.get_idx());
      if(op.op != SFlop_Op)
        continue;
      flop_idx = c2.get_idx();
    }
    if(!mux_idx || !flop_idx)
      continue;

    bool reset_set = false;
    for(const auto &c : g->inp_edges(flop_idx)) {
      // const auto &re = c.get_reverse_edge();
      if(c.get_inp_pin().get_pid() == Node_Type::get(SFlop_Op).get_input_match("R")) {
        reset_set = true;
        break; // Can not handle 2 resets
      }
      if(c.get_inp_pin().get_pid() == Node_Type::get(SFlop_Op).get_input_match("Rval")) {
        reset_set = true;
        break; // Can not handle 2 resets
      }
    }
    if(reset_set)
      continue;

    Index_ID mux_a_idx = 0;
    Index_ID mux_a_pid = 0;
    Index_ID mux_b_idx = 0;
    Index_ID mux_b_pid = 0;
    for(const auto &c : g->inp_edges(mux_idx)) {
      // const auto &re = c.get_reverse_edge();
      if(c.get_inp_pin().get_pid() == Node_Type::get(Mux_Op).get_input_match("A")) {
        mux_a_idx = c.get_idx();
        mux_a_pid = c.get_out_pin().get_pid();
      } else if(c.get_inp_pin().get_pid() == Node_Type::get(Mux_Op).get_input_match("B")) {
        mux_b_idx = c.get_idx();
        mux_b_pid = c.get_out_pin().get_pid();
      }
    }

    if(!mux_a_idx || !mux_b_idx)
      continue;

    g->del_node(mux_idx);

    Node_pin src_d(mux_b_idx, mux_b_pid, false);
    Node_pin dst_d(flop_idx, Node_Type::get(SFlop_Op).get_input_match("D"), true);
    g->add_edge(src_d, dst_d);

    Node_pin src_val(mux_a_idx, mux_a_pid, false);
    Node_pin dst_val(flop_idx, Node_Type::get(SFlop_Op).get_input_match("Rval"), true);
    g->add_edge(src_val, dst_val);

    Node_pin src_r(reset_idx, reset_pid, false);
    Node_pin dst_r(flop_idx, Node_Type::get(SFlop_Op).get_input_match("R"), true);
    g->add_edge(src_r, dst_r);
  }
}

void Pass_vectorize::collapse_join(LGraph *g) {

  for(auto idx : g->fast()) {
    const auto op = g->node_type_get(idx);
    if(op.op != Join_Op)
      continue;

    if(g->get_node_int(idx).get_num_inputs() != 1)
      continue;

    Index_ID src_idx = 0;
    Port_ID  src_pid = 0;
    for(const auto &c : g->inp_edges(idx)) {
      // const auto &re = c.get_reverse_edge();
      src_idx = c.get_idx();
      src_pid = c.get_out_pin().get_pid();
    }

    Node_pin src(src_idx, src_pid, false);

    for(const auto &c : g->out_edges(idx)) {
      Index_ID dst_idx = c.get_idx();
      Port_ID  dst_pid = c.get_inp_pin().get_pid();

      g->del_edge(c);

      Node_pin dst(dst_idx, dst_pid, true);
      g->add_edge(src, dst);
    }
    g->del_node(idx);
  }
}

void Pass_vectorize::transform(LGraph *g) {

  collapse_join(g);
  collapse_reset(g);

#if 0
  g->each_input([](Index_ID idx) {
      fmt::print("input {}\n",idx);
      }
      );

  g->each_output([](Index_ID idx) {
      fmt::print("output {}\n",idx);
      }
      );

  for(auto idx:g->backward()) {
    fmt::print("b visited idx:{}\n",idx);
    output_used.set_bit(idx);
    for(const auto &c:g->inp_edges(idx)) {
      fmt::print("setting idx:{}\n",c.get_idx());
      output_used.set_bit(c.get_idx());
    }
  }

  for(auto idx:g->forward()) {
    fmt::print("f visited idx:{}\n",idx);
  }

  for(auto idx:g->fast()) {
    if (!output_used.is_bit(idx)) {
      fmt::print("idx:{} is not used. DELETE\n",idx);

      bool deleted;
      do {
        deleted = false;
        for(const auto &c:g->out_edges(idx)) {
          g->del_edge(c);
          deleted = true;
        }
      }while(deleted);

      fmt::print("vectorize finished outedges\n");

      do {
        deleted = false;
        for(const auto &c:g->inp_edges(idx)) {
          g->del_edge(c);
          deleted = true;
        }
      }while(deleted); // Delete can mess the iterator, try again
    }
  }

  for(auto idx:g->forward()) {
    fmt::print("f visited idx:{}\n",idx);
  }
#endif
}

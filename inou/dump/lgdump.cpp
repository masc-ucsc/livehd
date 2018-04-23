
#include <stdio.h>
#include <stdlib.h>
#include <iostream>
#include <random>

#include "lgraph.hpp"
#include "inou.hpp"
#include "lgedgeiter.hpp"


void lgdump_digraph(const LGraph *g) {
  fprintf(stderr,"digraph fwd_%s {\n", g->get_name().c_str());

  for(auto idx:g->fast()) {
    if (g->is_graph_input(idx))
      fprintf(stderr,"node%d[label =\"%d $%s\"];\n",(int)idx, (int)idx, g->get_graph_input_name(idx));
    else if (g->is_graph_output(idx))
      fprintf(stderr,"node%d[label =\"%d %%%s\"];\n",(int)idx, (int)idx, g->get_graph_output_name(idx));
    else
      fprintf(stderr,"node%d[label =\"%d %s\"];\n",(int)idx, (int)idx, g->node_type_get(idx).get_name().c_str());
  }

#if 1
#if 0
  for(auto idx:g->forward()) {
    for(const auto &c:g->out_edges(idx)) {
      fprintf(stderr,"node%d -> node%d\n", (int)idx, (int)c.get_idx());
    }
  }
#endif
  for(auto idx:g->fast()) {
    for(const auto &c:g->out_edges(idx)) {
      fprintf(stderr,"node%d -> node%d\n", (int)idx, (int)c.get_idx());
    }
  }
  fprintf(stderr,"}\n");
#else
  fprintf(stderr,"digraph bck_%s {\n", g->get_name().c_str());

  for(auto idx:g->fast()) {
    fprintf(stderr,"node%d[label =\"%d %s\"];\n",(int)idx, (int)idx, g->node_type_get(idx).get_name().c_str());
  }
  for(auto idx:g->backward()) {
    for(const auto &c:g->inp_edges(idx)) {
      fprintf(stderr,"node%d -> node%d\n", (int)idx, (int)c.get_idx());
    }
  }
  fprintf(stderr,"}\n");
#endif

  for(auto idx:g->backward()) {
    for(const auto &c:g->inp_edges(idx)) {
      fprintf(stderr,"node%d:%d -> node%d:%d TSTbck\n"
          , (int)c.get_idx(), (int)c.get_inp_pin().get_pid()
          , (int)idx, (int)c.get_out_pin().get_pid());
      const auto &re = c.get_reverse_edge();
      fprintf(stderr,"node%d:%d -> node%d:%d TSTrbck\n"
          , (int)re.get_self_nid(), (int)re.get_inp_pin().get_pid()
          , (int)re.get_idx()     , (int)re.get_out_pin().get_pid());
    }
  }

  for(auto idx:g->forward()) {
    for(const auto &c:g->out_edges(idx)) {
      fprintf(stderr,"node%d:%d -> node%d:%d TSTfwd\n", (int)idx, (int)c.get_inp_pin().get_pid(), (int)c.get_idx(), (int)c.get_out_pin().get_pid());
    }
  }
  for(auto idx:g->fast()) {
    for(const auto &c:g->out_edges(idx)) {
      fprintf(stderr,"node%d:%d -> node%d:%d TSTfast\n", (int)idx, (int)c.get_inp_pin().get_pid(), (int)c.get_idx(), (int)c.get_out_pin().get_pid());
    }
  }
}

int main(int argc, const char **argv) {
  Options::setup(argc, argv);

  Inou_trivial trivial;

  if (!trivial.is_graph_name_provided()) {
    fmt::print("Specify a graph to be dump with --graph_name\n");
    exit(-1);
  }

  Options::setup_lock();

  std::vector<LGraph *> rvec = trivial.generate();

  for(auto &g:rvec) {
    g->print_stats();
    //g->dump();

    //lgdump_digraph(g);
#if 1
    for(Index_ID idx=0;idx<g->size();idx++) {
		//for(auto &idx : g->forward()) {
      const auto &node = g->get_node_int(idx);

      if (node.is_page_align()) {
        fmt::print("{} p\n", idx);
      }else{
        bool root   = node.is_root();
        bool master = node.is_master_root();
        const char *str = "x";
        if (master) {
          str = "m";
        }else if (root) {
          str = "r";
        }

        fmt::print("{} {}:{} {} {} {} {}\n"
        ,idx
        ,node.get_master_root_nid()
        ,node.get_out_pid(), str, node.get_inp_pos(), node.get_out_pos(), node.get_space_available());
        if (node.is_master_root()) {
          for (const auto &c : g->out_edges(idx)) {
            fmt::print("O {} {} {} ", (int)idx, (int)c.get_idx(), (int)c.get_idx() - (int)idx);
            if (abs((int)c.get_idx() - (int)idx)>2048)
              fmt::print("L\n");
            else
              fmt::print("S\n");
          }
          for (const auto &c : g->inp_edges(idx)) {
            fmt::print("I {} {} {} ", (int)idx, (int)c.get_idx(), (int)c.get_idx() - (int)idx);
            if (abs((int)c.get_idx() - (int)idx)>2048)
              fmt::print("L\n");
            else
              fmt::print("S\n");
          }
        }
      }
    }
#endif
  }
}


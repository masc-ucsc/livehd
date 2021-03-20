// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <bits/stdint-uintn.h>
#include <mutex>
#include <vector>

#include "absl/container/node_hash_map.h"
#include "lgedgeiter.hpp"
#include "bitwidth_range.hpp"
#include "lgraph.hpp"
#include "lbench.hpp"
#include "lnast.hpp"
#include "likely.hpp"
#include "thread_pool.hpp"
#include "inou_graphviz.hpp"
#include "lnast_tolg.hpp"
#include "cprop.hpp"
#include "gioc.hpp"
#include "bitwidth.hpp"
#include "firmap.hpp"

// FIXME->sh: centralized bwmap won't work in the multi-threaded compilation, mimic the new
//            firbits/map design
using BWMap_flat = absl::flat_hash_map<Node_pin::Compact_flat, Bitwidth_range>;
using BWMap_hier = absl::flat_hash_map<Node_pin::Compact,      Bitwidth_range>;


class Lcompiler {
private:
  const std::string_view path;  
  const std::string      odir; // FIXME->sh: why not use string_view?
  const std::string_view top;
  const bool             gviz;

  // FIXME->sh: centralized bwmap won't work in the multi-threaded compilation, mimic the new firbits/map design
  BWMap_flat global_flat_bwmap;
  BWMap_hier global_hier_bwmap;

  // firrtl only tables
  absl::node_hash_map<uint32_t, FBMap>    fbmaps;        // Lg_type_id -> fbmap
  absl::node_hash_map<uint32_t, PinMap>   pinmaps;       // Lg_type_id -> pinmap
  absl::node_hash_map<uint32_t, XorrMap>  spinmaps_xorr; // Lg_type_id -> spinmaps

  Graphviz gv;

protected:
  std::mutex lgs_mutex;
  std::vector<LGraph *> lgs;

public:
  Lcompiler(std::string_view path, std::string_view odir, std::string_view top, bool gviz);

  void local_bitwidth_inference();
  void global_io_connection();
  void global_bitwidth_inference();
  void add_pyrope_thread(std::shared_ptr<Lnast> lnast);

  void do_prp_lnast2lgraph(std::vector<std::shared_ptr<Lnast>>);
  void do_local_cprop_bitwidth();
  void prp_thread_ln2lg(std::shared_ptr<Lnast> lnast);
  void prp_thread_local_cprop_bitwidth(LGraph *lg, Cprop &cp, Bitwidth &bw);


  void do_fir_lnast2lgraph(std::vector<std::shared_ptr<Lnast>>);
  void do_cprop();
  void do_firbits();
  void do_firmap_bitwidth();
  void fir_thread_ln2lg(std::shared_ptr<Lnast> lnast);
  void fir_thread_cprop(LGraph* lg, Cprop &cp);
  void fir_thread_firmap_bw(LGraph *lg, Bitwidth &bw, std::vector<LGraph*> &mapped_lgs);
  
  std::string_view get_top() { return top; };
  std::vector<LGraph *> get_lgraphs() {
    return lgs;
  }


  std::vector<LGraph *> wait_all() {
    thread_pool.wait_all();
    return lgs;
  }
};

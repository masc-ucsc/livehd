// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <mutex>
#include <vector>

#include "absl/container/node_hash_map.h"
#include "bitwidth.hpp"
#include "bitwidth_range.hpp"
#include "cprop.hpp"
#include "firmap.hpp"
#include "inou_graphviz.hpp"
#include "lbench.hpp"
#include "lgedgeiter.hpp"
#include "lgraph.hpp"
#include "likely.hpp"
#include "lnast.hpp"
#include "lnast_tolg.hpp"
#include "thread_pool.hpp"

class Lcompiler {
private:
  mmap_lib::str path;
  mmap_lib::str odir;  
  mmap_lib::str top;
  const bool             gviz;

  // firrtl only tables
  absl::node_hash_map<Lgraph *, FBMap>   fbmaps;         // Lg_type_id -> fbmap
  absl::node_hash_map<Lgraph *, PinMap>  pinmaps;        // Lg_type_id -> pinmap
  absl::node_hash_map<Lgraph *, XorrMap> spinmaps_xorr;  // Lg_type_id -> spinmaps

  Graphviz gv;

  void setup_maps();

protected:
  std::mutex            lgs_mutex;
  std::vector<Lgraph *> lgs;

public:
  Lcompiler(mmap_lib::str path, mmap_lib::str odir, mmap_lib::str top, bool gviz);

  void do_prp_lnast2lgraph(const std::vector<std::shared_ptr<Lnast>> &);
  void do_prp_local_cprop_bitwidth();
  void do_prp_global_bitwidth_inference();
  void prp_thread_ln2lg(const std::shared_ptr<Lnast> &lnast);

  void do_fir_lnast2lgraph(const std::vector<std::shared_ptr<Lnast>> &);
  void do_fir_cprop();
  void do_fir_firbits();
  void do_fir_firmap_bitwidth();
  void fir_thread_cprop(Lgraph *lg);
  void fir_thread_ln2lg(const std::shared_ptr<Lnast> &lnast);

  mmap_lib::str    get_top() { return top; };
  std::vector<Lgraph *> get_lgraphs() { return lgs; }

  std::vector<Lgraph *> wait_all() {
    thread_pool.wait_all();
    return lgs;
  }
};

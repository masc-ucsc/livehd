// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <bits/stdint-uintn.h>
#include <mutex>
#include <vector>

#include "lgedgeiter.hpp"
#include "bitwidth_range.hpp"
#include "lgraph.hpp"
#include "lbench.hpp"
#include "lnast.hpp"
#include "likely.hpp"
#include "thread_pool.hpp"

using BWMap_flat = absl::flat_hash_map<Node_pin::Compact_flat, Bitwidth_range>;
using BWMap_hier = absl::flat_hash_map<Node_pin::Compact,      Bitwidth_range>;

class Lcompiler {
private:
  const std::string_view path;  
  const std::string      odir; // FIXME->sh: why not use string_view?
  const std::string_view top;
  const bool             gviz;
  uint8_t                threads;
  BWMap_flat global_flat_bwmap;
  BWMap_hier global_hier_bwmap;

protected:
  std::mutex lgs_mutex;
  std::vector<LGraph *> lgs;

  void compile_thread(std::shared_ptr<Lnast> ln);
  void compile_thread(std::string_view file); // future allow to call inou.pyrope or inou.verilog or comp error

public:
  Lcompiler(std::string_view path, std::string_view odir, std::string_view top, bool gviz);

  void local_bitwidth_inference();
  void global_io_connection();
  void global_bitwidth_inference();
  void bottom_up_firbits_analysis_map();
  void add_pyrope_thread(std::shared_ptr<Lnast> lnast);
  void fir_thread_ln2lg_cprop(std::shared_ptr<Lnast> lnast);
  
  std::string_view get_top() { return top; };



  std::vector<LGraph *> wait_all() {
    thread_pool.wait_all();
    return lgs;
  }
};

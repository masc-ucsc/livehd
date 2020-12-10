// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <mutex>
#include <vector>

#include "lgedgeiter.hpp"
#include "bitwidth_range.hpp"
#include "lgraph.hpp"
#include "lbench.hpp"
#include "lnast.hpp"
#include "likely.hpp"

using BWMap = absl::flat_hash_map<Node_pin::Compact, Bitwidth_range>;

class Lcompiler {
private:
  const std::string_view path;  
  const std::string odir;
  const bool gviz;
  BWMap global_bwmap;

protected:
  std::mutex lgs_mutex;
  std::vector<LGraph *> lgs;

  void compile_thread(std::shared_ptr<Lnast> ln);
  void compile_thread(std::string_view file); // future allow to call inou.pyrope or inou.verilog or comp error
  void add_thread(std::shared_ptr<Lnast> lnast, bool is_firrtl);

public:
  Lcompiler(std::string_view path, std::string_view odir, bool gviz);

  void add(std::shared_ptr<Lnast> lnast, bool is_firrtl);
  void global_io_connection();
  void global_bitwidth_inference(std::string_view top);
  void global_firrtl_bits_analysis_map(std::string_view top);
  /* void add(std::string_view file); */

  std::vector<LGraph *> wait_all();
};

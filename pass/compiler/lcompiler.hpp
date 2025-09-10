// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <mutex>
#include <vector>

#include "absl/container/node_hash_map.h"
#include "bitwidth.hpp"
#include "bitwidth_range.hpp"
#include "cprop.hpp"
#include "inou_graphviz.hpp"
#include "lgedgeiter.hpp"
#include "lgraph.hpp"
#include "likely.hpp"
#include "lnast.hpp"
#include "lnast_tolg.hpp"
#include "thread_pool.hpp"

class Lcompiler {
private:
  std::string path;
  std::string odir;
  std::string top;
  const bool  gviz;

  Graphviz gv;

  void setup_maps();

protected:
  std::mutex            lgs_mutex;
  std::vector<Lgraph *> lgs;

public:
  Lcompiler(std::string_view path, std::string_view odir, std::string_view top, bool gviz);

  void do_prp_lnast2lgraph(const std::vector<std::shared_ptr<Lnast>> &);
  void do_prp_local_cprop_bitwidth();
  void do_prp_global_bitwidth_inference();
  void prp_thread_ln2lg(const std::shared_ptr<Lnast> &lnast);

  std::string_view             get_top() const { return top; };
  const std::vector<Lgraph *> &get_lgraphs() const { return lgs; }

  std::vector<Lgraph *> wait_all() {
    thread_pool.wait_all();
    return lgs;
  }
};

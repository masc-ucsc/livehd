// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "lcompiler.hpp"

Lcompiler::Lcompiler(std::string_view _path) : path(_path) {}


void Lcompiler::add_thread(std::shared_ptr<Lnast> ln) {
  ln->ssa_trans();


  auto module_name = ln->get_top_module_name();
  Lnast_tolg tolg(module_name, path);

  const auto top = ln->get_root();
  const auto top_stmts = ln->get_first_child(top);
  auto local_lgs = tolg.do_tolg(ln, top_stmts);
#ifndef NDEBUG
  Inou_graphviz gviz();
#endif

  // FIXME: add the cprop and bitwidth calls
/* retry: */
/*   Pass_cprop cprop; */
/*   Pass_bitwidth bw; */
/*   cprop.trans(lg); */
/*   bw.trans(lg); */
/*   if (cprop.tup_get_left || bw.not_finished) { */
/*     if (cprop.made_progress || bw.made_progress) */
/*       goto retry; */
/*   } */

  std::lock_guard<std::mutex> guard(lgs_mutex);

  for(auto *lg:local_lgs)
    lgs.emplace_back(lg);
}

void Lcompiler::add(std::shared_ptr<Lnast> ln) {
  //thread_pool.add(Lcompiler::add_thread, this, ln);
  add_thread(ln);
}


std::vector<LGraph *> Lcompiler::wait_all() {
  /* thread_pool.wait_all(); */
  return lgs;
}


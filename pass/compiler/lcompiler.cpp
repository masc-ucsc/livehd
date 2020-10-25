// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "lcompiler.hpp"
#include "inou_graphviz.hpp"
#include "lnast_tolg.hpp"
#include "cprop.hpp"
#include "bitwidth.hpp"

Lcompiler::Lcompiler(std::string_view _path, std::string_view _odir, bool _gviz) 
  : path(_path), odir(_odir), gviz(_gviz) {}


void Lcompiler::add_thread(std::shared_ptr<Lnast> ln) {
  Graphviz gv(true, false, odir);
  if (gviz) 
    gv.do_from_lnast(ln, "raw"); // rename dot with postfix raw
  
  fmt::print("------------------------ Pyrope -> LNAST-SSA ------------------------ (1)\n");

  ln->ssa_trans();

  if (gviz) 
    gv.do_from_lnast(ln);

  fmt::print("------------------------ LNAST-> LGraph ----------------------------- (2)\n");

  auto module_name = ln->get_top_module_name();
  Lnast_tolg ln2lg(module_name, path);

  const auto top = ln->get_root();
  const auto top_stmts = ln->get_first_child(top);
  auto local_lgs = ln2lg.do_tolg(ln, top_stmts);
  if (gviz) {
    for (const auto &lg : local_lgs) 
      gv.do_from_lgraph(lg, "raw"); // rename dot with postfix raw
  }


  for (const auto &lg : local_lgs) {
    retry:
    Cprop cp(false); //hier = false
    Bitwidth bw(false, 10); // hier = false, max_iters = 10

    fmt::print("------------------------ Copy-Propagation --------------------------- (3)\n");
    cp.do_trans(lg);
    cp.do_trans(lg);
    cp.do_trans(lg);
    cp.do_trans(lg);
    if (gviz) 
      gv.do_from_lgraph(lg, "no_bits"); // rename dot with postfix raw
    

    fmt::print("------------------------ Bitwidth-Inference ------------------------- (4)\n");
    bw.do_trans(lg);
    bw.do_trans(lg);

    fmt::print("------------------------ Copy-Propagation --------------------------- (5)\n");
    cp.do_trans(lg);

    fmt::print("------------------------ Bitwidth-Inference ------------------------- (6)\n");
    bw.do_trans(lg);

    fmt::print("------------------------ Final Copy-Propagation --------------------- (7)\n");
    cp.do_trans(lg);

    if (gviz) 
      gv.do_from_lgraph(lg); // rename dot with postfix raw
    

    // FIXEME:sh -> todo 
    /* if (cprop.tup_get_left || bw.not_finished) { */
    /*   if (cprop.made_progress || bw.made_progress) */
    /*     goto retry; */
    /* } */

    /* if (cp.tup_get_left) { */
    /*   if (cp.made_progress) { */
    /*     goto retry; */
    /*   } */
    /* } */
  }

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


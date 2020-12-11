// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "lcompiler.hpp"
#include "inou_graphviz.hpp"
#include "lnast_tolg.hpp"
#include "cprop.hpp"
#include "gioc.hpp"
#include "bitwidth.hpp"
#include "firmap.hpp"

Lcompiler::Lcompiler(std::string_view _path, std::string_view _odir, std::string_view _top, bool _gviz) 
  : path(_path), odir(_odir), top(_top), gviz(_gviz) {}


void Lcompiler::add_pyrope(std::shared_ptr<Lnast> ln) {
  //thread_pool.add(Lcompiler::add_thread, this, ln);
  add_pyrope_thread(ln);
}

void Lcompiler::add_pyrope_thread(std::shared_ptr<Lnast> ln) {
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

  const auto lnidx_top = ln->get_root();
  const auto top_stmts = ln->get_first_child(lnidx_top);
  auto local_lgs = ln2lg.do_tolg(ln, top_stmts);
  if (gviz) {
    for (const auto &lg : local_lgs) 
      gv.do_from_lgraph(lg, "local.raw"); // rename dot with postfix raw
  }


  for (const auto &lg : local_lgs) {
    /* retry: */
    Cprop    cp(false, false);  // hier = false, gioc = false
    Bitwidth bw(false, 10, global_bwmap);     // hier = false, max_iters = 10

    fmt::print("------------------------ Copy-Propagation --------------------------- (3)\n");
    cp.do_trans(lg);
    if (gviz) 
      gv.do_from_lgraph(lg, "local.no_bits"); // rename dot with postfix raw
    

    fmt::print("------------------------ Bitwidth-Inference ------------------------- (4)\n");
    bw.do_trans(lg);
    if (gviz) 
      gv.do_from_lgraph(lg, "local.debug0"); // rename dot with postfix raw

    fmt::print("------------------------ Bitwidth-Inference ------------------------- (5)\n");
    bw.do_trans(lg);
    if (gviz) 
      gv.do_from_lgraph(lg, "local.debug1"); // rename dot with postfix raw

    fmt::print("------------------------ Bitwidth-Inference ------------------------- (6)\n");
    bw.do_trans(lg);

    if (gviz) 
      gv.do_from_lgraph(lg, "local"); // rename dot with postfix raw

    // FIXEME:sh -> todo 
    /* if (cp.get_tuple_get_left()) { */
    /*   if (cp.made_progress) { */
    /*     goto retry; */
    /*   } */
    /* } */
  }

  std::lock_guard<std::mutex> guard(lgs_mutex);

  for(auto *lg:local_lgs)
    lgs.emplace_back(lg);
}


void Lcompiler::add_firrtl(std::shared_ptr<Lnast> ln) {
  //thread_pool.add(Lcompiler::add_thread, this, ln);
  add_firrtl_thread(ln);
}


void Lcompiler::add_firrtl_thread(std::shared_ptr<Lnast> ln) {
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

  const auto lnidx_top = ln->get_root();
  const auto top_stmts = ln->get_first_child(lnidx_top);
  auto local_lgs = ln2lg.do_tolg(ln, top_stmts);
  if (gviz) {
    for (const auto &lg : local_lgs) 
      gv.do_from_lgraph(lg, "local.raw"); // rename dot with postfix raw
  }


  for (const auto &lg : local_lgs) {
    /* retry: */
    Cprop    cp(false, false);  // hier = false, gioc = false
    Bitwidth bw(false, 10, global_bwmap);     // hier = false, max_iters = 10

    fmt::print("------------------------ Copy-Propagation --------------------------- (3)\n");
    cp.do_trans(lg);
    if (gviz) 
      gv.do_from_lgraph(lg, "local.no_bits"); // rename dot with postfix raw
  }

  std::lock_guard<std::mutex> guard(lgs_mutex);
  for(auto *lg:local_lgs)
    lgs.emplace_back(lg);
}



void Lcompiler::global_io_connection() {
  Graphviz gv(true,  false, odir);
  Cprop    cp(false, true);               // hier = false, at_gioc = true 
  Bitwidth bw(true, 10, global_bwmap);    // hier = false, max_iters = 10
  Gioc     gioc(path);

  for (auto &lg : lgs) {
    fmt::print("LGraph name:{}\n", lg->get_name());
    fmt::print("------------------------ Global IO Connection ----------------------- (7)\n");
    gioc.do_trans(lg);
    if (gviz) 
      gv.do_from_lgraph(lg, "gioc.raw"); // rename dot with postfix raw
    fmt::print("------------------------ Copy-Propagation --------------------------- (8)\n");
    cp.do_trans(lg);
    if (gviz) 
      gv.do_from_lgraph(lg, "gioc.no_bits"); // rename dot with postfix raw
  }
}

void Lcompiler::global_firrtl_bits_analysis_map() {
  Graphviz gv(true, false, odir);
  Firmap fm(true);   // hier = true

  auto lgcnt = 0;
  auto hit = false;
  for (auto &lg : lgs) {
    ++lgcnt;
    if (lg->get_name() == top) {
      hit = true;
      fmt::print("------------------------ Firrtl Bits Analysis ------------------------- (9)\n");
      fm.do_analysis(lg);
    }

    if (gviz) 
      gv.do_from_lgraph(lg, "gioc.firbits"); // rename dot with postfix raw
  }

  if (lgcnt > 1 && hit == false) 
    Pass::error("Top module not specified for firrtl codes!\n");
}

void Lcompiler::global_bitwidth_inference() {
  Graphviz gv(true, false, odir);
  Bitwidth bw(true, 10, global_bwmap);   // hier = true, max_iters = 10

  auto lgcnt = 0;
  auto hit = false;
  for (auto &lg : lgs) {
    ++lgcnt;
    if (lg->get_name() == top) {
      hit = true;
      fmt::print("------------------------ Bitwidth-Inference ------------------------- (A)\n");
      bw.do_trans(lg);
    }

    if (gviz) 
      gv.do_from_lgraph(lg, ""); // rename dot with postfix raw
  }

  if (lgcnt > 1 && hit == false) {
    Pass::error("Top module not specified from multiple Pyrope source codes!\n");
  }
}


std::vector<LGraph *> Lcompiler::wait_all() {
  /* thread_pool.wait_all(); */
  return lgs;
}


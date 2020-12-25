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


void Lcompiler::add_pyrope_thread(std::shared_ptr<Lnast> ln) {
  Graphviz gv(true, false, odir); 
  gviz ? gv.do_from_lnast(ln, "raw") : void(); 
  
  fmt::print("------------------------ Pyrope -> LNAST-SSA ------------------------ \n");
  ln->ssa_trans();
  gviz ? gv.do_from_lnast(ln) : void();


  fmt::print("------------------------ LNAST-> LGraph ----------------------------- \n");
  auto module_name = ln->get_top_module_name();
  Lnast_tolg ln2lg(module_name, path);

  const auto lnidx_top = ln->get_root();
  const auto top_stmts = ln->get_first_child(lnidx_top);
  auto local_lgs = ln2lg.do_tolg(ln, top_stmts);
  if (gviz) {
    for (const auto &lg : local_lgs) 
      gv.do_from_lgraph(lg, "local.raw"); 
  }

  Cprop    cp(false, false);                // hier = false, gioc = false
  Bitwidth bw(false, 10, global_bwmap);     // hier = false, max_iters = 10
  for (const auto &lg : local_lgs) {
    fmt::print("------------------------ Local Copy-Propagation ---------------------- (C-1)\n");
    cp.do_trans(lg);
    gviz ? gv.do_from_lgraph(lg, "local.no_bits") : void();

    fmt::print("------------------------ Local Bitwidth-Inference -------------------- (B-1)\n");
    bw.do_trans(lg);
    gviz ? gv.do_from_lgraph(lg, "local.debug0") : void(); 

    fmt::print("------------------------ Local Bitwidth-Inference -------------------- (B-2)\n");
    bw.do_trans(lg);
    gviz ? gv.do_from_lgraph(lg, "local.debug1") : void(); 

    fmt::print("------------------------ Local Bitwidth-Inference -------------------- (B-3)\n");
    bw.do_trans(lg);
    gviz ? gv.do_from_lgraph(lg, "local") : void(); 
  }

  std::lock_guard<std::mutex> guard(lgs_mutex);

  for(auto *lg:local_lgs)
    lgs.emplace_back(lg);
}


void Lcompiler::add_firrtl_thread(std::shared_ptr<Lnast> ln) {
  Graphviz gv(true, false, odir); 
  gviz ? gv.do_from_lnast(ln, "raw") : void(); 
  
  fmt::print("---------------- Firrtl_Protobuf -> LNAST-SSA ({}) ------- (LN-1)\n", absl::StrCat(ln->get_top_module_name(), "_firrtl"));
  ln->ssa_trans();
  gviz ? gv.do_from_lnast(ln) : void();


  fmt::print("---------------- LNAST-> LGraph ({}) --------------------- (LN-2)\n", absl::StrCat(ln->get_top_module_name(), "_firrtl"));
  // note: since the first generated lgraphs are firrtl_op_lgs, they will be removed in the end,
  // we should keep the original module_name for the firrtl_op mapped lgraph, so here I attached
  // "_firrtl" postfix for the firrtl_op_lgs
  auto module_name = absl::StrCat(ln->get_top_module_name(), "_firrtl");
  Lnast_tolg ln2lg(module_name, path);

  const auto lnidx_top = ln->get_root();
  const auto top_stmts = ln->get_first_child(lnidx_top);
  auto local_lgs = ln2lg.do_tolg(ln, top_stmts);
  if (gviz) {
    for (const auto &lg : local_lgs) 
      gv.do_from_lgraph(lg, "local.raw"); 
  }


  for (const auto &lg : local_lgs) {
    Cprop    cp(false, false);  // hier = false, gioc = false

    fmt::print("---------------- Copy-Propagation ({}) ------------------- (C-1)\n", lg->get_name());
    cp.do_trans(lg);
    gviz ? gv.do_from_lgraph(lg, "local.no_bits") : void();
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
    fmt::print("---------------- Global IO Connection ({}) --------------- (GIOC)\n", lg->get_name());
    gioc.do_trans(lg);
    gviz ? gv.do_from_lgraph(lg, "gioc.raw") : void(); 
    fmt::print("---------------- Global Copy-Propagation ({}) ------------ (GC)\n", lg->get_name());
    cp.do_trans(lg);
    gviz ? gv.do_from_lgraph(lg, "gioc.no_bits") : void();
  }
}


void Lcompiler::global_firrtl_bits_analysis_map() {
  Graphviz gv(true, false, odir);
  Firmap   fm;   

  auto lgcnt = 0;
  auto hit = false;
  auto top_name_before_mapping = absl::StrCat(top, "_firrtl");

  // hierarchical traversal
  for (auto &lg : lgs) {
    ++lgcnt;
    if (lg->get_name() == top_name_before_mapping) {
      hit = true;
      fmt::print("---------------- Firrtl Bits Analysis ({}) --------------- (F-1)\n", lg->get_name());
      fm.do_firbits_analysis(lg);
      fmt::print("---------------- Firrtl Bits Analysis ({}) --------------- (F-2)\n", lg->get_name());
      fm.do_firbits_analysis(lg);
    }
    gviz ? gv.do_from_lgraph(lg, "gioc.firbits") : void(); 
  }

  if (lgcnt > 1 && hit == false) 
    Pass::error("Top module not specified for firrtl codes!\n");

  std::vector<LGraph*> mapped_lgs;
  for (auto &lg : lgs) {
    fmt::print("---------------- Firrtl Op Mapping ({}) --------------- (F-3)\n", lg->get_name());
    auto new_lg = fm.do_firrtl_mapping(lg);
    mapped_lgs.emplace_back(new_lg);
  }


  lgs = mapped_lgs;
  for (auto &lg : lgs) {
    gviz ? gv.do_from_lgraph(lg, "gioc.firmap") : void(); 
  }
}


void Lcompiler::local_bitwidth_inference() {
  Graphviz gv(true, false, odir); 
  Bitwidth bw(false, 10, global_bwmap);     // hier = false, max_iters = 10
  for (auto &lg: lgs) {
    fmt::print("---------------- Local Bitwidth-Inference ({}) ----------- (B-1)\n", lg->get_name());
    bw.do_trans(lg);
    gviz ? gv.do_from_lgraph(lg, "local.debug0") : void(); 


    fmt::print("---------------- Local Bitwidth-Inference ({}) ----------- (B-2)\n", lg->get_name());
    bw.do_trans(lg);
    gviz ? gv.do_from_lgraph(lg, "local.debug1") : void(); 


    fmt::print("---------------- Local Bitwidth-Inference ({}) ----------- (B-3)\n", lg->get_name());
    bw.do_trans(lg);
    gviz ? gv.do_from_lgraph(lg, "local") : void(); 
  }
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
      fmt::print("---------------- Global Bitwidth-Inference ({}) ----------------- (GB)\n", lg->get_name());
      bw.do_trans(lg);
    }

    gviz ? gv.do_from_lgraph(lg, "") : void();
  }

  if (lgcnt > 1 && hit == false) 
    Pass::error("Top module not specified from multiple Pyrope source codes!\n");
}

std::vector<LGraph *> Lcompiler::wait_all() {
  /* thread_pool.wait_all(); */
  return lgs;
}


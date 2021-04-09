// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "lcompiler.hpp"

#include <bits/stdint-uintn.h>

Lcompiler::Lcompiler(std::string_view _path, std::string_view _odir, std::string_view _top, bool _gviz)
    : path(_path), odir(_odir), top(_top), gviz(_gviz), gv(true, false, _odir) {}

void Lcompiler::do_prp_lnast2lgraph(std::vector<std::shared_ptr<Lnast>> lnasts) {
  for (const auto &ln : lnasts) {
    thread_pool.add(&Lcompiler::prp_thread_ln2lg, this, ln);
  }
  thread_pool.wait_all();
}

void Lcompiler::prp_thread_ln2lg(std::shared_ptr<Lnast> ln) {
  gviz ? gv.do_from_lnast(ln, "raw") : void();

  fmt::print("------------------------ Pyrope -> LNAST-SSA ({})------------------------ \n", ln->get_top_module_name());
  ln->ssa_trans();
  gviz ? gv.do_from_lnast(ln) : void();

  fmt::print("------------------------ LNAST-> Lgraph ({})----------------------------- \n", ln->get_top_module_name());

  auto module_name = ln->get_top_module_name();

  Lnast_tolg ln2lg(module_name, path);

  const auto lnidx_top = ln->get_root();
  const auto top_stmts = ln->get_first_child(lnidx_top);

  auto local_lgs = ln2lg.do_tolg(ln, top_stmts);

  if (gviz) {
    for (const auto &lg : local_lgs) gv.do_from_lgraph(lg, "local.raw");
  }

  // FIXME->sh: DEBUG: cannot separate to do_local_cprop_bitwidth(), why?
  Cprop cp(false, false);                                        // hier = false, gioc = false
  Bitwidth bw(false, 10, global_flat_bwmap, global_hier_bwmap);  // hier = false, max_iters = 10
  for (const auto &lg : local_lgs) {
    thread_pool.add(&Lcompiler::prp_thread_local_cprop_bitwidth, this, lg, cp, bw);
  }
  thread_pool.wait_all();

  std::lock_guard<std::mutex>guard(lgs_mutex);  // guarding Lcompiler::lgs
  for (auto *lg : local_lgs) lgs.emplace_back(lg);
}

// TODO: try to make it work
void Lcompiler::do_local_cprop_bitwidth() {
  Cprop    cp(false, false);                                     // hier = false, gioc = false
  Bitwidth bw(false, 10, global_flat_bwmap, global_hier_bwmap);  // hier = false, max_iters = 10
  for (const auto &lg : lgs) {
    thread_pool.add(&Lcompiler::prp_thread_local_cprop_bitwidth, this, lg, cp, bw);
  }
  thread_pool.wait_all();
}

void Lcompiler::prp_thread_local_cprop_bitwidth(Lgraph *lg, Cprop &cp, Bitwidth &bw) {
  fmt::print("------------------------ Local Copy-Propagation ({})---------------------- (C-0)\n", lg->get_name());
  cp.do_trans(lg);
  gviz ? gv.do_from_lgraph(lg, "local.cprop-ed") : void();

  fmt::print("------------------------ Local Bitwidth-Inference ({})-------------------- (B-0)\n", lg->get_name());
  bw.do_trans(lg);
  gviz ? gv.do_from_lgraph(lg, "local.bitwidth-ed-0") : void();

  fmt::print("------------------------ Local Bitwidth-Inference ({})-------------------- (B-1)\n", lg->get_name());
  bw.do_trans(lg);
  gviz ? gv.do_from_lgraph(lg, "local.bitwidth-ed-1") : void();
}

void Lcompiler::add_pyrope_thread(std::shared_ptr<Lnast> ln) {
  gviz ? gv.do_from_lnast(ln, "raw") : void();

  fmt::print("------------------------ Pyrope -> LNAST-SSA ({})------------------------ \n", ln->get_top_module_name());
  ln->ssa_trans();
  gviz ? gv.do_from_lnast(ln) : void();

  fmt::print("------------------------ LNAST-> Lgraph ({})----------------------------- \n", ln->get_top_module_name());

  auto module_name = ln->get_top_module_name();

  Lnast_tolg ln2lg(module_name, path);

  const auto lnidx_top = ln->get_root();
  const auto top_stmts = ln->get_first_child(lnidx_top);

  auto local_lgs = ln2lg.do_tolg(ln, top_stmts);

  if (gviz) {
    for (const auto &lg : local_lgs) gv.do_from_lgraph(lg, "local.raw");
  }

  Cprop cp(false, false);                                     // hier = false, gioc = false
  Bitwidth bw(false, 10, global_flat_bwmap, global_hier_bwmap);  // hier = false, max_iters = 10
  for (const auto &lg : local_lgs) {
    fmt::print("------------------------ Local Copy-Propagation ({})---------------------- (C-0)\n", lg->get_name());
    cp.do_trans(lg);
    gviz ? gv.do_from_lgraph(lg, "local.cprop-ed") : void();

    fmt::print("------------------------ Local Bitwidth-Inference ({})-------------------- (B-0)\n", lg->get_name());
    bw.do_trans(lg);
    gviz ? gv.do_from_lgraph(lg, "local.bitwidth-ed-0") : void();

    fmt::print("------------------------ Local Bitwidth-Inference ({})-------------------- (B-1)\n", lg->get_name());
    bw.do_trans(lg);
    gviz ? gv.do_from_lgraph(lg, "local.bitwidth-ed-1") : void();
  }

  std::lock_guard<std::mutex>guard(lgs_mutex);

  for (auto *lg : local_lgs) lgs.emplace_back(lg);
}

void Lcompiler::do_fir_lnast2lgraph(std::vector<std::shared_ptr<Lnast>> lnasts) {
  for (const auto &ln : lnasts) {
    thread_pool.add(&Lcompiler::fir_thread_ln2lg, this, ln);
  }
  thread_pool.wait_all();
  // Graph_library::sync_all();
}

void Lcompiler::fir_thread_ln2lg(std::shared_ptr<Lnast> ln) {
  gviz ? gv.do_from_lnast(ln, "raw") : void();

  fmt::print("---------------- Firrtl_Protobuf -> LNAST-SSA ({}) ------- (LN-0)\n",
             absl::StrCat("__firrtl_", ln->get_top_module_name()));
  ln->ssa_trans();
  gviz ? gv.do_from_lnast(ln) : void();

  // note: since the first generated lgraphs are firrtl_op_lgs, they will be removed in the end,
  // we should keep the original module_name for the firrtl_op mapped lgraph, so here I attached
  // "__firrtl_" prefix for the firrtl_op_lgs
  fmt::print("---------------- LNAST-> Lgraph ({}) --------------------- (LN-1)\n",
             absl::StrCat("__firrtl_", ln->get_top_module_name()));

  auto module_name = absl::StrCat("__firrtl_", ln->get_top_module_name());

  Lnast_tolg ln2lg(module_name, path);

  auto lnidx_top = ln->get_root();
  auto top_stmts = ln->get_first_child(lnidx_top);
  auto local_lgs = ln2lg.do_tolg(ln, top_stmts);

  if (gviz) {
    for (const auto &lg : local_lgs) gv.do_from_lgraph(lg, "local.raw");
  }

  std::lock_guard<std::mutex>guard(lgs_mutex);  // guarding Lcompiler::lgs
  for (auto *lg : local_lgs) {
    lgs.emplace_back(lg);
  }
}

void Lcompiler::do_cprop() {
  Cprop cp(false, false);  // hier = false, gioc = false
  auto  lgcnt                   = 0;
  auto  hit                     = false;
  auto  top_name_before_mapping = absl::StrCat("__firrtl_", top);

  // hierarchical traversal
  for (auto &lg : lgs) {
    ++lgcnt;
    // bottom up approach to parallelly do cprop
    if (lg->get_name() == top_name_before_mapping) {
      hit = true;
      lg->each_hier_unique_sub_bottom_up_parallel([this, &cp](Lgraph *lg_sub) {
        fmt::print("---------------- Copy-Propagation ({}) ------------------- (C-0)\n", lg_sub->get_name());
        cp.do_trans(lg_sub);
        // fmt::print("---------------- Copy-Propagation ({}) ------------------- (C-1)\n", lg_sub->get_name());
        // cp.do_trans(lg_sub);
        gviz ? gv.do_from_lgraph(lg_sub, "local.cprop-ed") : void();
      });

      // for top lgraph
      fmt::print("---------------- Copy-Propagation ({}) ------------------- (C-0)\n", lg->get_name());
      cp.do_trans(lg);
      // fmt::print("---------------- Copy-Propagation ({}) ------------------- (C-1)\n", lg->get_name());
      // cp.do_trans(lg);
      gviz ? gv.do_from_lgraph(lg, "local.cprop-ed") : void();
    }
  }

  if (lgcnt > 1 && hit == false)
    Pass::error("Top module not specified for firrtl codes!\n");

  // for (const auto &lg : lgs) {
  //   thread_pool.add(&Lcompiler::fir_thread_cprop, this, lg, cp);
  // }
  // thread_pool.wait_all();
}

// FIXME->sh: to be deprecated by bottom-up paralellism
void Lcompiler::fir_thread_cprop(Lgraph *lg, Cprop &cp) {
  fmt::print("---------------- Copy-Propagation ({}) ------------------- (C-0)\n", lg->get_name());
  cp.do_trans(lg);
  gviz ? gv.do_from_lgraph(lg, "local.cprop-ed") : void();
}

void Lcompiler::do_firmap_bitwidth() {
  // map __firrtl_foo.lg to foo.lg
  std::vector<Lgraph *> mapped_lgs;
  Bitwidth              bw(false, 10, global_flat_bwmap, global_hier_bwmap);  // hier = false, max_iters = 10
  for (auto &lg : lgs) {
    thread_pool.add(&Lcompiler::fir_thread_firmap_bw, this, lg, bw, mapped_lgs);
  }
  lgs = mapped_lgs;
}

void Lcompiler::fir_thread_firmap_bw(Lgraph *lg, Bitwidth &bw, std::vector<Lgraph *> &mapped_lgs) {
  Firmap fm(fbmaps, pinmaps, spinmaps_xorr);
  fmt::print("---------------- Firrtl Op Mapping ({}) --------------- (F-2)\n", lg->get_name());
  auto new_lg = fm.do_firrtl_mapping(lg);
  gviz ? gv.do_from_lgraph(new_lg, "gioc.firmap-ed") : void();

  fmt::print("---------------- Local Bitwidth-Inference ({}) ----------- (B-0)\n", new_lg->get_name());
  bw.do_trans(new_lg);
  fmt::print("---------------- Local Bitwidth-Inference ({}) ----------- (B-1)\n", new_lg->get_name());
  bw.do_trans(new_lg);
  
  gviz ? gv.do_from_lgraph(new_lg, "") : void();

  mapped_lgs.emplace_back(new_lg);
}

// TODO: move to multithreaded later
void Lcompiler::do_firbits() {
  auto lgcnt                   = 0;
  auto hit                     = false;
  auto top_name_before_mapping = absl::StrCat("__firrtl_", top);

  // hierarchical traversal
  for (auto &lg : lgs) {
    ++lgcnt;
    // bottom up approach to parallelly analyze the firbits
    if (lg->get_name() == top_name_before_mapping) {
      hit = true;

      lg->each_hier_unique_sub_bottom_up_parallel([this](Lgraph *lg_sub) {
        Firmap fm(fbmaps, pinmaps, spinmaps_xorr);
        fmt::print("visiting lgraph name:{}\n", lg_sub->get_name());
        fmt::print("---------------- Firrtl Bits Analysis ({}) --------------- (F-0)\n", lg_sub->get_name());
        fm.do_firbits_analysis(lg_sub);
        fmt::print("---------------- Firrtl Bits Analysis ({}) --------------- (F-1)\n", lg_sub->get_name());
        fm.do_firbits_analysis(lg_sub);
        gviz ? gv.do_from_lgraph(lg_sub, "firbits-ed") : void();
      });

      // for top lgraph
      Firmap fm(fbmaps, pinmaps, spinmaps_xorr);
      fmt::print("---------------- Firrtl Bits Analysis ({}) --------------- (F-0)\n", lg->get_name());
      fm.do_firbits_analysis(lg);
      fmt::print("---------------- Firrtl Bits Analysis ({}) --------------- (F-1)\n", lg->get_name());
      fm.do_firbits_analysis(lg);
      gviz ? gv.do_from_lgraph(lg, "firbits-ed") : void();
    }
  }
  if (lgcnt > 1 && hit == false)
    Pass::error("Top module not specified for firrtl codes!\n");
}

void Lcompiler::global_io_connection() {
  Cprop    cp(false, true);                                     // hier = false, at_gioc = true
  Bitwidth bw(true, 10, global_flat_bwmap, global_hier_bwmap);  // hier = true,  max_iters = 10
  Gioc     gioc(path);

  for (auto &lg : lgs) {
    fmt::print("---------------- Global IO Connection ({}) --------------- (GIOC)\n", lg->get_name());
    gioc.do_trans(lg);
    gviz ? gv.do_from_lgraph(lg, "gioc.raw") : void();
    fmt::print("---------------- Global Copy-Propagation ({}) ------------ (GC)\n", lg->get_name());
    cp.do_trans(lg);
    gviz ? gv.do_from_lgraph(lg, "gioc.cprop-ed") : void();
  }
}

void Lcompiler::global_bitwidth_inference() {
  Bitwidth bw(true, 10, global_flat_bwmap, global_hier_bwmap);  // hier = true, max_iters = 10

  auto lgcnt = 0;
  auto hit   = false;
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

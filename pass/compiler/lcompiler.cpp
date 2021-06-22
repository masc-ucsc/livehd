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

  fmt::print("---------------- Pyrope -> LNAST-SSA ({})------------------- \n", ln->get_top_module_name());
  ln->ssa_trans();
  gviz ? gv.do_from_lnast(ln) : void();

  fmt::print("---------------- LNAST -> Lgraph ({})----------------------- \n", ln->get_top_module_name());

  auto module_name = ln->get_top_module_name();
  Lnast_tolg ln2lg(module_name, path);
  const auto top_stmts = ln->get_first_child(mmap_lib::Tree_index::root());
  auto local_lgs = ln2lg.do_tolg(ln, top_stmts);

  if (gviz) {
    for (const auto &lg : local_lgs) 
      gv.do_from_lgraph(lg, "raw");
  }
  
  std::lock_guard<std::mutex>guard(lgs_mutex);  // guarding Lcompiler::lgs
  for (auto *lg : local_lgs) 
    lgs.emplace_back(lg);
}


void Lcompiler::do_prp_local_cprop_bitwidth() {

  // for each lgraph, bottom up approach to parallelly do cprop->bw->cprop;
  // also use a visited table to avoid duplicated visitations 
  std::set<Lg_type_id> visited_lg_table;
  for (auto &lg : lgs) {
    {
      std::unique_lock<std::mutex> guard(lgs_mutex);
      auto lgid = lg->get_lgid();
      if (visited_lg_table.find(lgid) != visited_lg_table.end())
        continue;
      visited_lg_table.insert(lgid);
    }

    lg->each_hier_unique_sub_bottom_up_parallel([this, &visited_lg_table](Lgraph *lg_sub) {
      {
        std::unique_lock<std::mutex> guard(lgs_mutex);
        auto lgid_sub = lg_sub->get_lgid();
        if (visited_lg_table.find(lgid_sub) != visited_lg_table.end())
          return;
        visited_lg_table.insert(lgid_sub);
      }

      Bitwidth bw(false, 10);  
      Cprop    cp(false);      
      int n_iters = 0;
      while(true) {
        fmt::print("---------------- Copy-Propagation ({}) ------------------- (C-0)\n", lg_sub->get_name());
        cp.do_trans(lg_sub);
        gviz ? gv.do_from_lgraph(lg_sub, "cprop-ed") : void();

        fmt::print("---------------- Local Bitwidth-Inference ({}) ----------- (B-0)\n", lg_sub->get_name());
        bw.do_trans(lg_sub);
        gviz ? gv.do_from_lgraph(lg_sub, "bitwidth-ed") : void();
        if (bw.is_finished()) {
          break;
        }
        n_iters++;
        if (n_iters > 4) {
          Pass::error("graph {} could not converge bw/cprop in {} iterations", lg_sub->get_name(), n_iters);
        }
      }

#if 1 
      // FIXME: needed until pending patch
      // only hier_tuple2 capricious_bits capricious_bits2 capricious_bits4 need this extra cprop
      fmt::print("---------------- Copy-Propagation ({}) ------------------- (C-1)\n", lg_sub->get_name());
      cp.do_trans(lg_sub);
      gviz ? gv.do_from_lgraph(lg_sub, "cprop-ed") : void();
#endif

    });

    Bitwidth bw(false, 10);  // hier = false, max_iters = 10
    Cprop    cp(false);  // hier = false
    fmt::print("---------------- Copy-Propagation ({}) ------------------- (C-0)\n", lg->get_name());
    cp.do_trans(lg);
    gviz ? gv.do_from_lgraph(lg, "cprop-ed") : void();

    fmt::print("---------------- Local Bitwidth-Inference ({}) ----------- (B-0)\n", lg->get_name());
    bw.do_trans(lg);
    gviz ? gv.do_from_lgraph(lg, "bitwidth-ed") : void();

    // only hier_tuple2 capricious_bits capricious_bits2 capricious_bits4 need this extra cprop
    fmt::print("---------------- Copy-Propagation ({}) ------------------- (C-1)\n", lg->get_name());
    cp.do_trans(lg);
    gviz ? gv.do_from_lgraph(lg, "cprop-ed") : void();
  }
}


void Lcompiler::do_prp_global_bitwidth_inference() {
  Bitwidth bw(true, 10);  // hier = true, max_iters = 10

  auto lgcnt = 0;
  auto hit   = false;

  // todo: when move to the new compressed bottom-up tree paralellism, you don't need to specify top
  if (top == "") { // top will be specified only if it is a hierarchical design
    I(lgs.size() == 1);  
    for (auto &lg : lgs) {
      bw.do_trans(lg);
      gviz ? gv.do_from_lgraph(lg, "") : void();
    }
  } else {
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
}



// ----------------------- FIRRTL compilation functions start ----------------------------

void Lcompiler::do_fir_lnast2lgraph(std::vector<std::shared_ptr<Lnast>> lnasts) {
  for (const auto &ln : lnasts) {
    thread_pool.add(&Lcompiler::fir_thread_ln2lg, this, ln);
  }
  thread_pool.wait_all();
}


void Lcompiler::fir_thread_ln2lg(std::shared_ptr<Lnast> ln) {
  gviz ? gv.do_from_lnast(ln, "raw") : void();

  fmt::print("-------- {:<28} ({:<30}) -------- (LN-0)\n", "Firrtl_Protobuf -> LNAST-SSA", absl::StrCat("__firrtl_", ln->get_top_module_name()));
  ln->ssa_trans();
  gviz ? gv.do_from_lnast(ln) : void();

  // since the first generated lgraphs are firrtl_op_lgs, they will be removed
  // in the end, we should keep the original module_name for the firrtl_op
  // mapped lgraph, so here I attached "__firrtl_" prefix for the firrtl_op_lgs

  fmt::print("-------- {:<28} ({:<30}) -------- (LN-1)\n", "LNAST -> Lgraph", absl::StrCat("__firrtl_", ln->get_top_module_name()));
  auto module_name = absl::StrCat("__firrtl_", ln->get_top_module_name());

  Lnast_tolg ln2lg(module_name, path);

  auto top_stmts = ln->get_first_child(mmap_lib::Tree_index::root());
  auto local_lgs = ln2lg.do_tolg(ln, top_stmts);

  if (gviz) 
    for (const auto &lg : local_lgs) gv.do_from_lgraph(lg, "raw");
  

  std::lock_guard<std::mutex>guard(lgs_mutex);  // guarding Lcompiler::lgs
  for (auto *lg : local_lgs) 
    lgs.emplace_back(lg);
}


void Lcompiler::do_fir_cprop() {
  for (auto &lg : lgs) 
    thread_pool.add(&Lcompiler::fir_thread_cprop, this, lg);
  
  thread_pool.wait_all();
}

void Lcompiler::fir_thread_cprop(Lgraph* lg) {
  Cprop cp(false); 

  fmt::print("-------- {:<28} ({:<30}) -------- (C-0)\n", "Copy-Propagation", lg->get_name());
  cp.do_trans(lg);
  gviz ? gv.do_from_lgraph(lg, "cprop-ed") : void();
}



void Lcompiler::do_fir_firmap_bitwidth() {
  // map __firrtl_foo.lg to foo.lg
  std::vector<Lgraph *> mapped_lgs;
  auto  lgcnt = 0;
  auto  hit   = false;
  auto  top_name_before_firmap = absl::StrCat("__firrtl_", top);

  std::set<Lg_type_id> visited_lg_table;
  for (auto &lg : lgs) {
    ++lgcnt;
    // bottom up approach to parallelly do cprop, start from top. Note that since we could start from top in firrtl, the visitation will never duplicated
    if (lg->get_name() == top_name_before_firmap) {
      hit = true;
      // thread task already enqueued in the lambda each_hier_unique_sub_bottom_up_parallel()
      lg->each_hier_unique_sub_bottom_up_parallel([this, &mapped_lgs](Lgraph *lg_sub) {
        I(lg_sub->get_name().substr(0,6) != "__fir_");

        Firmap   fm(fbmaps, pinmaps, spinmaps_xorr);
        Bitwidth bw(false, 10);  

        fmt::print("-------- {:<28} ({:<30}) -------- (F-2)\n", "Firrtl Op Mapping", lg_sub->get_name());
        auto new_lg_sub = fm.do_firrtl_mapping(lg_sub);
        gviz ? gv.do_from_lgraph(new_lg_sub, "firmap-ed") : void();

        fmt::print("-------- {:<28} ({:<30}) -------- (B-0)\n", "Local Bitwidth-Inference", new_lg_sub->get_name());
        bw.do_trans(new_lg_sub);
        gviz ? gv.do_from_lgraph(new_lg_sub, "") : void();

        {
          std::unique_lock<std::mutex> guard(lgs_mutex);
          mapped_lgs.emplace_back(new_lg_sub);
        }
      });

      // for top lgraph
      Firmap   fm(fbmaps, pinmaps, spinmaps_xorr);
      Bitwidth bw(false, 10); 

      fmt::print("-------- {:<28} ({:<30}) -------- (F-2)\n", "Firrtl Op Mapping", lg->get_name());
      auto new_lg = fm.do_firrtl_mapping(lg);
      gviz ? gv.do_from_lgraph(new_lg, "firmap-ed") : void();

      fmt::print("-------- {:<28} ({:<30}) -------- (B-0)\n", "Local Bitwidth-Inference", new_lg->get_name());
      bw.do_trans(new_lg);
      gviz ? gv.do_from_lgraph(new_lg, "") : void();

      {
        std::unique_lock<std::mutex> guard(lgs_mutex);
        mapped_lgs.emplace_back(new_lg);
      }
    }
  }

  if (lgcnt > 1 && hit == false)
    Pass::error("Top module not specified for firrtl codes!\n");

  lgs = mapped_lgs;
}


void Lcompiler::do_fir_firbits() {
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
        fmt::print("-------- {:<28} ({:<30}) -------- (F-0)\n", "Firrtl Bits Analysis", lg_sub->get_name());
        fm.do_firbits_analysis(lg_sub);
        fmt::print("-------- {:<28} ({:<30}) -------- (F-1)\n", "Firrtl Bits Analysis", lg_sub->get_name());
        fm.do_firbits_analysis(lg_sub);
        gviz ? gv.do_from_lgraph(lg_sub, "firbits-ed") : void();
      });

      // for top lgraph
      Firmap fm(fbmaps, pinmaps, spinmaps_xorr);
      fmt::print("-------- {:<28} ({:<30}) -------- (F-0)\n", "Firrtl Bits Analysis", lg->get_name());
      fm.do_firbits_analysis(lg);
      fmt::print("-------- {:<28} ({:<30}) -------- (F-1)\n", "Firrtl Bits Analysis", lg->get_name());
      fm.do_firbits_analysis(lg);
      gviz ? gv.do_from_lgraph(lg, "firbits-ed") : void();
    }
  }
  if (lgcnt > 1 && hit == false)
    Pass::error("Top module not specified for firrtl codes!\n");
}



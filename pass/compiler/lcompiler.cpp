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

void Lcompiler::do_local_cprop_bitwidth() {
  Bitwidth bw(false, 10);  // hier = false, max_iters = 10
  Cprop    cp(false);  // hier = false

  // for each lgraph, bottom up approach to parallelly do cprop->bw->cprop;
  // also use a visited table to avoid duplicated visitations 
  std::set<Lg_type_id> visited_lg_table;
  for (auto &lg : lgs) {
    auto lgid = lg->get_lgid();
    if (visited_lg_table.find(lgid) != visited_lg_table.end())
      continue;

    visited_lg_table.insert(lgid);

    lg->each_hier_unique_sub_bottom_up_parallel([this, &cp, &bw, &visited_lg_table](Lgraph *lg_sub) {
      auto lgid_sub = lg_sub->get_lgid();
      if (visited_lg_table.find(lgid_sub) != visited_lg_table.end())
        return;
      visited_lg_table.insert(lgid_sub);

      fmt::print("---------------- Copy-Propagation ({}) ------------------- (C-0)\n", lg_sub->get_name());
      cp.do_trans(lg_sub);
      gviz ? gv.do_from_lgraph(lg_sub, "cprop-ed") : void();

      fmt::print("---------------- Local Bitwidth-Inference ({}) ----------- (B-0)\n", lg_sub->get_name());
      bw.do_trans(lg_sub);
      gviz ? gv.do_from_lgraph(lg_sub, "bitwidth-ed") : void();

      // only hier_tuple2 capricious_bits capricious_bits2 capricious_bits4 need this extra cprop
      fmt::print("---------------- Copy-Propagation ({}) ------------------- (C-1)\n", lg_sub->get_name());
      cp.do_trans(lg_sub);
      gviz ? gv.do_from_lgraph(lg_sub, "cprop-ed") : void();

    });

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



void Lcompiler::do_fir_lnast2lgraph(std::vector<std::shared_ptr<Lnast>> lnasts) {
  for (const auto &ln : lnasts) {
    thread_pool.add(&Lcompiler::fir_thread_ln2lg, this, ln);
  }
  thread_pool.wait_all();
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

  auto top_stmts = ln->get_first_child(mmap_lib::Tree_index::root());
  auto local_lgs = ln2lg.do_tolg(ln, top_stmts);

  if (gviz) {
    for (const auto &lg : local_lgs) gv.do_from_lgraph(lg, "raw");
  }

  std::lock_guard<std::mutex>guard(lgs_mutex);  // guarding Lcompiler::lgs
  for (auto *lg : local_lgs) {
    lgs.emplace_back(lg);
  }
}

void Lcompiler::do_cprop() {
  Cprop cp(false);  // hier = false
  auto  lgcnt                   = 0;
  auto  hit                     = false;
  auto  top_name_before_firmap = absl::StrCat("__firrtl_", top);

  // hierarchical traversal
  for (auto &lg : lgs) {
    ++lgcnt;
    // bottom up approach to parallelly do cprop
    if (lg->get_name() == top_name_before_firmap) {
      hit = true;
      // thread task already enqueued in the lambda each_hier_unique_sub_bottom_up_parallel()
      lg->each_hier_unique_sub_bottom_up_parallel([this, &cp](Lgraph *lg_sub) {
        fmt::print("---------------- Copy-Propagation ({}) ------------------- (C-0)\n", lg_sub->get_name());
        cp.do_trans(lg_sub);
        gviz ? gv.do_from_lgraph(lg_sub, "cprop-ed") : void();
      });

      // for top lgraph
      fmt::print("---------------- Copy-Propagation ({}) ------------------- (C-0)\n", lg->get_name());
      cp.do_trans(lg);
      gviz ? gv.do_from_lgraph(lg, "cprop-ed") : void();
    }
  }

  if (lgcnt > 1 && hit == false)
    Pass::error("Top module not specified for firrtl codes!\n");
}

// FIXME->sh: to be deprecated by bottom-up paralellism
void Lcompiler::fir_thread_cprop(Lgraph *lg, Cprop &cp) {
  fmt::print("---------------- Copy-Propagation ({}) ------------------- (C-0)\n", lg->get_name());
  cp.do_trans(lg);
  gviz ? gv.do_from_lgraph(lg, "cprop-ed") : void();
}




void Lcompiler::do_firmap_bitwidth() {
  // map __firrtl_foo.lg to foo.lg
  std::vector<Lgraph *> mapped_lgs;
  auto  lgcnt = 0;
  auto  hit   = false;
  auto  top_name_before_firmap = absl::StrCat("__firrtl_", top);
  auto  prefix_fir_op_str = "__fir_"; // create once for many comparation


  for (auto &lg : lgs) {
    ++lgcnt;
    // bottom up approach to parallelly do cprop
    if (lg->get_name() == top_name_before_firmap) {
      hit = true;
      // thread task already enqueued in the lambda each_hier_unique_sub_bottom_up_parallel()
      lg->each_hier_unique_sub_bottom_up_parallel([this, &mapped_lgs, &prefix_fir_op_str](Lgraph *lg_sub) {
        //FIXME: still, the bottom-up will visit fir_op :(
        if (lg_sub->get_name().substr(0,6) == prefix_fir_op_str)
          return;

        Firmap   fm(fbmaps, pinmaps, spinmaps_xorr);
        Bitwidth bw(false, 10);  // hier = false, max_iters = 10

        fmt::print("---------------- Firrtl Op Mapping ({}) --------------- (F-2)\n", lg_sub->get_name());
        auto new_lg_sub = fm.do_firrtl_mapping(lg_sub);
        gviz ? gv.do_from_lgraph(new_lg_sub, "firmap-ed") : void();

        fmt::print("---------------- Local Bitwidth-Inference ({}) ----------- (B-0)\n", new_lg_sub->get_name());
        bw.do_trans(new_lg_sub);
        gviz ? gv.do_from_lgraph(new_lg_sub, "") : void();
        mapped_lgs.emplace_back(new_lg_sub);
      });

      // for top lgraph
      Firmap   fm(fbmaps, pinmaps, spinmaps_xorr);
      Bitwidth bw(false, 10);  // hier = false, max_iters = 10

      fmt::print("---------------- Firrtl Op Mapping ({}) --------------- (F-2)\n", lg->get_name());
      auto new_lg = fm.do_firrtl_mapping(lg);
      gviz ? gv.do_from_lgraph(new_lg, "firmap-ed") : void();

      fmt::print("---------------- Local Bitwidth-Inference ({}) ----------- (B-0)\n", new_lg->get_name());
      bw.do_trans(new_lg);
      gviz ? gv.do_from_lgraph(new_lg, "") : void();
      mapped_lgs.emplace_back(new_lg);
    }
  }

  if (lgcnt > 1 && hit == false)
    Pass::error("Top module not specified for firrtl codes!\n");

  lgs = mapped_lgs;

  // for (auto &lg : lgs) {
  //   thread_pool.add(&Lcompiler::fir_thread_firmap_bw, this, lg, bw, mapped_lgs);
  // }
  // lgs = mapped_lgs;
}

void Lcompiler::fir_thread_firmap_bw(Lgraph *lg, Bitwidth &bw, std::vector<Lgraph *> &mapped_lgs) {
  Firmap fm(fbmaps, pinmaps, spinmaps_xorr);
  fmt::print("---------------- Firrtl Op Mapping ({}) --------------- (F-2)\n", lg->get_name());
  auto new_lg = fm.do_firrtl_mapping(lg);
  gviz ? gv.do_from_lgraph(new_lg, "gioc.firmap-ed") : void();

  fmt::print("---------------- Local Bitwidth-Inference ({}) ----------- (B-0)\n", new_lg->get_name());
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


void Lcompiler::global_bitwidth_inference() {
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

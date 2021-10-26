// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "lcompiler.hpp"
#include "lbench.hpp"

// FIXME: todo: the top should always specified so that the bottom-up mechanism works


Lcompiler::Lcompiler(mmap_lib::str _path, mmap_lib::str _odir, mmap_lib::str _top, bool _gviz)
    : path(_path), odir(_odir), top(_top), gviz(_gviz), gv(true, false, _odir) {}

void Lcompiler::do_prp_lnast2lgraph(const std::vector<std::shared_ptr<Lnast>> &lnasts) {
  for (const auto &ln : lnasts) {
    thread_pool.add(&Lcompiler::prp_thread_ln2lg, this, ln);
  }
  thread_pool.wait_all();
}

void Lcompiler::prp_thread_ln2lg(const std::shared_ptr<Lnast> &ln) {
  gviz == true ? gv.do_from_lnast(ln, "raw") : void();

  //fmt::print("---------------- Pyrope -> LNAST-SSA ({})------------------- \n", ln->get_top_module_name());
  ln->ssa_trans();

  gviz == true ? gv.do_from_lnast(ln) : void();

  //fmt::print("---------------- LNAST -> Lgraph ({})----------------------- \n", ln->get_top_module_name());

  auto       mod_name = ln->get_top_module_name();
  Lnast_tolg ln2lg(mod_name, path);
  const auto top_stmts = ln->get_first_child(mmap_lib::Tree_index::root());
  auto       local_lgs = ln2lg.do_tolg(ln, top_stmts);
  // Lbench foo("pass.wierd");

  if (gviz)
    for (const auto &lg : local_lgs) gv.do_from_lgraph(lg, "raw");


  std::lock_guard<std::mutex> guard(lgs_mutex);  // guarding Lcompiler::lgs
  for (auto *lg : local_lgs) lgs.emplace_back(lg);
  // auto tp = std::chrono::system_clock::now();
  // auto tp2 = std::chrono::system_clock::now();
  // fmt::print("Yo2 {}, mod_name:{}\n", (tp2-tp).count(), mod_name);

}

void Lcompiler::do_prp_local_cprop_bitwidth() {
  auto lgcnt = 0;
  auto hit   = false;
  for (auto &lg : lgs) {
    ++lgcnt;
    if (lg->get_name() == top) {
      hit = true;
      lg->each_hier_unique_sub_bottom_up_parallel2([this](Lgraph *lg_sub) {
        Bitwidth bw(false, 10);
        Cprop    cp(false);
        //fmt::print("---------------- Copy-Propagation ({}) ------------------- (C-0)\n", lg_sub->get_name());
        cp.do_trans(lg_sub);
        gviz == true ? gv.do_from_lgraph(lg_sub, "cprop-ed") : void();

        //fmt::print("---------------- Local Bitwidth-Inference ({}) ----------- (B-0)\n", lg_sub->get_name());
        bw.do_trans(lg_sub);
        gviz == true ? gv.do_from_lgraph(lg_sub, "bitwidth-ed") : void();


        // only hier_tuple2 capricious_bits capricious_bits2 capricious_bits4 need this extra cprop
        //fmt::print("---------------- Copy-Propagation ({}) ------------------- (C-1)\n", lg_sub->get_name());
        cp.do_trans(lg_sub);
        gviz == true ? gv.do_from_lgraph(lg_sub, "cprop-ed") : void();


        // FIXME->sh: bw.is_finished() never raise high!
        // int      n_iters = 0;
        // while (true) {
        //   fmt::print("---------------- Copy-Propagation ({}) ------------------- (C-0)\n", lg_sub->get_name());
        //   cp.do_trans(lg_sub);
        //   gviz == true ? gv.do_from_lgraph(lg_sub, "cprop-ed") : void();


        //   fmt::print("---------------- Local Bitwidth-Inference ({}) ----------- (B-0)\n", lg_sub->get_name());
        //   bw.do_trans(lg_sub);
        //   gviz == true ? gv.do_from_lgraph(lg_sub, "bitwidth-ed") : void();

        //   if (bw.is_finished()) {
        //     break;
        //   }
        //   n_iters++;
        //   if (n_iters > 4) {
        //     Pass::error("graph {} could not converge bw/cprop in {} iterations", lg_sub->get_name(), n_iters);
        //   }
        // }
        // return true
      });

      Bitwidth bw(false, 10);  // hier = false, max_iters = 10
      Cprop    cp(false);      // hier = false

      //fmt::print("---------------- Copy-Propagation ({}) ------------------- (C-0)\n", lg->get_name());
      cp.do_trans(lg);
      gviz == true ? gv.do_from_lgraph(lg, "cprop-ed") : void();


      //fmt::print("---------------- Local Bitwidth-Inference ({}) ----------- (B-0)\n", lg->get_name());
      bw.do_trans(lg);
      gviz == true ? gv.do_from_lgraph(lg, "bitwidth-ed") : void();


      // only hier_tuple2 capricious_bits capricious_bits2 capricious_bits4 need this extra cprop

      //fmt::print("---------------- Copy-Propagation ({}) ------------------- (C-1)\n", lg->get_name());
      cp.do_trans(lg);
      gviz == true ? gv.do_from_lgraph(lg, "cprop-ed") : void();
    }
  }
  if (lgcnt > 1 && hit == false)
    Pass::error("Top module not specified for pyrope codes!\n");
}

void Lcompiler::do_prp_global_bitwidth_inference() {
  Bitwidth bw(true, 10);  // hier = true, max_iters = 10

  auto lgcnt = 0;
  auto hit   = false;

  // todo: when move to the new compressed bottom-up tree paralellism, you don't need to specify top
  if (top == "") {  // top will be specified only if it is a hierarchical design
    //I(lgs.size() == 1);
    for (auto &lg : lgs) {
      bw.do_trans(lg);
      gviz == true ? gv.do_from_lgraph(lg, "") : void();

    }
  } else {
    for (auto &lg : lgs) {
      ++lgcnt;
      if (lg->get_name() == top) {
        hit = true;

        //fmt::print("---------------- Global Bitwidth-Inference ({}) ----------------- (GB)\n", lg->get_name());
        bw.do_trans(lg);
      }
      gviz == true ? gv.do_from_lgraph(lg, "") : void();

    }

    if (lgcnt > 1 && hit == false)
      Pass::error("Top module not specified from multiple Pyrope source codes!\n");
  }
}

// ----------------------- FIRRTL compilation functions start ----------------------------

void Lcompiler::do_fir_lnast2lgraph(const std::vector<std::shared_ptr<Lnast>> &lnasts) {
  for (const auto &ln : lnasts) {
    thread_pool.add(&Lcompiler::fir_thread_ln2lg, this, ln);
  }
  thread_pool.wait_all();

  setup_maps();
}

void Lcompiler::fir_thread_ln2lg(const std::shared_ptr<Lnast> &ln) {
  gviz == true ? gv.do_from_lnast(ln, "raw") : void();


  //fmt::print("-------- {:<28} ({:<30}) -------- (LN-0)\n", "Firrtl_Protobuf -> LNAST-SSA", mmap_lib::str::concat("__firrtl_", ln->get_top_module_name()));
  ln->ssa_trans();
  gviz == true ? gv.do_from_lnast(ln) : void();


  // since the first generated lgraphs are firrtl_op_lgs, they will be removed
  // in the end, we should keep the original mod_name for the firrtl_op
  // mapped lgraph, so here I attached "__firrtl_" prefix for the firrtl_op_lgs

  //fmt::print("-------- {:<28} ({:<30}) -------- (LN-1)\n", "LNAST -> Lgraph", mmap_lib::str::concat("__firrtl_", ln->get_top_module_name()));
  auto mod_name = mmap_lib::str::concat("__firrtl_", ln->get_top_module_name());

  Lnast_tolg ln2lg(mod_name, path);

  auto top_stmts = ln->get_first_child(mmap_lib::Tree_index::root());
  auto local_lgs = ln2lg.do_tolg(ln, top_stmts);

  // auto tp = std::chrono::system_clock::now();

  // Lbench foo("pass.wierd");


  if (gviz)
    for (const auto &lg : local_lgs) gv.do_from_lgraph(lg, "raw");


  std::lock_guard<std::mutex> guard(lgs_mutex);  // guarding Lcompiler::lgs
  for (auto *lg : local_lgs)
    lgs.emplace_back(lg);

  // auto tp2 = std::chrono::system_clock::now();
  // fmt::print("Yo2 {}, mod_name:{}\n", (tp2-tp).count(), mod_name);
}

void Lcompiler::do_fir_cprop() {
  auto lgcnt                   = 0;
  auto hit                     = false;
  auto top_name_before_mapping = mmap_lib::str::concat("__firrtl_", top);

  // hierarchical traversal
  for (auto &lg : lgs) {
    ++lgcnt;
    // bottom up approach to parallelly analyze the firbits
    if (lg->get_name() == top_name_before_mapping) {
      hit = true;
      lg->each_hier_unique_sub_bottom_up_parallel2([this](Lgraph *lg_sub) {
        Cprop cp(false);

        //fmt::print("-------- {:<28} ({:<30}) -------- (C-0)\n", "Copy-Propagation", lg_sub->get_name());
        cp.do_trans(lg_sub);
        gviz == true ? gv.do_from_lgraph(lg_sub, "cprop-ed") : void();

      });
      break;
    }
  }
  if (lgcnt > 1 && hit == false)
    Pass::error("Top module not specified for firrtl codes!\n");
}


void Lcompiler::setup_maps() {  // single-thread

  I(fbmaps.empty());  // compiler object not reused across compilations (is it?)

  // If reused:
  // fbmaps.clear();
  // pinmaps.clear();
  // spinmaps_xorr.clear();

  for (auto *lg : lgs) {
    if (fbmaps.find(lg) != fbmaps.end())
      continue;

    fbmaps.insert_or_assign(lg, FBMap());
    pinmaps.insert_or_assign(lg, PinMap());
    spinmaps_xorr.insert_or_assign(lg, XorrMap());

    lg->each_hier_unique_sub_bottom_up([this](Lgraph *lg_sub) {
      if (fbmaps.find(lg_sub) != fbmaps.end())
        return;
      fbmaps.insert_or_assign(lg_sub, FBMap());
      pinmaps.insert_or_assign(lg_sub, PinMap());
      spinmaps_xorr.insert_or_assign(lg_sub, XorrMap());
    });
  }
}

void Lcompiler::do_fir_firmap_bitwidth() {
  auto lgcnt                  = 0;
  auto hit                    = false;
  auto top_name_before_firmap = mmap_lib::str::concat("__firrtl_", top);

  std::mutex                    lg_visited_mutex;
  absl::flat_hash_set<Lgraph *> lg_visited;

  std::vector<Lgraph *> new_lgs;

  for (auto &lg : lgs) {
    {
      std::unique_lock<std::mutex> guard(lg_visited_mutex);
      if (lg_visited.find(lg) != lg_visited.end())
        continue;  // already processed
    }
    ++lgcnt;
    // bottom up approach to parallelly do cprop, start from top. Since we could start from top in firrtl, the visitation will never
    // duplicated
    if (lg->get_name() != top_name_before_firmap)
      continue;  // WE COULD REMOTE THIS. THEN NO TOP NEEDED

    hit = true;
    // thread task already enqueued in the lambda each_hier_unique_sub_bottom_up_parallel()
    lg->each_hier_unique_sub_bottom_up_parallel2([this, &lg_visited, &lg_visited_mutex, &new_lgs](Lgraph *lg_sub) {
      {
        std::unique_lock<std::mutex> guard(lg_visited_mutex);
        if (lg_visited.find(lg_sub) != lg_visited.end())
          return;  // already processed
        lg_visited.insert(lg_sub);
      }

      I(lg_sub->get_name().substr(0, 6) != "__fir_");

      Firmap   fm(fbmaps, pinmaps, spinmaps_xorr);
      Bitwidth bw(false, 10);

      //fmt::print("-------- {:<28} ({:<30}) -------- (F-2)\n", "Firrtl Op Mapping", lg_sub->get_name());
      auto new_lg_sub = fm.do_firrtl_mapping(lg_sub);
      gviz == true ? gv.do_from_lgraph(new_lg_sub, "firmap-ed") : void();

      // FIXME->sh: bw.is_finished() malfunction
      // int n_iters = 0;
      // while (true) {
      //   fmt::print("-------- {:<28} ({:<30}) -------- (B-{})\n", "Local Bitwidth-Inference", new_lg_sub->get_name(), n_iters);
      //   bw.do_trans(new_lg_sub);
      //   gviz == true ? gv.do_from_lgraph(new_lg_sub, "") : void();

      //   if (bw.is_finished()) {
      //     break;
      //   }
      //   n_iters++;
      //   if (n_iters > 4) {
      //     Pass::error("graph {} could not converge bw in {} iterations", new_lg_sub->get_name(), n_iters);
      //   }
      // }

      //fmt::print("-------- {:<28} ({:<30}) -------- (B-0)\n", "Local Bitwidth-Inference", new_lg_sub->get_name());
      bw.do_trans(new_lg_sub);
      bw.do_trans(new_lg_sub);
      gviz == true ? gv.do_from_lgraph(new_lg_sub, "") : void();


      {
        std::unique_lock<std::mutex> guard(lgs_mutex);
        new_lgs.emplace_back(new_lg_sub);
      }
    });

    // for top lgraph
    Firmap   fm(fbmaps, pinmaps, spinmaps_xorr);
    Bitwidth bw(false, 10);

    //fmt::print("-------- {:<28} ({:<30}) -------- (F-2)\n", "Firrtl Op Mapping", lg->get_name());
    auto new_lg = fm.do_firrtl_mapping(lg);
    gviz == true ? gv.do_from_lgraph(new_lg, "firmap-ed") : void();


    // FIXME->sh: bw.is_finished() malfunction
    // int n_iters = 0;
    // while (true) {
    //   fmt::print("-------- {:<28} ({:<30}) -------- (B-{})\n", "Local Bitwidth-Inference", new_lg->get_name(), n_iters);
    //   bw.do_trans(new_lg);
    //   gviz == true ? gv.do_from_lgraph(new_lg, "") : void();

    //   if (bw.is_finished()) {
    //     break;
    //   }
    //   n_iters++;
    //   if (n_iters > 4) {
    //     Pass::error("graph {} could not converge bw in {} iterations", new_lg->get_name(), n_iters);
    //   }
    // }



    //fmt::print("-------- {:<28} ({:<30}) -------- (B-0)\n", "Local Bitwidth-Inference", new_lg->get_name());
    bw.do_trans(new_lg);
    bw.do_trans(new_lg);
    gviz == true ? gv.do_from_lgraph(new_lg, "") : void();

    {
      std::unique_lock<std::mutex> guard(lg_visited_mutex);
      lg_visited.insert(new_lg);
    }
  }

  if (lgcnt > 1 && hit == false)
    Pass::error("Top module not specified for firrtl codes!\n");

  lgs = new_lgs;  // this line is single threaded code or it will fail
}

void Lcompiler::do_fir_firbits() {
  auto lgcnt                   = 0;
  auto hit                     = false;
  auto top_name_before_mapping = mmap_lib::str::concat("__firrtl_", top);

  // hierarchical traversal
  // create a lamda function for every firrtl/pyrope passes. if top provided, use it, if not, use the visited table
  for (auto &lg : lgs) {
    ++lgcnt;
    // bottom up approach to parallelly analyze the firbits
    if (lg->get_name() == top_name_before_mapping) {
      hit = true;
      lg->each_hier_unique_sub_bottom_up_parallel2([this](Lgraph *lg_sub) {
        Firmap fm(fbmaps, pinmaps, spinmaps_xorr);

        //fmt::print("-------- {:<28} ({:<30}) -------- (F-0)\n", "Firrtl Bits Analysis", lg_sub->get_name());
        fm.do_firbits_analysis(lg_sub);

        //fmt::print("-------- {:<28} ({:<30}) -------- (F-1)\n", "Firrtl Bits Analysis", lg_sub->get_name());
        fm.do_firbits_analysis(lg_sub);
        gviz == true ? gv.do_from_lgraph(lg_sub, "firbits-ed") : void();

      });
      break;
    }
  }
  if (lgcnt > 1 && hit == false)
    Pass::error("Top module not specified for firrtl codes!\n");
}

// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "lcompiler.hpp"

// FIXME: todo: the top should always specified so that the bottom-up mechanism works

Lcompiler::Lcompiler(std::string_view _path, std::string_view _odir, std::string_view _top, bool _gviz)
    : path(_path), odir(_odir), top(_top), gviz(_gviz), gv(true, false, _odir) {}

void Lcompiler::do_prp_lnast2lgraph(const std::vector<std::shared_ptr<Lnast>> &lnasts) {
  TRACE_EVENT("pass", "lnast_ssa + lnast_tolg");
  for (const auto &ln : lnasts) {
    thread_pool.add(&Lcompiler::prp_thread_ln2lg, this, ln);
  }
  thread_pool.wait_all();
}

void Lcompiler::prp_thread_ln2lg(const std::shared_ptr<Lnast> &ln) {
  gviz == true ? gv.do_from_lnast(ln, "raw") : void();
  ln->ssa_trans();
  gviz == true ? gv.do_from_lnast(ln) : void();

  auto mod_name = ln->get_top_module_name();
  Lnast_tolg ln2lg(mod_name, path);
  const auto top_stmts = ln->get_first_child(lh::Tree_index::root());
  auto local_lgs = ln2lg.do_tolg(ln, top_stmts);

  if (gviz) {
    for (const auto &lg : local_lgs) {
      gv.do_from_lgraph(lg, "raw");
    }
  }

  std::lock_guard<std::mutex> guard(lgs_mutex);  // guarding Lcompiler::lgs
  for (auto *lg : local_lgs) {
    lgs.emplace_back(lg);
  }
  // auto tp = std::chrono::system_clock::now();
  // auto tp2 = std::chrono::system_clock::now();
  // std::print("Yo2 {}, mod_name:{}\n", (tp2-tp).count(), mod_name);
}

void Lcompiler::do_prp_local_cprop_bitwidth() {
  TRACE_EVENT("pass", "cprop + bitwidth");

  I(lgs.size());
  auto *top_lg = lgs[0];
  if (!top.empty()) {
    top_lg = nullptr;
    for (auto lg2 : lgs) {
      if (lg2->get_name() != top) {
        continue;
      }
      top_lg = lg2;
      break;
    }
    if (top_lg == nullptr) {
      Pass::error("Top module {} not specified for pyrope codes!\n", top);
      return;
    }
  }

  top_lg->each_hier_unique_sub_bottom_up_parallel2([this](Lgraph *lg_sub) {
    Bitwidth bw(false, 11);
    Cprop    cp(false);

    // std::print("---------------- Copy-Propagation ({}) ------------------- (C-0)\n", lg_sub->get_name());
    cp.do_trans(lg_sub);
    gviz == true ? gv.do_from_lgraph(lg_sub, "cprop-ed") : void();

    // std::print("---------------- Local Bitwidth-Inference ({}) ----------- (B-0)\n", lg_sub->get_name());
    bw.do_trans(lg_sub);
    gviz == true ? gv.do_from_lgraph(lg_sub, "bitwidth-ed") : void();

    // std::print("---------------- Copy-Propagation ({}) ------------------- (C-1)\n", lg_sub->get_name());

    // FIXME: This is needed because bitwidth has OVERFLOW, and can not perform
    // all the constant propagation operations. Change to non-overflow (share
    // code with LNAST BW pass)
    cp.do_trans(lg_sub);
    bw.do_trans(lg_sub);
  });
}

void Lcompiler::do_prp_global_bitwidth_inference() {
  Bitwidth bw(true, 10);  // hier = true, max_iters = 10

  auto lgcnt = 0;
  auto hit   = false;

  // todo: when move to the new compressed bottom-up tree paralellism, you don't need to specify top
  if (top.empty()) {  // top will be specified only if it is a hierarchical design
    // I(lgs.size() == 1);
    for (auto &lg : lgs) {
      bw.do_trans(lg);
      gviz == true ? gv.do_from_lgraph(lg, "") : void();
    }
  } else {
    for (auto &lg : lgs) {
      ++lgcnt;
      if (lg->get_name() == top) {
        hit = true;

        // std::print("---------------- Global Bitwidth-Inference ({}) ----------------- (GB)\n", lg->get_name());
        bw.do_trans(lg);
      }
      gviz == true ? gv.do_from_lgraph(lg, "") : void();
    }

    if (lgcnt > 1 && hit == false) {
      Pass::error("Top module not specified from multiple Pyrope source codes!\n");
    }
  }
}

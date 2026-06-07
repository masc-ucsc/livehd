//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "upass_pipe.hpp"

#include <string>

#include "lnast_ntype.hpp"

void uPass_pipe::run(const std::shared_ptr<Lnast>& lnast) {
  if (!lnast) {
    return;
  }
  // Task 1r — gate on the durable lambda kind, NOT on stages_min>0: a mod
  // output legitimately carries a declared stages(N,N) (its interface
  // landing cycle) and must never get a pipe output flop appended — a mod's
  // structural latency comes from its body (stage decls / callee
  // instances); `@[N]` never inserts flops.
  if (lnast->get_lambda_kind() != "pipe") {
    return;
  }
  const auto& outs = lnast->io_meta().outputs;
  bool        any  = false;
  for (const auto& e : outs) {
    if (e.stages_min > 0) {
      any = true;
      break;
    }
  }
  if (!any) {
    return;  // pre-ssa shape: nothing to insert
  }

  // Locate the top-level stmts (post-SSA shape: top -> stmts).
  auto      root = lnast->get_root();
  Lnast_nid stmts_nid;
  for (auto c = lnast->get_first_child(root); !c.is_invalid(); c = lnast->get_sibling_next(c)) {
    if (Lnast_ntype::is_stmts(lnast->get_type(c))) {
      stmts_nid = c;
      break;
    }
  }
  if (stmts_nid.is_invalid()) {
    return;
  }

  for (const auto& e : outs) {
    if (e.stages_min <= 0) {
      continue;
    }
    // Reg-as-output (the counter idiom) must NOT get a second flop — the reg
    // itself is the pipeline stage (home stage min-1, checked by LG pass1).
    // The Pyrope->LG path cannot declare reg outputs yet, so every output
    // reaching here is comb-driven; the guard lands with the 2d milestone.

    const std::string reg_name = "___pipe_" + e.name;

    // declare(ref(reg), prim_type_none, const("reg")) + stages(min,max)
    auto d = lnast->add_child(stmts_nid, Lnast_ntype::create_declare());
    lnast->add_child(d, Lnast_node::create_ref(reg_name));
    lnast->add_child(d, Lnast_ntype::create_prim_type_none());
    lnast->add_child(d, Lnast_node::create_const("reg"));
    auto st = lnast->add_child(d, Lnast_ntype::create_stages());
    lnast->add_child(st, Lnast_node::create_const(std::to_string(e.stages_min)));
    lnast->add_child(st, Lnast_node::create_const(std::to_string(e.stages_max)));

    // store(reg, out): reg din <- the body's comb value of the output.
    auto din = lnast->add_child(stmts_nid, Lnast_ntype::create_store());
    lnast->add_child(din, Lnast_node::create_ref(reg_name));
    lnast->add_child(din, Lnast_node::create_ref(e.name));

    // store(out, reg): the output reads the reg's q.
    auto outw = lnast->add_child(stmts_nid, Lnast_ntype::create_store());
    lnast->add_child(outw, Lnast_node::create_ref(e.name));
    lnast->add_child(outw, Lnast_node::create_ref(reg_name));
  }
}

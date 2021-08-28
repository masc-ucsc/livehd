//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "opt_lnast.hpp"

#include "lnast.hpp"

/* TODO:

   The current code is designed to work in parallel. The overall flow:

   -There should be a "top_tree" pass that runs BEFORE all the Opt_lnast. This
   pass does not traverse inside the subgraphs to be processed in Opt_lnast.
   The Opt_lnast can be spawned as the top_tree iterates, no need to wait for
   the end. Since it does not traverse inside, it can not know the value for
   the live-ins at spawn time, but it can know the potential live-ins.

   A possible spawn decision is the "stmts" sequence. If there are many
   statements spawn an Opt_lnast. if it is HUGE, it can be even splited in
   multiple but the optimization may not be as good.

   (Any node in the tree is a valid spawn point to process the children in
   Opt_lnast. Only large subtrees make sense due to overheads)

   (NOTE: immutable variables could be passed because the sub-tree/Opt_lnast is
   not allowed to touch. Immutable are ___XX or inputs $foo)

   -Each thread has a Opt_lnast object starting with a node_id. When the
   traversal reaches the node_id, the opt finished.

   -The Opt_lnat is a "local" optimization that can not perform the best result
   because it does not know all the live-ins values. The result is to not
   forward/optimize unless the variable was used in a higher or equal scope.

   The Opt_lnast updates the level_forward_* tables and creates a "live-ins"
   for any variable that was read before.

   -After all the Opt_lnast passes are done, there should be a new "cleanup"
   pass. This pass uses information left by the Opt_lnast (level_forward for
   live-ins). This allows to detect variables that can be deleted from the
   tree. If live-in was not read by any succeded Opt_lnast, the variable can be
   deleted.

   -The Opt_lnast should return the "read live-ins". This allows a fast top-level
   cleanup method. Starting from the end we can spawn cleanups (bottom-level). Each
   bottom-level cleanup should create a new LNAST tree which will allow to run
   future passes in parallel.

COMMENTS:

   1- The current solution "slowdown" compilation because it propagates values,
      but it does NOT remove nodes that are unused.

      The issue is that the current pass is built to allow "parallel"
      processing of the LNAST tree.  This is the same reason why the
      level_forward_* clears more variables than needed.

      After "aggregating" live-ins, we should be able to handle this.

   2- The current model level_forward_* only remember current level. In theory,
      If the variable was inserted in a higher level, no need to clear.

      We still need to clear because we do not know if the scope of the
      variable declaration

   3- If the variable is a $/%/# the level is top, not cleared unless going out
      of method/define

*/

Opt_lnast::Opt_lnast(const Eprp_var &var) {
  (void)var;
  // could check for options
}

void Opt_lnast::process_plus(const std::shared_ptr<Lnast> &ln, const Lnast_nid &lnid) {
  mmap_lib::str var;
  Lconst        result_trivial;

  bool first_child = true;
  for (auto child : ln->children(lnid)) {
    const auto &data = ln->get_data(child);
    // fmt::print("plus:{} n_children:{}\n", data.type.debug_name(), n_children);

    if (first_child) {  // first child
      first_child = false;

      I(data.type.is_ref());
      var = data.token.get_text();
      continue;
    }

    if (data.type.is_ref()) {
      result_trivial = result_trivial + st.get_trivial(data.token.get_text());
    } else {
      result_trivial = result_trivial + Lconst::from_pyrope(data.token.get_text());
    }
  }

  st.set(var, result_trivial);
}

void Opt_lnast::process_tuple_add(const std::shared_ptr<Lnast> &ln, const Lnast_nid &lnid) {
  (void)ln;
  (void)lnid;
  // child can be const/ref/assign

#if 0
  //-------------------------------
  auto idx                     = ln->get_first_child(lnid);
  const auto &first_child_data = ln->get_data(idx);
  I(first_child_data.type.is_ref());

  auto var = first_child_data.token.get_text();

  //-------------------------------
  auto last_idx = ln->get_last_child(lnid);
  idx = ln->get_sibling_next(idx);

  int assign_pos = 0;
  while(idx!=last_idx) {
    const auto &data = ln->get_data(idx);
    if (data.type.is_const()) {
    }else if (data.type.is_const()) {
    }else{
      I(data.type.is_assign());

      ++assign_pos;
    }
  }


  bool first_child = true;
  for (auto child : ln->children(lnid)) {
    if (first_child) {  // first child
      first_child = false;

      I(data.type.is_ref());
      var = data.token.get_text();
      continue;
    }
HERE    if LAST CHILD, just add the value

    if (data.type.is_const()) {
      var = mmap_lib::str::concat(var, ".", data.token.get_text());
    } else {
      I(data.type.is_ref());
      var idx = st.get_trivial(data.token.get_text());
      HERE!!! If invalid, it should be an array or compile error
    }

  }
#endif

}

void Opt_lnast::process_assign(const std::shared_ptr<Lnast> &ln, const Lnast_nid &lnid) {
  auto lhs_id = ln->get_first_child(lnid);
  auto rhs_id = ln->get_sibling_next(lhs_id);

  const auto &lhs_data = ln->get_data(lhs_id);
  const auto &rhs_data = ln->get_data(rhs_id);

  I(lhs_data.type.is_ref());

  auto lhs_txt = lhs_data.token.get_text();
  auto rhs_txt = rhs_data.token.get_text();

  if (rhs_data.type.is_ref()) {
    auto bundle = st.get_bundle(rhs_txt);
    st.set(lhs_txt, bundle);
  }else{
    auto v = Lconst::from_pyrope(rhs_txt);
    st.set(lhs_txt, v);
  }
}

void Opt_lnast::process_todo(const std::shared_ptr<Lnast> &ln, const Lnast_nid &lnid) {
  auto &data = ln->get_data(lnid);

  fmt::print("not handling lnast type:{} (TODO)\n", data.type.debug_name());
}

void Opt_lnast::opt(const std::shared_ptr<Lnast> &ln) {

  st.funcion_scope(ln->get_top_module_name());

  for (auto &lnid : ln->depth_postorder()) {
    // fmt::print("lnast:{}\n", data.type.debug_name());
    if (ln->is_leaf(lnid))
      continue;  // TODO: Maybe a faster postorder traversal

    auto &data = ln->get_data(lnid);
    // fmt::print("lnast:{}\n", data.type.debug_name());

    switch (data.type.get_raw_ntype()) {
      case Lnast_ntype::Lnast_ntype_int::Lnast_ntype_plus     : process_plus     (ln, lnid); break;
      case Lnast_ntype::Lnast_ntype_int::Lnast_ntype_assign   : process_assign   (ln, lnid); break;
      case Lnast_ntype::Lnast_ntype_int::Lnast_ntype_tuple_add: process_tuple_add(ln, lnid); break;
      default                                                 : process_todo     (ln, lnid);
    }
  }

  auto outputs = st.leave_scope();
  if (outputs)
    outputs->dump();
}

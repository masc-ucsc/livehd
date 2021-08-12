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
  Lconst    result;
  Lnast_nid const_pos;
  int       n_children_left = 0;
  bool      simplified      = false;

  for (auto child : ln->children(lnid)) {
    auto &data = ln->get_data(child);
    // fmt::print("plus:{}\n", data.type.debug_name());

    Lconst constant;
    if (data.type.is_ref()) {
      if (n_children_left == 0) {  // first child
        ++n_children_left;
        continue;
      }

      auto rhs = ln->get_data(child).token.get_text();

      auto val_it = level_forward_val.find(rhs);
      if (val_it == level_forward_val.end()) {
        auto ref_it = level_forward_ref.find(rhs);
        if (ref_it != level_forward_ref.end()) {
          auto ln_txt = ref_it->second;
          auto lnode  = Lnast_node::create_ref(ln_txt);
          ln->set_data(child, lnode);
        }
        ++n_children_left;
#ifndef LNAST_REWRITE
        if (n_children_left > 2)
          return;  // not possible to compute the whole plus node
#endif
        continue;
      } else {
        constant = val_it->second;
      }
    } else {
      constant = Lconst::from_pyrope(data.token.get_text());
      I(data.type.is_const());
    }

    if (const_pos.is_invalid()) {
      result    = constant;
      const_pos = child;
      ++n_children_left;
    } else {
      simplified = true;
      result     = result + constant;
#ifdef LNAST_REWRITE
      auto lnode = Lnast_node::create_const("0");
      ln->set_data(child, lnode);  // HACK until mmap_tree2 gets online
                                   // FIXME: delete_leaf does not work. mmap_tree2. Then use this: ln->delete_leaf(child);
#endif
    }
  }

  if (n_children_left <= 1) {
    auto &data = ln->get_data(lnid);
    Pass::error("undriven LNAST plus node:{}", data.token.get_text());
    return;
  }

#ifdef LNAST_REWRITE
  if (simplified) {
    I(!const_pos.is_invalid());
    auto ln_txt = mmap_lib::str(result.to_pyrope());
    auto lnode  = Lnast_node::create_const(ln_txt);
    ln->set_data(const_pos, lnode);
  }
#endif

  if (n_children_left == 2) {  // + tmp val  -> same as -> tmp = val
    auto lhs_id = ln->get_first_child(lnid);
    auto rhs_id = ln->get_sibling_next(lhs_id);

    auto lhs = ln->get_data(lhs_id).token.get_text();
    auto rhs = ln->get_data(rhs_id).token.get_text();

    if (simplified)
      level_forward_val[lhs] = result;
    else
      level_forward_ref[lhs] = rhs;
  }
}

void Opt_lnast::process_assign(const std::shared_ptr<Lnast> &ln, const Lnast_nid &lnid) {
  auto lhs_id = ln->get_first_child(lnid);
  auto rhs_id = ln->get_sibling_next(lhs_id);

  auto lhs = ln->get_data(lhs_id).token.get_text();
  auto rhs = ln->get_data(rhs_id).token.get_text();

  auto val_it = level_forward_val.find(rhs);
  if (val_it != level_forward_val.end()) {
    auto lnode  = Lnast_node::create_const(val_it->second.to_pyrope());
    ln->set_data(rhs_id, lnode);

    level_forward_val[lhs] = val_it->second;
    return;
  }

  auto ref_it = level_forward_ref.find(rhs);
  if (ref_it != level_forward_ref.end()) {
    auto ln_txt = ref_it->second;
    auto lnode  = Lnast_node::create_ref(ln_txt);
    ln->set_data(rhs_id, lnode);

    level_forward_ref[lhs] = ref_it->second;
  }
}

void Opt_lnast::process_todo(const std::shared_ptr<Lnast> &ln, const Lnast_nid &lnid) {
  auto &data = ln->get_data(lnid);

  fmt::print("not handling lnast type:{} (TODO)\n", data.type.debug_name());
}

void Opt_lnast::opt(const std::shared_ptr<Lnast> &ln) {
  level = INT32_MAX;
  for (auto &lnid : ln->depth_postorder()) {
    if (ln->is_leaf(lnid))
      continue;  // TODO: Maybe a faster postorder traversal

    if (level != lnid.level) {
      level = lnid.level;
      level_forward_ref.clear();
      level_forward_val.clear();
    }

    auto &data = ln->get_data(lnid);
    // fmt::print("lnast:{}\n", data.type.debug_name());

    switch (data.type.get_raw_ntype()) {
      case Lnast_ntype::Lnast_ntype_int::Lnast_ntype_plus: process_plus(ln, lnid); break;
      case Lnast_ntype::Lnast_ntype_int::Lnast_ntype_assign: process_assign(ln, lnid); break;
      default: process_todo(ln, lnid);
    }
  }
}

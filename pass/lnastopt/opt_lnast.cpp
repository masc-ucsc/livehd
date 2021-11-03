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

  if (var.has_label("top"))
    top = var.get("top");

  needs_hierarchy = false;
  hier_mode       = false;
}

void Opt_lnast::set_needs_hierarchy() { needs_hierarchy = true; }

void Opt_lnast::hierarchy_info_int(const std::string &msg) {
  needs_hierarchy = true;
  if (hier_mode) {
    throw Lnast::error(msg);
  } else {
    Lnast::info(msg);
  }
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

void Opt_lnast::process_minus(const std::shared_ptr<Lnast> &ln, const Lnast_nid &lnid) {
  mmap_lib::str var;
  Lconst        result_trivial;

  bool first_child   = true;
  bool first_operand = true;
  for (auto child : ln->children(lnid)) {
    const auto &data = ln->get_data(child);

    if (first_child) {  // first child
      first_child = false;

      I(data.type.is_ref());
      var = data.token.get_text();
      continue;
    }

    if (first_operand) {
      first_operand = false;

      if (data.type.is_ref()) {
        result_trivial = result_trivial + st.get_trivial(data.token.get_text());
      } else {
        result_trivial = result_trivial + Lconst::from_pyrope(data.token.get_text());
      }
    } else {
      if (data.type.is_ref()) {
        result_trivial = result_trivial - st.get_trivial(data.token.get_text());
      } else {
        result_trivial = result_trivial - Lconst::from_pyrope(data.token.get_text());
      }
    }
  }

  st.set(var, result_trivial);
}

void Opt_lnast::process_bit_or(const std::shared_ptr<Lnast> &ln, const Lnast_nid &lnid) {
  mmap_lib::str var;
  Lconst        result_trivial;

  bool first_child = true;
  for (auto child : ln->children(lnid)) {
    const auto &data = ln->get_data(child);

    if (first_child) {  // first child
      first_child = false;

      I(data.type.is_ref());
      var = data.token.get_text();
      continue;
    }

    if (data.type.is_ref()) {
      result_trivial = result_trivial | st.get_trivial(data.token.get_text());
    } else {
      result_trivial = result_trivial | Lconst::from_pyrope(data.token.get_text());
    }
  }

  st.set(var, result_trivial);
}

void Opt_lnast::process_tuple_set(const std::shared_ptr<Lnast> &ln, const Lnast_nid &lnid) {
  //-------------------------------
  auto        idx              = ln->get_first_child(lnid);
  const auto &first_child_data = ln->get_data(idx);
  I(first_child_data.type.is_ref());

  auto          var_root = first_child_data.token.get_text();
  mmap_lib::str var_field;

  //-------------------------------
  auto rhs_id = ln->get_last_child(lnid);
  idx         = ln->get_sibling_next(idx);

  while (idx != rhs_id) {
    const auto &data = ln->get_data(idx);
    if (data.type.is_const()) {  // CASE 1: foo.bar
      var_field = mmap_lib::str::concat(var_field, ".", data.token.get_text());
    } else {
      I(data.type.is_ref());
      auto ref = data.token.get_text();

      auto v = st.get_trivial(ref);
      if (!v.is_invalid()) {  // CASE 2: foo['bar'] or foo[123]
        var_field = mmap_lib::str::concat(var_field, ".", v.to_field());
      } else {
        auto var_bundle = st.get_bundle(var_root);
        if (var_bundle == nullptr) {  // CASE 3: foo[$runtime] foo is unknown and $runtime may not be constant
          hierarchy_info("bundle '{}{}' does not exist when indexed with non-constant '{}'", var_root, var_field, ref);
          return;
        }
        if (var_bundle->is_ordered(var_field)) {  // CASE 3: foo[$runtime] OK (foo is ordered/array)
          ln->dump(lnid);
          fmt::print("FIXME: handle non-const (array)\n");  // could become const with hier
        } else {
          // CASE 4: foo[(1,'bar')] -- Not allowed in tuple_set/get (for the moment)
          ln->dump(lnid);
          fmt::print("FIXME: handle non-array (it will be error unless hier solves it)\n");
          return;
        }
      }
    }

    idx = ln->get_sibling_next(idx);
  }

  const auto &rhs_data = ln->get_data(rhs_id);
  auto        rhs_txt  = rhs_data.token.get_text();

  auto lhs_txt = mmap_lib::str::concat(var_root, var_field);

  if (rhs_data.type.is_ref()) {
    auto bundle = st.get_bundle(rhs_txt);
    st.set(lhs_txt, bundle);
  } else {
    auto v = Lconst::from_pyrope(rhs_txt);
    st.set(lhs_txt, v);
  }
}

void Opt_lnast::process_tuple_get(const std::shared_ptr<Lnast> &ln, const Lnast_nid &lnid) {
  auto        lhs_id   = ln->get_first_child(lnid);
  const auto &lhs_data = ln->get_data(lhs_id);
  auto        lhs_txt  = lhs_data.token.get_text();

  auto        idx               = ln->get_sibling_next(lhs_id);
  const auto &second_child_data = ln->get_data(idx);
  I(second_child_data.type.is_ref());

  auto          var_root = second_child_data.token.get_text();
  mmap_lib::str var_field;

  //-------------------------------
  auto rhs_id = ln->get_last_child(lnid);

  while (idx != rhs_id) {
    const auto &data = ln->get_data(idx);
    if (data.type.is_const()) {  // CASE 1: foo.bar
      var_field = mmap_lib::str::concat(var_field, ".", data.token.get_text());
    } else {
      I(data.type.is_ref());
      auto ref        = data.token.get_text();
      auto ref_bundle = st.get_bundle(ref);
      if (ref_bundle == nullptr) {  // Stores reference if reference value is unknown
        st.set(lhs_txt, ref_bundle);
        return;
      }
    }

    idx = ln->get_sibling_next(idx);
  }
  // Repeat one last time for last child
  const auto &rhs_data = ln->get_data(rhs_id);
  if (rhs_data.type.is_const()) {  // CASE 1: foo.bar
    var_field = mmap_lib::str::concat(var_field, ".", rhs_data.token.get_text());
  } else {
    I(rhs_data.type.is_ref());
    auto ref        = rhs_data.token.get_text();
    auto ref_bundle = st.get_bundle(ref);
    if (ref_bundle == nullptr) {  // Stores reference if reference value is unknown
      st.set(lhs_txt, ref_bundle);
      return;
    }
  }

  // Stores value if reference value is known
  auto rhs_txt = mmap_lib::str::concat(var_root, var_field);
  auto bundle  = st.get_bundle(rhs_txt);
  st.set(lhs_txt, bundle);

  // TEMP CODE
  // Creates a new assign node in the LNAST with the trivial value stored from tuple_get
  // auto lhs_trivial = st.get_trivial(lhs_txt);
  //
  // auto node_assign = Lnast_node::create_assign();
  // auto node_target = Lnast_node::create_ref("tempref");
  // auto node_const  = Lnast_node::create_const(lhs_trivial.to_pyrope());
  //
  // auto idx_assign = ln->add_child(ln->get_parent(lnid), node_assign);
  // ln->add_child(idx_assign, node_target);
  // ln->add_child(idx_assign, node_const);
}

void Opt_lnast::process_tuple_add(const std::shared_ptr<Lnast> &ln, const Lnast_nid &lnid) {
  //-------------------------------
  auto        idx              = ln->get_first_child(lnid);
  const auto &first_child_data = ln->get_data(idx);
  I(first_child_data.type.is_ref());

  auto var_root = first_child_data.token.get_text();

  //-------------------------------
  idx = ln->get_sibling_next(idx);

  if (idx.is_invalid()) {
    st.set(var_root, Lconst::invalid());  // just declare the variable as an empty tuple
    return;
  }

  auto bundle = std::make_shared<Bundle>(var_root);

  int pos = 0;
  while (!idx.is_invalid()) {
    auto pos_txt = mmap_lib::str(pos);

    const auto &data = ln->get_data(idx);
    if (data.type.is_const()) {  // CASE 1: (..., 123, ...)
      bundle->set(pos_txt, Lconst::from_pyrope(data.token.get_text()));
    } else if (data.type.is_ref()) {  // CASE 2: (..., $run, ...)
      bundle->set(pos_txt, st.get_bundle(data.token.get_text()));
    } else {  // CASE 3: (...,a=..., ...)
      I(data.type.is_assign());
      auto lhs_id = ln->get_first_child(idx);
      auto rhs_id = ln->get_sibling_next(lhs_id);

      const auto &data_lhs = ln->get_data(lhs_id);
      const auto &data_rhs = ln->get_data(rhs_id);
      I(data_lhs.type.is_ref());
      auto data_lhs_txt = data_lhs.token.get_text();
      if (data_lhs_txt.is_i()) {
        throw Lnast::error("bundle '{}' can not have '{}' as field (numeric not allowed)", var_root, data_lhs_txt);
      }
      auto field_lhs = mmap_lib::str::concat(":", pos_txt, ":", data_lhs_txt);

      if (data_rhs.type.is_const()) {  // CASE 1: (..., a=123, ...)
        bundle->set(field_lhs, Lconst::from_pyrope(data_rhs.token.get_text()));
      } else {
        I(data_rhs.type.is_ref());  // CASE 2: (..., a=$run, ...)
        bundle->set(field_lhs, st.get_bundle(data_rhs.token.get_text()));
      }
    }

    idx = ln->get_sibling_next(idx);
    ++pos;
  }

  st.set(var_root, bundle);
}

void Opt_lnast::process_assign(const std::shared_ptr<Lnast> &ln, const Lnast_nid &lnid) {
  auto lhs_id = ln->get_first_child(lnid);
  auto rhs_id = ln->get_sibling_next(lhs_id);

  const auto &lhs_data = ln->get_data(lhs_id);
  const auto &rhs_data = ln->get_data(rhs_id);

  I(lhs_data.type.is_ref());

  auto lhs_txt = lhs_data.token.get_text();
  auto rhs_txt = rhs_data.token.get_text();

#if 0
  // CODE FOR DP_ASSIGN??
  //
  // CASE 1:
zz = (x=0,y=0)
zz = (1,a=3)


  // CASE 2:
zz = (x=0,y=0)
yy = (1,a=3)


  // CASE 3:
(x=0,y=0) = (1,a=3)

  // CASE 4:
(x,y) = (1,a=3)

  if (st.has_bundle(lhs_txt)) {
    auto lhs_bundle = st.get_bundle(lhs_txt);

    std::vector<mmap_lib::str> lhs_ids;
    for(const auto &e:lhs_bundle->get_sort_map()) {
      if (e.first.contains('.')) {
        lhs_bundle->dump();
        hierarchy_info_int("the lhs of the assigned must be a trivial bundle when assigning a constant");
        return;
      }
      if (e.first.is_i()) {
        lhs_bundle->dump();
        hierarchy_info_int("the lhs bundle must be named");
        return;
      }
      lhs_ids.emplace_back(e.first);
    }

    if (rhs_data.type.is_ref()) {
      auto bundle = st.get_bundle(rhs_txt);
      if (!bundle->is_ordered(""_str)) {
        bundle->dump();
        hierarchy_info_int("bundle assigned must be between ordered bundles");
        return;
      }

      for(auto i=0u;i<lhs_ids.size();++i) {
        auto b = bundle->get_bundle(mmap_lib::str(i));
        st.set(lhs_ids[i], b);
      }
    }else{
      auto v = Lconst::from_pyrope(rhs_txt);
      if (lhs_ids.size()!=1) {
        ln->dump(lnid);
        hierarchy_info_int("the lhs of the assigned must be a trivial bundle when assigning a constant");
        return;
      }
      st.set(lhs_ids[0], v);
    }

  }else
#endif

  if (rhs_data.type.is_ref()) {
    auto bundle = st.get_bundle(rhs_txt);
    st.set(lhs_txt, bundle);
  } else {
    auto v = Lconst::from_pyrope(rhs_txt);
    st.set(lhs_txt, v);
  }
}

void Opt_lnast::process_todo(const std::shared_ptr<Lnast> &ln, const Lnast_nid &lnid) {
  auto &data = ln->get_data(lnid);

  fmt::print("not handling lnast type:{} (TODO)\n", data.type.debug_name());
}

void Opt_lnast::process_stmts(const std::shared_ptr<Lnast> &ln, const Lnast_nid &lnid) {
  I(ln->get_data(lnid).type.is_stmts());

  auto idx = ln->get_first_child(lnid);

  while (!idx.is_invalid()) {
    const auto &data = ln->get_data(idx);

    switch (data.type.get_raw_ntype()) {
      case Lnast_ntype::Lnast_ntype_int::Lnast_ntype_plus: process_plus(ln, idx); break;
      case Lnast_ntype::Lnast_ntype_int::Lnast_ntype_minus: process_minus(ln, idx); break;
      case Lnast_ntype::Lnast_ntype_int::Lnast_ntype_bit_or: process_bit_or(ln, idx); break;
      case Lnast_ntype::Lnast_ntype_int::Lnast_ntype_assign: process_assign(ln, idx); break;
      case Lnast_ntype::Lnast_ntype_int::Lnast_ntype_tuple_set: process_tuple_set(ln, idx); break;
      case Lnast_ntype::Lnast_ntype_int::Lnast_ntype_tuple_get: process_tuple_get(ln, idx); break;
      case Lnast_ntype::Lnast_ntype_int::Lnast_ntype_tuple_add: process_tuple_add(ln, idx); break;
      case Lnast_ntype::Lnast_ntype_int::Lnast_ntype_stmts: process_stmts(ln, idx); break;
      default: process_todo(ln, idx);
    }

    idx = ln->get_sibling_next(idx);
  }
}

void Opt_lnast::reconstruct_stmts(const std::shared_ptr<Lnast> &ln, const Lnast_nid &lnid, Lnast_create &ln2) {
  I(ln->get_data(lnid).type.is_stmts());

  auto idx = ln->get_first_child(lnid);

  while (!idx.is_invalid()) {
    const auto &data = ln->get_data(idx);

    auto  lhs_id   = ln->get_first_child(idx);
    auto &lhs_data = ln->get_data(lhs_id);
    auto  lhs_text = lhs_data.token.get_text();

    switch (data.type.get_raw_ntype()) {
      case Lnast_ntype::Lnast_ntype_int::Lnast_ntype_plus: {
        ln2.create_assign_stmts(lhs_text, st.get_trivial(lhs_text).to_pyrope());
        break;
      }
      case Lnast_ntype::Lnast_ntype_int::Lnast_ntype_minus:
        process_minus(ln, idx);
        break;
        {
          ln2.create_assign_stmts(lhs_text, st.get_trivial(lhs_text).to_pyrope());
          break;
        }
      case Lnast_ntype::Lnast_ntype_int::Lnast_ntype_bit_or:
        process_bit_or(ln, idx);
        break;
        {
          ln2.create_assign_stmts(lhs_text, st.get_trivial(lhs_text).to_pyrope());
          break;
        }
      case Lnast_ntype::Lnast_ntype_int::Lnast_ntype_assign: {
        ln2.create_assign_stmts(lhs_text, st.get_trivial(lhs_text).to_pyrope());
        break;
      }
      case Lnast_ntype::Lnast_ntype_int::Lnast_ntype_tuple_set: break;
      case Lnast_ntype::Lnast_ntype_int::Lnast_ntype_tuple_get: break;
      case Lnast_ntype::Lnast_ntype_int::Lnast_ntype_tuple_add: break;
      case Lnast_ntype::Lnast_ntype_int::Lnast_ntype_stmts: reconstruct_stmts(ln, idx, ln2); break;
      // default: process_todo(ln, idx);
      default: break;
    }

    idx = ln->get_sibling_next(idx);
  }
}

void Opt_lnast::opt(const std::shared_ptr<Lnast> &ln) {
  st.funcion_scope(ln->get_top_module_name());

  if (ln->get_top_module_name() == top || top.empty())
    hier_mode = true;
  else
    hier_mode = false;

  const auto &data = ln->get_data(Lnast_nid::root());
  if (!data.type.is_top()) {
    throw Lnast::error("invalid lnast. It should be top");
  }

  auto idx = ln->get_first_child(Lnast_nid::root());
  process_stmts(ln, idx);

  // auto outputs = st.leave_scope();
  // if (outputs)
  //   outputs->dump();
}

void Opt_lnast::reconstruct(const std::shared_ptr<Lnast> &ln, Lnast_create &ln2) {
  if (ln->get_top_module_name() == top || top.empty())
    hier_mode = true;
  else
    hier_mode = false;

  const auto &data = ln->get_data(Lnast_nid::root());
  if (!data.type.is_top()) {
    throw Lnast::error("invalid lnast. It should be top");
  }

  auto idx = ln->get_first_child(Lnast_nid::root());
  reconstruct_stmts(ln, idx, ln2);

  auto outputs = st.leave_scope();
  if (outputs)
    outputs->dump();
}

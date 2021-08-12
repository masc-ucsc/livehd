//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <google/protobuf/message.h>

#include <fstream>
#include <iostream>

#include "inou_firrtl.hpp"

#define PRINT_DEBUG

void Inou_firrtl::toFIRRTL(Eprp_var &var) {
  Inou_firrtl p(var);

  firrtl::FirrtlPB          fir_design;
  firrtl::FirrtlPB_Circuit *circuit = fir_design.add_circuit();
  auto                      top_msg = circuit->add_top();

  for (const auto &lnast : var.lnasts) {
    p.do_tofirrtl(lnast, circuit);
    auto n = lnast->get_name(mmap_lib::Tree_index::root());

    top_msg->set_name(n.to_s());  // FIXME: Placeholder for now, need to figure out which LNAST is "top"
  }

  // fir_design.PrintDebugString();
  std::fstream output(absl::StrCat(top_msg->name(), ".pb"), std::ios::out | std::ios::trunc | std::ios::binary);
  if (!fir_design.SerializeToOstream(&output)) {
    fmt::print("Failed to write firrtl design\n");
  }
  google::protobuf::ShutdownProtobufLibrary();
}

void Inou_firrtl::do_tofirrtl(const std::shared_ptr<Lnast> &ln, firrtl::FirrtlPB_Circuit *circuit) {
  io_map.clear();
  reg_wire_map.clear();
  wire_rename_map.clear();
  name_to_range_map.clear();
  dot_map.clear();

  constexpr auto top      = mmap_lib::Tree_index::root();
  const auto     stmts    = ln->get_first_child(top);
  const auto     top_name = ln->get_name(top);

  auto *mod  = circuit->add_module();
  auto *umod = new firrtl::FirrtlPB_Module_UserModule();
  umod->set_id(top_name.to_s());  // FIXME: Need to make sure top node has module name
  FindCircuitComps(*ln, umod);

  for (const auto &lnidx : ln->children(stmts)) {
    process_ln_stmt(*ln, lnidx, umod);
  }
  mod->set_allocated_user_module(umod);
}

void Inou_firrtl::process_ln_stmt(Lnast &ln, const Lnast_nid &lnidx, firrtl::FirrtlPB_Statement_When *when, uint8_t pos_to_add_to) {
  const auto ntype = ln.get_data(lnidx).type;
  if (ntype.is_assign() || ntype.is_dp_assign()) {
    auto fstmt       = pos_to_add_to == 0 ? when->add_consequent() : when->add_otherwise();
    auto stmt_needed = process_ln_assign_op(ln, lnidx, fstmt);
    if (!stmt_needed) {
      /*Note->hunter: If the assign returned false, then we
       * didn't need that assign so erase that statement made.*/
      if (pos_to_add_to == 0) {
        when->mutable_consequent()->RemoveLast();
      } else {
        when->mutable_otherwise()->RemoveLast();
      }
    }
  } else if (ntype.is_nary_op()) {
    auto fstmt = pos_to_add_to == 0 ? when->add_consequent() : when->add_otherwise();
    process_ln_nary_op(ln, lnidx, fstmt);
  } else if (ntype.is_bit_not()) {
    auto fstmt = pos_to_add_to == 0 ? when->add_consequent() : when->add_otherwise();
    process_ln_bit_not_op(ln, lnidx, fstmt);
  } else if (ntype.is_reduce_xor()) {
    auto fstmt = pos_to_add_to == 0 ? when->add_consequent() : when->add_otherwise();
    process_ln_reduce_xor_op(ln, lnidx, fstmt);
  } else if (ntype.is_if()) {
    auto nested_when_stmt = process_ln_if_op(ln, lnidx);
    auto fstmt            = pos_to_add_to == 0 ? when->add_consequent() : when->add_otherwise();
    fstmt->set_allocated_when(nested_when_stmt);
  } else if (ntype.is_select()) {
    auto fstmt       = pos_to_add_to == 0 ? when->add_consequent() : when->add_otherwise();
    auto stmt_needed = process_ln_select(ln, lnidx, fstmt);
    if (!stmt_needed) {
      /*Note->hunter: If the assign returned false, then we
       * didn't need that assign so erase that statement made.*/
      if (pos_to_add_to == 0) {
        when->mutable_consequent()->RemoveLast();
      } else {
        when->mutable_otherwise()->RemoveLast();
      }
    }
  } else if (ntype.is_func_call()) {
    return;  // Nothing to do, submod_inst already made in FindCircuitComps
  } else if (ntype.is_tuple()) {
    mmap_lib::str tup_name;
    bool             first = true;
    for (const auto &child : ln.children(lnidx)) {
      if (first) {
        tup_name = ln.get_name(child);
        first    = false;
      } else {
        auto fstmt = pos_to_add_to == 0 ? when->add_consequent() : when->add_otherwise();
        process_tup_asg(ln, child, tup_name, fstmt);
      }
    }
  } else if (ntype.is_invalid()) {
    return;
  } else if (ntype.is_const()) {
    I(ln.get_name(lnidx) == "default_const");
    return;
  } else if (ntype.is_err_flag()) {
    I(ln.get_name(lnidx) == mmap_lib::str("err_var_undefined"));
    return;
  } else {
    fmt::print("Error: node with token {} not yet supported\n", ln.get_name(lnidx));
    I(false);
    return;
  }
}

/* TODO:
 *   dots (needed for signedness/subgraphs/pick)
 */
void Inou_firrtl::process_ln_stmt(Lnast &ln, const Lnast_nid &lnidx, firrtl::FirrtlPB_Module_UserModule *umod) {
  const auto ntype = ln.get_data(lnidx).type;
  if (ntype.is_assign() || ntype.is_dp_assign()) {
    auto fstmt       = umod->add_statement();
    auto stmt_needed = process_ln_assign_op(ln, lnidx, fstmt);
    if (!stmt_needed) {
      /*Note->hunter: If the assign returned false, then we
       * didn't need that assign so erase that statement made.*/
      umod->mutable_statement()->RemoveLast();
    }
  } else if (ntype.is_nary_op()) {
    auto fstmt = umod->add_statement();
    process_ln_nary_op(ln, lnidx, fstmt);
  } else if (ntype.is_bit_not()) {
    auto fstmt = umod->add_statement();
    process_ln_bit_not_op(ln, lnidx, fstmt);
  } else if (ntype.is_reduce_xor()) {
    auto fstmt = umod->add_statement();
    process_ln_reduce_xor_op(ln, lnidx, fstmt);
  } else if (ntype.is_if()) {
    auto when_stmt = process_ln_if_op(ln, lnidx);
    auto fstmt     = umod->add_statement();
    fstmt->set_allocated_when(when_stmt);
  } else if (ntype.is_select()) {
    auto fstmt       = umod->add_statement();
    auto stmt_needed = process_ln_select(ln, lnidx, fstmt);
    if (!stmt_needed) {
      /*Note->hunter: If the assign returned false, then we
       * didn't need that assign so erase that statement made.*/
      umod->mutable_statement()->RemoveLast();
    }
  } else if (ntype.is_func_call()) {
    return;  // Nothing to do, submod_inst already made in FindCircuitComps
  } else if (ntype.is_tuple()) {
    mmap_lib::str tup_name;
    bool             first = true;
    for (const auto &child : ln.children(lnidx)) {
      if (first) {
        tup_name = ln.get_name(child);
        first    = false;
      } else {
        auto fstmt = umod->add_statement();
        process_tup_asg(ln, child, tup_name, fstmt);
      }
    }
  } else if (ntype.is_invalid()) {
    return;
  } else if (ntype.is_const()) {
    I(ln.get_name(lnidx) == "default_const");
    return;
  } else if (ntype.is_err_flag()) {
    I(ln.get_name(lnidx) == mmap_lib::str("err_var_undefined"));
    return;
  } else {
    fmt::print("Error: node with token {} not yet supported\n", ln.get_name(lnidx));
    I(false);
    return;
  }
}

bool Inou_firrtl::process_ln_assign_op(Lnast &ln, const Lnast_nid &lnidx_assign, firrtl::FirrtlPB_Statement *fstmt) {
  auto c0       = ln.get_first_child(lnidx_assign);
  auto c1       = ln.get_sibling_next(c0);
  auto ntype_c0 = ln.get_type(c0);
  I(ntype_c0.is_ref());
  auto ntype_c1 = ln.get_type(c1);
  I(ntype_c1.is_const() || ntype_c1.is_ref());

  if (dot_map.contains(ln.get_name(c0))) {
    auto field_node = dot_map[ln.get_name(c0)].second;
    if (ln.get_name(field_node).substr(0, 2) == "__") {
      // This isn't an actual assign, and is instead assigning an attribute.
      handle_attr_assign(ln, c0, c1);
      return false;
    }
  }

  // Form expression that holds RHS contents.
  auto *rhs_expr = new firrtl::FirrtlPB_Expression();
  add_refcon_as_expr(ln, c1, rhs_expr);

  // Now assign lhs to rhs.
  make_assignment(ln, c0, rhs_expr, fstmt);
  return true;
}

/* When a tuple node is encountered, get the name (lhs).
 * Then look at each assign node and their LHS (L) and RHS (R).
 * Form a bunch of connect statements that do lhs.L <= R */
void Inou_firrtl::process_tup_asg(Lnast &ln, const Lnast_nid &lnidx_asg, const mmap_lib::str &lhs,
                                  firrtl::FirrtlPB_Statement *fstmt) {
  auto c0       = ln.get_first_child(lnidx_asg);
  auto c1       = ln.get_sibling_next(c0);
  auto ntype_c0 = ln.get_type(c0);
  I(ntype_c0.is_ref());
  auto ntype_c1 = ln.get_type(c1);
  I(ntype_c1.is_const() || ntype_c1.is_ref());

  firrtl::FirrtlPB_Expression_SubField *subfield_expr;
  if (wire_rename_map.contains(lhs)) {
    auto rename_str = wire_rename_map[lhs];
    subfield_expr   = make_subfield_expr(mmap_lib::str::concat(rename_str, ".", ln.get_name(c0)));
  } else {
    subfield_expr = make_subfield_expr(mmap_lib::str::concat(lhs, ".", ln.get_name(c0)));
  }
  auto lhs_expr = new firrtl::FirrtlPB_Expression();
  lhs_expr->set_allocated_sub_field(subfield_expr);

  auto rhs_expr = new firrtl::FirrtlPB_Expression();
  add_refcon_as_expr(ln, c1, rhs_expr);

  auto assign = new firrtl::FirrtlPB_Statement_Connect();
  assign->set_allocated_location(lhs_expr);
  assign->set_allocated_expression(rhs_expr);

  fstmt->set_allocated_connect(assign);
}

/* Take in a string, separate by "." symbol,
 * then create subfield expression out of it. */
// FIXME: See if I can't make this more general (SubInd, SubAcc, SubF)
firrtl::FirrtlPB_Expression_SubField *Inou_firrtl::make_subfield_expr(const mmap_lib::str &name) {

  auto subnames = name.split('.');
  I(subnames.size() >= 2);

  // This currently only works if subname.size() == 2
  auto subfield_expr = new firrtl::FirrtlPB_Expression_SubField();

  // Make bundle accessor
  auto tup_expr = new firrtl::FirrtlPB_Expression();
  auto ref_expr = new firrtl::FirrtlPB_Expression_Reference();
  ref_expr->set_id(subnames[0].to_s());
  tup_expr->set_allocated_reference(ref_expr);
  subfield_expr->set_allocated_expression(tup_expr);
  // Set field
  subfield_expr->set_field(subnames[1].to_s());

  // subfield_expr->PrintDebugString();
  return subfield_expr;
}

void Inou_firrtl::process_ln_bit_not_op(Lnast &ln, const Lnast_nid &lnidx_not, firrtl::FirrtlPB_Statement *fstmt) {
  auto c0       = ln.get_first_child(lnidx_not);
  auto c1       = ln.get_sibling_next(c0);
  auto ntype_c0 = ln.get_type(c0);
  I(ntype_c0.is_ref());
  auto ntype_c1 = ln.get_type(c1);
  I(ntype_c1.is_const() || ntype_c1.is_ref());

  // Form expression that holds RHS contents.
  auto *rhs_expr    = new firrtl::FirrtlPB_Expression();
  auto *rhs_prim_op = new firrtl::FirrtlPB_Expression_PrimOp();
  rhs_prim_op->set_op(firrtl::FirrtlPB_Expression_PrimOp_Op_OP_BIT_NOT);

  add_refcon_as_expr(ln, c1, rhs_prim_op->add_arg());
  rhs_expr->set_allocated_prim_op(rhs_prim_op);

  // Now assign lhs to rhs.
  make_assignment(ln, c0, rhs_expr, fstmt);
}

void Inou_firrtl::process_ln_reduce_xor_op(Lnast &ln, const Lnast_nid &lnidx_par, firrtl::FirrtlPB_Statement *fstmt) {
  auto c0       = ln.get_first_child(lnidx_par);
  auto c1       = ln.get_sibling_next(c0);
  auto ntype_c0 = ln.get_type(c0);
  I(ntype_c0.is_ref());
  auto ntype_c1 = ln.get_type(c1);
  I(ntype_c1.is_const() || ntype_c1.is_ref());

  // Form expression that holds RHS contents.
  auto *rhs_expr    = new firrtl::FirrtlPB_Expression();
  auto *rhs_prim_op = new firrtl::FirrtlPB_Expression_PrimOp();
  rhs_prim_op->set_op(firrtl::FirrtlPB_Expression_PrimOp_Op_OP_XOR_REDUCE);

  add_refcon_as_expr(ln, c1, rhs_prim_op->add_arg());
  rhs_expr->set_allocated_prim_op(rhs_prim_op);

  // Now assign lhs to rhs.
  make_assignment(ln, c0, rhs_expr, fstmt);
}

void Inou_firrtl::process_ln_nary_op(Lnast &ln, const Lnast_nid &lnidx_op, firrtl::FirrtlPB_Statement *fstmt) {
  bool        first     = true;
  bool        first_arg = true;
  uint8_t     child_count = 0;

  auto firrtl_oper_code = get_firrtl_oper_code(ln.get_data(lnidx_op).type);

  // Grab the LHS for later use. Also create PrimOP for RHS expr.
  Lnast_nid                           lnidx_lhs;
  firrtl::FirrtlPB_Expression_PrimOp *rhs_prim_op      = nullptr;
  firrtl::FirrtlPB_Expression        *rhs_expr         = nullptr;
  firrtl::FirrtlPB_Expression        *rhs_highest_expr = nullptr;
  for (const auto &lnchild_idx : ln.children(lnidx_op)) {
    if (first) {
      first     = false;
      lnidx_lhs = lnchild_idx;
    } else if (lnchild_idx == ln.get_last_child(lnidx_op)) {
      // If this is the last element, don't create another new operator.
      add_refcon_as_expr(ln, lnchild_idx, rhs_prim_op->add_arg());
    } else {
      // If this is not the last element, we'll need another operator to grab next element.

      // This is pretty dense code so here's the concept:
      // 1. Create new rhs_expr. (if first, create a holder so we don't lose it)
      // 2. Create new prim_op (rhs_prim_op) underneath the new expression.
      // 3. Attach const/ref arg to this (rhs_prim_op).
      if (first_arg) {
        first_arg        = false;
        rhs_expr         = new firrtl::FirrtlPB_Expression();
        rhs_highest_expr = rhs_expr;
      } else {
        rhs_expr = rhs_prim_op->add_arg();
      }
      rhs_prim_op = new firrtl::FirrtlPB_Expression_PrimOp();
      rhs_prim_op->set_op(firrtl_oper_code);
      rhs_expr->set_allocated_prim_op(rhs_prim_op);

      // Add current child node as member of the primitive op (rhs_prim_op)
      add_refcon_as_expr(ln, lnchild_idx, rhs_prim_op->add_arg());
    }
    child_count++;
  }

  auto ntype = ln.get_data(lnidx_op).type;
  if (ntype.is_ge() || ntype.is_gt() || ntype.is_le() || ntype.is_lt()) {
    I(child_count == 3);  // I think there should only be 1 output + 2 inputs to a comp node.
  }

  // Now assign lhs to rhs.
  make_assignment(ln, lnidx_lhs, rhs_highest_expr, fstmt);
}

/* Assign LHS to RHS Determines whether LHS is intermediate or not.
 * If so, make assignment type: node. Otherwise, do normal connect. */
void Inou_firrtl::make_assignment(Lnast &ln, const Lnast_nid &lnidx_lhs, firrtl::FirrtlPB_Expression *expr_rhs,
                                  firrtl::FirrtlPB_Statement *fstmt) {
  if (is_outp(ln.get_name(lnidx_lhs)) || is_reg(ln.get_name(lnidx_lhs)) || is_wire(ln.get_name(lnidx_lhs))) {
    create_connect_stmt(ln, lnidx_lhs, expr_rhs, fstmt);

  } else {
    /* If I'm assigning to some wire/intermediate,
     * this should make FIRRTL node statement. */
    create_node_stmt(ln, lnidx_lhs, expr_rhs, fstmt);
  }
}

firrtl::FirrtlPB_Statement_When *Inou_firrtl::process_ln_if_op(Lnast &ln, const Lnast_nid &lnidx_if) {
  auto    when_highest  = new firrtl::FirrtlPB_Statement_When();
  auto    when_lowest   = when_highest;
  uint8_t pos_to_add_to = 0;  // 0 indicates add to when_lowest's consequent, 1 means add to otherwise
  for (const auto &if_child : ln.children(lnidx_if)) {
    const auto ntype = ln.get_data(if_child).type;
    if (ntype.is_stmts()) {
      for (const auto &stmt_child : ln.children(if_child)) {
        process_ln_stmt(ln, stmt_child, when_lowest, pos_to_add_to);
      }
      pos_to_add_to++;  // Set to 1 (or 2 if last stmt)
    } else if (ntype.is_ref() || ntype.is_const()) {
      // A new cond has been found, meaning we need to create another when
      if (pos_to_add_to == 1) {
        auto new_when = new firrtl::FirrtlPB_Statement_When();
        auto new_stmt = when_lowest->add_otherwise();
        new_stmt->set_allocated_when(new_when);
        when_lowest   = new_when;
        pos_to_add_to = 0;
      }
      // Specify the 'predicate' (condition)
      auto predicate = new firrtl::FirrtlPB_Expression();
      add_refcon_as_expr(ln, if_child, predicate);
      when_lowest->set_allocated_predicate(predicate);
    } else {
      I(false);
    }
  }

  return when_highest;
}

#if 0
/* Since the range op only indicates that there is a
 * value range from the low value to the high value,
 * this is added to a map for later access. */
// Note->hunter: This only accepts positive values, and not LNAST's negative range capability.
void Inou_firrtl::process_ln_range_op(Lnast &ln, const Lnast_nid &lnidx_range) {
  auto lhs_node = ln.get_first_child(lnidx_range);
  auto range_lo = ln.get_sibling_next(lhs_node);
  auto range_hi = ln.get_sibling_next(range_lo);

  name_to_range_map[ln.get_name(lhs_node)] = {range_lo, range_hi};
}

void Inou_firrtl::process_ln_bitsel_op(Lnast &ln, const Lnast_nid &lnidx_bitsel, firrtl::FirrtlPB_Statement *fstmt) {
  auto lhs_node  = ln.get_first_child(lnidx_bitsel);
  auto elem_node = ln.get_sibling_next(lhs_node);
  auto range_acc = ln.get_sibling_next(elem_node);

  auto range_pair = name_to_range_map[ln.get_name(range_acc)];
  // Note->hunter: FIRRTL requires range values to be constant numbers.
  I(ln.get_data(range_pair.first).type.is_const() && ln.get_data(range_pair.second).type.is_const());

  // Create rhs expr, make if a prim op of type "extract bits"
  firrtl::FirrtlPB_Expression *       rhs_expr         = new firrtl::FirrtlPB_Expression();
  auto                                firrtl_oper_code = get_firrtl_oper_code(ln.get_data(lnidx_bitsel).type);
  firrtl::FirrtlPB_Expression_PrimOp *rhs_prim_op      = new firrtl::FirrtlPB_Expression_PrimOp();
  rhs_prim_op->set_op(firrtl_oper_code);

  // Add arguments to "EXTRACT_BITS" op
  add_refcon_as_expr(ln, elem_node, rhs_prim_op->add_arg());
  add_const_as_ilit(ln, range_pair.second, rhs_prim_op->add_const_());
  add_const_as_ilit(ln, range_pair.first, rhs_prim_op->add_const_());
  rhs_expr->set_allocated_prim_op(rhs_prim_op);

  // Now assign lhs to rhs.
  make_assignment(ln, lhs_node, rhs_expr, fstmt);
}
#endif

bool Inou_firrtl::process_ln_select(Lnast &ln, const Lnast_nid &lnidx_dot, firrtl::FirrtlPB_Statement *fstmt) {
  auto lhs   = ln.get_first_child(lnidx_dot);
  auto tup   = ln.get_sibling_next(lhs);
  auto field = ln.get_sibling_next(tup);

  dot_map[ln.get_name(lhs)] = {tup, field};

  if (!(ln.get_name(field).substr(0, 2) == "__")) {
    /* If this is not a dot for an attribute, then create an
     * assign statement to hold value. */
    // NOTE/FIXME: Doing it this way means we can have no nested bundles or
    // vecs in the LNAST. If there's a dot, it's either an attribute or accessing
    // an element of a bundle/vec. That element cannot be another bundle/vec.
    auto tup_name = ln.get_name(tup);
    if (wire_rename_map.contains(tup_name)) {
      tup_name = wire_rename_map[tup_name];
    }
    auto field_name    = ln.get_name(field);
    auto subfield_expr = make_subfield_expr(mmap_lib::str::concat(tup_name, ".", field_name));
    auto expr          = new firrtl::FirrtlPB_Expression();
    expr->set_allocated_sub_field(subfield_expr);

    make_assignment(ln, lhs, expr, fstmt);
    return true;
  } else if (ln.get_name(field) == "__q_pin") {
    // For FIRRTL, this is just the same lhs = tup
    auto *rhs_ref = new firrtl::FirrtlPB_Expression_Reference();
    rhs_ref->set_id(ln.get_name(tup).substr(1).to_s());  // Remove 1st char since it's a '#'
    auto expr = new firrtl::FirrtlPB_Expression();
    expr->set_allocated_reference(rhs_ref);
    make_assignment(ln, lhs, expr, fstmt);
    return true;
  } else {
    // This is for an attribute, no FIRRTL assign statement needed.
    return false;
  }
}

void Inou_firrtl::handle_attr_assign(Lnast &ln, const Lnast_nid &lhs, const Lnast_nid &rhs) {
  auto pair      = dot_map[ln.get_name(lhs)];
  auto var       = pair.first;
  auto var_name  = ln.get_name(var);
  auto attr      = pair.second;
  auto attr_name = ln.get_name(attr);

  if (attr_name == "__clk_pin") {
    handle_clock_attr(ln, var_name, rhs);
  } else if (attr_name == "__reset_pin") {
    handle_reset_attr(ln, var_name, rhs);
  } else if (attr_name == "__reset_async") {
    handle_async_attr(ln, var_name, rhs);
  } else if (attr_name == "__sign") {
    handle_sign_attr(ln, var_name, rhs);
  } else if (attr_name == "__posedge") {
    if (ln.get_name(rhs) == "false") {
      Pass::warn(
          "Attribute __posedge was set to false, but currently FIRRTL does not support negedge-triggered registers. Will ignore.");
    }
  } else if (attr_name == "__bits") {
    // Ignore. Should have only been specified in bw_table attribute of LNAST.
  } else if (attr_name == "__latch") {
    if (ln.get_name(rhs) == "true") {
      Pass::error("A latch is in your design, but latches are not supported in FIRRTL. Cannot translate.");
    }
  } else {
    fmt::print("Error: compiler attribute \"{}\" found, but it is either incorrect or not supported yet.\n", attr_name);
  }
}

void Inou_firrtl::handle_sign_attr(Lnast &ln, const mmap_lib::str &var_name, const Lnast_nid &rhs) {
  if (ln.get_name(rhs) != "true") {
    // If the sign is being set to not true, then it's a UInt (which is already the default).
    return;
  }

  if (io_map.contains(var_name.substr(1))) {
    auto port_ptr = io_map[var_name.substr(1)];
    if (port_ptr->type().has_sint_type()) {
      return;
    } else if (port_ptr->type().has_uint_type()) {
      auto sint_type = new firrtl::FirrtlPB_Type_SIntType();
      auto width     = new firrtl::FirrtlPB_Width();
      width->set_value(port_ptr->type().uint_type().width().value());
      sint_type->set_allocated_width(width);
      auto type = new firrtl::FirrtlPB_Type();
      type->set_allocated_sint_type(sint_type);
      port_ptr->set_allocated_type(type);
    } else {
      // FIXME: Unsure of how to deal with conflicting types.
      I(false);
    }
  } else if (reg_wire_map.contains(var_name.substr(1))) {
    I(reg_wire_map[var_name.substr(1)]->has_register_());
    auto reg_ptr = reg_wire_map[var_name.substr(1)]->mutable_register_();
    if (reg_ptr->type().has_sint_type()) {
      return;
    } else if (reg_ptr->type().has_uint_type()) {
      auto sint_type = new firrtl::FirrtlPB_Type_SIntType();
      auto width     = new firrtl::FirrtlPB_Width();
      width->set_value(reg_ptr->type().uint_type().width().value());
      sint_type->set_allocated_width(width);
      auto type = new firrtl::FirrtlPB_Type();
      type->set_allocated_sint_type(sint_type);
      reg_ptr->set_allocated_type(type);
    } else {
      // FIXME: Unsure of how to deal with conflicting types.
      I(false);
    }
  } else if (reg_wire_map.contains(var_name)) {
    I(reg_wire_map[var_name]->has_wire());
    auto wire_ptr = reg_wire_map[var_name.substr(1)]->mutable_wire();
    if (wire_ptr->type().has_sint_type()) {
      return;
    } else if (wire_ptr->type().has_uint_type()) {
      auto sint_type = new firrtl::FirrtlPB_Type_SIntType();
      auto width     = new firrtl::FirrtlPB_Width();
      width->set_value(wire_ptr->type().uint_type().width().value());
      sint_type->set_allocated_width(width);
      auto type = new firrtl::FirrtlPB_Type();
      type->set_allocated_sint_type(sint_type);
      wire_ptr->set_allocated_type(type);
    } else {
      // FIXME: Unsure of how to deal with conflicting types.
      I(false);
    }
  } else {
    /* A temporary variable was declared as signed...
     * In theory (if this makes sense), FIRRTL's type
     * inference should take care of this. */
    return;
  }
}

void Inou_firrtl::handle_clock_attr(Lnast &ln, const mmap_lib::str &var_name, const Lnast_nid &rhs) {
  I(reg_wire_map.contains(var_name.substr(1)));
  auto stmt = reg_wire_map[var_name.substr(1)];
  I(stmt->has_register_());
  auto reg = stmt->mutable_register_();

  /* Now we need to set whatever is represented by var_name to have type Clock.
   * If it's a circuit component, then just change the type to be clock. If it's
   * not, then we have to create as asClock expression to convert the intermediate */
  // FIXME: Perhaps I should add checks to make sure changing this to Clock won't break something else.
  auto clk_name = ln.get_name(rhs);
  if (io_map.contains(clk_name.substr(1))) {
    auto port_ptr = io_map[clk_name.substr(1)];
    auto clk_type = new firrtl::FirrtlPB_Type_ClockType();
    auto type     = new firrtl::FirrtlPB_Type();
    type->set_allocated_clock_type(clk_type);
    port_ptr->set_allocated_type(type);

  } else if (reg_wire_map.contains(clk_name.substr(1))) {
    auto stmt_ptr = reg_wire_map[clk_name.substr(1)];
    auto clk_type = new firrtl::FirrtlPB_Type_ClockType();
    auto type     = new firrtl::FirrtlPB_Type();
    type->set_allocated_clock_type(clk_type);
    stmt_ptr->mutable_register_()->set_allocated_type(type);

  } else if (reg_wire_map.contains(clk_name)) {
    auto stmt_ptr = reg_wire_map[clk_name];
    auto clk_type = new firrtl::FirrtlPB_Type_ClockType();
    auto type     = new firrtl::FirrtlPB_Type();
    type->set_allocated_clock_type(clk_type);
    stmt_ptr->mutable_wire()->set_allocated_type(type);

  } else {
    auto *prim = new firrtl::FirrtlPB_Expression_PrimOp();
    prim->set_op(firrtl::FirrtlPB_Expression_PrimOp_Op_OP_AS_CLOCK);
    auto arg = prim->add_arg();
    add_refcon_as_expr(ln, rhs, arg);
    auto *expr = new firrtl::FirrtlPB_Expression();
    expr->set_allocated_prim_op(prim);

    reg->set_allocated_clock(expr);
    return;  // Don't do final step, since we did actual assignment on previous line
  }

  // Set the register's clock.
  auto clk_expr = new firrtl::FirrtlPB_Expression();
  add_refcon_as_expr(ln, rhs, clk_expr);
  reg->set_allocated_clock(clk_expr);
}

// (See handle_clock_attr for description)
// Note: __reseet_async has to come before __reset_pin
void Inou_firrtl::handle_async_attr(Lnast &ln, const mmap_lib::str &var_name, const Lnast_nid &rhs) {
  if (ln.get_name(rhs) == "true") {
    async_regs.insert(var_name.substr(1));
    fmt::print("Adding to async_regs: {}\n", var_name.substr(1));
  }
}

// (See handle_clock_attr for description)
void Inou_firrtl::handle_reset_attr(Lnast &ln, const mmap_lib::str &var_name, const Lnast_nid &rhs) {
  I(reg_wire_map.contains(var_name.substr(1)));
  auto stmt = reg_wire_map[var_name.substr(1)];
  I(stmt->has_register_());
  auto reg = stmt->mutable_register_();

  auto reset_name = ln.get_name(rhs);
  if (io_map.contains(reset_name.substr(1))) {
    auto port_ptr = io_map[reset_name.substr(1)];
    auto type     = new firrtl::FirrtlPB_Type();
    if (async_regs.contains(var_name.substr(1))) {
      auto areset_type = new firrtl::FirrtlPB_Type_AsyncResetType();
      type->set_allocated_async_reset_type(areset_type);
    } else {
      auto reset_type = new firrtl::FirrtlPB_Type_ResetType();
      type->set_allocated_reset_type(reset_type);
    }
    port_ptr->set_allocated_type(type);

  } else if (reg_wire_map.contains(reset_name.substr(1))) {
    auto stmt_ptr = reg_wire_map[reset_name.substr(1)];
    auto type     = new firrtl::FirrtlPB_Type();
    if (async_regs.contains(var_name.substr(1))) {
      auto areset_type = new firrtl::FirrtlPB_Type_AsyncResetType();
      type->set_allocated_async_reset_type(areset_type);
    } else {
      auto reset_type = new firrtl::FirrtlPB_Type_ResetType();
      type->set_allocated_reset_type(reset_type);
    }
    stmt_ptr->mutable_register_()->set_allocated_type(type);

  } else if (reg_wire_map.contains(reset_name)) {
    auto stmt_ptr = reg_wire_map[reset_name];
    auto type     = new firrtl::FirrtlPB_Type();
    if (async_regs.contains(var_name.substr(1))) {
      auto areset_type = new firrtl::FirrtlPB_Type_AsyncResetType();
      type->set_allocated_async_reset_type(areset_type);
    } else {
      auto reset_type = new firrtl::FirrtlPB_Type_ResetType();
      type->set_allocated_reset_type(reset_type);
    }
    stmt_ptr->mutable_wire()->set_allocated_type(type);

  } else {
    auto *prim = new firrtl::FirrtlPB_Expression_PrimOp();
    prim->set_op(firrtl::FirrtlPB_Expression_PrimOp_Op_OP_AS_UINT);
    auto arg = prim->add_arg();
    add_refcon_as_expr(ln, rhs, arg);
    auto *expr = new firrtl::FirrtlPB_Expression();
    expr->set_allocated_prim_op(prim);

    reg->set_allocated_reset(expr);
    return;  // Don't do final step, since we did actual assignment on previous line
  }

  // Set the register's clock.
  auto reset_expr = new firrtl::FirrtlPB_Expression();
  add_refcon_as_expr(ln, rhs, reset_expr);
  reg->set_allocated_reset(reset_expr);
}

//----- Helper Functions -----
bool Inou_firrtl::is_inp(const mmap_lib::str &str) { return str.front() == '$'; }

bool Inou_firrtl::is_outp(const mmap_lib::str &str) { return str.front() == '%'; }

bool Inou_firrtl::is_reg(const mmap_lib::str &str) { return str.front() == '#'; }

bool Inou_firrtl::is_wire(const mmap_lib::str &str) {
  // Anything that doesn't have a prefix
  auto first_char = str.front();
  return !(str.substr(0, 3) == "___") && !(first_char == '#') && !(first_char == '$') && !(first_char == '%');
}

mmap_lib::str Inou_firrtl::get_firrtl_name_format(Lnast &ln, const Lnast_nid &lnidx) {
  auto       ntype = ln.get_type(lnidx);
  const auto str   = ln.get_name(lnidx);
  if (ntype.is_ref() || ntype.is_const()) {
    return strip_prefixes(str);
  } else if (ntype.is_const()) {
    return mmap_lib::str(ln.get_name(lnidx));
  }
  fmt::print("{}\n", str);
  I(false);  // When getting names, I would think we should only be checking those two node types.
  return "";
}

mmap_lib::str Inou_firrtl::strip_prefixes(const mmap_lib::str &str) {

  auto skip=0u;

  auto ch = str.front();
  if (ch == '$' || ch == '%' || ch == '#')
    skip=1;
  else if (ch=='_' && str.substr(0,3) == "_._")
    skip=3;

  if (skip)
    return str.substr(skip);
  return str;
}

/* If the LHS of some sort of statement that does assigning
 * is a register or an output, then the statement needs to
 * be treated as a FIRRTL "connect" statement. */
void Inou_firrtl::create_connect_stmt(Lnast &ln, const Lnast_nid &lhs, firrtl::FirrtlPB_Expression *rhs_expr,
                                      firrtl::FirrtlPB_Statement *fstmt) {
  auto *lhs_ref = new firrtl::FirrtlPB_Expression_Reference();
  lhs_ref->set_id(get_firrtl_name_format(ln, lhs).to_s());

  auto *lhs_expr = new firrtl::FirrtlPB_Expression();
  lhs_expr->set_allocated_reference(lhs_ref);

  auto *conn = new firrtl::FirrtlPB_Statement_Connect();
  conn->set_allocated_location(lhs_expr);
  conn->set_allocated_expression(rhs_expr);

  fstmt->set_allocated_connect(conn);
}

/* If the LHS of some sort of statement that does assigning
 * is not a register or an output or a wire, then the statement
 * needs to be treated as a FIRRTL "node" statement. */
void Inou_firrtl::create_node_stmt(Lnast &ln, const Lnast_nid &lhs, firrtl::FirrtlPB_Expression *rhs_expr,
                                   firrtl::FirrtlPB_Statement *fstmt) {
  auto *node = new firrtl::FirrtlPB_Statement_Node();
  node->set_id(ln.get_name(lhs).to_s());
  node->set_allocated_expression(rhs_expr);

  fstmt->set_allocated_node(node);
}

firrtl::FirrtlPB_Expression_PrimOp_Op Inou_firrtl::get_firrtl_oper_code(const Lnast_ntype &ntype) {
  if (ntype.is_plus()) {
    return firrtl::FirrtlPB_Expression_PrimOp_Op_OP_ADD;
  } else if (ntype.is_minus()) {
    return firrtl::FirrtlPB_Expression_PrimOp_Op_OP_SUB;
  } else if (ntype.is_mult()) {
    return firrtl::FirrtlPB_Expression_PrimOp_Op_OP_TIMES;
  } else if (ntype.is_div()) {
    return firrtl::FirrtlPB_Expression_PrimOp_Op_OP_DIVIDE;
  } else if (ntype.is_mod()) {
    return firrtl::FirrtlPB_Expression_PrimOp_Op_OP_REM;
  } else if (ntype.is_ge()) {
    return firrtl::FirrtlPB_Expression_PrimOp_Op_OP_GREATER_EQ;
  } else if (ntype.is_gt()) {
    return firrtl::FirrtlPB_Expression_PrimOp_Op_OP_GREATER;
  } else if (ntype.is_le()) {
    return firrtl::FirrtlPB_Expression_PrimOp_Op_OP_LESS_EQ;
  } else if (ntype.is_lt()) {
    return firrtl::FirrtlPB_Expression_PrimOp_Op_OP_LESS;
  } else if (ntype.is_ne()) {
    return firrtl::FirrtlPB_Expression_PrimOp_Op_OP_NOT_EQUAL;
  } else if (ntype.is_eq()) {
    return firrtl::FirrtlPB_Expression_PrimOp_Op_OP_EQUAL;
  } else if (ntype.is_bit_and()) {
    return firrtl::FirrtlPB_Expression_PrimOp_Op_OP_BIT_AND;
  } else if (ntype.is_bit_or()) {
    return firrtl::FirrtlPB_Expression_PrimOp_Op_OP_BIT_OR;
  } else if (ntype.is_bit_xor()) {
    return firrtl::FirrtlPB_Expression_PrimOp_Op_OP_BIT_XOR;
  } else if (ntype.is_shl()) {
    return firrtl::FirrtlPB_Expression_PrimOp_Op_OP_DYNAMIC_SHIFT_LEFT;
  } else if (ntype.is_shr()) {
    return firrtl::FirrtlPB_Expression_PrimOp_Op_OP_DYNAMIC_SHIFT_RIGHT;
  } else {
    I(false);  // some nary op not yet supported
    return firrtl::FirrtlPB_Expression_PrimOp_Op_OP_UNKNOWN;
  }
}

/* Provided some ref or const node and an expression pointer,
 * this will form that ref/const in the expression pointer. */
void Inou_firrtl::add_refcon_as_expr(Lnast &ln, const Lnast_nid &lnidx, firrtl::FirrtlPB_Expression *expr) {
  if (ln.get_data(lnidx).type.is_ref()) {
    // Lnidx is a variable, so I need to make a Reference argument.
    auto                                   str     = get_firrtl_name_format(ln, lnidx);
    auto *rhs_ref = new firrtl::FirrtlPB_Expression_Reference();
    rhs_ref->set_id(str.to_s());
    expr->set_allocated_reference(rhs_ref);

  } else if (ln.get_data(lnidx).type.is_const()) {
    // Lnidx is a number, so I need to make a [U/S]IntLiteral argument.
    auto lconst_holder = Lconst::from_string(ln.get_name(lnidx));
    auto lconst_str    = ln.get_name(lnidx).to_s();

    auto *num = new firrtl::FirrtlPB_Expression_IntegerLiteral();
    num->set_value(lconst_str);
    auto *width = new firrtl::FirrtlPB_Width();
    width->set_value(lconst_holder.get_bits());

    if (lconst_holder.is_negative()) {
      auto *ulit = new firrtl::FirrtlPB_Expression_UIntLiteral();
      ulit->set_allocated_value(num);
      ulit->set_allocated_width(width - 1);
      expr->set_allocated_uint_literal(ulit);
    } else {
      auto *slit = new firrtl::FirrtlPB_Expression_SIntLiteral();
      slit->set_allocated_value(num);
      slit->set_allocated_width(width);
      expr->set_allocated_sint_literal(slit);
    }

  } else {
    I(false);  // Should const and ref be only things allowed on RHS?
  }
}

/* Provided a const node and an IntegerLit pointer,
 * this will form that const in the pointer. */
void Inou_firrtl::add_const_as_ilit(Lnast &ln, const Lnast_nid &lnidx, firrtl::FirrtlPB_Expression_IntegerLiteral *ilit) {
  I(ln.get_data(lnidx).type.is_const());

  auto lconst_holder = Lconst::from_string(ln.get_name(lnidx));
  auto lconst_str    = lconst_holder.to_firrtl().to_s();

  ilit->set_value(lconst_str);
}

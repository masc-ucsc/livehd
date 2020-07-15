//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <iostream>
#include <fstream>
#include <google/protobuf/message.h>

#include "inou_firrtl.hpp"

#define PRINT_DEBUG

void Inou_firrtl::toFIRRTL(Eprp_var &var) {
  Inou_firrtl p(var);
  for (const auto &lnast : var.lnasts) {
    //lnast->ssa_trans();//FIXME: Do I need to do SSA?

    //firrtl::FirrtlPB_Circuit circuit;
    p.do_tofirrtl(lnast);//, circuit);
  }
}

void Inou_firrtl::do_tofirrtl(std::shared_ptr<Lnast> ln) {//, firrtl::FirrtlPB_Circuit& circuit) {
  const auto top   = ln->get_root();
  const auto stmts = ln->get_first_child(top);
  firrtl::FirrtlPB fir_design;//firrtl::FirrtlPB()
  firrtl::FirrtlPB_Circuit *circuit = fir_design.add_circuit();//new firrtl::FirrtlPB_Circuit();
  //FIXME: I need to add a "top" message to Circuit.
  auto top_msg = circuit->add_top();
  top_msg->set_name("Trivial");//FIXME: Placeholder for now

  firrtl::FirrtlPB_Module *mod = circuit->add_module();
  firrtl::FirrtlPB_Module_UserModule *umod = new firrtl::FirrtlPB_Module_UserModule();
  FindCircuitComps(*ln, umod);

  for (const auto &lnidx : ln->children(stmts)) {
    process_ln_stmt(*ln, lnidx, umod);
  }
  mod->set_allocated_user_module(umod);
  fir_design.PrintDebugString();

  std::fstream output("Trivial_testout.pb", std::ios::out | std::ios::trunc | std::ios::binary);
  if (!fir_design.SerializeToOstream(&output)) {
    fmt::print("Failed to write firrtl design\n");
  }
  //google::protobuf::ShutDownProtobufLibrary();
}

void Inou_firrtl::process_ln_stmt(Lnast &ln, const Lnast_nid &lnidx, firrtl::FirrtlPB_Statement_When *when, uint8_t pos_to_add_to) {
  const auto ntype = ln.get_data(lnidx).type;
  // FIXME->sh: how to use switch to gain performance?
  if (ntype.is_assign()) {
    auto fstmt = pos_to_add_to == 0 ? when->add_consequent() : when->add_otherwise();
    process_ln_assign_op(ln, lnidx, fstmt);
  } else if (ntype.is_nary_op()) {
    auto fstmt = pos_to_add_to == 0 ? when->add_consequent() : when->add_otherwise();
    process_ln_nary_op(ln, lnidx, fstmt);
  } else if (ntype.is_not()) {
    auto fstmt = pos_to_add_to == 0 ? when->add_consequent() : when->add_otherwise();
    process_ln_not_op(ln, lnidx, fstmt);
  /*} else if (ntype.is_tuple_add()) {
    process_ast_tuple_add_op(dfg, lnidx);
  } else if (ntype.is_tuple_get()) {
    process_ast_tuple_get_op(dfg, lnidx);
  } else if (ntype.is_tuple_phi_add()) {
    process_ast_tuple_phi_add_op(dfg, lnidx);
  } else if (ntype.is_dot()) {
    I(false); // should has been converted to tuple chain 
  } else if (ntype.is_select()) {
    I(false); // should has been converted to tuple chain
  } else if (ntype.is_logical_op()) {
    process_ast_logical_op(dfg, lnidx);
  } else if (ntype.is_as()) {
    process_ast_as_op(dfg, lnidx);
  } else if (ntype.is_label()) {
    process_ast_label_op(dfg, lnidx);
  } else if (ntype.is_dp_assign()) {
    process_ast_dp_assign_op(dfg, lnidx);
  } else if (ntype.is_tuple()) {
    process_ast_tuple_struct(dfg, lnidx);
  } else if (ntype.is_tuple_concat()) {
    process_ast_concat_op(dfg, lnidx);*/
  } else if (ntype.is_if()) {
    add_cstmts(ln, lnidx, when, pos_to_add_to);
    auto nested_when_stmt = process_ln_if_op(ln, lnidx);
    auto fstmt = pos_to_add_to == 0 ? when->add_consequent() : when->add_otherwise();
    fstmt->set_allocated_when(nested_when_stmt);
  }/* else if (ntype.is_uif()) {
    process_ast_uif_op(dfg, lnidx);
  } else if (ntype.is_func_call()) {
    process_ast_func_call_op(dfg, lnidx);
  } else if (ntype.is_func_def()) {
    process_ast_func_def_op(dfg, lnidx);
  } else if (ntype.is_for()) {
    process_ast_for_op(dfg, lnidx);
  } else if (ntype.is_while()) {
    process_ast_while_op(dfg, lnidx);
  } else if (ntype.is_invalid()) { // FIXME->sh: add ignore type in LNAST?
    continue;
  }*/ else if (ntype.is_const()) {
    I(ln.get_name(lnidx) == "default_const");
    return;
  } else if (ntype.is_err_flag()) {
    I(ln.get_name(lnidx) == "err_var_undefined");
    return;
  } else {
    //I(false);
    //return;
  }
}

void Inou_firrtl::process_ln_stmt(Lnast &ln, const Lnast_nid &lnidx, firrtl::FirrtlPB_Module_UserModule *umod) {
  const auto ntype = ln.get_data(lnidx).type;
  // FIXME->sh: how to use switch to gain performance?
  if (ntype.is_assign()) {
    auto fstmt = umod->add_statement();
    process_ln_assign_op(ln, lnidx, fstmt);
  } else if (ntype.is_nary_op()) {
    auto fstmt = umod->add_statement();
    process_ln_nary_op(ln, lnidx, fstmt);
  } else if (ntype.is_not()) {
    auto fstmt = umod->add_statement();
    process_ln_not_op(ln, lnidx, fstmt);
  /*} else if (ntype.is_tuple_add()) {
    process_ast_tuple_add_op(dfg, lnidx);
  } else if (ntype.is_tuple_get()) {
    process_ast_tuple_get_op(dfg, lnidx);
  } else if (ntype.is_tuple_phi_add()) {
    process_ast_tuple_phi_add_op(dfg, lnidx);
  } else if (ntype.is_dot()) {
    I(false); // should has been converted to tuple chain 
  } else if (ntype.is_select()) {
    I(false); // should has been converted to tuple chain
  } else if (ntype.is_logical_op()) {
    process_ast_logical_op(dfg, lnidx);
  } else if (ntype.is_as()) {
    process_ast_as_op(dfg, lnidx);
  } else if (ntype.is_label()) {
    process_ast_label_op(dfg, lnidx);
  } else if (ntype.is_dp_assign()) {
    process_ast_dp_assign_op(dfg, lnidx);
  } else if (ntype.is_tuple()) {
    process_ast_tuple_struct(dfg, lnidx);
  } else if (ntype.is_tuple_concat()) {
    process_ast_concat_op(dfg, lnidx);*/
  } else if (ntype.is_if()) {
    add_cstmts(ln, lnidx, umod);
    auto when_stmt = process_ln_if_op(ln, lnidx);
    auto fstmt = umod->add_statement();
    fstmt->set_allocated_when(when_stmt);
  }/* else if (ntype.is_uif()) {
    process_ast_uif_op(dfg, lnidx);
  } else if (ntype.is_func_call()) {
    process_ast_func_call_op(dfg, lnidx);
  } else if (ntype.is_func_def()) {
    process_ast_func_def_op(dfg, lnidx);
  } else if (ntype.is_for()) {
    process_ast_for_op(dfg, lnidx);
  } else if (ntype.is_while()) {
    process_ast_while_op(dfg, lnidx);
  } else if (ntype.is_invalid()) { // FIXME->sh: add ignore type in LNAST?
    continue;
  }*/ else if (ntype.is_const()) {
    I(ln.get_name(lnidx) == "default_const");
    return;
  } else if (ntype.is_err_flag()) {
    I(ln.get_name(lnidx) == "err_var_undefined");
    return;
  } else {
    //I(false);
    //return;
  }
}

void Inou_firrtl::process_ln_assign_op(Lnast &ln, const Lnast_nid &lnidx_assign, firrtl::FirrtlPB_Statement* fstmt) {
  auto c0 = ln.get_first_child(lnidx_assign);
  auto c1 = ln.get_sibling_next(c0);
  auto ntype_c0 = ln.get_type(c0);
  I(ntype_c0.is_ref());
  auto ntype_c1 = ln.get_type(c1);
  I(ntype_c1.is_const() || ntype_c1.is_ref());//FIXME: May have to expand to other types.

  // Form expression that holds RHS contents.
  firrtl::FirrtlPB_Expression *rhs_expr = new firrtl::FirrtlPB_Expression();
  if (ntype_c1.is_ref()) {
    // RHS is a variable, so I need to make a Reference.
    firrtl::FirrtlPB_Expression_Reference *rhs_ref = new firrtl::FirrtlPB_Expression_Reference();
    rhs_ref->set_id(get_firrtl_name_format(ln, c1));
    rhs_expr->set_allocated_reference(rhs_ref);
  } else if (ntype_c1.is_const()) {
    /* RHS is a number, so I need to make a UIntLiteral (which has the value
     * stored in an IntegerLiteral and the bitwidth stored in a Width). */
    create_integer_object(ln, c1, rhs_expr);
  } else {
      I(false); //FIXME: Should const and ref be only things allowed on RHS?
  }

  /* Now handle LHS. If LHS is an output or register then
   * the statement should be a Connect. If it isn't, then the
   * statement should be a Node. */
  if (is_outp(ln.get_name(c0)) || is_reg(ln.get_name(c0)) || is_wire(ln.get_name(c0))) {
    create_connect_stmt(ln, c0, rhs_expr, fstmt);

    #ifdef PRINT_DEBUG
    fmt::print("{} <= {}\n", get_firrtl_name_format(ln, c0), get_firrtl_name_format(ln, c1));
    #endif

  } else {
    /* If I'm assigning to some wire/intermediate,
     * I can make FIRRTL node statement. */
    create_node_stmt(ln, c0, rhs_expr, fstmt);

    #ifdef PRINT_DEBUG
    fmt::print("node {} = {}\n", get_firrtl_name_format(ln, c0), get_firrtl_name_format(ln, c1));
    #endif
  }
}

void Inou_firrtl::process_ln_not_op(Lnast &ln, const Lnast_nid &lnidx_not, firrtl::FirrtlPB_Statement* fstmt) {
  auto c0 = ln.get_first_child(lnidx_not);
  auto c1 = ln.get_sibling_next(c0);
  auto ntype_c0 = ln.get_type(c0);
  I(ntype_c0.is_ref());
  auto ntype_c1 = ln.get_type(c1);
  I(ntype_c1.is_const() || ntype_c1.is_ref());//FIXME: May have to expand to other types.

  // Form expression that holds RHS contents.
  firrtl::FirrtlPB_Expression *rhs_expr = new firrtl::FirrtlPB_Expression();
  firrtl::FirrtlPB_Expression_PrimOp *rhs_prim_op = new firrtl::FirrtlPB_Expression_PrimOp();
  rhs_prim_op->set_op(firrtl::FirrtlPB_Expression_PrimOp_Op_OP_BIT_NOT);

  add_const_or_ref_to_primop(ln, c1, rhs_prim_op);
  rhs_expr->set_allocated_prim_op(rhs_prim_op);

  /* Now handle LHS. If LHS is an output or register then
   * the statement should be a Connect. If it isn't, then the
   * statement should be a Node. */
  if (is_outp(ln.get_name(c0)) || is_reg(ln.get_name(c0)) || is_wire(ln.get_name(c0))) {
    create_connect_stmt(ln, c0, rhs_expr, fstmt);

    #ifdef PRINT_DEBUG
    fmt::print("{} <= not({})\n", get_firrtl_name_format(ln, c0), get_firrtl_name_format(ln, c1));
    #endif

  } else {
    /* If I'm assigning to some wire/intermediate,
     * I can make FIRRTL node statement. */
    create_node_stmt(ln, c0, rhs_expr, fstmt);

    #ifdef PRINT_DEBUG
    fmt::print("node {} = not({})\n", get_firrtl_name_format(ln, c0), get_firrtl_name_format(ln, c1));
    #endif
  }
}

void Inou_firrtl::process_ln_nary_op(Lnast &ln, const Lnast_nid &lnidx_op, firrtl::FirrtlPB_Statement* fstmt) {
  bool first = true;
  bool first_arg = true;
  std::string lhs, rhs;
  uint8_t child_count = 0;

  auto ntype = ln.get_data(lnidx_op).type;
  if (ntype.is_ge() || ntype.is_gt() || ntype.is_le() || ntype.is_lt()) {
    I(child_count == 3); //I think there should only be 1 output + 2 inputs to a comp node.
  }

  auto firrtl_oper_code = get_firrtl_oper_code(ln.get_data(lnidx_op).type);

  // Grab the LHS for later use. Also create PrimOP for RHS expr.
  Lnast_nid lnidx_lhs;
  firrtl::FirrtlPB_Expression_PrimOp *rhs_prim_op = NULL;
  firrtl::FirrtlPB_Expression        *rhs_expr = NULL;
  firrtl::FirrtlPB_Expression        *rhs_highest_expr = NULL;
  for (const auto &lnchild_idx : ln.children(lnidx_op)) {
    if (first) {
      first = false;
      lnidx_lhs = lnchild_idx;
    } else if (lnchild_idx == ln.get_last_child(lnidx_op)) {
      // If this is the last element, don't create another new operator.
      add_const_or_ref_to_primop(ln, lnchild_idx, rhs_prim_op);
    } else {
      // If this is not the last element, we'll need another operator to grab next element.

      // This is pretty dense code so here's the concept:
      // 1. Create new rhs_expr. (if first, create a holder so we don't lose it)
      // 2. Create new prim_op (rhs_prim_op) underneath the new expression.
      // 3. Attach const/ref arg to this (rhs_prim_op).
      if (first_arg) {
        first_arg = false;
        rhs_expr = new firrtl::FirrtlPB_Expression();
        rhs_highest_expr = rhs_expr;
      } else {
        rhs_expr = rhs_prim_op->add_arg();
      }
      rhs_prim_op = new firrtl::FirrtlPB_Expression_PrimOp();
      rhs_prim_op->set_op(firrtl_oper_code);
      rhs_expr->set_allocated_prim_op(rhs_prim_op);

      // Add current child node as member of the primitive op (rhs_prim_op)
      add_const_or_ref_to_primop(ln, lnchild_idx, rhs_prim_op);
    }
    child_count++;
  }

  if (is_outp(ln.get_name(lnidx_lhs)) || is_reg(ln.get_name(lnidx_lhs)) || is_wire(ln.get_name(lnidx_lhs))) {
    create_connect_stmt(ln, lnidx_lhs, rhs_highest_expr, fstmt);

    #ifdef PRINT_DEBUG
    fmt::print("{} <= nary_op...\n", get_firrtl_name_format(ln, lnidx_lhs));
    #endif

  } else {
    /* If I'm assigning to some wire/intermediate,
     * I can make FIRRTL node statement. */
    create_node_stmt(ln, lnidx_lhs, rhs_highest_expr, fstmt);

    #ifdef PRINT_DEBUG
    fmt::print("node {} = nary_op...\n", get_firrtl_name_format(ln, lnidx_lhs));
    #endif
  }

}

void Inou_firrtl::add_cstmts(Lnast &ln, const Lnast_nid &lnidx_if, firrtl::FirrtlPB_Module_UserModule *umod) {
  for (const auto &if_child : ln.children(lnidx_if)) {
    const auto ntype = ln.get_data(if_child).type;
    if (ntype.is_cstmts()) {
      process_ln_stmt(ln, if_child, umod);
    }
  }
}

void Inou_firrtl::add_cstmts(Lnast &ln, const Lnast_nid &lnidx_if, firrtl::FirrtlPB_Statement_When *when, uint8_t pos_to_add_to) {
  for (const auto &if_child : ln.children(lnidx_if)) {
    const auto ntype = ln.get_data(if_child).type;
    if (ntype.is_cstmts()) {
      process_ln_stmt(ln, if_child, when, pos_to_add_to);
    }
  }
}

firrtl::FirrtlPB_Statement_When* Inou_firrtl::process_ln_if_op(Lnast &ln, const Lnast_nid &lnidx_if) {
  auto when_highest = new firrtl::FirrtlPB_Statement_When();
  auto when_lowest = when_highest;
  uint8_t pos_to_add_to = 0; //0 indicates add to when_lowest's consequent, 1 means add to otherwise
  for (const auto &if_child : ln.children(lnidx_if)) {
    const auto ntype = ln.get_data(if_child).type;
    if (ntype.is_stmts()) {
      process_ln_stmt(ln, if_child, when_lowest, pos_to_add_to);
      pos_to_add_to++; // Set to 1 (or 2 if last stmt)
    } else if (ntype.is_cond()) {
      // A new cond has been found, meaning we need to create another when
      if (pos_to_add_to == 1) {
        auto new_when = new firrtl::FirrtlPB_Statement_When();
        auto new_stmt = when_lowest->add_otherwise();
        new_stmt->set_allocated_when(new_when);
        when_lowest = new_when;
        pos_to_add_to = 0;
      }
      // Specify the 'predicate' (condition)
      auto predicate = new firrtl::FirrtlPB_Expression();
      if (isdigit(ln.get_name(if_child)[0])) {
        create_integer_object(ln, if_child, predicate);
      } else {
        auto ref = new firrtl::FirrtlPB_Expression_Reference();
        ref->set_id(get_firrtl_name_format(ln, if_child));
        predicate->set_allocated_reference(ref);
      }
      when_lowest->set_allocated_predicate(predicate);
    } else if (ntype.is_cstmts()) {
      continue;
    } else {
      I(false); //FIXME->hunter: Should phi or anything else be acceptable here?
    }
  }

  return when_highest;
  //auto fstmt = umod->add_statement();
  //fstmt->set_allocated_when(when_stmt);
}

void Inou_firrtl::process_ln_phi_op(Lnast &ln, const Lnast_nid &lnidx_phi) {
  bool first = true;
  bool second = false;
  bool third = false;
  bool fourth = false;
  std::string lhs, rhs;
  for (const auto &lnchild_idx : ln.children(lnidx_phi)) {
    if(first) { // lhs
      first = false;
      second = true;
      lhs = get_firrtl_name_format(ln, lnchild_idx);
    } else if (second) { // cond
      second = false;
      third = true;
      lhs = absl::StrCat("mux(", get_firrtl_name_format(ln, lnchild_idx));
    } else if (third) { // val - true
      third = false;
      fourth = true;
      lhs = absl::StrCat(lhs, ", ", get_firrtl_name_format(ln, lnchild_idx));

    } else if (fourth) { // val - false
      fourth = false;
      lhs = absl::StrCat(", ", get_firrtl_name_format(ln, lnchild_idx), ")");
    } else {
      I(false); //There should only be 3 input + 1 output.
    }
  }

  fmt::print("{} = {}\n", lhs, rhs);
}

//----- Helper Functions -----
bool Inou_firrtl::is_inp(const std::string_view str) {
  return str.substr(0,1) == "$";
}

bool Inou_firrtl::is_outp(const std::string_view str) {
  return str.substr(0,1) == "%";
}

bool Inou_firrtl::is_reg(const std::string_view str) {
  return str.substr(0,1) == "#";
}

bool Inou_firrtl::is_wire(const std::string_view str) {
  // Anything that doesn't have a prefix
  auto first_char = str.substr(0,1);
  return !(str.substr(0,3) == "___") && !(first_char == "#") && !(first_char == "$") && !(first_char == "%");
}

std::string Inou_firrtl::get_firrtl_name_format(Lnast &ln, const Lnast_nid &lnidx) {
  auto ntype = ln.get_type(lnidx);
  const std::string_view str = ln.get_name(lnidx);
  if(ntype.is_ref() || ntype.is_cond()) {
    return strip_prefixes(str);
  } else if (ntype.is_const()) {
    return create_const_token(str);
  }
  fmt::print("{}\n", str);
  I(false); //When getting names, I would think we should only be checking those two node types.
  return "";
}

std::string Inou_firrtl::strip_prefixes(const std::string_view str) {
  std::string temp = (std::string)str;
  while((temp.substr(0,1) == "$") || (temp.substr(0,1) == "%") || (temp.substr(0,1) == "#")) {
    temp.replace(0, 1, ""); //Remove first char from a string
  }

  if(temp.substr(0,3) == "_._") {
    temp = temp.substr(2);
  }

  return temp;
}

std::string Inou_firrtl::create_const_token(const std::string_view str) {
  //FIXME?: I need to somehow get the number of bits (if taking min isn't good idea, though it might be).
  //        Also, I don't handle everything as unsigned right now.
  //Form should be like: 0d15 -> UInt(15) or UInt<#bits>(15)
  //                     0h10 -> UInt("h10") or UInt<#bits>("h10")
  std::string temp = (std::string)str;
  if((temp.substr(0,2) == "0d")) {
    //We need to drop the "0d" part of the string.
    temp.replace(0, 2, "");
    return absl::StrCat("UInt(", temp, ")");
  } else if ((temp.substr(0,2) == "0h") || (temp.substr(0,2) == "0b") || (temp.substr(0,2) == "0o")) {
    //We have to drop the 0 in front and then wrap the whole thing in quotes.
    temp.replace(0, 1, "");
    return absl::StrCat("UInt(\"", temp, "\")");
  } else {
    //If given just a number with no "0d" or the like preceding it, I'm assuming it's decimal.
    return absl::StrCat("UInt(", temp, ")");
  }
}

/* If the LHS of some sort of statement that does assigning
 * is a register or an output, then the statement needs to
 * be treated as a FIRRTL "connect" statement. */
void Inou_firrtl::create_connect_stmt(Lnast &ln, const Lnast_nid &lhs, firrtl::FirrtlPB_Expression* rhs_expr, firrtl::FirrtlPB_Statement* fstmt) {
  firrtl::FirrtlPB_Expression_Reference *lhs_ref = new firrtl::FirrtlPB_Expression_Reference();
  lhs_ref->set_id(get_firrtl_name_format(ln, lhs));

  firrtl::FirrtlPB_Expression *lhs_expr = new firrtl::FirrtlPB_Expression();
  lhs_expr->set_allocated_reference(lhs_ref);

  firrtl::FirrtlPB_Statement_Connect *conn = new firrtl::FirrtlPB_Statement_Connect();
  conn->set_allocated_location(lhs_expr);
  conn->set_allocated_expression(rhs_expr);

  fstmt->set_allocated_connect(conn);
}

/* If the LHS of some sort of statement that does assigning
 * is not a register or an output, then the statement needs
 * to be treated as a FIRRTL "node" statement. */
//FIXME: Maybe it coudl also be a wire???
void Inou_firrtl::create_node_stmt(Lnast &ln, const Lnast_nid &lhs, firrtl::FirrtlPB_Expression* rhs_expr, firrtl::FirrtlPB_Statement* fstmt) {
  firrtl::FirrtlPB_Statement_Node *node = new firrtl::FirrtlPB_Statement_Node();
  node->set_id((std::string)ln.get_name(lhs));
  node->set_allocated_expression(rhs_expr);

  // Have the generic statement of type "node".
  fstmt->set_allocated_node(node);
}

void Inou_firrtl::create_integer_object(Lnast &ln, const Lnast_nid &lnidx_const, firrtl::FirrtlPB_Expression* rhs_expr) {
  firrtl::FirrtlPB_Expression_IntegerLiteral *ilit = new firrtl::FirrtlPB_Expression_IntegerLiteral();
  auto lconst_holder = Lconst(ln.get_name(lnidx_const));
  auto lconst_str = lconst_holder.to_pyrope();
  ilit->set_value(lconst_str);

  firrtl::FirrtlPB_Width *width = new firrtl::FirrtlPB_Width();
  width->set_value(lconst_holder.get_bits());

  if (lconst_holder.to_pyrope().find("s") != std::string::npos) {
    firrtl::FirrtlPB_Expression_SIntLiteral *rhs_slit = new firrtl::FirrtlPB_Expression_SIntLiteral();
    rhs_slit->set_allocated_value(ilit);
    rhs_slit->set_allocated_width(width);

    rhs_expr->set_allocated_sint_literal(rhs_slit);
  } else {
    firrtl::FirrtlPB_Expression_UIntLiteral *rhs_ulit = new firrtl::FirrtlPB_Expression_UIntLiteral();
    rhs_ulit->set_allocated_value(ilit);
    rhs_ulit->set_allocated_width(width);

    rhs_expr->set_allocated_uint_literal(rhs_ulit);
  }
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
  //} else if (ntype.is_rem()) { //FIXME: Modulo not yet implemented in LNAST
  //  firrtl_op = "rem";
  } else if (ntype.is_ge()) {
    return firrtl::FirrtlPB_Expression_PrimOp_Op_OP_GREATER_EQ;
  } else if (ntype.is_gt()) {
    return firrtl::FirrtlPB_Expression_PrimOp_Op_OP_GREATER;
  } else if (ntype.is_le()) {
    return firrtl::FirrtlPB_Expression_PrimOp_Op_OP_LESS_EQ;
  } else if (ntype.is_lt()) {
    return firrtl::FirrtlPB_Expression_PrimOp_Op_OP_LESS;
  } else if (ntype.is_same()) {//FIXME: Is this the best way to handle "same" node type?
    return firrtl::FirrtlPB_Expression_PrimOp_Op_OP_EQUAL;
  } else if (ntype.is_and()) {
    return firrtl::FirrtlPB_Expression_PrimOp_Op_OP_BIT_AND;
  } else if (ntype.is_or()) {
    return firrtl::FirrtlPB_Expression_PrimOp_Op_OP_BIT_OR;
  } else if (ntype.is_xor()) {
    return firrtl::FirrtlPB_Expression_PrimOp_Op_OP_BIT_XOR;
  } else {
    I(false); //some nary op not yet supported
    return firrtl::FirrtlPB_Expression_PrimOp_Op_OP_UNKNOWN;
  }
}

void Inou_firrtl::add_const_or_ref_to_primop(Lnast &ln, const Lnast_nid &lnidx, firrtl::FirrtlPB_Expression_PrimOp* prim_op) {
  if (ln.get_data(lnidx).type.is_ref()) {
    // Lnidx is a variable, so I need to make a Reference message.
    firrtl::FirrtlPB_Expression *rhs_prim_expr = prim_op->add_arg();
    firrtl::FirrtlPB_Expression_Reference *rhs_ref = new firrtl::FirrtlPB_Expression_Reference();
    rhs_ref->set_id(get_firrtl_name_format(ln, lnidx));
    rhs_prim_expr->set_allocated_reference(rhs_ref);
  } else if (ln.get_data(lnidx).type.is_const()) {
    // Lnidx is a number, so I need to make an IntegerLiteral message.
    firrtl::FirrtlPB_Expression_IntegerLiteral *rhs_prim_ilit = prim_op->add_const_();
    auto lconst_holder = Lconst(ln.get_name(lnidx));
    auto lconst_str = lconst_holder.to_pyrope();
    rhs_prim_ilit->set_value(lconst_str);
  } else {
      I(false); //FIXME: Should const and ref be only things allowed on RHS?
  }
}


//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "inou_firrtl.hpp"

#define PRINT_DEBUG

void Inou_firrtl::toFIRRTL(Eprp_var &var) {
  Inou_firrtl p(var);
  for (const auto &lnast : var.lnasts) {
    lnast->ssa_trans();

    firrtl::FirrtlPB_Circuit circuit;
    p.do_tofirrtl(lnast);//, circuit);
  }
}

void Inou_firrtl::do_tofirrtl(std::shared_ptr<Lnast> ln) {//, firrtl::FirrtlPB_Circuit& circuit) {
  const auto top   = ln->get_root();
  const auto stmts = ln->get_first_child(top);
  firrtl::FirrtlPB_Circuit *circuit = new firrtl::FirrtlPB_Circuit();
  //FIXME: I need to add a "top" message to Circuit.
  firrtl::FirrtlPB_Module *mod = circuit->add_module();
  firrtl::FirrtlPB_Module_UserModule *umod = new firrtl::FirrtlPB_Module_UserModule();
  for (const auto &lnidx : ln->children(stmts)) {
    process_ln_stmt(*ln, lnidx, umod->add_statement());
  }
  mod->set_allocated_user_module(umod);
}

void Inou_firrtl::process_ln_stmt(Lnast &ln, const Lnast_nid &lnidx, firrtl::FirrtlPB_Statement* fstmt) {
  const auto ntype = ln.get_data(lnidx).type;
  // FIXME->sh: how to use switch to gain performance?
  if (ntype.is_assign()) {
    process_ln_assign_op(ln, lnidx, fstmt);
  } else if (ntype.is_nary_op()) {
    process_ln_nary_op(ln, lnidx);
  } else if (ntype.is_not()) {
    process_ln_not_op(ln, lnidx);
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
    process_ln_if_op(ln, lnidx);
  /*} else if (ntype.is_uif()) {
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
    continue;*/
  } else if (ntype.is_const()) {
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
    // FIXME: This only works for unsigned numbers right now.
    firrtl::FirrtlPB_Expression_IntegerLiteral *ilit = new firrtl::FirrtlPB_Expression_IntegerLiteral();
    ilit->set_value(get_const_val(ln.get_sname(c1)));

    firrtl::FirrtlPB_Width *width = new firrtl::FirrtlPB_Width();
    width->set_value(0);//Note + FIXME: Check and make sure that 0 in width means implicit.

    firrtl::FirrtlPB_Expression_UIntLiteral *rhs_ulit = new firrtl::FirrtlPB_Expression_UIntLiteral();
    rhs_ulit->set_allocated_value(ilit);
    rhs_ulit->set_allocated_width(width);

    rhs_expr->set_allocated_uint_literal(rhs_ulit);
  } else {
      I(false); //FIXME: Should const and ref be only things allowed on RHS?
  }

  /* Now handle LHS. If LHS is an output or register then
   * the statement should be a Connect. If it isn't, then the
   * statement should be a Node. */
  if (is_outp(ln.get_sname(c0)) || is_reg(ln.get_sname(c0))) {
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

void Inou_firrtl::process_ln_not_op(Lnast &ln, const Lnast_nid &lnidx_op) {
  bool first = true;
  bool second = false;
  std::string lhs, rhs;
  for (const auto &lnchild_idx : ln.children(lnidx_op)) {
    if (first) {
      first = false;
      second = true;
      lhs = get_firrtl_name_format(ln, lnchild_idx);//FIXME: Should I use sname (comes with _#) or just name?
    } else if (second) {
      second = false;
      rhs = absl::StrCat("not(", get_firrtl_name_format(ln, lnchild_idx), ")");
    } else {
      I(false); // There should only be 1 input and 1 output on a "not" node.
    }
  }

  fmt::print("{} = {}\n", lhs, rhs);
}

void Inou_firrtl::process_ln_nary_op(Lnast &ln, const Lnast_nid &lnidx_op) {
  const auto ntype = ln.get_data(lnidx_op).type;
  std::string firrtl_op;
  if (ntype.is_plus()) {
    firrtl_op = "add";
  } else if (ntype.is_minus()) {
    firrtl_op = "sub";
  } else if (ntype.is_mult()) {
    firrtl_op = "mul";
  } else if (ntype.is_div()) {
    firrtl_op = "div";
  /*} else if (ntype.is_rem()) { //FIXME: Modulo not yet implemented in LNAST
    firrtl_op = "rem";*/
  } else if (ntype.is_ge()) {
    firrtl_op = "geq";
  } else if (ntype.is_gt()) {
    firrtl_op = "gt";
  } else if (ntype.is_le()) {
    firrtl_op = "leq";
  } else if (ntype.is_lt()) {
    firrtl_op = "lt";
  } else if (ntype.is_same()) {//FIXME: Is this the best way to handle "same" node type?
    firrtl_op = "eq";
  } else if (ntype.is_and()) {
    firrtl_op = "and";
  } else if (ntype.is_or()) {
    firrtl_op = "or";
  } else if (ntype.is_xor()) {
    firrtl_op = "xor";
  } else {
    I(false); //some nary op not yet supported
  }

  auto child_count = process_op_children(ln, lnidx_op, firrtl_op);

  if (ntype.is_ge() || ntype.is_gt() || ntype.is_le() || ntype.is_lt()) {
    I(child_count == 3); //There should only be 1 output and 2 input, I think?
  }
}

void Inou_firrtl::process_ln_if_op(Lnast &ln, const Lnast_nid &lnidx_if) {
  for (const auto &if_child : ln.children(lnidx_if)) {
    auto ntype = ln.get_type(if_child);
    if (ntype.is_cstmts() || ntype.is_stmts()) {
      //process_ln_stmts(ln, if_child);
    } else if (ntype.is_cond()) {
      continue;//???
    } else if (ntype.is_phi()) {
      process_ln_phi_op(ln, if_child);
    } else {
      I(false); //children of an if should only be cstmts/stmts/cond/phi nodes
    }
  }
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

/* This function will work if the FIRRTL expression takes two inputs.
 * As an example, if we have a + b + c in LNAST, it will convert to
 * add(add(a+b), c) */
uint8_t Inou_firrtl::process_op_children(Lnast &ln, const Lnast_nid &lnidx_op, const std::string firrtl_op) {
  bool first = true;
  bool second = false;
  std::string lhs, rhs;
  uint8_t child_count = 0;
  for (const auto &lnchild_idx : ln.children(lnidx_op)) {
    if (first) {
      first = false;
      second = true;
      lhs = get_firrtl_name_format(ln, lnchild_idx);//FIXME: Should I use sname (comes with _#) or just name?
    } else if (second) {
      second = false;
      rhs = get_firrtl_name_format(ln, lnchild_idx);
    } else {
      rhs = absl::StrCat(firrtl_op, "(", rhs, ", ", get_firrtl_name_format(ln, lnchild_idx), ")");
    }
    child_count++;
  }

  fmt::print("{} = {}\n", lhs, rhs);
  //Note: usually this output will be ignored, unless we want to make sure a certain # of children are present.
  return child_count;
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

std::string Inou_firrtl::get_firrtl_name_format(Lnast &ln, const Lnast_nid &lnidx) {
  auto ntype = ln.get_type(lnidx);
  const std::string_view str = ln.get_sname(lnidx);
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

std::string Inou_firrtl::get_const_val(std::string_view const_name) {
  //FIXME: This only works for unsigned numbers.
  std::string name = (std::string)const_name;
  char base = 10; // 0 = decimal, 1 = hex, 2 = binary, 3 = octal
  if (const_name.length() >= 2) {
    if (name[0] == '0') {
      if (name[1] == 'd') {
        base = 10;
        name = name.substr(2);
      } else if (name[1] == 'h') {
        base = 16;
        name = name.substr(2);
      } else if (name[1] == 'b') {
        base = 2;
        name = name.substr(2);
      } else if (name[1] == 'o') {
        base = 8;
        name = name.substr(2);
      } else {
        //Assume base 10 since no base info provided.
      }
    }
  }
  auto decimal_val = stoi(name, nullptr, base);
  return std::to_string(decimal_val);
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
  node->set_id(ln.get_sname(lhs));
  node->set_allocated_expression(rhs_expr);

  // Have the generic statement of type "node".
  fstmt->set_allocated_node(node);
}

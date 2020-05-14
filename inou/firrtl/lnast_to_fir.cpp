//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "inou_firrtl.hpp"

void Inou_firrtl::toFIRRTL(Eprp_var &var) {
  Inou_firrtl p(var);
  for (const auto &lnast : var.lnasts) {
    lnast->ssa_trans();
    p.do_tofirrtl(lnast);
  }
}

void Inou_firrtl::do_tofirrtl(std::shared_ptr<Lnast> ln) {
  const auto top   = ln->get_root();
  const auto stmts = ln->get_first_child(top);
  process_ln_stmts(*ln, stmts);
}

void Inou_firrtl::process_ln_stmts(Lnast &ln, const Lnast_nid &lnidx_stmts) {
  for (const auto &lnidx : ln.children(lnidx_stmts)) {
    const auto ntype = ln.get_data(lnidx).type;
    // FIXME->sh: how to use switch to gain performance?
    if (ntype.is_assign()) {
      process_ln_assign_op(ln, lnidx);
    } else if (ntype.is_nary_op()) {
      process_ln_nary_op(ln, lnidx);
    } else if (ntype.is_unary_op()) {
      process_ln_nary_op(ln, lnidx); // could be handled like unary
    } else if (ntype.is_tuple_add()) {
      //process_ast_tuple_add_op(dfg, lnidx);
    } else if (ntype.is_tuple_get()) {
      //process_ast_tuple_get_op(dfg, lnidx);
    } else if (ntype.is_tuple_phi_add()) {
      //process_ast_tuple_phi_add_op(dfg, lnidx);
    } else if (ntype.is_dot()) {
      //I(false); // should has been converted to tuple chain 
    } else if (ntype.is_select()) {
      //I(false); // should has been converted to tuple chain
    } else if (ntype.is_logical_op()) {
      //process_ast_logical_op(dfg, lnidx);
    } else if (ntype.is_as()) {
      //process_ast_as_op(dfg, lnidx);
    } else if (ntype.is_label()) {
      //process_ast_label_op(dfg, lnidx);
    } else if (ntype.is_dp_assign()) {
      //process_ast_dp_assign_op(dfg, lnidx);
    } else if (ntype.is_tuple()) {
      //process_ast_tuple_struct(dfg, lnidx);
    } else if (ntype.is_tuple_concat()) {
      //process_ast_concat_op(dfg, lnidx);
    } else if (ntype.is_if()) {
      process_ln_if_op(ln, lnidx);
    } else if (ntype.is_uif()) {
      //process_ast_uif_op(dfg, lnidx);
    } else if (ntype.is_func_call()) {
      //process_ast_func_call_op(dfg, lnidx);
    } else if (ntype.is_func_def()) {
      //process_ast_func_def_op(dfg, lnidx);
    } else if (ntype.is_for()) {
      //process_ast_for_op(dfg, lnidx);
    } else if (ntype.is_while()) {
      //process_ast_while_op(dfg, lnidx);
    } else if (ntype.is_invalid()) { // FIXME->sh: add ignore type in LNAST?
      //continue;
    } else if (ntype.is_const()) {
      //I(lnast->get_name(lnidx) == "default_const");
      //continue;
    } else if (ntype.is_err_flag()) {
      //I(lnast->get_name(lnidx) == "err_var_undefined");
      //continue;
    } else {
      //I(false);
      //return;
    }
  }
}

void Inou_firrtl::process_ln_assign_op(Lnast &ln, const Lnast_nid &lnidx_assign) {
  auto c0 = ln.get_first_child(lnidx_assign);
  auto c1 = ln.get_sibling_next(c0);
  auto c0_name = ln.get_sname(c0);
  auto c1_name = ln.get_sname(c1);

  I(is_inp_outp_or_reg(c0_name));
  fmt::print("{} <= {}\n", c0_name, c1_name);
}

void Inou_firrtl::process_ln_nary_op(Lnast &ln, const Lnast_nid &lnidx_op) {
  const auto ntype = ln.get_data(lnidx_op).type;
  if (ntype.is_minus()) {
    //handle_rh_arith();
    //FIXME: This is temporary for design exploration right now.
    bool first = true;
    bool second = false;
    std::string_view lhs, rhs;
    for (const auto &lnidx : ln.children(lnidx_op)) {
      if (first) {
        first = false;
        second = true;
        lhs = ln.get_name(lnidx);
      } else if (second) {
        second = false;
        lhs = ln.get_name(lnidx);//FIXME: Need to strip off prefix $, #, % if any (for all get_name)
      } else {
        lhs = absl::StrCat("sub(", lhs, ", ", ln.get_name(lnidx), ")");
      }
    }
  }
}

void Inou_firrtl::process_ln_if_op(Lnast &ln, const Lnast_nid &lnidx_if) {
  for (const auto &if_child : ln.children(lnidx_if)) {
    auto ntype = ln.get_type(if_child);
    if (ntype.is_cstmts() || ntype.is_stmts()) {
      process_ln_stmts(ln, if_child);
    } else if (ntype.is_cond()) {
      continue;//???
    } else if (ntype.is_phi()) {
      //process_ln_phi_op(ln, if_child);
    } else {
      I(false); //children of an if should only be cstmts/stmts/cond/phi nodes
    }
  }
}

//----- Helper Functions -----
bool Inou_firrtl::is_inp_outp_or_reg(const std::string_view str) {
  auto first_char = str.substr(0, 1);
  return (first_char == "$") || (first_char == "%") || (first_char == "#");
}

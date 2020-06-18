//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
#include "inou_firrtl.hpp"

#include <fstream>
#include <google/protobuf/util/time_util.h>
#include <iostream>
#include <stdlib.h>

#include "firrtl.pb.h"

using namespace std;

using google::protobuf::util::TimeUtil;

/* For help understanding FIRRTL/Protobuf:
 * 1) Semantics regarding FIRRTL language:
 * www2.eecs.berkeley.edu/Pubs/TechRpts/2019/EECS-2019-168.pdf
 * 2) Structure of FIRRTL Protobuf file:
 * github.com/freechipsproject/firrtl/blob/master/src/main/proto/firrtl.proto */

void Inou_firrtl::toLNAST(Eprp_var &var) {
  Inou_firrtl p(var);

  if(var.has_label("files")) {
    auto files = var.get("files");
    for (const auto &f : absl::StrSplit(files, ",")) {
      cout << "FILE: " << f << "\n";
      firrtl::FirrtlPB firrtl_input;
      fstream input(std::string(f).c_str(), ios::in | ios::binary);
      if (!firrtl_input.ParseFromIstream(&input)) {
        cerr << "Failed to parse FIRRTL from protobuf format." << endl;
        return;
      }
      p.temp_var_count = 0;
      p.seq_counter = 0;
      p.IterateCircuits(var, firrtl_input);
    }
  } else {
    cout << "No file provided. This requires a file input.\n";
    return;
  }

  // Optional:  Delete all global objects allocated by libprotobuf.
  google::protobuf::ShutdownProtobufLibrary();
}

//----------------Helper Functions--------------------------
std::string_view Inou_firrtl::create_temp_var(Lnast& lnast) {
  auto temp_var_name = lnast.add_string(absl::StrCat("___F", temp_var_count));
  temp_var_count++;
  return temp_var_name;
}

std::string_view Inou_firrtl::get_new_seq_name(Lnast& lnast) {
  auto seq_name = lnast.add_string(absl::StrCat("SEQ", seq_counter));
  seq_counter++;
  return seq_name;
}

std::string Inou_firrtl::get_full_name(std::string term) {
  //FIXME: Eventually add a list for registers, too.
  if(std::find(input_names.begin(), input_names.end(), term) != input_names.end()) {
    //string matching "term" was found to be an input to the module
    return absl::StrCat("$", term);
  } else if(std::find(output_names.begin(), output_names.end(), term) != output_names.end()) {
    return absl::StrCat("%", term);
  } else if(std::find(register_names.begin(), register_names.end(), term) != register_names.end()) {
    return absl::StrCat(term, "__q_pin");
  } else {
    return term;
  }
}

std::string Inou_firrtl::get_full_name_lhs(std::string term) {
  //FIXME: Eventually add a list for registers, too.
  if(std::find(input_names.begin(), input_names.end(), term) != input_names.end()) {
    //string matching "term" was found to be an input to the module
    return absl::StrCat("$", term);
  } else if(std::find(output_names.begin(), output_names.end(), term) != output_names.end()) {
    return absl::StrCat("%", term);
  } else if(std::find(register_names.begin(), register_names.end(), term) != register_names.end()) {
    return absl::StrCat("#", term);
  } else {
    return term;
  }
}

//If the bitwidth is specified, in LNAST we have to create a new variable which represents
//  the number of bits that a variable will have.
//FIXME: I need to add stuff to determine if input/output/register and add $/%/# respectively.
void Inou_firrtl::create_bitwidth_dot_node(Lnast& lnast, uint32_t bitwidth, Lnast_nid& parent_node, std::string port_id) {
  auto temp_var_name = create_temp_var(lnast);

  auto idx_dot = lnast.add_child(parent_node, Lnast_node::create_dot("dot"));
  lnast.add_child(idx_dot, Lnast_node::create_ref(temp_var_name));
  lnast.add_child(idx_dot, Lnast_node::create_ref(lnast.add_string(port_id)));
  lnast.add_child(idx_dot, Lnast_node::create_ref("__bits"));

  auto idx_asg = lnast.add_child(parent_node, Lnast_node::create_assign("asg"));
  lnast.add_child(idx_asg, Lnast_node::create_ref(temp_var_name));
  lnast.add_child(idx_asg, Lnast_node::create_const(lnast.add_string(absl::StrCat("0d", bitwidth))));
}

/* When creating a register, we have to set the register's
 * clock, reset, and init values using "dot" nodes in the LNAST.
 * This function creates all of those when a reg is first declared. */
void Inou_firrtl::init_register_dots(Lnast& lnast, const firrtl::FirrtlPB_Statement_Register& expr, Lnast_nid& parent_node) {
  auto reg_name = lnast.add_string(absl::StrCat("#", expr.id()));
  auto clk_name = lnast.add_string(get_full_name(ReturnExprString(lnast, expr.clock(), parent_node)));

  // Since FIRRTL designs access register qpin, I need to do:
  // #reg_name.__q_pin. The name will always be ___reg_name__q_pin
  auto qpin_var_name_temp = lnast.add_string(absl::StrCat("___", expr.id(), "__q_pin_t"));
  auto qpin_var_name = lnast.add_string(absl::StrCat(expr.id(), "__q_pin"));

  auto idx_dot = lnast.add_child(parent_node, Lnast_node::create_dot("dot"));
  lnast.add_child(idx_dot, Lnast_node::create_ref(qpin_var_name_temp));
  lnast.add_child(idx_dot, Lnast_node::create_ref(reg_name));
  lnast.add_child(idx_dot, Lnast_node::create_ref("__q_pin"));

  //Required to identify ___regname__q_pin as RHS.
  auto idx_asg = lnast.add_child(parent_node, Lnast_node::create_assign("asg"));
  lnast.add_child(idx_asg, Lnast_node::create_ref(qpin_var_name));
  lnast.add_child(idx_asg, Lnast_node::create_ref(qpin_var_name_temp));


  //auto res_name = lnast.add_string(get_full_name(ReturnExprString(expr.reset())));
  //auto init_name = lnast.add_string(get_full_name(ReturnExprString(expr.init())));

  //FIXME: maybe I should also specify __bits here

  //Creating clock "dot" node.
  /*auto clk_dot_node = lnast.add_child(parent_node, Lnast_node::create_dot("dot_clk"));
  auto temp_var_name_clk = create_temp_var(lnast);
  lnast.add_child(clk_dot_node, Lnast_node::create_ref(temp_var_name_clk));
  lnast.add_child(clk_dot_node, Lnast_node::create_ref(reg_name));
  lnast.add_child(clk_dot_node, Lnast_node::create_ref("__clk_pin"));

  auto clk_asg_node = lnast.add_child(parent_node, Lnast_node::create_assign("asg_clk"));
  lnast.add_child(clk_asg_node, Lnast_node::create_ref(temp_var_name_clk));
  if(clk_name.compare(0, 2, "0d") == 0) {
    lnast.add_child(clk_asg_node, Lnast_node::create_const(clk_name));
  } else {
    lnast.add_child(clk_asg_node, Lnast_node::create_ref(clk_name));
  }*/

  //Creating reset "dot" node.
  /*auto res_dot_node = lnast.add_child(parent_node, Lnast_node::create_dot("dot_reset"));
  auto temp_var_name_res = create_temp_var(lnast);
  lnast.add_child(res_dot_node, Lnast_node::create_ref(temp_var_name_res));
  lnast.add_child(res_dot_node, Lnast_node::create_ref(reg_name));
  lnast.add_child(res_dot_node, Lnast_node::create_ref("__reset_pin"));

  auto res_asg_node = lnast.add_child(parent_node, Lnast_node::create_assign("asg_reset"));
  lnast.add_child(res_asg_node, Lnast_node::create_ref(temp_var_name_res));
  if(clk_name.compare(0, 2, "0d") == 0) {
    lnast.add_child(res_asg_node, Lnast_node::create_const(res_name));
  } else {
    lnast.add_child(res_asg_node, Lnast_node::create_ref(temp_var_name_res));
  }*/

  //Creating init value "dot" node.
  //auto init_dot_node = lnast.add_child(parent_node, Lnast_node::create_dot("dot_init"));
  //FIXME: Add later.
}

/* These functions are useful because they analyze what the condition of something is
 * (useful for whens, muxes, ifs, etc.) but instead of creating some node type based
 * off the expression type, they instead create a "condition" typed node whose
 * token is the condition itself. */
void Inou_firrtl::CreateConditionNode(Lnast& lnast, const firrtl::FirrtlPB_Expression& expr, Lnast_nid& parent_node, Lnast_nid& stmts_node) {
  //FIXME: This doesn't add necessary $/%/# yet
  switch(expr.expression_case()) {
    case 1: { //Reference
      auto full_name = get_full_name(expr.reference().id());
      lnast.add_child(parent_node, Lnast_node::create_cond(lnast.add_string(full_name)));
      break;

    } case 7: { //SubField -- this is called when you're accessing a bundle's field (like io.var1)
      std::string subfield_accessor = ReturnExprString(lnast, expr, stmts_node);//FIXME: Double check expr is correct here.
      lnast.add_child(parent_node, Lnast_node::create_cond(lnast.add_string(subfield_accessor)));
      //CreateConditionNode(lnast, expr.sub_field().expression(), parent_node, expr.sub_field().field());
      break;

    } default:
      cout << "ERROR CreateConditionNode: Trying to create a condition node for an expression not yet supported or unknown.: " << expr.expression_case() << endl;
      assert(false);
  }
}

/* When a module instance is created in FIRRTL, we need to do the same
 * in LNAST. Note that the instance command in FIRRTL does not hook
 * any input or outputs. */
void Inou_firrtl::create_module_inst(Lnast& lnast, const firrtl::FirrtlPB_Statement_Instance& inst, Lnast_nid& parent_node) {
  /*            fn_call
   *         /     |     \
   * inst_name  mod_name  null */
  auto idx_fncall = lnast.add_child(parent_node, Lnast_node::create_func_call("fn_call"));
  lnast.add_child(idx_fncall, Lnast_node::create_ref(lnast.add_string(inst.id())));
  lnast.add_child(idx_fncall, Lnast_node::create_ref(lnast.add_string(inst.module_id())));
  lnast.add_child(idx_fncall, Lnast_node::create_ref("null")); //FIXME: Is this correct way to show no inputs specified?
}

/* No mux node type exists in lnast. To support FIRRTL muxes, we instead
 * map a mux to an if-else statement whose condition is the same condition
 * as the first argument (the condition) of the mux. */
void Inou_firrtl::HandleMuxAssign(Lnast& lnast, const firrtl::FirrtlPB_Expression& expr, Lnast_nid& parent_node, std::string lhs_of_asg) {
  //I(parent_node.is_stmts() | parent.is_cstmts());

  auto idx_mux_if    = lnast.add_child(parent_node, Lnast_node::create_if("mux"));
  CreateConditionNode(lnast, expr.mux().condition(), idx_mux_if, parent_node);
  auto idx_stmt_tr   = lnast.add_child(idx_mux_if, Lnast_node::create_stmts(get_new_seq_name(lnast)));
  auto idx_stmt_f    = lnast.add_child(idx_mux_if, Lnast_node::create_stmts(get_new_seq_name(lnast)));

  InitialExprAdd(lnast, expr.mux().t_value(), idx_stmt_tr, lhs_of_asg);

  InitialExprAdd(lnast, expr.mux().f_value(), idx_stmt_f, lhs_of_asg);
}

/* ValidIfs get detected as the RHS of an assign statement and we can't have a child of
 * an assign be an if-typed node. Thus, we have to detect ahead of time if it is a validIf
 * if we're doing an assign. If that is the case, do this instead of using ListExprType().*/
void Inou_firrtl::HandleValidIfAssign(Lnast& lnast, const firrtl::FirrtlPB_Expression& expr, Lnast_nid& parent_node, std::string lhs_of_asg) {
  //I(parent_node.is_stmts() | parent.is_cstmts());

  auto idx_v_if      = lnast.add_child(parent_node, Lnast_node::create_if("validIf"));
  CreateConditionNode(lnast, expr.valid_if().condition(), idx_v_if, parent_node);
  auto idx_stmt_tr   = lnast.add_child(idx_v_if, Lnast_node::create_stmts(get_new_seq_name(lnast)));
  auto idx_stmt_f    = lnast.add_child(idx_v_if, Lnast_node::create_stmts(get_new_seq_name(lnast)));

  InitialExprAdd(lnast, expr.valid_if().value(), idx_stmt_tr, lhs_of_asg);

  //For validIf, if the condition is not met then what the LHS equals is undefined. We'll just use 0.
  auto idx_asg_false = lnast.add_child(idx_stmt_f, Lnast_node::create_assign("assign"));
  lnast.add_child(idx_asg_false, Lnast_node::create_ref(lnast.add_string(lhs_of_asg)));
  lnast.add_child(idx_asg_false, Lnast_node::create_const("0d0"));
}

/* We have to handle NEQ operations different than any other primitive op.
 * This is because NEQ has to be broken down into two sub-operations:
 * checking equivalence and then performing the not. */
void Inou_firrtl::HandleNEQOp(Lnast& lnast, const firrtl::FirrtlPB_Expression_PrimOp& op, Lnast_nid& parent_node, std::string lhs) {
  //I(parent_node.is_stmts() | parent_node.is_cstmts());

  /* x = neq(a, b) should take graph form:
   *     equal        ~
   *    /  |  \     /   \
   *___F0  a   b   x  ___F0  */

  auto temp_var_name = create_temp_var(lnast);

  auto idx_eq = lnast.add_child(parent_node, Lnast_node::create_same("==="));
  lnast.add_child(idx_eq, Lnast_node::create_ref(temp_var_name));
  PrintPrimOp(lnast, op, "===", idx_eq);

  auto idx_asg = lnast.add_child(parent_node, Lnast_node::create_not("asg_neg"));//FIXME: This should probably be logical_not instead?
  lnast.add_child(idx_asg, Lnast_node::create_ref(lnast.add_string(lhs)));
  lnast.add_child(idx_asg, Lnast_node::create_ref(temp_var_name));
}

/* Unary operations are handled in a way where (currently) there is no LNAST
 * node type that supports unary ops. Instead, we would want to have an assign
 * node and have the "rhs" child of the assign node be "[op]temp". */
void Inou_firrtl::HandleUnaryOp(Lnast& lnast, const firrtl::FirrtlPB_Expression_PrimOp& op, Lnast_nid& parent_node, std::string lhs) {
  //I(parent_node.is_stmts() | parent_node.is_cstmts());

  /* x = not(y) should take graph form: (xor_/and_/or_reduce all look same just different op)
   *     ~
   *   /   \
   *  x     y  */

  auto idx_not = lnast.add_child(parent_node, Lnast_node::create_not("not"));
  lnast.add_child(idx_not, Lnast_node::create_ref(lnast.add_string(lhs)));
  if ((op.arg_size() == 1) && (op.const__size() == 0)) {
    auto arg_string = ReturnExprString(lnast, op.arg(0), parent_node);
    auto full_name = get_full_name(arg_string);
    lnast.add_child(idx_not, Lnast_node::create_ref(lnast.add_string(full_name)));
  } else if ((op.arg_size() == 0) && (op.const__size() == 1)) {
    std::string const_string = op.const_(0).value();
    lnast.add_child(idx_not, Lnast_node::create_const(lnast.add_string(const_string)));//FIXME(?): Should consts take this form?
  } else {
    cout << "Error in HandleUnaryOp: not correct # of operators given (unary ops should have 1 argument)." << endl;
    assert(false);
  }
}

/* */
void Inou_firrtl::HandleNegateOp(Lnast& lnast, const firrtl::FirrtlPB_Expression_PrimOp& op, Lnast_nid& parent_node, std::string lhs) {
  //I(parent_node.is_stmts() | parent_node.is_cstmts());

  /* x = negate(y) should take graph form:
   *     minus
   *    /  |  \
   *   x  0d0  y */

  auto idx_mns = lnast.add_child(parent_node, Lnast_node::create_minus("minus_negate"));
  lnast.add_child(idx_mns, Lnast_node::create_ref(lnast.add_string(lhs)));
  lnast.add_child(idx_mns, Lnast_node::create_const("0d0"));
  if ((op.arg_size() == 1) && (op.const__size() == 0)) {
    AttachExprToOperator(lnast, op.arg(0), idx_mns);
  } else if ((op.arg_size() == 0) && (op.const__size() == 1)) {
    lnast.add_child(idx_mns, Lnast_node::create_const(lnast.add_string(absl::StrCat("0d", op.const_(0).value()))));
  } else {
    cout << "Error in HandleNegateOp: not correct # of operators given ('negate' should have 1 argument)." << endl;
    assert(false);
  }
}

/* The Extract Bits primitive op is invoked on some variable
 * and functions as you would expect in a language like Verilog.
 * We have to break this down into multiple statements so
 * LNAST can properly handle it (see diagram below).*/
void Inou_firrtl::HandleExtractBitsOp(Lnast &lnast, const firrtl::FirrtlPB_Expression_PrimOp& op, Lnast_nid& parent_node, std::string lhs) {
  //I(parent_node.is_stmts() | parent_node.is_cstmts());

  /* x = bits(a)(numH, numL) should take graph form:
   *     range                bit_sel                 asg
   *    /   |   \             /   |   \             /     \
   *___F0 numH numL        ___F1  a ___F0          x    ___F1 */

  auto temp_var_name_f0 = create_temp_var(lnast);
  auto temp_var_name_f1 = create_temp_var(lnast);

  auto idx_range = lnast.add_child(parent_node, Lnast_node::create_range("range_EB"));
  lnast.add_child(idx_range, Lnast_node::create_ref(temp_var_name_f0));
  I(op.const__size() == 2);
  lnast.add_child(idx_range, Lnast_node::create_const(lnast.add_string(absl::StrCat("0d", op.const_(0).value()))));
  lnast.add_child(idx_range, Lnast_node::create_const(lnast.add_string(absl::StrCat("0d", op.const_(1).value()))));

  auto idx_bit_sel = lnast.add_child(parent_node, Lnast_node::create_bit_select("bit_sel_EB"));
  lnast.add_child(idx_bit_sel, Lnast_node::create_ref(temp_var_name_f1));
  I(op.arg_size() == 1);
  AttachExprToOperator(lnast, op.arg(0), idx_bit_sel);
  lnast.add_child(idx_bit_sel, Lnast_node::create_ref(temp_var_name_f0));

  auto idx_asg = lnast.add_child(parent_node, Lnast_node::create_assign("asg_eb"));
  lnast.add_child(idx_asg, Lnast_node::create_ref(lnast.add_string(lhs)));
  lnast.add_child(idx_asg, Lnast_node::create_ref(temp_var_name_f1));
}

/* The Head primitive op requires special handling since
 * it is actually doing quite a lot. We have to break this
 * down into multiple statements so LNAST can properly
 * handle it (see diagram below).*/
void Inou_firrtl::HandleHeadOp(Lnast& lnast, const firrtl::FirrtlPB_Expression_PrimOp& op, Lnast_nid& parent_node, std::string lhs) {
  //I(parent_node.is_stmts() | parent_node.is_cstmts());
  //
  /* x = head(tmp)(4) should take graph form: (like x = tmp >> (tmp.__bits - 0d4))
   *      dot               minus         log_shr
   *   /   |   \           /  |   \     /   |   \
   * ___F0 tmp __bits ___F1 ___F0 0d4  x   tmp __F1 */

  //FIXME: Not done!

  auto temp_var_name_f0 = create_temp_var(lnast);
  auto temp_var_name_f1 = create_temp_var(lnast);

  auto idx_dot = lnast.add_child(parent_node, Lnast_node::create_dot("dot_head"));
  lnast.add_child(idx_dot, Lnast_node::create_ref(temp_var_name_f0));
  I(op.arg_size() == 1);
  AttachExprToOperator(lnast, op.arg(0), idx_dot);
  lnast.add_child(idx_dot, Lnast_node::create_ref("__bits"));



  /* x = head(tmp)(4) should take graph form: (like x = tmp[tmp.__bits - 1 : tmp.__bits - 4])
   *     dot                minus           minus           range            bit_sel           asg
   *  /   |   \           /   |   \       /   |   \       /   |    \        /   |   \        /     \
   *___F0 tmp __bits  ___F1 ___F0  1  ___F2 ___F0  4  ___F3 ___F1 ___F2   ___F4 tmp ___F3   x    ___F4 */

  /*auto temp_var_name_f0 = create_temp_var(lnast);
  auto temp_var_name_f1 = create_temp_var(lnast);
  auto temp_var_name_f2 = create_temp_var(lnast);
  auto temp_var_name_f3 = create_temp_var(lnast);
  auto temp_var_name_f4 = create_temp_var(lnast);

  auto idx_dot = lnast.add_child(parent_node, Lnast_node::create_dot("dot_head"));
  lnast.add_child(idx_dot, Lnast_node::create_ref(temp_var_name_f0));
  I(op.arg_size() == 1);
  AttachExprToOperator(lnast, op.arg(0), idx_dot);
  lnast.add_child(idx_dot, Lnast_node::create_ref("__bits"));

  auto idx_mns1 = lnast.add_child(parent_node, Lnast_node::create_minus("minus1_head"));
  lnast.add_child(idx_mns1, Lnast_node::create_ref(temp_var_name_f1));
  lnast.add_child(idx_mns1, Lnast_node::create_ref(temp_var_name_f0));
  lnast.add_child(idx_mns1, Lnast_node::create_const("0d1"));

  auto idx_mnsN = lnast.add_child(parent_node, Lnast_node::create_minus("minusN_head"));
  lnast.add_child(idx_mnsN, Lnast_node::create_ref(temp_var_name_f2));
  lnast.add_child(idx_mnsN, Lnast_node::create_ref(temp_var_name_f0));
  I(op.const__size() == 1);
  lnast.add_child(idx_mnsN, Lnast_node::create_const(lnast.add_string(absl::StrCat("0d", op.const_(0).value()))));

  auto idx_range = lnast.add_child(parent_node, Lnast_node::create_range("range_head"));
  lnast.add_child(idx_range, Lnast_node::create_ref(temp_var_name_f3));
  lnast.add_child(idx_range, Lnast_node::create_ref(temp_var_name_f1));
  lnast.add_child(idx_range, Lnast_node::create_ref(temp_var_name_f2));

  auto idx_bit_sel = lnast.add_child(parent_node, Lnast_node::create_bit_select("bit_sel_head"));
  lnast.add_child(idx_bit_sel, Lnast_node::create_ref(temp_var_name_f4));
  AttachExprToOperator(lnast, op.arg(0), idx_bit_sel);
  lnast.add_child(idx_bit_sel, Lnast_node::create_ref(temp_var_name_f3));

  auto idx_asg = lnast.add_child(parent_node, Lnast_node::create_assign("asg_head"));
  lnast.add_child(idx_asg, Lnast_node::create_ref(lnast.add_string(lhs)));
  lnast.add_child(idx_asg, Lnast_node::create_ref(temp_var_name_f4));*/
}

/* */
void Inou_firrtl::HandleTailOp(Lnast& lnast, const firrtl::FirrtlPB_Expression_PrimOp& op, Lnast_nid& parent_node, std::string lhs) {
  //I(parent_node.is_stmts() | parent_node.is_cstmts());


  /* x = tail(tmp)(2) should take graph form:
   * NOTE: shift right is only used to get correct # bits for :=
   *
   *    log_shr       :=
   *   /  |   \      /  \
   *  x  tmp  0d2   x   tmp */
  auto lhs_str = lnast.add_string(lhs);

  auto idx_shr = lnast.add_child(parent_node, Lnast_node::create_shift_right("shr_tail"));
  //lnast.add_child(idx_shr, Lnast_node::create_ref(lhs_str));
  auto temp_var_name_f1 = create_temp_var(lnast);//FIXME: REMOVE ONCE DUMMY ASSIGNS
  lnast.add_child(idx_shr, Lnast_node::create_ref(temp_var_name_f1));//FIXME: REMOVE ONCE DUMMY ASSIGNS
  I(op.arg_size() == 1);
  AttachExprToOperator(lnast, op.arg(0), idx_shr);
  I(op.const__size() == 1);
  lnast.add_child(idx_shr, Lnast_node::create_const(lnast.add_string(absl::StrCat("0d", op.const_(0).value()))));

  //FIXME: REMOVE ONCE DUMMY ASSIGNS ARE DEALT WITH-----------------------------------
  auto idx_asg = lnast.add_child(parent_node, Lnast_node::create_assign("asg_tail"));
  lnast.add_child(idx_asg, Lnast_node::create_ref(lhs_str));
  lnast.add_child(idx_asg, Lnast_node::create_ref(temp_var_name_f1));
  //FIXME: REMOVE ONCE DUMMY ASSIGNS ARE DEALT WITH-----------------------------------

  auto idx_dp_asg = lnast.add_child(parent_node, Lnast_node::create_dp_assign("dpasg_tail"));
  lnast.add_child(idx_dp_asg, Lnast_node::create_ref(lhs_str));
  AttachExprToOperator(lnast, op.arg(0), idx_dp_asg);

  //auto idx_asg = lnast.add_child(parent_node, Lnast_node::create_dp_assign("asg_tail"));
  //lnast.add_child(idx_asg, Lnast_node::create_ref(lnast.add_string(lhs)));
  //AttachExprToOperator(lnast, op.arg(0), idx_asg);

  /* x = tail(tmp)(2) should take graph form: (like x = tmp[tmp.__bits - 3 : 0])
   *     dot                minus          range            bit_sel         asg
   *  /   |   \           /   |   \      /   |    \        /   |   \      /     \
   *___F0 tmp __bits  ___F1 ___F0  3  ___F2 ___F1  0   ___F3 tmp ___F2   x    ___F3 */

  /*auto temp_var_name_f0 = create_temp_var(lnast);
  auto temp_var_name_f1 = create_temp_var(lnast);
  auto temp_var_name_f2 = create_temp_var(lnast);
  auto temp_var_name_f3 = create_temp_var(lnast);

  auto idx_dot = lnast.add_child(parent_node, Lnast_node::create_dot("dot_tail"));
  lnast.add_child(idx_dot, Lnast_node::create_ref(temp_var_name_f0));
  I(op.arg_size() == 1);
  AttachExprToOperator(lnast, op.arg(0), idx_dot);
  lnast.add_child(idx_dot, Lnast_node::create_ref("__bits"));

  auto idx_mns = lnast.add_child(parent_node, Lnast_node::create_minus("minus_tail"));
  lnast.add_child(idx_mns, Lnast_node::create_ref(temp_var_name_f1));
  lnast.add_child(idx_mns, Lnast_node::create_ref(temp_var_name_f0));
  I(op.const__size() == 1);
  int const_plus_one = stoi(op.const_(0).value()) + 1;//FIXME: If the const str is a # that overflows an int, this will be problems here.
  lnast.add_child(idx_mns, Lnast_node::create_const(lnast.add_string(absl::StrCat("0d", const_plus_one))));

  auto idx_range = lnast.add_child(parent_node, Lnast_node::create_range("range_tail"));
  lnast.add_child(idx_range, Lnast_node::create_ref(temp_var_name_f2));
  lnast.add_child(idx_range, Lnast_node::create_ref(temp_var_name_f1));
  lnast.add_child(idx_range, Lnast_node::create_const("0d0"));

  auto idx_bit_sel = lnast.add_child(parent_node, Lnast_node::create_bit_select("bit_sel_tail"));
  lnast.add_child(idx_bit_sel, Lnast_node::create_ref(temp_var_name_f3));
  AttachExprToOperator(lnast, op.arg(0), idx_bit_sel);
  lnast.add_child(idx_bit_sel, Lnast_node::create_ref(temp_var_name_f2));

  auto idx_asg = lnast.add_child(parent_node, Lnast_node::create_assign("asg_tail"));
  lnast.add_child(idx_asg, Lnast_node::create_ref(lnast.add_string(lhs)));
  lnast.add_child(idx_asg, Lnast_node::create_ref(temp_var_name_f3));*/
}

void Inou_firrtl::HandlePadOp(Lnast &lnast, const firrtl::FirrtlPB_Expression_PrimOp& op, Lnast_nid& parent_node, std::string lhs) {
  //I(parent_node.is_stmts() | parent_node.is_cstmts());

  /* x = pad(e)(4) sets x = e and sets bw(x) = max(4, bw(e));
   *               [___________________________if_________________________________]                         asg
   *               /               /           /                                 \                         /   \
   *        [__ cstmts__]     cond, ___F1    stmts                     [________stmts________]            x     e
   *        /           \                   /      \                  /          |           \
   *      dot            lt, <           dot         asg          dot            dot           asg
   *    /  | \          /  |   \        / | \       /   \        / | \          / | \         /   \
   * ___F0 e __bits ___F1 ___F0 4   ___F2 x __bits ___F2 4   ___F3 x __bits ___F4 e __bits ___F3 ___F4 */

  auto temp_var_name_f0 = create_temp_var(lnast);
  auto temp_var_name_f1 = create_temp_var(lnast);
  auto temp_var_name_f2 = create_temp_var(lnast);
  auto temp_var_name_f3 = create_temp_var(lnast);
  auto temp_var_name_f4 = create_temp_var(lnast);

  auto idx_if = lnast.add_child(parent_node, Lnast_node::create_if("if_pad"));

  auto idx_if_cstmts = lnast.add_child(idx_if, Lnast_node::create_cstmts(get_new_seq_name(lnast)));

  auto idx_dot1 = lnast.add_child(idx_if_cstmts, Lnast_node::create_dot("dot"));
  lnast.add_child(idx_dot1, Lnast_node::create_ref(temp_var_name_f0));
  I(op.arg_size() == 1);
  AttachExprToOperator(lnast, op.arg(0), idx_dot1);
  lnast.add_child(idx_dot1, Lnast_node::create_ref("__bits"));

  auto idx_lt = lnast.add_child(idx_if_cstmts, Lnast_node::create_lt("lt"));
  lnast.add_child(idx_lt, Lnast_node::create_ref(temp_var_name_f1));
  lnast.add_child(idx_lt, Lnast_node::create_ref(temp_var_name_f0));
  I(op.const__size() == 1);
  lnast.add_child(idx_lt, Lnast_node::create_const(lnast.add_string(absl::StrCat("0d", op.const_(0).value()))));


  auto idx_if_cond = lnast.add_child(idx_if, Lnast_node::create_cond(temp_var_name_f1));


  auto idx_if_stmtT = lnast.add_child(idx_if, Lnast_node::create_stmts(get_new_seq_name(lnast)));

  auto idx_dot2 = lnast.add_child(idx_if_stmtT, Lnast_node::create_dot("dot"));
  lnast.add_child(idx_dot2, Lnast_node::create_ref(temp_var_name_f2));
  lnast.add_child(idx_dot2, Lnast_node::create_ref(lnast.add_string(lhs)));
  lnast.add_child(idx_dot2, Lnast_node::create_ref("__bits"));

  auto idx_asgT = lnast.add_child(idx_if_stmtT, Lnast_node::create_assign("asg"));
  lnast.add_child(idx_asgT, Lnast_node::create_ref(temp_var_name_f2));
  I(op.const__size() == 1);
  lnast.add_child(idx_asgT, Lnast_node::create_const(op.const_(0).value()));


  auto idx_if_stmtF = lnast.add_child(idx_if, Lnast_node::create_stmts(get_new_seq_name(lnast)));

  auto idx_dot3 = lnast.add_child(idx_if_stmtF, Lnast_node::create_dot("dot"));
  lnast.add_child(idx_dot3, Lnast_node::create_ref(temp_var_name_f3));
  lnast.add_child(idx_dot3, Lnast_node::create_ref(lnast.add_string(lhs)));
  lnast.add_child(idx_dot3, Lnast_node::create_ref("__bits"));

  auto idx_dot4 = lnast.add_child(idx_if_stmtF, Lnast_node::create_dot("dot"));
  lnast.add_child(idx_dot4, Lnast_node::create_ref(temp_var_name_f4));
  AttachExprToOperator(lnast, op.arg(0), idx_dot4);
  //lnast.add_child(idx_dot4, Lnast_node::create_ref(lnast.add_string(lhs)));
  lnast.add_child(idx_dot4, Lnast_node::create_ref("__bits"));

  auto idx_asgF = lnast.add_child(idx_if_stmtF, Lnast_node::create_assign("asg"));
  lnast.add_child(idx_asgF, Lnast_node::create_ref(temp_var_name_f3));
  lnast.add_child(idx_asgF, Lnast_node::create_ref(temp_var_name_f4));


  auto idx_asg = lnast.add_child(parent_node, Lnast_node::create_assign("asg_pad"));
  lnast.add_child(idx_asg, Lnast_node::create_ref(lnast.add_string(lhs)));
  AttachExprToOperator(lnast, op.arg(0), idx_asg);
}

/* A SubField access is equivalent to accessing an element
 * of a tuple in LNAST. We have to create the associated "dot"
 * node(s) to be able to access the correct element of the
 * correct tuple. This function returns the string needed
 * to access it
 * As an example, here's the LNAST for "submod.io.a":
 *        dot              dot
 *     /   |   \         /  |  \
 * ___F0 submod io  ___F1 ___F0 a
 * where ___F1 would be returned by this function. */
std::string Inou_firrtl::handle_subfield_acc(Lnast& ln, const firrtl::FirrtlPB_Expression_SubField sub_field, Lnast_nid& parent_node) {
  std::stack<std::string> names;
  //Create a list of each tuple + the element... So submoid.io.a becomes [submod, io, a]
  create_name_stack(sub_field, names);

  /*fmt::print("[");
  while(!names.empty()) {
    fmt::print("{}, ", names.top());
    names.pop();
  }
  fmt::print("]\n");*/

  //Create each dot node
  auto bundle_accessor = names.top();
  names.pop();
  do {
    auto temp_var_name = create_temp_var(ln);
    auto element_name = names.top();
    names.pop();
    fmt::print("dot {} {} {}\n", temp_var_name, bundle_accessor, element_name);
    auto idx_dot = ln.add_child(parent_node, Lnast_node::create_dot(""));
    if (names.empty()) {
      ln.add_child(idx_dot, Lnast_node::create_ref(temp_var_name));
      ln.add_child(idx_dot, Lnast_node::create_ref(ln.add_string(bundle_accessor)));
      ln.add_child(idx_dot, Lnast_node::create_ref(ln.add_string(element_name)));
    } else {
      ln.add_child(idx_dot, Lnast_node::create_ref(temp_var_name));
      ln.add_child(idx_dot, Lnast_node::create_ref(ln.add_string(bundle_accessor)));
      ln.add_child(idx_dot, Lnast_node::create_ref(ln.add_string(element_name)));
    }

    bundle_accessor = temp_var_name;
  } while (!names.empty());

  fmt::print("return {}\n", bundle_accessor);
  return bundle_accessor;
}

void Inou_firrtl::create_name_stack(const firrtl::FirrtlPB_Expression_SubField sub_field, std::stack<std::string>& names) {
  names.push(sub_field.field());
  if (sub_field.expression().has_sub_field()) {
    create_name_stack(sub_field.expression().sub_field(), names);
  } else if (sub_field.expression().has_reference()) {
    names.push(sub_field.expression().reference().id());
  } else {
    I(false);
  }
}


//----------Ports-------------------------
/* This function is used for the following syntax rules in FIRRTL:
 * creating a wire, creating a register, instantiating an input/output (port),
 *
 * This function returns a pair which holds the full name of a wire/output/input/register
 * and the bitwidth of it (if the bw is 0, that means the bitwidth will be inferred later.
 */
void Inou_firrtl::create_io_list(const firrtl::FirrtlPB_Type& type, uint8_t dir, std::string port_id,
                                    std::vector<std::tuple<std::string, uint8_t, uint32_t>>& vec) {
  switch (type.type_case()) {
    case 2: { //UInt type
      vec.push_back(std::make_tuple(port_id, dir, type.uint_type().width().value()));
      break;

    } case 3: { //SInt type
      vec.push_back(std::make_tuple(port_id, dir, type.sint_type().width().value()));
      break;

    } case 4: { //Clock type
      vec.push_back(std::make_tuple(port_id, dir, 1));
      break;

    } case 5: { //Bundle type
      //cout << "Bundle {" << endl;
      const firrtl::FirrtlPB_Type_BundleType btype = type.bundle_type();
      for (int i = 0; i < type.bundle_type().field_size(); i++) {
        //cout << "\t" << btype.field(i).id() << ": ";
        //FIXME: This will flatten out any bundles from Chisel design, like in LoFIRRTL. Do we want that?
        if(btype.field(i).is_flipped()) {
          uint8_t new_dir;
          if (dir == 1) {
            new_dir = 2;
          } else if (dir == 2) {
            new_dir = 1;
          }
          create_io_list(btype.field(i).type(), new_dir, port_id + "." + btype.field(i).id(), vec);
        } else {
          create_io_list(btype.field(i).type(), dir, port_id + "." + btype.field(i).id(), vec);
        }
      }
      break;

    } case 6: { //Vector type
      //const firrtl::FirrtlPB_Type_VectorType vtype = type.vector_type();
      //cout << "FIXME: Vector[" << vtype.size()  << "]" << endl;
      //FIXME: How do we want to handle Vectors for LNAST? Should I flatten?
      //ListTypeInfo(vtype.type(), parent_node, );//FIXME: Should this be parent_idx?
      break;

    } case 7: { //Fixed type
      //cout << "Fixed[" << type.fixed_type().width().value() << "." << type.fixed_type().point().value() << "]" << endl;
      break;

    } case 8: { //Analog type
      //cout << "Analog[" << type.uint_type().width().value() << "]" << endl;
      break;

    } case 9: { //AsyncReset type
      //cout << "AsyncReset" << endl;
      break;

    } case 10: { //Reset type
      vec.push_back(std::make_tuple(port_id, dir, 1));
      break;

    } default:
      cout << "Unknown port type." << endl;
      return;
  }
}

/* This function iterates over the IO of a module and
 * sets the bitwidth of each using a dot node in LNAST. */
void Inou_firrtl::ListPortInfo(Lnast &lnast, const firrtl::FirrtlPB_Port& port, Lnast_nid parent_node) {
  std::vector<std::tuple<std::string, uint8_t, uint32_t>> port_list;//Terms are as follows: name, direction, # of bits.
  create_io_list(port.type(), port.direction(), port.id(), port_list);

  fmt::print("Port_list:\n");
  for(auto val : port_list) {
    if(std::get<1>(val) == 1) { //PORT_DIRECTION_IN
      input_names.push_back(std::get<0>(val));
      create_bitwidth_dot_node(lnast, std::get<2>(val), parent_node, absl::StrCat("$", std::get<0>(val)));
    } else if (std::get<1>(val) == 2) { //PORT_DIRECTION_OUT
      output_names.push_back(std::get<0>(val));
      create_bitwidth_dot_node(lnast, std::get<2>(val), parent_node, absl::StrCat("%", std::get<0>(val)));
    } else {
      I(false);//FIXME: I'm not sure yet how to deal with PORT_DIRECTION_UNKNOWN
    }
    fmt::print("\tname:{} dir:{} bits:{}\n", std::get<0>(val), std::get<1>(val), std::get<2>(val));
  }
}



//-----------Primitive Operations---------------------
void Inou_firrtl::PrintPrimOp(Lnast &lnast, const firrtl::FirrtlPB_Expression_PrimOp& op, const std::string symbol, Lnast_nid& parent_node) {
  for (int i = 0; i < op.arg_size(); i++) {
    AttachExprToOperator(lnast, op.arg(i), parent_node);
    if ((i == (op.arg_size()-1) && (op.const__size() == 0)))
        break;
    cout << symbol;
  }
  for (int j = 0; j < op.const__size(); j++) {
    lnast.add_child(parent_node, Lnast_node::create_const(op.const_(j).value()));
    cout << op.const_(j).value();
    if (j == (op.const__size()-1))
        break;
    cout << symbol;
  }
}

/* TODO:
 * Need review/testing:
 *   Tail
 *   Head
 *   Neg
 *   Extract_Bits
 *   Shift_Left/Right -- In FIRRTL these are different than what is used in Verilog. May need other way to represent.
 *   Or/And/Xor_Reduce -- Reductions use same node type as normal, but will only have 1 input "ref". Is this ok?
 *   Bit_Not
 *   Not_Equal
 *   Pad
 * Need 'As' node type:
 *   As_UInt
 *   As_SInt
 *   As_Clock
 *   As_Fixed_Point
 *   As_Async_Reset
 *   As_Interval
 * Rely upon intervals:
 *   Wrap
 *   Clip
 *   Squeeze
 * Rely upon precision/fixed point:
 *   Increase_Precision
 *   Decrease_Precision
 *   Set_Precision
 * Not yet implemented node types (?):
 *   Rem
 *   Concat
 *   Convert
 * In progress:
 *   -
 */
void Inou_firrtl::ListPrimOpInfo(Lnast& lnast, const firrtl::FirrtlPB_Expression_PrimOp& op, Lnast_nid& parent_node, std::string lhs) {
  switch(op.op()) {
    case 1: { //Op_Add
      auto idx_add = lnast.add_child(parent_node, Lnast_node::create_plus("plus"));
      lnast.add_child(idx_add, Lnast_node::create_ref(lnast.add_string(lhs)));
      PrintPrimOp(lnast, op, "+", idx_add);
      break;

    } case 2: { //Op_Sub
      auto idx_mns = lnast.add_child(parent_node, Lnast_node::create_minus("minus"));
      lnast.add_child(idx_mns, Lnast_node::create_ref(lnast.add_string(lhs)));
      PrintPrimOp(lnast, op, "-", idx_mns);
      break;

    } case 3: { //Op_Tail -- take in some 'n', returns value with 'n' MSBs removed
      HandleTailOp(lnast, op, parent_node, lhs);
      break;

    } case 4: { //Op_Head -- take in some 'n', returns 'n' MSBs of variable invoked on
      HandleHeadOp(lnast, op, parent_node, lhs);
      break;

    } case 5: { //Op_Times
      auto idx_mul = lnast.add_child(parent_node, Lnast_node::create_mult("mult"));
      lnast.add_child(idx_mul, Lnast_node::create_ref(lnast.add_string(lhs)));
      PrintPrimOp(lnast, op, "*", idx_mul);
      break;

    } case 6: { //Op_Divide
      auto idx_div = lnast.add_child(parent_node, Lnast_node::create_div("div"));
      lnast.add_child(idx_div, Lnast_node::create_ref(lnast.add_string(lhs)));
      PrintPrimOp(lnast, op, "/", idx_div);
      break;

    } case 7: { //Op_Rem
      PrintPrimOp(lnast, op, "%", parent_node);
      break;

    } case 8: { //Op_ShiftLeft
      //Note: used if one operand is variable, other is const #.
      //      a = x << #... bw(a) = w(x) + #
      auto idx_shl = lnast.add_child(parent_node, Lnast_node::create_shift_left("sl"));
      lnast.add_child(idx_shl, Lnast_node::create_ref(lnast.add_string(lhs)));
      PrintPrimOp(lnast, op, "<<", idx_shl);
      break;

    } case 9: { //Op_Shift_Right
      //Note: used if one operand is variable, other is const #.
      //      a = x >> #... bw(a) = w(x) - #
      auto idx_shr = lnast.add_child(parent_node, Lnast_node::create_shift_right("sr"));
      lnast.add_child(idx_shr, Lnast_node::create_ref(lnast.add_string(lhs)));
      PrintPrimOp(lnast, op, ">>", idx_shr);
      break;

    } case 10: { //Op_Dynamic_Shift_Left
      //Note: used if operands are both variables.
      //      a = x << y... bw(a) = w(x) + maxVal(y)
      auto idx_dshl = lnast.add_child(parent_node, Lnast_node::create_dynamic_shift_left("dsl"));
      lnast.add_child(idx_dshl, Lnast_node::create_ref(lnast.add_string(lhs)));
      PrintPrimOp(lnast, op, "<<d", idx_dshl);
      break;

    } case 11: { //Op_Dynamic_Shift_Right
      //Note: used if operands are both variables.
      //      a = x >> y... bw(a) = w(x) - minVal(y)
      auto idx_dshr = lnast.add_child(parent_node, Lnast_node::create_dynamic_shift_right("dsr"));
      lnast.add_child(idx_dshr, Lnast_node::create_ref(lnast.add_string(lhs)));
      PrintPrimOp(lnast, op, ">>d", idx_dshr);
      break;

    } case 12: { //Op_Bit_And
      auto idx_and = lnast.add_child(parent_node, Lnast_node::create_and("and"));
      lnast.add_child(idx_and, Lnast_node::create_ref(lnast.add_string(lhs)));
      PrintPrimOp(lnast, op, " & ", idx_and);
      break;

    } case 13: { //Op_Bit_Or
      auto idx_or = lnast.add_child(parent_node, Lnast_node::create_or("or"));
      lnast.add_child(idx_or, Lnast_node::create_ref(lnast.add_string(lhs)));
      PrintPrimOp(lnast, op, " | ", idx_or);
      break;

    } case 14: { //Op_Bit_Xor
      auto idx_xor = lnast.add_child(parent_node, Lnast_node::create_xor("xor"));
      lnast.add_child(idx_xor, Lnast_node::create_ref(lnast.add_string(lhs)));
      PrintPrimOp(lnast, op, " ^ ", idx_xor);
      break;

    } case 15: { //Op_Bit_Not
      HandleUnaryOp(lnast, op, parent_node, lhs);
      break;

    } case 16: { //Op_Concat
      cout << "Cat(";
      PrintPrimOp(lnast, op, ", ", parent_node);
      cout << ");";
      break;

    } case 17: { //Op_Less
      auto idx_lt = lnast.add_child(parent_node, Lnast_node::create_lt("lt"));
      lnast.add_child(idx_lt, Lnast_node::create_ref(lnast.add_string(lhs)));
      PrintPrimOp(lnast, op, "<", idx_lt);
      break;

    } case 18: { //Op_Less_Eq
      auto idx_lte = lnast.add_child(parent_node, Lnast_node::create_le("lte"));
      lnast.add_child(idx_lte, Lnast_node::create_ref(lnast.add_string(lhs)));
      PrintPrimOp(lnast, op, "<=", idx_lte);
      break;

    } case 19: { //Op_Greater
      auto idx_gt = lnast.add_child(parent_node, Lnast_node::create_gt("gt"));
      lnast.add_child(idx_gt, Lnast_node::create_ref(lnast.add_string(lhs)));
      PrintPrimOp(lnast, op, ">", idx_gt);
      break;

    } case 20: { //Op_Greater_Eq
      auto idx_gte = lnast.add_child(parent_node, Lnast_node::create_ge("gte"));
      lnast.add_child(idx_gte, Lnast_node::create_ref(lnast.add_string(lhs)));
      PrintPrimOp(lnast, op, ">=", idx_gte);
      break;

    } case 21: { //Op_Equal
      auto idx_eq = lnast.add_child(parent_node, Lnast_node::create_same("same"));
      lnast.add_child(idx_eq, Lnast_node::create_ref(lnast.add_string(lhs)));
      PrintPrimOp(lnast, op, "===", idx_eq);
      break;

    } case 22: { //Op_Pad
      HandlePadOp(lnast, op, parent_node, lhs);
      break;

    } case 23: { //Op_Not_Equal
      HandleNEQOp(lnast, op, parent_node, lhs);
      break;

    } case 24: { //Op_Negate -- this takes a # (UInt or SInt) and returns it's negative value 10 -> -10 or -20 -> 20.
      //Note: the output's bitwidth = bitwidth of the input + 1.
      HandleNegateOp(lnast, op, parent_node, lhs);
      break;

    } case 26: { //Op_Xor_Reduce
      //HandleUnaryOp(op, parent_node, lhs, "^");
      //break;
      auto idx_xor = lnast.add_child(parent_node, Lnast_node::create_xor("xorR"));
      lnast.add_child(idx_xor, Lnast_node::create_ref(lnast.add_string(lhs)));
      PrintPrimOp(lnast, op, " ^ ", idx_xor);
  lnast.add_child(idx_xor, Lnast_node::create_const("0d1"));
      break;

    } case 27: { //Op_Convert
      cout << "primOp: " << op.op();
      break;

    } case 28: { //Op_As_UInt
      PrintPrimOp(lnast, op, "", parent_node);
      cout << ".asUint";
      break;

    } case 29: { //Op_As_SInt
      PrintPrimOp(lnast, op, "", parent_node);
      cout << ".asSint";
      break;

    } case 30: { //Op_Extract_Bits -- this is what's used for grabbing a range of bits from a variable (i.e. foo[3:1])
      //Note to self: extract bits has two parameters which always must be static int literals
      HandleExtractBitsOp(lnast, op, parent_node, lhs);
      break;

    } case 31: { //Op_As_Clock
      PrintPrimOp(lnast, op, "", parent_node);
      cout << ".asClock";
      break;

    } case 32: { //Op_As_Fixed_Point
      //FIXME: Might need to take one # from front into parens so I know precision bit count
      PrintPrimOp(lnast, op, ".asFixedPoint(", parent_node);
      cout << ")";
      break;

    } case 33: { //Op_And_Reduce
      //HandleUnaryOp(op, parent_node, lhs, "&");
      //break;
      auto idx_and = lnast.add_child(parent_node, Lnast_node::create_and("andR"));
      lnast.add_child(idx_and, Lnast_node::create_ref(lnast.add_string(lhs)));
      PrintPrimOp(lnast, op, " & ", idx_and);
  lnast.add_child(idx_and, Lnast_node::create_const("0d1"));
      break;

    } case 34: { //Op_Or_Reduce
      //HandleUnaryOp(op, parent_node, lhs, "|");
      //break;
      auto idx_or = lnast.add_child(parent_node, Lnast_node::create_or("orR"));
      lnast.add_child(idx_or, Lnast_node::create_ref(lnast.add_string(lhs)));
      PrintPrimOp(lnast, op, " | ", idx_or);
  lnast.add_child(idx_or, Lnast_node::create_const("0d1"));
      break;

    } case 35: { //Op_Increase_Precision
      //FIXME: Might need to take one # from front into parens so I know precision bit count
      PrintPrimOp(lnast, op, ".increasePrecision(", parent_node);
      cout << ")";
      break;

    } case 36: { //Op_Decrease_Precision
      //FIXME: Might need to take one # from front into parens so I know precision bit count
      PrintPrimOp(lnast, op, ".decreasePrecision(", parent_node);
      cout << ")";
      break;

    } case 37: { //Op_Set_Precision
      PrintPrimOp(lnast, op, ".setPrecision(", parent_node);
      cout << ")";
      break;

    } case 38: { //Op_As_Async_Reset
      PrintPrimOp(lnast, op, "", parent_node);
      cout << ".asAsyncReset";
      break;

    } case 39: { //Op_Wrap ----- FIXME: Rely upon Intervals (not supported in LNAST yet?)
      cout << "primOp: " << op.op();
      break;

    } case 40: { //Op_Clip ----- FIXME: Rely upon Intervals (not supported in LNAST yet?)
      cout << "primOp: " << op.op();
      break;

    } case 41: { //Op_Squeeze ----- FIXME: Rely upon Intervals (not supported in LNAST yet?)
      cout << "primOp: " << op.op();
      break;

    } case 42: { //Op_As_interval ----- FIXME: Rely upon Intervals (not supported in LNAST yet?)
      cout << "primOp: " << op.op();
      break;

    } default:
      cout << "Unknown PrimaryOp\n";
      assert(false);
  }
}

//--------------Expressions-----------------------
/*TODO:
 * UIntLiteral (make sure used correct syntax: #u(bits))
 * SIntLiteral (make sure used correct syntax: #s(bits))
 * FixedLiteral
 * SubField (figure out how to include #/$/% if necessary)
 */

/* */
void Inou_firrtl::InitialExprAdd(Lnast& lnast, const firrtl::FirrtlPB_Expression& expr, Lnast_nid& parent_node, std::string lhs_unaltered) {
  //Note: here, parent_node is the "stmt" node above where this expression will go.
  I(lnast.get_data(parent_node).type.is_stmts() || lnast.get_data(parent_node).type.is_cstmts());
  auto lhs = get_full_name_lhs(lhs_unaltered);
  switch(expr.expression_case()) {
    case 1: { //Reference
      Lnast_nid idx_asg;
      //idx_asg = lnast.add_child(parent_node, Lnast_node::create_assign("asg"));
      if (lhs.substr(0,1) == "%") {
        idx_asg = lnast.add_child(parent_node, Lnast_node::create_dp_assign("dp_asg"));
      } else {
        idx_asg = lnast.add_child(parent_node, Lnast_node::create_assign("asg"));
      }
      lnast.add_child(idx_asg, Lnast_node::create_ref(lnast.add_string(lhs)));
      auto full_name = get_full_name(expr.reference().id());
      lnast.add_child(idx_asg, Lnast_node::create_ref(lnast.add_string(full_name)));//expr.reference().id()));
      break;

    } case 2: { //UIntLiteral
      auto idx_asg = lnast.add_child(parent_node, Lnast_node::create_assign("asg"));
      lnast.add_child(idx_asg, Lnast_node::create_ref(lnast.add_string(lhs)));
      auto str_val = absl::StrCat("0d", expr.uint_literal().value().value());// + "u" + to_string(expr.uint_literal().width().value());
      lnast.add_child(idx_asg, Lnast_node::create_const(lnast.add_string(str_val)));
      break;

    } case 3: { //SIntLiteral
      auto idx_asg = lnast.add_child(parent_node, Lnast_node::create_assign("asg"));
      lnast.add_child(idx_asg, Lnast_node::create_ref(lnast.add_string(lhs)));
      auto str_val = absl::StrCat("0d", expr.sint_literal().value().value());// + "s" + to_string(expr.sint_literal().width().value());
      lnast.add_child(idx_asg, Lnast_node::create_const(lnast.add_string(str_val)));
      break;

    } case 4: { //ValidIf
      HandleValidIfAssign(lnast, expr, parent_node, lhs);
      break;

    } case 6: { //Mux
      HandleMuxAssign(lnast, expr, parent_node, lhs);
      break;

    } case 7: { //SubField
      std::string rhs = handle_subfield_acc(lnast, expr.sub_field(), parent_node);

      auto idx_asg = lnast.add_child(parent_node, Lnast_node::create_assign("asg"));
      lnast.add_child(idx_asg, Lnast_node::create_ref(lnast.add_string(lhs)));
      lnast.add_child(idx_asg, Lnast_node::create_ref(lnast.add_string(rhs)));
      break;

    } case 8: { //SubIndex
      auto idx_select = lnast.add_child(parent_node, Lnast_node(Lnast_ntype::create_select(), Token(0, 0, 0, 0, "selectSI")));
      lnast.add_child(idx_select, Lnast_node::create_ref(lnast.add_string(lhs)));
      //SecondaryExprCheck() -- attach expression to idx_select
      AttachExprToOperator(lnast, expr.sub_index().expression(), idx_select);
      lnast.add_child(idx_select, Lnast_node::create_const(expr.sub_index().index().value()));
      break;

    } case 9: { //SubAccess
      auto idx_select = lnast.add_child(parent_node, Lnast_node(Lnast_ntype::create_select(), Token(0, 0, 0, 0, "selectSA")));
      lnast.add_child(idx_select, Lnast_node::create_ref(lnast.add_string(lhs)));
      //SecondaryExprCheck() -- attach expression to idx_select
      //SecondaryExprCheck() -- attach index to idx_select
      AttachExprToOperator(lnast, expr.sub_access().expression(), idx_select);
      AttachExprToOperator(lnast, expr.sub_access().index(), idx_select);
      break;

    } case 10: { //PrimOp
      ListPrimOpInfo(lnast, expr.prim_op(), parent_node, lhs);
      break;

    } case 11: { //FixedLiteral
      auto idx_asg = lnast.add_child(parent_node, Lnast_node::create_assign("asg_FP"));
      lnast.add_child(idx_asg, Lnast_node::create_ref(lnast.add_string(lhs)));
      //FIXME: How do I represent a FixedPoint literal???
      break;

    } default:
      cout << "ERROR in InitialExprAdd ... unknown expression type: " << expr.expression_case() << endl;
      assert(false);
  }
}

void Inou_firrtl::AttachExprToOperator(Lnast& lnast, const firrtl::FirrtlPB_Expression& expr, Lnast_nid& parent_node) {
  AttachExprToOperator(lnast, expr, parent_node, "");
}

void Inou_firrtl::AttachExprToOperator(Lnast& lnast, const firrtl::FirrtlPB_Expression& expr, Lnast_nid& parent_node, std::string tail) {
  switch(expr.expression_case()) {
    case 1: { //Reference
      cout << expr.reference().id();
      if(tail == "") {
        auto expr_name = get_full_name(expr.reference().id());
        lnast.add_child(parent_node, Lnast_node::create_ref(lnast.add_string(expr_name)));//expr.reference().id()));
      } else {
        std::string whole_name = expr.reference().id() + "." + tail;
        auto expr_name = get_full_name(whole_name);
        lnast.add_child(parent_node, Lnast_node::create_ref(lnast.add_string(expr_name)));//lnast.add_string(full_name)));
      }
      break;

    } case 2: { //UIntLiteral
      auto const_val = absl::StrCat("0d", expr.uint_literal().value().value());// + "u" + to_string(expr.uint_literal().width().value());
      lnast.add_child(parent_node, Lnast_node::create_const(lnast.add_string(const_val)));
      break;

    } case 3: { //SIntLiteral
      auto const_val = absl::StrCat("0d", expr.sint_literal().value().value());// + "s" + to_string(expr.sint_literal().width().value());
      lnast.add_child(parent_node, Lnast_node::create_const(lnast.add_string(const_val)));
      break;

    } case 4: { //ValidIf -- "conditionally valids", looks like: c <= validIf(valid, a), if valid then c = a else c = undefined
      cout << "AttachExprToOperator Error: Handling validIfs should not be handled here (see HandleValidIfAssign function)\n";
      assert(false);
      break;

    } case 6: { //Mux -- always has form mux(sel, a, b);
      cout << "AttachExprToOperator Error: Handling muxes should not be handled here (see HandleMuxAssign function)\n";
      assert(false);
      break;

    } case 7: { //SubField -- this is called when you're accessing a bundle's field (like io.var1)
      AttachExprToOperator(lnast, expr.sub_field().expression(), parent_node, expr.sub_field().field());
      cout << "." << expr.sub_field().field();
      break;

    } case 8: { //SubIndex -- this is used when statically accessing an element of a vector-like object
      cout << "Subindex()\n";
      auto idx_select = lnast.add_child(parent_node, Lnast_node(Lnast_ntype::create_select(), Token(0, 0, 0, 0, "selectSI")));
      AttachExprToOperator(lnast, expr.sub_index().expression(), idx_select);
      lnast.add_child(parent_node, Lnast_node::create_const(expr.sub_index().index().value()));
      break;

    } case 9: { //SubAccess -- this is used when dynamically accessing an element of a vector-like object
      cout << "Subaccess()\n";
      auto idx_select = lnast.add_child(parent_node, Lnast_node(Lnast_ntype::create_select(), Token(0, 0, 0, 0, "selectSA")));
      AttachExprToOperator(lnast, expr.sub_access().expression(), idx_select);
      AttachExprToOperator(lnast, expr.sub_access().index(), idx_select);
      break;

    } case 10: { //PrimOp
      //ListPrimOpInfo(expr.prim_op(), parent_node);
      assert(false);
      break;

    } case 11: { //FixedLiteral
      //FIXME: I'm not sure how to deal with FixedPoint literals yet.
      //
      cout << expr.fixed_literal().value().value() << " ("
           << expr.fixed_literal().width().value() << ".W, "
           << expr.fixed_literal().point().value() << ".W)";
      break;

    } default:
      cout << "Unknown expression type: " << expr.expression_case() << endl;
      assert(false);
  }
}

/* TODO:
 *   Make sure all of these are formatted consistently with other LNAST representations.
 */
/* This function is used when I need the string to access something.
 * If it's a Reference or a Const, we format them as a string and return.
 * If it's a SubField, we have to create dot nodes and get the variable
 * name that points to the right bundle element (see handle_subfield_acc function). */
std::string Inou_firrtl::ReturnExprString(Lnast& lnast, const firrtl::FirrtlPB_Expression& expr, Lnast_nid& parent_node) {
  std::string expr_string = "";
  switch(expr.expression_case()) {
    case 1: { //Reference
      expr_string = expr.reference().id();
      break;
    } case 2: { //UIntLiteral
      expr_string = absl::StrCat("0d", expr.uint_literal().value().value());// + "u" + to_string(expr.uint_literal().width().value());
      break;
    } case 3: { //SIntLiteral
      expr_string = absl::StrCat("0d", expr.sint_literal().value().value());// + "s" + to_string(expr.sint_literal().width().value());
      break;
    } case 7: { //SubField
      expr_string = handle_subfield_acc(lnast, expr.sub_field(), parent_node);
      break;
    } case 11: { //FixedLiteral
      //FIXME: Unsure of how this should be.
      break;
    } default:
      //Error: I don't think this should occur if we're using Chisel's protobuf utility.
      I(false);
  }
  return expr_string;
}

//------------Statements----------------------
/*TODO:
 * Wire -- I don't think I need to do anything for this unless bw is explicit. If so, create "dot" node.
 * Register -- Same as Wire.
 * Memory
 * CMemory
 * Instances
 * Stop
 * Printf
 * Connect
 * PartialConnect
 * IsInvalid
 * MemoryPort
 * Attach
*/
void Inou_firrtl::ListStatementInfo(Lnast& lnast, const firrtl::FirrtlPB_Statement& stmt, Lnast_nid& parent_node) {
  //Print out statement
  switch(stmt.statement_case()) {
    case 1: { //Wire
      //FIXME BIG: Need to make a new interface for wire, since I altered old one.
      //ListTypeInfo(lnast, stmt.wire().type(), parent_node, stmt.wire().id());
      break;

    } case 2: { //Register
      register_names.push_back(stmt.register_().id());
      init_register_dots(lnast, stmt.register_(), parent_node);
      break;

    } case 3: { //Memory
      cout << "mem " << stmt.memory().id() << " :\n\t";
      //ListTypeInfo(
      cout << "\tdepth => ";
      switch(stmt.memory().depth_case()) {
        case 0: {
          cout << "Depth not set, ERROR\n";
          break;
        } case 3: {
          cout << stmt.memory().uint_depth() << "\n";
          break;
        } case 9: {
          //FIXME: Not sure this case will work properly... More testing needed.
          std::string depth = stmt.memory().bigint_depth().value();//2s complement binary rep.
          cout << depth << "\n";
          break;
        } default:
          cout << "Memory depth error\n";
      }
      cout << "\tread-latency => " << stmt.memory().read_latency() << "\n";
      cout << "\twrite-latency => " << stmt.memory().write_latency() << "\n";
      for (int i = 0; i < stmt.memory().reader_id_size(); i++) {
        cout << "\treader => " << stmt.memory().reader_id(i) << "\n";
      }
      for (int j = 0; j < stmt.memory().writer_id_size(); j++) {
        cout << "\twriter => " << stmt.memory().writer_id(j) << "\n";
      }
      for (int k = 0; k < stmt.memory().readwriter_id_size(); k++) {
        cout << "\tread-writer => " << stmt.memory().readwriter_id(k) << "\n";
      }
      cout << "\tread-under-write <= ";
      switch(stmt.memory().read_under_write()) {
        case 0:
          cout << "undefined\n";
          break;
        case 1:
          cout << "old\n";
          break;
        case 2:
          cout << "new\n";
          break;
        default:
          cout << "RUW Error...\n";
      }
      break;

    } case 4: { //CMemory
      break;

    } case 5: { //Instance -- creating an instance of a module inside another
      fmt::print("----Instance!\n");
      fmt::print("id: {}\n", stmt.instance().id());
      fmt::print("module_id: {}\n", stmt.instance().module_id());

      create_module_inst(lnast, stmt.instance(), parent_node);
      break;

    } case 6: { //Node -- nodes are simply named intermediates in a circuit
      cout << "node " << stmt.node().id();
      cout << " = ";

      InitialExprAdd(lnast, stmt.node().expression(), parent_node, stmt.node().id());

      cout << "\n";
      break;

    } case 7: { //When
      auto idx_when = lnast.add_child(parent_node, Lnast_node::create_if("when"));
      lnast.add_child(idx_when, Lnast_node::create_cstmts(""));//NOTE: Empty cstmts right now for SSA.
      CreateConditionNode(lnast, stmt.when().predicate(), idx_when, parent_node);
      auto idx_stmts_ifTrue = lnast.add_child(idx_when, Lnast_node::create_stmts(get_new_seq_name(lnast)));
      //FIXME: I might have to conform to cstmts model. If that's the case, add those here/in this case block.


      cout << "when(";
      cout << ") {\n";
      for (int i = 0; i < stmt.when().consequent_size(); i++) {
        ListStatementInfo(lnast, stmt.when().consequent(i), idx_stmts_ifTrue);
      }
      if(stmt.when().otherwise_size() > 0) {
        cout << "} .otherwise {\n";
        auto idx_stmts_ifFalse = lnast.add_child(idx_when, Lnast_node::create_stmts(get_new_seq_name(lnast)));
        for (int j = 0; j < stmt.when().otherwise_size(); j++) {
          ListStatementInfo(lnast, stmt.when().otherwise(j), idx_stmts_ifFalse);
        }
      }
      cout << "}\n";
      break;

    } case 8: { //Stop
      cout << "stop(" << stmt.stop().return_value() << ")\n";
      break;

    } case 10: { //Printf
      //FIXME: Not fully implemented, I think.
      cout << "printf(" << stmt.printf().value() << ")\n";

      break;

    } case 14: { //Skip
      cout << "skip;\n";
      break;

    } case 15: { //Connect -- Must have form (female/bi-gender expression) <= (male/bi-gender/passive expression)
      //FIXME: Should this be just an "assign" or something special?
      //
      /*auto idx_asg = lnast->add_child(parent_node, Lnast_node::create_assign("asg"));

      AttachExprToOperator(stmt.connect().location(), idx_asg);
      cout << " <= ";
      AttachExprToOperator(stmt.connect().expression(), idx_asg);
      cout << ";\n";*/
      std::string lhs_string = ReturnExprString(lnast, stmt.connect().location(), parent_node);
      InitialExprAdd(lnast, stmt.connect().expression(), parent_node, lhs_string);

      break;

    } case 16: { //PartialConnect
      //FIXME: Like Connect, should bundles have some special type of assign?
      /*AttachExprToOperator(stmt.connect().location(), parent_node);//FIXME
      cout << " <- ";
      AttachExprToOperator(stmt.connect().expression(), parent_node);//FIXME
      cout << ";\n";*/
      cout << "Error: need to design partialConnect in ListStatementInfo.\n";
      break;

    } case 17: { //IsInvalid
      break;

    } case 18: { //MemoryPort
      break;

    } case 20: { //Attach
      cout << "Attach\n";
      break;

    } default:
      cout << "Unknown statement type." << endl;
      return;
  }

  //TODO: Attach source info into node creation (line #, col #).
}

//--------------Modules/Circuits--------------------
//Create basis of LNAST tree. Set root to "top" and have "stmts" be top's child.
void Inou_firrtl::ListUserModuleInfo(Eprp_var &var, const firrtl::FirrtlPB_Module& module) {
  cout << "Module (user): " << module.user_module().id() << endl;
  std::unique_ptr<Lnast> lnast = std::make_unique<Lnast>(module.user_module().id());

  const firrtl::FirrtlPB_Module_UserModule& user_module = module.user_module();

  //lnast = std::make_unique<Lnast>("top_module_name"); // NOTE: no need to transfer ownership (no parser)

  lnast->set_root(Lnast_node(Lnast_ntype::create_top(), Token(0, 0, 0, 0, "top")));
  auto idx_stmts = lnast->add_child(lnast->get_root(), Lnast_node::create_stmts(get_new_seq_name(*lnast)));

  //Iterate over I/O of the module.
  for (int i = 0; i < user_module.port_size(); i++) {
    const firrtl::FirrtlPB_Port& port = user_module.port(i);
    ListPortInfo(*lnast, port, idx_stmts);
  }

  //Iterate over statements of the module.
  for (int j = 0; j < user_module.statement_size(); j++) {
    const firrtl::FirrtlPB_Statement& stmt = user_module.statement(j);
    ListStatementInfo(*lnast, stmt, idx_stmts);
    //lnast->dump();
  }
  lnast->dump();
  var.add(std::move(lnast));
}

//TODO: External module handling.
void Inou_firrtl::ListModuleInfo(Eprp_var &var, const firrtl::FirrtlPB_Module& module) {
  if(module.module_case() == 1) {
    cout << "External module.\n";
  } else if (module.module_case() == 2) {
    ListUserModuleInfo(var, module);
  } else {
    cout << "Module not set.\n";
    assert(false);
  }
}

//Invoke function which creates LNAST tree for each module. After created, push back into vector.
void Inou_firrtl::IterateModules(Eprp_var &var, const firrtl::FirrtlPB_Circuit& circuit) {
  for (int i = 0; i < circuit.module_size(); i++) {
    if (circuit.top_size() > 1) {
      cout << "ERROR: More than 1 top module?\n";
      assert(false);
    }

    //For each module of the circuit, create its own lnast->
    //auto lnast_tmp = ListModuleInfo(circuit.module(i));
    ListModuleInfo(var, circuit.module(i));
    //*lnast->dump();
    //lnast_vec.push_back(lnast);
  }
}

//Iterate over every FIRRTL circuit (design), each circuit can contain multiple modules.
void Inou_firrtl::IterateCircuits(Eprp_var &var, const firrtl::FirrtlPB& firrtl_input) {
  for (int i = 0; i < firrtl_input.circuit_size(); i++) {
    const firrtl::FirrtlPB_Circuit& circuit = firrtl_input.circuit(i);
    IterateModules(var, circuit);
  }
}

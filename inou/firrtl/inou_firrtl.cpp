//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
#include "inou_firrtl.hpp"

#include <fstream>
#include <google/protobuf/util/time_util.h>
#include <iostream>
#include <string>
#include <charconv>
#include <stdlib.h>

#include "firrtl.pb.h"

using namespace std;

using google::protobuf::util::TimeUtil;

//----------------Helper Functions--------------------------

//If the bitwidth is specified, in LNAST we have to create a new variable which represents
//  the number of bits that a variable will have.
//FIXME: I need to add stuff to determine if input/output/register and add $/%/# respectively.
void Inou_firrtl::CreateBitwidthAttribute(uint32_t bitwidth, Lnast_nid& parent_node, std::string port_id) {
  std::string str_fix = "___" + port_id;

  auto idx_dot = lnast.add_child(parent_node, Lnast_node::create_dot("dot"));
  lnast.add_child(idx_dot, Lnast_node::create_ref( str_fix ));
  lnast.add_child(idx_dot, Lnast_node::create_ref(port_id));
  lnast.add_child(idx_dot, Lnast_node::create_ref("__bits"));

  auto idx_asg = lnast.add_child(parent_node, Lnast_node::create_assign("asg"));
  lnast.add_child(idx_asg, Lnast_node::create_ref(str_fix));
  lnast.add_child(idx_asg, Lnast_node::create_const(to_string(bitwidth)));//TODO: Make sure this gives right value.
}

/* These functions are useful because they analyze what the condition of something is
 * (useful for whens, muxes, ifs, etc.) but instead of creating some node type based
 * off the expression type, they instead create a "condition" typed node whose
 * token is the condition itself. */
void Inou_firrtl::CreateConditionNode(const firrtl::FirrtlPB_Expression& expr, Lnast_nid& parent_node) {
  CreateConditionNode(expr, parent_node, "");
}

void Inou_firrtl::CreateConditionNode(const firrtl::FirrtlPB_Expression& expr, Lnast_nid& parent_node, const std::string tail) {
  switch(expr.expression_case()) {
    case 1: { //Reference
      cout << expr.reference().id();
      if(tail == "") {
        lnast.add_child(parent_node, Lnast_node::create_cond(expr.reference().id()));
      } else {
        std::string full_name = expr.reference().id() + "." + tail;
        //TODO: Figure out how to do this better since right now this leads to string_view out-of-scope corruption.
        lnast.add_child(parent_node, Lnast_node::create_cond(full_name));
      }
      break;
    } case 7: { //SubField -- this is called when you're accessing a bundle's field (like io.var1)
      CreateConditionNode(expr.sub_field().expression(), parent_node, expr.sub_field().field());
      cout << "." << expr.sub_field().field();
      break;

    } default:
      cout << "CreateConditionNode: ERROR. Trying to create a condition node for an expression not yet supported or unknown.: " << expr.expression_case() << endl;
      return;
  }
}

/* No mux node type exists in LNAST. To support FIRRTL muxes, we instead
 * map a mux to an if-else statement whose condition is the same condition
 * as the first argument of the mux. */
void Inou_firrtl::HandleMuxAssign(const firrtl::FirrtlPB_Expression& expr, Lnast_nid& parent_node, std::string lhs_of_asg) {
  cout << "Mux\n";

  auto idx_mux_if    = lnast.add_child(parent_node, Lnast_node::create_if("mux"));
  CreateConditionNode(expr.mux().condition(), idx_mux_if);
  auto idx_stmt_tr   = lnast.add_child(idx_mux_if, Lnast_node::create_stmts("mux_stmt_true"));
  auto idx_stmt_f    = lnast.add_child(idx_mux_if, Lnast_node::create_stmts("mux_stmt_false"));

  auto idx_asg_true  = lnast.add_child(idx_stmt_tr, Lnast_node::create_assign("assign"));
  lnast.add_child(idx_asg_true, Lnast_node::create_ref(lhs_of_asg));
  ListExprInfo(expr.mux().t_value(), idx_asg_true);

  auto idx_asg_false = lnast.add_child(idx_stmt_f, Lnast_node::create_assign("assign"));
  lnast.add_child(idx_asg_false, Lnast_node::create_ref(lhs_of_asg));
  ListExprInfo(expr.mux().f_value(), idx_asg_false);
}

/* ValidIfs get detected as the RHS of an assign statement and we can't have a child of
 * an assign be an if-typed node. Thus, we have to detect ahead of time if it is a validIf
 * if we're doing an assign. If that is the case, do this instead of using ListExprType().*/
void Inou_firrtl::HandleValidIfAssign(const firrtl::FirrtlPB_Expression& expr, Lnast_nid& parent_node, std::string lhs_of_asg) {
  cout << "ValidIf\n";

  auto idx_v_if      = lnast.add_child(parent_node, Lnast_node::create_if("validIf"));
  CreateConditionNode(expr.valid_if().condition(), idx_v_if);
  auto idx_stmt_tr   = lnast.add_child(idx_v_if, Lnast_node::create_stmts("vIf_stmt_true"));
  auto idx_stmt_f    = lnast.add_child(idx_v_if, Lnast_node::create_stmts("vIf_stmt_false"));

  auto idx_asg_true  = lnast.add_child(idx_stmt_tr, Lnast_node::create_assign("assign"));
  lnast.add_child(idx_asg_true, Lnast_node::create_ref(lhs_of_asg));
  ListExprInfo(expr.valid_if().value(), idx_v_if);

  //For validIf, if the condition is not met then what the LHS equals is undefined. We'll just use 0.
  auto idx_asg_false = lnast.add_child(idx_stmt_f, Lnast_node::create_assign("assign"));
  lnast.add_child(idx_asg_false, Lnast_node::create_ref(lhs_of_asg));
  lnast.add_child(idx_asg_false, Lnast_node::create_const("0"));
}

/* We have to handle NEQ operations different than any other primitive op.
 * This is because NEQ has to be broken down into two sub-operations:
 * checking equivalence and then performing the not. */
void Inou_firrtl::HandleNEQOp(const firrtl::FirrtlPB_Expression_PrimOp& op, Lnast_nid& parent_node) {
  //auto stmt_node = get_parent(parent_node);

  //auto idx_eq = lnast.add_child(stmt_node, Lnast_node::create_eq("eq2"));
  //FIXME not done yet...
  //PrintPrimOp(op, "===", parent_node);
}

/* "Not" operations are handled in a way where (currently) there is no LNAST
 * node type that supports "not". Instead, we would want to have an assign
 * node and have the "rhs" child of the assign node be "~temp". */
void Inou_firrtl::HandleNotOp(const firrtl::FirrtlPB_Expression_PrimOp& op, Lnast_nid& parent_node) {
  /*std::string str_not_port;// = "___" + port_id;

  if (op.arg_size() == 1) {
    //FIXME: Current problem is that I can't just get the string for the arg, or it's possible arg(0)
    //  isn't even a string. It could possibly be a prim op, in which this would no longer work,
    //  or at least it would involve a more thorough solution.
    //str_not_port = "~" + op.arg(0);
  } else if (op.const__size() == 1) {
    str_not_port = "~" + op.const_(0).value();
  } else {
    cout << "Error in HandleNotOp: too many operators given ('not' should have 1 argument)." << endl;
  }
  lnast.add_child(parent_node, Lnast_node::create_ref(str_not_port));*/

}

/* The Extract Bits primitive op is invoked on some variable
 * and functions as you would expect in a language like Verilog.
 * We have to break this down into multiple statements so
 * LNAST can properly handle it (see diagram below).*/
void Inou_firrtl::HandleExtractBitsOp(const firrtl::FirrtlPB_Expression_PrimOp& op, Lnast_nid& parent_node) {
  auto idx_stmt = lnast.get_parent(parent_node);
  //I(idx_stmt.is_stmts() | idx_stmt.is_cstmts());

  /* x = a[num1:num2] should take graph form:
   *     range                bit_sel                 asg
   *    /   |   \             /   |   \             /     \
   *___F0 num1 num2        ___F1  a ___F0          x    ___F1 */

  //FIXME: Out-of-scope string issues
  auto idx_range = lnast.add_child(idx_stmt, Lnast_node::create_range("range_EB"));
  lnast.add_child(idx_range, Lnast_node::create_ref("___F" + to_string(id_counter)));
  I(op.const__size() == 2);
  lnast.add_child(idx_range, Lnast_node::create_const(op.const_(0).value()));
  lnast.add_child(idx_range, Lnast_node::create_const(op.const_(1).value()));

  auto idx_bit_sel = lnast.add_child(idx_stmt, Lnast_node::create_bit_select("bit_sel_EB"));
  lnast.add_child(idx_bit_sel, Lnast_node::create_ref("___F" + to_string(id_counter+1)));
  I(op.arg_size() == 1);
  ListExprInfo(op.arg(0), idx_bit_sel);//FIXME(?)
  lnast.add_child(idx_bit_sel, Lnast_node::create_ref("___F" + to_string(id_counter)));

  lnast.add_child(parent_node, Lnast_node::create_ref("___F" + to_string(id_counter + 1)));

  id_counter += 2;
}

/* The Head primitive op requires special handling since
 * it is actually doing quite a lot. We have to break this
 * down into multiple statements so LNAST can properly
 * handle it (see diagram below).*/
void Inou_firrtl::HandleHeadOp(const firrtl::FirrtlPB_Expression_PrimOp& op, Lnast_nid& parent_node) {
  auto idx_stmt = lnast.get_parent(parent_node);
  //I(idx_stmt.is_stmts() | idx_stmt.is_cstmts());

  /* x = head(tmp)(4) should take graph form: (like x = tmp[tmp.__bits - 1 : tmp.__bits - 4])
   *     dot                minus           minus           range            bit_sel           asg
   *  /   |   \           /   |   \       /   |   \       /   |    \        /   |   \        /     \
   *___F0 tmp __bits  ___F1 ___F0  1  ___F2 ___F0  4  ___F3 ___F1 ___F2   ___F4 tmp ___F3   x    ___F4 */

  //FIXME: Out-of-scope string issues
  auto idx_dot = lnast.add_child(idx_stmt, Lnast_node::create_dot("dot_head"));
  lnast.add_child(idx_dot, Lnast_node::create_ref("___F" + to_string(id_counter)));
  I(op.arg_size() == 1);
  ListExprInfo(op.arg(0), idx_dot);//FIXME(?)
  lnast.add_child(idx_dot, Lnast_node::create_ref("__bits"));

  auto idx_mns1 = lnast.add_child(idx_stmt, Lnast_node::create_minus("minus1_head"));
  lnast.add_child(idx_mns1, Lnast_node::create_ref("___F" + to_string(id_counter + 1)));
  lnast.add_child(idx_mns1, Lnast_node::create_ref("___F" + to_string(id_counter)));
  lnast.add_child(idx_mns1, Lnast_node::create_const("1"));

  auto idx_mnsN = lnast.add_child(idx_stmt, Lnast_node::create_minus("minusN_head"));
  lnast.add_child(idx_mnsN, Lnast_node::create_ref("___F" + to_string(id_counter + 2)));
  lnast.add_child(idx_mnsN, Lnast_node::create_ref("___F" + to_string(id_counter)));
  I(op.const__size() == 1);
  lnast.add_child(idx_mnsN, Lnast_node::create_const(op.const_(0).value()));

  auto idx_range = lnast.add_child(idx_stmt, Lnast_node::create_range("range_head"));
  lnast.add_child(idx_range, Lnast_node::create_ref("___F" + to_string(id_counter + 3)));
  lnast.add_child(idx_range, Lnast_node::create_ref("___F" + to_string(id_counter + 1)));
  lnast.add_child(idx_range, Lnast_node::create_ref("___F" + to_string(id_counter + 2)));

  auto idx_bit_sel = lnast.add_child(idx_stmt, Lnast_node::create_bit_select("bit_sel_head"));
  lnast.add_child(idx_bit_sel, Lnast_node::create_ref("___F" + to_string(id_counter + 4)));
  ListExprInfo(op.arg(0), idx_bit_sel);//FIXME(?)
  lnast.add_child(idx_bit_sel, Lnast_node::create_ref("___F" + to_string(id_counter + 3)));

  lnast.add_child(parent_node, Lnast_node::create_ref("___F" + to_string(id_counter + 4)));

  id_counter += 5;
}

/* */
void Inou_firrtl::HandleTailOp(const firrtl::FirrtlPB_Expression_PrimOp& op, Lnast_nid& parent_node) {
  auto idx_stmt = lnast.get_parent(parent_node);
  //I(idx_stmt.is_stmts() | idx_stmt.is_cstmts());

  /* x = tail(tmp)(2) should take graph form: (like x = tmp[tmp.__bits - 3 : 0])
   *     dot                minus          range            bit_sel         asg
   *  /   |   \           /   |   \      /   |    \        /   |   \      /     \
   *___F0 tmp __bits  ___F1 ___F0  3  ___F2 ___F1  0   ___F3 tmp ___F2   x    ___F3 */

  auto idx_dot = lnast.add_child(idx_stmt, Lnast_node::create_ref("dot_tail"));
  lnast.add_child(idx_dot, Lnast_node::create_ref("___F" + to_string(id_counter)));
  I(op.arg_size() == 1);
  ListExprInfo(op.arg(0), idx_dot);//FIXME(?)
  lnast.add_child(idx_dot, Lnast_node::create_ref("__bits"));

  auto idx_mns = lnast.add_child(idx_stmt, Lnast_node::create_minus("minus_tail"));
  lnast.add_child(idx_mns, Lnast_node::create_ref("___F" + to_string(id_counter + 1)));
  lnast.add_child(idx_mns, Lnast_node::create_ref("___F" + to_string(id_counter)));
  I(op.const__size() == 1);
  int const_plus_one = stoi(op.const_(0).value()) + 1;//FIXME: If the const str is a # that overflows an int, this will be problems here.
  lnast.add_child(idx_mns, Lnast_node::create_const(to_string(const_plus_one)));

  auto idx_range = lnast.add_child(idx_stmt, Lnast_node::create_range("range_tail"));
  lnast.add_child(idx_range, Lnast_node::create_ref("___F" + to_string(id_counter + 2)));
  lnast.add_child(idx_range, Lnast_node::create_ref("___F" + to_string(id_counter + 1)));
  lnast.add_child(idx_range, Lnast_node::create_const("0"));

  auto idx_bit_sel = lnast.add_child(idx_stmt, Lnast_node::create_bit_select("bit_sel_tail"));
  lnast.add_child(idx_bit_sel, Lnast_node::create_ref("___F" + to_string(id_counter + 3)));
  ListExprInfo(op.arg(0), idx_bit_sel);//FIXME(?)
  lnast.add_child(idx_bit_sel, Lnast_node::create_ref("___F" + to_string(id_counter + 2)));

  lnast.add_child(parent_node, Lnast_node::create_ref("___F" + to_string(id_counter + 3)));

  id_counter += 4;
}

//----------Ports-------------------------
/* This function is used for the following syntax rules in FIRRTL:
 * creating a wire, creating a register, instantiating an input/output (port),
 */
void Inou_firrtl::ListTypeInfo(const firrtl::FirrtlPB_Type& type, Lnast_nid& parent_node, std::string port_id) {
  switch (type.type_case()) {
    case 2: { //UInt type
      cout << "UInt[" << type.uint_type().width().value() << "]" << endl;
      if(type.uint_type().width().value() != 0) { //if BW is explicit.
        CreateBitwidthAttribute(type.uint_type().width().value(), parent_node, port_id);
      }
      break;

    } case 3: { //SInt type
      cout << "SInt[" << type.uint_type().width().value() << "]" << endl;
      if(type.sint_type().width().value() != 0) { //if BW is explicit.
        //CreateBitwidthAttribute(type.uint_type().width().value(), parent_node, port_id);
      }
      break;

    } case 4: { //Clock type
      cout << "Clock" << endl;
      break;

    } case 5: { //Bundle type
      cout << "Bundle {" << endl;
      const firrtl::FirrtlPB_Type_BundleType btype = type.bundle_type();
      for (int i = 0; i < type.bundle_type().field_size(); i++) {
        cout << "\t" << btype.field(i).id() << ": ";
        //FIXME: This will flatten out any bundles from Chisel design, like in LoFIRRTL. Do we want that?
        ListTypeInfo(btype.field(i).type(), parent_node, port_id + "." + btype.field(i).id());
      }
      cout << "\t}\n";
      break;

    } case 6: { //Vector type
      const firrtl::FirrtlPB_Type_VectorType vtype = type.vector_type();
      cout << "FIXME: Vector[" << vtype.size()  << "]" << endl;
      //FIXME: How do we want to handle Vectors for LNAST? Should I flatten?
      //ListTypeInfo(vtype.type(), parent_node, );//FIXME: Should this be parent_idx?
      break;

    } case 7: { //Fixed type
      cout << "Fixed[" << type.fixed_type().width().value() << "." << type.fixed_type().point().value() << "]" << endl;
      break;

    } case 8: { //Analog type
      cout << "Analog[" << type.uint_type().width().value() << "]" << endl;
      break;

    } case 9: { //AsyncReset type
      cout << "AsyncReset" << endl;
      break;

    } case 10: { //Reset type
      cout << "Reset" << endl;
      break;

    } default:
      cout << "Unknown port type." << endl;
      return;
  }
}

void Inou_firrtl::ListPortInfo(const firrtl::FirrtlPB_Port& port, Lnast_nid parent_node) {
  cout << "\t" << port.id() << ": " << port.direction() << ", ";
  ListTypeInfo(port.type(), parent_node, port.id());
}



//-----------Primitive Operations---------------------
void Inou_firrtl::PrintPrimOp(const firrtl::FirrtlPB_Expression_PrimOp& op, const std::string symbol, Lnast_nid& parent_node) {
  for (int i = 0; i < op.arg_size(); i++) {
    ListExprInfo(op.arg(i), parent_node);//FIXME
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
 * Many primitive ops...
 * Tail
 * Head
 * Rem
 * Shift_Left/Right -- In FIRRTL these are different than what is used in Verilog. May need other way to represent.
 * Bit_Not
 * Extract_Bits
 * Concat
 * Pad
 * Not_Equal
 * Neg
 * Convert
 * As_UInt
 * As_SInt
 * As_Clock
 * As_Fixed_Point
 * Or/And/Xor_Reduce -- Reductions use same node type as normal, but will only have 1 input "ref". Is this ok?
 * Increase_Precision
 * Decrease_Precision
 * Set_Precision
 * As_Async_Reset
 * Wrap
 * Clip
 * Squeeze
 * As_Interval
 */
void Inou_firrtl::ListPrimOpInfo(const firrtl::FirrtlPB_Expression_PrimOp& op, Lnast_nid& parent_node) {
  //FIXME: Add stuff to this eventually;
  switch(op.op()) {
    case 1: { //Op_Add
      auto idx_add = lnast.add_child(parent_node, Lnast_node::create_plus("plus"));
      PrintPrimOp(op, "+", idx_add);
      break;

    } case 2: { //Op_Sub
      auto idx_mns = lnast.add_child(parent_node, Lnast_node::create_minus("minus"));
      PrintPrimOp(op, "-", idx_mns);
      break;

    } case 3: { //Op_Tail -- take in some 'n', returns value with 'n' MSBs removed
      HandleTailOp(op, parent_node);
      break;

    } case 4: { //Op_Head -- take in some 'n', returns 'n' MSBs of variable invoked on
      HandleHeadOp(op, parent_node);
      break;

    } case 5: { //Op_Times
      auto idx_mul = lnast.add_child(parent_node, Lnast_node::create_mult("mult"));
      PrintPrimOp(op, "*", idx_mul);
      break;

    } case 6: { //Op_Divide
      auto idx_div = lnast.add_child(parent_node, Lnast_node::create_div("div"));
      PrintPrimOp(op, "/", idx_div);
      break;

    } case 7: { //Op_Rem
      PrintPrimOp(op, "%", parent_node);
      break;

    } case 8: { //Op_ShiftLeft
      //Note: used if one operand is variable, other is const #.
      //      a = x << #... bw(a) = w(x) + #
      auto idx_shl = lnast.add_child(parent_node, Lnast_node::create_shift_left("sl"));
      PrintPrimOp(op, "<<", idx_shl);
      break;

    } case 9: { //Op_Shift_Right
      //Note: used if one operand is variable, other is const #.
      //      a = x >> #... bw(a) = w(x) - #
      auto idx_shr = lnast.add_child(parent_node, Lnast_node::create_shift_right("sr"));
      PrintPrimOp(op, ">>", idx_shr);
      break;

    } case 10: { //Op_Dynamic_Shift_Left
      //Note: used if operands are both variables.
      //      a = x << y... bw(a) = w(x) + maxVal(y)
      auto idx_dshl = lnast.add_child(parent_node, Lnast_node::create_dynamic_shift_left("dsl"));
      PrintPrimOp(op, "<<", idx_dshl);
      break;

    } case 11: { //Op_Dynamic_Shift_Right
      //Note: used if operands are both variables.
      //      a = x >> y... bw(a) = w(x) - minVal(y)
      auto idx_dshr = lnast.add_child(parent_node, Lnast_node::create_dynamic_shift_right("dsr"));
      PrintPrimOp(op, ">>", idx_dshr);
      break;

    } case 12: { //Op_Bit_And
      auto idx_and = lnast.add_child(parent_node, Lnast_node::create_and("&"));
      PrintPrimOp(op, " & ", idx_and);
      break;

    } case 13: { //Op_Bit_Or
      auto idx_or = lnast.add_child(parent_node, Lnast_node::create_or("|"));
      PrintPrimOp(op, " | ", idx_or);
      break;

    } case 14: { //Op_Bit_Xor
      auto idx_xor = lnast.add_child(parent_node, Lnast_node::create_xor("^"));
      PrintPrimOp(op, " ^ ", idx_xor);
      break;

    } case 15: { //Op_Bit_Not
      //HandleNotOp(op, parent_node);
      //PrintPrimOp(op, "~", parent_node);
      break;

    } case 16: { //Op_Concat
      cout << "Cat(";
      PrintPrimOp(op, ", ", parent_node);
      cout << ");";
      break;

    } case 17: { //Op_Less
      auto idx_lt = lnast.add_child(parent_node, Lnast_node::create_lt("lt"));
      PrintPrimOp(op, "<", idx_lt);
      break;

    } case 18: { //Op_Less_Eq
      auto idx_lte = lnast.add_child(parent_node, Lnast_node::create_le("lte"));
      PrintPrimOp(op, "<=", idx_lte);
      break;

    } case 19: { //Op_Greater
      auto idx_gt = lnast.add_child(parent_node, Lnast_node::create_gt("gt"));
      PrintPrimOp(op, ">", idx_gt);
      break;

    } case 20: { //Op_Greater_Eq
      auto idx_gte = lnast.add_child(parent_node, Lnast_node::create_ge("gte"));
      PrintPrimOp(op, ">=", idx_gte);
      break;

    } case 21: { //Op_Equal
      auto idx_eq = lnast.add_child(parent_node, Lnast_node::create_eq("eq"));
      PrintPrimOp(op, "===", idx_eq);
      break;

    } case 22: { //Op_Pad ----- FIXME
      cout << "primOp: " << op.op();
      break;

    } case 23: { //Op_Not_Equal
      /*auto idx_neq = lnast.add_child(parent_node, Lnast_node::create_eq("eq2"));
      PrintPrimOp(op, "=/=", parent_node);*/
      break;

    } case 24: { //Op_Negate -- this takes a # (UInt or SInt) and returns it's negative value 10 -> -10 or -20 -> 20.
      //Note: the output's bitwidth = bitwidth of the input + 1.
      //PrintPrimOp(op, "!", parent_node);
      break;

    } case 26: { //Op_Xor_Reduce
      auto idx_xorr = lnast.add_child(parent_node, Lnast_node::create_xor("xor_red"));
      PrintPrimOp(op, ".xorR", idx_xorr);
      break;

    } case 27: { //Op_Convert ----- FIXME
      cout << "primOp: " << op.op();
      break;

    } case 28: { //Op_As_UInt
      PrintPrimOp(op, "", parent_node);
      cout << ".asUint";
      break;

    } case 29: { //Op_As_SInt
      PrintPrimOp(op, "", parent_node);
      cout << ".asSint";
      break;

    } case 30: { //Op_Extract_Bits -- this is what's used for grabbing a range of bits from a variable (i.e. foo[3:1])
      /*auto idx_extrBits = lnast.add_child(parent_node, Lnast_node(Lnast_ntype::create_bit_select(), Token(0, 0, 0, 0, "bitSelect_EB")));
      cout << "EXTRACT BITS = ";
      PrintPrimOp(op, "", idx_extrBits);*/
      //Note to self: extract bits has two parameters which always must be static int literals
      HandleExtractBitsOp(op, parent_node);
      break;

    } case 31: { //Op_As_Clock
      PrintPrimOp(op, "", parent_node);
      cout << ".asClock";
      break;

    } case 32: { //Op_As_Fixed_Point
      //FIXME: Might need to take one # from front into parens so I know precision bit count
      PrintPrimOp(op, ".asFixedPoint(", parent_node);
      cout << ")";
      break;

    } case 33: { //Op_And_Reduce
      auto idx_andr = lnast.add_child(parent_node, Lnast_node::create_and("and_red"));
      PrintPrimOp(op, ".andR", idx_andr);
      break;

    } case 34: { //Op_Or_Reduce
      auto idx_orr = lnast.add_child(parent_node, Lnast_node::create_or("or_red"));
      PrintPrimOp(op, ".orR", idx_orr);
      break;

    } case 35: { //Op_Increase_Precision
      //FIXME: Might need to take one # from front into parens so I know precision bit count
      PrintPrimOp(op, ".increasePrecision(", parent_node);
      cout << ")";
      break;

    } case 36: { //Op_Decrease_Precision
      //FIXME: Might need to take one # from front into parens so I know precision bit count
      PrintPrimOp(op, ".decreasePrecision(", parent_node);
      cout << ")";
      break;

    } case 37: { //Op_Set_Precision
      PrintPrimOp(op, ".setPrecision(", parent_node);
      cout << ")";
      break;

    } case 38: { //Op_As_Async_Reset
      PrintPrimOp(op, "", parent_node);
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
      return;
  }
}

//--------------Expressions-----------------------
/*TODO:
 * Reference (need to resolve out-of-scope error)
 * UIntLiteral (need to resolve out-of-scope error, also make sure used correct syntax: #u(bits))
 * SIntLiteral (need to resolve out-of-scope error, also make sure used correct syntax: #s(bits))
 * FixedLiteral
 */
void Inou_firrtl::ListExprInfo(const firrtl::FirrtlPB_Expression& expr, Lnast_nid& parent_node) {
  ListExprInfo(expr, parent_node, "");
}

void Inou_firrtl::ListExprInfo(const firrtl::FirrtlPB_Expression& expr, Lnast_nid& parent_node, std::string tail) {
  switch(expr.expression_case()) {
    case 1: { //Reference
      cout << expr.reference().id();
      if(tail == "") {
        lnast.add_child(parent_node, Lnast_node::create_ref(expr.reference().id()));
      } else {
        std::string full_name = expr.reference().id() + "." + tail;
        //TODO: Figure out how to do this better since right now this leads to string_view out-of-scope corruption.
        lnast.add_child(parent_node, Lnast_node::create_ref(full_name));
      }
      break;

    } case 2: { //UIntLiteral
      cout << expr.uint_literal().value().value() << ".U("
           << expr.uint_literal().width().value() << ".W)";
      std::string constVal = expr.uint_literal().value().value() + "u"
                             + std::to_string(expr.uint_literal().width().value());
      //TODO: Figure out how to do this better since right now this leads to string_view out-of-scope corruption.
      lnast.add_child(parent_node, Lnast_node::create_const(constVal));
      break;

    } case 3: { //SIntLiteral
      cout << expr.sint_literal().value().value() << ".S("
           << expr.sint_literal().width().value() << ".W)";
      std::string constVal = expr.sint_literal().value().value() + "s"
                             + std::to_string(expr.sint_literal().width().value());
      //TODO: Figure out how to do this better since right now this leads to string_view out-of-scope corruption.
      lnast.add_child(parent_node, Lnast_node::create_const(constVal));
      break;

    } case 4: { //ValidIf -- "conditionally valids", looks like: c <= validIf(valid, a), if valid then c = a else c = undefined
      cout << "ListExprInfo Error: Handling validIfs should not be handled here (see HandleValidIfAssign function)\n";
      break;

    } case 6: { //Mux -- always has form mux(sel, a, b);
      cout << "ListExprInfo Error: Handling muxes should not be handled here (see HandleMuxAssign function)\n";
      break;

    } case 7: { //SubField -- this is called when you're accessing a bundle's field (like io.var1)
      ListExprInfo(expr.sub_field().expression(), parent_node, expr.sub_field().field());
      cout << "." << expr.sub_field().field();
      break;

    } case 8: { //SubIndex -- this is used when statically accessing an element of a vector-like object
      cout << "Subindex()\n";
      auto idx_select = lnast.add_child(parent_node, Lnast_node(Lnast_ntype::create_select(), Token(0, 0, 0, 0, "selectSI")));
      ListExprInfo(expr.sub_index().expression(), idx_select);
      lnast.add_child(parent_node, Lnast_node::create_const(expr.sub_index().index().value()));
      break;

    } case 9: { //SubAccess -- this is used when dynamically accessing an element of a vector-like object
      cout << "Subaccess()\n";
      auto idx_select = lnast.add_child(parent_node, Lnast_node(Lnast_ntype::create_select(), Token(0, 0, 0, 0, "selectSA")));
      ListExprInfo(expr.sub_access().expression(), idx_select);
      ListExprInfo(expr.sub_access().index(), idx_select);
      break;

    } case 10: { //PrimOp
      if(expr.prim_op().op() == 23) { //If Not_Equal primitive operation, handle differently
        HandleNEQOp(expr.prim_op(), parent_node);
      } else {
        ListPrimOpInfo(expr.prim_op(), parent_node);
      }
      break;

    } case 11: { //FixedLiteral
      cout << expr.uint_literal().value().value() << " ("
           << expr.uint_literal().width().value() << "bits)";
      break;

    } default:
      cout << "Unknown expression type: " << expr.expression_case() << endl;
      return;
  }
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
void Inou_firrtl::ListStatementInfo(const firrtl::FirrtlPB_Statement& stmt, Lnast_nid& parent_node) {
  //Print out statement
  switch(stmt.statement_case()) {
    case 1: { //Wire
      const firrtl::FirrtlPB_Statement_Wire& wire = stmt.wire();
      cout << wire.id() << " := Wire(";
      ListTypeInfo(wire.type(), parent_node, wire.id());//FIXME: Should this be parent_idx?
      break;

    } case 2: { //Register
      cout << "Reg(";
      /*ListExprInfo(stmt.register_().clock(), parent_node);//FIXME
      cout << ", ";
      ListExprInfo(stmt.register_().reset(), parent_node);//FIXME
      cout << ", ";
      ListExprInfo(stmt.register_().init(), parent_node);//FIXME
      cout << ");\n" << endl;*/
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
      break;

    } case 6: { //Node -- nodes are simply named intermediates in a circuit
      //FIXME: This doesn't follow the tutorial which says we have to break down complex assigns.

      cout << "node " << stmt.node().id();
      cout << " = ";

      if (stmt.node().expression().expression_case() == 4) { //Expr is a validIf
        //if expr is a validIf, handle differently
        HandleValidIfAssign(stmt.node().expression(), parent_node, stmt.node().id());
      } else if (stmt.node().expression().expression_case() == 6) { //Expr is a mux
        //if expr is a mux, handle differently.
        HandleMuxAssign(stmt.node().expression(), parent_node, stmt.node().id());
      } else {
        //the expr can be handled normally.
        auto idx_asg = lnast.add_child(parent_node, Lnast_node::create_assign("asg"));
        lnast.add_child(idx_asg, Lnast_node::create_ref(stmt.node().id()));
        ListExprInfo(stmt.node().expression(), idx_asg);
      }

      cout << "\n";
      break;

    } case 7: { //When -- FIXME I handle conditions wrong here.
      auto idx_when         = lnast.add_child(parent_node, Lnast_node::create_if("when"));
      auto idx_cond         = lnast.add_child(idx_when, Lnast_node::create_cond("cond"));
      auto idx_stmts_ifTrue = lnast.add_child(idx_when, Lnast_node::create_stmts("stmts_when"));
      //FIXME: I might have to conform to cstmts model. If that's the case, add those here/in this case block.

      ListExprInfo(stmt.when().predicate(), idx_cond);

      cout << "when(";
      cout << ") {\n";
      for (int i = 0; i < stmt.when().consequent_size(); i++) {
        ListStatementInfo(stmt.when().consequent(i), idx_stmts_ifTrue);
      }
      if(stmt.when().otherwise_size() > 0) {
        cout << "} .otherwise {\n";
        auto idx_stmts_ifFalse = lnast.add_child(idx_when, Lnast_node::create_stmts("stmts_otherwise"));
        for (int j = 0; j < stmt.when().otherwise_size(); j++) {
          ListStatementInfo(stmt.when().otherwise(j), idx_stmts_ifFalse);
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
      auto idx_asg = lnast.add_child(parent_node, Lnast_node::create_assign("asg"));

      ListExprInfo(stmt.connect().location(), idx_asg);
      cout << " <= ";
      ListExprInfo(stmt.connect().expression(), idx_asg);
      cout << ";\n";
      break;

    } case 16: { //PartialConnect
      ListExprInfo(stmt.connect().location(), parent_node);//FIXME
      cout << " <- ";
      ListExprInfo(stmt.connect().expression(), parent_node);//FIXME
      cout << ";\n";
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
Lnast Inou_firrtl::ListUserModuleInfo(const firrtl::FirrtlPB_Module& module) {
  cout << "Module (user): " << module.user_module().id() << endl;
  const firrtl::FirrtlPB_Module_UserModule& user_module = module.user_module();

  //Setup basic LNAST for this module (top -> stmts).
  //FIXME: This will probably break for multi-module designs since "lnast" object isn't empty.
  I(lnast.empty());
  //Lnast lnast(module.user_module().id()); //FIXME: The LNAST is currently gets no module name.
  lnast.set_root(Lnast_node(Lnast_ntype::create_top(), Token(0, 0, 0, 0, "top")));
  auto idx_stmts = lnast.add_child(lnast.get_root(), Lnast_node::create_stmts("stmts"));

  //Iterate over I/O of the module.
  for (int i = 0; i < user_module.port_size(); i++) {
    const firrtl::FirrtlPB_Port& port = user_module.port(i);
    ListPortInfo(port, idx_stmts);//FIXME: Careful about this bc until LNAST has the internal string map the names of vars will be off.
  }

  //Iterate over statements of the module.
  for (int j = 0; j < user_module.statement_size(); j++) {
    const firrtl::FirrtlPB_Statement& stmt = user_module.statement(j);
    ListStatementInfo(stmt, idx_stmts);
    lnast.dump();
  }

  return lnast;
}

//TODO: External module handling.
Lnast Inou_firrtl::ListModuleInfo(const firrtl::FirrtlPB_Module& module) {
  if(module.module_case() == 1) {
    cout << "External module.\n";
  } else if (module.module_case() == 2) {
    return ListUserModuleInfo(module);
  } else {
    cout << "Module not set.\n";
  }
}

//Invoke function which creates LNAST tree for each module. After created, push back into vector.
void Inou_firrtl::IterateModules(const firrtl::FirrtlPB_Circuit& circuit) {
  for (int i = 0; i < circuit.module_size(); i++) {
    if (circuit.top_size() > 1) {
      cout << "ERROR: More than 1 top module?\n";
      exit(-1);//FIXME?
    }

    //For each module of the circuit, create its own LNAST.
    auto lnast_tmp = ListModuleInfo(circuit.module(i));
    lnast.dump();
    lnast_vec.push_back(lnast);
  }
}

//Iterate over every FIRRTL circuit (design), each circuit can contain multiple modules.
void Inou_firrtl::IterateCircuits(const firrtl::FirrtlPB& firrtl_input) {
  for (int i = 0; i < firrtl_input.circuit_size(); i++) {
    const firrtl::FirrtlPB_Circuit& circuit = firrtl_input.circuit(i);
    IterateModules(circuit);
  }
}

//-------- Above: LNAST Creation occurs above, Below: INOU Setup ---------------------

void setup_inou_firrtl() { Inou_firrtl::setup(); }

void Inou_firrtl::setup() {
  Eprp_method m1("inou.firrtl", "Translate FIRRTL to LNAST (in progress)", &Inou_firrtl::toLNAST);
  m1.add_label_required("files", "protobuf data files gotten from Chisel's toProto functionality");
  register_inou("firrtl", m1);
}

Inou_firrtl::Inou_firrtl(const Eprp_var &var) : Pass("firrtl", var) {
  if(var.has_label("files")) {
    //auto file_name = var.get("files");
    for (const auto &f : absl::StrSplit(files, ",")) {
      cout << "FILE: " << f << "\n";
      firrtl::FirrtlPB firrtl_input;
      fstream input(std::string(f).c_str(), ios::in | ios::binary);
      if (!firrtl_input.ParseFromIstream(&input)) {
        cerr << "Failed to parse FIRRTL from protobuf format." << endl;
        return;
      }
      id_counter = 0;
      IterateCircuits(firrtl_input);
    }
  } else {
    cout << "No file provided. This requires a file input.\n";
    return;
  }

  // Optional:  Delete all global objects allocated by libprotobuf.
  google::protobuf::ShutdownProtobufLibrary();
}

void Inou_firrtl::toLNAST(Eprp_var &var) {
  Inou_firrtl p(var);
}

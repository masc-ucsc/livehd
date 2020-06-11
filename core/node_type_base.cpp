//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "node_type_base.hpp"

#include "lgraphbase.hpp"

Node_Type *Node_Type::table[Last_invalid_Op] = {
    nullptr,
};
// std::map<std::string, Node_Type *> Node_Type::name2node;
absl::flat_hash_map<std::string, Node_Type *> Node_Type::name2node;

Node_Type::_init Node_Type::_static_initializer;

Node_Type::_init::_init() {
  // Static initialization

  Node_Type::table[Invalid_Op]           = new Node_Type_Invalid();
  Node_Type::table[Sum_Op]               = new Node_Type_Sum();
  Node_Type::table[Mult_Op]              = new Node_Type_Mult();
  Node_Type::table[Div_Op]               = new Node_Type_Div();
  Node_Type::table[Mod_Op]               = new Node_Type_Mod();
  Node_Type::table[Not_Op]               = new Node_Type_Not();
  Node_Type::table[Join_Op]              = new Node_Type_Join();
  Node_Type::table[Pick_Op]              = new Node_Type_Pick();
  Node_Type::table[And_Op]               = new Node_Type_And();
  Node_Type::table[Or_Op]                = new Node_Type_Or();
  Node_Type::table[Xor_Op]               = new Node_Type_Xor();
  Node_Type::table[LessThan_Op]          = new Node_Type_LessThan();
  Node_Type::table[GreaterThan_Op]       = new Node_Type_GreaterThan();
  Node_Type::table[LessEqualThan_Op]     = new Node_Type_LessEqualThan();
  Node_Type::table[GreaterEqualThan_Op]  = new Node_Type_GreaterEqualThan();
  Node_Type::table[Equals_Op]            = new Node_Type_Equals();
  Node_Type::table[Mux_Op]               = new Node_Type_Mux();
  Node_Type::table[LogicShiftRight_Op]   = new Node_Type_LogicShiftRight();
  Node_Type::table[ArithShiftRight_Op]   = new Node_Type_ArithShiftRight();
  Node_Type::table[DynamicShiftRight_Op] = new Node_Type_DynamicShiftRight();
  Node_Type::table[DynamicShiftLeft_Op]  = new Node_Type_DynamicShiftLeft();
  Node_Type::table[ShiftRight_Op]        = new Node_Type_ShiftRight();
  Node_Type::table[ShiftLeft_Op]         = new Node_Type_ShiftLeft();
  Node_Type::table[LUT_Op]               = new Node_Type_LUT();
  Node_Type::table[GraphIO_Op]           = new Node_Type_GraphIO();

  Node_Type::table[TupAdd_Op]     = new Node_Type_TupAdd();
  Node_Type::table[TupGet_Op]     = new Node_Type_TupGet();
  Node_Type::table[TupRef_Op]     = new Node_Type_TupRef();
  Node_Type::table[TupKey_Op]     = new Node_Type_TupKey();
  Node_Type::table[CompileErr_Op] = new Node_Type_CompileErr();
  Node_Type::table[DontCare_Op]   = new Node_Type_DontCare();

  Node_Type::table[Loop_breaker_begin] = new Node_Type_Invalid();
  Node_Type::table[SFlop_Op]           = new Node_Type_Flop();
  Node_Type::table[AFlop_Op]           = new Node_Type_AFlop();
  Node_Type::table[Latch_Op]           = new Node_Type_Latch();
  Node_Type::table[FFlop_Op]           = new Node_Type_FFlop();
  Node_Type::table[Memory_Op]          = new Node_Type_Memory();
  Node_Type::table[SubGraph_Op]        = new Node_Type_SubGraph();
  Node_Type::table[Const_Op]           = new Node_Type_Const();

  I(Invalid_Op == 0);
  for (size_t i = Invalid_Op; i <= SubGraph_Op; i++) {
    I(table[i]);
    name2node[table[i]->get_name()] = table[i];
  }
}

Node_Type_Op Node_Type::get(std::string_view opname) {
  I(is_type(opname));
  return name2node[opname]->op;
}

bool Node_Type::is_type(std::string_view opname) { return (name2node.find(opname) != name2node.end()); }

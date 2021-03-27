//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "inou_firrtl.hpp"

/* For help understanding FIRRTL/Protobuf:
 * 1) Semantics regarding FIRRTL language:
 * www2.eecs.berkeley.edu/Pubs/TechRpts/2019/EECS-2019-168.pdf
 * 2) Structure of FIRRTL Protobuf file:
 * github.com/freechipsproject/firrtl/blob/master/src/main/proto/firrtl.proto */

// void setup_inou_firrtl() { Inou_firrtl::setup(); }
static Pass_plugin sample("Inou_firrtl", Inou_firrtl::setup);

void Inou_firrtl::setup() {
  Eprp_method m1("inou.firrtl.tolnast", "Translate FIRRTL to LNAST (in progress)", &Inou_firrtl::toLNAST);
  m1.add_label_required("files", "FIRRTL-protobuf data file[s]");
  m1.add_label_optional("path", "location to store lgraph subgraph nodes", "lgdb");
  register_inou("firrtl", m1);

  Eprp_method m2("inou.firrtl.tofirrtl", "LNAST to FIRRTL", &Inou_firrtl::toFIRRTL);
  m2.add_label_optional("odir", "output directory for generated firrtl files", ".");
  register_inou("firrtl", m2);
}

Inou_firrtl::Inou_firrtl(const Eprp_var &var) : Pass("firrtl", var) {
  if (op2firsub.empty()) {
    op2firsub.emplace(firrtl::FirrtlPB_Expression_PrimOp_Op_OP_ADD, "__fir_add");
    op2firsub.emplace(firrtl::FirrtlPB_Expression_PrimOp_Op_OP_SUB, "__fir_sub");
    op2firsub.emplace(firrtl::FirrtlPB_Expression_PrimOp_Op_OP_TIMES, "__fir_mul");
    op2firsub.emplace(firrtl::FirrtlPB_Expression_PrimOp_Op_OP_DIVIDE, "__fir_div");
    op2firsub.emplace(firrtl::FirrtlPB_Expression_PrimOp_Op_OP_REM, "__fir_rem");
    op2firsub.emplace(firrtl::FirrtlPB_Expression_PrimOp_Op_OP_DYNAMIC_SHIFT_LEFT, "__fir_dshl");
    op2firsub.emplace(firrtl::FirrtlPB_Expression_PrimOp_Op_OP_DYNAMIC_SHIFT_RIGHT, "__fir_dshr");
    op2firsub.emplace(firrtl::FirrtlPB_Expression_PrimOp_Op_OP_BIT_AND, "__fir_and");
    op2firsub.emplace(firrtl::FirrtlPB_Expression_PrimOp_Op_OP_BIT_OR, "__fir_or");
    op2firsub.emplace(firrtl::FirrtlPB_Expression_PrimOp_Op_OP_BIT_XOR, "__fir_xor");
    op2firsub.emplace(firrtl::FirrtlPB_Expression_PrimOp_Op_OP_LESS, "__fir_lt");
    op2firsub.emplace(firrtl::FirrtlPB_Expression_PrimOp_Op_OP_LESS_EQ, "__fir_leq");
    op2firsub.emplace(firrtl::FirrtlPB_Expression_PrimOp_Op_OP_GREATER, "__fir_gt");
    op2firsub.emplace(firrtl::FirrtlPB_Expression_PrimOp_Op_OP_GREATER_EQ, "__fir_geq");
    op2firsub.emplace(firrtl::FirrtlPB_Expression_PrimOp_Op_OP_EQUAL, "__fir_eq");
    op2firsub.emplace(firrtl::FirrtlPB_Expression_PrimOp_Op_OP_NOT_EQUAL, "__fir_neq");
    op2firsub.emplace(firrtl::FirrtlPB_Expression_PrimOp_Op_OP_SHIFT_LEFT, "__fir_shl");
    op2firsub.emplace(firrtl::FirrtlPB_Expression_PrimOp_Op_OP_SHIFT_RIGHT, "__fir_shr");
    op2firsub.emplace(firrtl::FirrtlPB_Expression_PrimOp_Op_OP_AS_UINT, "__fir_as_uint");
    op2firsub.emplace(firrtl::FirrtlPB_Expression_PrimOp_Op_OP_AS_SINT, "__fir_as_sint");
    op2firsub.emplace(firrtl::FirrtlPB_Expression_PrimOp_Op_OP_AS_CLOCK, "__fir_as_clock");
    op2firsub.emplace(firrtl::FirrtlPB_Expression_PrimOp_Op_OP_AS_FIXED_POINT, "__fir_fixed_point");
    op2firsub.emplace(firrtl::FirrtlPB_Expression_PrimOp_Op_OP_AS_ASYNC_RESET, "__fir_as_async");
  }
}

//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "inou_firrtl.hpp"

#include "perf_tracing.hpp"

/* For help understanding FIRRTL/Protobuf:
 * 1) Semantics regarding FIRRTL language:
 * www2.eecs.berkeley.edu/Pubs/TechRpts/2019/EECS-2019-168.pdf
 * 2) Structure of FIRRTL Protobuf file:
 * github.com/freechipsproject/firrtl/blob/master/src/main/proto/firrtl.proto */

// void setup_inou_firrtl() { Inou_firrtl::setup(); }
static Pass_plugin sample("Inou_firrtl", Inou_firrtl::setup);

void Inou_firrtl::setup() {
  Eprp_method m1("inou.firrtl.tolnast", "Translate FIRRTL to LNAST (in progress)", &Inou_firrtl::to_lnast);
  m1.add_label_required("files", "FIRRTL-protobuf data file[s]");
  m1.add_label_optional("path", "location to store lgraph subgraph nodes", "lgdb");
  register_inou("firrtl", m1);

  Eprp_method m2("inou.firrtl.tofirrtl", "LNAST to FIRRTL", &Inou_firrtl::toFIRRTL);
  m2.add_label_optional("odir", "output directory for generated firrtl files", ".");
  register_inou("firrtl", m2);
}

static Sub_node *setup_firmap_library_gen(Graph_library *lib, std::string_view cell_name,
                                                  const std::vector<std::string> &inp, std::string_view out) {
  auto *sub = lib->ref_or_create_sub(cell_name);
  auto  pos = 0;
  for (const auto &i : inp) {
    sub->add_input_pin(i, pos++);
  }
  sub->add_output_pin(out, pos);
  sub->clear_loop_last();

  return sub;
}

static void setup_firmap_library(std::string_view path) {
  auto *lib = Graph_library::instance(path);
  if (lib->exists("__fir_const")) {
    return;
  }

  auto *sub_fir_const = setup_firmap_library_gen(lib, "__fir_const", {}, "Y");
  sub_fir_const->set_loop_first();

  setup_firmap_library_gen(lib, "__fir_bits", {"e1", "e2", "e3"}, "Y");

  setup_firmap_library_gen(lib, "__fir_add", {"e1", "e2"}, "Y");
  setup_firmap_library_gen(lib, "__fir_sub", {"e1", "e2"}, "Y");
  setup_firmap_library_gen(lib, "__fir_mul", {"e1", "e2"}, "Y");
  setup_firmap_library_gen(lib, "__fir_div", {"e1", "e2"}, "Y");
  setup_firmap_library_gen(lib, "__fir_rem", {"e1", "e2"}, "Y");
  setup_firmap_library_gen(lib, "__fir_lt", {"e1", "e2"}, "Y");
  setup_firmap_library_gen(lib, "__fir_leq", {"e1", "e2"}, "Y");
  setup_firmap_library_gen(lib, "__fir_gt", {"e1", "e2"}, "Y");
  setup_firmap_library_gen(lib, "__fir_geq", {"e1", "e2"}, "Y");
  setup_firmap_library_gen(lib, "__fir_eq", {"e1", "e2"}, "Y");
  setup_firmap_library_gen(lib, "__fir_neq", {"e1", "e2"}, "Y");
  setup_firmap_library_gen(lib, "__fir_pad", {"e1", "e2"}, "Y");
  setup_firmap_library_gen(lib, "__fir_shl", {"e1", "e2"}, "Y");
  setup_firmap_library_gen(lib, "__fir_shr", {"e1", "e2"}, "Y");
  setup_firmap_library_gen(lib, "__fir_dshl", {"e1", "e2"}, "Y");
  setup_firmap_library_gen(lib, "__fir_dshr", {"e1", "e2"}, "Y");
  setup_firmap_library_gen(lib, "__fir_and", {"e1", "e2"}, "Y");
  setup_firmap_library_gen(lib, "__fir_or", {"e1", "e2"}, "Y");
  setup_firmap_library_gen(lib, "__fir_xor", {"e1", "e2"}, "Y");
  setup_firmap_library_gen(lib, "__fir_cat", {"e1", "e2"}, "Y");
  setup_firmap_library_gen(lib, "__fir_head", {"e1", "e2"}, "Y");
  setup_firmap_library_gen(lib, "__fir_tail", {"e1", "e2"}, "Y");

  setup_firmap_library_gen(lib, "__fir_as_uint", {"e1"}, "Y");
  setup_firmap_library_gen(lib, "__fir_as_sint", {"e1"}, "Y");
  setup_firmap_library_gen(lib, "__fir_as_clock", {"e1"}, "Y");
  setup_firmap_library_gen(lib, "__fir_as_async", {"e1"}, "Y");
  setup_firmap_library_gen(lib, "__fir_cvt", {"e1"}, "Y");
  setup_firmap_library_gen(lib, "__fir_neg", {"e1"}, "Y");
  setup_firmap_library_gen(lib, "__fir_not", {"e1"}, "Y");
  setup_firmap_library_gen(lib, "__fir_andr", {"e1"}, "Y");
  setup_firmap_library_gen(lib, "__fir_orr", {"e1"}, "Y");
  setup_firmap_library_gen(lib, "__fir_xorr", {"e1"}, "Y");
}

Inou_firrtl::Inou_firrtl(const Eprp_var &var) : Pass("firrtl", var) {


  if (op2firsub.empty()) {
    auto  path_2 = get_path(var);
    setup_firmap_library(path_2);

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

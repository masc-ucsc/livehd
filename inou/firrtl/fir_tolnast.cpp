//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//

#include <fstream>
#include <iostream>
#include <queue>
#include <string_view>

#include "firrtl.pb.h"
#include "google/protobuf/util/time_util.h"
#include "inou_firrtl.hpp"
#include "lbench.hpp"

using google::protobuf::util::TimeUtil;

/* For help understanding FIRRTL/Protobuf:
 * 1) Semantics regarding FIRRTL language:
 * www2.eecs.berkeley.edu/Pubs/TechRpts/2019/EECS-2019-168.pdf
 * 2) Structure of FIRRTL Protobuf file:
 * github.com/freechipsproject/firrtl/blob/master/src/main/proto/firrtl.proto */

void Inou_firrtl::toLNAST(Eprp_var& var) {
  Lbench b("inou.fir_tolnast");

  Inou_firrtl p(var);

  if (var.has_label("files")) {
    auto files = var.get("files");
    for (const auto& f : absl::StrSplit(files, ",")) {
      fmt::print("FILE: {}\n", f);
      firrtl::FirrtlPB firrtl_input;
      std::fstream     input(std::string(f).c_str(), std::ios::in | std::ios::binary);
      if (!firrtl_input.ParseFromIstream(&input)) {
        Pass::error("Failed to parse FIRRTL from protobuf format: {}", f);
        return;
      }
      p.tmp_var_cnt         = 0;
      p.seq_cnt             = 0;
      p.dummy_expr_node_cnt = 0;
      // firrtl_input.PrintDebugString();
      p.IterateCircuits(var, firrtl_input, std::string(f));
    }
  } else {
    fmt::print("No file provided. This requires a file input.\n");
    return;
  }

  // Optional:  Delete all global objects allocated by libprotobuf.
  google::protobuf::ShutdownProtobufLibrary();
}

//----------------Helper Functions--------------------------
std::string_view Inou_firrtl::create_tmp_var(Lnast& lnast) {
  auto temp_var_name = lnast.add_string(absl::StrCat("___F", tmp_var_cnt));
  tmp_var_cnt++;
  return temp_var_name;
}

std::string_view Inou_firrtl::get_new_seq_name(Lnast& lnast) {
  auto seq_name = lnast.add_string(absl::StrCat("SEQ", seq_cnt));
  seq_cnt++;
  return seq_name;
}

std::string_view Inou_firrtl::create_dummy_expr_node_var(Lnast& lnast) {
  auto dummy_expr_node_name = lnast.add_string(absl::StrCat("_._", "dummy", dummy_expr_node_cnt));
  dummy_expr_node_cnt++;
  return dummy_expr_node_name;
}

/* Determine if 'term' refers to any IO/reg/etc... If it does,
 * add the appropriate symbol or (in case of a register on RHS)
 * create a DOT node to access the correct value. */
// std::string Inou_firrtl::get_full_name(Lnast& lnast, Lnast_nid& parent_node, const std::string& term, const bool is_rhs) {
std::string Inou_firrtl::get_full_name(const std::string& term, const bool is_rhs) {
  if (input_names.count(term)) {
    return absl::StrCat("$", term);
  } else if (output_names.count(term)) {
    return absl::StrCat("%", term);
  // } else if (register_names.count(term)) {
  } else if (reg2qpin.count(term)) {
    if (is_rhs) {
      return reg2qpin[term];
    } else {
      return absl::StrCat("#", term);
    }
  } else if (dangling_ports_map.contains(term)) {
    auto mem_name = dangling_ports_map[term];
    PortDirInference(term, mem_name, is_rhs);
    return term;
  } else {
    // We add _. in front of firrtl temporary names
    if (term.substr(0, 2) == "_T") {
      return absl::StrCat("_.", term);
    } else if (term.substr(0, 4) == "_GEN") {
      return absl::StrCat("_.", term);
    } else {
      return term;
    }
  }
}

void Inou_firrtl::PortDirInference(const std::string& port_name, const std::string& mem_name, const bool is_rhs) {
  if (is_rhs) {
    // Performing a read to a memory port (if type is INFER, do inference)
    if (late_assign_ports[absl::StrCat(mem_name, ".", port_name)] == INFER) {
      late_assign_ports[absl::StrCat(mem_name, ".", port_name)] = READI;
    } else if (late_assign_ports[absl::StrCat(mem_name, ".", port_name)] == WRITEI) {
      late_assign_ports[absl::StrCat(mem_name, ".", port_name)] = READ_WRITEI;
    }
  } else {
    // Performing a write to a memory port (if type is INFER, do inference)
    if (late_assign_ports[absl::StrCat(mem_name, ".", port_name)] == INFER) {
      late_assign_ports[absl::StrCat(mem_name, ".", port_name)] = WRITEI;
    } else if (late_assign_ports[absl::StrCat(mem_name, ".", port_name)] == READI) {
      late_assign_ports[absl::StrCat(mem_name, ".", port_name)] = READ_WRITEI;
    }
  }
}

/* If the bitwidth is specified, in LNAST we have to create a new variable
 *  which represents the number of bits that a variable will have. */
void Inou_firrtl::create_bitwidth_dot_node(Lnast& lnast, uint32_t bitwidth, Lnast_nid& parent_node, const std::string& _port_id,
                                           bool is_signed) {
  std::string port_id{_port_id};  // FIXME: Instead of erase, use a string_view and change lenght (much faster, not need to do mem)

  if (bitwidth <= 0) {
    /* No need to make a bitwidth node, 0 means implicit bitwidth.
     * If -1, then that's how I specify that the "port_id" is not an
     * actual wire but instead the general vector name. */
    return;
  }

  std::string_view bit_acc_name;
  if (is_signed) {
    bit_acc_name = CreateSelectsFromStr(lnast, parent_node, absl::StrCat(port_id, ".__sbits"));
  } else {
    bit_acc_name = CreateSelectsFromStr(lnast, parent_node, absl::StrCat(port_id, ".__ubits"));
  }

  auto idx_asg = lnast.add_child(parent_node, Lnast_node::create_assign());
  lnast.add_child(idx_asg, Lnast_node::create_ref(bit_acc_name));
  lnast.add_child(idx_asg, Lnast_node::create_const(lnast.add_string(std::to_string(bitwidth))));
}

uint32_t Inou_firrtl::get_bit_count(const firrtl::FirrtlPB_Type type) {
  switch (type.type_case()) {
    case firrtl::FirrtlPB_Type::kUintType: {  // UInt type
      return type.uint_type().width().value();
    }
    case firrtl::FirrtlPB_Type::kSintType: {  // SInt type
      return type.sint_type().width().value();
    }
    case firrtl::FirrtlPB_Type::kClockType: {  // Clock type
      return 1;
    }
    case firrtl::FirrtlPB_Type::kBundleType:    // Bundle type
    case firrtl::FirrtlPB_Type::kVectorType: {  // Vector type
      I(false);                                 // get_bit_count should never be called on these (no sense)
    }
    case firrtl::FirrtlPB_Type::kFixedType: {  // Fixed type
      I(false);                                // TODO: Not yet supported.
    }
    case firrtl::FirrtlPB_Type::kAnalogType: {  // Analog type
      return type.analog_type().width().value();
    }
    case firrtl::FirrtlPB_Type::kAsyncResetType: {  // AsyncReset type
      return 1;
    }
    case firrtl::FirrtlPB_Type::kResetType: {  // Reset type
      return 1;
    }
    default: Pass::error("Unknown port type.");
  }
  return -1;
}

void Inou_firrtl::init_wire_dots(Lnast& lnast, const firrtl::FirrtlPB_Type& type, const std::string& id, Lnast_nid& parent_node) {
  switch (type.type_case()) {
    case firrtl::FirrtlPB_Type::kBundleType: {  // Bundle Type
      for (int i = 0; i < type.bundle_type().field_size(); i++) {
        init_wire_dots(lnast,
                       type.bundle_type().field(i).type(),
                       absl::StrCat(id, ".", type.bundle_type().field(i).id()),
                       parent_node);
      }
      break;
    }
    case firrtl::FirrtlPB_Type::kVectorType: {  // Vector Type
      for (uint32_t i = 0; i < type.vector_type().size(); i++) {
        init_wire_dots(lnast, type.vector_type().type(), absl::StrCat(id, ".", i), parent_node);
      }
      break;
    }
    case firrtl::FirrtlPB_Type::kFixedType: {  // Fixed Point Type
      I(false);                                // TODO: LNAST does not support fixed point yet.
      break;
    }
    case firrtl::FirrtlPB_Type::kAsyncResetType: {  // AsyncReset
      auto wire_bits = get_bit_count(type);
      create_bitwidth_dot_node(lnast, wire_bits, parent_node, id, false);
      async_rst_names.insert(id);
      break;
    }
    case firrtl::FirrtlPB_Type::kSintType: {  // signed
      auto wire_bits = get_bit_count(type);
      create_bitwidth_dot_node(lnast, wire_bits, parent_node, id, true);
      break;
    }
    case firrtl::FirrtlPB_Type::kClockType: {
      break;
    }
    default: {
      // UInt Analog Reset Types 
      auto wire_bits = get_bit_count(type);
      create_bitwidth_dot_node(lnast, wire_bits, parent_node, id, false);
    }
  }
}

// When creating a register, we have to set the register's
// clock, reset, and init values using "dot" nodes in the LNAST.
// These functions create all of those when a reg is first declared. 
void Inou_firrtl::init_reg_dots(Lnast& lnast, const firrtl::FirrtlPB_Type& type, const std::string& id,
                                const firrtl::FirrtlPB_Expression& clocke, const firrtl::FirrtlPB_Expression& resete,
                                const firrtl::FirrtlPB_Expression& inite, Lnast_nid& parent_node) {
  switch (type.type_case()) {
    case firrtl::FirrtlPB_Type::kBundleType: {  // Bundle Type
      for (int i = 0; i < type.bundle_type().field_size(); i++) {
        init_reg_dots(lnast,
                      type.bundle_type().field(i).type(),
                      absl::StrCat(id, ".", type.bundle_type().field(i).id()),
                      clocke,
                      resete,
                      inite,
                      parent_node);
      }
      break;
    }
    case firrtl::FirrtlPB_Type::kVectorType: {  // Vector Type
      for (uint32_t i = 0; i < type.vector_type().size(); i++) {
        /* init_reg_dots(lnast, type.vector_type().type(), absl::StrCat(id, "[", i, "]"), clocke, resete, inite, parent_node); */
        init_reg_dots(lnast, type.vector_type().type(), absl::StrCat(id, ".", i), clocke, resete, inite, parent_node);
      }
      break;
    }
    case firrtl::FirrtlPB_Type::kFixedType: {  // Fixed Point
      I(false);                                // FIXME: Unsure how to implement
      break;
    }
    case firrtl::FirrtlPB_Type::kAsyncResetType: {  // AsyncReset
      auto reg_bits = get_bit_count(type);
      init_reg_ref_dots(lnast, id, clocke, resete, inite, reg_bits, parent_node, false);
      async_rst_names.insert(id.substr(1));
      break;
    }
    case firrtl::FirrtlPB_Type::kSintType: {
      auto reg_bits = get_bit_count(type);
      init_reg_ref_dots(lnast, id, clocke, resete, inite, reg_bits, parent_node, true);
      break;
    }
    case firrtl::FirrtlPB_Type::kClockType: {
      break;
    }

    default: {
      /* UInt Analog Reset Types*/
      auto reg_bits = get_bit_count(type);
      init_reg_ref_dots(lnast, id, clocke, resete, inite, reg_bits, parent_node, false);
    }
  }
}

// FIXME: Eventually add in other "dot" nodes when supported.
void Inou_firrtl::init_reg_ref_dots(Lnast& lnast, const std::string& _id, const firrtl::FirrtlPB_Expression& clocke,
                                    const firrtl::FirrtlPB_Expression& resete, const firrtl::FirrtlPB_Expression& inite,
                                    uint32_t bitwidth, Lnast_nid& parent_node, bool is_signed) {
  std::string id{_id};

  lnast.add_string(ReturnExprString(lnast, clocke, parent_node, true));
  lnast.add_string(ReturnExprString(lnast, resete, parent_node, true));

  // register_names.insert(id.substr(1, id.length() - 1));  // Use substr to remove "#"
  // fmt::print("DEBUG1 register name:{}\n", id.substr(1, id.length() - 1));

  // Specify __bits, if bitwidth is explicit
  if (bitwidth > 0) {
    std::string_view acc_name_bw;
    if (is_signed) {
      acc_name_bw = CreateSelectsFromStr(lnast, parent_node, absl::StrCat(id, ".__sbits"));
    } else {
      acc_name_bw = CreateSelectsFromStr(lnast, parent_node, absl::StrCat(id, ".__ubits"));
    }

    auto idx_asg = lnast.add_child(parent_node, Lnast_node::create_assign());
    lnast.add_child(idx_asg, Lnast_node::create_ref(acc_name_bw));
    lnast.add_child(idx_asg, Lnast_node::create_const(lnast.add_string(std::to_string(bitwidth))));
  } 

  // else {
  //   // if from chirrtl, the register bw is not explicit, create a dummy lhs(#x) = rhs(#x.__q_pin) for SSA to continue
  //   std::string_view acc_name;
  //   acc_name     = CreateSelectsFromStr(lnast, parent_node, absl::StrCat(id, ".__q_pin"));
  //   auto idx_asg = lnast.add_child(parent_node, Lnast_node::create_assign());
  //   lnast.add_child(idx_asg, Lnast_node::create_ref(lnast.add_string(id)));
  //   lnast.add_child(idx_asg, Lnast_node::create_ref(lnast.add_string(acc_name)));
  // }

  // handle the most common reset initialization (only hiFIRRTL has a meaningful flop reset description)
  // if (resete.expression_case() == firrtl::FirrtlPB_Expression::kReference) {
  //   reg_name2rst_init_expr.insert_or_assign(id, std::make_pair(resete, inite));
  // }


  // __reset/__init per expanded hier-tuple
  bool tied0_reset = false;
  auto resete_case = resete.expression_case();
  if (resete_case == firrtl::FirrtlPB_Expression::kUintLiteral ||
      resete_case == firrtl::FirrtlPB_Expression::kSintLiteral
      ) {
    auto acc_name = CreateSelectsFromStr(lnast, parent_node, absl::StrCat(id, ".__reset"));
    auto idx_asg = lnast.add_child(parent_node, Lnast_node::create_assign());
    lnast.add_child(idx_asg, Lnast_node::create_ref(lnast.add_string(acc_name)));
    auto str_val = resete.uint_literal().value().value();
    lnast.add_child(idx_asg, Lnast_node::create_const(lnast.add_string(str_val)));
    if (str_val == "0")
      tied0_reset = true;
  } else if (resete_case == firrtl::FirrtlPB_Expression::kReference) {
    auto acc_name = CreateSelectsFromStr(lnast, parent_node, absl::StrCat(id, ".__reset"));
    auto idx_asg = lnast.add_child(parent_node, Lnast_node::create_assign());
    lnast.add_child(idx_asg, Lnast_node::create_ref(lnast.add_string(acc_name)));
    //std::string_view ref_str = resete.reference().id();
    auto ref_str = get_full_name(resete.reference().id(), true);
    lnast.add_child(idx_asg, Lnast_node::create_ref(lnast.add_string(ref_str)));
  }


  auto inite_case = inite.expression_case();
  if (tied0_reset) {
    return;
  } else if (inite_case == firrtl::FirrtlPB_Expression::kUintLiteral ||
             inite_case == firrtl::FirrtlPB_Expression::kSintLiteral ) {
    std::string_view acc_name;
    acc_name = CreateSelectsFromStr(lnast, parent_node, absl::StrCat(id, ".__initial"));
    auto idx_asg = lnast.add_child(parent_node, Lnast_node::create_assign());
    lnast.add_child(idx_asg, Lnast_node::create_ref(lnast.add_string(acc_name)));
    auto str_val = inite.uint_literal().value().value();
    lnast.add_child(idx_asg, Lnast_node::create_const(lnast.add_string(str_val)));
  } else if (inite_case == firrtl::FirrtlPB_Expression::kReference) {
    std::string_view acc_name;
    acc_name = CreateSelectsFromStr(lnast, parent_node, absl::StrCat(id, ".__initial"));
    auto idx_asg = lnast.add_child(parent_node, Lnast_node::create_assign());
    lnast.add_child(idx_asg, Lnast_node::create_ref(lnast.add_string(acc_name)));
    // std::string_view ref_str = inite.reference().id();
    auto ref_str = get_full_name(resete.reference().id(), true);
    lnast.add_child(idx_asg, Lnast_node::create_ref(lnast.add_string(ref_str)));
  }

  // // Specify __reset_async
  // if (resete.has_reference() || resete.has_sub_field() || resete.has_sub_index() || resete.has_sub_access() || resete.has_prim_op()) {
  //   bool is_reset_async = false;
  //   if (resete.has_prim_op()) {
  //     auto op = resete.prim_op().op();
  //     if (op == firrtl::FirrtlPB_Expression_PrimOp_Op_OP_AS_ASYNC_RESET) {
  //       is_reset_async = true;
  //     }
  //   } else if (async_rst_names.contains(FlattenExpression(lnast, parent_node, resete))) {
  //     is_reset_async = true;
  //   }

  //   if (is_reset_async) {
  //     auto acc_name_sy = CreateSelectsFromStr(lnast, parent_node, absl::StrCat(id, ".__reset_async"));

  //     auto idx_asg = lnast.add_child(parent_node, Lnast_node::create_assign());
  //     lnast.add_child(idx_asg, Lnast_node::create_ref(acc_name_sy));
  //     lnast.add_child(idx_asg, Lnast_node::create_const("true"));
  //   }
  // }
}

// Set up any of the parameters related to a Memory block.
void Inou_firrtl::InitMemory(Lnast& lnast, Lnast_nid& parent_node, const firrtl::FirrtlPB_Statement_Memory& mem) {
  auto mem_name = lnast.add_string(absl::StrCat("#", mem.id()));

  // Set __size
  std::string_view depth;
  switch (mem.depth_case()) {
    case firrtl::FirrtlPB_Statement_Memory::kUintDepth: {
      depth = lnast.add_string(std::to_string(mem.uint_depth()));
      break;
    }
    case firrtl::FirrtlPB_Statement_Memory::kBigintDepth: {
      depth = lnast.add_string(ConvertBigIntToStr(mem.bigint_depth()));
      break;
    }
    default: {
      Pass::error("Unspecified/incorrectly specified memory depth");
      I(false);
    }
  }
  auto temp_var_d = create_tmp_var(lnast);
  auto idx_dot_d  = lnast.add_child(parent_node, Lnast_node::create_select());
  lnast.add_child(idx_dot_d, Lnast_node::create_ref(temp_var_d));
  lnast.add_child(idx_dot_d, Lnast_node::create_ref(mem_name));
  lnast.add_child(idx_dot_d, Lnast_node::create_const("__size"));
  auto idx_asg_d = lnast.add_child(parent_node, Lnast_node::create_assign());
  lnast.add_child(idx_asg_d, Lnast_node::create_ref(temp_var_d));
  lnast.add_child(idx_asg_d, Lnast_node::create_const(depth));

  // Acquire latency values.
  auto rd_lat = lnast.add_string(std::to_string(mem.read_latency()));
  auto wr_lat = lnast.add_string(std::to_string(mem.write_latency()));

  // Specify ReadUnderWrite policy (do writes get forwarded to reads of same addr on same cycle)
  bool fwd = false;
  if (mem.read_under_write() == firrtl::FirrtlPB_Statement_ReadUnderWrite::FirrtlPB_Statement_ReadUnderWrite_NEW) {
    fwd = true;
  }

  /* For each port, instantiate something to the effect of:
   * (id = ( __latency = r/w_lat))
   * Then pull all of those together into one single tuple.
   * This used to grab more attributes, but it's easiest
   * to just assign those later when those assigns come up. */
  std::vector<std::pair<std::string, std::string_view>> tup_ids;
  for (int i = 0; i < mem.reader_id_size(); i++) {
    auto idx_tup    = lnast.add_child(parent_node, Lnast_node::create_tuple());
    auto temp_var_t = create_tmp_var(lnast);
    lnast.add_child(idx_tup, Lnast_node::create_ref(temp_var_t));

    auto idx_asg_l = lnast.add_child(idx_tup, Lnast_node::create_assign());
    lnast.add_child(idx_asg_l, Lnast_node::create_const("__latency"));
    lnast.add_child(idx_asg_l, Lnast_node::create_const(rd_lat));

    if (fwd) {
      auto idx_asg_ruw = lnast.add_child(idx_tup, Lnast_node::create_assign());
      lnast.add_child(idx_asg_ruw, Lnast_node::create_const("__fwd"));
      lnast.add_child(idx_asg_ruw, Lnast_node::create_const("true"));
    }

    // Setup for late assigns: addr, en, clk
    late_assign_ports[absl::StrCat(mem.id(), ".", mem.reader_id(i))] = READ;
    auto str_prefix                                                  = absl::StrCat(/*"___",*/ mem.id(), "_", mem.reader_id(i));
    auto idx_asg_1                                                   = lnast.add_child(parent_node, Lnast_node::create_assign());
    lnast.add_child(idx_asg_1, Lnast_node::create_ref(lnast.add_string(absl::StrCat(str_prefix, "_addr"))));
    lnast.add_child(idx_asg_1, Lnast_node::create_const("0"));
    auto idx_asg_2 = lnast.add_child(parent_node, Lnast_node::create_assign());
    lnast.add_child(idx_asg_2, Lnast_node::create_ref(lnast.add_string(absl::StrCat(str_prefix, "_clk"))));
    lnast.add_child(idx_asg_2, Lnast_node::create_const("0"));
    auto idx_asg_3 = lnast.add_child(parent_node, Lnast_node::create_assign());
    lnast.add_child(idx_asg_3, Lnast_node::create_ref(lnast.add_string(absl::StrCat(str_prefix, "_en"))));
    lnast.add_child(idx_asg_3, Lnast_node::create_const("0"));

    tup_ids.emplace_back(mem.reader_id(i), temp_var_t);
  }

  for (int j = 0; j < mem.writer_id_size(); j++) {
    auto idx_tup    = lnast.add_child(parent_node, Lnast_node::create_tuple());
    auto temp_var_t = create_tmp_var(lnast);
    lnast.add_child(idx_tup, Lnast_node::create_ref(temp_var_t));

    auto idx_asg_l = lnast.add_child(idx_tup, Lnast_node::create_assign());
    lnast.add_child(idx_asg_l, Lnast_node::create_const("__latency"));
    lnast.add_child(idx_asg_l, Lnast_node::create_const(wr_lat));

    if (fwd) {
      auto idx_asg_ruw = lnast.add_child(idx_tup, Lnast_node::create_assign());
      lnast.add_child(idx_asg_ruw, Lnast_node::create_const("__fwd"));
      lnast.add_child(idx_asg_ruw, Lnast_node::create_const("true"));
    }

    // Setup for late assigns: addr, en, clk, data, mask
    late_assign_ports[absl::StrCat(mem.id(), ".", mem.writer_id(j))] = WRITE;
    auto str_prefix                                                  = absl::StrCat(/*"___",*/ mem.id(), "_", mem.writer_id(j));
    auto idx_asg_1                                                   = lnast.add_child(parent_node, Lnast_node::create_assign());
    lnast.add_child(idx_asg_1, Lnast_node::create_ref(lnast.add_string(absl::StrCat(str_prefix, "_addr"))));
    lnast.add_child(idx_asg_1, Lnast_node::create_const("0"));
    auto idx_asg_2 = lnast.add_child(parent_node, Lnast_node::create_assign());
    lnast.add_child(idx_asg_2, Lnast_node::create_ref(lnast.add_string(absl::StrCat(str_prefix, "_clk"))));
    lnast.add_child(idx_asg_2, Lnast_node::create_const("0"));
    auto idx_asg_3 = lnast.add_child(parent_node, Lnast_node::create_assign());
    lnast.add_child(idx_asg_3, Lnast_node::create_ref(lnast.add_string(absl::StrCat(str_prefix, "_en"))));
    lnast.add_child(idx_asg_3, Lnast_node::create_const("0"));
    auto idx_asg_4 = lnast.add_child(parent_node, Lnast_node::create_assign());
    lnast.add_child(idx_asg_4, Lnast_node::create_ref(lnast.add_string(absl::StrCat(str_prefix, "_data"))));
    lnast.add_child(idx_asg_4, Lnast_node::create_const("0"));
    auto idx_asg_5 = lnast.add_child(parent_node, Lnast_node::create_assign());
    lnast.add_child(idx_asg_5, Lnast_node::create_ref(lnast.add_string(absl::StrCat(str_prefix, "_mask"))));
    lnast.add_child(idx_asg_5, Lnast_node::create_const("0"));

    tup_ids.emplace_back(mem.writer_id(j), temp_var_t);
  }

  for (int k = 0; k < mem.readwriter_id_size(); k++) {
    auto idx_tup    = lnast.add_child(parent_node, Lnast_node::create_tuple());
    auto temp_var_t = create_tmp_var(lnast);
    lnast.add_child(idx_tup, Lnast_node::create_ref(temp_var_t));

    /* FIXME: The read and write latencies shouldn't be same for this port,
     * but we set that way since we only have one __latency attribute. */
    auto idx_asg_l = lnast.add_child(idx_tup, Lnast_node::create_assign());
    lnast.add_child(idx_asg_l, Lnast_node::create_const("__latency"));
    lnast.add_child(idx_asg_l, Lnast_node::create_const(wr_lat));

    if (fwd) {
      auto idx_asg_ruw = lnast.add_child(idx_tup, Lnast_node::create_assign());
      lnast.add_child(idx_asg_ruw, Lnast_node::create_const("__fwd"));
      lnast.add_child(idx_asg_ruw, Lnast_node::create_const("true"));
    }

    // Setup for late assigns: addr, en, clk, wdata, wmask (may have to do wmode later?)
    late_assign_ports[absl::StrCat(mem.id(), ".", mem.readwriter_id(k))] = READ_WRITE;
    auto str_prefix = absl::StrCat(/*"___",*/ mem.id(), "_", mem.readwriter_id(k));
    auto idx_asg_1  = lnast.add_child(parent_node, Lnast_node::create_assign());
    lnast.add_child(idx_asg_1, Lnast_node::create_ref(lnast.add_string(absl::StrCat(str_prefix, "_addr"))));
    lnast.add_child(idx_asg_1, Lnast_node::create_const("0"));
    auto idx_asg_2 = lnast.add_child(parent_node, Lnast_node::create_assign());
    lnast.add_child(idx_asg_2, Lnast_node::create_ref(lnast.add_string(absl::StrCat(str_prefix, "_clk"))));
    lnast.add_child(idx_asg_2, Lnast_node::create_const("0"));
    auto idx_asg_3 = lnast.add_child(parent_node, Lnast_node::create_assign());
    lnast.add_child(idx_asg_3, Lnast_node::create_ref(lnast.add_string(absl::StrCat(str_prefix, "_en"))));
    lnast.add_child(idx_asg_3, Lnast_node::create_const("0"));
    auto idx_asg_4 = lnast.add_child(parent_node, Lnast_node::create_assign());
    lnast.add_child(idx_asg_4, Lnast_node::create_ref(lnast.add_string(absl::StrCat(str_prefix, "_wdata"))));
    lnast.add_child(idx_asg_4, Lnast_node::create_const("0"));
    auto idx_asg_5 = lnast.add_child(parent_node, Lnast_node::create_assign());
    lnast.add_child(idx_asg_5, Lnast_node::create_ref(lnast.add_string(absl::StrCat(str_prefix, "_wmask"))));
    lnast.add_child(idx_asg_5, Lnast_node::create_const("0"));

    tup_ids.emplace_back(mem.readwriter_id(k), temp_var_t);
  }

  auto temp_var_lhs = create_tmp_var(lnast);
  auto idx_dotLHS   = lnast.add_child(parent_node, Lnast_node::create_select());
  lnast.add_child(idx_dotLHS, Lnast_node::create_ref(temp_var_lhs));
  lnast.add_child(idx_dotLHS, Lnast_node::create_ref(mem_name));
  lnast.add_child(idx_dotLHS, Lnast_node::create_const("__port"));

  // Create tuple node that ties port id to tuple node previously created.
  auto temp_var_t = create_tmp_var(lnast);
  auto idx_tupAll = lnast.add_child(parent_node, Lnast_node::create_tuple());
  lnast.add_child(idx_tupAll, Lnast_node::create_ref(temp_var_t));
  for (const auto& port_temp : tup_ids) {
    auto idx_asg = lnast.add_child(idx_tupAll, Lnast_node::create_assign());
    lnast.add_child(idx_asg, Lnast_node::create_ref(lnast.add_string(port_temp.first)));
    lnast.add_child(idx_asg, Lnast_node::create_ref(port_temp.second));
  }

  auto idx_asg_f = lnast.add_child(parent_node, Lnast_node::create_assign());
  lnast.add_child(idx_asg_f, Lnast_node::create_ref(temp_var_lhs));
  lnast.add_child(idx_asg_f, Lnast_node::create_ref(temp_var_t));

  // Store into mem_props_map (used by Memory Port statements)
  mem_props_map[mem.id()] = {fwd, rd_lat, wr_lat};

  // To save space in LNAST, only specify __bits info for 0th element of Mem.
  /* init_wire_dots(lnast, mem.type(), absl::StrCat(mem_name, "[0]"), parent_node); */
  init_wire_dots(lnast, mem.type(), absl::StrCat(mem_name, ".0"), parent_node);
}

/* CMemory is Chirrtl's version of FIRRTL Memory (where a cmemory statement
 * specifies memory data type and depth), but no ports. If using CMemory,
 * the Chirrtl later specifies read/write/read-write ports using MemoryPort
 * statement. Some defaults are given */
void Inou_firrtl::InitCMemory(Lnast& lnast, Lnast_nid& parent_node, const firrtl::FirrtlPB_Statement_CMemory& cmem) {
  auto cmem_name = lnast.add_string(absl::StrCat("#", cmem.id()));

  // Specify __size
  std::string_view      depth_str;
  firrtl::FirrtlPB_Type type;
  if (cmem.type_case() == firrtl::FirrtlPB_Statement_CMemory::kVectorType) {
    depth_str = lnast.add_string(std::to_string(cmem.vector_type().size()));
    type      = cmem.vector_type().type();
  } else if (cmem.type_case() == firrtl::FirrtlPB_Statement_CMemory::kTypeAndDepth) {
    depth_str = lnast.add_string(ConvertBigIntToStr(cmem.type_and_depth().depth()));
    type      = cmem.type_and_depth().data_type();
  } else {
    I(false);
  }
  auto temp_var_s = create_tmp_var(lnast);
  auto idx_dot_s  = lnast.add_child(parent_node, Lnast_node::create_select());
  lnast.add_child(idx_dot_s, Lnast_node::create_ref(temp_var_s));
  lnast.add_child(idx_dot_s, Lnast_node::create_ref(cmem_name));
  lnast.add_child(idx_dot_s, Lnast_node::create_const("__size"));
  auto idx_asg_s = lnast.add_child(parent_node, Lnast_node::create_assign());
  lnast.add_child(idx_asg_s, Lnast_node::create_ref(temp_var_s));
  lnast.add_child(idx_asg_s, Lnast_node::create_const(depth_str));

  // Specify attributes and store into mem_props_map (used by Memory Port statements).
  bool fwd = false;
  if (cmem.read_under_write() == firrtl::FirrtlPB_Statement_ReadUnderWrite::FirrtlPB_Statement_ReadUnderWrite_NEW) {
    fwd = true;
  }
  std::string_view rd_lat;
  if (cmem.sync_read()) {  // FIXME: Make sure this is correct (0 and 1 in right spot)
    rd_lat = "1";
  } else {
    rd_lat = "0";
  }
  std::string_view wr_lat = "1";

  mem_props_map[cmem.id()] = {fwd, rd_lat, wr_lat};

  // To save space in LNAST, only specify __bits info for 0th element of CMem.
  /* init_wire_dots(lnast, type, absl::StrCat(cmem_name, "[0]"), parent_node); */
  init_wire_dots(lnast, type, absl::StrCat(cmem_name, ".0"), parent_node);
}

/* Because memory and memory ports can be declared inside of
 * a nested-scope but then used outside of that scope, I have
 * to go into any nested scope and pull all of the memory out.
 * NOTE: This is a pre-traversal and looks only for memories. */
void Inou_firrtl::PreCheckForMem(Lnast& lnast, Lnast_nid& stmt_node, const firrtl::FirrtlPB_Statement& stmt) {
  switch (stmt.statement_case()) {
    case firrtl::FirrtlPB_Statement::kMemory: {
      memory_names.insert(stmt.memory().id());
      InitMemory(lnast, stmt_node, stmt.memory());
      break;
    }
    case firrtl::FirrtlPB_Statement::kCmemory: {
      memory_names.insert(stmt.cmemory().id());
      InitCMemory(lnast, stmt_node, stmt.cmemory());
      break;
    }
    case firrtl::FirrtlPB_Statement::kMemoryPort: {
      HandleMemPortPre(lnast, stmt_node, stmt.memory_port());
      break;
    }
    case firrtl::FirrtlPB_Statement::kWhen: {
      for (int i = 0; i < stmt.when().consequent_size(); i++) {
        PreCheckForMem(lnast, stmt_node, stmt.when().consequent(i));
      }
      for (int j = 0; j < stmt.when().otherwise_size(); j++) {
        PreCheckForMem(lnast, stmt_node, stmt.when().otherwise(j));
      }
      break;
    }
    default: return;
  }
}

/* This is called during the pre-traversal when looking for
 * any memory ports to pull out of nested scopes. What this
 * will do is ignore the scope the memory port is currently
 * in then instead redefine it at the highest possible scope.
 * It will tuple concat this onto its #[mem_name].__port
 * with its attributes, then any attributes that can't
 * necessarily be defined globally will be just set to 0
 * (where they will just be handled later). */
void Inou_firrtl::HandleMemPortPre(Lnast& lnast, Lnast_nid& parent_node, const firrtl::FirrtlPB_Statement_MemoryPort& mport) {
  dangling_ports_map[mport.id()] = mport.memory_id();
  auto mem_name                  = lnast.add_string(absl::StrCat("#", mport.memory_id()));
  auto port_name                 = lnast.add_string(mport.id());
  auto mem_props                 = mem_props_map[mport.memory_id()];
  auto dir_case                  = mport.direction();

  // Build tuple for new port.
  auto idx_tup = lnast.add_child(parent_node, Lnast_node::create_tuple());
  lnast.add_child(idx_tup, Lnast_node::create_ref(port_name));

  auto idx_asg_f = lnast.add_child(idx_tup, Lnast_node::create_assign());
  lnast.add_child(idx_asg_f, Lnast_node::create_const("__fwd"));
  if (std::get<0>(mem_props) == true) {
    lnast.add_child(idx_asg_f, Lnast_node::create_const("true"));
  } else {
    lnast.add_child(idx_asg_f, Lnast_node::create_const("false"));
  }

  // Specify port-specific attributes in this tuple.
  if (dir_case
      == firrtl::FirrtlPB_Statement_MemoryPort_Direction::FirrtlPB_Statement_MemoryPort_Direction_MEMORY_PORT_DIRECTION_READ) {
    // if READ port
    late_assign_ports[absl::StrCat(mport.memory_id(), ".", mport.id())] = READP;

    auto idx_asg_l = lnast.add_child(idx_tup, Lnast_node::create_assign());
    lnast.add_child(idx_asg_l, Lnast_node::create_const("__latency"));
    lnast.add_child(idx_asg_l, Lnast_node::create_const(std::get<1>(mem_props)));

  } else if (dir_case
             == firrtl::FirrtlPB_Statement_MemoryPort_Direction::
                 FirrtlPB_Statement_MemoryPort_Direction_MEMORY_PORT_DIRECTION_WRITE) {
    // if WRITE port
    late_assign_ports[absl::StrCat(mport.memory_id(), ".", mport.id())] = WRITEP;

    auto idx_asg_m = lnast.add_child(idx_tup, Lnast_node::create_assign());
    lnast.add_child(idx_asg_m, Lnast_node::create_const("__wrmask"));
    lnast.add_child(idx_asg_m, Lnast_node::create_const("0"));

    auto idx_asg_l = lnast.add_child(idx_tup, Lnast_node::create_assign());
    lnast.add_child(idx_asg_l, Lnast_node::create_const("__latency"));
    lnast.add_child(idx_asg_l, Lnast_node::create_const(std::get<2>(mem_props)));

  } else if (dir_case
             == firrtl::FirrtlPB_Statement_MemoryPort_Direction::
                 FirrtlPB_Statement_MemoryPort_Direction_MEMORY_PORT_DIRECTION_READ_WRITE) {
    // if READ-WRITE port
    late_assign_ports[absl::StrCat(mport.memory_id(), ".", mport.id())] = READ_WRITEP;

    auto idx_asg_m = lnast.add_child(idx_tup, Lnast_node::create_assign());
    lnast.add_child(idx_asg_m, Lnast_node::create_const("__wrmask"));
    lnast.add_child(idx_asg_m, Lnast_node::create_const("0"));

    auto idx_asg_l = lnast.add_child(idx_tup, Lnast_node::create_assign());
    lnast.add_child(idx_asg_l, Lnast_node::create_const("__latency"));
    // FIXME: Can only provide 1 latency, so go with write lat
    lnast.add_child(idx_asg_l, Lnast_node::create_const(std::get<2>(mem_props)));

  } else if (dir_case
             == firrtl::FirrtlPB_Statement_MemoryPort_Direction::
                 FirrtlPB_Statement_MemoryPort_Direction_MEMORY_PORT_DIRECTION_INFER) {
    // if port dir needs to be inferred
    late_assign_ports[absl::StrCat(mport.memory_id(), ".", mport.id())] = INFER;

  } else {
    I(false);
  }

  // Now that the port's tuple has been made, attach it to the memory's .__port attribute
  auto temp_var_L = create_tmp_var(lnast);
  auto idx_dotLHS = lnast.add_child(parent_node, Lnast_node::create_select());
  lnast.add_child(idx_dotLHS, Lnast_node::create_ref(temp_var_L));
  lnast.add_child(idx_dotLHS, Lnast_node::create_ref(mem_name));
  lnast.add_child(idx_dotLHS, Lnast_node::create_const("__port"));

  auto temp_var_R = create_tmp_var(lnast);
  auto idx_dotRHS = lnast.add_child(parent_node, Lnast_node::create_select());
  lnast.add_child(idx_dotRHS, Lnast_node::create_ref(temp_var_R));
  lnast.add_child(idx_dotRHS, Lnast_node::create_ref(mem_name));
  lnast.add_child(idx_dotRHS, Lnast_node::create_const("__port"));

  auto idx_concat = lnast.add_child(parent_node, Lnast_node::create_tuple_concat());
  lnast.add_child(idx_concat, Lnast_node::create_ref(temp_var_L));
  lnast.add_child(idx_concat, Lnast_node::create_ref(temp_var_R));
  lnast.add_child(idx_concat, Lnast_node::create_ref(port_name));

  /* Now, depending on what type of port is being dealt with,
   * certain assigns must occur. These are the "late assigns"
   * which are given 0 but assigned to later. */
  auto                     lhs_prefix = absl::StrCat(mport.memory_id(), "_", mport.id(), "_");
  std::vector<std::string> suffix_list;
  suffix_list.emplace_back("addr");
  suffix_list.emplace_back("clk");
  suffix_list.emplace_back("en");
  if (dir_case
      == firrtl::FirrtlPB_Statement_MemoryPort_Direction::FirrtlPB_Statement_MemoryPort_Direction_MEMORY_PORT_DIRECTION_READ) {
    // Things to set to 0 at highest scope: addr, en, clk
  } else if (dir_case
             == firrtl::FirrtlPB_Statement_MemoryPort_Direction::
                 FirrtlPB_Statement_MemoryPort_Direction_MEMORY_PORT_DIRECTION_WRITE) {
    // Things to set to 0 at highest scope: addr, en, clk, data
    suffix_list.emplace_back("data");
  } else if (dir_case
             == firrtl::FirrtlPB_Statement_MemoryPort_Direction::
                 FirrtlPB_Statement_MemoryPort_Direction_MEMORY_PORT_DIRECTION_READ_WRITE) {
    // Things to set to 0 at highest scope: addr, en, clk, data
    suffix_list.emplace_back("data");
  } else if (dir_case
             == firrtl::FirrtlPB_Statement_MemoryPort_Direction::
                 FirrtlPB_Statement_MemoryPort_Direction_MEMORY_PORT_DIRECTION_INFER) {
    /* Assume worst case (read-write) and specify everything like that.
     * If it turns out this is inferred to read, the data will just never
     * be used or set (can be DCE'd). */
    suffix_list.emplace_back("data");
  } else {
    I(false);
  }

  for (const auto& suffix : suffix_list) {
    auto idx_asg = lnast.add_child(parent_node, Lnast_node::create_assign());
    lnast.add_child(idx_asg, Lnast_node::create_ref(lnast.add_string(absl::StrCat(lhs_prefix, suffix))));
    lnast.add_child(idx_asg, Lnast_node::create_const("0"));
  }
}

/* When a memory port is seen in the normal traversal (not pre-traversal),
 * then it must have already been added to the highest scope because of
 * the pre-traversal. What needs to happen here is the setting of the
 * attributes specified on that mport line (that weren't already): addr, clk.
 * NOTE: The reason we couldn't do it at the highest scope is because
 * what is used for addr and/or clk could be variables local to this scope. */
void Inou_firrtl::HandleMemPort(Lnast& lnast, Lnast_nid& parent_node, const firrtl::FirrtlPB_Statement_MemoryPort& mport) {
  I(dangling_ports_map.contains(mport.id()));

  auto clk_str = lnast.add_string(ReturnExprString(lnast, mport.expression(), parent_node, true));
  auto adr_str = lnast.add_string(ReturnExprString(lnast, mport.memory_index(), parent_node, true));
  // auto dir_case  = mport.direction();
  // auto mem_props = mem_props_map[mport.memory_id()];
  auto lhs_prefix = absl::StrCat(mport.memory_id(), "_", mport.id(), "_");

  auto idx_asg_al = lnast.add_child(parent_node, Lnast_node::create_assign());
  lnast.add_child(idx_asg_al, Lnast_node::create_ref(lnast.add_string(absl::StrCat(lhs_prefix, "addr"))));
  AttachExprStrToNode(lnast, adr_str, idx_asg_al);

  auto idx_asg_cl = lnast.add_child(parent_node, Lnast_node::create_assign());
  lnast.add_child(idx_asg_cl, Lnast_node::create_ref(lnast.add_string(absl::StrCat(lhs_prefix, "clk"))));
  AttachExprStrToNode(lnast, clk_str, idx_asg_cl);
}

/* When a module instance is created in FIRRTL, we need to do the same
 * in LNAST. Note that the instance command in FIRRTL does not hook
 * any input or outputs. */
void Inou_firrtl::create_module_inst(Lnast& lnast, const firrtl::FirrtlPB_Statement_Instance& inst, Lnast_nid& parent_node) {
  /*            dot                       assign                      fn_call
   *      /      |        \                / \                     /     |     \
   * ___F0 itup_[inst_name] __last_value   F1 ___F0  otup_[inst_name] [mod_name]  F1 */
  auto temp_var_name  = create_tmp_var(lnast);
  auto temp_var_name2 = lnast.add_string(absl::StrCat("F", std::to_string(tmp_var_cnt)));
  tmp_var_cnt++;
  auto inst_name = inst.id();
  if (inst.id().substr(0, 2) == "_T") {
    inst_name = absl::StrCat("_.", inst_name);
  }
  auto inp_name = lnast.add_string(absl::StrCat("itup_", inst_name));
  auto out_name = lnast.add_string(absl::StrCat("otup_", inst_name));

  auto idx_dot = lnast.add_child(parent_node, Lnast_node::create_select());
  lnast.add_child(idx_dot, Lnast_node::create_ref(temp_var_name));
  lnast.add_child(idx_dot, Lnast_node::create_ref(inp_name));
  lnast.add_child(idx_dot, Lnast_node::create_const("__last_value"));

  auto idx_asg = lnast.add_child(parent_node, Lnast_node::create_assign());
  lnast.add_child(idx_asg, Lnast_node::create_ref(temp_var_name2));
  lnast.add_child(idx_asg, Lnast_node::create_ref(temp_var_name));

  auto idx_fncall = lnast.add_child(parent_node, Lnast_node::create_func_call());
  lnast.add_child(idx_fncall, Lnast_node::create_ref(out_name));
  lnast.add_child(idx_fncall, Lnast_node::create_ref(lnast.add_string(std::string("__firrtl_") + inst.module_id())));
  lnast.add_child(idx_fncall, Lnast_node::create_ref(temp_var_name2));

  /* Also, I need to record this module instance in
   * a map that maps instance name to module name. */
  inst_to_mod_map[inst.id()] = inst.module_id();

  // If any parameters exist (for ext module), specify those.
  // NOTE->hunter: We currently specify parameters the same way as inputs.
  for (const auto& param : emod_to_param_map[inst.module_id()]) {
    auto temp_var_name_p = create_tmp_var(lnast);
    auto idx_dot_p       = lnast.add_child(parent_node, Lnast_node::create_select());
    lnast.add_child(idx_dot_p, Lnast_node::create_ref(temp_var_name_p));
    lnast.add_child(idx_dot_p, Lnast_node::create_ref(inp_name));
    lnast.add_child(idx_dot_p, Lnast_node::create_ref(lnast.add_string(param.first)));

    auto idx_asg_p = lnast.add_child(parent_node, Lnast_node::create_assign());
    lnast.add_child(idx_asg_p, Lnast_node::create_ref(temp_var_name_p));
    if (isdigit(param.second[0])) {
      lnast.add_child(idx_asg_p, Lnast_node::create_const(lnast.add_string(param.second)));
    } else {
      lnast.add_child(idx_asg_p, Lnast_node::create_ref(lnast.add_string(param.second)));
    }
  }
}

/* No mux node type exists in LNAST. To support FIRRTL muxes, we instead
 * map a mux to an if-else statement whose condition is the same condition
 * as the first argument (the condition) of the mux. */
void Inou_firrtl::HandleMuxAssign(Lnast& lnast, const firrtl::FirrtlPB_Expression& expr, Lnast_nid& parent_node,
                                  const std::string& lhs) {
  I(lnast.get_data(parent_node).type.is_stmts());

  auto lhs_full    = get_full_name(lhs, false);
  auto idx_pre_asg = lnast.add_child(parent_node, Lnast_node::create_assign());
  lnast.add_child(idx_pre_asg, Lnast_node::create_ref(lnast.add_string(lhs_full)));
  lnast.add_child(idx_pre_asg, Lnast_node::create_const("0b?"));

  auto cond_str   = lnast.add_string(ReturnExprString(lnast, expr.mux().condition(), parent_node, true));
  auto idx_mux_if = lnast.add_child(parent_node, Lnast_node::create_if());
  lnast.add_child(idx_mux_if, Lnast_node::create_ref(cond_str));

  auto idx_stmt_tr = lnast.add_child(idx_mux_if, Lnast_node::create_stmts());
  auto idx_stmt_f  = lnast.add_child(idx_mux_if, Lnast_node::create_stmts());

  InitialExprAdd(lnast, expr.mux().t_value(), idx_stmt_tr, lhs);
  InitialExprAdd(lnast, expr.mux().f_value(), idx_stmt_f, lhs);
}

/* ValidIfs get detected as the RHS of an assign statement and we can't have a child of
 * an assign be an if-typed node. Thus, we have to detect ahead of time if it is a validIf
 * if we're doing an assign. If that is the case, do this instead of using ListExprType().*/
void Inou_firrtl::HandleValidIfAssign(Lnast& lnast, const firrtl::FirrtlPB_Expression& expr, Lnast_nid& parent_node,
                                      const std::string& lhs) {
  I(lnast.get_data(parent_node).type.is_stmts());

  // FIXME->sh: do the trick to declare variable with the validif value, hope this could make the validif to fit the role of "else
  // mux"
  /* auto lhs_full = get_full_name(lnast, parent_node, lhs, false); */
  /* auto idx_pre_asg = lnast.add_child(parent_node, Lnast_node::create_assign()); */
  /* lnast.add_child(idx_pre_asg, Lnast_node::create_ref(lnast.add_string(lhs_full))); */
  /* lnast.add_child(idx_pre_asg, Lnast_node::create_const("0b?")); */
  InitialExprAdd(lnast, expr.valid_if().value(), parent_node, lhs);

  auto cond_str = lnast.add_string(ReturnExprString(lnast, expr.valid_if().condition(), parent_node, true));
  auto idx_v_if = lnast.add_child(parent_node, Lnast_node::create_if());
  lnast.add_child(idx_v_if, Lnast_node::create_ref(cond_str));

  auto idx_stmt_tr = lnast.add_child(idx_v_if, Lnast_node::create_stmts());

  InitialExprAdd(lnast, expr.valid_if().value(), idx_stmt_tr, lhs);
}

// ----------------- primitive op start -------------------------------------------------------------------------------
void Inou_firrtl::HandleUnaryOp(Lnast& lnast, const firrtl::FirrtlPB_Expression_PrimOp& op, Lnast_nid& parent_node,
                                const std::string& lhs) {
  I(lnast.get_data(parent_node).type.is_stmts());
  I(op.arg_size() == 1);

  auto lhs_str = lnast.add_string(lhs);
  auto e1_str  = lnast.add_string(ReturnExprString(lnast, op.arg(0), parent_node, true));
  auto idx_not = lnast.add_child(parent_node, Lnast_node::create_func_call());  // "__fir_not"));
  lnast.add_child(idx_not, Lnast_node::create_ref(lhs_str));
  lnast.add_child(idx_not, Lnast_node::create_const("__fir_not"));
  lnast.add_child(idx_not, Lnast_node::create_ref(e1_str));
}

void Inou_firrtl::HandleAndReducOp(Lnast& lnast, const firrtl::FirrtlPB_Expression_PrimOp& op, Lnast_nid& parent_node,
                                   const std::string& lhs) {
  I(lnast.get_data(parent_node).type.is_stmts());
  I(op.arg_size() == 1);

  auto lhs_str  = lnast.add_string(lhs);
  auto e1_str   = lnast.add_string(ReturnExprString(lnast, op.arg(0), parent_node, true));
  auto idx_andr = lnast.add_child(parent_node, Lnast_node::create_func_call());  // "__fir_andr"));
  lnast.add_child(idx_andr, Lnast_node::create_ref(lhs_str));
  lnast.add_child(idx_andr, Lnast_node::create_const("__fir_andr"));
  lnast.add_child(idx_andr, Lnast_node::create_ref(e1_str));
}

void Inou_firrtl::HandleOrReducOp(Lnast& lnast, const firrtl::FirrtlPB_Expression_PrimOp& op, Lnast_nid& parent_node,
                                  const std::string& lhs) {
  I(lnast.get_data(parent_node).type.is_stmts());
  I(op.arg_size() == 1);

  auto lhs_str = lnast.add_string(lhs);
  auto e1_str  = lnast.add_string(ReturnExprString(lnast, op.arg(0), parent_node, true));
  auto idx_orr = lnast.add_child(parent_node, Lnast_node::create_func_call());  // "__fir_orr"));
  lnast.add_child(idx_orr, Lnast_node::create_ref(lhs_str));
  lnast.add_child(idx_orr, Lnast_node::create_const("__fir_orr"));
  lnast.add_child(idx_orr, Lnast_node::create_ref(e1_str));
}

void Inou_firrtl::HandleXorReducOp(Lnast& lnast, const firrtl::FirrtlPB_Expression_PrimOp& op, Lnast_nid& parent_node,
                                   const std::string& lhs) {
  I(lnast.get_data(parent_node).type.is_stmts());
  I(op.arg_size() == 1);

  auto lhs_str  = lnast.add_string(lhs);
  auto e1_str   = lnast.add_string(ReturnExprString(lnast, op.arg(0), parent_node, true));
  auto idx_xorr = lnast.add_child(parent_node, Lnast_node::create_func_call());  // "__fir_xorr"));
  lnast.add_child(idx_xorr, Lnast_node::create_ref(lhs_str));
  lnast.add_child(idx_xorr, Lnast_node::create_const("__fir_xorr"));
  lnast.add_child(idx_xorr, Lnast_node::create_ref(e1_str));
}

void Inou_firrtl::HandleNegateOp(Lnast& lnast, const firrtl::FirrtlPB_Expression_PrimOp& op, Lnast_nid& parent_node,
                                 const std::string& lhs) {
  I(lnast.get_data(parent_node).type.is_stmts());
  I(op.arg_size() == 1);

  auto lhs_str = lnast.add_string(lhs);
  auto e1_str  = lnast.add_string(ReturnExprString(lnast, op.arg(0), parent_node, true));
  auto idx_neg = lnast.add_child(parent_node, Lnast_node::create_func_call());  // "__fir_neg"));
  lnast.add_child(idx_neg, Lnast_node::create_ref(lhs_str));
  lnast.add_child(idx_neg, Lnast_node::create_const("__fir_neg"));
  lnast.add_child(idx_neg, Lnast_node::create_ref(e1_str));
}

void Inou_firrtl::HandleConvOp(Lnast& lnast, const firrtl::FirrtlPB_Expression_PrimOp& op, Lnast_nid& parent_node,
                               const std::string& lhs) {
  I(lnast.get_data(parent_node).type.is_stmts());
  I(op.arg_size() == 1);

  auto lhs_str = lnast.add_string(lhs);
  auto e1_str  = lnast.add_string(ReturnExprString(lnast, op.arg(0), parent_node, true));
  auto idx_cvt = lnast.add_child(parent_node, Lnast_node::create_func_call());  // "__fir_cvt"));
  lnast.add_child(idx_cvt, Lnast_node::create_ref(lhs_str));
  lnast.add_child(idx_cvt, Lnast_node::create_const("__fir_cvt"));
  lnast.add_child(idx_cvt, Lnast_node::create_ref(e1_str));
}

void Inou_firrtl::HandleExtractBitsOp(Lnast& lnast, const firrtl::FirrtlPB_Expression_PrimOp& op, Lnast_nid& parent_node,
                                      const std::string& lhs) {
  I(lnast.get_data(parent_node).type.is_stmts());
  I(op.arg_size() == 1 && op.const__size() == 2);

  auto lhs_str       = lnast.add_string(lhs);
  auto e1_str        = lnast.add_string(ReturnExprString(lnast, op.arg(0), parent_node, true));
  auto idx_bits_exct = lnast.add_child(parent_node, Lnast_node::create_func_call());  // "__fir_bits"));
  lnast.add_child(idx_bits_exct, Lnast_node::create_ref(lhs_str));
  lnast.add_child(idx_bits_exct, Lnast_node::create_const("__fir_bits"));
  lnast.add_child(idx_bits_exct, Lnast_node::create_ref(e1_str));
  lnast.add_child(idx_bits_exct, Lnast_node::create_const(lnast.add_string(op.const_(0).value())));
  lnast.add_child(idx_bits_exct, Lnast_node::create_const(lnast.add_string(op.const_(1).value())));
}

void Inou_firrtl::HandleHeadOp(Lnast& lnast, const firrtl::FirrtlPB_Expression_PrimOp& op, Lnast_nid& parent_node,
                               const std::string& lhs) {
  I(lnast.get_data(parent_node).type.is_stmts());
  I(op.arg_size() == 1 && op.const__size() == 1);

  auto lhs_str  = lnast.add_string(lhs);
  auto e1_str   = lnast.add_string(ReturnExprString(lnast, op.arg(0), parent_node, true));
  auto idx_head = lnast.add_child(parent_node, Lnast_node::create_func_call());  // "__fir_head"));
  lnast.add_child(idx_head, Lnast_node::create_ref(lhs_str));
  lnast.add_child(idx_head, Lnast_node::create_const("__fir_head"));
  lnast.add_child(idx_head, Lnast_node::create_ref(e1_str));
  lnast.add_child(idx_head, Lnast_node::create_const(lnast.add_string(op.const_(0).value())));
}

void Inou_firrtl::HandleTailOp(Lnast& lnast, const firrtl::FirrtlPB_Expression_PrimOp& op, Lnast_nid& parent_node,
                               const std::string& lhs) {
  I(lnast.get_data(parent_node).type.is_stmts());
  I(op.arg_size() == 1 && op.const__size() == 1);
  auto lhs_str = lnast.add_string(lhs);
  auto e1_str  = lnast.add_string(ReturnExprString(lnast, op.arg(0), parent_node, true));

  auto idx_tail = lnast.add_child(parent_node, Lnast_node::create_func_call());  // "__fir_tail"));
  lnast.add_child(idx_tail, Lnast_node::create_ref(lhs_str));
  lnast.add_child(idx_tail, Lnast_node::create_const("__fir_tail"));
  lnast.add_child(idx_tail, Lnast_node::create_ref(e1_str));
  lnast.add_child(idx_tail, Lnast_node::create_const(lnast.add_string(op.const_(0).value())));
}

void Inou_firrtl::HandleConcatOp(Lnast& lnast, const firrtl::FirrtlPB_Expression_PrimOp& op, Lnast_nid& parent_node,
                                 const std::string& lhs) {
  I(lnast.get_data(parent_node).type.is_stmts());
  I(op.arg_size() == 2);

  auto lhs_str = lnast.add_string(lhs);
  auto e1_str  = lnast.add_string(ReturnExprString(lnast, op.arg(0), parent_node, true));
  auto e2_str  = lnast.add_string(ReturnExprString(lnast, op.arg(1), parent_node, true));

  auto idx_concat = lnast.add_child(parent_node, Lnast_node::create_func_call());  // "__fir_cat"));
  lnast.add_child(idx_concat, Lnast_node::create_ref(lhs_str));
  lnast.add_child(idx_concat, Lnast_node::create_const("__fir_cat"));
  lnast.add_child(idx_concat, Lnast_node::create_ref(e1_str));
  lnast.add_child(idx_concat, Lnast_node::create_ref(e2_str));
}

void Inou_firrtl::HandlePadOp(Lnast& lnast, const firrtl::FirrtlPB_Expression_PrimOp& op, Lnast_nid& parent_node,
                              const std::string& lhs) {
  I(lnast.get_data(parent_node).type.is_stmts());
  I(op.arg_size() == 1 && op.const__size() == 1);

  auto lhs_str = lnast.add_string(lhs);
  auto e1_str  = lnast.add_string(ReturnExprString(lnast, op.arg(0), parent_node, true));

  auto idx_pad = lnast.add_child(parent_node, Lnast_node::create_func_call());  // "__fir_pad"));
  lnast.add_child(idx_pad, Lnast_node::create_ref(lhs_str));
  lnast.add_child(idx_pad, Lnast_node::create_const("__fir_pad"));
  lnast.add_child(idx_pad, Lnast_node::create_ref(e1_str));
  lnast.add_child(idx_pad, Lnast_node::create_const(lnast.add_string(op.const_(0).value())));
}

void Inou_firrtl::HandleTwoExprPrimOp(Lnast& lnast, const firrtl::FirrtlPB_Expression_PrimOp& op, Lnast_nid& parent_node,
                                      const std::string& lhs) {
  I(lnast.get_data(parent_node).type.is_stmts());
  I(op.arg_size() == 2);
  auto      e1_str = ReturnExprString(lnast, op.arg(0), parent_node, true);
  auto      e2_str = ReturnExprString(lnast, op.arg(1), parent_node, true);
  Lnast_nid idx_primop;

  auto sub_it = op2firsub.find(op.op());
  I(sub_it != op2firsub.end());

  idx_primop = lnast.add_child(parent_node, Lnast_node::create_func_call());
  lnast.add_child(idx_primop, Lnast_node::create_ref(lnast.add_string(lhs)));
  lnast.add_child(idx_primop, Lnast_node::create_const(lnast.add_string(sub_it->second)));

  AttachExprStrToNode(lnast, lnast.add_string(e1_str), idx_primop);
  AttachExprStrToNode(lnast, lnast.add_string(e2_str), idx_primop);
}

void Inou_firrtl::HandleStaticShiftOp(Lnast& lnast, const firrtl::FirrtlPB_Expression_PrimOp& op, Lnast_nid& parent_node,
                                      const std::string& lhs) {
  I(lnast.get_data(parent_node).type.is_stmts());
  I(op.arg_size() == 1 || op.const__size() == 1);

  auto lhs_str = lnast.add_string(lhs);
  auto e1_str  = lnast.add_string(ReturnExprString(lnast, op.arg(0), parent_node, true));

  Lnast_nid idx_shift;
  auto      sub_it = op2firsub.find(op.op());
  I(sub_it != op2firsub.end());

  idx_shift = lnast.add_child(parent_node, Lnast_node::create_func_call());

  lnast.add_child(idx_shift, Lnast_node::create_ref(lhs_str));
  lnast.add_child(idx_shift, Lnast_node::create_const(lnast.add_string(sub_it->second)));
  lnast.add_child(idx_shift, Lnast_node::create_ref(e1_str));
  lnast.add_child(idx_shift, Lnast_node::create_const(lnast.add_string(op.const_(0).value())));
}

void Inou_firrtl::HandleTypeConvOp(Lnast& lnast, const firrtl::FirrtlPB_Expression_PrimOp& op, Lnast_nid& parent_node,
                                   const std::string& lhs) {
  I(op.arg_size() == 1 && op.const__size() == 0);
  auto lhs_str = lnast.add_string(lhs);
  auto e1_str  = lnast.add_string(ReturnExprString(lnast, op.arg(0), parent_node, true));

  auto sub_it = op2firsub.find(op.op());
  I(sub_it != op2firsub.end());

  auto idx_conv = lnast.add_child(parent_node, Lnast_node::create_func_call());

  lnast.add_child(idx_conv, Lnast_node::create_ref(lhs_str));
  lnast.add_child(idx_conv, Lnast_node::create_const(lnast.add_string(sub_it->second)));
  lnast.add_child(idx_conv, Lnast_node::create_ref(e1_str));
}

// --------------------------------------- end of primitive op ----------------------------------------------

/* A SubField access is equivalent to accessing an element
 * of a tuple in LNAST. Just create a dot with each level
 * of hierarchy as a child of the DOT node. SubAccess/Index
 * instead rely upon a SELECT node. Sometimes, these three
 * can exist inside one another (vector of bundles) which
 * means we may need more than one DOT and/or SELECT node.
 * NOTE: This return the first child of the last DOT/SELECT node made. */

std::string_view Inou_firrtl::HandleBundVecAcc(Lnast& ln, const firrtl::FirrtlPB_Expression expr, Lnast_nid& parent_node,
                                               const bool is_rhs) {
  auto flattened_str  = FlattenExpression(ln, parent_node, expr);
  auto alter_full_str = get_full_name(flattened_str, false);  // Note: I put false here so if reg I get the "#"

  if (alter_full_str[0] == '$') {
    flattened_str = absl::StrCat("$inp_", flattened_str);
  } else if (alter_full_str[0] == '%') {
    flattened_str = absl::StrCat("%out_", flattened_str);
  } else if (alter_full_str[0] == '#') {
    if (is_rhs) {
      flattened_str = absl::StrCat("#", flattened_str, ".__q_pin");
    } else {
      flattened_str = absl::StrCat("#", flattened_str);
    }
  } else if (memory_names.count(alter_full_str.substr(0, alter_full_str.find(".")))) {
    // We found an access to some memory port.
    auto per1       = alter_full_str.find(".");
    auto per2       = alter_full_str.find(".", per1 + 1);
    auto mem_name   = absl::StrCat("#", alter_full_str.substr(0, per1));
    auto port_name  = alter_full_str.substr(per1 + 1, per2 - per1 - 1);
    auto field_name = alter_full_str.substr(per2 + 1);

    if ((field_name.substr(0, 4) == "data") && is_rhs) {
      flattened_str = absl::StrCat(mem_name, ".", port_name, ".__data");
    } else if ((field_name.substr(0, 5) == "rdata") && is_rhs) {
      flattened_str = absl::StrCat(mem_name, ".", port_name, ".__data");
    } else {
      replace(flattened_str.begin(), flattened_str.end(), '.', '_');
      return ln.add_string(flattened_str);  // absl::StrCat("___", flattened_str));
    }

  } else if (inst_to_mod_map.count(alter_full_str.substr(0, alter_full_str.find(".")))) {
    // note: instead of using alter_full_str, I use flattened_str.
    auto inst_name = flattened_str.substr(0, flattened_str.find("."));
    if (inst_name.substr(0, 2) == "_T") {
      inst_name = absl::StrCat("_.", inst_name);
    }
    auto        str_without_inst = flattened_str.substr(flattened_str.find(".") + 1);
    auto        first_field_name = str_without_inst.substr(0, str_without_inst.find("."));
    std::string str_without_inst_and_io{str_without_inst};
    auto        str_pos = str_without_inst.find('.');
    if (str_pos != std::string::npos) {
      str_without_inst_and_io = str_without_inst.substr(str_pos + 1);
    }
    auto module_name = inst_to_mod_map[inst_name];
    auto dir         = mod_to_io_dir_map[std::make_pair(module_name, str_without_inst)];

    // note: here I assume all module io will start from a hierarchy call "IO" in all firrtl module
    // FIXME->sh: wrong assumption, check failing cases in BOOM.hifir

    if (first_field_name == "io") {
      if (dir == 1) {  // PORT_DIRECTION_IN
        flattened_str = absl::StrCat("itup_", inst_name, ".inp_io.", str_without_inst_and_io);
      } else if (dir == 2) {
        flattened_str = absl::StrCat("otup_", inst_name, ".out_io.", str_without_inst_and_io);
      } else {
        Pass::error("direction unknown of {}\n", flattened_str);
        I(false);
      }
    } else {           // something like clock, reset, ... etc
      if (dir == 1) {  // PORT_DIRECTION_IN
        flattened_str = absl::StrCat("itup_", flattened_str);
      } else if (dir == 2) {
        flattened_str = absl::StrCat("otup_", flattened_str);
      } else {
        Pass::error("direction unknown of {}\n", flattened_str);
        I(false);
      }
    }
  }

  I(flattened_str.find("."));
  return CreateSelectsFromStr(ln, parent_node, flattened_str);
}

void Inou_firrtl::set_leaf_type(std::string_view subname, std::string_view full_name, size_t prev,
                                std::vector<std::pair<std::string_view, Inou_firrtl::Leaf_type>>& hier_subnames) {
  if (prev == 0) {
    hier_subnames.emplace_back(std::make_pair(subname, Leaf_type::Ref));
  } else if (full_name.at(prev - 1) == '.') {
    auto first_char = subname.at(0);
    if (isdigit(first_char) || first_char == '-' || first_char == '+') {
      hier_subnames.emplace_back(std::make_pair(subname, Leaf_type::Const_num));
    } else {
      hier_subnames.emplace_back(std::make_pair(subname, Leaf_type::Const_str));
    }
  } else if (full_name.at(prev - 1) == '[') {
    auto first_char = subname.at(0);
    if (isdigit(first_char) || first_char == '-' || first_char == '+') {
      hier_subnames.emplace_back(std::make_pair(subname, Leaf_type::Const_num));
    } else {
      hier_subnames.emplace_back(std::make_pair(subname, Leaf_type::Ref));
    }
  }
}

void Inou_firrtl::split_hier_name(std::string_view                                                  full_name,
                                  std::vector<std::pair<std::string_view, Inou_firrtl::Leaf_type>>& hier_subnames) {
  std::size_t prev = 0;
  std::size_t pos;

  while ((pos = full_name.find_first_of(".[", prev)) != std::string_view::npos) {
    if (pos > prev) {
      auto subname = full_name.substr(prev, pos - prev);
      if (subname.back() == ']') {
        subname = subname.substr(0, subname.size() - 1);  // exclude ']'
      }
      set_leaf_type(subname, full_name, prev, hier_subnames);
    }
    prev = pos + 1;
  }

  if (prev < full_name.length()) {
    auto subname = full_name.substr(prev, std::string_view::npos);
    if (subname.back() == ']') {
      subname = subname.substr(0, subname.size() - 1);  // exclude ']'
    }
    set_leaf_type(subname, full_name, prev, hier_subnames);
  }
}

/* Given a string with "."s and "["s in it, this
 * function will be able to deconstruct it into
 * DOT and SELECT nodes in an LNAST. */
std::string_view Inou_firrtl::CreateSelectsFromStr(Lnast& ln, Lnast_nid& parent_node, const std::string& full_name) {
  I((full_name.find(".") != std::string::npos));

  auto tmp_var_name = create_tmp_var(ln);
  std::vector<std::pair<std::string_view, Inou_firrtl::Leaf_type>> hier_subnames;
  hier_subnames.emplace_back(std::make_pair(tmp_var_name, Leaf_type::Ref));
  split_hier_name(full_name, hier_subnames);
  auto selc_node = ln.add_child(parent_node, Lnast_node::create_select());
  for (auto subname : hier_subnames) {
    switch (subname.second) {
      case Leaf_type::Ref: {
        ln.add_child(selc_node, Lnast_node::create_ref(ln.add_string(subname.first)));
        break;
      }
      case Leaf_type::Const_num: {
        ln.add_child(selc_node, Lnast_node::create_const(ln.add_string(subname.first)));
        break;
      }
      case Leaf_type::Const_str: {
        ln.add_child(selc_node, Lnast_node::create_const(ln.add_string(subname.first)));
        break;
      }
      default: Pass::error("Unknown port type.");
    }
  }

  return ln.get_name(ln.get_first_child(selc_node));
}

/* Given an expression that may or may
 * not have hierarchy, flatten it. */
std::string Inou_firrtl::FlattenExpression(Lnast& ln, Lnast_nid& parent_node, const firrtl::FirrtlPB_Expression& expr) {
  if (expr.has_sub_field()) {
    return absl::StrCat(FlattenExpression(ln, parent_node, expr.sub_field().expression()), ".", expr.sub_field().field());

  } else if (expr.has_sub_access()) {
    auto idx_str = ReturnExprString(ln, expr.sub_access().index(), parent_node, true);
    // DEBUGG
    return absl::StrCat(FlattenExpression(ln, parent_node, expr.sub_access().expression()), ".", idx_str);
    /* return absl::StrCat(FlattenExpression(ln, parent_node, expr.sub_access().expression()), "[", idx_str, "]"); */

  } else if (expr.has_sub_index()) {
    return absl::StrCat(FlattenExpression(ln, parent_node, expr.sub_index().expression()), ".", expr.sub_index().index().value());
    /* return absl::StrCat(FlattenExpression(ln, parent_node, expr.sub_index().expression()), "[", expr.sub_index().index().value(),
     * "]"); */

  } else if (expr.has_reference()) {
    return expr.reference().id();

  } else {
    I(false);
    return "";
  }
}

//----------Ports-------------------------
/* This function is used for the following syntax rules in FIRRTL:
 * creating a wire, creating a register, instantiating an input/output (port),
 *
 * This function returns a pair which holds the full name of a wire/output/input/register
 * and the bitwidth of it (if the bw is 0, that means the bitwidth will be inferred later.
 */
void Inou_firrtl::create_io_list(const firrtl::FirrtlPB_Type& type, uint8_t dir, const std::string& port_id,
                                 std::vector<std::tuple<std::string, uint8_t, uint32_t, bool>>& vec) {
  switch (type.type_case()) {
    case firrtl::FirrtlPB_Type::kUintType: {  // UInt type
      vec.emplace_back(port_id, dir, type.uint_type().width().value(), false);
      break;
    }
    case firrtl::FirrtlPB_Type::kSintType: {  // SInt type
      vec.emplace_back(port_id, dir, type.sint_type().width().value(), true);
      break;
    }
    case firrtl::FirrtlPB_Type::kClockType: {  // Clock type
      // vec.emplace_back(port_id, dir, 1, false);
      vec.emplace_back(port_id, dir, 0, false); // intentionally put 0 bits, LiveHD compiler will handle clock bits later
      break;
    }
    case firrtl::FirrtlPB_Type::kBundleType: {  // Bundle type
      const firrtl::FirrtlPB_Type_BundleType btype = type.bundle_type();
      for (int i = 0; i < type.bundle_type().field_size(); i++) {
        if (btype.field(i).is_flipped()) {
          uint8_t new_dir = 0;
          if (dir == 1) {  // PORT_DIRECTION_IN
            new_dir = 2;
          } else if (dir == 2) {
            new_dir = 1;
          }
          I(new_dir != 0);
          create_io_list(btype.field(i).type(), new_dir, port_id + "." + btype.field(i).id(), vec);
        } else {
          create_io_list(btype.field(i).type(), dir, port_id + "." + btype.field(i).id(), vec);
        }
      }
      break;
    }
    case firrtl::FirrtlPB_Type::kVectorType: {  // Vector type
      for (uint32_t i = 0; i < type.vector_type().size(); i++) {
        vec.emplace_back(port_id, dir, 0, false);
        // DEBUGG
        /* create_io_list(type.vector_type().type(), dir, absl::StrCat(port_id, "[", i, "]"), vec); */
        create_io_list(type.vector_type().type(), dir, absl::StrCat(port_id, ".", i), vec);
      }
      break;
    }
    case firrtl::FirrtlPB_Type::kFixedType: {  // Fixed type
      I(false);                                // FIXME: Not yet supported.
      break;
    }
    case firrtl::FirrtlPB_Type::kAnalogType: {  // Analog type
      I(false);                                 // FIXME: Not yet supported.
      break;
    }
    case firrtl::FirrtlPB_Type::kAsyncResetType: {  // AsyncReset type
      vec.emplace_back(port_id, dir, 1, false);
      async_rst_names.insert(port_id);
      break;
    }
    case firrtl::FirrtlPB_Type::kResetType: {  // Reset type
      vec.emplace_back(port_id, dir, 1, false);
      break;
    }
    default: Pass::error("Unknown port type.");
  }
}

/* This function iterates over the IO of a module and sets
 * the bitwidth + sign of each using a dot node in LNAST. */
void Inou_firrtl::ListPortInfo(Lnast& lnast, const firrtl::FirrtlPB_Port& port, Lnast_nid parent_node) {
  // Terms in port_list as follows: <name, direction, bits, sign>
  std::vector<std::tuple<std::string, uint8_t, uint32_t, bool>> port_list;
  create_io_list(port.type(), port.direction(), port.id(), port_list);

  for (auto val : port_list) {
    auto port_name = std::get<0>(val);
    auto port_dir  = std::get<1>(val);
    auto port_bits = std::get<2>(val);
    auto port_sign = std::get<3>(val);

    std::string full_port_name;
    if (port_dir == firrtl::FirrtlPB_Port_Direction::FirrtlPB_Port_Direction_PORT_DIRECTION_IN) {
      input_names.insert(port_name);
      /* if (port_name.find_first_of("[.") != std::string::npos) { */
      if (port_name.find(".") != std::string::npos) {
        full_port_name = absl::StrCat("$inp_", port_name);
      } else {
        full_port_name = absl::StrCat("$", port_name);
      }

    } else if (port_dir == firrtl::FirrtlPB_Port_Direction::FirrtlPB_Port_Direction_PORT_DIRECTION_OUT) {
      output_names.insert(port_name);
      /* if (port_name.find_first_of("[.") != std::string::npos) { */
      if (port_name.find(".") != std::string::npos) {
        full_port_name = absl::StrCat("%out_", port_name);
      } else {
        full_port_name = absl::StrCat("%", port_name);
      }
      // note: create output bits attr_set at the end of lnast to avoid firrtl_bits interference problem
      output_name2port_info.insert_or_assign(full_port_name, std::make_tuple(parent_node, port_sign, port_bits));
    } else {
      Pass::error("Found IO port {} specified with unknown direction in Protobuf message.", port_name);
    }

    if (port_bits > 0) {  // Specify __bits
      std::string_view bit_acc_name;
      if (port_sign == true) {
        bit_acc_name = CreateSelectsFromStr(lnast, parent_node, absl::StrCat(full_port_name, ".__sbits"));
      } else {
        bit_acc_name = CreateSelectsFromStr(lnast, parent_node, absl::StrCat(full_port_name, ".__ubits"));
      }
      auto idx_asg = lnast.add_child(parent_node, Lnast_node::create_assign());
      lnast.add_child(idx_asg, Lnast_node::create_ref(bit_acc_name));
      lnast.add_child(idx_asg, Lnast_node::create_const(lnast.add_string(std::to_string(port_bits))));
    }
  }
}

//-----------Primitive Operations---------------------
/* TODO:
 * Rely upon intervals:
 *   Wrap
 *   Clip
 *   Squeeze
 *   As_Interval
 * Rely upon precision/fixed point:
 *   Increase_Precision
 *   Decrease_Precision
 *   Set_Precision
 *   As_Fixed_Point
 */
void Inou_firrtl::ListPrimOpInfo(Lnast& lnast, const firrtl::FirrtlPB_Expression_PrimOp& op, Lnast_nid& parent_node,
                                 const std::string& lhs) {
  switch (op.op()) {
    case firrtl::FirrtlPB_Expression_PrimOp_Op_OP_ADD:
    case firrtl::FirrtlPB_Expression_PrimOp_Op_OP_SUB:
    case firrtl::FirrtlPB_Expression_PrimOp_Op_OP_TIMES:
    case firrtl::FirrtlPB_Expression_PrimOp_Op_OP_DIVIDE:
    case firrtl::FirrtlPB_Expression_PrimOp_Op_OP_REM:
    case firrtl::FirrtlPB_Expression_PrimOp_Op_OP_DYNAMIC_SHIFT_LEFT:
    case firrtl::FirrtlPB_Expression_PrimOp_Op_OP_DYNAMIC_SHIFT_RIGHT:
    case firrtl::FirrtlPB_Expression_PrimOp_Op_OP_BIT_AND:
    case firrtl::FirrtlPB_Expression_PrimOp_Op_OP_BIT_OR:
    case firrtl::FirrtlPB_Expression_PrimOp_Op_OP_BIT_XOR:
    case firrtl::FirrtlPB_Expression_PrimOp_Op_OP_LESS:
    case firrtl::FirrtlPB_Expression_PrimOp_Op_OP_LESS_EQ:
    case firrtl::FirrtlPB_Expression_PrimOp_Op_OP_GREATER:
    case firrtl::FirrtlPB_Expression_PrimOp_Op_OP_GREATER_EQ:
    case firrtl::FirrtlPB_Expression_PrimOp_Op_OP_EQUAL:
    case firrtl::FirrtlPB_Expression_PrimOp_Op_OP_NOT_EQUAL: {
      HandleTwoExprPrimOp(lnast, op, parent_node, lhs);
      break;
    }

    case firrtl::FirrtlPB_Expression_PrimOp_Op_OP_TAIL: {  // take in some 'n', returns value with 'n' MSBs removed
      HandleTailOp(lnast, op, parent_node, lhs);
      break;
    }

    case firrtl::FirrtlPB_Expression_PrimOp_Op_OP_HEAD: {  // take in some 'n', returns 'n' MSBs of variable invoked on
      HandleHeadOp(lnast, op, parent_node, lhs);
      break;
    }

    case firrtl::FirrtlPB_Expression_PrimOp_Op_OP_SHIFT_LEFT:
    case firrtl::FirrtlPB_Expression_PrimOp_Op_OP_SHIFT_RIGHT: {
      HandleStaticShiftOp(lnast, op, parent_node, lhs);
      break;
    }

    case firrtl::FirrtlPB_Expression_PrimOp_Op_OP_BIT_NOT: {
      HandleUnaryOp(lnast, op, parent_node, lhs);
      break;
    }

    case firrtl::FirrtlPB_Expression_PrimOp_Op_OP_CONCAT: {
      HandleConcatOp(lnast, op, parent_node, lhs);
      break;
    }

    case firrtl::FirrtlPB_Expression_PrimOp_Op_OP_PAD: {
      HandlePadOp(lnast, op, parent_node, lhs);
      break;
    }

    case firrtl::FirrtlPB_Expression_PrimOp_Op_OP_NEG: {  // takes a # (UInt or SInt) and returns it * -1
      HandleNegateOp(lnast, op, parent_node, lhs);
      break;
    }
    case firrtl::FirrtlPB_Expression_PrimOp_Op_OP_CONVERT: {
      HandleConvOp(lnast, op, parent_node, lhs);
      break;
    }
    case firrtl::FirrtlPB_Expression_PrimOp_Op_OP_EXTRACT_BITS: {
      HandleExtractBitsOp(lnast, op, parent_node, lhs);
      break;
    }

    case firrtl::FirrtlPB_Expression_PrimOp_Op_OP_AS_UINT:
    case firrtl::FirrtlPB_Expression_PrimOp_Op_OP_AS_SINT:
    case firrtl::FirrtlPB_Expression_PrimOp_Op_OP_AS_CLOCK:
    case firrtl::FirrtlPB_Expression_PrimOp_Op_OP_AS_FIXED_POINT:
    case firrtl::FirrtlPB_Expression_PrimOp_Op_OP_AS_ASYNC_RESET: {
      HandleTypeConvOp(lnast, op, parent_node, lhs);
      break;
    }
    case firrtl::FirrtlPB_Expression_PrimOp_Op_OP_XOR_REDUCE: {
      HandleXorReducOp(lnast, op, parent_node, lhs);
      break;
    }
    case firrtl::FirrtlPB_Expression_PrimOp_Op_OP_AND_REDUCE: {
      HandleAndReducOp(lnast, op, parent_node, lhs);
      break;
    }
    case firrtl::FirrtlPB_Expression_PrimOp_Op_OP_OR_REDUCE: {
      HandleOrReducOp(lnast, op, parent_node, lhs);
      break;
    }
    case firrtl::FirrtlPB_Expression_PrimOp_Op_OP_INCREASE_PRECISION:
    case firrtl::FirrtlPB_Expression_PrimOp_Op_OP_DECREASE_PRECISION:
    case firrtl::FirrtlPB_Expression_PrimOp_Op_OP_SET_PRECISION: {
      Pass::error("PrimOp: {} not yet supported (related to FloatingPoint type)", op.op());
      break;
    }
    case firrtl::FirrtlPB_Expression_PrimOp_Op_OP_WRAP:
    case firrtl::FirrtlPB_Expression_PrimOp_Op_OP_CLIP:
    case firrtl::FirrtlPB_Expression_PrimOp_Op_OP_SQUEEZE:
    case firrtl::FirrtlPB_Expression_PrimOp_Op_OP_AS_INTERVAL: {
      Pass::error("PrimOp: {} not yet supported (related to Interavls)", op.op());
      break;
    }
    default: Pass::error("Unknown PrimaryOp");
  }
}

//--------------Expressions-----------------------
/*TODO:
 * FixedLiteral */
void Inou_firrtl::InitialExprAdd(Lnast& lnast, const firrtl::FirrtlPB_Expression& rhs_expr, Lnast_nid& parent_node,
                                 const std::string& lhs_noprefixes) {
  // Note: here, parent_node is the "stmt" node above where this expression will go.
  I(lnast.get_data(parent_node).type.is_stmts());
  auto lhs_str = get_full_name(lhs_noprefixes, false);
  switch (rhs_expr.expression_case()) {
    case firrtl::FirrtlPB_Expression::kReference: {  // Reference
      std::string_view rhs_str = rhs_expr.reference().id();
      if (dangling_ports_map.contains(rhs_str)) {
        /* If its a memory port created after the memory, the name found will
         * just be the port id (i.e. "r"). This needs to be changed to
         * #mem_name.r.__data . Also set the mem_name_r_en to be 1 (since memory
         * ports I have set up to have a default enable of 0). */

        auto mem_name = dangling_ports_map[rhs_str];
        auto en_str   = lnast.add_string(absl::StrCat(mem_name, "_", rhs_str, "_en"));
        auto idx_asg  = lnast.add_child(parent_node, Lnast_node::create_assign());
        lnast.add_child(idx_asg, Lnast_node::create_ref(en_str));
        lnast.add_child(idx_asg, Lnast_node::create_const("1"));

        // If port type was INFER, then we can perform inference here.
        PortDirInference((std::string)rhs_str, mem_name, true);

        rhs_str = CreateSelectsFromStr(lnast, parent_node, absl::StrCat("#", mem_name, ".", rhs_str, ".__data"));
      } else {
        rhs_str = lnast.add_string(get_full_name(rhs_expr.reference().id(), true));
      }

      // note: hiFirrtl might have bits mismatch between lhs and rhs. To solve this problem, we use dp_assign to avoid this
      //       problem when lhs is a pre-defined circuit component (not ___tmp variable)
      Lnast_nid idx_asg;

      if (lhs_str.substr(0, 1) == "_") {
        idx_asg = lnast.add_child(parent_node, Lnast_node::create_assign());
      } else {
        idx_asg = lnast.add_child(parent_node, Lnast_node::create_dp_assign());
        // idx_asg = lnast.add_child(parent_node, Lnast_node::create_assign());
      }

      lnast.add_child(idx_asg, Lnast_node::create_ref(lnast.add_string(lhs_str)));
      lnast.add_child(idx_asg, Lnast_node::create_ref(rhs_str));
      break;
    }
    case firrtl::FirrtlPB_Expression::kUintLiteral: {  // UIntLiteral
      Lnast_nid idx_asg;
      idx_asg = lnast.add_child(parent_node, Lnast_node::create_assign());
      lnast.add_child(idx_asg, Lnast_node::create_ref(lnast.add_string(lhs_str)));
      auto str_val = rhs_expr.uint_literal().value().value();
      lnast.add_child(idx_asg, Lnast_node::create_const(lnast.add_string(str_val)));
      break;
    }
    case firrtl::FirrtlPB_Expression::kSintLiteral: {  // SIntLiteral
      Lnast_nid idx_asg;
      idx_asg = lnast.add_child(parent_node, Lnast_node::create_assign());

      lnast.add_child(idx_asg, Lnast_node::create_ref(lnast.add_string(lhs_str)));
      auto str_val = rhs_expr.sint_literal().value().value();
      lnast.add_child(idx_asg, Lnast_node::create_const(lnast.add_string(str_val)));
      break;
    }
    case firrtl::FirrtlPB_Expression::kValidIf: {  // ValidIf
      HandleValidIfAssign(lnast, rhs_expr, parent_node, lhs_str);
      break;
    }
    case firrtl::FirrtlPB_Expression::kMux: {  // Mux
      HandleMuxAssign(lnast, rhs_expr, parent_node, lhs_str);
      break;
    }
    case firrtl::FirrtlPB_Expression::kSubField: {  // SubField
      auto rhs = HandleBundVecAcc(lnast, rhs_expr, parent_node, true);

      Lnast_nid idx_asg;
      idx_asg = lnast.add_child(parent_node, Lnast_node::create_assign());

      lnast.add_child(idx_asg, Lnast_node::create_ref(lnast.add_string(lhs_str)));
      lnast.add_child(idx_asg, Lnast_node::create_ref(rhs));
      break;
    }
    case firrtl::FirrtlPB_Expression::kSubIndex: {  // SubIndex
      auto rhs = HandleBundVecAcc(lnast, rhs_expr, parent_node, true);

      Lnast_nid idx_asg;
      idx_asg = lnast.add_child(parent_node, Lnast_node::create_assign());

      lnast.add_child(idx_asg, Lnast_node::create_ref(lnast.add_string(lhs_str)));
      lnast.add_child(idx_asg, Lnast_node::create_ref(rhs));
      break;
    }
    case firrtl::FirrtlPB_Expression::kSubAccess: {  // SubAccess
      auto expr_name     = lnast.add_string(ReturnExprString(lnast, rhs_expr.sub_access().expression(), parent_node, true));
      auto index_name    = lnast.add_string(ReturnExprString(lnast, rhs_expr.sub_access().index(), parent_node, true));
      auto temp_var_name = create_tmp_var(lnast);

      auto idx_select = lnast.add_child(parent_node, Lnast_node::create_select());
      lnast.add_child(idx_select, Lnast_node::create_ref(temp_var_name));
      AttachExprStrToNode(lnast, expr_name, idx_select);
      AttachExprStrToNode(lnast, index_name, idx_select);

      auto idx_asg = lnast.add_child(parent_node, Lnast_node::create_assign());
      lnast.add_child(idx_asg, Lnast_node::create_ref(lnast.add_string(lhs_str)));
      lnast.add_child(idx_asg, Lnast_node::create_ref(temp_var_name));
      break;
    }
    case firrtl::FirrtlPB_Expression::kPrimOp: {  // PrimOp
      ListPrimOpInfo(lnast, rhs_expr.prim_op(), parent_node, lhs_str);
      break;
    }
    case firrtl::FirrtlPB_Expression::kFixedLiteral: {  // FixedLiteral
      // FIXME: FixedPointLiteral not yet supported in LNAST
      I(false);
      break;
    }
    default: Pass::error("In InitialExprAdd, found unknown expression type: {}", rhs_expr.expression_case());
  }
}

/* This function is used when I need the string to access something.
 * If it's a Reference or a Const, we format them as a string and return.
 * If it's a SubField, we have to create dot nodes and get the variable
 * name that points to the right bundle element (see HandleBundVecAcc function). */
std::string Inou_firrtl::ReturnExprString(Lnast& lnast, const firrtl::FirrtlPB_Expression& expr, Lnast_nid& parent_node,
                                          const bool is_rhs) {
  I(lnast.get_data(parent_node).type.is_stmts());

  std::string expr_string = "";
  switch (expr.expression_case()) {
    case firrtl::FirrtlPB_Expression::kReference: {  // Reference
      expr_string = get_full_name(expr.reference().id(), is_rhs);

      if (dangling_ports_map.contains(expr_string)) {
        /* If it's a memory port created after the memory, the name found will
         * just be the port id (i.e. "r"). This needs to be changed to
         * #mem_name.r.__data if on RHS. If this is on the LHS, we need to set
         * the mem_name_r_enable = 1 then mem_name_r_data = ... . */
        auto mem_name = dangling_ports_map[expr_string];

        auto en_str  = lnast.add_string(absl::StrCat(mem_name, "_", expr_string, "_en"));
        auto idx_asg = lnast.add_child(parent_node, Lnast_node::create_assign());
        lnast.add_child(idx_asg, Lnast_node::create_ref(en_str));
        lnast.add_child(idx_asg, Lnast_node::create_const("1"));

        PortDirInference(expr_string, mem_name, is_rhs);
        if (!is_rhs) {
          expr_string = lnast.add_string(absl::StrCat(mem_name, "_", expr_string, "_data"));
        } else {
          expr_string = CreateSelectsFromStr(lnast, parent_node, absl::StrCat("#", mem_name, ".", expr_string, ".__data"));
        }
      }
      break;
    }
    case firrtl::FirrtlPB_Expression::kUintLiteral: {  // UIntLiteral
      expr_string = absl::StrCat(expr.uint_literal().value().value(), "ubits", expr.uint_literal().width().value());
      break;
    }
    case firrtl::FirrtlPB_Expression::kSintLiteral: {  // SIntLiteral
      expr_string = absl::StrCat(expr.sint_literal().value().value(), "sbits", expr.uint_literal().width().value());
      /* expr_string = expr.sint_literal().value().value(); */
      break;
    }
    case firrtl::FirrtlPB_Expression::kValidIf: {  // ValidIf
      expr_string = create_tmp_var(lnast);
      HandleValidIfAssign(lnast, expr, parent_node, expr_string);
      break;
    }
    case firrtl::FirrtlPB_Expression::kMux: {  // Mux
      expr_string = create_tmp_var(lnast);
      HandleMuxAssign(lnast, expr, parent_node, expr_string);
      break;
    }
    case firrtl::FirrtlPB_Expression::kSubField:     // SubField
    case firrtl::FirrtlPB_Expression::kSubIndex:     // SubIndex
    case firrtl::FirrtlPB_Expression::kSubAccess: {  // SubAccess
      expr_string = HandleBundVecAcc(lnast, expr, parent_node, is_rhs);
      break;
    }
    case firrtl::FirrtlPB_Expression::kPrimOp: {  // PrimOp
      // This case is special. We need to create a set of nodes for it and return the lhs of that node.
      expr_string = create_tmp_var(lnast);
      ListPrimOpInfo(lnast, expr.prim_op(), parent_node, expr_string);
      break;
    }
    case firrtl::FirrtlPB_Expression::kFixedLiteral: {  // FixedLiteral
      // FIXME: Not yet supported in LNAST.
      I(false);
      break;
    }
    default: {
      // Error: I don't think this should occur if we're using Chisel's protobuf utility.
      Pass::error("provided invalid expression number: {}", expr.expression_case());
      I(false);
    }
  }
  return expr_string;
}

/* This function takes in a string and adds it into the LNAST as
 * a child of the provided "parent_node". Note: the access_str should
 * already have any $/%/#/__q_pin added to it before this is called. */
void Inou_firrtl::AttachExprStrToNode(Lnast& lnast, const std::string_view access_str, Lnast_nid& parent_node) {
  I(!lnast.get_data(parent_node).type.is_stmts());

  auto first_char = ((std::string)access_str)[0];
  if (isdigit(first_char) || first_char == '-' || first_char == '+') {
    // Represents an integer value.
    lnast.add_child(parent_node, Lnast_node::create_const(access_str));
  } else {
    // Represents a wire/variable/io.
    lnast.add_child(parent_node, Lnast_node::create_ref(access_str));
  }
}

//------------Statements----------------------
// TODO: Attach
 
void Inou_firrtl::setup_register_q_pin(Lnast &lnast, Lnast_nid &parent_node, const firrtl::FirrtlPB_Statement &stmt) {
  auto idx_attget = lnast.add_child(parent_node, Lnast_node::create_attr_get());
  auto full_register_name = lnast.add_string(absl::StrCat("#", stmt.register_().id()));
  auto tmp_var_str = create_tmp_var(lnast);
  lnast.add_child(idx_attget, Lnast_node::create_ref(tmp_var_str));
  // lnast.add_child(idx_attget, Lnast_node::create_ref(lnast.add_string(full_register_name)));
  lnast.add_child(idx_attget, Lnast_node::create_ref(full_register_name));
  std::string tmp_str = "__create_flop";
  lnast.add_child(idx_attget, Lnast_node::create_const(lnast.add_string(tmp_str)));

  auto idx_asg = lnast.add_child(parent_node, Lnast_node::create_assign());
  // lnast.add_child(idx_asg, Lnast_node::create_ref(lnast.add_string(full_register_name)));
  lnast.add_child(idx_asg, Lnast_node::create_ref(full_register_name));
  lnast.add_child(idx_asg, Lnast_node::create_ref(tmp_var_str));


  auto flop_qpin_var = lnast.add_string(absl::StrCat("_._", stmt.register_().id(), "_q"));
  auto idx_asg2 = lnast.add_child(parent_node, Lnast_node::create_assign());
  lnast.add_child(idx_asg2,    Lnast_node::create_ref(flop_qpin_var));
  lnast.add_child(idx_asg2, Lnast_node::create_ref(lnast.add_string(full_register_name)));

  reg2qpin.insert_or_assign(stmt.register_().id(), flop_qpin_var);


  // auto flop_qpin_var = lnast.add_string(absl::StrCat("___", stmt.register_().id(), "_q"));
  // auto idx_attget = lnast.add_child(parent_node, Lnast_node::create_attr_get());
  // lnast.add_child(idx_attget, Lnast_node::create_ref(flop_qpin_var));
  // lnast.add_child(idx_attget, Lnast_node::create_ref(lnast.add_string(absl::StrCat("#", stmt.register_().id()))));
  
  // std::string tmp_str = "__create_flop";
  // lnast.add_child(idx_attget, Lnast_node::create_const(lnast.add_string(tmp_str)));
  // reg2qpin.insert_or_assign(stmt.register_().id(), flop_qpin_var);
}


void Inou_firrtl::ListStatementInfo(Lnast& lnast, const firrtl::FirrtlPB_Statement& stmt, Lnast_nid& parent_node) {
  switch (stmt.statement_case()) {
    case firrtl::FirrtlPB_Statement::kWire: {  // Wire
      // FIXME->sh: 1/25/2021, we don't need to specify bits for wires as in FIRRTL 
      // we could propagate the necessary information from inputs/reg
      // init_wire_dots(lnast, stmt.wire().type(), stmt.wire().id(), parent_node); 
      break;
    }
    case firrtl::FirrtlPB_Statement::kRegister: {  // Register
      // register_names.insert(stmt.register_().id());
      // no matter it's scalar or tuple register, we only create for the top hierarchical variable,
      // the flop expansion is handled at lgraph
      setup_register_q_pin(lnast, parent_node, stmt);

      init_reg_dots(lnast,
                    stmt.register_().type(),
                    absl::StrCat("#", stmt.register_().id()),
                    stmt.register_().clock(),
                    stmt.register_().reset(),
                    stmt.register_().init(),
                    parent_node);
      break;
    }
    case firrtl::FirrtlPB_Statement::kMemory: {  // Memory
      // Handled in pre-traversal (PreCheckForMem)
      break;
    }
    case firrtl::FirrtlPB_Statement::kCmemory: {  // CMemory
      // Handled in pre-traversal (PreCheckForMem)
      break;
    }
    case firrtl::FirrtlPB_Statement::kInstance: {  // Instance -- creating an instance of a module inside another
      create_module_inst(lnast, stmt.instance(), parent_node);
      break;
    }
    case firrtl::FirrtlPB_Statement::kNode: {  // Node -- nodes are simply named intermediates in a circuit
      InitialExprAdd(lnast, stmt.node().expression(), parent_node, stmt.node().id());
      break;
    }
    case firrtl::FirrtlPB_Statement::kWhen: {  // When
      auto cond_str = lnast.add_string(ReturnExprString(lnast, stmt.when().predicate(), parent_node, true));
      auto idx_when = lnast.add_child(parent_node, Lnast_node::create_if());
      lnast.add_child(idx_when, Lnast_node::create_ref(cond_str));

      auto idx_stmts_t = lnast.add_child(idx_when, Lnast_node::create_stmts());

      for (int i = 0; i < stmt.when().consequent_size(); i++) {
        ListStatementInfo(lnast, stmt.when().consequent(i), idx_stmts_t);
      }

      if (stmt.when().otherwise_size() > 0) {
        auto idx_stmts_f = lnast.add_child(idx_when, Lnast_node::create_stmts());
        for (int j = 0; j < stmt.when().otherwise_size(); j++) {
          ListStatementInfo(lnast, stmt.when().otherwise(j), idx_stmts_f);
        }
      }
      break;
    }
    case firrtl::FirrtlPB_Statement::kStop: {  // Stop
      // Translate to: if (cond) then stop(clk, return val)
      std::string stop_cond = ReturnExprString(lnast, stmt.stop().en(), parent_node, true);
      std::string stop_clk  = ReturnExprString(lnast, stmt.stop().clk(), parent_node, true);

      auto idx_if = lnast.add_child(parent_node, Lnast_node::create_if());
      lnast.add_child(idx_if, Lnast_node::create_ref(lnast.add_string(stop_cond)));
      auto idx_stmts = lnast.add_child(idx_if, Lnast_node::create_if());

      auto idx_fncall = lnast.add_child(idx_stmts, Lnast_node::create_func_call());
      lnast.add_child(idx_fncall, Lnast_node::create_ref("null"));
      lnast.add_child(idx_fncall, Lnast_node::create_const("stop"));
      lnast.add_child(idx_fncall, Lnast_node::create_ref(lnast.add_string(stop_clk)));
      lnast.add_child(idx_fncall, Lnast_node::create_ref(lnast.add_string(std::to_string(stmt.stop().return_value()))));
      break;
    }
    case firrtl::FirrtlPB_Statement::kPrintf: {  // Printf
      // Translate to: if (cond) then printf(clk, str, vals)
      std::string              printf_cond = ReturnExprString(lnast, stmt.printf().en(), parent_node, true);
      std::string              printf_clk  = ReturnExprString(lnast, stmt.printf().clk(), parent_node, true);
      std::vector<std::string> arg_list;
      for (int i = 0; i < stmt.printf().arg_size(); i++) {
        arg_list.emplace_back(ReturnExprString(lnast, stmt.printf().arg(i), parent_node, true));
      }

      auto idx_if = lnast.add_child(parent_node, Lnast_node::create_if());
      lnast.add_child(idx_if, Lnast_node::create_ref(lnast.add_string(printf_cond)));
      auto idx_stmts = lnast.add_child(idx_if, Lnast_node::create_if());

      auto idx_fncall = lnast.add_child(idx_stmts, Lnast_node::create_func_call());
      lnast.add_child(idx_fncall, Lnast_node::create_ref("null"));
      lnast.add_child(idx_fncall, Lnast_node::create_const("printf"));
      lnast.add_child(idx_fncall, Lnast_node::create_ref(lnast.add_string(printf_clk)));
      lnast.add_child(idx_fncall, Lnast_node::create_ref(lnast.add_string(stmt.printf().value())));
      for (const auto& arg_str : arg_list) {
        lnast.add_child(idx_fncall, Lnast_node::create_ref(lnast.add_string(arg_str)));
      }
      break;
    }
    case firrtl::FirrtlPB_Statement::kSkip: {  // Skip
      // Nothing to do.
      break;
    }
    case firrtl::FirrtlPB_Statement::kConnect: {  // Connect
      auto& lhs_expr      = stmt.connect().location();
      auto& rhs_expr      = stmt.connect().expression();
      auto  lhs_expr_case = stmt.connect().location().expression_case();
      auto  rhs_expr_case = stmt.connect().expression().expression_case();
      // FIXME->sh: might need to extend the special cases to lhs_expr_case != firrtl::FirrtlPB_Expression::kNode
      bool is_lhs_tuple
          = (lhs_expr_case == firrtl::FirrtlPB_Expression::kSubField || lhs_expr_case == firrtl::FirrtlPB_Expression::kSubAccess
             || lhs_expr_case == firrtl::FirrtlPB_Expression::kSubIndex);
      bool is_rhs_mux = rhs_expr_case == firrtl::FirrtlPB_Expression::kMux;

      if (is_lhs_tuple && is_rhs_mux) {
        // (1) prepare the rhs mux first
        auto dummy_expr_node_name = create_dummy_expr_node_var(lnast);
        InitialExprAdd(lnast, rhs_expr, parent_node, (std::string)dummy_expr_node_name);

        // (2) create the lhs dot and lhs <- rhs assignment
        std::string lhs_name = ReturnExprString(lnast, lhs_expr, parent_node, false);
        auto        idx_asg  = lnast.add_child(parent_node, Lnast_node::create_assign());
        lnast.add_child(idx_asg, Lnast_node::create_ref(lnast.add_string(lhs_name)));
        lnast.add_child(idx_asg, Lnast_node::create_ref(dummy_expr_node_name));
      } else {
        std::string lhs_str = ReturnExprString(lnast, lhs_expr, parent_node, false);
        InitialExprAdd(lnast, rhs_expr, parent_node, lhs_str);
      }
      break;
    }
    case firrtl::FirrtlPB_Statement::kPartialConnect: {  // PartialConnect
      /* Note->hunter: Partial connects are treated same as full Connect. It's difficult to
       * track the exact subfields that need to be assigned. FIXME: Do as future work. */
      Pass::warn("FIRRTL partial connects are error-prone on this interface. Be careful using them.\n");
      std::string lhs_str = ReturnExprString(lnast, stmt.partial_connect().location(), parent_node, false);
      InitialExprAdd(lnast, stmt.partial_connect().expression(), parent_node, lhs_str);
      break;
    }
    case firrtl::FirrtlPB_Statement::kIsInvalid: {  // IsInvalid
      // Nothing to do.
      break;
    }
    case firrtl::FirrtlPB_Statement::kMemoryPort: {  // MemoryPort
      HandleMemPort(lnast, parent_node, stmt.memory_port());
      break;
    }
    case firrtl::FirrtlPB_Statement::kAttach: {  // Attach
      Pass::error("Attach statement not yet supported due to bidirectionality.");
      I(false);
      break;
    }
    default:
      Pass::error("Unknown statement type: {}.", stmt.statement_case());
      I(false);
      return;
  }

  // TODO: Attach source info into node creation (line #, col #).
}

/* Due to how LNAST attributes work (being compiler-based), I have to
 * do many of the assigns to memory ports at the end of the statements.
 * This will perform something like: #mymem.r.addr = ___mymem_r_addr
 * for each of the input attributes to each port. */
void Inou_firrtl::PerformLateMemAssigns(Lnast& lnast, Lnast_nid& parent_node) {
  for (const auto& mem_port : late_assign_ports) {
    auto mem_props   = mem_props_map[mem_port.first.substr(0, mem_port.first.find('.'))];
    auto port_name   = absl::StrCat("#", mem_port.first);
    auto rstr_prefix = absl::StrCat(port_name.substr(1), "_");
    replace(rstr_prefix.begin(), rstr_prefix.end(), '.', '_');
    // rstr_prefix = absl::StrCat("___", rstr_prefix);

    // Specify what attributes need to be assigned to what for this port.
    std::vector<std::pair<std::string, std::string>> assign_pairs;
    assign_pairs.emplace_back(absl::StrCat(port_name, ".__addr"), absl::StrCat(rstr_prefix, "addr"));
    assign_pairs.emplace_back(absl::StrCat(port_name, ".__clk_pin"), absl::StrCat(rstr_prefix, "clk"));

    switch (mem_port.second) {
      case READ: {
        assign_pairs.emplace_back(absl::StrCat(port_name, ".__enable"), absl::StrCat(rstr_prefix, "en"));
        break;
      }
      case WRITE: {
        assign_pairs.emplace_back(absl::StrCat(port_name, ".__enable"), absl::StrCat(rstr_prefix, "en"));
        assign_pairs.emplace_back(absl::StrCat(port_name, ".__data"), absl::StrCat(rstr_prefix, "data"));
        assign_pairs.emplace_back(absl::StrCat(port_name, ".__wrmask"), absl::StrCat(rstr_prefix, "mask"));
        break;
      }
      case READ_WRITE: {
        assign_pairs.emplace_back(absl::StrCat(port_name, ".__enable"), absl::StrCat(rstr_prefix, "en"));
        assign_pairs.emplace_back(absl::StrCat(port_name, ".__data"), absl::StrCat(rstr_prefix, "wdata"));
        assign_pairs.emplace_back(absl::StrCat(port_name, ".__wrmask"), absl::StrCat(rstr_prefix, "wmask"));
        break;
      }
      case READP: {
        assign_pairs.emplace_back(absl::StrCat(port_name, ".__enable"), absl::StrCat(rstr_prefix, "en"));
        break;
      }
      case WRITEP:
      case READ_WRITEP: {
        assign_pairs.emplace_back(absl::StrCat(port_name, ".__enable"), absl::StrCat(rstr_prefix, "en"));
        assign_pairs.emplace_back(absl::StrCat(port_name, ".__data"), absl::StrCat(rstr_prefix, "data"));
        break;
      }
      case READI: {
        assign_pairs.emplace_back(absl::StrCat(port_name, ".__enable"), absl::StrCat(rstr_prefix, "en"));
        assign_pairs.emplace_back(absl::StrCat(port_name, ".__latency"), (std::string)std::get<1>(mem_props));
        break;
      }
      case WRITEI:
      case READ_WRITEI: {
        assign_pairs.emplace_back(absl::StrCat(port_name, ".__enable"), absl::StrCat(rstr_prefix, "en"));
        assign_pairs.emplace_back(absl::StrCat(port_name, ".__data"), absl::StrCat(rstr_prefix, "data"));
        assign_pairs.emplace_back(absl::StrCat(port_name, ".__wrmask"), "0u");
        assign_pairs.emplace_back(absl::StrCat(port_name, ".__latency"), (std::string)std::get<2>(mem_props));
        break;
      }
      default: Pass::warn("Memory port {} was given INFER direction, but was never used so unable to infer.", port_name);
    }

    // Actually create all of the assigns based off what was specified.
    for (const auto& assign_pair : assign_pairs) {
      auto lhs_str = CreateSelectsFromStr(lnast, parent_node, assign_pair.first);
      auto rhs_str = lnast.add_string(assign_pair.second);
      auto idx_asg = lnast.add_child(parent_node, Lnast_node::create_assign());
      lnast.add_child(idx_asg, Lnast_node::create_ref(lhs_str));
      if (isdigit(rhs_str[0]) || (rhs_str.substr(0, 2) == "-1")) {
        lnast.add_child(idx_asg, Lnast_node::create_const(rhs_str));
      } else {
        lnast.add_child(idx_asg, Lnast_node::create_ref(rhs_str));
      }
    }
  }
}

//--------------Modules/Circuits--------------------
// Create basis of LNAST tree. Set root to "top" and have "stmts" be top's child.
void Inou_firrtl::ListUserModuleInfo(Eprp_var& var, const firrtl::FirrtlPB_Module& module, const std::string& file_name) {
  fmt::print("Module (user): {}\n", module.user_module().id());
  std::unique_ptr<Lnast> lnast = std::make_unique<Lnast>(module.user_module().id(), file_name);

  const firrtl::FirrtlPB_Module_UserModule& user_module = module.user_module();

  lnast->set_root(Lnast_node::create_top());
  auto idx_stmts = lnast->add_child(lnast->get_root(), Lnast_node::create_stmts());

  // Iterate over I/O of the module.
  for (int i = 0; i < user_module.port_size(); i++) {
    const firrtl::FirrtlPB_Port& port = user_module.port(i);
    ListPortInfo(*lnast, port, idx_stmts);
  }

  // Iterate over statements of the module.
  for (int j = 0; j < user_module.statement_size(); j++) {
    const firrtl::FirrtlPB_Statement& stmt = user_module.statement(j);
    PreCheckForMem(*lnast, idx_stmts, stmt);
    ListStatementInfo(*lnast, stmt, idx_stmts);
  }

  // deprecated the reset insertion for the highest proority. 
  // RegResetInitialization(*lnast, idx_stmts);

  PerformLateMemAssigns(*lnast, idx_stmts);
  var.add(std::move(lnast));
}

// insert the reset initialization logic, which should be the highest priority and insert in the end
// void Inou_firrtl::RegResetInitialization(Lnast& lnast, Lnast_nid& idx_stmts) {
//   for (auto const& [reg_name, expr_pair] : reg_name2rst_init_expr) {
//     auto resete = expr_pair.first;
//     auto inite  = expr_pair.second;
//     /* auto init_expr_str = lnast->add_string(ReturnExprString(*lnast, inite, idx_stmts, true)); */
//     auto reset_expr_str = lnast.add_string(ReturnExprString(lnast, resete, idx_stmts, true));
//     auto idx_mux_if     = lnast.add_child(idx_stmts, Lnast_node::create_if());
//     lnast.add_child(idx_mux_if, Lnast_node::create_ref(reset_expr_str));  // from the firrtl spec, it's always reset high
//     auto idx_stmt_tr = lnast.add_child(idx_mux_if, Lnast_node::create_stmts());
//     InitialExprAdd(lnast, inite, idx_stmt_tr, reg_name);
//   }
// }

void Inou_firrtl::ListModuleInfo(Eprp_var& var, const firrtl::FirrtlPB_Module& module, const std::string& file_name) {
  if (module.has_external_module()) {
    GrabExtModuleInfo(module.external_module());
  } else if (module.has_user_module()) {
    ListUserModuleInfo(var, module, file_name);
  } else {
    Pass::error("Module not set.");
  }
}

void Inou_firrtl::PopulateAllModsIO(Eprp_var& var, const firrtl::FirrtlPB_Circuit& circuit, const std::string& file_name) {
  for (int i = 0; i < circuit.module_size(); i++) {
    // std::vector<std::pair<std::string, uint8_t>> vec;
    if (circuit.module(i).has_external_module()) {
      /* NOTE->hunter: This is a Verilog blackbox. If we want to link it, it'd have to go through either V->LG
       * or V->LN->LG. I will create a Sub_Node in case the Verilog isn't provided. */
      auto     sub     = AddModToLibrary(var, circuit.module(i).external_module().id(), file_name);
      uint64_t inp_pos = 0;
      uint64_t out_pos = 0;
      for (int j = 0; j < circuit.module(i).external_module().port_size(); j++) {
        auto port = circuit.module(i).external_module().port(j);
        AddPortToMap(circuit.module(i).external_module().id(), port.type(), port.direction(), port.id(), sub, inp_pos, out_pos);
      }
      continue;
    } else if (circuit.module(i).has_user_module()) {
      auto     sub     = AddModToLibrary(var, circuit.module(i).user_module().id(), file_name);
      uint64_t inp_pos = 0;
      uint64_t out_pos = 0;
      for (int j = 0; j < circuit.module(i).user_module().port_size(); j++) {
        auto port = circuit.module(i).user_module().port(j);
        AddPortToMap(circuit.module(i).user_module().id(), port.type(), port.direction(), port.id(), sub, inp_pos, out_pos);
      }
    } else {
      Pass::error("Module not set.");
    }
  }
}

Sub_node Inou_firrtl::AddModToLibrary(Eprp_var& var, const std::string& mod_name, const std::string& file_name) {
  std::string fpath;
  if (var.has_label("path")) {
    fpath = var.get("path");
  } else {
    fpath = "lgdb";
  }

  auto* library = Graph_library::instance(fpath);
  auto& sub     = library->reset_sub(mod_name, file_name);
  // FIXME->sh: also add %/$ ?
  return sub;
}

/* Used to populate Sub_Nodes so that when Lgraphs are constructed,
 * all the Lgraphs will be able to populate regardless of order. */
void Inou_firrtl::AddPortToSub(Sub_node& sub, uint64_t& inp_pos, uint64_t& out_pos, const std::string& port_id,
                               const uint8_t& dir) {
  if (dir == 1) {                // PORT_DIRECTION_IN
    sub.add_input_pin(port_id);  //, inp_pos);
    inp_pos++;
  } else {
    sub.add_output_pin(port_id);  //, out_pos);
    out_pos++;
  }
}

void Inou_firrtl::AddPortToMap(const std::string& mod_id, const firrtl::FirrtlPB_Type& type, uint8_t dir,
                               const std::string& port_id, Sub_node& sub, uint64_t& inp_pos, uint64_t& out_pos) {
  switch (type.type_case()) {
    case firrtl::FirrtlPB_Type::kUintType: {  // UInt type
      AddPortToSub(sub, inp_pos, out_pos, port_id, dir);
      mod_to_io_dir_map[std::make_pair(mod_id, port_id)] = dir;
      mod_to_io_map[mod_id].insert({port_id, type.uint_type().width().value(), dir, false});
      break;
    }
    case firrtl::FirrtlPB_Type::kSintType: {  // SInt type
      AddPortToSub(sub, inp_pos, out_pos, port_id, dir);
      mod_to_io_dir_map[std::make_pair(mod_id, port_id)] = dir;
      mod_to_io_map[mod_id].insert({port_id, type.sint_type().width().value(), dir, true});
      break;
    }
    case firrtl::FirrtlPB_Type::kClockType: {  // Clock type
      AddPortToSub(sub, inp_pos, out_pos, port_id, dir);
      mod_to_io_dir_map[std::make_pair(mod_id, port_id)] = dir;
      mod_to_io_map[mod_id].insert({port_id, 1, dir, false});
      break;
    }
    case firrtl::FirrtlPB_Type::kAsyncResetType: {  // AsyncReset type
      AddPortToSub(sub, inp_pos, out_pos, port_id, dir);
      mod_to_io_dir_map[std::make_pair(mod_id, port_id)] = dir;
      mod_to_io_map[mod_id].insert({port_id, 1, dir, false});
      async_rst_names.insert(port_id);
      break;
    }
    case firrtl::FirrtlPB_Type::kResetType: {  // Reset type
      AddPortToSub(sub, inp_pos, out_pos, port_id, dir);
      mod_to_io_dir_map[std::make_pair(mod_id, port_id)] = dir;
      mod_to_io_map[mod_id].insert({port_id, 1, dir, false});
      break;
    }
    case firrtl::FirrtlPB_Type::kBundleType: {  // Bundle type
      const firrtl::FirrtlPB_Type_BundleType btype = type.bundle_type();
      for (int i = 0; i < type.bundle_type().field_size(); i++) {
        if (btype.field(i).is_flipped()) {
          uint8_t new_dir = 0;
          if (dir == 1) {  // PORT_DIRECTION_IN
            new_dir = 2;
          } else if (dir == 2) {
            new_dir = 1;
          }
          I(new_dir != 0);
          AddPortToMap(mod_id, btype.field(i).type(), new_dir, port_id + "." + btype.field(i).id(), sub, inp_pos, out_pos);
        } else {
          AddPortToMap(mod_id, btype.field(i).type(), dir, port_id + "." + btype.field(i).id(), sub, inp_pos, out_pos);
        }
      }
      break;
    }
    case firrtl::FirrtlPB_Type::kVectorType: {  // Vector type
      // FIXME: How does mod_to_io_map interact with a vector?
      mod_to_io_dir_map[std::make_pair(mod_id, port_id)] = dir;
      for (uint32_t i = 0; i < type.vector_type().size(); i++) {
        // note: get rid of [], use . all the time

        // DEBUGG
        AddPortToMap(mod_id, type.vector_type().type(), dir, absl::StrCat(port_id, ".", i), sub, inp_pos, out_pos);
      }
      break;
    }
    case firrtl::FirrtlPB_Type::kFixedType: {  // Fixed type
      I(false);                                // TODO: Not yet supported.
      break;
    }
    case firrtl::FirrtlPB_Type::kAnalogType: {  // Analog type
      I(false);                                 // TODO: Not yet supported.
      break;
    }
    default: Pass::error("Unknown port type.");
  }
}

/* Not much to do here since this is just a Verilog
 * module that FIRRTL is going to use. Will have to
 * rely upon some Verilog pass to get the actual
 * contents of this into Lgraph form. */
void Inou_firrtl::GrabExtModuleInfo(const firrtl::FirrtlPB_Module_ExternalModule& emod) {
  // Figure out all of mods IO and their respective bw + dir.
  std::vector<std::tuple<std::string, uint8_t, uint32_t, bool>>
      port_list;  // Terms are as follows: name, direction, # of bits, sign.
  for (int i = 0; i < emod.port_size(); i++) {
    auto port = emod.port(i);
    create_io_list(port.type(), port.direction(), port.id(), port_list);
  }

  // Figure out what the value for each parameter is, add to map.
  for (int j = 0; j < emod.parameter_size(); j++) {
    std::string param_str = "";
    switch (emod.parameter(j).value_case()) {
      case firrtl::FirrtlPB_Module_ExternalModule_Parameter::kInteger:
        param_str = ConvertBigIntToStr(emod.parameter(j).integer());
        break;
      case firrtl::FirrtlPB_Module_ExternalModule_Parameter::kDouble:
        param_str = std::to_string(emod.parameter(j).double_());
        break;
      case firrtl::FirrtlPB_Module_ExternalModule_Parameter::kString: param_str = emod.parameter(j).string(); break;
      case firrtl::FirrtlPB_Module_ExternalModule_Parameter::kRawString: param_str = emod.parameter(j).raw_string(); break;
      default: I(false);
    }
    emod_to_param_map[emod.defined_name()].insert({emod.parameter(j).id(), param_str});
  }

  // Add them to the map to let us know what ports exist in this module.
  for (const auto& elem : port_list) {
    mod_to_io_dir_map[std::make_pair(emod.defined_name(), std::get<0>(elem))] = std::get<1>(elem);
    mod_to_io_map[emod.defined_name()].insert({std::get<0>(elem), std::get<2>(elem), std::get<1>(elem), std::get<3>(elem)});
  }
}

std::string Inou_firrtl::ConvertBigIntToStr(const firrtl::FirrtlPB_BigInt& bigint) {
  if (bigint.value().length() == 0) {
    return absl::StrCat("0b", 0, "s1bit");
  }

  std::string bigint_val = "";
  for (long unsigned int i = 0; i < bigint.value().length(); i++) {
    char        bigint_char = bigint.value()[i];
    std::string bit_str     = "";
    for (int j = 0; j < 8; j++) {
      if (bigint_char % 2) {
        bit_str = absl::StrCat("1", bit_str);
      } else {
        bit_str = absl::StrCat("0", bit_str);
      }
      bigint_char >>= 1;
    }
    bigint_val = absl::StrCat(bigint_val, bit_str);
  }
  return absl::StrCat("0b", bigint_val, "s", bigint.value().length() * 8, "bits");
}

void Inou_firrtl::IterateModules(Eprp_var& var, const firrtl::FirrtlPB_Circuit& circuit, const std::string& file_name) {
  if (circuit.top_size() > 1) {
    Pass::error("More than 1 top module specified.");
    I(false);
  }

  // Create ModuleName to I/O Pair List
  PopulateAllModsIO(var, circuit, file_name);

  for (int i = 0; i < circuit.module_size(); i++) {
    // Between modules, module specific lists.
    tmp_var_cnt         = 0;
    dummy_expr_node_cnt = 0;
    input_names.clear();
    output_names.clear();
    register_names.clear();
    reg2qpin.clear();
    memory_names.clear();
    async_rst_names.clear();
    inst_to_mod_map.clear();
    mem_props_map.clear();
    dangling_ports_map.clear();
    late_assign_ports.clear();
    reg_name2rst_init_expr.clear();
    output_name2port_info.clear();

    ListModuleInfo(var, circuit.module(i), file_name);
  }
}

// Iterate over every FIRRTL circuit (design), each circuit can contain multiple modules.
void Inou_firrtl::IterateCircuits(Eprp_var& var, const firrtl::FirrtlPB& firrtl_input, const std::string& file_name) {
  for (int i = 0; i < firrtl_input.circuit_size(); i++) {
    mod_to_io_dir_map.clear();
    mod_to_io_map.clear();
    emod_to_param_map.clear();

    const firrtl::FirrtlPB_Circuit& circuit = firrtl_input.circuit(i);
    IterateModules(var, circuit, file_name);
  }
}

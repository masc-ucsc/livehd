//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//

#include <cstdint>
#include <fstream>
#include <iostream>
#include <queue>
#include <string>
#include <utility>

#include "absl/strings/match.h"
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
    for (const auto& f : files.split(',')) {
      fmt::print("FILE: {}\n", f);
      firrtl::FirrtlPB firrtl_input;
      std::fstream     input(f.to_s().c_str(), std::ios::in | std::ios::binary);
      if (!firrtl_input.ParseFromIstream(&input)) {
        Pass::error("Failed to parse FIRRTL from protobuf format: {}", f);
        return;
      }
      p.tmp_var_cnt         = 0;
      p.seq_cnt             = 0;
      p.dummy_expr_node_cnt = 0;
      p.IterateCircuits(var, firrtl_input, f);
    }
  } else {
    fmt::print("No file provided. This requires a file input.\n");
    return;
  }

  // Optional:  Delete all global objects allocated by libprotobuf.
  google::protobuf::ShutdownProtobufLibrary();
}

//----------------Helper Functions--------------------------
mmap_lib::str Inou_firrtl::create_tmp_var() {
  return mmap_lib::str::concat(mmap_lib::str("___F"), ++tmp_var_cnt);
}

mmap_lib::str Inou_firrtl::create_tmp_mut_var() {
  return mmap_lib::str::concat(mmap_lib::str("_._M"), ++dummy_expr_node_cnt);
}

/* Determine if 'term' refers to any IO/reg/etc... If it does,
 * add the appropriate symbol or (in case of a register on RHS)
 * create a DOT node to access the correct value. */
mmap_lib::str Inou_firrtl::get_full_name(const mmap_lib::str &term, const bool is_rhs) {
  auto term2     = term;
  mmap_lib::str term_rest;
  auto pos       = term.find('.');
  if (pos != std::string::npos) {
    term2     = term.substr(0, pos);
    term_rest = term.substr(pos);
  }

  if (input_names.count(term)) {
    return mmap_lib::str::concat("$", term);
  } else if (output_names.count(term)) {
    return mmap_lib::str::concat("%", term);
  } else if (reg2qpin.count(term2)) {  // check if the register exists
    I(reg2qpin[term2].substr(0, 3) == "_#_");
    if (is_rhs) {
      return mmap_lib::str::concat(reg2qpin[term2], term_rest);
    } else {
      return mmap_lib::str::concat("#", term);
    }
  } else {
    // // We add _. in front of firrtl temporary names
    // if (term.substr(0, 2) == "_T") {
    //   return absl::StrCat("_.", term);
    // if (term.substr(0, 4) == "_GEN") {
    //   return absl::StrCat("_.", term);
    // } else {
    return term;
  }
}

/* If the bitwidth is specified, in LNAST we have to create a new variable
 *  which represents the number of bits that a variable will have. */
void Inou_firrtl::create_bitwidth_dot_node(Lnast& lnast, uint32_t bitwidth, Lnast_nid& parent_node, const mmap_lib::str &port_id, bool is_signed) {

  if (bitwidth <= 0) {
    /* No need to make a bitwidth node, 0 means implicit bitwidth.
     * If -1, then that's how I specify that the "port_id" is not an
     * actual wire but instead the general vector name. */
    return;
  }
  auto value_node = Lnast_node::create_const(bitwidth);
  auto extension  = is_signed ? ".__sbits" : ".__ubits";

  CreateTupAddFromStr(lnast, parent_node, mmap_lib::str::concat(port_id, extension), value_node);
}

uint32_t Inou_firrtl::get_bit_count(const firrtl::FirrtlPB_Type& type) {
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

void Inou_firrtl::init_wire_dots(Lnast& lnast, const firrtl::FirrtlPB_Type &type, const mmap_lib::str &id, Lnast_nid &parent_node) {
  switch (type.type_case()) {
    case firrtl::FirrtlPB_Type::kBundleType: {  // Bundle Type
      // Todo: also initialize variable to zero
      for (int i = 0; i < type.bundle_type().field_size(); i++) {
        init_wire_dots(lnast,
                       type.bundle_type().field(i).type(),
                       mmap_lib::str::concat(id, ".", type.bundle_type().field(i).id()),
                       parent_node);
      }
      break;
    }
    case firrtl::FirrtlPB_Type::kVectorType: {  // Vector Type
      for (uint32_t i = 0; i < type.vector_type().size(); i++) {
        init_wire_dots(lnast, type.vector_type().type(), mmap_lib::str::concat(id, ".", i), parent_node);
      }
      break;
    }
    case firrtl::FirrtlPB_Type::kFixedType: {  // Fixed Point Type
      I(false);                                // TODO: LNAST does not support fixed point yet.
      break;
    }
    case firrtl::FirrtlPB_Type::kAsyncResetType: {  // AsyncReset
      // auto wire_bits = get_bit_count(type);
      // create_bitwidth_dot_node(lnast, wire_bits, parent_node, id, false);
      // async_rst_names.insert(id);
      break;
    }
    case firrtl::FirrtlPB_Type::kSintType: {  // signed
      auto idx_asg_wire = lnast.add_child(parent_node, Lnast_node::create_assign());
      lnast.add_child(idx_asg_wire, Lnast_node::create_ref(id));
      lnast.add_child(idx_asg_wire, Lnast_node::create_const("0"));
      auto wire_bits = get_bit_count(type);
      create_bitwidth_dot_node(lnast, wire_bits, parent_node, id, true);
      break;
    }
    case firrtl::FirrtlPB_Type::kUintType: {  // unsigned
      auto idx_asg_wire = lnast.add_child(parent_node, Lnast_node::create_assign());
      lnast.add_child(idx_asg_wire, Lnast_node::create_ref(id));
      lnast.add_child(idx_asg_wire, Lnast_node::create_const("0"));
      auto wire_bits = get_bit_count(type);
      create_bitwidth_dot_node(lnast, wire_bits, parent_node, id, false);
      break;
    }
    default: {
      // UInt Analog Reset Clock Types
      auto wire_bits = get_bit_count(type);
      create_bitwidth_dot_node(lnast, wire_bits, parent_node, id, false);
    }
  }
}

// When creating a register, we have to set the register's
// clock, reset, and init values using "dot" nodes in the LNAST.
// These functions create all of those when a reg is first declared.
void Inou_firrtl::setup_register_bits(Lnast& lnast, const firrtl::FirrtlPB_Type& type, const mmap_lib::str& id, Lnast_nid& parent_node) {
  switch (type.type_case()) {
    case firrtl::FirrtlPB_Type::kBundleType: {  // Bundle Type
      for (int i = 0; i < type.bundle_type().field_size(); i++) {
        setup_register_bits(lnast,
                            type.bundle_type().field(i).type(),
                            mmap_lib::str::concat(id, ".", type.bundle_type().field(i).id()),
                            parent_node);
      }
      break;
    }
    case firrtl::FirrtlPB_Type::kVectorType: {  // Vector Type
      for (uint32_t i = 0; i < type.vector_type().size(); i++) {
        setup_register_bits(lnast, type.vector_type().type(), mmap_lib::str::concat(id, ".", i), parent_node);
      }
      break;
    }
    case firrtl::FirrtlPB_Type::kFixedType: {  // Fixed Point
      I(false);                                // FIXME: Unsure how to implement
      break;
    }
    case firrtl::FirrtlPB_Type::kAsyncResetType: {  // AsyncReset
      auto reg_bits = get_bit_count(type);
      setup_register_bits_scalar(lnast, id, reg_bits, parent_node, false);
      async_rst_names.insert(id.substr(1));
      break;
    }
    case firrtl::FirrtlPB_Type::kSintType: {
      auto reg_bits = get_bit_count(type);
      setup_register_bits_scalar(lnast, id, reg_bits, parent_node, true);
      break;
    }
    case firrtl::FirrtlPB_Type::kClockType: {
      break;
    }

    default: {
      /* UInt Analog Reset Types*/
      auto reg_bits = get_bit_count(type);
      setup_register_bits_scalar(lnast, id, reg_bits, parent_node, false);
    }
  }
}

void Inou_firrtl::setup_register_bits_scalar(Lnast& lnast, const mmap_lib::str& id, uint32_t bitwidth, Lnast_nid& parent_node, bool is_signed) {
  // Specify __bits, if bitwidth is explicit
  if (bitwidth > 0) {
    auto value_node = Lnast_node::create_const(bitwidth);
    auto extension  = is_signed ? ".__sbits" : ".__ubits";
    CreateTupAddFromStr(lnast, parent_node, mmap_lib::str::concat(id, extension), value_node);
  }
}

void Inou_firrtl::collect_memory_data_struct_hierarchy(const mmap_lib::str& mem_name, const firrtl::FirrtlPB_Type& type_in, const mmap_lib::str& hier_fields_concats) {
  mmap_lib::str new_hier_fields_concats;
  if (type_in.type_case() == firrtl::FirrtlPB_Type::kBundleType) {
    for (int i = 0; i < type_in.bundle_type().field_size(); i++) {
      if (hier_fields_concats.empty()) {
        new_hier_fields_concats = mmap_lib::str(type_in.bundle_type().field(i).id());
      } else {
        new_hier_fields_concats = mmap_lib::str::concat(hier_fields_concats, ".", type_in.bundle_type().field(i).id());
      }

      auto type_sub_field = type_in.bundle_type().field(i).type();
      if (type_sub_field.type_case() == firrtl::FirrtlPB_Type::kBundleType
          || type_sub_field.type_case() == firrtl::FirrtlPB_Type::kVectorType) {
        collect_memory_data_struct_hierarchy(mem_name, type_sub_field, new_hier_fields_concats);
      } else {
        auto bits = get_bit_count(type_sub_field);
        new_hier_fields_concats = mmap_lib::str::concat(new_hier_fields_concats, ".", bits); // encode .bits at the end of hier-fields
        mem2din_fields[mem_name].emplace_back(new_hier_fields_concats);
      }
    }
  } else if (type_in.type_case() == firrtl::FirrtlPB_Type::kVectorType) {
    for (uint32_t i = 0; i < type_in.vector_type().size(); i++) {
      if (hier_fields_concats.empty()) {
        new_hier_fields_concats = mmap_lib::str(i);
      } else {
        new_hier_fields_concats = mmap_lib::str::concat(hier_fields_concats, ".", std::to_string(i));
      }

      auto type_sub_field = type_in.vector_type().type();
      if (type_sub_field.type_case() == firrtl::FirrtlPB_Type::kBundleType
          || type_sub_field.type_case() == firrtl::FirrtlPB_Type::kVectorType) {
        collect_memory_data_struct_hierarchy(mem_name, type_sub_field, new_hier_fields_concats);
      } else {
        auto bits = get_bit_count(type_sub_field);
        // absl::StrAppend(&new_hier_fields_concats, ".", bits);  // encode .bits at the end of hier-fields
        new_hier_fields_concats = mmap_lib::str::concat(new_hier_fields_concats, ".", bits);
        mem2din_fields[mem_name].emplace_back(new_hier_fields_concats);
      }
    }
  }
}

void Inou_firrtl::InitCMemory(Lnast& lnast, Lnast_nid& parent_node, const firrtl::FirrtlPB_Statement_CMemory& cmem) {
  mmap_lib::str depth_str;  // depth in firrtl = size in LiveHD
  uint8_t          wensize_init = 1;
  if (cmem.type_case() == firrtl::FirrtlPB_Statement_CMemory::kTypeAndDepth) {
    depth_str = ConvertBigIntToStr(cmem.type_and_depth().depth());
  } else {
    I(false, "happened somewhere in boom!");  // never happened?
  }

  firrtl::FirrtlPB_Type din_type = cmem.type_and_depth().data_type();
  if (din_type.type_case() == firrtl::FirrtlPB_Type::kBundleType || din_type.type_case() == firrtl::FirrtlPB_Type::kVectorType) {
    collect_memory_data_struct_hierarchy(mmap_lib::str(cmem.id()), din_type, "");
    wensize_init = din_type.vector_type().size();
    // } else if (din_type.type_case() == firrtl::FirrtlPB_Type::kVectorType) {
    //   I(false, "it's masked case!");
  } else if (din_type.type_case() == firrtl::FirrtlPB_Type::kUintType) {
    auto bits = get_bit_count(din_type);
    mem2din_fields[mmap_lib::str(cmem.id())].emplace_back(absl::StrCat(".", bits));  // encode .bits at the end of hier-fields
  } else {
    I(false);
  }

  // auto bits = get_bit_count(type);

  // Specify attributes
  bool fwd = false;
  if (cmem.read_under_write() == firrtl::FirrtlPB_Statement_ReadUnderWrite::FirrtlPB_Statement_ReadUnderWrite_NEW) {
    fwd = true;
  }

  // create foo_mem_res = __memory(foo_mem_aruments.__last_value)
  auto idx_attr_get = lnast.add_child(parent_node, Lnast_node::create_attr_get());
  auto temp_var_str = create_tmp_var();
  lnast.add_child(idx_attr_get, Lnast_node::create_ref(temp_var_str));
  lnast.add_child(idx_attr_get, Lnast_node::create_ref((mmap_lib::str::concat(cmem.id(), "_interface_args"))));
  lnast.add_child(idx_attr_get, Lnast_node::create_const("__last_value"));

  auto idx_fncall = lnast.add_child(parent_node, Lnast_node::create_func_call());
  lnast.add_child(idx_fncall, Lnast_node::create_ref(mmap_lib::str::concat(cmem.id(), "_res")));
  lnast.add_child(idx_fncall, Lnast_node::create_ref("__memory"));
  lnast.add_child(idx_fncall, Lnast_node::create_ref(temp_var_str));

  auto idx_ta_maddr = lnast.add_child(parent_node, Lnast_node::create_tuple_add());
  lnast.add_child(idx_ta_maddr, Lnast_node::create_ref(mmap_lib::str::concat(cmem.id(), "_addr")));

  auto idx_ta_mdin = lnast.add_child(parent_node, Lnast_node::create_tuple_add());
  lnast.add_child(idx_ta_mdin, Lnast_node::create_ref(mmap_lib::str::concat(cmem.id(), "_din")));

  auto idx_ta_men = lnast.add_child(parent_node, Lnast_node::create_tuple_add());
  lnast.add_child(idx_ta_men, Lnast_node::create_ref(mmap_lib::str::concat(cmem.id(), "_enable")));

  auto idx_asg_mfwd = lnast.add_child(parent_node, Lnast_node::create_assign());
  lnast.add_child(idx_asg_mfwd, Lnast_node::create_ref(mmap_lib::str::concat(cmem.id(), "_fwd")));
  lnast.add_child(idx_asg_mfwd, Lnast_node::create_const(fwd));  // note: initialized

  auto idx_ta_mlat = lnast.add_child(parent_node, Lnast_node::create_tuple_add());
  lnast.add_child(idx_ta_mlat, Lnast_node::create_ref(mmap_lib::str::concat(cmem.id(), "_type")));
  lnast.add_child(idx_ta_mlat, Lnast_node::create_const(cmem.sync_read()?1:0));

  auto idx_asg_mwensize = lnast.add_child(parent_node, Lnast_node::create_assign());
  lnast.add_child(idx_asg_mwensize, Lnast_node::create_ref(mmap_lib::str::concat(cmem.id(), "_wensize")));
  lnast.add_child(idx_asg_mwensize, Lnast_node::create_const(wensize_init));

  auto idx_asg_msize = lnast.add_child(parent_node, Lnast_node::create_assign());
  lnast.add_child(idx_asg_msize, Lnast_node::create_ref(mmap_lib::str::concat(cmem.id(), "_size")));
  lnast.add_child(idx_asg_msize, Lnast_node::create_const(depth_str));  // note: initialized

  auto idx_ta_mrport = lnast.add_child(parent_node, Lnast_node::create_tuple_add());
  lnast.add_child(idx_ta_mrport, Lnast_node::create_ref(mmap_lib::str::concat(cmem.id(), "_rdport")));

  // create if true scope for foo_mem_din field variable initialization/declaration
  auto idx_if = lnast.add_child(parent_node, Lnast_node::create_if());
  lnast.add_child(idx_if, Lnast_node::create_ref("true"));
  auto idx_stmts = lnast.add_child(idx_if, Lnast_node::create_stmts());
  mem2initial_idx.insert_or_assign(mmap_lib::str(cmem.id()), idx_stmts);

  mem2port_cnt.insert_or_assign(mmap_lib::str(cmem.id()), -1);
}

void Inou_firrtl::HandleMportDeclaration(Lnast& lnast, Lnast_nid& parent_node, const firrtl::FirrtlPB_Statement_MemoryPort& mport) {
  // (void) parent_node;
  auto mem_name = mmap_lib::str(mport.memory_id());
  mport2mem.insert_or_assign(mmap_lib::str(mport.id()), mem_name);

  mem2port_cnt[mem_name]++;
  auto clk_str         = ReturnExprString(lnast, mport.expression(), parent_node, true);
  auto adr_str         = ReturnExprString(lnast, mport.memory_index(), parent_node, true);
  auto port_cnt_str    = mem2port_cnt[mem_name];
  auto default_val_str = 0;

  // assign whatever adder/enable the mport variable comes with in the current scope, either top or scope
  auto idx_ta_maddr = lnast.add_child(parent_node, Lnast_node::create_tuple_add());
  lnast.add_child(idx_ta_maddr, Lnast_node::create_ref(mmap_lib::str::concat(mem_name, "_addr")));
  lnast.add_child(idx_ta_maddr, Lnast_node::create_const(port_cnt_str));
  lnast.add_child(idx_ta_maddr, Lnast_node::create_ref(adr_str));

  // note: because any port might be declared inside a subscope but be used at upper scope, at the time you see a mport
  //       declaration, you must specify the port enable signal, even it's a masked write port. For the maksed write, a
  //       bit-vector wr_enable will be handled at the HandleWrMportUsage()
  auto idx_ta_men = lnast.add_child(parent_node, Lnast_node::create_tuple_add());
  lnast.add_child(idx_ta_men, Lnast_node::create_ref(mmap_lib::str::concat(mem_name, "_enable")));
  lnast.add_child(idx_ta_men, Lnast_node::create_const(port_cnt_str));
  lnast.add_child(idx_ta_men, Lnast_node::create_const(1));

  // initialized port interfaces at the top scope
  I(mem2initial_idx.find(mem_name) != mem2initial_idx.end());
  auto& idx_initialize_stmts = mem2initial_idx[mem_name];

  auto idx_ta_mclk_ini = lnast.add_child(idx_initialize_stmts, Lnast_node::create_assign());
  lnast.add_child(idx_ta_mclk_ini, Lnast_node::create_ref(mmap_lib::str::concat(mem_name, "_clock")));
  lnast.add_child(idx_ta_mclk_ini, Lnast_node::create_ref(clk_str));

  auto idx_ta_maddr_ini = lnast.add_child(idx_initialize_stmts, Lnast_node::create_tuple_add());
  lnast.add_child(idx_ta_maddr_ini, Lnast_node::create_ref(mmap_lib::str::concat(mem_name, "_addr")));
  lnast.add_child(idx_ta_maddr_ini, Lnast_node::create_const(port_cnt_str));
  lnast.add_child(idx_ta_maddr_ini, Lnast_node::create_const(default_val_str));

  auto idx_ta_men_ini = lnast.add_child(idx_initialize_stmts, Lnast_node::create_tuple_add());
  lnast.add_child(idx_ta_men_ini, Lnast_node::create_ref(mmap_lib::str::concat(mem_name, "_enable")));
  lnast.add_child(idx_ta_men_ini, Lnast_node::create_const(port_cnt_str));
  lnast.add_child(idx_ta_men_ini, Lnast_node::create_const(default_val_str));

  auto idx_ta_mlat_ini = lnast.add_child(idx_initialize_stmts, Lnast_node::create_tuple_add());
  lnast.add_child(idx_ta_mlat_ini, Lnast_node::create_ref(mmap_lib::str::concat(mem_name, "_type")));
  lnast.add_child(idx_ta_mlat_ini, Lnast_node::create_const(port_cnt_str));
  lnast.add_child(idx_ta_mlat_ini, Lnast_node::create_const(default_val_str));

  auto idx_ta_mrdport_ini = lnast.add_child(idx_initialize_stmts, Lnast_node::create_tuple_add());
  lnast.add_child(idx_ta_mrdport_ini, Lnast_node::create_ref(mmap_lib::str::concat(mem_name, "_rdport")));
  lnast.add_child(idx_ta_mrdport_ini, Lnast_node::create_const(port_cnt_str));
  lnast.add_child(idx_ta_mrdport_ini, Lnast_node::create_const("true"));

  auto dir_case = mport.direction();
  if (dir_case == firrtl::FirrtlPB_Statement_MemoryPort_Direction::FirrtlPB_Statement_MemoryPort_Direction_MEMORY_PORT_DIRECTION_READ) {
    // only need to initialize mem_res[rd_port] when you are sure it's a read mport
    InitMemRes(lnast, mem_name, port_cnt_str);
    // FIXME->sh:
    // if you already know it's a read mport,  you should let mport = mem_res[rd_port] here, or the mem_port_cnt might duplicately
    // count one more port_cnt, see the cases from ListBuffer.fir (search push_tail).
  } else if (dir_case
             == firrtl::FirrtlPB_Statement_MemoryPort_Direction::
                 FirrtlPB_Statement_MemoryPort_Direction_MEMORY_PORT_DIRECTION_WRITE) {
    // noly need to initialize mem_din[wr_port] when you are sure it's a write mport
    InitMemDin(lnast, mem_name, port_cnt_str);
    I(mport2mask_bitvec.find(mmap_lib::str(mport.id())) == mport2mask_bitvec.end());
    I(mport2mask_cnt.find(mmap_lib::str(mport.id())) == mport2mask_cnt.end());
    mport2mask_bitvec.insert_or_assign(mmap_lib::str(mport.id()), 1);
    mport2mask_cnt.insert_or_assign(mmap_lib::str(mport.id()), 0);
  } else {
    // need to initialize both mem_din[wr_port] mem_res[res_port] when you are not sure the port type
    InitMemRes(lnast, mem_name, port_cnt_str);
    InitMemDin(lnast, mem_name, port_cnt_str);
  }
}

// we have to set the memory result bits so the later fir_bits pass could start propagate bits information from.
void Inou_firrtl::InitMemRes(Lnast& lnast, const mmap_lib::str& mem_name, const mmap_lib::str& port_cnt_str) {
  I(mem2initial_idx.find(mem_name) != mem2initial_idx.end());
  auto& idx_initialize_stmts = mem2initial_idx[mem_name];
  auto  it                   = mem2din_fields.find(mem_name);
  I(it != mem2din_fields.end());

  auto  mem_res_str     = mmap_lib::str::concat(mem_name, "_res");
  auto& hier_full_names = mem2din_fields[mem_name];
  for (const auto& hier_full_name : hier_full_names) {  // hier_full_name example: foo.bar.baz.20, the last field is bit
    fmt::print("hier_name:{}\n", hier_full_name);
    std::vector<mmap_lib::str> hier_sub_names;
    split_hier_name(hier_full_name, hier_sub_names);

    if (hier_sub_names.size() == 1) {  // it's a pure sclalar memory dout
      auto idx_ta = lnast.add_child(idx_initialize_stmts, Lnast_node::create_tuple_add());
      lnast.add_child(idx_ta, Lnast_node::create_ref(mem_res_str));
      lnast.add_child(idx_ta, Lnast_node::create_const(port_cnt_str));
      lnast.add_child(idx_ta, Lnast_node::create_const("__ubits"));
      lnast.add_child(idx_ta, Lnast_node::create_const(hier_sub_names.at(0)));
    } else {
      auto idx_ta = lnast.add_child(idx_initialize_stmts, Lnast_node::create_tuple_add());
      lnast.add_child(idx_ta, Lnast_node::create_ref(mem_res_str));
      lnast.add_child(idx_ta, Lnast_node::create_const(port_cnt_str));
      uint8_t idx = 0;
      for (const auto& sub_name : hier_sub_names) {
        if (idx == hier_sub_names.size() - 1) {
          lnast.add_child(idx_ta, Lnast_node::create_const("__ubits"));
        }
        lnast.add_child(idx_ta, Lnast_node::create_const(sub_name));
        idx++;
      }
    }
  }
}

void Inou_firrtl::InitMemDin(Lnast& lnast, const mmap_lib::str& mem_name, const mmap_lib::str& port_cnt_str) {
  auto default_val_str = 0;
  I(mem2initial_idx.find(mem_name) != mem2initial_idx.end());
  auto& idx_initialize_stmts = mem2initial_idx[mem_name];
  auto  it                   = mem2din_fields.find(mem_name);
  I(it != mem2din_fields.end());

  if (it->second.at(0) == ".") {  // din is scalar, the din_fields starts with something like .17
    auto idx_ta_mdin_ini = lnast.add_child(idx_initialize_stmts, Lnast_node::create_tuple_add());
    lnast.add_child(idx_ta_mdin_ini, Lnast_node::create_ref(mmap_lib::str::concat(mem_name, "_din")));
    lnast.add_child(idx_ta_mdin_ini, Lnast_node::create_const(port_cnt_str));
    lnast.add_child(idx_ta_mdin_ini, Lnast_node::create_const(default_val_str));
  } else {  // din is tuple
    auto& hier_full_names = mem2din_fields[mem_name];
    for (const auto& hier_full_name : hier_full_names) {  // hier_full_name example: foo.bar.baz.20, the last field is bit
      fmt::print("hier_name:{}\n", hier_full_name);
      std::vector<mmap_lib::str> hier_sub_names;
      // auto found = hier_full_name.find_last_of('.');  // get rid of last bit field
      auto found = hier_full_name.rfind('.');  // get rid of last bit field
      split_hier_name(hier_full_name.substr(0, found), hier_sub_names);

      auto idx_ta_mdin_ini = lnast.add_child(idx_initialize_stmts, Lnast_node::create_tuple_add());
      lnast.add_child(idx_ta_mdin_ini, Lnast_node::create_ref(mmap_lib::str::concat(mem_name, "_din")));
      lnast.add_child(idx_ta_mdin_ini, Lnast_node::create_const(port_cnt_str));

      for (const auto& sub_name : hier_sub_names)
        lnast.add_child(idx_ta_mdin_ini, Lnast_node::create_const(sub_name));

      lnast.add_child(idx_ta_mdin_ini, Lnast_node::create_const(default_val_str));
    }
  }
}

/* When a module instance is created in FIRRTL, we need to do the same
 * in LNAST. Note that the instance command in FIRRTL does not hook
 * any input or outputs. */
void Inou_firrtl::create_module_inst(Lnast& lnast, const firrtl::FirrtlPB_Statement_Instance& inst, Lnast_nid& parent_node) {
  /*            dot                       assign                      fn_call
   *      /      |        \                / \                     /     |     \
   * ___F0 itup_[inst_name] __last_value   F1 ___F0  otup_[inst_name] [mod_name]  F1 */
  auto temp_var_name2 = mmap_lib::str::concat("F", std::to_string(tmp_var_cnt));
  tmp_var_cnt++;
  auto inst_name = inst.id();
  if (inst.id().substr(0, 2) == "_T") {
    inst_name = absl::StrCat("_.", inst_name);
  }
  auto inp_name = mmap_lib::str::concat("itup_", inst_name);
  auto out_name = mmap_lib::str::concat("otup_", inst_name);

  auto idx_dot = lnast.add_child(parent_node, Lnast_node::create_attr_get());
  lnast.add_child(idx_dot, Lnast_node::create_ref(temp_var_name2));
  lnast.add_child(idx_dot, Lnast_node::create_ref(inp_name));
  lnast.add_child(idx_dot, Lnast_node::create_const("__last_value"));

  auto idx_fncall = lnast.add_child(parent_node, Lnast_node::create_func_call());
  lnast.add_child(idx_fncall, Lnast_node::create_ref(out_name));
  lnast.add_child(idx_fncall, Lnast_node::create_ref(mmap_lib::str::concat("__firrtl_", inst.module_id())));
  lnast.add_child(idx_fncall, Lnast_node::create_ref(temp_var_name2));

  /* Also, I need to record this module instance in
   * a map that maps instance name to module name. */
  inst_to_mod_map[mmap_lib::str(inst.id())] = mmap_lib::str(inst.module_id());

  // If any parameters exist (for ext module), specify those.
  // NOTE->hunter: We currently specify parameters the same way as inputs.
  for (const auto& param : emod_to_param_map[mmap_lib::str(inst.module_id())]) {
    auto idx_dot_p = lnast.add_child(parent_node, Lnast_node::create_tuple_add());
    if (isdigit(param.second[0])) {
      lnast.add_child(idx_dot_p, Lnast_node::create_const(param.second));
    } else {
      lnast.add_child(idx_dot_p, Lnast_node::create_ref(param.second));
    }
    lnast.add_child(idx_dot_p, Lnast_node::create_ref(inp_name));
    lnast.add_child(idx_dot_p, Lnast_node::create_ref(param.first));
  }
}

/* No mux node type exists in LNAST. To support FIRRTL muxes, we instead
 * map a mux to an if-else statement whose condition is the same condition
 * as the first argument (the condition) of the mux. */
void Inou_firrtl::HandleMuxAssign(Lnast& lnast, const firrtl::FirrtlPB_Expression& expr, Lnast_nid& parent_node, const mmap_lib::str& lhs) {
  I(lnast.get_data(parent_node).type.is_stmts());

  auto lhs_full    = get_full_name(lhs, false);
  auto idx_pre_asg = lnast.add_child(parent_node, Lnast_node::create_assign());
  lnast.add_child(idx_pre_asg, Lnast_node::create_ref(lhs_full));
  lnast.add_child(idx_pre_asg, Lnast_node::create_const("0b?"));

  auto cond_str   = ReturnExprString(lnast, expr.mux().condition(), parent_node, true);
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
void Inou_firrtl::HandleValidIfAssign(Lnast& lnast, const firrtl::FirrtlPB_Expression& expr, Lnast_nid& parent_node, const mmap_lib::str& lhs) {
  I(lnast.get_data(parent_node).type.is_stmts());

  // FIXME->sh: do the trick to declare variable with the validif value, hope this could make the validif to fit the role of "else
  // mux"
  /* auto lhs_full = get_full_name(lnast, parent_node, lhs, false); */
  /* auto idx_pre_asg = lnast.add_child(parent_node, Lnast_node::create_assign()); */
  /* lnast.add_child(idx_pre_asg, Lnast_node::create_ref(lnast.add_string(lhs_full))); */
  /* lnast.add_child(idx_pre_asg, Lnast_node::create_const("0b?")); */
  InitialExprAdd(lnast, expr.valid_if().value(), parent_node, lhs);

  auto cond_str = ReturnExprString(lnast, expr.valid_if().condition(), parent_node, true);
  auto idx_v_if = lnast.add_child(parent_node, Lnast_node::create_if());
  lnast.add_child(idx_v_if, Lnast_node::create_ref(cond_str));

  auto idx_stmt_tr = lnast.add_child(idx_v_if, Lnast_node::create_stmts());

  InitialExprAdd(lnast, expr.valid_if().value(), idx_stmt_tr, lhs);
}

// ----------------- primitive op start -------------------------------------------------------------------------------
void Inou_firrtl::HandleUnaryOp(Lnast& lnast, const firrtl::FirrtlPB_Expression_PrimOp& op, Lnast_nid& parent_node, const mmap_lib::str& lhs) {
  I(lnast.get_data(parent_node).type.is_stmts());
  I(op.arg_size() == 1);

  auto lhs_str = lhs;
  auto e1_str  = ReturnExprString(lnast, op.arg(0), parent_node, true);
  auto idx_not = lnast.add_child(parent_node, Lnast_node::create_func_call());  // "__fir_not"));
  lnast.add_child(idx_not, Lnast_node::create_ref(lhs_str));
  lnast.add_child(idx_not, Lnast_node::create_const("__fir_not"));
  lnast.add_child(idx_not, Lnast_node::create_ref(e1_str));
}

void Inou_firrtl::HandleAndReducOp(Lnast& lnast, const firrtl::FirrtlPB_Expression_PrimOp& op, Lnast_nid& parent_node, const mmap_lib::str& lhs) {
  I(lnast.get_data(parent_node).type.is_stmts());
  I(op.arg_size() == 1);

  auto lhs_str  = lhs;
  auto e1_str   = ReturnExprString(lnast, op.arg(0), parent_node, true);
  auto idx_andr = lnast.add_child(parent_node, Lnast_node::create_func_call());  // "__fir_andr"));
  lnast.add_child(idx_andr, Lnast_node::create_ref(lhs_str));
  lnast.add_child(idx_andr, Lnast_node::create_const("__fir_andr"));
  lnast.add_child(idx_andr, Lnast_node::create_ref(e1_str));
}

void Inou_firrtl::HandleOrReducOp(Lnast& lnast, const firrtl::FirrtlPB_Expression_PrimOp& op, Lnast_nid& parent_node, const mmap_lib::str& lhs) {
  I(lnast.get_data(parent_node).type.is_stmts());
  I(op.arg_size() == 1);

  auto lhs_str = lhs;
  auto e1_str  = ReturnExprString(lnast, op.arg(0), parent_node, true);
  auto idx_orr = lnast.add_child(parent_node, Lnast_node::create_func_call());  // "__fir_orr"));
  lnast.add_child(idx_orr, Lnast_node::create_ref(lhs_str));
  lnast.add_child(idx_orr, Lnast_node::create_const("__fir_orr"));
  lnast.add_child(idx_orr, Lnast_node::create_ref(e1_str));
}

void Inou_firrtl::HandleXorReducOp(Lnast& lnast, const firrtl::FirrtlPB_Expression_PrimOp& op, Lnast_nid& parent_node, const mmap_lib::str& lhs) {
  I(lnast.get_data(parent_node).type.is_stmts());
  I(op.arg_size() == 1);

  auto lhs_str  = lhs;
  auto e1_str   = ReturnExprString(lnast, op.arg(0), parent_node, true);
  auto idx_xorr = lnast.add_child(parent_node, Lnast_node::create_func_call());  // "__fir_xorr"));
  lnast.add_child(idx_xorr, Lnast_node::create_ref(lhs_str));
  lnast.add_child(idx_xorr, Lnast_node::create_const("__fir_xorr"));
  lnast.add_child(idx_xorr, Lnast_node::create_ref(e1_str));
}

void Inou_firrtl::HandleNegateOp(Lnast& lnast, const firrtl::FirrtlPB_Expression_PrimOp& op, Lnast_nid& parent_node, const mmap_lib::str& lhs) {
  I(lnast.get_data(parent_node).type.is_stmts());
  I(op.arg_size() == 1);

  auto lhs_str = lhs;
  auto e1_str  = ReturnExprString(lnast, op.arg(0), parent_node, true);
  auto idx_neg = lnast.add_child(parent_node, Lnast_node::create_func_call());  // "__fir_neg"));
  lnast.add_child(idx_neg, Lnast_node::create_ref(lhs_str));
  lnast.add_child(idx_neg, Lnast_node::create_const("__fir_neg"));
  lnast.add_child(idx_neg, Lnast_node::create_ref(e1_str));
}

void Inou_firrtl::HandleConvOp(Lnast& lnast, const firrtl::FirrtlPB_Expression_PrimOp& op, Lnast_nid& parent_node, const mmap_lib::str& lhs) {
  I(lnast.get_data(parent_node).type.is_stmts());
  I(op.arg_size() == 1);

  auto lhs_str = lhs;
  auto e1_str  = ReturnExprString(lnast, op.arg(0), parent_node, true);
  auto idx_cvt = lnast.add_child(parent_node, Lnast_node::create_func_call());  // "__fir_cvt"));
  lnast.add_child(idx_cvt, Lnast_node::create_ref(lhs_str));
  lnast.add_child(idx_cvt, Lnast_node::create_const("__fir_cvt"));
  lnast.add_child(idx_cvt, Lnast_node::create_ref(e1_str));
}

void Inou_firrtl::HandleExtractBitsOp(Lnast& lnast, const firrtl::FirrtlPB_Expression_PrimOp& op, Lnast_nid& parent_node, const mmap_lib::str& lhs) {
  I(lnast.get_data(parent_node).type.is_stmts());
  I(op.arg_size() == 1 && op.const__size() == 2);

  auto lhs_str       = lhs;
  auto e1_str        = ReturnExprString(lnast, op.arg(0), parent_node, true);
  auto idx_bits_exct = lnast.add_child(parent_node, Lnast_node::create_func_call());  // "__fir_bits"));
  lnast.add_child(idx_bits_exct, Lnast_node::create_ref(lhs_str));
  lnast.add_child(idx_bits_exct, Lnast_node::create_const("__fir_bits"));
  lnast.add_child(idx_bits_exct, Lnast_node::create_ref(e1_str));
  lnast.add_child(idx_bits_exct, Lnast_node::create_const(mmap_lib::str(op.const_(0).value())));
  lnast.add_child(idx_bits_exct, Lnast_node::create_const(mmap_lib::str(op.const_(1).value())));
}

void Inou_firrtl::HandleHeadOp(Lnast& lnast, const firrtl::FirrtlPB_Expression_PrimOp& op, Lnast_nid& parent_node, const mmap_lib::str& lhs) {
  I(lnast.get_data(parent_node).type.is_stmts());
  I(op.arg_size() == 1 && op.const__size() == 1);

  auto lhs_str  = lhs;
  auto e1_str   = ReturnExprString(lnast, op.arg(0), parent_node, true);
  auto idx_head = lnast.add_child(parent_node, Lnast_node::create_func_call());  // "__fir_head"));
  lnast.add_child(idx_head, Lnast_node::create_ref(lhs_str));
  lnast.add_child(idx_head, Lnast_node::create_const("__fir_head"));
  lnast.add_child(idx_head, Lnast_node::create_ref(e1_str));
  lnast.add_child(idx_head, Lnast_node::create_const(mmap_lib::str(op.const_(0).value())));
}

void Inou_firrtl::HandleTailOp(Lnast& lnast, const firrtl::FirrtlPB_Expression_PrimOp& op, Lnast_nid& parent_node, const mmap_lib::str& lhs) {
  I(lnast.get_data(parent_node).type.is_stmts());
  I(op.arg_size() == 1 && op.const__size() == 1);
  auto lhs_str = lhs;
  auto e1_str  = ReturnExprString(lnast, op.arg(0), parent_node, true);

  auto idx_tail = lnast.add_child(parent_node, Lnast_node::create_func_call());  // "__fir_tail"));
  lnast.add_child(idx_tail, Lnast_node::create_ref(lhs_str));
  lnast.add_child(idx_tail, Lnast_node::create_const("__fir_tail"));
  lnast.add_child(idx_tail, Lnast_node::create_ref(e1_str));
  lnast.add_child(idx_tail, Lnast_node::create_const(mmap_lib::str(op.const_(0).value())));
}

void Inou_firrtl::HandleConcatOp(Lnast& lnast, const firrtl::FirrtlPB_Expression_PrimOp& op, Lnast_nid& parent_node, const mmap_lib::str& lhs) {
  I(lnast.get_data(parent_node).type.is_stmts());
  I(op.arg_size() == 2);

  auto lhs_str = lhs;
  auto e1_str  = ReturnExprString(lnast, op.arg(0), parent_node, true);
  auto e2_str  = ReturnExprString(lnast, op.arg(1), parent_node, true);

  auto idx_concat = lnast.add_child(parent_node, Lnast_node::create_func_call());  // "__fir_cat"));
  lnast.add_child(idx_concat, Lnast_node::create_ref(lhs_str));
  lnast.add_child(idx_concat, Lnast_node::create_const("__fir_cat"));
  lnast.add_child(idx_concat, Lnast_node::create_ref(e1_str));
  lnast.add_child(idx_concat, Lnast_node::create_ref(e2_str));
}

void Inou_firrtl::HandlePadOp(Lnast& lnast, const firrtl::FirrtlPB_Expression_PrimOp& op, Lnast_nid& parent_node, const mmap_lib::str& lhs) {
  I(lnast.get_data(parent_node).type.is_stmts());
  I(op.arg_size() == 1 && op.const__size() == 1);

  auto lhs_str = lhs;
  auto e1_str  = ReturnExprString(lnast, op.arg(0), parent_node, true);

  auto idx_pad = lnast.add_child(parent_node, Lnast_node::create_func_call());  // "__fir_pad"));
  lnast.add_child(idx_pad, Lnast_node::create_ref(lhs_str));
  lnast.add_child(idx_pad, Lnast_node::create_const("__fir_pad"));
  lnast.add_child(idx_pad, Lnast_node::create_ref(e1_str));
  lnast.add_child(idx_pad, Lnast_node::create_const(mmap_lib::str(op.const_(0).value())));
}

void Inou_firrtl::HandleTwoExprPrimOp(Lnast& lnast, const firrtl::FirrtlPB_Expression_PrimOp& op, Lnast_nid& parent_node, const mmap_lib::str& lhs) {
  I(lnast.get_data(parent_node).type.is_stmts());
  I(op.arg_size() == 2);
  auto      e1_str = ReturnExprString(lnast, op.arg(0), parent_node, true);
  auto      e2_str = ReturnExprString(lnast, op.arg(1), parent_node, true);
  Lnast_nid idx_primop;

  auto sub_it = op2firsub.find(op.op());
  I(sub_it != op2firsub.end());

  idx_primop = lnast.add_child(parent_node, Lnast_node::create_func_call());
  lnast.add_child(idx_primop, Lnast_node::create_ref(lhs));
  lnast.add_child(idx_primop, Lnast_node::create_const(sub_it->second));

  AttachExprStrToNode(lnast, e1_str, idx_primop);
  AttachExprStrToNode(lnast, e2_str, idx_primop);
}

void Inou_firrtl::HandleStaticShiftOp(Lnast& lnast, const firrtl::FirrtlPB_Expression_PrimOp& op, Lnast_nid& parent_node, const mmap_lib::str& lhs) {
  I(lnast.get_data(parent_node).type.is_stmts());
  I(op.arg_size() == 1 || op.const__size() == 1);

  auto lhs_str = lhs;
  auto e1_str  = ReturnExprString(lnast, op.arg(0), parent_node, true);

  Lnast_nid idx_shift;
  auto      sub_it = op2firsub.find(op.op());
  I(sub_it != op2firsub.end());

  idx_shift = lnast.add_child(parent_node, Lnast_node::create_func_call());

  lnast.add_child(idx_shift, Lnast_node::create_ref(lhs_str));
  lnast.add_child(idx_shift, Lnast_node::create_const(sub_it->second));
  lnast.add_child(idx_shift, Lnast_node::create_ref(e1_str));
  lnast.add_child(idx_shift, Lnast_node::create_const(mmap_lib::str(op.const_(0).value())));
}

void Inou_firrtl::HandleTypeConvOp(Lnast& lnast, const firrtl::FirrtlPB_Expression_PrimOp& op, Lnast_nid& parent_node, const mmap_lib::str& lhs) {
  I(op.arg_size() == 1 && op.const__size() == 0);
  auto lhs_str = lhs;
  auto e1_str  = ReturnExprString(lnast, op.arg(0), parent_node, true);

  auto sub_it = op2firsub.find(op.op());
  I(sub_it != op2firsub.end());

  auto idx_conv = lnast.add_child(parent_node, Lnast_node::create_func_call());

  lnast.add_child(idx_conv, Lnast_node::create_ref(lhs_str));
  lnast.add_child(idx_conv, Lnast_node::create_const(sub_it->second));
  lnast.add_child(idx_conv, Lnast_node::create_const(e1_str));
}

// --------------------------------------- end of primitive op ----------------------------------------------

/* A SubField access is equivalent to accessing an element
 * of a tuple in LNAST. Just create a dot with each level
 * of hierarchy as a child of the DOT node. SubAccess/Index
 * instead rely upon a SELECT node. Sometimes, these three
 * can exist inside one another (vector of bundles) which
 * means we may need more than one DOT and/or SELECT node.
 * NOTE: This return the first child of the last DOT/SELECT node made. */

// FIXME:sh-> rewrite a clean code later
void Inou_firrtl::HandleBundVecAcc(Lnast& lnast, const firrtl::FirrtlPB_Expression& expr, Lnast_nid& parent_node, const bool is_rhs,
                                   const Lnast_node& value_node) {
  auto flattened_str  = FlattenExpression(lnast, parent_node, expr);
  auto alter_full_str = get_full_name(flattened_str, is_rhs);

  if (alter_full_str[0] == '$') {
    flattened_str = mmap_lib::str::concat("$", flattened_str);
  } else if (alter_full_str[0] == '%') {
    flattened_str = mmap_lib::str::concat("%", flattened_str);
  } else if (alter_full_str.substr(0, 3) == "_#_") {  // note: use _#_ to judge is a reg_qpin without split_hier_name again
    if (is_rhs) {
      flattened_str = alter_full_str;
    } else {
      flattened_str = mmap_lib::str::concat("#", flattened_str);
    }
  } else if (alter_full_str[0] == '#') {
    if (is_rhs) {
      I(false);
      flattened_str = alter_full_str.substr(1);
    } else {
      flattened_str = mmap_lib::str::concat("#", flattened_str);
    }
  } else if (mport2mem.count(alter_full_str.substr(0, alter_full_str.find('.')))) {
    // FIXME-sh: the mport name is not necessary at the head, it might be used as an index
    //       to cover this case, you might need to split the hierarchy name and check if any of
    //       the hierarchy name is a mport
    auto mport_name = alter_full_str.substr(0, alter_full_str.find('.'));
    if (is_rhs) {
      HandleRdMportUsage(lnast, parent_node, mport_name);
      CreateTupGetFromStr(lnast, parent_node, flattened_str, value_node);
    } else {  // lhs mport used as write
      HandleWrMportUsage(lnast, parent_node, mport_name);
      CreateTupAddFromStr(lnast, parent_node, flattened_str, value_node);
    }
    return;
  } else if (inst_to_mod_map.count(alter_full_str.substr(0, alter_full_str.find('.')))) {
    // note: instead of using alter_full_str, I use flattened_str.
    auto inst_name = flattened_str.substr(0, flattened_str.find('.'));
    if (inst_name.substr(0, 2) == "_T")
      inst_name = mmap_lib::str::concat("_.", inst_name);

    auto str_without_inst = flattened_str.substr(flattened_str.find('.') + 1);
    auto first_field_name = str_without_inst.substr(0, str_without_inst.find('.'));
    auto str_without_inst_and_io = str_without_inst;
    bool is_hier_io = false;
    auto str_pos    = str_without_inst.find('.');
    if (str_pos != std::string::npos) {
      str_without_inst_and_io = str_without_inst.substr(str_pos + 1);
      is_hier_io              = true;
    }

    auto module_name = inst_to_mod_map[inst_name];
    auto dir         = mod_to_io_dir_map[std::make_pair(module_name, str_without_inst)];

    if (is_hier_io) {
      if (dir == 1) {  // PORT_DIRECTION_IN
        flattened_str = mmap_lib::str::concat("itup_", inst_name, ".", first_field_name, ".", str_without_inst_and_io);
      } else if (dir == 2) {
        flattened_str = mmap_lib::str::concat("otup_", inst_name, ".", first_field_name, ".", str_without_inst_and_io);
      } else {
        Pass::error("direction unknown of {}\n", flattened_str);
        I(false);
      }
    }
  }

  I(flattened_str.find('.'));
  if (is_rhs) {
    CreateTupGetFromStr(lnast, parent_node, flattened_str, value_node);
  } else {
    CreateTupAddFromStr(lnast, parent_node, flattened_str, value_node);
  }
}

void Inou_firrtl::HandleRdMportUsage(Lnast& lnast, Lnast_nid& parent_node, const mmap_lib::str& mport_name) {
  // fmt::print("DEBUG2 rd_mport:{}\n", mport_name);
  auto mem_name     = mport2mem[mport_name];
  auto mem_port_str = mem2port_cnt[mem_name];
  // fmt::print("DEBUG4 rd port cnt:{}\n", mem_port_str);

  auto it = mport_usage_visited.find(mport_name);
  if (it == mport_usage_visited.end()) {
    mport_usage_visited.insert(mport_name);

    auto idx_ta_mrdport = lnast.add_child(parent_node, Lnast_node::create_tuple_add());
    lnast.add_child(idx_ta_mrdport, Lnast_node::create_ref(mmap_lib::str::concat(mem_name, "_rdport")));
    lnast.add_child(idx_ta_mrdport, Lnast_node::create_const(mem_port_str));
    lnast.add_child(idx_ta_mrdport, Lnast_node::create_const("true"));

    auto it2 = mem2rd_mports.find(mem_name);
    if (it2 == mem2rd_mports.end()) {
      mem2rd_mports.insert(std::pair<mmap_lib::str, std::vector<std::pair<mmap_lib::str, uint8_t>>>(
          mem_name,
          {std::make_pair(mport_name, mem2port_cnt[mem_name])}));
    } else {
      mem2rd_mports[mem_name].emplace_back(std::make_pair(mport_name, mem2port_cnt[mem_name]));
    }

    // note: I defer the handling of rd_mport = mem_res[rd_port] to the interface connection phase.
    //       the reason is we need to do tuple field recovering mem_din, like
    //       rd_mport = mem_din[some_wr_port]
    //       rd_mport := mem_res[rd_port]
    //
    //       but the wr_port are not necessary happened before rd_mport

    // deprecated
    // I(mem2initial_idx.find(mem_name) != mem2initial_idx.end());
    // auto &idx_initialize_stmts = mem2initial_idx[mem_name];
    // auto idx_tg = lnast.add_child(idx_initialize_stmts, Lnast_node::create_tuple_get());
    // auto temp_var_name = create_tmp_var(lnast);
    // lnast.add_child(idx_tg, Lnast_node::create_ref(temp_var_name));
    // lnast.add_child(idx_tg, Lnast_node::create_ref(mmap_lib::str::concat(mem_name, "_res"))));
    // lnast.add_child(idx_tg, Lnast_node::create_const(mem_port_str));

    // auto idx_asg = lnast.add_child(idx_initialize_stmts, Lnast_node::create_assign());
    // lnast.add_child(idx_asg, Lnast_node::create_ref(lnast.add_string(mport_name)));
    // lnast.add_child(idx_asg, Lnast_node::create_ref(temp_var_name));
  }
}

void Inou_firrtl::HandleWrMportUsage(Lnast& lnast, Lnast_nid& parent_node, const mmap_lib::str& mport_name) {
  // fmt::print("DEBUG2 wr_mport:{}\n", mport_name);
  auto mem_name     = mport2mem[mport_name];
  auto port_cnt     = mem2port_cnt[mem_name];
  auto port_cnt_str = port_cnt;
  auto it           = mport_usage_visited.find(mport_name);
  if (it == mport_usage_visited.end()) {
    auto& idx_initialize_stmts = mem2initial_idx[mem_name];
    mem2one_wr_mport.insert_or_assign(mem_name, port_cnt);

    mport_usage_visited.insert(mport_name);

    auto idx_ta_mrdport = lnast.add_child(idx_initialize_stmts, Lnast_node::create_tuple_add());
    lnast.add_child(idx_ta_mrdport, Lnast_node::create_ref(mmap_lib::str::concat(mem_name, "_rdport")));
    lnast.add_child(idx_ta_mrdport, Lnast_node::create_const(port_cnt_str));
    lnast.add_child(idx_ta_mrdport, Lnast_node::create_const("false"));

    auto idx_attr_get     = lnast.add_child(idx_initialize_stmts, Lnast_node::create_attr_get());
    auto mport_last_value = create_tmp_var();
    lnast.add_child(idx_attr_get, Lnast_node::create_ref(mport_last_value));
    lnast.add_child(idx_attr_get, Lnast_node::create_ref(mport_name));
    lnast.add_child(idx_attr_get, Lnast_node::create_const("__last_value"));

    auto idx_ta_mdin = lnast.add_child(idx_initialize_stmts, Lnast_node::create_tuple_add());
    lnast.add_child(idx_ta_mdin, Lnast_node::create_ref(mmap_lib::str::concat(mem_name, "_din")));
    lnast.add_child(idx_ta_mdin, Lnast_node::create_const(port_cnt_str));
    lnast.add_child(idx_ta_mdin, Lnast_node::create_ref(mport_last_value));
  }

  auto it2 = mport2mask_bitvec.find(mport_name);
  if (it2 != mport2mask_bitvec.end()) {
    auto bitvec = mport2mask_bitvec[mport_name];
    auto shtamt = mport2mask_cnt[mport_name];
    bitvec      = bitvec | 1 << shtamt;

    auto idx_ta_men = lnast.add_child(parent_node, Lnast_node::create_tuple_add());
    lnast.add_child(idx_ta_men, Lnast_node::create_ref(mmap_lib::str::concat(mem_name, "_enable")));
    lnast.add_child(idx_ta_men, Lnast_node::create_const(port_cnt_str));
    lnast.add_child(idx_ta_men, Lnast_node::create_const(bitvec));

    mport2mask_bitvec[mport_name] = bitvec;
    mport2mask_cnt[mport_name]++;
  }
}

void Inou_firrtl::set_leaf_type(const mmap_lib::str &subname, const mmap_lib::str &full_name, size_t prev,
                                std::vector<std::pair<mmap_lib::str, Inou_firrtl::Leaf_type>>& hier_subnames) {
  if (prev == 0) {
    hier_subnames.emplace_back(std::make_pair(subname, Leaf_type::Ref));
  } else if (full_name[prev - 1] == '.') {
    auto first_char = subname[0];
    if (isdigit(first_char) || first_char == '-' || first_char == '+') {
      hier_subnames.emplace_back(std::make_pair(subname, Leaf_type::Const_num));
    } else {
      hier_subnames.emplace_back(std::make_pair(subname, Leaf_type::Const_str));
    }
  } else if (full_name[prev - 1] == '[') {
    auto first_char = subname[0];
    if (isdigit(first_char) || first_char == '-' || first_char == '+') {
      hier_subnames.emplace_back(std::make_pair(subname, Leaf_type::Const_num));
    } else {
      hier_subnames.emplace_back(std::make_pair(subname, Leaf_type::Ref));
    }
  }
}

void Inou_firrtl::split_hier_name(const mmap_lib::str &full_name, std::vector<mmap_lib::str>& hier_subnames) {
  std::size_t prev = 0;
  std::size_t pos;

  // while ((pos = full_name.find_first_of(".[", prev)) != std::string_view::npos) {
  while ((pos = full_name.find(".", prev)) != std::string::npos || 
         (pos = full_name.find("[", prev)) != std::string::npos) {
    if (pos > prev) {
      auto subname = full_name.substr(prev, pos - prev);
      if (subname.back() == ']') {
        subname = subname.substr(0, subname.size() - 1);  // exclude ']'
      }
      hier_subnames.emplace_back(subname);
    }
    prev = pos + 1;
  }

  if (prev < full_name.size()) {
    auto subname = full_name.substr(prev, std::string::npos);
    if (subname.back() == ']') {
      subname = subname.substr(0, subname.size() - 1);  // exclude ']'
    }
    hier_subnames.emplace_back(subname);
  }
}

void Inou_firrtl::split_hier_name(const mmap_lib::str& full_name,
                                  std::vector<std::pair<mmap_lib::str, Inou_firrtl::Leaf_type>>& hier_subnames) {
  std::size_t prev = 0;
  std::size_t pos;

  // while ((pos = full_name.find_first_of(".[", prev)) != std::string_view::npos) {
  while ((pos = full_name.find(".", prev)) != std::string::npos || 
         (pos = full_name.find("[", prev)) != std::string::npos) {
    if (pos > prev) {
      auto subname = full_name.substr(prev, pos - prev);
      if (subname.back() == ']') {
        subname = subname.substr(0, subname.size() - 1);  // exclude ']'
      }
      set_leaf_type(subname, full_name, prev, hier_subnames);
    }
    prev = pos + 1;
  }

  if (prev < full_name.size()) {
    auto subname = full_name.substr(prev, std::string::npos);
    if (subname.back() == ']') {
      subname = subname.substr(0, subname.size() - 1);  // exclude ']'
    }
    set_leaf_type(subname, full_name, prev, hier_subnames);
  }
}

// note: Given a string with "."s and "["s in it, this
// function will be able to deconstruct it into
// DOT and SELECT nodes in an LNAST.
// note: "#" prefix need to be ready if the full_name is a register
void Inou_firrtl::CreateTupGetFromStr(Lnast& ln, Lnast_nid& parent_node, const mmap_lib::str& full_name, const Lnast_node& dest_node) {
  I(full_name.contains('.'));
  I(!dest_node.is_invalid());

  auto selc_node = ln.add_child(parent_node, Lnast_node::create_tuple_get());
  ln.add_child(selc_node, dest_node);

  std::vector<std::pair<mmap_lib::str, Inou_firrtl::Leaf_type>> hier_subnames;
  split_hier_name(full_name, hier_subnames);

  for (auto subname : hier_subnames) {
    switch (subname.second) {
      case Leaf_type::Ref: {
        ln.add_child(selc_node, Lnast_node::create_ref(subname.first));
        break;
      }
      case Leaf_type::Const_num: 
      case Leaf_type::Const_str: {
        ln.add_child(selc_node, Lnast_node::create_const(subname.first));
        break;
      }
      default: Pass::error("Unknown port type.");
    }
  }
}

void Inou_firrtl::CreateTupAddFromStr(Lnast& ln, Lnast_nid& parent_node, const mmap_lib::str& full_name, const Lnast_node& value_node) {
  I(full_name.contains('.'));

  std::vector<std::pair<mmap_lib::str, Inou_firrtl::Leaf_type>> hier_subnames;
  split_hier_name(full_name, hier_subnames);
  auto selc_node = ln.add_child(parent_node, Lnast_node::create_tuple_add());
  for (auto subname : hier_subnames) {
    switch (subname.second) {
      case Leaf_type::Ref: {
        ln.add_child(selc_node, Lnast_node::create_ref(subname.first));
        break;
      }
      case Leaf_type::Const_num: 
      case Leaf_type::Const_str: {
        ln.add_child(selc_node, Lnast_node::create_const(subname.first));
        break;
      }
      default: Pass::error("Unknown port type.");
    }
  }

  ln.add_child(selc_node, value_node);
}

/* Given an expression that may or may
 * not have hierarchy, flatten it. */
mmap_lib::str Inou_firrtl::FlattenExpression(Lnast& ln, Lnast_nid& parent_node, const firrtl::FirrtlPB_Expression& expr) {
  if (expr.has_sub_field()) {
    return mmap_lib::str::concat(FlattenExpression(ln, parent_node, expr.sub_field().expression()), ".", expr.sub_field().field());

  } else if (expr.has_sub_access()) {
    auto idx_str = ReturnExprString(ln, expr.sub_access().index(), parent_node, true);
    return mmap_lib::str::concat(FlattenExpression(ln, parent_node, expr.sub_access().expression()), ".", idx_str);

  } else if (expr.has_sub_index()) {
    return mmap_lib::str::concat(FlattenExpression(ln, parent_node, expr.sub_index().expression()), ".", expr.sub_index().index().value());

  } else if (expr.has_reference()) {
    return mmap_lib::str(expr.reference().id());

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
void Inou_firrtl::create_io_list(const firrtl::FirrtlPB_Type& type, uint8_t dir, const mmap_lib::str& port_id,
                                 std::vector<std::tuple<mmap_lib::str, uint8_t, uint32_t, bool>>& vec) {
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
      vec.emplace_back(port_id, dir, 1, true);  // intentionally put 1 signed bits, LiveHD compiler will handle clock bits later
      break;
    }
    case firrtl::FirrtlPB_Type::kBundleType: {  // Bundle type
      const auto &btype = type.bundle_type();
      for (int i = 0; i < type.bundle_type().field_size(); i++) {
        if (btype.field(i).is_flipped()) {
          uint8_t new_dir = 0;
          if (dir == 1) {  // PORT_DIRECTION_IN
            new_dir = 2;
          } else if (dir == 2) {
            new_dir = 1;
          }
          I(new_dir != 0);
          create_io_list(btype.field(i).type(), new_dir, mmap_lib::str::concat(port_id , "." , btype.field(i).id()), vec);
        } else {
          create_io_list(btype.field(i).type(), dir, mmap_lib::str::concat(port_id , "." , btype.field(i).id()), vec);
        }
      }
      break;
    }
    case firrtl::FirrtlPB_Type::kVectorType: {  // Vector type
      for (uint32_t i = 0; i < type.vector_type().size(); i++) {
        vec.emplace_back(port_id, dir, 0, false);
        create_io_list(type.vector_type().type(), dir, mmap_lib::str::concat(port_id, ".", i), vec);
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
  std::vector<std::tuple<mmap_lib::str, uint8_t, uint32_t, bool>> port_list;
  create_io_list(port.type(), port.direction(), mmap_lib::str(port.id()), port_list);

  for (auto val : port_list) {
    auto port_name = std::get<0>(val);
    auto port_dir  = std::get<1>(val);
    auto port_bits = std::get<2>(val);
    auto port_sign = std::get<3>(val);

    mmap_lib::str full_port_name;
    if (port_dir == firrtl::FirrtlPB_Port_Direction::FirrtlPB_Port_Direction_PORT_DIRECTION_IN) {
      // input_names.insert(port_name);
      record_all_input_hierarchy(port_name);

      full_port_name = mmap_lib::str::concat("$", port_name);
    } else if (port_dir == firrtl::FirrtlPB_Port_Direction::FirrtlPB_Port_Direction_PORT_DIRECTION_OUT) {
      // output_names.insert(port_name);
      record_all_output_hierarchy(port_name);
      full_port_name = mmap_lib::str::concat("%", port_name);
    } else {
      Pass::error("Found IO port {} specified with unknown direction in Protobuf message.", port_name);
    }

    if (port_bits > 0) {  // Specify __bits
      auto value_node = Lnast_node::create_const(port_bits);
      auto extension  = port_sign ? ".__sbits" : ".__ubits";
      CreateTupAddFromStr(lnast, parent_node, mmap_lib::str::concat(full_port_name, extension), value_node);
    }
  }
}


void Inou_firrtl::record_all_input_hierarchy(const mmap_lib::str &port_name) {
  std::size_t pos = port_name.size();
  while (pos != std::string::npos) {
    mmap_lib::str  port_name2 = port_name.substr(0, pos);
    input_names.insert(port_name2);
    pos = port_name2.rfind('.');
  }
}

void Inou_firrtl::record_all_output_hierarchy(const mmap_lib::str &port_name) {
  std::size_t pos = port_name.size();
  while (pos != std::string::npos) {
    auto port_name2 = port_name.substr(0, pos);
    output_names.insert(port_name2);
    pos = port_name2.rfind('.');
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
                                 const mmap_lib::str& lhs) {
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
                                 const mmap_lib::str& lhs_noprefixes) {
  // Note: here, parent_node is the "stmt" node above where this expression will go.
  I(lnast.get_data(parent_node).type.is_stmts());
  auto lhs_str = get_full_name(lhs_noprefixes, false);
  switch (rhs_expr.expression_case()) {
    case firrtl::FirrtlPB_Expression::kReference: {  // Reference
      auto tmp_rhs_str = mmap_lib::str(rhs_expr.reference().id());
      if (mport2mem.count(tmp_rhs_str)) {
        HandleRdMportUsage(lnast, parent_node, tmp_rhs_str);
      }

      mmap_lib::str rhs_str;
      if (is_invalid_table.find(tmp_rhs_str) != is_invalid_table.end()) {
        // create __last_value
        auto idx_attr_get = lnast.add_child(parent_node, Lnast_node::create_attr_get());
        auto temp_var_str = create_tmp_var();
        lnast.add_child(idx_attr_get, Lnast_node::create_ref(temp_var_str));
        lnast.add_child(idx_attr_get, Lnast_node::create_ref(tmp_rhs_str));
        lnast.add_child(idx_attr_get, Lnast_node::create_const("__last_value"));
        rhs_str = temp_var_str;
      } else {
        rhs_str = get_full_name(mmap_lib::str(rhs_expr.reference().id()), true);
      }

      // note: hiFirrtl might have bits mismatch between lhs and rhs. To solve
      // this problem, we use dp_assign to avoid this problem when lhs is a
      // pre-defined circuit component (not ___tmp variable)

      auto it = is_invalid_table.find(lhs_str);

      if (it != is_invalid_table.end()) {  // lhs is declared as invalid before
        auto idx_asg = lnast.add_child(parent_node, Lnast_node::create_assign());
        lnast.add_child(idx_asg, Lnast_node::create_ref(lhs_str));
        lnast.add_child(idx_asg, Lnast_node::create_ref(rhs_str));
        is_invalid_table.erase(lhs_str);
      } else if (lhs_str.substr(0, 1) == "_") {  // lhs is declared as kNode
        auto idx_asg = lnast.add_child(parent_node, Lnast_node::create_assign());
        lnast.add_child(idx_asg, Lnast_node::create_ref(lhs_str));
        lnast.add_child(idx_asg, Lnast_node::create_ref(rhs_str));

      } else {
        auto idx_asg = lnast.add_child(parent_node, Lnast_node::create_dp_assign());
        lnast.add_child(idx_asg, Lnast_node::create_ref(lhs_str));
        lnast.add_child(idx_asg, Lnast_node::create_ref(rhs_str));
      }
      break;
    }
    case firrtl::FirrtlPB_Expression::kUintLiteral: {  // UIntLiteral
      Lnast_nid idx_asg;
      idx_asg = lnast.add_child(parent_node, Lnast_node::create_assign());
      lnast.add_child(idx_asg, Lnast_node::create_ref(lhs_str));
      auto str_val = mmap_lib::str(rhs_expr.uint_literal().value().value());
      lnast.add_child(idx_asg, Lnast_node::create_const(str_val));
      break;
    }
    case firrtl::FirrtlPB_Expression::kSintLiteral: {  // SIntLiteral
      Lnast_nid idx_asg;
      idx_asg = lnast.add_child(parent_node, Lnast_node::create_assign());

      lnast.add_child(idx_asg, Lnast_node::create_ref(lhs_str));
      auto str_val = mmap_lib::str(rhs_expr.sint_literal().value().value());
      lnast.add_child(idx_asg, Lnast_node::create_const(str_val));
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
    case firrtl::FirrtlPB_Expression::kSubField:
    case firrtl::FirrtlPB_Expression::kSubIndex: {  // SubIndex
      HandleBundVecAcc(lnast, rhs_expr, parent_node, true, Lnast_node::create_ref(lhs_str));
      break;
    }
    case firrtl::FirrtlPB_Expression::kSubAccess: {  // SubAccess
      auto expr_name     = ReturnExprString(lnast, rhs_expr.sub_access().expression(), parent_node, true);
      auto index_name    = ReturnExprString(lnast, rhs_expr.sub_access().index(), parent_node, true);
      auto temp_var_name = create_tmp_var();

      auto idx_select = lnast.add_child(parent_node, Lnast_node::create_tuple_get());
      lnast.add_child(idx_select, Lnast_node::create_ref(temp_var_name));
      AttachExprStrToNode(lnast, expr_name, idx_select);
      AttachExprStrToNode(lnast, index_name, idx_select);

      auto idx_asg = lnast.add_child(parent_node, Lnast_node::create_assign());
      lnast.add_child(idx_asg, Lnast_node::create_ref(lhs_str));
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
mmap_lib::str Inou_firrtl::ReturnExprString(Lnast& lnast, const firrtl::FirrtlPB_Expression& expr, Lnast_nid& parent_node, const bool is_rhs, const Lnast_node value_node) {
  I(lnast.get_data(parent_node).type.is_stmts());

  mmap_lib::str expr_string = "";
  switch (expr.expression_case()) {
    case firrtl::FirrtlPB_Expression::kReference: {  // Reference
      expr_string = get_full_name(mmap_lib::str(expr.reference().id()), is_rhs);
      break;
    }
    case firrtl::FirrtlPB_Expression::kUintLiteral: {  // UIntLiteral
      expr_string = mmap_lib::str::concat(expr.uint_literal().value().value(), "ubits", expr.uint_literal().width().value());
      break;
    }
    case firrtl::FirrtlPB_Expression::kSintLiteral: {  // SIntLiteral
      expr_string = mmap_lib::str::concat(expr.sint_literal().value().value(), "sbits", expr.uint_literal().width().value());
      /* expr_string = expr.sint_literal().value().value(); */
      break;
    }
    case firrtl::FirrtlPB_Expression::kValidIf: {  // ValidIf
      expr_string = create_tmp_var();
      HandleValidIfAssign(lnast, expr, parent_node, expr_string);
      break;
    }
    case firrtl::FirrtlPB_Expression::kMux: {  // Mux
      expr_string = create_tmp_var();
      HandleMuxAssign(lnast, expr, parent_node, expr_string);
      break;
    }
    case firrtl::FirrtlPB_Expression::kSubField:     // SubField
    case firrtl::FirrtlPB_Expression::kSubIndex:     // SubIndex
    case firrtl::FirrtlPB_Expression::kSubAccess: {  // SubAccess
      if (is_rhs) {
        I(value_node.is_invalid());
        auto tmp_sv = create_tmp_var();
        HandleBundVecAcc(lnast, expr, parent_node, true, Lnast_node::create_ref(tmp_sv));
        expr_string = tmp_sv;
      } else {
        I(!value_node.is_invalid());
        expr_string = "__INVALID__";
        HandleBundVecAcc(lnast, expr, parent_node, false, value_node);
      }
      break;
    }
    case firrtl::FirrtlPB_Expression::kPrimOp: {  // PrimOp
      // This case is special. We need to create a set of nodes for it and return the lhs of that node.
      expr_string = create_tmp_var();
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
void Inou_firrtl::AttachExprStrToNode(Lnast& lnast, const mmap_lib::str &access_str, Lnast_nid& parent_node) {
  I(!lnast.get_data(parent_node).type.is_stmts());

  auto first_char = access_str[0];
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
//
void Inou_firrtl::setup_register_q_pin(Lnast& lnast, Lnast_nid& parent_node, const firrtl::FirrtlPB_Statement& stmt) {
  auto flop_qpin_var = mmap_lib::str::concat("_#_", stmt.register_().id(), "_q");
  auto idx_asg2      = lnast.add_child(parent_node, Lnast_node::create_assign());
  lnast.add_child(idx_asg2, Lnast_node::create_ref(flop_qpin_var));
  lnast.add_child(idx_asg2, Lnast_node::create_ref(mmap_lib::str::concat("#", stmt.register_().id())));

  reg2qpin.insert_or_assign(mmap_lib::str(stmt.register_().id()), flop_qpin_var);
}

void Inou_firrtl::declare_register(Lnast& lnast, Lnast_nid& parent_node, const firrtl::FirrtlPB_Statement& stmt) {
  auto idx_attget         = lnast.add_child(parent_node, Lnast_node::create_attr_get());
  auto full_register_name = mmap_lib::str::concat("#", stmt.register_().id());
  auto tmp_var_str        = create_tmp_var();
  lnast.add_child(idx_attget, Lnast_node::create_ref(tmp_var_str));
  // lnast.add_child(idx_attget, Lnast_node::create_ref(lnast.add_string(full_register_name)));
  lnast.add_child(idx_attget, Lnast_node::create_ref(full_register_name));
  lnast.add_child(idx_attget, Lnast_node::create_const("__create_flop"));

  auto idx_asg = lnast.add_child(parent_node, Lnast_node::create_assign());
  // lnast.add_child(idx_asg, Lnast_node::create_ref(lnast.add_string(full_register_name)));
  lnast.add_child(idx_asg, Lnast_node::create_ref(full_register_name));
  lnast.add_child(idx_asg, Lnast_node::create_ref(tmp_var_str));

  // auto flop_qpin_var = mmap_lib::str::concat("_._", stmt.register_().id(), "_q"));
  // auto idx_asg2 = lnast.add_child(parent_node, Lnast_node::create_assign());
  // lnast.add_child(idx_asg2, Lnast_node::create_ref(flop_qpin_var));
  // lnast.add_child(idx_asg2, Lnast_node::create_ref(lnast.add_string(full_register_name)));

  // reg2qpin.insert_or_assign(stmt.register_().id(), flop_qpin_var);
}

void Inou_firrtl::setup_register_reset_init(Lnast& lnast, Lnast_nid& parent_node, const mmap_lib::str& reg_raw_name,
                                            const firrtl::FirrtlPB_Expression& resete, const firrtl::FirrtlPB_Expression& inite) {
  bool tied0_reset = false;
  auto resete_case = resete.expression_case();

  Lnast_node value_node;

  if (resete_case == firrtl::FirrtlPB_Expression::kUintLiteral || resete_case == firrtl::FirrtlPB_Expression::kSintLiteral) {
    auto str_val = mmap_lib::str(resete.uint_literal().value().value());
    value_node   = Lnast_node::create_const(str_val);
    if (str_val == "0")
      tied0_reset = true;
  } else if (resete_case == firrtl::FirrtlPB_Expression::kReference) {
    auto ref_str = get_full_name(mmap_lib::str(resete.reference().id()), true);
    value_node   = Lnast_node::create_ref(ref_str);
  }

  if (!value_node.is_invalid())
    CreateTupAddFromStr(lnast, parent_node, mmap_lib::str::concat("#", reg_raw_name, ".__reset"), value_node);

  if (tied0_reset) {
    return;
  }

  Lnast_node initial_node;

  auto inite_case = inite.expression_case();
  if (inite_case == firrtl::FirrtlPB_Expression::kUintLiteral || inite_case == firrtl::FirrtlPB_Expression::kSintLiteral) {
    auto str_val = mmap_lib::str(inite.uint_literal().value().value());
    initial_node = Lnast_node::create_const(str_val);
  } else if (inite_case == firrtl::FirrtlPB_Expression::kReference) {
    auto ref_str = mmap_lib::str(inite.reference().id());
    // initial_node = Lnast_node::create_ref(lnast.add_string(ref_str));
    auto empty_tup_add_op  = lnast.add_child(parent_node, Lnast_node::create_tuple_add());
    auto empty_tup_add_var = Lnast_node::create_ref(create_tmp_var());
    lnast.add_child(empty_tup_add_op, empty_tup_add_var);

    auto get_mask_op = lnast.add_child(parent_node, Lnast_node::create_get_mask());
    initial_node     = Lnast_node::create_ref(create_tmp_var());
    lnast.add_child(get_mask_op, initial_node);
    lnast.add_child(get_mask_op, Lnast_node::create_ref(ref_str));
    lnast.add_child(get_mask_op, empty_tup_add_var);
  }

  if (!initial_node.is_invalid())
    CreateTupAddFromStr(lnast, parent_node, mmap_lib::str::concat("#", reg_raw_name, ".__initial"), initial_node);
}

void Inou_firrtl::ListStatementInfo(Lnast& lnast, const firrtl::FirrtlPB_Statement& stmt, Lnast_nid& parent_node) {
  switch (stmt.statement_case()) {
    case firrtl::FirrtlPB_Statement::kWire: {
      auto stmt_wire_id = mmap_lib::str(stmt.wire().id());
      wire_names.insert(stmt_wire_id);
      init_wire_dots(lnast, stmt.wire().type(), stmt_wire_id, parent_node);
      break;
    }
    case firrtl::FirrtlPB_Statement::kRegister: {
      // no matter it's scalar or tuple register, we only create for the top hierarchical variable,
      // the flop expansion is handled at lgraph
      setup_register_bits(lnast, stmt.register_().type(), mmap_lib::str::concat("#", stmt.register_().id()), parent_node);

      setup_register_reset_init(lnast, parent_node, mmap_lib::str(stmt.register_().id()), stmt.register_().reset(), stmt.register_().init());
      declare_register(lnast, parent_node, stmt);
      setup_register_q_pin(lnast, parent_node, stmt);
      break;
    }
    case firrtl::FirrtlPB_Statement::kMemory: {
      I(false, "never happen in chirrtl");
      break;
    }
    case firrtl::FirrtlPB_Statement::kCmemory: {
      memory_names.insert(mmap_lib::str(stmt.cmemory().id()));
      // fmt::print("DEBUG0 cmemory:{}\n", stmt.cmemory().id());
      InitCMemory(lnast, parent_node, stmt.cmemory());
      break;
    }
    case firrtl::FirrtlPB_Statement::kMemoryPort: {
      HandleMportDeclaration(lnast, parent_node, stmt.memory_port());
      break;
    }
    case firrtl::FirrtlPB_Statement::kInstance: {  // Instance -- creating an instance of a module inside another
      create_module_inst(lnast, stmt.instance(), parent_node);
      break;
    }
    case firrtl::FirrtlPB_Statement::kNode: {  // Node -- nodes are simply named intermediates in a circuit
      InitialExprAdd(lnast, stmt.node().expression(), parent_node, mmap_lib::str(stmt.node().id()));
      break;
    }
    case firrtl::FirrtlPB_Statement::kWhen: {
      auto cond_str = ReturnExprString(lnast, stmt.when().predicate(), parent_node, true);
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
    case firrtl::FirrtlPB_Statement::kStop: 
    case firrtl::FirrtlPB_Statement::kPrintf: 
    case firrtl::FirrtlPB_Statement::kSkip: {  // Skip
      // Nothing to do.
      break;
    }
    case firrtl::FirrtlPB_Statement::kConnect: {  // Connect
      auto& lhs_expr      = stmt.connect().location();
      auto& rhs_expr      = stmt.connect().expression();
      auto  lhs_expr_case = stmt.connect().location().expression_case();
      // FIXME->sh: might need to extend the special cases to lhs_expr_case != firrtl::FirrtlPB_Expression::kNode

      bool is_lhs_tuple
          = (lhs_expr_case == firrtl::FirrtlPB_Expression::kSubField || lhs_expr_case == firrtl::FirrtlPB_Expression::kSubAccess
             || lhs_expr_case == firrtl::FirrtlPB_Expression::kSubIndex);

      if (is_lhs_tuple) {
        auto rhs_expr_case = stmt.connect().expression().expression_case();
        bool is_rhs_mux    = rhs_expr_case == firrtl::FirrtlPB_Expression::kMux;

        mmap_lib::str tmp_var_string;
        if (is_rhs_mux) {
          tmp_var_string = create_tmp_mut_var();  // must do SSA afterwards
        } else {
          tmp_var_string = create_tmp_var();  // No SSA
        }

        InitialExprAdd(lnast, rhs_expr, parent_node, tmp_var_string);

        // (2) create the lhs dot and lhs <- rhs assignment
        ReturnExprString(lnast, lhs_expr, parent_node, false, Lnast_node::create_ref(tmp_var_string));
      } else {
        auto lhs_str = ReturnExprString(lnast, lhs_expr, parent_node, false, Lnast_node::create_invalid());
        InitialExprAdd(lnast, rhs_expr, parent_node, lhs_str);

        // FIXME: renau (is commenting this right)
        // if (mport2mem.count(lhs_str)) { //lhs is rd_mport (and must be a scalar rd_mport)
        //   HandleWrMportUsage(lnast, parent_node, lhs_str);
        // }
      }
      break;
    }
    case firrtl::FirrtlPB_Statement::kPartialConnect: {
      // Pass::warn("FIRRTL partial connects are error-prone on this interface. Be careful using them.\n");
      auto& lhs_expr      = stmt.partial_connect().location();
      auto& rhs_expr      = stmt.partial_connect().expression();
      auto  lhs_expr_case = stmt.partial_connect().location().expression_case();
      // FIXME->sh: might need to extend the special cases to lhs_expr_case != firrtl::FirrtlPB_Expression::kNode

      bool is_lhs_tuple
          = (lhs_expr_case == firrtl::FirrtlPB_Expression::kSubField || lhs_expr_case == firrtl::FirrtlPB_Expression::kSubAccess
             || lhs_expr_case == firrtl::FirrtlPB_Expression::kSubIndex);

      if (is_lhs_tuple) {
        auto rhs_expr_case = stmt.partial_connect().expression().expression_case();
        bool is_rhs_mux    = rhs_expr_case == firrtl::FirrtlPB_Expression::kMux;

        mmap_lib::str tmp_var_string;
        if (is_rhs_mux) {
          tmp_var_string = create_tmp_mut_var();  // must do SSA afterwards
        } else {
          tmp_var_string = create_tmp_var();  // No SSA
        }

        InitialExprAdd(lnast, rhs_expr, parent_node, tmp_var_string);

        // (2) create the lhs dot and lhs <- rhs assignment
        ReturnExprString(lnast, lhs_expr, parent_node, false, Lnast_node::create_ref(tmp_var_string));
      } else {
        auto lhs_str = ReturnExprString(lnast, lhs_expr, parent_node, false, Lnast_node::create_invalid());
        InitialExprAdd(lnast, rhs_expr, parent_node, lhs_str);

        // FIXME: renau (is commenting this right)
        // if (mport2mem.count(lhs_str)) { //lhs is rd_mport (and must be a scalar rd_mport)
        //   HandleWrMportUsage(lnast, parent_node, lhs_str);
        // }
      }
      break;
    }
    case firrtl::FirrtlPB_Statement::kIsInvalid: {
      auto id = mmap_lib::str(stmt.is_invalid().expression().reference().id());
      auto it = wire_names.find(id);
      if (it != wire_names.end()) {
        is_invalid_table.insert(id);
      }

      break;
    }
    case firrtl::FirrtlPB_Statement::kAttach: {
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

void Inou_firrtl::FinalMemInterfaceAssign(Lnast& lnast, Lnast_nid& parent_node) {
  for (auto& mem_name : memory_names) {
    // try to recover tuplpe field from the mem_din
    auto& idx_initialize_stmts = mem2initial_idx[mem_name];

    for (auto& it : mem2rd_mports[mem_name]) {
      auto mport_name          = it.first;
      auto cnt_of_rd_mport     = it.second;
      auto one_of_wr_mport_cnt = mem2one_wr_mport[mem_name];

      auto idx_tg        = lnast.add_child(idx_initialize_stmts, Lnast_node::create_tuple_get());
      auto temp_var_name = create_tmp_var();
      lnast.add_child(idx_tg, Lnast_node::create_ref(temp_var_name));
      lnast.add_child(idx_tg, Lnast_node::create_ref(mmap_lib::str::concat(mem_name, "_din")));
      lnast.add_child(idx_tg, Lnast_node::create_const(one_of_wr_mport_cnt));

      auto idx_asg = lnast.add_child(idx_initialize_stmts, Lnast_node::create_assign());
      lnast.add_child(idx_asg, Lnast_node::create_ref(mport_name));
      lnast.add_child(idx_asg, Lnast_node::create_ref(temp_var_name));

      // FIXME->sh: might need get the __last_value?
      auto idx_tg2        = lnast.add_child(idx_initialize_stmts, Lnast_node::create_tuple_get());
      auto temp_var_name2 = create_tmp_var();
      lnast.add_child(idx_tg2, Lnast_node::create_ref(temp_var_name2));
      lnast.add_child(idx_tg2, Lnast_node::create_ref(mmap_lib::str::concat(mem_name, "_res")));
      lnast.add_child(idx_tg2, Lnast_node::create_const(cnt_of_rd_mport));

      auto idx_asg2 = lnast.add_child(idx_initialize_stmts, Lnast_node::create_dp_assign());
      lnast.add_child(idx_asg2, Lnast_node::create_ref(mport_name));
      lnast.add_child(idx_asg2, Lnast_node::create_ref(temp_var_name2));
    }

    std::vector<mmap_lib::str> tmp_flattened_fields_per_port;
    for (int pcnt = 0; pcnt <= mem2port_cnt[mem_name]; pcnt++) {
      // fmt::print("DEBUG3 @port_cnt:{}\n", pcnt);
      auto gmask_tmp_var_str = create_tmp_var();
      auto tg_tmp_var_str    = create_tmp_var();
      auto ta_tmp_var_str    = create_tmp_var();
      auto idx_tg            = lnast.add_child(parent_node, Lnast_node::create_tuple_get());
      lnast.add_child(idx_tg, Lnast_node::create_ref(tg_tmp_var_str));
      lnast.add_child(idx_tg, Lnast_node::create_ref(mmap_lib::str::concat(mem_name, "_din")));
      lnast.add_child(idx_tg, Lnast_node::create_const(pcnt));

      auto idx_empty_ta = lnast.add_child(parent_node, Lnast_node::create_tuple_add());
      lnast.add_child(idx_empty_ta, Lnast_node::create_ref(ta_tmp_var_str));

      auto idx_gmask = lnast.add_child(parent_node, Lnast_node::create_get_mask());
      lnast.add_child(idx_gmask, Lnast_node::create_ref(gmask_tmp_var_str));
      lnast.add_child(idx_gmask, Lnast_node::create_ref(tg_tmp_var_str));
      lnast.add_child(idx_gmask, Lnast_node::create_ref(ta_tmp_var_str));
      tmp_flattened_fields_per_port.emplace_back(gmask_tmp_var_str);
    }
    // note:  __F33 = (tmp0, tmp1, ..., tmp_pcnt); din = __F33
    auto idx_final_mem_din_ta = lnast.add_child(parent_node, Lnast_node::create_tuple_add());
    auto final_ta_tmp_var_str = create_tmp_var();
    lnast.add_child(idx_final_mem_din_ta, Lnast_node::create_ref(final_ta_tmp_var_str));

    for (auto& e : tmp_flattened_fields_per_port) {
      lnast.add_child(idx_final_mem_din_ta, Lnast_node::create_ref(e));
    }

    auto idx_ta_margs = lnast.add_child(parent_node, Lnast_node::create_tuple_add());
    auto temp_var_str = create_tmp_var();
    lnast.add_child(idx_ta_margs, Lnast_node::create_ref(temp_var_str));

    auto idx_asg_addr = lnast.add_child(idx_ta_margs, Lnast_node::create_assign());
    lnast.add_child(idx_asg_addr, Lnast_node::create_const("addr"));
    lnast.add_child(idx_asg_addr, Lnast_node::create_ref(mmap_lib::str::concat(mem_name, "_addr")));

    auto idx_asg_clock = lnast.add_child(idx_ta_margs, Lnast_node::create_assign());
    lnast.add_child(idx_asg_clock, Lnast_node::create_const("clock"));
    lnast.add_child(idx_asg_clock, Lnast_node::create_ref(mmap_lib::str::concat(mem_name, "_clock")));

    auto idx_asg_din = lnast.add_child(idx_ta_margs, Lnast_node::create_assign());
    lnast.add_child(idx_asg_din, Lnast_node::create_const("din"));
    lnast.add_child(idx_asg_din, Lnast_node::create_ref(final_ta_tmp_var_str));

    // auto idx_asg_din = lnast.add_child(idx_ta_margs, Lnast_node::create_assign());
    // lnast.add_child(idx_asg_din, Lnast_node::create_const(lnast.add_string(std::string("din"))));
    // lnast.add_child(idx_asg_din, Lnast_node::create_ref(mmap_lib::str::concat(mem_name, "_din"))));

    auto idx_asg_enable = lnast.add_child(idx_ta_margs, Lnast_node::create_assign());
    lnast.add_child(idx_asg_enable, Lnast_node::create_const("enable"));
    lnast.add_child(idx_asg_enable, Lnast_node::create_ref(mmap_lib::str::concat(mem_name, "_enable")));

    auto idx_asg_fwd = lnast.add_child(idx_ta_margs, Lnast_node::create_assign());
    lnast.add_child(idx_asg_fwd, Lnast_node::create_const("fwd"));
    lnast.add_child(idx_asg_fwd, Lnast_node::create_ref(mmap_lib::str::concat(mem_name, "_fwd")));

    auto idx_asg_lat = lnast.add_child(idx_ta_margs, Lnast_node::create_assign());
    lnast.add_child(idx_asg_lat, Lnast_node::create_const("type"));
    lnast.add_child(idx_asg_lat, Lnast_node::create_ref(mmap_lib::str::concat(mem_name, "_type")));

    auto idx_asg_wensize = lnast.add_child(idx_ta_margs, Lnast_node::create_assign());
    lnast.add_child(idx_asg_wensize, Lnast_node::create_const("wensize"));
    lnast.add_child(idx_asg_wensize, Lnast_node::create_ref(mmap_lib::str::concat(mem_name, "_wensize")));

    auto idx_asg_size = lnast.add_child(idx_ta_margs, Lnast_node::create_assign());
    lnast.add_child(idx_asg_size, Lnast_node::create_const("size"));
    lnast.add_child(idx_asg_size, Lnast_node::create_ref(mmap_lib::str::concat(mem_name, "_size")));

    auto idx_asg_rdport = lnast.add_child(idx_ta_margs, Lnast_node::create_assign());
    lnast.add_child(idx_asg_rdport, Lnast_node::create_const("rdport"));
    lnast.add_child(idx_asg_rdport, Lnast_node::create_ref(mmap_lib::str::concat(mem_name, "_rdport")));

    auto idx_asg_margs = lnast.add_child(parent_node, Lnast_node::create_assign());
    lnast.add_child(idx_asg_margs, Lnast_node::create_ref(mmap_lib::str::concat(mem_name, "_interface_args")));
    lnast.add_child(idx_asg_margs, Lnast_node::create_ref(temp_var_str));
  }
}

//--------------Modules/Circuits--------------------
// Create basis of LNAST tree. Set root to "top" and have "stmts" be top's child.
void Inou_firrtl::ListUserModuleInfo(Eprp_var& var, const firrtl::FirrtlPB_Module& fmodule, const mmap_lib::str& file_name) {

#ifndef NDEBUG
  fmt::print("Module (user): {}\n", fmodule.user_module().id());
#endif
  std::unique_ptr<Lnast> lnast = std::make_unique<Lnast>(mmap_lib::str(fmodule.user_module().id()), file_name);

  const firrtl::FirrtlPB_Module_UserModule& user_module = fmodule.user_module();

  lnast->set_root(Lnast_node::create_top());
  auto idx_stmts = lnast->add_child(mmap_lib::Tree_index::root(), Lnast_node::create_stmts());

  // Iterate over I/O of the module.
  // FIXME->sh: maybe only this method needs mutex protection if all other global object is just "READ" in the ListStatementInfo?
  //            -> no, as far as I know, at least Memory interface will be "WRITEN" in the ListStatementInfo
  for (int i = 0; i < user_module.port_size(); i++) {
    const firrtl::FirrtlPB_Port& port = user_module.port(i);
    ListPortInfo(*lnast, port, idx_stmts);
  }

  // Iterate over statements of the module.
  for (int j = 0; j < user_module.statement_size(); j++) {
    const firrtl::FirrtlPB_Statement& stmt = user_module.statement(j);
    // PreCheckForMem(*lnast, stmt, idx_stmts);
    ListStatementInfo(*lnast, stmt, idx_stmts);
  }

  FinalMemInterfaceAssign(*lnast, idx_stmts);
  var.add(std::move(lnast));
}

void Inou_firrtl::ListModuleInfo(Eprp_var& var, const firrtl::FirrtlPB_Module& fmodule, const mmap_lib::str& file_name) {
  if (fmodule.has_external_module()) {
    GrabExtModuleInfo(fmodule.external_module());
  } else if (fmodule.has_user_module()) {
    ListUserModuleInfo(var, fmodule, file_name);
  } else {
    Pass::error("Module not set.");
  }
}

void Inou_firrtl::PopulateAllModsIO(Eprp_var& var, const firrtl::FirrtlPB_Circuit& circuit, const mmap_lib::str& file_name) {
  for (int i = 0; i < circuit.module_size(); i++) {
    // std::vector<std::pair<std::string, uint8_t>> vec;
    if (circuit.module(i).has_external_module()) {
      /* NOTE->hunter: This is a Verilog blackbox. If we want to link it, it'd have to go through either V->LG
       * or V->LN->LG. I will create a Sub_Node in case the Verilog isn't provided. */
      auto     module_i_external_module_id =  mmap_lib::str(circuit.module(i).external_module().id());
      auto     sub     = AddModToLibrary(var, module_i_external_module_id, file_name);
      uint64_t inp_pos = 0;
      uint64_t out_pos = 0;
      for (int j = 0; j < circuit.module(i).external_module().port_size(); j++) {
        auto port = circuit.module(i).external_module().port(j);
        AddPortToMap(module_i_external_module_id, port.type(), port.direction(), mmap_lib::str(port.id()), sub, inp_pos, out_pos);
      }
      continue;
    } else if (circuit.module(i).has_user_module()) {
      auto     module_i_user_module_id =  mmap_lib::str(circuit.module(i).user_module().id());
      auto     sub     = AddModToLibrary(var, module_i_user_module_id, file_name);
      uint64_t inp_pos = 0;
      uint64_t out_pos = 0;
      for (int j = 0; j < circuit.module(i).user_module().port_size(); j++) {
        auto port = circuit.module(i).user_module().port(j);
        AddPortToMap(module_i_user_module_id, port.type(), port.direction(), mmap_lib::str(port.id()), sub, inp_pos, out_pos);
      }
    } else {
      Pass::error("Module not set.");
    }
  }
}

Sub_node Inou_firrtl::AddModToLibrary(Eprp_var& var, const mmap_lib::str& mod_name, const mmap_lib::str& file_name) {
  mmap_lib::str fpath;
  if (var.has_label("path")) {
    fpath = var.get("path");
  } else {
    fpath = "lgdb";
  }

  auto* library = Graph_library::instance(fpath);
  auto& sub     = library->reset_sub(mod_name, file_name);
  return sub;
}

/* Used to populate Sub_Nodes so that when Lgraphs are constructed,
 * all the Lgraphs will be able to populate regardless of order. */
void Inou_firrtl::AddPortToSub(Sub_node& sub, uint64_t& inp_pos, uint64_t& out_pos, const mmap_lib::str& port_id,
                               const uint8_t& dir) {
  if (dir == 1) {                // PORT_DIRECTION_IN
    sub.add_input_pin(port_id);  //, inp_pos);
    inp_pos++;
  } else {
    sub.add_output_pin(port_id);  //, out_pos);
    out_pos++;
  }
}

void Inou_firrtl::AddPortToMap(const mmap_lib::str& mod_id, const firrtl::FirrtlPB_Type& type, uint8_t dir,
                               const mmap_lib::str& port_id, Sub_node& sub, uint64_t& inp_pos, uint64_t& out_pos) {
  switch (type.type_case()) {
    case firrtl::FirrtlPB_Type::kUintType:     // UInt type
    case firrtl::FirrtlPB_Type::kSintType:     // SInt type
    case firrtl::FirrtlPB_Type::kResetType:    // Reset type
    case firrtl::FirrtlPB_Type::kClockType: {  // Clock type
      AddPortToSub(sub, inp_pos, out_pos, port_id, dir);
      mod_to_io_dir_map[std::make_pair(mod_id, port_id)] = dir;
      // mod_to_io_map[mod_id].insert({port_id, 1, dir, false});
      break;
    }
    case firrtl::FirrtlPB_Type::kAsyncResetType: {  // AsyncReset type
      AddPortToSub(sub, inp_pos, out_pos, port_id, dir);
      mod_to_io_dir_map[std::make_pair(mod_id, port_id)] = dir;
      // mod_to_io_map[mod_id].insert({port_id, 1, dir, false});
      async_rst_names.insert(port_id);
      break;
    }
    case firrtl::FirrtlPB_Type::kBundleType: {  // Bundle type
      auto &btype = type.bundle_type();
      for (int i = 0; i < type.bundle_type().field_size(); i++) {
        if (btype.field(i).is_flipped()) {
          uint8_t new_dir = 0;
          if (dir == 1) {  // PORT_DIRECTION_IN
            new_dir = 2;
          } else if (dir == 2) {
            new_dir = 1;
          }
          I(new_dir != 0);
          AddPortToMap(mod_id, btype.field(i).type(), new_dir, mmap_lib::str::concat(port_id, ".", btype.field(i).id()), sub, inp_pos, out_pos);
        } else {
          AddPortToMap(mod_id, btype.field(i).type(), dir, mmap_lib::str::concat(port_id, ".", btype.field(i).id()), sub, inp_pos, out_pos);
        }
      }
      break;
    }
    case firrtl::FirrtlPB_Type::kVectorType: {  // Vector type
      mod_to_io_dir_map[std::make_pair(mod_id, port_id)] = dir;
      for (uint32_t i = 0; i < type.vector_type().size(); i++) {
        AddPortToMap(mod_id, type.vector_type().type(), dir, mmap_lib::str::concat(port_id, ".", i), sub, inp_pos, out_pos);
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
void Inou_firrtl::GrabExtModuleInfo(const firrtl::FirrtlPB_Module_ExternalModule &emod) {
  // Figure out all of mods IO and their respective bw + dir.
  std::vector<std::tuple<mmap_lib::str, uint8_t, uint32_t, bool>> port_list;  // Terms are as follows: name, direction, # of bits, sign.
  for (int i = 0; i < emod.port_size(); i++) {
    const auto &port = emod.port(i);
    create_io_list(port.type(), port.direction(), mmap_lib::str(port.id()), port_list);
  }

  // Figure out what the value for each parameter is, add to map.
  for (int j = 0; j < emod.parameter_size(); j++) {
    mmap_lib::str param_str;
    switch (emod.parameter(j).value_case()) {
      case firrtl::FirrtlPB_Module_ExternalModule_Parameter::kInteger:
        param_str = ConvertBigIntToStr(emod.parameter(j).integer());
        break;
      case firrtl::FirrtlPB_Module_ExternalModule_Parameter::kDouble:
        param_str = emod.parameter(j).double_();
        break;
      case firrtl::FirrtlPB_Module_ExternalModule_Parameter::kString: 
        param_str = mmap_lib::str(emod.parameter(j).string()); 
        break;
      case firrtl::FirrtlPB_Module_ExternalModule_Parameter::kRawString: 
        param_str = mmap_lib::str(emod.parameter(j).raw_string()); 
        break;
      default: I(false);
    }
    emod_to_param_map[mmap_lib::str(emod.defined_name())].insert({mmap_lib::str(emod.parameter(j).id()), param_str});
  }

  // Add them to the map to let us know what ports exist in this module.
  for (const auto &elem : port_list) {
    mod_to_io_dir_map[std::make_pair(mmap_lib::str(emod.defined_name()), mmap_lib::str(std::get<0>(elem)))] = std::get<1>(elem);
    // mod_to_io_map[emod.defined_name()].insert({std::get<0>(elem), std::get<2>(elem), std::get<1>(elem), std::get<3>(elem)});
  }
}

mmap_lib::str Inou_firrtl::ConvertBigIntToStr(const firrtl::FirrtlPB_BigInt &bigint) {
  if (bigint.value().size() == 0) {
    return mmap_lib::str("0b0");
  }

  mmap_lib::str bigint_val("0b");
  for (char bigint_char : bigint.value()) {
    mmap_lib::str bit_str;
    for (int j = 0; j < 8; j++) {
      if (bigint_char % 2) {
        bit_str = bit_str.prepend('1');
      } else {
        bit_str = bit_str.prepend('0');
      }
      bigint_char >>= 1;
    }
    bigint_val = mmap_lib::str::concat(bigint_val, bit_str);
  }

  return bigint_val;
}



void Inou_firrtl::IterateModules(Eprp_var& var, const firrtl::FirrtlPB_Circuit& circuit, const mmap_lib::str& file_name) {
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
    memory_names.clear();
    async_rst_names.clear();
    mport_usage_visited.clear();

    reg2qpin.clear();
    inst_to_mod_map.clear();
    reg_name2rst_init_expr.clear();
    mem2port_cnt.clear();
    mem2wensize.clear();
    mem2initial_idx.clear();
    mport2mem.clear();
    mem2din_fields.clear();

    ListModuleInfo(var, circuit.module(i), file_name);
  }
}

// Iterate over every FIRRTL circuit (design), each circuit can contain multiple modules.
void Inou_firrtl::IterateCircuits(Eprp_var& var, const firrtl::FirrtlPB& firrtl_input, const mmap_lib::str& file_name) {
  for (int i = 0; i < firrtl_input.circuit_size(); i++) {
    mod_to_io_dir_map.clear();
    // mod_to_io_map.clear();
    emod_to_param_map.clear();

    const firrtl::FirrtlPB_Circuit& circuit = firrtl_input.circuit(i);
    IterateModules(var, circuit, file_name);
  }
}

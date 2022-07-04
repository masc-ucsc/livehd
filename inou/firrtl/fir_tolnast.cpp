//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

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
#include "perf_tracing.hpp"
#include "thread_pool.hpp"

using google::protobuf::util::TimeUtil;

/* For help understanding FIRRTL/Protobuf:
 * 1) Semantics regarding FIRRTL language:
 * www2.eecs.berkeley.edu/Pubs/TechRpts/2019/EECS-2019-168.pdf
 * 2) Structure of FIRRTL Protobuf file:
 * github.com/freechipsproject/firrtl/blob/master/src/main/proto/firrtl.proto */

void Inou_firrtl::to_lnast(Eprp_var& var) {
  TRACE_EVENT("inou", "fir_tolnast:all");
  Lbench b("inou.fir_tolnast:all");

  Inou_firrtl p(var);

  if (var.has_label("files")) {
    auto files = var.get("files");
    for (const auto& f_sv : absl::StrSplit(files, ',')) {
      std::string f(f_sv);

      fmt::print("FILE: {}\n", f);
      // FIXME: even I make the PB object static, and it does get destructed after the
      // to_lnast() pass. The lbench still last untill the PB object really destroied,
      // is it a bug in lbench?
      // static firrtl::FirrtlPB firrtl_input;
      firrtl::FirrtlPB firrtl_input;
      std::fstream     input(f.c_str(), std::ios::in | std::ios::binary);
      if (!firrtl_input.ParseFromIstream(&input)) {
        Pass::error("Failed to parse FIRRTL from protobuf format: {}", f);
        return;
      }

      p.iterate_circuits(var, firrtl_input, f);
    }
  } else {
    fmt::print("No file provided. This requires a file input.\n");
    return;
  }

  // Optional:  Delete all global objects allocated by libprotobuf.
  // FIXME: dispatch to a new thread to overlap with ln2lg
  //        or defer to the end of lcompiler
  google::protobuf::ShutdownProtobufLibrary();
}

//----------------Helper Functions--------------------------
std::string Inou_firrtl_module::create_tmp_var() { return absl::StrCat("___F", ++tmp_var_cnt); }

std::string Inou_firrtl_module::create_tmp_mut_var() { return absl::StrCat("_._M", ++dummy_expr_node_cnt); }

/* Determine if 'name' refers to any IO/reg/etc... If it does,
 * add the appropriate symbol and return the flattened version*/
std::string Inou_firrtl_module::name_prefix_modifier_flattener(std::string_view name, const bool is_rhs) {
  std::string flattened_name = std::string{name};
  std::replace(flattened_name.begin(), flattened_name.end(), '.', '_');

  if (output_names.count(name)) {
    return absl::StrCat("%", flattened_name);
  } else if (input_names.count(name)) {
    return absl::StrCat("$", flattened_name);
  } else if (reg2qpin.count(flattened_name)) {  // check if the register exists
    I(reg2qpin[flattened_name].substr(0, 3) == "_#_");
    if (is_rhs) {
      return reg2qpin[flattened_name];
    } else {
      return absl::StrCat("#", flattened_name);
    }
  } else {
    return flattened_name;
  }
}

std::string Inou_firrtl_module::get_runtime_idx_field_name(const firrtl::FirrtlPB_Expression &expr) {
  if (expr.has_sub_field()) {
    return get_runtime_idx_field_name(expr.sub_field().expression());
  } else if (expr.has_sub_access()) {
    bool dummy = false;
    auto idx_str = get_expr_hier_name(expr.sub_access().index(), dummy);
    return idx_str;
  } else if (expr.has_sub_index()) {
    return get_runtime_idx_field_name(expr.sub_index().expression());
  } else if (expr.has_reference()) {
    I(false);
    return "";
  } else {
    I(false);
    return "";
  }
}

void Inou_firrtl_module::handle_lhs_runtime_idx(Lnast &lnast, Lnast_nid &parent_node, std::string_view hier_name_l_ori, std::string_view hier_name_r_ori, const firrtl::FirrtlPB_Expression &lhs_expr) {
  std::string rhs_flattened_name = name_prefix_modifier_flattener(hier_name_r_ori, true);
  // fmt::print("DEBUG YYY hier_name_r_ori:{} rhs_flattened_name:{}\n", hier_name_r_ori, rhs_flattened_name);
  // (1) get the runtime idx tuple field
  std::string rtidx_str = get_runtime_idx_field_name(lhs_expr);
  rtidx_str = name_prefix_modifier_flattener(rtidx_str, true);


  bool is_2d_vector = false;
  std::string leaf_field_name = "";
  auto pos = hier_name_l_ori.find(".."); 
  std::string vec_name;
  if (pos != std::string::npos) {
    is_2d_vector = true;
    leaf_field_name = hier_name_l_ori.substr(pos + 2);
		vec_name = hier_name_l_ori.substr(0, pos);
  }
  

  auto pos2 = hier_name_l_ori.rfind('.');
  if (pos2 != std::string::npos && !is_2d_vector) {
		vec_name = hier_name_l_ori.substr(0, pos2);
  }

  // (2) know the vector size of this field
  auto rt_vec_size = get_vector_size(lnast, vec_name);

  vec_name = name_prefix_modifier_flattener(vec_name, true);
  // (3) create another function to create __fir_mux for 2^field_bits cases
  //     lhs <- __fir_mux(runtime_idx, value0, value1, ..., value2^filed_bits-1);
  std::vector<std::string> cond_strs;
  for (int i = 0; i < rt_vec_size; i++) {
    auto idx_eq = lnast.add_child(parent_node, Lnast_node::create_func_call());
    auto cond_str = create_tmp_var();
    lnast.add_child(idx_eq, Lnast_node::create_ref(cond_str));
    lnast.add_child(idx_eq, Lnast_node::create_const("__fir_eq"));
    lnast.add_child(idx_eq, Lnast_node::create_ref(rtidx_str));
    lnast.add_child(idx_eq, Lnast_node::create_const(i));
    cond_strs.push_back(cond_str);
  }

  auto idx_mux = lnast.add_child(parent_node, Lnast_node::create_if());
  for (int i = 0; i < rt_vec_size; i++) {
    lnast.add_child(idx_mux, Lnast_node::create_ref(cond_strs[i]));
    auto idx_stmt_t = lnast.add_child(idx_mux, Lnast_node::create_stmts());
    // auto rhs_flattened_name = name_prefix_modifier_flattener(tup_head, true);
    std::string lhs_flattened_name;
    if (is_2d_vector) {
      lhs_flattened_name = absl::StrCat(vec_name,".", i, ".", leaf_field_name);
    } else {
      lhs_flattened_name = absl::StrCat(vec_name,".", i);
    }
    lhs_flattened_name = name_prefix_modifier_flattener(lhs_flattened_name, false);

    add_lnast_assign(lnast, idx_stmt_t, lhs_flattened_name, rhs_flattened_name);
  }
  return;
}

void Inou_firrtl_module::handle_rhs_runtime_idx(Lnast &lnast, Lnast_nid &parent_node, std::string_view hier_name_l_ori, std::string_view hier_name_r_ori, const firrtl::FirrtlPB_Expression &rhs_expr) {
  std::string lhs_flattened_name = name_prefix_modifier_flattener(hier_name_l_ori, false);

  // idea: 
  // (1) get the runtime idx tuple field
  // FIXME->sh: does not pass the RenameTable pattern .... check it later, but focus on Mul.fir now
  std::string rtidx_str = get_runtime_idx_field_name(rhs_expr);
  rtidx_str = name_prefix_modifier_flattener(rtidx_str, true);


  std::string vec_name;
  bool is_2d_vector = false;
  std::string leaf_field_name = "";
  auto pos = hier_name_r_ori.find(".."); 
  if (pos != std::string::npos) {
    is_2d_vector = true;
    leaf_field_name = hier_name_r_ori.substr(pos + 2);
		vec_name = hier_name_r_ori.substr(0, pos);
  }
  

  auto pos2 = hier_name_r_ori.rfind('.');
  if (pos2 != std::string::npos && !is_2d_vector) {
		vec_name = hier_name_r_ori.substr(0, pos2);
  }

  // (2) know the vector size of this field
  auto rt_vec_size = get_vector_size(lnast, vec_name);


  vec_name = name_prefix_modifier_flattener(vec_name, true);
  // (3) create another function to create __fir_mux for 2^field_bits cases
  //     lhs <- __fir_mux(runtime_idx, value0, value1, ..., value2^filed_bits-1);

  std::vector<std::string> cond_strs;
  for (int i = 0; i < rt_vec_size; i++) {
    auto idx_eq = lnast.add_child(parent_node, Lnast_node::create_func_call());
    auto cond_str = create_tmp_var();
    lnast.add_child(idx_eq, Lnast_node::create_ref(cond_str));
    lnast.add_child(idx_eq, Lnast_node::create_const("__fir_eq"));
    lnast.add_child(idx_eq, Lnast_node::create_ref(rtidx_str));
    lnast.add_child(idx_eq, Lnast_node::create_const(i));
    cond_strs.push_back(cond_str);
  }

  auto idx_mux = lnast.add_child(parent_node, Lnast_node::create_if());
  for (int i = 0; i < rt_vec_size; i++) {
    lnast.add_child(idx_mux, Lnast_node::create_ref(cond_strs[i]));
    auto idx_stmt_t = lnast.add_child(idx_mux, Lnast_node::create_stmts());

    std::string rhs_flattened_name;
    if (is_2d_vector) {
      rhs_flattened_name = absl::StrCat(vec_name, ".", i, ".", leaf_field_name);
    } else {
      rhs_flattened_name = absl::StrCat(vec_name, ".", i);
    }
    rhs_flattened_name = name_prefix_modifier_flattener(rhs_flattened_name, true);

    add_lnast_assign(lnast, idx_stmt_t, lhs_flattened_name, rhs_flattened_name);
  }
  return;
}

uint16_t Inou_firrtl_module::get_vector_size(const Lnast &lnast, std::string_view vec_name) {
  if (var2vec_size.find(vec_name) == var2vec_size.end()) {
    auto module_name = lnast.get_top_module_name();
    I(Inou_firrtl::glob_info.module_var2vec_size.find(module_name) != Inou_firrtl::glob_info.module_var2vec_size.end());
    auto &io_var2vec_size = Inou_firrtl::glob_info.module_var2vec_size[module_name];

    // I(io_var2vec_size.find(vec_name) != io_var2vec_size.end());
    return io_var2vec_size[vec_name];
  } else {
    return var2vec_size[vec_name];
  }
}


int32_t Inou_firrtl_module::get_bit_count(const firrtl::FirrtlPB_Type& type) {
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


void Inou_firrtl_module::handle_register(Lnast& lnast, const firrtl::FirrtlPB_Type& type, std::string id, Lnast_nid& parent_node, const firrtl::FirrtlPB_Statement &stmt) {
  switch (type.type_case()) {
    case firrtl::FirrtlPB_Type::kBundleType: {  // Bundle Type
      for (int i = 0; i < type.bundle_type().field_size(); i++) {
        handle_register(lnast, type.bundle_type().field(i).type(), absl::StrCat(id, ".", type.bundle_type().field(i).id()), parent_node, stmt);
      }
      break;
    }
    case firrtl::FirrtlPB_Type::kVectorType: {  // Vector Type
      var2vec_size.insert_or_assign(id, type.vector_type().size());                                               
      for (uint32_t i = 0; i < type.vector_type().size(); i++) {
        handle_register(lnast, type.vector_type().type(), absl::StrCat(id, ".", i), parent_node, stmt);
      }
      break;
    }
    case firrtl::FirrtlPB_Type::kSintType: 
    case firrtl::FirrtlPB_Type::kUintType:{  
      add_local_flip_info(false, id);
      std::string head_chopped_hier_name;
      // if (id.find_first_of('.') != std::string::npos)
      if (absl::StrContains(id, '.')) {
        head_chopped_hier_name = id.substr(id.find_first_of('.') + 1);
      }

      std::replace(id.begin(), id.end(), '.', '_');

      auto reg_bits = get_bit_count(type);
      bool bits_set_done = reg_bits > 0 ? true : false; // some chirrtl code don't have bits set on register, but must have bits set on init expression
      setup_scalar_bits(lnast, absl::StrCat("#", id), reg_bits, parent_node, false);
      setup_register_reset_init(lnast, parent_node, id, stmt.register_().reset(), stmt.register_().init(), head_chopped_hier_name, bits_set_done);
      
      declare_register(lnast, parent_node, id);
      setup_register_q_pin(lnast, parent_node, id);
      break;
    }
    default: {
      I(false);
      break;
    }
  }
}


void Inou_firrtl_module::wire_init_flip_handling(Lnast& lnast, const firrtl::FirrtlPB_Type& type, std::string id, bool flipped_in, Lnast_nid& parent_node) {
  switch (type.type_case()) {
    case firrtl::FirrtlPB_Type::kBundleType: {  // Bundle Type
      auto& btype = type.bundle_type();
      for (int i = 0; i < type.bundle_type().field_size(); i++) {
        if (btype.field(i).is_flipped()) {
          wire_init_flip_handling(lnast, type.bundle_type().field(i).type(), absl::StrCat(id, ".", type.bundle_type().field(i).id()), !flipped_in, parent_node);
        } else {
          wire_init_flip_handling(lnast, type.bundle_type().field(i).type(), absl::StrCat(id, ".", type.bundle_type().field(i).id()),  flipped_in, parent_node);
        }
      }
      break;
    }
    case firrtl::FirrtlPB_Type::kVectorType: {  // Vector Type
      var2vec_size.insert_or_assign(id, type.vector_type().size());                                               
      for (uint32_t i = 0; i < type.vector_type().size(); i++) {
        wire_init_flip_handling(lnast, type.vector_type().type(), absl::StrCat(id, ".", i), false, parent_node);
      }
      break;
    }
    case firrtl::FirrtlPB_Type::kFixedType: {  // Fixed Point Type
      I(false);                                // TODO: LNAST does not support fixed point yet.
      break;
    }
    case firrtl::FirrtlPB_Type::kAsyncResetType: {  // AsyncReset
      add_local_flip_info(flipped_in, id);
      std::replace(id.begin(), id.end(), '.', '_');
      Lnast_node zero_node = Lnast_node::create_const(0);
      create_default_value_for_scalar_var(lnast, parent_node, id, zero_node);

      break;
    }
    case firrtl::FirrtlPB_Type::kSintType: {  // signed
      add_local_flip_info(flipped_in, id);
      std::replace(id.begin(), id.end(), '.', '_');
      // fmt::print("DEBUG CCC flattend vec id:{}\n", id);
      Lnast_node zero_node = Lnast_node::create_const(0);
      create_default_value_for_scalar_var(lnast, parent_node, id, zero_node);
      break;
    }
    case firrtl::FirrtlPB_Type::kUintType: {  // unsigned
      add_local_flip_info(flipped_in, id);

      std::replace(id.begin(), id.end(), '.', '_');
      Lnast_node zero_node = Lnast_node::create_const(0);
      create_default_value_for_scalar_var(lnast, parent_node, id, zero_node);
      break;
    }
    default: {
      // UInt Analog Reset Clock Types
      add_local_flip_info(flipped_in, id);
      break;
    }
  }
}

// When creating a register, we have to set the register's
// clock, reset, and init values using "dot" nodes in the LNAST.
// These functions create all of those when a reg is first declared.
void Inou_firrtl_module::setup_scalar_bits(Lnast& lnast, std::string_view id, uint32_t bits, Lnast_nid& parent_node, bool is_signed) {
  // Specify __bits, if bitwidth is explicit
  if (bits > 0) {
    auto value_node = Lnast_node::create_const(bits);
    auto extension  = is_signed ? ".__sbits" : ".__ubits";
    create_tuple_add_from_str(lnast, parent_node, absl::StrCat(id, extension), value_node);
  }
}

void Inou_firrtl_module::collect_memory_data_struct_hierarchy(std::string_view mem_name, const firrtl::FirrtlPB_Type& type_in,
                                                              std::string_view hier_fields_concats) {
  std::string new_hier_fields_concats;
  if (type_in.type_case() == firrtl::FirrtlPB_Type::kBundleType) {
    for (int i = 0; i < type_in.bundle_type().field_size(); i++) {
      if (hier_fields_concats.empty()) {
        new_hier_fields_concats = type_in.bundle_type().field(i).id();
      } else {
        new_hier_fields_concats = absl::StrCat(hier_fields_concats, ".", type_in.bundle_type().field(i).id());
      }

      auto type_sub_field = type_in.bundle_type().field(i).type();
      if (type_sub_field.type_case() == firrtl::FirrtlPB_Type::kBundleType
          || type_sub_field.type_case() == firrtl::FirrtlPB_Type::kVectorType) {
        collect_memory_data_struct_hierarchy(mem_name, type_sub_field, new_hier_fields_concats);
      } else {
        auto bits = get_bit_count(type_sub_field);
        absl::StrAppend(&new_hier_fields_concats, ".", bits);
        mem2din_fields[mem_name].emplace_back(new_hier_fields_concats);
      }
    }
  } else if (type_in.type_case() == firrtl::FirrtlPB_Type::kVectorType) {
    for (uint32_t i = 0; i < type_in.vector_type().size(); i++) {
      if (hier_fields_concats.empty()) {
        new_hier_fields_concats = std::to_string(i);
      } else {
        new_hier_fields_concats = absl::StrCat(hier_fields_concats, ".", std::to_string(i));
      }

      auto type_sub_field = type_in.vector_type().type();
      if (type_sub_field.type_case() == firrtl::FirrtlPB_Type::kBundleType
          || type_sub_field.type_case() == firrtl::FirrtlPB_Type::kVectorType) {
        collect_memory_data_struct_hierarchy(mem_name, type_sub_field, new_hier_fields_concats);
      } else {
        auto bits = get_bit_count(type_sub_field);
        absl::StrAppend(&new_hier_fields_concats, ".", bits);  // encode .bits at the end of hier-fields
        // new_hier_fields_concats = absl::StrCat(new_hier_fields_concats, ".", bits);
        mem2din_fields[mem_name].emplace_back(new_hier_fields_concats);
      }
    }
  }
}

void Inou_firrtl_module::init_cmemory(Lnast& lnast, Lnast_nid& parent_node, const firrtl::FirrtlPB_Statement_CMemory& cmem) {
  std::string depth_str;  // depth in firrtl = size in LiveHD
  uint8_t     wensize_init = 1;
  if (cmem.type_case() == firrtl::FirrtlPB_Statement_CMemory::kTypeAndDepth) {
    depth_str = Inou_firrtl::convert_bigint_to_str(cmem.type_and_depth().depth());
  } else {
    I(false, "happened somewhere in boom!");  // never happened?
  }

  firrtl::FirrtlPB_Type din_type = cmem.type_and_depth().data_type();
  if (din_type.type_case() == firrtl::FirrtlPB_Type::kBundleType || din_type.type_case() == firrtl::FirrtlPB_Type::kVectorType) {
    collect_memory_data_struct_hierarchy(cmem.id(), din_type, "");
    wensize_init = din_type.vector_type().size();
  } else if (din_type.type_case() == firrtl::FirrtlPB_Type::kVectorType) {
    I(false, "it's masked case!");
  } else if (din_type.type_case() == firrtl::FirrtlPB_Type::kUintType) {
    auto bits = get_bit_count(din_type);
    mem2din_fields[cmem.id()].emplace_back(absl::StrCat(".", bits));  // encode .bits at the end of hier-fields
  } else {
    I(false);
  }

  // Specify attributes
  bool fwd = false;
  if (cmem.read_under_write() == firrtl::FirrtlPB_Statement_ReadUnderWrite::FirrtlPB_Statement_ReadUnderWrite_NEW) {
    fwd = true;
  }

  // create foo_mem_res = __memory(foo_mem_aruments.__last_value)
  auto idx_attr_get = lnast.add_child(parent_node, Lnast_node::create_attr_get());
  auto temp_var_str = create_tmp_var();
  lnast.add_child(idx_attr_get, Lnast_node::create_ref(temp_var_str));
  lnast.add_child(idx_attr_get, Lnast_node::create_ref((absl::StrCat(cmem.id(), "_interface_args"))));
  lnast.add_child(idx_attr_get, Lnast_node::create_const("__last_value"));

  auto idx_fncall = lnast.add_child(parent_node, Lnast_node::create_func_call());
  lnast.add_child(idx_fncall, Lnast_node::create_ref(absl::StrCat(cmem.id(), "_res")));
  lnast.add_child(idx_fncall, Lnast_node::create_ref("__memory"));
  lnast.add_child(idx_fncall, Lnast_node::create_ref(temp_var_str));

  // bare initialization of memory interfaces so that SSA can continue later
  auto idx_ta_maddr = lnast.add_child(parent_node, Lnast_node::create_tuple_add());
  lnast.add_child(idx_ta_maddr, Lnast_node::create_ref(absl::StrCat(cmem.id(), "_addr")));

  auto idx_ta_mdin = lnast.add_child(parent_node, Lnast_node::create_tuple_add());
  lnast.add_child(idx_ta_mdin, Lnast_node::create_ref(absl::StrCat(cmem.id(), "_din")));

  auto idx_ta_men = lnast.add_child(parent_node, Lnast_node::create_tuple_add());
  lnast.add_child(idx_ta_men, Lnast_node::create_ref(absl::StrCat(cmem.id(), "_enable")));

  auto idx_asg_mfwd = lnast.add_child(parent_node, Lnast_node::create_assign());
  lnast.add_child(idx_asg_mfwd, Lnast_node::create_ref(absl::StrCat(cmem.id(), "_fwd")));
  lnast.add_child(idx_asg_mfwd, Lnast_node::create_const(fwd));  // note: initialized

  auto idx_ta_mlat = lnast.add_child(parent_node, Lnast_node::create_assign());
  lnast.add_child(idx_ta_mlat, Lnast_node::create_ref(absl::StrCat(cmem.id(), "_type")));
  lnast.add_child(idx_ta_mlat, Lnast_node::create_const(cmem.sync_read() ? 1 : 0));

  auto idx_asg_mwensize = lnast.add_child(parent_node, Lnast_node::create_assign());
  lnast.add_child(idx_asg_mwensize, Lnast_node::create_ref(absl::StrCat(cmem.id(), "_wensize")));
  lnast.add_child(idx_asg_mwensize, Lnast_node::create_const(wensize_init));

  auto idx_asg_msize = lnast.add_child(parent_node, Lnast_node::create_assign());
  lnast.add_child(idx_asg_msize, Lnast_node::create_ref(absl::StrCat(cmem.id(), "_size")));
  lnast.add_child(idx_asg_msize, Lnast_node::create_const(depth_str));  // note: initialized

  auto idx_ta_mrport = lnast.add_child(parent_node, Lnast_node::create_tuple_add());
  lnast.add_child(idx_ta_mrport, Lnast_node::create_ref(absl::StrCat(cmem.id(), "_rdport")));

  // create if true scope so you have a inserted temporary stmt node 
  // for foo_mem_din field variable initialization/declaration later
  auto idx_if = lnast.add_child(parent_node, Lnast_node::create_if());
  lnast.add_child(idx_if, Lnast_node::create_ref("true"));
  auto idx_stmts = lnast.add_child(idx_if, Lnast_node::create_stmts());
  mem2initial_idx.insert_or_assign(cmem.id(), idx_stmts);

  mem2port_cnt.insert_or_assign(cmem.id(), -1);
}

void Inou_firrtl_module::handle_mport_declaration(Lnast& lnast, Lnast_nid& parent_node,
                                                  const firrtl::FirrtlPB_Statement_MemoryPort& mport) {
  // (void) parent_node;
  const auto &mem_name = mport.memory_id();
  mport2mem.insert_or_assign(mport.id(), mem_name);

  mem2port_cnt[mem_name]++;
  auto clk_str = get_expr_hier_name(lnast, parent_node, mport.expression());
  auto adr_str = get_expr_hier_name(lnast, parent_node, mport.memory_index());
  clk_str = name_prefix_modifier_flattener(clk_str, true);
  adr_str = name_prefix_modifier_flattener(adr_str, true);
  auto port_cnt_str    = mem2port_cnt[mem_name];
  auto default_val_str = 0;

  // assign whatever adder/enable the mport variable comes with in the current scope, either top or scope
  auto idx_ta_maddr = lnast.add_child(parent_node, Lnast_node::create_tuple_add());
  lnast.add_child(idx_ta_maddr, Lnast_node::create_ref(absl::StrCat(mem_name, "_addr")));
  lnast.add_child(idx_ta_maddr, Lnast_node::create_const(port_cnt_str));
  lnast.add_child(idx_ta_maddr, Lnast_node::create_ref(adr_str));

  // note: because any port might be declared inside a subscope but be used at upper scope, at the time you see a mport
  //       declaration, you must specify the port enable signal, even it's a masked write port. For the maksed write, a
  //       bit-vector wr_enable will be handled at the initialize_wr_mport_from_usage()
  auto idx_ta_men = lnast.add_child(parent_node, Lnast_node::create_tuple_add());
  lnast.add_child(idx_ta_men, Lnast_node::create_ref(absl::StrCat(mem_name, "_enable")));
  lnast.add_child(idx_ta_men, Lnast_node::create_const(port_cnt_str));
  lnast.add_child(idx_ta_men, Lnast_node::create_const(1));

  // initialized port interfaces at the top scope
  I(mem2initial_idx.find(mem_name) != mem2initial_idx.end());
  auto& idx_initialize_stmts = mem2initial_idx[mem_name];

  auto idx_ta_mclk_ini = lnast.add_child(idx_initialize_stmts, Lnast_node::create_assign());
  lnast.add_child(idx_ta_mclk_ini, Lnast_node::create_ref(absl::StrCat(mem_name, "_clock")));
  lnast.add_child(idx_ta_mclk_ini, Lnast_node::create_ref(clk_str));

  auto idx_ta_maddr_ini = lnast.add_child(idx_initialize_stmts, Lnast_node::create_tuple_add());
  lnast.add_child(idx_ta_maddr_ini, Lnast_node::create_ref(absl::StrCat(mem_name, "_addr")));
  lnast.add_child(idx_ta_maddr_ini, Lnast_node::create_const(port_cnt_str));
  lnast.add_child(idx_ta_maddr_ini, Lnast_node::create_const(default_val_str));

  auto idx_ta_men_ini = lnast.add_child(idx_initialize_stmts, Lnast_node::create_tuple_add());
  lnast.add_child(idx_ta_men_ini, Lnast_node::create_ref(absl::StrCat(mem_name, "_enable")));
  lnast.add_child(idx_ta_men_ini, Lnast_node::create_const(port_cnt_str));
  lnast.add_child(idx_ta_men_ini, Lnast_node::create_const(default_val_str));

  auto idx_ta_mrdport_ini = lnast.add_child(idx_initialize_stmts, Lnast_node::create_tuple_add());
  lnast.add_child(idx_ta_mrdport_ini, Lnast_node::create_ref(absl::StrCat(mem_name, "_rdport")));
  lnast.add_child(idx_ta_mrdport_ini, Lnast_node::create_const(port_cnt_str));
  lnast.add_child(idx_ta_mrdport_ini, Lnast_node::create_const("true"));

  auto dir_case = mport.direction();
  if (dir_case == firrtl::FirrtlPB_Statement_MemoryPort_Direction::FirrtlPB_Statement_MemoryPort_Direction_MEMORY_PORT_DIRECTION_READ) {
    // rd_port: only need to initialize mem_res[rd_port] when you are sure it's a read mport
    init_mem_res(lnast, mem_name, std::to_string(port_cnt_str));
    // FIXME->sh:
    // if you already know it's a read mport,  you should let mport = mem_res[rd_port] here, or the mem_port_cnt might duplicately
    // count one more port_cnt, see the cases from ListBuffer.fir (search push_tail).
  } else if (dir_case == firrtl::FirrtlPB_Statement_MemoryPort_Direction::FirrtlPB_Statement_MemoryPort_Direction_MEMORY_PORT_DIRECTION_WRITE) {
    // wr_port: noly need to initialize mem_din[wr_port] when you are sure it's a write mport
    init_mem_din(lnast, mem_name, std::to_string(port_cnt_str));
    I(mport2mask_bitvec.find(mport.id()) == mport2mask_bitvec.end());
    I(mport2mask_cnt.find(mport.id()) == mport2mask_cnt.end());
    mport2mask_bitvec.insert_or_assign(mport.id(), 1);
    mport2mask_cnt.insert_or_assign(mport.id(), 0);
  } else {
    // need to initialize both mem_din[wr_port] mem_res[res_port] when you are not sure the port type
    init_mem_res(lnast, mem_name, std::to_string(port_cnt_str));
    init_mem_din(lnast, mem_name, std::to_string(port_cnt_str));
  }
}

// we have to set the memory result bits so the later fir_bits pass could start propagate bits information from.
void Inou_firrtl_module::init_mem_res(Lnast& lnast, std::string_view mem_name, std::string_view port_cnt_str) {
  I(mem2initial_idx.find(mem_name) != mem2initial_idx.end());
  auto& idx_initialize_stmts = mem2initial_idx[mem_name];
  auto  it                   = mem2din_fields.find(mem_name);
  I(it != mem2din_fields.end());

  auto  mem_res_str     = absl::StrCat(mem_name, "_res");
  auto& hier_full_names = mem2din_fields[mem_name];
  for (const auto& hier_full_name : hier_full_names) {  // hier_full_name example: foo.bar.baz.20, the last field is bit
    // fmt::print("DEBUG DDD hier_name:{}\n", hier_full_name);
    std::vector<std::string> hier_sub_names;
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

void Inou_firrtl_module::init_mem_din(Lnast& lnast, std::string_view mem_name, std::string_view port_cnt_str) {
  auto default_val_str = 0;
  I(mem2initial_idx.find(mem_name) != mem2initial_idx.end());
  auto& idx_initialize_stmts = mem2initial_idx[mem_name];
  auto  it                   = mem2din_fields.find(mem_name);
  I(it != mem2din_fields.end());

  if (it->second.at(0) == ".") {  // din is scalar, the din_fields starts with something like .17
    auto idx_ta_mdin_ini = lnast.add_child(idx_initialize_stmts, Lnast_node::create_tuple_add());
    lnast.add_child(idx_ta_mdin_ini, Lnast_node::create_ref(absl::StrCat(mem_name, "_din")));
    lnast.add_child(idx_ta_mdin_ini, Lnast_node::create_const(port_cnt_str));
    lnast.add_child(idx_ta_mdin_ini, Lnast_node::create_const(default_val_str));
  } else {  // din is tuple
    auto& hier_full_names = mem2din_fields[mem_name];
    for (const auto& hier_full_name : hier_full_names) {  // hier_full_name example: foo.bar.baz.20, the last field is bit
      // fmt::print("hier_name:{}\n", hier_full_name);
      // fmt::print("DEBUG EEE hier_full_name:{}\n", hier_full_name);
      std::vector<std::string> hier_sub_names;
      // auto found = hier_full_name.find_last_of('.');  // get rid of last bit field
      auto found = hier_full_name.rfind('.');  // get rid of last bit field
      split_hier_name(hier_full_name.substr(0, found), hier_sub_names);

      auto idx_ta_mdin_ini = lnast.add_child(idx_initialize_stmts, Lnast_node::create_tuple_add());
      lnast.add_child(idx_ta_mdin_ini, Lnast_node::create_ref(absl::StrCat(mem_name, "_din")));
      lnast.add_child(idx_ta_mdin_ini, Lnast_node::create_const(port_cnt_str));

      for (const auto& sub_name : hier_sub_names) {
       lnast.add_child(idx_ta_mdin_ini, Lnast_node::create_const(sub_name));
      }

      lnast.add_child(idx_ta_mdin_ini, Lnast_node::create_const(default_val_str));
    }
  }
}

/* When a module instance is created in FIRRTL, we need to do the same
 * in LNAST. Note that the instance command in FIRRTL does not hook
 * any input or outputs. */
void Inou_firrtl_module::create_module_inst(Lnast& lnast, const firrtl::FirrtlPB_Statement_Instance& inst, Lnast_nid& parent_node) {
  /*            dot                       assign                      fn_call
   *      /      |        \                / \                     /     |     \
   * ___F0 itup_[inst_name] __last_value   F1 ___F0  otup_[inst_name] [mod_name]  F1 */
  auto temp_var_name2 = absl::StrCat("F", std::to_string(tmp_var_cnt));
  tmp_var_cnt++;
  auto inst_name = inst.id();
  if (inst.id().substr(0, 2) == "_T") {
    inst_name = absl::StrCat("_.", inst_name);
  }
  auto inp_name = absl::StrCat("itup_", inst_name);
  auto out_name = absl::StrCat("otup_", inst_name);

  auto idx_dot = lnast.add_child(parent_node, Lnast_node::create_attr_get());
  lnast.add_child(idx_dot, Lnast_node::create_ref(temp_var_name2));
  lnast.add_child(idx_dot, Lnast_node::create_ref(inp_name));
  lnast.add_child(idx_dot, Lnast_node::create_const("__last_value"));

  auto idx_fncall = lnast.add_child(parent_node, Lnast_node::create_func_call());
  lnast.add_child(idx_fncall, Lnast_node::create_ref(out_name));
  lnast.add_child(idx_fncall, Lnast_node::create_ref(absl::StrCat("__firrtl_", inst.module_id())));
  lnast.add_child(idx_fncall, Lnast_node::create_ref(temp_var_name2));
  
  const auto &module_name = inst.module_id();
  inst2module[inst.id()] = module_name;
  
  const auto &outputs_set = Inou_firrtl::glob_info.module2outputs[module_name];
  for (const auto &itr : outputs_set) {
    auto hier_name_r = absl::StrCat(inst.id(), ".", std::get<0>(itr));
    auto flattened_out_name = name_prefix_modifier_flattener(hier_name_r, false);
    create_tuple_get_for_instance_otup(lnast, parent_node, hier_name_r, flattened_out_name);
    setup_scalar_bits(lnast, flattened_out_name, std::get<1>(itr),parent_node, std::get<2>(itr));
  }
}

/* No mux node type exists in LNAST. To support FIRRTL muxes, we instead
 * map a mux to an if-else statement whose condition is the same condition
 * as the first argument (the condition) of the mux. */
void Inou_firrtl_module::handle_mux_assign(Lnast& lnast, const firrtl::FirrtlPB_Expression& expr, Lnast_nid& parent_node, std::string_view lhs) {
  I(lnast.get_data(parent_node).type.is_stmts());

	// ori
  // std::string t_rhs_str = expr_str_flattened_or_tg(lnast, parent_node, expr.mux().t_value());
  // std::string f_rhs_str = expr_str_flattened_or_tg(lnast, parent_node, expr.mux().f_value());
	
	
	// FIXME:
	// 此 solution 是可以 work, 缺點就是本來好端端直接 flatten extr_str 就了事, 但是現在
	// 不能爽爽直接 flatten, 還要 check 一堆 table .....
	// TODO: think about a better solution
	std::string t_str = get_expr_hier_name(lnast, parent_node, expr.mux().t_value());
	std::string f_str = get_expr_hier_name(lnast, parent_node, expr.mux().f_value());
	// fmt::print("DEBUG AAA t_str:{}, f_str:{}\n", t_str, f_str);


	// preparation: get head of the tuple name so you know entry for the var2flip table
	auto pos_t = t_str.find_first_of('.');
	// std::string tup_head_t, tup_head_f;
	// std::string tup_rest_t, tup_rest_f;
	std::string tup_head_t;
	std::string tup_rest_t;
	if (pos_t != std::string::npos) {
		tup_head_t = t_str.substr(0, pos_t);
		tup_rest_t = t_str.substr(pos_t + 1);
	} else {
		tup_head_t = t_str;
	}

	std::vector<std::string> head_chopped_hier_names;
	auto const& module_var2flip = Inou_firrtl::glob_info.var2flip[lnast.get_top_module_name()];
  for (auto &[var, set] : module_var2flip) {
		if (var == tup_head_t) {
			for (auto &[hier_name, flipped] : set) {
				auto pos = hier_name.find(t_str);
				if ( pos != std::string::npos && hier_name != t_str) {
					head_chopped_hier_names.push_back(hier_name.substr(t_str.size() + 1));
				}
			}
		}
  }
	
	// most cases
	if (head_chopped_hier_names.size() == 0) {
		auto idx_pre_asg = lnast.add_child(parent_node, Lnast_node::create_assign());
		lnast.add_child(idx_pre_asg, Lnast_node::create_ref(lhs));
		lnast.add_child(idx_pre_asg, Lnast_node::create_const("0b?"));

		auto cond_str = expr_str_flattened_or_tg(lnast, parent_node, expr.mux().condition());

		auto idx_mux_if = lnast.add_child(parent_node, Lnast_node::create_if());
		attach_expr_str2node(lnast, cond_str, idx_mux_if);

		auto idx_stmt_t = lnast.add_child(idx_mux_if, Lnast_node::create_stmts());
		auto idx_stmt_f = lnast.add_child(idx_mux_if, Lnast_node::create_stmts());


		t_str = name_prefix_modifier_flattener(t_str, true);
		f_str = name_prefix_modifier_flattener(f_str, true);
		add_lnast_assign(lnast, idx_stmt_t, lhs, t_str);
		add_lnast_assign(lnast, idx_stmt_f, lhs, f_str);
		return;
	}

	// rare cases
	for (const auto &head_chopped_hier_name : head_chopped_hier_names) {
		auto new_lhs = absl::StrCat(lhs, ".", head_chopped_hier_name);
		new_lhs      = name_prefix_modifier_flattener(new_lhs, false);
		auto idx_pre_asg = lnast.add_child(parent_node, Lnast_node::create_assign());
		lnast.add_child(idx_pre_asg, Lnast_node::create_ref(new_lhs));
		lnast.add_child(idx_pre_asg, Lnast_node::create_const("0b?"));

		auto cond_str = expr_str_flattened_or_tg(lnast, parent_node, expr.mux().condition());

		auto idx_mux_if = lnast.add_child(parent_node, Lnast_node::create_if());
		attach_expr_str2node(lnast, cond_str, idx_mux_if);

		auto idx_stmt_t = lnast.add_child(idx_mux_if, Lnast_node::create_stmts());
		auto idx_stmt_f = lnast.add_child(idx_mux_if, Lnast_node::create_stmts());


		auto new_t_str = absl::StrCat(t_str, ".", head_chopped_hier_name);
		auto new_f_str = absl::StrCat(f_str, ".", head_chopped_hier_name);
		new_t_str = name_prefix_modifier_flattener(new_t_str, true);
		new_f_str = name_prefix_modifier_flattener(new_f_str, true);
		add_lnast_assign(lnast, idx_stmt_t, new_lhs, new_t_str);
		add_lnast_assign(lnast, idx_stmt_f, new_lhs, new_f_str);
	}

	// t_str = name_prefix_modifier_flattener(t_str, true);
	// f_str = name_prefix_modifier_flattener(f_str, true);
	// fmt::print("DEBUG AAA t_str_prefix: {}, f_str_prefixed: {}\n", t_str, f_str);

  
	// ori
  // add_lnast_assign(lnast, idx_stmt_t, lhs_full, t_str);
  // add_lnast_assign(lnast, idx_stmt_f, lhs_full, f_str);
}

/* ValidIfs get detected as the RHS of an assign statement and we can't have a child of
 * an assign be an if-typed node. Thus, we have to detect ahead of time if it is a validIf
 * if we're doing an assign. If that is the case, do this instead of using ListExprType().*/
void Inou_firrtl_module::handle_valid_if_assign(Lnast& lnast, const firrtl::FirrtlPB_Expression& expr, Lnast_nid& parent_node,
                                                std::string_view lhs) {
  I(lnast.get_data(parent_node).type.is_stmts());

	// FIXME->sh: do the trick to declare variable with the validif value, hope
	// this could make the validif to fit the role of "else mux"
  init_expr_add(lnast, expr.valid_if().value(), parent_node, lhs);

  auto cond_str = expr_str_flattened_or_tg(lnast, parent_node, expr.valid_if().condition());
  auto idx_v_if = lnast.add_child(parent_node, Lnast_node::create_if());
  lnast.add_child(idx_v_if, Lnast_node::create_ref(cond_str));

  auto idx_stmt_t = lnast.add_child(idx_v_if, Lnast_node::create_stmts());

  init_expr_add(lnast, expr.valid_if().value(), idx_stmt_t, lhs);
}

// ----------------- primitive op start -------------------------------------------------------------------------------
void Inou_firrtl_module::handle_unary_op(Lnast& lnast, const firrtl::FirrtlPB_Expression_PrimOp& op, Lnast_nid& parent_node,
                                         std::string_view lhs) {
  I(lnast.get_data(parent_node).type.is_stmts());
  I(op.arg_size() == 1);

  auto lhs_str = lhs;
  auto e1_str = expr_str_flattened_or_tg(lnast, parent_node, op.arg(0));
  auto idx_not = lnast.add_child(parent_node, Lnast_node::create_func_call());  // "__fir_not"));
  lnast.add_child(idx_not, Lnast_node::create_ref(lhs_str));
  lnast.add_child(idx_not, Lnast_node::create_const("__fir_not"));
  attach_expr_str2node(lnast, e1_str, idx_not);
}

void Inou_firrtl_module::handle_and_reduce_op(Lnast& lnast, const firrtl::FirrtlPB_Expression_PrimOp& op, Lnast_nid& parent_node,
                                              std::string_view lhs) {
  I(lnast.get_data(parent_node).type.is_stmts());
  I(op.arg_size() == 1);

  auto lhs_str  = lhs;
  auto e1_str   = expr_str_flattened_or_tg(lnast, parent_node, op.arg(0));
  auto idx_andr = lnast.add_child(parent_node, Lnast_node::create_func_call());  // "__fir_andr"));
  lnast.add_child(idx_andr, Lnast_node::create_ref(lhs_str));
  lnast.add_child(idx_andr, Lnast_node::create_const("__fir_andr"));
  // lnast.add_child(idx_andr, Lnast_node::create_ref(e1_str));
  attach_expr_str2node(lnast, e1_str, idx_andr);
}

void Inou_firrtl_module::handle_or_reduce_op(Lnast& lnast, const firrtl::FirrtlPB_Expression_PrimOp& op, Lnast_nid& parent_node,
                                             std::string_view lhs) {
  I(lnast.get_data(parent_node).type.is_stmts());
  I(op.arg_size() == 1);

  auto lhs_str = lhs;
  auto e1_str  = expr_str_flattened_or_tg(lnast, parent_node, op.arg(0));
  auto idx_orr = lnast.add_child(parent_node, Lnast_node::create_func_call());  // "__fir_orr"));
                                                                                
  lnast.add_child(idx_orr, Lnast_node::create_ref(lhs_str));
  lnast.add_child(idx_orr, Lnast_node::create_const("__fir_orr"));
  lnast.add_child(idx_orr, Lnast_node::create_ref(e1_str));
}

void Inou_firrtl_module::handle_xor_reduce_op(Lnast& lnast, const firrtl::FirrtlPB_Expression_PrimOp& op, Lnast_nid& parent_node,
                                              std::string_view lhs) {
  I(lnast.get_data(parent_node).type.is_stmts());
  I(op.arg_size() == 1);

  auto lhs_str  = lhs;
  auto e1_str   = expr_str_flattened_or_tg(lnast, parent_node, op.arg(0));
  auto idx_xorr = lnast.add_child(parent_node, Lnast_node::create_func_call());  // "__fir_xorr"));
  lnast.add_child(idx_xorr, Lnast_node::create_ref(lhs_str));
  lnast.add_child(idx_xorr, Lnast_node::create_const("__fir_xorr"));
  lnast.add_child(idx_xorr, Lnast_node::create_ref(e1_str));
}

void Inou_firrtl_module::handle_negate_op(Lnast& lnast, const firrtl::FirrtlPB_Expression_PrimOp& op, Lnast_nid& parent_node,
                                          std::string_view lhs) {
  I(lnast.get_data(parent_node).type.is_stmts());
  I(op.arg_size() == 1);

  auto lhs_str = lhs;
  auto e1_str = expr_str_flattened_or_tg(lnast, parent_node, op.arg(0));
  auto idx_neg = lnast.add_child(parent_node, Lnast_node::create_func_call());  // "__fir_neg"));
  lnast.add_child(idx_neg, Lnast_node::create_ref(lhs_str));
  lnast.add_child(idx_neg, Lnast_node::create_const("__fir_neg"));
  lnast.add_child(idx_neg, Lnast_node::create_ref(e1_str));
}

void Inou_firrtl_module::handle_conv_op(Lnast& lnast, const firrtl::FirrtlPB_Expression_PrimOp& op, Lnast_nid& parent_node,
                                        std::string_view lhs) {
  I(lnast.get_data(parent_node).type.is_stmts());
  I(op.arg_size() == 1);

  auto lhs_str = lhs;
  auto e1_str = expr_str_flattened_or_tg(lnast, parent_node, op.arg(0));
  auto idx_cvt = lnast.add_child(parent_node, Lnast_node::create_func_call());  // "__fir_cvt"));
  lnast.add_child(idx_cvt, Lnast_node::create_ref(lhs_str));
  lnast.add_child(idx_cvt, Lnast_node::create_const("__fir_cvt"));
  lnast.add_child(idx_cvt, Lnast_node::create_ref(e1_str));
}

void Inou_firrtl_module::handle_extract_bits_op(Lnast& lnast, const firrtl::FirrtlPB_Expression_PrimOp& op, Lnast_nid& parent_node,
                                                std::string_view lhs) {
  I(lnast.get_data(parent_node).type.is_stmts());
  I(op.arg_size() == 1 && op.const__size() == 2);

  auto lhs_str       = lhs;
  auto e1_str        = expr_str_flattened_or_tg(lnast, parent_node, op.arg(0));
  auto idx_bits_exct = lnast.add_child(parent_node, Lnast_node::create_func_call());  // "__fir_bits"));
                                                                                      
  lnast.add_child(idx_bits_exct, Lnast_node::create_ref(lhs_str));
  lnast.add_child(idx_bits_exct, Lnast_node::create_const("__fir_bits"));
  lnast.add_child(idx_bits_exct, Lnast_node::create_ref(e1_str));
  lnast.add_child(idx_bits_exct, Lnast_node::create_const(op.const_(0).value()));
  lnast.add_child(idx_bits_exct, Lnast_node::create_const(op.const_(1).value()));
}

void Inou_firrtl_module::handle_head_op(Lnast& lnast, const firrtl::FirrtlPB_Expression_PrimOp& op, Lnast_nid& parent_node,
                                        std::string_view lhs) {
  I(lnast.get_data(parent_node).type.is_stmts());
  I(op.arg_size() == 1 && op.const__size() == 1);

  auto lhs_str  = lhs;
  auto e1_str = expr_str_flattened_or_tg(lnast, parent_node, op.arg(0));
  auto idx_head = lnast.add_child(parent_node, Lnast_node::create_func_call());  // "__fir_head"));
  lnast.add_child(idx_head, Lnast_node::create_ref(lhs_str));
  lnast.add_child(idx_head, Lnast_node::create_const("__fir_head"));
  lnast.add_child(idx_head, Lnast_node::create_ref(e1_str));
  lnast.add_child(idx_head, Lnast_node::create_const(op.const_(0).value()));
}

void Inou_firrtl_module::handle_tail_op(Lnast& lnast, const firrtl::FirrtlPB_Expression_PrimOp& op, Lnast_nid& parent_node,
                                        std::string_view lhs) {
  I(lnast.get_data(parent_node).type.is_stmts());
  I(op.arg_size() == 1 && op.const__size() == 1);
  auto lhs_str = lhs;
  auto e1_str = expr_str_flattened_or_tg(lnast, parent_node, op.arg(0));

  auto idx_tail = lnast.add_child(parent_node, Lnast_node::create_func_call());  // "__fir_tail"));
  lnast.add_child(idx_tail, Lnast_node::create_ref(lhs_str));
  lnast.add_child(idx_tail, Lnast_node::create_const("__fir_tail"));
  lnast.add_child(idx_tail, Lnast_node::create_ref(e1_str));
  lnast.add_child(idx_tail, Lnast_node::create_const(op.const_(0).value()));
}

void Inou_firrtl_module::handle_concat_op(Lnast& lnast, const firrtl::FirrtlPB_Expression_PrimOp& op, Lnast_nid& parent_node,
                                          std::string_view lhs) {
  I(lnast.get_data(parent_node).type.is_stmts());
  I(op.arg_size() == 2);

  auto lhs_str = lhs;
  auto e1_str = expr_str_flattened_or_tg(lnast, parent_node, op.arg(0));
  auto e2_str = expr_str_flattened_or_tg(lnast, parent_node, op.arg(1));

  auto idx_concat = lnast.add_child(parent_node, Lnast_node::create_func_call());  // "__fir_cat"));
  lnast.add_child(idx_concat, Lnast_node::create_ref(lhs_str));
  lnast.add_child(idx_concat, Lnast_node::create_const("__fir_cat"));
  attach_expr_str2node(lnast, e1_str, idx_concat);
  attach_expr_str2node(lnast, e2_str, idx_concat);
}

void Inou_firrtl_module::handle_pad_op(Lnast& lnast, const firrtl::FirrtlPB_Expression_PrimOp& op, Lnast_nid& parent_node,
                                       std::string_view lhs) {
  I(lnast.get_data(parent_node).type.is_stmts());
  I(op.arg_size() == 1 && op.const__size() == 1);

  auto lhs_str = lhs;
  auto e1_str = expr_str_flattened_or_tg(lnast, parent_node, op.arg(0));

  auto idx_pad = lnast.add_child(parent_node, Lnast_node::create_func_call());  // "__fir_pad"));
  lnast.add_child(idx_pad, Lnast_node::create_ref(lhs_str));
  lnast.add_child(idx_pad, Lnast_node::create_const("__fir_pad"));
  lnast.add_child(idx_pad, Lnast_node::create_ref(e1_str));
  lnast.add_child(idx_pad, Lnast_node::create_const(op.const_(0).value()));
}

void Inou_firrtl_module::handle_binary_op(Lnast& lnast, const firrtl::FirrtlPB_Expression_PrimOp& op,
                                                  Lnast_nid& parent_node, std::string_view lhs) {
  I(lnast.get_data(parent_node).type.is_stmts());
  I(op.arg_size() == 2);
  
  auto e1_str = expr_str_flattened_or_tg(lnast, parent_node, op.arg(0));
  auto e2_str = expr_str_flattened_or_tg(lnast, parent_node, op.arg(1));

  Lnast_nid idx_primop;
  auto sub_it = Inou_firrtl::op2firsub.find(op.op());
  I(sub_it != Inou_firrtl::op2firsub.end());

  idx_primop = lnast.add_child(parent_node, Lnast_node::create_func_call());
  lnast.add_child(idx_primop, Lnast_node::create_ref(lhs));
  lnast.add_child(idx_primop, Lnast_node::create_const(sub_it->second));

  attach_expr_str2node(lnast, e1_str, idx_primop);
  attach_expr_str2node(lnast, e2_str, idx_primop);
}

void Inou_firrtl_module::handle_static_shift_op(Lnast& lnast, const firrtl::FirrtlPB_Expression_PrimOp& op, Lnast_nid& parent_node,
                                                std::string_view lhs) {
  I(lnast.get_data(parent_node).type.is_stmts());
  I(op.arg_size() == 1 || op.const__size() == 1);

  auto lhs_str = lhs;
  auto e1_str = expr_str_flattened_or_tg(lnast, parent_node, op.arg(0));

  Lnast_nid idx_shift;
  auto      sub_it = Inou_firrtl::op2firsub.find(op.op());
  I(sub_it != Inou_firrtl::op2firsub.end());

  idx_shift = lnast.add_child(parent_node, Lnast_node::create_func_call());

  lnast.add_child(idx_shift, Lnast_node::create_ref(lhs_str));
  lnast.add_child(idx_shift, Lnast_node::create_const(sub_it->second));
	attach_expr_str2node(lnast, e1_str, idx_shift);
  lnast.add_child(idx_shift, Lnast_node::create_const(op.const_(0).value()));
}

void Inou_firrtl_module::handle_as_usint_op(Lnast& lnast, const firrtl::FirrtlPB_Expression_PrimOp& op, Lnast_nid& parent_node,
                                            std::string_view lhs) {
  I(op.arg_size() == 1 && op.const__size() == 0);
  auto lhs_str = lhs;
  auto e1_str  = expr_str_flattened_or_tg(lnast, parent_node, op.arg(0));

  auto sub_it = Inou_firrtl::op2firsub.find(op.op());
  I(sub_it != Inou_firrtl::op2firsub.end());

  auto idx_conv = lnast.add_child(parent_node, Lnast_node::create_func_call());

  lnast.add_child(idx_conv, Lnast_node::create_ref(lhs_str));
  lnast.add_child(idx_conv, Lnast_node::create_const(sub_it->second));
  attach_expr_str2node(lnast, e1_str, idx_conv);
}

void Inou_firrtl_module::handle_type_conv_op(Lnast& lnast, const firrtl::FirrtlPB_Expression_PrimOp& op, Lnast_nid& parent_node,
                                             std::string_view lhs) {
  I(op.arg_size() == 1 && op.const__size() == 0);
  auto lhs_str = lhs;
  auto e1_str  = expr_str_flattened_or_tg(lnast, parent_node, op.arg(0));
  auto sub_it  = Inou_firrtl::op2firsub.find(op.op());
  I(sub_it != Inou_firrtl::op2firsub.end());

  auto idx_conv = lnast.add_child(parent_node, Lnast_node::create_func_call());

  lnast.add_child(idx_conv, Lnast_node::create_ref(lhs_str));
  lnast.add_child(idx_conv, Lnast_node::create_const(sub_it->second));
  // lnast.add_child(idx_conv, Lnast_node::create_const(e1_str));
  auto first_char = e1_str[0];
  if (isdigit(first_char) || first_char == '-' || first_char == '+') {
    lnast.add_child(idx_conv, Lnast_node::create_const(e1_str));
  } else {
    lnast.add_child(idx_conv, Lnast_node::create_ref(e1_str));
  }
}

// --------------------------------------- end of primitive op ----------------------------------------------


void Inou_firrtl_module::initialize_rd_mport_from_usage(Lnast& lnast, Lnast_nid& parent_node, std::string_view mport_name) {
  auto mem_name     = mport2mem[mport_name];
  auto mem_port_str = mem2port_cnt[mem_name];

  auto it = mport_usage_visited.find(mport_name);
  if (it == mport_usage_visited.end()) {
    mport_usage_visited.emplace(mport_name);

    auto idx_ta_mrdport = lnast.add_child(parent_node, Lnast_node::create_tuple_add());
    lnast.add_child(idx_ta_mrdport, Lnast_node::create_ref(absl::StrCat(mem_name, "_rdport")));
    lnast.add_child(idx_ta_mrdport, Lnast_node::create_const(mem_port_str));
    lnast.add_child(idx_ta_mrdport, Lnast_node::create_const("true"));

    auto it2 = mem2rd_mports.find(mem_name);
    if (it2 == mem2rd_mports.end()) {
      mem2rd_mports.insert(std::pair<std::string, std::vector<std::pair<std::string, uint8_t>>>(
          mem_name,
          {std::pair(std::string(mport_name), mem2port_cnt[mem_name])}));
    } else {
      mem2rd_mports[mem_name].emplace_back(std::pair(mport_name, mem2port_cnt[mem_name]));
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
    // lnast.add_child(idx_tg, Lnast_node::create_ref(absl::StrCat(mem_name, "_res"))));
    // lnast.add_child(idx_tg, Lnast_node::create_const(mem_port_str));

    // auto idx_asg = lnast.add_child(idx_initialize_stmts, Lnast_node::create_assign());
    // lnast.add_child(idx_asg, Lnast_node::create_ref(lnast.add_string(mport_name)));
    // lnast.add_child(idx_asg, Lnast_node::create_ref(temp_var_name));
  }
}

void Inou_firrtl_module::initialize_wr_mport_from_usage(Lnast& lnast, Lnast_nid& parent_node, std::string_view mport_name) {
  auto mem_name     = mport2mem[mport_name];
  auto port_cnt     = mem2port_cnt[mem_name];

  //
  auto it           = mport_usage_visited.find(mport_name);
  if (it == mport_usage_visited.end()) {
    auto& idx_initialize_stmts = mem2initial_idx[mem_name];
    mem2one_wr_mport.insert_or_assign(mem_name, port_cnt);

    mport_usage_visited.emplace(mport_name);

    auto idx_ta_mrdport = lnast.add_child(idx_initialize_stmts, Lnast_node::create_tuple_add());
    lnast.add_child(idx_ta_mrdport, Lnast_node::create_ref(absl::StrCat(mem_name, "_rdport")));
    lnast.add_child(idx_ta_mrdport, Lnast_node::create_const(port_cnt));
    lnast.add_child(idx_ta_mrdport, Lnast_node::create_const("false"));

    auto idx_attr_get     = lnast.add_child(idx_initialize_stmts, Lnast_node::create_attr_get());
    auto mport_last_value = create_tmp_var();
    lnast.add_child(idx_attr_get, Lnast_node::create_ref(mport_last_value));
    lnast.add_child(idx_attr_get, Lnast_node::create_ref(mport_name));
    lnast.add_child(idx_attr_get, Lnast_node::create_const("__last_value"));

    auto idx_ta_mdin = lnast.add_child(idx_initialize_stmts, Lnast_node::create_tuple_add());
    lnast.add_child(idx_ta_mdin, Lnast_node::create_ref(absl::StrCat(mem_name, "_din")));
    lnast.add_child(idx_ta_mdin, Lnast_node::create_const(port_cnt));
    lnast.add_child(idx_ta_mdin, Lnast_node::create_ref(mport_last_value));
  }

  
  // FIXME->sh: mask related, comment out for now
  (void) parent_node;
  // auto it2 = mport2mask_bitvec.find(mport_name);
  // if (it2 != mport2mask_bitvec.end()) {
  //   auto bitvec = mport2mask_bitvec[mport_name];
  //   auto shtamt = mport2mask_cnt[mport_name];
  //   bitvec      = bitvec | 1 << shtamt;

  //   auto idx_ta_men = lnast.add_child(parent_node, Lnast_node::create_tuple_add());
  //   lnast.add_child(idx_ta_men, Lnast_node::create_ref(absl::StrCat(mem_name, "_enable")));
  //   lnast.add_child(idx_ta_men, Lnast_node::create_const(port_cnt_str));
  //   lnast.add_child(idx_ta_men, Lnast_node::create_const(bitvec));

  //   mport2mask_bitvec[mport_name] = bitvec;
  //   mport2mask_cnt[mport_name]++;
  // }
}

void Inou_firrtl_module::set_leaf_type(std::string_view subname, std::string_view full_name, size_t prev,
                                       std::vector<std::pair<std::string, Inou_firrtl_module::Leaf_type>>& hier_subnames) {
  if (prev == 0) {
    hier_subnames.emplace_back(std::pair(subname, Leaf_type::Ref));
  } else if (full_name[prev - 1] == '.') {
    auto first_char = subname[0];
    if (isdigit(first_char) || first_char == '-' || first_char == '+') {
      hier_subnames.emplace_back(std::pair(subname, Leaf_type::Const_num));
    } else {
      hier_subnames.emplace_back(std::pair(subname, Leaf_type::Const_str));
    }
  } else if (full_name[prev - 1] == '[') {
    auto first_char = subname[0];
    if (isdigit(first_char) || first_char == '-' || first_char == '+') {
      hier_subnames.emplace_back(std::pair(subname, Leaf_type::Const_num));
    } else {
      hier_subnames.emplace_back(std::pair(subname, Leaf_type::Ref));
    }
  }
}

void Inou_firrtl_module::split_hier_name(std::string_view full_name, std::vector<std::string>& hier_subnames) {
  std::size_t prev = 0;
  std::size_t pos = full_name.find('.', prev);

  while (pos != std::string::npos) {
    if (pos > prev) {
      auto subname = full_name.substr(prev, pos - prev);
      if (subname.back() == ']') {
        subname = subname.substr(0, subname.size() - 1);  // exclude ']'
      }
      hier_subnames.emplace_back(subname);
    }
    prev = pos + 1;
    pos = full_name.find('.', prev);
  }

  if (prev < full_name.size()) {
    auto subname = full_name.substr(prev);
    if (subname.back() == ']') {
      subname = subname.substr(0, subname.size() - 1);  // exclude ']'
    }
    hier_subnames.emplace_back(subname);
  }
}

void Inou_firrtl_module::split_hier_name(std::string_view full_name, std::vector<std::pair<std::string, Inou_firrtl_module::Leaf_type>>& hier_subnames) {
  std::size_t prev = 0;

  std::size_t pos = full_name.find('.', prev);
  while (pos != std::string::npos) {
    if (pos > prev) {
      auto subname = full_name.substr(prev, pos - prev);
      if (subname.back() == ']') {
        subname = subname.substr(0, subname.size() - 1);  // exclude ']'
      }
      set_leaf_type(subname, full_name, prev, hier_subnames);
    }
    prev = pos + 1;
    pos = full_name.find('.', prev);
  }

  if (prev < full_name.size()) {
    auto subname = full_name.substr(prev);
    if (subname.back() == ']') {
      subname = subname.substr(0, subname.size() - 1);  // exclude ']'
    }
    set_leaf_type(subname, full_name, prev, hier_subnames);
  }
}


void Inou_firrtl_module::direct_instances_connection(Lnast &lnast, Lnast_nid &parent_node, std::string lhs_full_name, std::string rhs_full_name) {
  I(str_tools::contains(lhs_full_name, '.'));
  I(str_tools::contains(rhs_full_name, '.'));

  // create TG for the rhs instances
  auto tg_node = lnast.add_child(parent_node, Lnast_node::create_tuple_get());
  auto temp_var_str = create_tmp_var();

  auto pos = rhs_full_name.find('.');
  auto tg_head = rhs_full_name.substr(0, pos);
  auto tg_merged_fields = std::string{rhs_full_name.substr(pos + 1)};
  std::replace(tg_merged_fields.begin(), tg_merged_fields.end(), '.', '_');
  lnast.add_child(tg_node, Lnast_node::create_ref(temp_var_str));
  lnast.add_child(tg_node, Lnast_node::create_ref(absl::StrCat("otup_", tg_head)));
  lnast.add_child(tg_node, Lnast_node::create_const(tg_merged_fields));

  // create TA for the lhs instances
  auto ta_node = lnast.add_child(parent_node, Lnast_node::create_tuple_add());
  auto pos2 = lhs_full_name.find('.');
  auto ta_head = lhs_full_name.substr(0, pos2);
  auto ta_merged_fields = std::string{lhs_full_name.substr(pos2 + 1)};
  std::replace(ta_merged_fields.begin(), ta_merged_fields.end(), '.', '_');
  lnast.add_child(ta_node, Lnast_node::create_ref(absl::StrCat("itup_", ta_head)));
  lnast.add_child(ta_node, Lnast_node::create_const(ta_merged_fields));
  lnast.add_child(ta_node, Lnast_node::create_ref(temp_var_str));

  return;
}

void Inou_firrtl_module::create_tuple_add_for_instance_itup(Lnast& lnast, Lnast_nid& parent_node, std::string_view lhs_full_name, std::string rhs_full_name) {
  I(absl::StrContains(lhs_full_name, '.'));
  auto selc_node = lnast.add_child(parent_node, Lnast_node::create_tuple_add());
  auto pos = lhs_full_name.find('.');
  auto tup_head = lhs_full_name.substr(0, pos);
  auto tup_merged_fields = std::string{lhs_full_name.substr(pos + 1)};
  std::replace(tup_merged_fields.begin(), tup_merged_fields.end(), '.', '_');
  std::replace(rhs_full_name.begin(), rhs_full_name.end(), '.', '_');

  auto first_char = rhs_full_name[0];
  Lnast_node value_node;
  if (isdigit(first_char) || first_char == '-' || first_char == '+') {
    value_node = Lnast_node::create_const(rhs_full_name);
  } else {
    value_node = Lnast_node::create_ref(rhs_full_name);
  }

  lnast.add_child(selc_node, Lnast_node::create_ref(absl::StrCat("itup_", tup_head)));
  lnast.add_child(selc_node, Lnast_node::create_const(tup_merged_fields));
  lnast.add_child(selc_node, value_node);
  create_wires_for_instance_itup(lnast, parent_node, lhs_full_name, rhs_full_name);
}

void Inou_firrtl_module::create_wires_for_instance_itup(Lnast& lnast, Lnast_nid& parent_node, std::string_view lhs_full_name, std::string_view rhs_full_name) {
  I(absl::StrContains(lhs_full_name, '.'));
  auto lhs = name_prefix_modifier_flattener(lhs_full_name, false);
  auto rhs = name_prefix_modifier_flattener(rhs_full_name, true);
  auto asg_node = lnast.add_child(parent_node, Lnast_node::create_assign());
  lnast.add_child(asg_node, Lnast_node::create_ref(lhs));

  auto first_char = rhs_full_name[0];
  Lnast_node value_node;
  if (isdigit(first_char) || first_char == '-' || first_char == '+') {
    value_node = Lnast_node::create_const(rhs_full_name);
  } else {
    value_node = Lnast_node::create_ref(rhs_full_name);
  }
  lnast.add_child(asg_node, value_node);
}

void Inou_firrtl_module::create_tuple_get_for_instance_otup(Lnast& lnast, Lnast_nid& parent_node, std::string_view rhs_full_name, std::string lhs_full_name) {
  I(absl::StrContains(rhs_full_name, '.'));

  auto selc_node = lnast.add_child(parent_node, Lnast_node::create_tuple_get());
  std::replace(lhs_full_name.begin(), lhs_full_name.end(), '.', '_');
  lnast.add_child(selc_node, Lnast_node::create_ref(lhs_full_name));
  auto pos = rhs_full_name.find('.');
  auto tup_head = rhs_full_name.substr(0, pos);
  auto tup_merged_fields = std::string{rhs_full_name.substr(pos + 1)};
  std::replace(tup_merged_fields.begin(), tup_merged_fields.end(), '.', '_');
  lnast.add_child(selc_node, Lnast_node::create_ref(absl::StrCat("otup_", tup_head)));
  lnast.add_child(selc_node, Lnast_node::create_const(tup_merged_fields));
}

void Inou_firrtl_module::create_tuple_get_from_str(Lnast& ln, Lnast_nid& parent_node, std::string_view full_name, const Lnast_node& target_node) {
  I(absl::StrContains(full_name, '.'));

  std::vector<std::pair<std::string, Inou_firrtl_module::Leaf_type>> hier_subnames;
  split_hier_name(full_name, hier_subnames);
  auto selc_node = ln.add_child(parent_node, Lnast_node::create_tuple_get());
  ln.add_child(selc_node, target_node);

  for (const auto &subname : hier_subnames) {
    std::string field_name = subname.first;
    if (inst2module.count(subname.first)) {
      field_name = absl::StrCat("otup_", field_name);
    }
    switch (subname.second) {
      case Leaf_type::Ref: {
        ln.add_child(selc_node, Lnast_node::create_ref(field_name));
        break;
      }
      case Leaf_type::Const_num:
      case Leaf_type::Const_str: {
        ln.add_child(selc_node, Lnast_node::create_const(field_name));
        break;
      }
      default: Pass::error("Unknown port type.");
    }
  }
}



void Inou_firrtl_module::create_tuple_add_from_str(Lnast& ln, Lnast_nid& parent_node, std::string_view full_name, const Lnast_node& value_node) {
  I(absl::StrContains(full_name, '.'));

  std::vector<std::pair<std::string, Inou_firrtl_module::Leaf_type>> hier_subnames;
  split_hier_name(full_name, hier_subnames);
  auto selc_node = ln.add_child(parent_node, Lnast_node::create_tuple_add());
  
  for (const auto &subname : hier_subnames) {
    std::string field_name = subname.first;
    if (inst2module.count(subname.first)) {
      field_name = absl::StrCat("itup_", field_name);
    }
    switch (subname.second) {
      case Leaf_type::Ref: {
        ln.add_child(selc_node, Lnast_node::create_ref(field_name));
        break;
      }
      case Leaf_type::Const_num:
      case Leaf_type::Const_str: {
        ln.add_child(selc_node, Lnast_node::create_const(field_name));
        break;
      }
      default: Pass::error("Unknown port type.");
    }
  }

  ln.add_child(selc_node, value_node);
}


//----------Ports-------------------------

/* This function iterates over the IO of a module and sets
 * the bitwidth + sign of each using a dot node in LNAST. */
void Inou_firrtl_module::list_port_info(Lnast& lnast, const firrtl::FirrtlPB_Port& port, Lnast_nid parent_node) {
  // Terms in port_list as follows: <name, direction, bits, sign>
  std::vector<std::tuple<std::string, uint8_t, uint32_t, bool>> port_list;
  Inou_firrtl::create_io_list(port.type(), port.direction(), port.id(), port_list);

  for (auto val : port_list) {
    auto port_name = std::get<0>(val);
    auto port_dir  = std::get<1>(val);
    auto port_bits = std::get<2>(val);
    auto port_sign = std::get<3>(val);

    std::string full_port_name;
    if (port_dir == firrtl::FirrtlPB_Port_Direction::FirrtlPB_Port_Direction_PORT_DIRECTION_IN) {
      record_all_input_hierarchy(port_name);
      full_port_name = absl::StrCat("$", port_name);
    } else if (port_dir == firrtl::FirrtlPB_Port_Direction::FirrtlPB_Port_Direction_PORT_DIRECTION_OUT) {
      record_all_output_hierarchy(port_name);
      full_port_name = absl::StrCat("%", port_name);
    } else {
      Pass::error("Found IO port {} specified with unknown direction in Protobuf message.", port_name);
    }

    std::replace(full_port_name.begin(), full_port_name.end(), '.', '_');

    if (port_bits > 0) {  
      // set default value 0 for all module outputs
      if (full_port_name.substr(0,1) == "%") {
        auto zero_node = Lnast_node::create_const(0);
        create_default_value_for_scalar_var(lnast, parent_node, full_port_name, zero_node);
      }

      // specify __bits for both inp/out
      auto value_node = Lnast_node::create_const(port_bits);
      auto extension  = port_sign ? ".__sbits" : ".__ubits";
      create_tuple_add_from_str(lnast, parent_node, absl::StrCat(full_port_name, extension), value_node);
    }
  }
}

  
void Inou_firrtl_module::create_default_value_for_scalar_var(Lnast &ln, Lnast_nid &parent_node, std::string_view sv, const Lnast_node &value_node) {
  auto idx_asg = ln.add_child(parent_node, Lnast_node::create_assign());
  ln.add_child(idx_asg, Lnast_node::create_ref(sv));
  ln.add_child(idx_asg, value_node);
}


void Inou_firrtl_module::record_all_input_hierarchy(std::string_view port_name) {
  std::size_t pos = port_name.size();
  while (pos != std::string::npos) {
    auto tmp = port_name.substr(0, pos);
    input_names.emplace(tmp);
    pos = tmp.rfind('.');
  }
}

void Inou_firrtl_module::record_all_output_hierarchy(std::string_view port_name) {
  std::size_t pos = port_name.size();
  while (pos != std::string::npos) {
    auto tmp = port_name.substr(0, pos);
    output_names.emplace(tmp);
    pos = tmp.rfind('.');
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
void Inou_firrtl_module::list_prime_op_info(Lnast& lnast, const firrtl::FirrtlPB_Expression_PrimOp& op, Lnast_nid& parent_node,
                                            std::string_view lhs) {
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
      handle_binary_op(lnast, op, parent_node, lhs);
      break;
    }

    case firrtl::FirrtlPB_Expression_PrimOp_Op_OP_TAIL: {  // take in some 'n', returns value with 'n' MSBs removed
      handle_tail_op(lnast, op, parent_node, lhs);
      break;
    }

    case firrtl::FirrtlPB_Expression_PrimOp_Op_OP_HEAD: {  // take in some 'n', returns 'n' MSBs of variable invoked on
      handle_head_op(lnast, op, parent_node, lhs);
      break;
    }

    case firrtl::FirrtlPB_Expression_PrimOp_Op_OP_SHIFT_LEFT:
    case firrtl::FirrtlPB_Expression_PrimOp_Op_OP_SHIFT_RIGHT: {
      handle_static_shift_op(lnast, op, parent_node, lhs);
      break;
    }

    case firrtl::FirrtlPB_Expression_PrimOp_Op_OP_BIT_NOT: {
      handle_unary_op(lnast, op, parent_node, lhs);
      break;
    }

    case firrtl::FirrtlPB_Expression_PrimOp_Op_OP_CONCAT: {
      handle_concat_op(lnast, op, parent_node, lhs);
      break;
    }

    case firrtl::FirrtlPB_Expression_PrimOp_Op_OP_PAD: {
      handle_pad_op(lnast, op, parent_node, lhs);
      break;
    }

    case firrtl::FirrtlPB_Expression_PrimOp_Op_OP_NEG: {  // takes a # (UInt or SInt) and returns it * -1
      handle_negate_op(lnast, op, parent_node, lhs);
      break;
    }
    case firrtl::FirrtlPB_Expression_PrimOp_Op_OP_CONVERT: {
      handle_conv_op(lnast, op, parent_node, lhs);
      break;
    }
    case firrtl::FirrtlPB_Expression_PrimOp_Op_OP_EXTRACT_BITS: {
      handle_extract_bits_op(lnast, op, parent_node, lhs);
      break;
    }

    case firrtl::FirrtlPB_Expression_PrimOp_Op_OP_AS_UINT:
    case firrtl::FirrtlPB_Expression_PrimOp_Op_OP_AS_SINT: {
      handle_as_usint_op(lnast, op, parent_node, lhs);
      break;
    }

    case firrtl::FirrtlPB_Expression_PrimOp_Op_OP_AS_CLOCK:
    case firrtl::FirrtlPB_Expression_PrimOp_Op_OP_AS_FIXED_POINT:
    case firrtl::FirrtlPB_Expression_PrimOp_Op_OP_AS_ASYNC_RESET: {
      handle_type_conv_op(lnast, op, parent_node, lhs);
      break;
    }
    case firrtl::FirrtlPB_Expression_PrimOp_Op_OP_XOR_REDUCE: {
      handle_xor_reduce_op(lnast, op, parent_node, lhs);
      break;
    }
    case firrtl::FirrtlPB_Expression_PrimOp_Op_OP_AND_REDUCE: {
      handle_and_reduce_op(lnast, op, parent_node, lhs);
      break;
    }
    case firrtl::FirrtlPB_Expression_PrimOp_Op_OP_OR_REDUCE: {
      handle_or_reduce_op(lnast, op, parent_node, lhs);
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
void Inou_firrtl_module::init_expr_add(Lnast& lnast, const firrtl::FirrtlPB_Expression& rhs_expr, Lnast_nid& parent_node, std::string_view lhs_noprefixes) {
  // Note: here, parent_node is the "stmt" node above where this expression will go.
  I(lnast.get_data(parent_node).type.is_stmts());
  auto lhs_str = name_prefix_modifier_flattener(lhs_noprefixes, false);
  switch (rhs_expr.expression_case()) {
    case firrtl::FirrtlPB_Expression::kReference: {  // Reference
      auto tmp_rhs_str = rhs_expr.reference().id();
      std::string rhs_str;
      if (is_invalid_table.find(tmp_rhs_str) != is_invalid_table.end()) {
        // create __last_value
        auto idx_attr_get = lnast.add_child(parent_node, Lnast_node::create_attr_get());
        auto temp_var_str = create_tmp_var();
        lnast.add_child(idx_attr_get, Lnast_node::create_ref(temp_var_str));
        lnast.add_child(idx_attr_get, Lnast_node::create_ref(tmp_rhs_str));
        lnast.add_child(idx_attr_get, Lnast_node::create_const("__last_value"));
        rhs_str = temp_var_str;
      } else {
        rhs_str = name_prefix_modifier_flattener(rhs_expr.reference().id(), true);
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
      // } else if (lhs_str.substr(0, 1) == "_") {  // lhs is declared as kNode
      } else if (node_names.find(lhs_str) != node_names.end()) {  // lhs is declared as kNode
        auto idx_asg = lnast.add_child(parent_node, Lnast_node::create_assign());
        lnast.add_child(idx_asg, Lnast_node::create_ref(lhs_str));
        lnast.add_child(idx_asg, Lnast_node::create_ref(rhs_str));
      } else {
        auto idx_asg = lnast.add_child(parent_node, Lnast_node::create_dp_assign());
        lnast.add_child(idx_asg, Lnast_node::create_ref(lhs_str));
				attach_expr_str2node(lnast, rhs_str, idx_asg);
      }
      break;
    }
    case firrtl::FirrtlPB_Expression::kUintLiteral: {  // UIntLiteral
      Lnast_nid idx_asg;
      idx_asg = lnast.add_child(parent_node, Lnast_node::create_assign());
      lnast.add_child(idx_asg, Lnast_node::create_ref(lhs_str));
      auto str_val = rhs_expr.uint_literal().value().value();
      lnast.add_child(idx_asg, Lnast_node::create_const(str_val));
      break;
    }
    case firrtl::FirrtlPB_Expression::kSintLiteral: {  // SIntLiteral
      Lnast_nid idx_conv;
      idx_conv = lnast.add_child(parent_node, Lnast_node::create_func_call());

      auto temp_var_str = create_tmp_var(); // ___F6
      auto tmp_node = Lnast_node::create_ref(temp_var_str);
      lnast.add_child(idx_conv, tmp_node);
      lnast.add_child(idx_conv, Lnast_node::create_const("__fir_as_sint"));

      auto str_val = rhs_expr.sint_literal().value().value();
      lnast.add_child(idx_conv, Lnast_node::create_const(str_val));

      Lnast_nid idx_asg;
      idx_asg = lnast.add_child(parent_node, Lnast_node::create_assign());
      lnast.add_child(idx_asg, Lnast_node::create_ref(lhs_str));
      lnast.add_child(idx_asg, Lnast_node::create_ref(temp_var_str));
      break;
    }
    case firrtl::FirrtlPB_Expression::kValidIf: {  // ValidIf
      handle_valid_if_assign(lnast, rhs_expr, parent_node, lhs_str);
      break;
    }
    case firrtl::FirrtlPB_Expression::kMux: {  // Mux
      handle_mux_assign(lnast, rhs_expr, parent_node, lhs_str);
      break;
    }
    case firrtl::FirrtlPB_Expression::kPrimOp: {  // PrimOp
      list_prime_op_info(lnast, rhs_expr.prim_op(), parent_node, lhs_str);
      break;
    }
    case firrtl::FirrtlPB_Expression::kSubField:
    case firrtl::FirrtlPB_Expression::kSubIndex:   // SubIndex
    case firrtl::FirrtlPB_Expression::kSubAccess: {  // SubAccess
      I(false); // tuple/vector related stuff already handled in connect statement
      break;
    }
    case firrtl::FirrtlPB_Expression::kFixedLiteral: {  // FixedLiteral
      // FIXME: FixedPointLiteral not yet supported in LNAST
      I(false);
      break;
    }
    default: Pass::error("In init_expr_add, found unknown expression type: {}", rhs_expr.expression_case());
  }
}

/* Given an expression that may or may
 * not have hierarchy, flatten it. */
std::string Inou_firrtl_module::get_expr_hier_name(Lnast& lnast, Lnast_nid& parent_node, const firrtl::FirrtlPB_Expression& expr) {
  if (expr.has_sub_field()) {
    return absl::StrCat(get_expr_hier_name(lnast, parent_node, expr.sub_field().expression()), ".", expr.sub_field().field());
  } else if (expr.has_sub_access()) {
    auto idx_str = get_expr_hier_name(lnast, parent_node, expr.sub_access().index());
    return absl::StrCat(get_expr_hier_name(lnast, parent_node, expr.sub_access().expression()), ".", idx_str);
  } else if (expr.has_sub_index()) {
    return absl::StrCat(get_expr_hier_name(lnast, parent_node, expr.sub_index().expression()), ".", expr.sub_index().index().value());
  } else if (expr.has_reference()) {
    return expr.reference().id();
  } else if (expr.has_prim_op()) {
		auto expr_string = create_tmp_var();
		list_prime_op_info(lnast, expr.prim_op(), parent_node, expr_string);
		return expr_string;
	} else if (expr.has_uint_literal()) {
    return absl::StrCat(expr.uint_literal().value().value(), "ubits", expr.uint_literal().width().value());
	} else {
    return "";
  }
}

std::string Inou_firrtl_module::get_expr_hier_name(const firrtl::FirrtlPB_Expression &expr, bool &is_runtime_idx) {
  if (expr.has_sub_field()) {
    return absl::StrCat(get_expr_hier_name(expr.sub_field().expression(), is_runtime_idx), ".", expr.sub_field().field());
  } else if (expr.has_sub_access()) {
    auto idx_str = expr.sub_access().index().uint_literal().value().value();
    if (!isdigit(idx_str[0])) {
      is_runtime_idx = true;
    }
    return absl::StrCat(get_expr_hier_name(expr.sub_access().expression(), is_runtime_idx), ".", idx_str);
  } else if (expr.has_sub_index()) {
    return absl::StrCat(get_expr_hier_name(expr.sub_index().expression(), is_runtime_idx), ".", expr.sub_index().index().value());
  } else if (expr.has_reference()) {
    return expr.reference().id();
  } else if (expr.has_uint_literal()) {
		return expr.uint_literal().value().value(); 
  } else if (expr.has_sint_literal()) {
		return expr.sint_literal().value().value(); 
	} else {
    return "";
  }
}

/* the new fir2lnast design want to have as many hierarchical flattened 
 * wires as possible, the only exception comes from the connections of sub-module instance io.
 * The sub-module connections will be involved with TupleGet from the instance. This is handled
 * in the old fir2lnast. So,
 * (1) check if the operand comes from instance
 * (2) if yes, follow the old mechanism: create tuple_gets to retrieve the hierarchical field value
 * (3) if not, flattened the operand str and create a simple assignment to the prime_op/mux/valid_if ... etc
 * */
std::string Inou_firrtl_module::expr_str_flattened_or_tg(Lnast &lnast, Lnast_nid &parent_node, const firrtl::FirrtlPB_Expression& operand_expr) {
  std::string expr_str;
	
	bool is_runtime_idx_r = false;
	std::string expr_str_tmp = get_expr_hier_name(operand_expr, is_runtime_idx_r);
	
	if (is_runtime_idx_r) {
		auto tmp_var = create_tmp_var();
		handle_rhs_runtime_idx(lnast, parent_node, tmp_var, expr_str_tmp, operand_expr);
		return tmp_var;
	}

  // here we only want to check the case of instance connection, so kSubField is sufficient
  auto expr_case = operand_expr.expression_case();
  if (expr_case == firrtl::FirrtlPB_Expression::kPrimOp) {
		expr_str = get_expr_hier_name(lnast, parent_node, operand_expr);
  } else if (expr_case == firrtl::FirrtlPB_Expression::kUintLiteral) {
    expr_str = absl::StrCat(operand_expr.uint_literal().value().value(), "ubits", operand_expr.uint_literal().width().value());
	} else {
    expr_str = name_prefix_modifier_flattener(expr_str_tmp, true);
  }
  return expr_str;
}

void Inou_firrtl_module::add_lnast_assign(Lnast& lnast, Lnast_nid &parent_node, std::string_view lhs, std::string_view rhs) {
  Lnast_nid idx_asg = lnast.add_child(parent_node, Lnast_node::create_assign());
  lnast.add_child(idx_asg, Lnast_node::create_ref(lhs));
  auto first_char = rhs[0];
  if (isdigit(first_char) || first_char == '-' || first_char == '+') {
    lnast.add_child(idx_asg, Lnast_node::create_const(rhs));
  } else {
    lnast.add_child(idx_asg, Lnast_node::create_ref(rhs));
  }
}

/* This function takes in a string and adds it into the LNAST as
 * a child of the provided "parent_node". Note: the access_str should
 * already have any $/%/#/__q_pin added to it before this is called. */
void Inou_firrtl_module::attach_expr_str2node(Lnast& lnast, std::string_view access_str, Lnast_nid& parent_node) {
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
void Inou_firrtl_module::setup_register_q_pin(Lnast& lnast, Lnast_nid& parent_node, std::string_view reg_name) {
  auto flop_qpin_var = absl::StrCat("_#_", reg_name, "_q");
  auto idx_asg2      = lnast.add_child(parent_node, Lnast_node::create_assign());
  lnast.add_child(idx_asg2, Lnast_node::create_ref(flop_qpin_var));
  lnast.add_child(idx_asg2, Lnast_node::create_ref(absl::StrCat("#", reg_name)));

  reg2qpin.insert_or_assign(reg_name, flop_qpin_var);
}

void Inou_firrtl_module::declare_register(Lnast& lnast, Lnast_nid& parent_node, std::string_view reg_name) {
  auto idx_attget         = lnast.add_child(parent_node, Lnast_node::create_attr_get());
  auto full_register_name = absl::StrCat("#", reg_name);
  auto tmp_var_str        = create_tmp_var();
  lnast.add_child(idx_attget, Lnast_node::create_ref(tmp_var_str));
  lnast.add_child(idx_attget, Lnast_node::create_ref(full_register_name));
  lnast.add_child(idx_attget, Lnast_node::create_const("__create_flop"));

  auto idx_asg = lnast.add_child(parent_node, Lnast_node::create_assign());
  lnast.add_child(idx_asg, Lnast_node::create_ref(full_register_name));
  lnast.add_child(idx_asg, Lnast_node::create_ref(tmp_var_str));
}

void Inou_firrtl_module::setup_register_reset_init(Lnast& lnast, Lnast_nid& parent_node, std::string_view reg_raw_name,
                                                   const firrtl::FirrtlPB_Expression& resete,
                                                   const firrtl::FirrtlPB_Expression& inite,
                                                   std::string_view head_chopped_hier_name, bool bits_set_done) {

  bool tied0_reset = false;
  auto resete_case = resete.expression_case();

  Lnast_node value_node;

  if (resete_case == firrtl::FirrtlPB_Expression::kUintLiteral || resete_case == firrtl::FirrtlPB_Expression::kSintLiteral) {
    auto str_val = resete.uint_literal().value().value();
    value_node   = Lnast_node::create_const(str_val);
    if (str_val == "0")
      tied0_reset = true;
  } else if (resete_case == firrtl::FirrtlPB_Expression::kReference) {
    auto ref_str = name_prefix_modifier_flattener(resete.reference().id(), true);
    value_node   = Lnast_node::create_ref(ref_str);
  }

  if (!value_node.is_invalid())
    create_tuple_add_from_str(lnast, parent_node, absl::StrCat("#", reg_raw_name, ".__reset"), value_node);

  if (tied0_reset) {
    return;
  }

  Lnast_node initial_node;

  auto inite_case = inite.expression_case();
  if (inite_case == firrtl::FirrtlPB_Expression::kUintLiteral || inite_case == firrtl::FirrtlPB_Expression::kSintLiteral) {
    auto str_val = inite.uint_literal().value().value();
    initial_node = Lnast_node::create_const(str_val);

    if (!bits_set_done) {
      auto bits = inite.uint_literal().width().value();
      // if (bits == 0) 
      //   bits = 1;
      setup_scalar_bits(lnast, absl::StrCat("#", reg_raw_name), bits, parent_node, false);
    }

  } else if (inite_case == firrtl::FirrtlPB_Expression::kReference) {
    // (void) head_chopped_hier_name;
    auto ref_str_pre = inite.reference().id();
    std::string ref_str;
    if (head_chopped_hier_name != "") {
      ref_str = absl::StrCat(ref_str_pre, ".", head_chopped_hier_name);
      ref_str = name_prefix_modifier_flattener(ref_str, true);
    } else {
      ref_str = ref_str_pre;
    }

    initial_node = Lnast_node::create_ref(ref_str);
  }

  if (!initial_node.is_invalid())
    create_tuple_add_from_str(lnast, parent_node, absl::StrCat("#", reg_raw_name, ".__initial"), initial_node);
}

void Inou_firrtl_module::dump_var2flip(const absl::flat_hash_map<std::string, absl::btree_set<std::pair<std::string, bool>>> &module_var2flip) {
  (void)module_var2flip;
#ifndef NDEBUG  
  for (auto &[var, set] : module_var2flip) {
    fmt::print("var:{} \n", var);
    for (auto &set_itr : set) {
      fmt::print("  hier_name:{:<20}, accu_flipped:{:<5}\n", set_itr.first, set_itr.second); 
    }
  }
#endif
}

// the sub-fields of lhs and rhs are the same, so we just pass the head of lhs
// (tup_l), and we could avoid traversing the big var2flip table again to get
// lhs flattened element.  
// e.g. tup_l == foo, rhs == bar.a.b.c, is_flipped == true
// then: bar.a.b.c = foo.a.b.c
void Inou_firrtl_module::tuple_flattened_connections(Lnast& lnast, Lnast_nid& parent_node, std::string_view hier_name_l_ori, std::string_view hier_name_r_ori, std::string_view flattened_element, bool is_flipped) {
  // 0. swap lhs/rhs if needed
  if (is_flipped) {
    auto tmp = hier_name_l_ori;
    hier_name_l_ori = hier_name_r_ori;
    hier_name_r_ori = tmp;
  }
  
  // 1. decorate io prefix
  std::string hier_name_r, hier_name_l;
  if (input_names.find(hier_name_r_ori) != input_names.end()) {
    hier_name_r = absl::StrCat("$", hier_name_r_ori);
  } else if (reg2qpin.find(hier_name_r_ori) != reg2qpin.end()) {
    hier_name_r = name_prefix_modifier_flattener(hier_name_r_ori, true);
  } else {
    hier_name_r = hier_name_r_ori;
  }

  if (output_names.find(hier_name_l_ori) != output_names.end()) {
    hier_name_l = absl::StrCat("%", hier_name_l_ori);
  } else if (reg2qpin.find(hier_name_l_ori) != reg2qpin.end()) {
    hier_name_l = name_prefix_modifier_flattener(hier_name_l_ori, false);
  } else {
    hier_name_l = hier_name_l_ori;
  }

  std::string_view chop_head_flattened_element;
  if (is_flipped) {
    chop_head_flattened_element = flattened_element.substr(hier_name_r_ori.length());
  } else {
    chop_head_flattened_element = flattened_element.substr(hier_name_l_ori.length());
  }

  auto lhs_full_name = absl::StrCat(hier_name_l, chop_head_flattened_element);
  auto rhs_full_name = absl::StrCat(hier_name_r, chop_head_flattened_element);

  std::string rhs_wire_name;
  auto pos = rhs_full_name.find('.');
  if (pos != std::string::npos) {
    rhs_wire_name = rhs_full_name.substr(0, pos);
  }

  lhs_full_name = name_prefix_modifier_flattener(lhs_full_name, false);
  rhs_full_name = name_prefix_modifier_flattener(rhs_full_name, true);

  auto it = wire_names.find(rhs_wire_name);
  bool rhs_is_wire_var = it != wire_names.end();
  if (rhs_is_wire_var) {
    auto temp_var_name = create_tmp_var();
    auto attr_get_node = lnast.add_child(parent_node, Lnast_node::create_attr_get());
    lnast.add_child(attr_get_node, Lnast_node::create_ref(temp_var_name));
    lnast.add_child(attr_get_node, Lnast_node::create_ref(rhs_full_name));
    lnast.add_child(attr_get_node, Lnast_node::create_const("__last_value"));

    auto idx_asg = lnast.add_child(parent_node, Lnast_node::create_assign());
    lnast.add_child(idx_asg, Lnast_node::create_ref(lhs_full_name));
    lnast.add_child(idx_asg, Lnast_node::create_ref(temp_var_name));
  } else if (node_names.find(lhs_full_name) != node_names.end()) {  // lhs is declared as kNode
    auto idx_asg = lnast.add_child(parent_node, Lnast_node::create_assign());
    lnast.add_child(idx_asg, Lnast_node::create_ref(lhs_full_name));
    lnast.add_child(idx_asg, Lnast_node::create_ref(rhs_full_name));
  } else if (lhs_full_name.at(0) == '#') {
    // auto idx_asg = lnast.add_child(parent_node, Lnast_node::create_dp_assign());
    auto idx_asg = lnast.add_child(parent_node, Lnast_node::create_assign());
    lnast.add_child(idx_asg, Lnast_node::create_ref(lhs_full_name));
		attach_expr_str2node(lnast, rhs_full_name, idx_asg);
  } else {
    auto idx_asg = lnast.add_child(parent_node, Lnast_node::create_assign());
    lnast.add_child(idx_asg, Lnast_node::create_ref(lhs_full_name));
		attach_expr_str2node(lnast, rhs_full_name, idx_asg);
  }
  return;
}

void Inou_firrtl_module::list_statement_info(Lnast& lnast, const firrtl::FirrtlPB_Statement& stmt, Lnast_nid& parent_node) {
  switch (stmt.statement_case()) {
    case firrtl::FirrtlPB_Statement::kWire: {
      auto stmt_wire_id = stmt.wire().id();
      wire_names.insert(stmt_wire_id);
      wire_init_flip_handling(lnast, stmt.wire().type(), stmt_wire_id, false, parent_node);
      break;
    }
    case firrtl::FirrtlPB_Statement::kRegister: {
      auto stmt_reg_id = stmt.register_().id();
      // step-I: recursively collect reg info into the var2flip-table 
      handle_register(lnast, stmt.register_().type(), stmt_reg_id, parent_node, stmt);
      break;
    }
    case firrtl::FirrtlPB_Statement::kMemory: {
      I(false, "never happen in chirrtl");
      break;
    }
    case firrtl::FirrtlPB_Statement::kCmemory: {
      memory_names.insert(stmt.cmemory().id());
      init_cmemory(lnast, parent_node, stmt.cmemory());
      break;
    }
    case firrtl::FirrtlPB_Statement::kMemoryPort: {
      handle_mport_declaration(lnast, parent_node, stmt.memory_port());
      break;
    }
    case firrtl::FirrtlPB_Statement::kInstance: {  // Instance -- creating an instance of a module inside another
      create_module_inst(lnast, stmt.instance(), parent_node);
      break;
    }
    case firrtl::FirrtlPB_Statement::kNode: {  // Node -- nodes are simply named intermediates in a circuit
      // add_local_flip_info(false, stmt.node().id());
      node_names.insert(stmt.node().id());
      init_expr_add(lnast, stmt.node().expression(), parent_node, stmt.node().id());
      break;
    }
    case firrtl::FirrtlPB_Statement::kWhen: {
      auto cond_str = expr_str_flattened_or_tg(lnast, parent_node, stmt.when().predicate());
      auto idx_when = lnast.add_child(parent_node, Lnast_node::create_if());
      // lnast.add_child(idx_when, Lnast_node::create_ref(cond_str));
			attach_expr_str2node(lnast, cond_str, idx_when);

      auto idx_stmts_t = lnast.add_child(idx_when, Lnast_node::create_stmts());

      for (int i = 0; i < stmt.when().consequent_size(); i++) {
        list_statement_info(lnast, stmt.when().consequent(i), idx_stmts_t);
      }

      if (stmt.when().otherwise_size() > 0) {
        auto idx_stmts_f = lnast.add_child(idx_when, Lnast_node::create_stmts());
        for (int j = 0; j < stmt.when().otherwise_size(); j++) {
          list_statement_info(lnast, stmt.when().otherwise(j), idx_stmts_f);
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
    case firrtl::FirrtlPB_Statement::kConnect:  
    case firrtl::FirrtlPB_Statement::kPartialConnect: {
      firrtl::FirrtlPB_Expression lhs_expr;
      firrtl::FirrtlPB_Expression rhs_expr;
      if (stmt.statement_case() == firrtl::FirrtlPB_Statement::kConnect) {
        lhs_expr = stmt.connect().location();
        rhs_expr = stmt.connect().expression();
      } else {
        lhs_expr = stmt.partial_connect().location();
        rhs_expr = stmt.partial_connect().expression();
      }
      
      // example: "_T <= io.in" means: hier_name_l == "_T", hier_name_r == "io.in"
      bool is_runtime_idx_l = false;
      bool is_runtime_idx_r = false;
      std::string hier_name_l = get_expr_hier_name(lhs_expr, is_runtime_idx_l);
      std::string hier_name_r = get_expr_hier_name(rhs_expr, is_runtime_idx_r);

      // case-I: runtime index
      // some facts: 
      // (1) firrtl runtime index don't have any flipness issue 
      // (2) it always be the flattened connection case action: we just flatten
      // the lhs and rhs and insert the multiplexers to handle the runtime
      // vector elements selection.
      if (is_runtime_idx_l) {
        handle_lhs_runtime_idx(lnast, parent_node, hier_name_l, hier_name_r, lhs_expr);
        return;
      } 

      if (is_runtime_idx_r) {
        handle_rhs_runtime_idx(lnast, parent_node, hier_name_l, hier_name_r, rhs_expr);
        return;
      }

      // case-II: the rhs is a component w/o names, such as validif,
      // primitive_op, unsigned integers, and mux in this case, the hier_name_l
      // must already a leaf in the hierarchy, and must be non-flipped. We
      // could safely create a simple lnast assignment.
      if (hier_name_r.empty()) {
        init_expr_add(lnast, rhs_expr, parent_node, hier_name_l);
        return;
      }
      
      
      // preparation: get head of the tuple name so you know entry for the var2flip table
      auto pos_l = hier_name_l.find_first_of('.');
      std::string tup_head_l, tup_head_r;
      std::string tup_rest_l, tup_rest_r;
      if (pos_l != std::string::npos) {
        tup_head_l = hier_name_l.substr(0, pos_l);
        tup_rest_l = hier_name_l.substr(pos_l + 1);
      } else {
        tup_head_l = hier_name_l;
      }

      auto pos_r = hier_name_r.find_first_of('.');
      if (pos_r != std::string::npos) {
        tup_head_r = hier_name_r.substr(0, pos_r);
        tup_rest_r = hier_name_r.substr(pos_r + 1);
      } else {
        tup_head_r = hier_name_r;
      }

      // case-III: submodule instantiation IO connection     
      // note: submodule outputs has been handled during create_module_inst(),
      // where all the outputs hierarchy have been flattened as a wire through
      // series of TupGet.  So here we only need to handle sub inputs. 
      bool is_sub_inp_connection = inst2module.find(tup_head_l) != inst2module.end();
      if (is_sub_inp_connection) {
        // FIXME: know the hier_name_l's var2flip sets, flattened the hier wires and connect to a 
        // corresponding rhs flattend wire with the head name of hier_name_r
        hier_name_r = name_prefix_modifier_flattener(hier_name_r, true);

        auto found = var2flip.find(hier_name_r);
        if (found != var2flip.end()) {
          auto &tup_r_sets = var2flip[hier_name_r];
          for (const auto &it : tup_r_sets) {
            // tuple_flattened_connections(lnast, parent_node, hier_name_l, hier_name_r, it.first, it.second);
            auto pos = it.first.find_first_of('.');
            if (pos == std::string::npos)
              continue;
            auto head_chopped_hier_name = it.first.substr(pos + 1);
            // hier_name_l = absl::StrCat(hier_name_l, ".", head_chopped_hier_name);
            auto new_hier_name_l = absl::StrCat(hier_name_l, ".", head_chopped_hier_name);
            auto new_hier_name_r = absl::StrCat(hier_name_r, ".", head_chopped_hier_name);
            create_tuple_add_for_instance_itup(lnast, parent_node, new_hier_name_l, new_hier_name_r);
          }
        } else {
          create_tuple_add_for_instance_itup(lnast, parent_node, hier_name_l, hier_name_r);
        }
        return;
      }
      
      // case-IV: memory port connection
      bool is_wr_mport = mport2mem.find(tup_head_l) != mport2mem.end();
      bool is_rd_mport = mport2mem.find(tup_head_r) != mport2mem.end();

      if (is_rd_mport) {
        // fmt::print("DEBUG BBB handle rd_mport hier_name_l:{}, hier_name_r:{}\n", hier_name_l, hier_name_r);
        initialize_rd_mport_from_usage(lnast, parent_node, tup_head_r);
        hier_name_l = name_prefix_modifier_flattener(hier_name_l, false);
        if (!absl::StrContains(hier_name_r, '.')) {
          add_lnast_assign(lnast, parent_node, hier_name_l, tup_head_r);
        } else {
          auto target_var_str = create_tmp_var();
          Lnast_node target_node = Lnast_node::create_ref(target_var_str);
          create_tuple_get_from_str(lnast, parent_node, hier_name_r, target_node);
          add_lnast_assign(lnast, parent_node, hier_name_l, target_var_str);
        }
        return;
      } else if (is_wr_mport) {
        // fmt::print("DEBUG CCC handle wr_mport hier_name_l:{}, hier_name_r:{}\n", hier_name_l, hier_name_r);
        initialize_wr_mport_from_usage(lnast, parent_node, tup_head_l);
        hier_name_r = name_prefix_modifier_flattener(hier_name_r, true);
        if (!absl::StrContains(hier_name_l, '.')) {
          add_lnast_assign(lnast, parent_node, tup_head_l, hier_name_r);
        } else {
          Lnast_node value_node = Lnast_node::create_ref(hier_name_r);
          create_tuple_add_from_str(lnast, parent_node, hier_name_l, value_node);
        }
        return;
      }

      // case-V: "combinational" connection that involves a node, no need to
      // worry about flipness, also no need to worry about flattened
      // hierarchical connection as kNode must be a scalar
      bool c0 = node_names.find(hier_name_l) != node_names.end() || 
                node_names.find(hier_name_r) != node_names.end();
      bool c1 = reg2qpin.find(hier_name_l) == reg2qpin.end() && 
                reg2qpin.find(hier_name_r) == reg2qpin.end();
      bool c2 = !is_sub_inp_connection;
      if (c0 && c1 && c2) {
        hier_name_l = name_prefix_modifier_flattener(hier_name_l, false);
        hier_name_r = name_prefix_modifier_flattener(hier_name_r, true);
        add_lnast_assign(lnast, parent_node, hier_name_l, hier_name_r);
        return;
      }

      // case-VI: lhs == module_output && rhs == module_input
      // Pseudo Algorithm:
      // (1) get the tup_l_sets
      // (2) get all the flattened fields, and base on that to get the
      //     corresponding flattened io
      // (3) check the leaf fields, if the leaf_field_name matched from both
      //     flattened input and output, connect them together
      // (4) no need to check flipness as you already know who is the
      //     module-output and who is the module-input, it must be %foo.a <-
      //     $bar.a, which also means you don't need to grep info from the var2flip
      //     table!
        
      if (tup_head_l == tup_head_r) {
        hier_name_l = name_prefix_modifier_flattener(hier_name_l, false);
        hier_name_r = name_prefix_modifier_flattener(hier_name_r, true);
        add_lnast_assign(lnast, parent_node, hier_name_l, hier_name_r);
        return;
      }
      


      // case-VII: this is the normal case that involves firrtl kWire connection
      // could be (1) wire <- module_input (2) wire <- wire 
      // (3) module_output <- wire I(tup_head_l == tup_head_r);
      absl::btree_set<std::pair<std::string, bool>> *tup_l_sets;
      auto cond0 = input_names.find(tup_head_l)  == input_names.end();
      auto cond1 = output_names.find(tup_head_l) == output_names.end();
      if (cond0 && cond1) {
        // it's local variable or wire or register
        tup_l_sets = &var2flip[tup_head_l];
      } else {
        // it's module io, check the global table
        tup_l_sets = &Inou_firrtl::glob_info.var2flip[lnast.get_top_module_name()][tup_head_l];
      }

      for (const auto &it : *tup_l_sets) {
        auto pos = it.first.find(hier_name_l);
        bool hit = false;
        if (pos != std::string::npos) {
          I(pos == 0);
          auto pos2 = hier_name_l.size();
          if (it.first.size() > pos2 && it.first.at(pos2) == '.') {
            hit = true;
          }
        }
        
        if (it.first == hier_name_l || hit) {
          tuple_flattened_connections(lnast, parent_node, hier_name_l, hier_name_r, it.first, it.second);
        }
      }
      return;
    }

    case firrtl::FirrtlPB_Statement::kIsInvalid: {
      auto id = stmt.is_invalid().expression().reference().id();
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



void Inou_firrtl_module::final_mem_interface_assign(Lnast& lnast, Lnast_nid& parent_node) {
  for (auto& mem_name : memory_names) {
    // try to recover tuplpe field from the mem_din
    auto& idx_initialize_stmts = mem2initial_idx[mem_name];

    for (auto& it : mem2rd_mports[mem_name]) {
      auto mport_name          = it.first;
      auto cnt_of_rd_mport     = it.second;

      auto idx_tg2        = lnast.add_child(idx_initialize_stmts, Lnast_node::create_tuple_get());
      auto temp_var_name2 = create_tmp_var();
      lnast.add_child(idx_tg2, Lnast_node::create_ref(temp_var_name2));
      lnast.add_child(idx_tg2, Lnast_node::create_ref(absl::StrCat(mem_name, "_res")));
      lnast.add_child(idx_tg2, Lnast_node::create_const(cnt_of_rd_mport));

      // FIXME->sh: new cprop should be able to reflect the memory input bits on the memory output fields, 
      //            so we don't need dp_assign here.
      // auto idx_asg2 = lnast.add_child(idx_initialize_stmts, Lnast_node::create_dp_assign());
      auto idx_asg2 = lnast.add_child(idx_initialize_stmts, Lnast_node::create_assign());
      lnast.add_child(idx_asg2, Lnast_node::create_ref(mport_name));
      lnast.add_child(idx_asg2, Lnast_node::create_ref(temp_var_name2));
    }

    std::vector<std::string> tmp_flattened_fields_per_port;

    auto idx_ta_margs = lnast.add_child(parent_node, Lnast_node::create_tuple_add());
    auto temp_var_str = create_tmp_var();
    lnast.add_child(idx_ta_margs, Lnast_node::create_ref(temp_var_str));

    auto idx_asg_addr = lnast.add_child(idx_ta_margs, Lnast_node::create_assign());
    lnast.add_child(idx_asg_addr, Lnast_node::create_const("addr"));
    lnast.add_child(idx_asg_addr, Lnast_node::create_ref(absl::StrCat(mem_name, "_addr")));

    auto idx_asg_clock = lnast.add_child(idx_ta_margs, Lnast_node::create_assign());
    lnast.add_child(idx_asg_clock, Lnast_node::create_const("clock"));
    lnast.add_child(idx_asg_clock, Lnast_node::create_ref(absl::StrCat(mem_name, "_clock")));


    auto idx_asg_din = lnast.add_child(idx_ta_margs, Lnast_node::create_assign());
    lnast.add_child(idx_asg_din, Lnast_node::create_const("din"));
    lnast.add_child(idx_asg_din, Lnast_node::create_ref(absl::StrCat(mem_name, "_din")));

    auto idx_asg_enable = lnast.add_child(idx_ta_margs, Lnast_node::create_assign());
    lnast.add_child(idx_asg_enable, Lnast_node::create_const("enable"));
    lnast.add_child(idx_asg_enable, Lnast_node::create_ref(absl::StrCat(mem_name, "_enable")));

    auto idx_asg_fwd = lnast.add_child(idx_ta_margs, Lnast_node::create_assign());
    lnast.add_child(idx_asg_fwd, Lnast_node::create_const("fwd"));
    lnast.add_child(idx_asg_fwd, Lnast_node::create_ref(absl::StrCat(mem_name, "_fwd")));

    auto idx_asg_lat = lnast.add_child(idx_ta_margs, Lnast_node::create_assign());
    lnast.add_child(idx_asg_lat, Lnast_node::create_const("type"));
    lnast.add_child(idx_asg_lat, Lnast_node::create_ref(absl::StrCat(mem_name, "_type")));

    auto idx_asg_wensize = lnast.add_child(idx_ta_margs, Lnast_node::create_assign());
    lnast.add_child(idx_asg_wensize, Lnast_node::create_const("wensize"));
    lnast.add_child(idx_asg_wensize, Lnast_node::create_ref(absl::StrCat(mem_name, "_wensize")));

    auto idx_asg_size = lnast.add_child(idx_ta_margs, Lnast_node::create_assign());
    lnast.add_child(idx_asg_size, Lnast_node::create_const("size"));
    lnast.add_child(idx_asg_size, Lnast_node::create_ref(absl::StrCat(mem_name, "_size")));

    auto idx_asg_rdport = lnast.add_child(idx_ta_margs, Lnast_node::create_assign());
    lnast.add_child(idx_asg_rdport, Lnast_node::create_const("rdport"));
    lnast.add_child(idx_asg_rdport, Lnast_node::create_ref(absl::StrCat(mem_name, "_rdport")));

    auto idx_asg_margs = lnast.add_child(parent_node, Lnast_node::create_assign());
    lnast.add_child(idx_asg_margs, Lnast_node::create_ref(absl::StrCat(mem_name, "_interface_args")));
    lnast.add_child(idx_asg_margs, Lnast_node::create_ref(temp_var_str));
  }
}

//--------------Modules/Circuits--------------------
// Create basis of LNAST tree. Set root to "top" and have "stmts" be top's child.
void Inou_firrtl::user_module_to_lnast(Eprp_var& var, const firrtl::FirrtlPB_Module& fmodule, std::string_view file_name) {
#ifndef NDEBUG
  fmt::print("Module (user): {}\n", fmodule.user_module().id());
#endif

  Inou_firrtl_module firmod;

  std::unique_ptr<Lnast> lnast = std::make_unique<Lnast>(fmodule.user_module().id(), file_name);

  const firrtl::FirrtlPB_Module_UserModule& user_module = fmodule.user_module();

  lnast->set_root(Lnast_node::create_top());
  auto idx_stmts = lnast->add_child(lh::Tree_index::root(), Lnast_node::create_stmts());

  // Iterate over I/O of the module.
  for (int i = 0; i < user_module.port_size(); i++) {
    const firrtl::FirrtlPB_Port& port = user_module.port(i);
    firmod.list_port_info(*lnast, port, idx_stmts);
  }

  // Iterate over statements of the module.
  for (int j = 0; j < user_module.statement_size(); j++) {
    const firrtl::FirrtlPB_Statement& stmt = user_module.statement(j);
    // PreCheckForMem(*lnast, stmt, idx_stmts);
    firmod.list_statement_info(*lnast, stmt, idx_stmts);
  }

  Inou_firrtl_module::dump_var2flip(firmod.var2flip);
  firmod.final_mem_interface_assign(*lnast, idx_stmts);

  std::lock_guard<std::mutex> guard(eprp_var_mutex);
  { 
    var.add(std::move(lnast)); 
  }
}


void Inou_firrtl::ext_module_to_lnast(Eprp_var& var, const firrtl::FirrtlPB_Module& fmodule, std::string_view file_name) {
#ifndef NDEBUG
  fmt::print("Module (ext): {}\n", fmodule.external_module().id());
#endif

  Inou_firrtl_module firmod;

  std::unique_ptr<Lnast> lnast = std::make_unique<Lnast>(fmodule.external_module().id(), file_name);

  const firrtl::FirrtlPB_Module_ExternalModule& ext_module = fmodule.external_module();

  lnast->set_root(Lnast_node::create_top());
  auto idx_stmts = lnast->add_child(lh::Tree_index::root(), Lnast_node::create_stmts());

  // Iterate over I/O of the module.
  for (int i = 0; i < ext_module.port_size(); i++) {
    const firrtl::FirrtlPB_Port& port = ext_module.port(i);
    firmod.list_port_info(*lnast, port, idx_stmts);
  }


  std::lock_guard<std::mutex> guard(eprp_var_mutex);
  { 
    var.add(std::move(lnast)); 
  }
}


void Inou_firrtl::populate_all_mods_io(Eprp_var& var, const firrtl::FirrtlPB_Circuit& circuit, std::string_view file_name) {

  Graph_library *lib = Graph_library::instance(var.get("path", "lgdb"));

  for (int i = 0; i < circuit.module_size(); i++) {
    // std::vector<std::pair<std::string, uint8_t>> vec;
    if (circuit.module(i).has_external_module()) {
      /* NOTE->hunter: This is a Verilog blackbox. If we want to link it, it'd have to go through either V->LG
       * or V->LN->LG. I will create a Sub_Node in case the Verilog isn't provided. */
      auto     module_i_external_module_id = circuit.module(i).external_module().id();
      auto     *sub                        = lib->create_sub(module_i_external_module_id, file_name);
      uint64_t inp_pos                     = 0;
      uint64_t out_pos                     = 0;
      absl::flat_hash_map<std::string, absl::btree_set<std::pair<std::string, bool>>> empty_map;
      glob_info.var2flip.insert_or_assign(module_i_external_module_id, empty_map); 

      for (int j = 0; j < circuit.module(i).external_module().port_size(); j++) {
        auto port = circuit.module(i).external_module().port(j);
        auto initial_set = absl::btree_set<std::pair<std::string, bool>>{};
        // initial_set.insert(std::pair(port.id(), false));
        glob_info.var2flip[module_i_external_module_id].insert_or_assign(port.id(), initial_set); 
        add_port_to_map(module_i_external_module_id, port.type(), port.direction(), false, port.id(), *sub, inp_pos, out_pos);
      }
      continue;
    } else if (circuit.module(i).has_user_module()) {
      auto     module_i_user_module_id = circuit.module(i).user_module().id();
      auto     *sub                    = lib->create_sub(module_i_user_module_id, file_name);
      uint64_t inp_pos                 = 0;
      uint64_t out_pos                 = 0;
      absl::flat_hash_map<std::string, absl::btree_set<std::pair<std::string, bool>>> empty_map;
      glob_info.var2flip.insert_or_assign(module_i_user_module_id, empty_map); 
      auto empty_set = absl::flat_hash_set<std::tuple<std::string, uint16_t, bool>>{};
      glob_info.module2outputs.insert_or_assign(module_i_user_module_id, empty_set);

      for (int j = 0; j < circuit.module(i).user_module().port_size(); j++) {
        auto port = circuit.module(i).user_module().port(j);
        auto initial_set = absl::btree_set<std::pair<std::string, bool>>{};
        glob_info.var2flip[module_i_user_module_id].insert_or_assign(port.id(), initial_set); 

        add_port_to_map(module_i_user_module_id, port.type(), port.direction(), false, port.id(), *sub, inp_pos, out_pos);
      }
    } else {
      Pass::error("Module not set.");
    }
  }
}

/* Used to populate Sub_Nodes so that when Lgraphs are constructed,
 * all the Lgraphs will be able to populate regardless of order. */
void Inou_firrtl::add_port_sub(Sub_node& sub, uint64_t& inp_pos, uint64_t& out_pos, std::string_view port_id, uint8_t dir) {
  if (dir == 1) {                // PORT_DIRECTION_IN
    sub.add_input_pin(port_id);  //, inp_pos);
    inp_pos++;
  } else {
    sub.add_output_pin(port_id);  //, out_pos);
    out_pos++;
  }
}

void Inou_firrtl_module::add_local_flip_info(bool flipped_in, std::string_view id) {
  auto found = id.find_first_of('.');
  // case-I: scalar flop or scalar wire or firrtl node
  if (found == std::string::npos) {
    auto pair = std::make_pair(std::string(id), flipped_in);
    I(var2flip.find(id) == var2flip.end());

    auto new_set = absl::btree_set<std::pair<std::string, bool>>{};
    new_set.insert(pair);
    var2flip.insert_or_assign(id, new_set);
    return;
  }


  // case-II: hier-flop or hier-wire
  I(found != std::string::npos);
  auto lnast_tupname = id.substr(0, found); 
  auto pair = std::make_pair(std::string(id), flipped_in);
  auto set_itr = var2flip.find(lnast_tupname);
  if (set_itr == var2flip.end()) {
    auto new_set = absl::btree_set<std::pair<std::string, bool>>{};
    new_set.insert(pair);
    var2flip.insert_or_assign(lnast_tupname, new_set);
  } else {
    set_itr->second.insert(pair);
  }
}


void Inou_firrtl::add_global_io_flipness(std::string_view mod_id, bool flipped_in, std::string_view port_id) {
  auto found = port_id.find_first_of('.');
  if (found == std::string::npos) {
    auto pair = std::make_pair(std::string(port_id), flipped_in);
    auto new_set = absl::btree_set<std::pair<std::string, bool>>{};

    new_set.insert(pair);
    glob_info.var2flip[mod_id].insert_or_assign(port_id, new_set);
    return;
  }

  auto lnast_tupname = port_id.substr(0, found); 
  auto pair = std::make_pair(std::string(port_id), flipped_in);
  auto set_itr = glob_info.var2flip[mod_id].find(lnast_tupname);
  if (set_itr == glob_info.var2flip[mod_id].end()) {
    auto new_set = absl::btree_set<std::pair<std::string, bool>>{};
    new_set.insert(pair);
    glob_info.var2flip[mod_id].insert_or_assign(lnast_tupname, new_set);
  } else {
    set_itr->second.insert(pair);
  }
}

void Inou_firrtl::add_port_to_map(std::string_view mod_id, const firrtl::FirrtlPB_Type& type, uint8_t dir, bool flipped_in, std::string_view port_id,
                                  Sub_node& sub, uint64_t& inp_pos, uint64_t& out_pos) {
  switch (type.type_case()) {
    case firrtl::FirrtlPB_Type::kBundleType: {  // Bundle type
      auto& btype = type.bundle_type();

      for (int i = 0; i < type.bundle_type().field_size(); i++) {
        auto concat_port_id = absl::StrCat(port_id, ".", btype.field(i).id());
        if (btype.field(i).is_flipped()) {
          uint8_t new_dir = 0;
          if (dir == 1) {  // PORT_DIRECTION_IN
            new_dir = 2;
          } else if (dir == 2) {
            new_dir = 1;
          }
          I(new_dir != 0);
          add_port_to_map(mod_id, btype.field(i).type(), new_dir, !flipped_in, concat_port_id, sub, inp_pos, out_pos);
        } else {
          add_port_to_map(mod_id, btype.field(i).type(), dir,      flipped_in, absl::StrCat(port_id, ".", btype.field(i).id()), sub, inp_pos, out_pos);
        }
      }
      break;
    }

    case firrtl::FirrtlPB_Type::kVectorType: {  // Vector type
      absl::flat_hash_map<std::string, uint16_t> var2vec_size;
      var2vec_size.insert_or_assign(port_id, type.vector_type().size());
      // fmt::print("DEBUG BBB vec id:{}, size:{}\n", port_id, type.vector_type().size());
      glob_info.module_var2vec_size.insert_or_assign(mod_id, var2vec_size);
      for (uint32_t i = 0; i < type.vector_type().size(); i++) {
        add_port_to_map(mod_id, type.vector_type().type(), dir, false, absl::StrCat(port_id, ".", i), sub, inp_pos, out_pos);
      }
      break;
    }
    case firrtl::FirrtlPB_Type::kUintType: {    // UInt type
      add_port_sub(sub, inp_pos, out_pos, port_id, dir);
      if (dir == 2) {
        auto bits = type.uint_type().width().value();
        glob_info.module2outputs[mod_id].insert({std::string{port_id}, bits, false});
      }
      if (type.uint_type().width().value() != 0)
        add_global_io_flipness(mod_id, flipped_in, port_id);
      break;
    }
    case firrtl::FirrtlPB_Type::kSintType: {   // SInt type
      add_port_sub(sub, inp_pos, out_pos, port_id, dir);
      if (dir == 2) {
        auto bits = type.uint_type().width().value();
        glob_info.module2outputs[mod_id].insert({std::string{port_id}, bits, true});
      }
      if (type.sint_type().width().value() != 0)
        add_global_io_flipness(mod_id, flipped_in, port_id);
      break;
    }
    case firrtl::FirrtlPB_Type::kResetType:     
    case firrtl::FirrtlPB_Type::kAsyncResetType:   
    case firrtl::FirrtlPB_Type::kClockType: {  
      add_port_sub(sub, inp_pos, out_pos, port_id, dir);
      if (dir == 2) {
        auto bits = type.uint_type().width().value();
        glob_info.module2outputs[mod_id].insert({std::string{port_id}, bits, false});
      }
      add_global_io_flipness(mod_id, flipped_in, port_id);
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
void Inou_firrtl::grab_ext_module_info(const firrtl::FirrtlPB_Module_ExternalModule& emod) {
  // Figure out all of mods IO and their respective bw + dir.
  std::vector<std::tuple<std::string, uint8_t, uint32_t, bool>>
      port_list;  // Terms are as follows: name, direction, # of bits, sign.
  for (int i = 0; i < emod.port_size(); i++) {
    const auto& port = emod.port(i);
    create_io_list(port.type(), port.direction(), port.id(), port_list);
  }

  // Figure out what the value for each parameter is, add to map.
  for (int j = 0; j < emod.parameter_size(); j++) {
    std::string param_str;
    switch (emod.parameter(j).value_case()) {
      case firrtl::FirrtlPB_Module_ExternalModule_Parameter::kInteger:
        param_str = convert_bigint_to_str(emod.parameter(j).integer());
        break;
      case firrtl::FirrtlPB_Module_ExternalModule_Parameter::kDouble: param_str = emod.parameter(j).double_(); break;
      case firrtl::FirrtlPB_Module_ExternalModule_Parameter::kString: param_str = emod.parameter(j).string(); break;
      case firrtl::FirrtlPB_Module_ExternalModule_Parameter::kRawString: param_str = emod.parameter(j).raw_string(); break;
      default: I(false);
    }
    glob_info.ext_module2param[emod.defined_name()].insert({emod.parameter(j).id(), param_str});
  }

  // Add them to the map to let us know what ports exist in this module.
  // for (const auto& elem : port_list) {
    // glob_info.module2outputs[std::pair(emod.defined_name(), std::get<0>(elem))] = std::get<1>(elem);
  // }
}

/* This function is used for the following syntax rules in FIRRTL:
 * creating a wire, creating a register, instantiating an input/output (port),
 *
 * This function returns a pair which holds the full name of a wire/output/input/register
 * and the bitwidth of it (if the bw is 0, that means the bitwidth will be inferred later.
 */
void Inou_firrtl::create_io_list(const firrtl::FirrtlPB_Type& type, uint8_t dir, std::string_view port_id,
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
      vec.emplace_back(port_id, dir, 1, true);  // intentionally put 1 signed bits, LiveHD compiler will handle clock bits later
      break;
    }
    case firrtl::FirrtlPB_Type::kBundleType: {  // Bundle type
      const auto& btype = type.bundle_type();
      for (int i = 0; i < type.bundle_type().field_size(); i++) {
        if (btype.field(i).is_flipped()) {
          uint8_t new_dir = 0;
          if (dir == 1) {  // PORT_DIRECTION_IN
            new_dir = 2;
          } else if (dir == 2) {
            new_dir = 1;
          }
          I(new_dir != 0);
          create_io_list(btype.field(i).type(), new_dir, absl::StrCat(port_id, ".", btype.field(i).id()), vec);
        } else {
          create_io_list(btype.field(i).type(), dir, absl::StrCat(port_id, ".", btype.field(i).id()), vec);
        }
      }
      break;
    }
    case firrtl::FirrtlPB_Type::kVectorType: {  // Vector type
      for (uint32_t i = 0; i < type.vector_type().size(); i++) {
        vec.emplace_back(port_id, dir, 0, false);
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
      // FIXME: handle it when encountered
      // async_rst_names.insert(port_id);
      break;
    }
    case firrtl::FirrtlPB_Type::kResetType: {  // Reset type
      vec.emplace_back(port_id, dir, 1, false);
      break;
    }
    default: Pass::error("Unknown port type.");
  }
}

std::string Inou_firrtl::convert_bigint_to_str(const firrtl::FirrtlPB_BigInt& bigint) {
  if (bigint.value().size() == 0) {
    return "0b0";
  }

  std::string bigint_val("0b");
  for (char bigint_char : bigint.value()) {
    std::string bit_str;
    for (int j = 0; j < 8; j++) {
      if (bigint_char % 2) {
        bit_str = absl::StrCat("1", bit_str);
      } else {
        bit_str = absl::StrCat("0", bit_str);
      }
      bigint_char >>= 1;
    }
    absl::StrAppend(&bigint_val, bit_str);
  }

  return bigint_val;
}

void Inou_firrtl::iterate_modules(Eprp_var& var, const firrtl::FirrtlPB_Circuit& circuit, std::string_view file_name) {
  if (circuit.top_size() > 1) {
    Pass::error("More than 1 top module specified.");
    I(false);
  }

  // Create ModuleName to I/O Pair List
  populate_all_mods_io(var, circuit, file_name);

  for (int i = 0; i < circuit.module_size(); i++) {
    if (circuit.module(i).has_external_module()) {
      // circuit.module(i).external_module();
      grab_ext_module_info(circuit.module(i).external_module());
    }
  }
  // so far you collect all global table informations

  // parallelize the rest of firrtl modules -> lnasts
  for (int i = 0; i < circuit.module_size(); i++) {
    if (circuit.module(i).has_user_module()) {
      thread_pool.add([this, &var, &circuit, i, &file_name]() -> void {
        TRACE_EVENT("inou", "fir_tolnast:module");
        Lbench b("inou.fir_tolnast:module");
        this->user_module_to_lnast(var, circuit.module(i), file_name);
      });
    } else if (circuit.module(i).has_external_module()) {
      thread_pool.add([this, &var, &circuit, i, &file_name]() -> void {
        TRACE_EVENT("inou", "fir_tolnast:module");
        Lbench b("inou.fir_tolnast:module");
        this->ext_module_to_lnast(var, circuit.module(i), file_name);
      });
    } else {
      Pass::error("Module not set.");
    }
  }
  thread_pool.wait_all();
}

// Iterate over every FIRRTL circuit (design), each circuit can contain multiple modules.
void Inou_firrtl::iterate_circuits(Eprp_var& var, const firrtl::FirrtlPB& firrtl_input, std::string_view file_name) {
  for (int i = 0; i < firrtl_input.circuit_size(); i++) {
    Inou_firrtl::glob_info.module2outputs.clear();
    Inou_firrtl::glob_info.ext_module2param.clear();
    Inou_firrtl::glob_info.var2flip.clear();

    const firrtl::FirrtlPB_Circuit& circuit = firrtl_input.circuit(i);
    iterate_modules(var, circuit, file_name);
  }
}

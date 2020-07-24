//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//

#include <fstream>
#include <iostream>

#include "firrtl.pb.h"
#include "google/protobuf/util/time_util.h"
#include "inou_firrtl.hpp"
#include "lbench.hpp"

using namespace std;

using google::protobuf::util::TimeUtil;

/* For help understanding FIRRTL/Protobuf:
 * 1) Semantics regarding FIRRTL language:
 * www2.eecs.berkeley.edu/Pubs/TechRpts/2019/EECS-2019-168.pdf
 * 2) Structure of FIRRTL Protobuf file:
 * github.com/freechipsproject/firrtl/blob/master/src/main/proto/firrtl.proto */

void Inou_firrtl::toLNAST(Eprp_var& var) {
  Lbench b("inou.firrtl.tolnast");

  Inou_firrtl p(var);

  if (var.has_label("files")) {
    auto files = var.get("files");
    for (const auto& f : absl::StrSplit(files, ",")) {
      cout << "FILE: " << f << "\n";
      firrtl::FirrtlPB firrtl_input;
      fstream          input(std::string(f).c_str(), ios::in | ios::binary);
      if (!firrtl_input.ParseFromIstream(&input)) {
        cerr << "Failed to parse FIRRTL from protobuf format." << endl;
        return;
      }
      p.temp_var_count = 0;
      p.seq_counter    = 0;
      p.IterateCircuits(var, firrtl_input, std::string(f));
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

std::string Inou_firrtl::get_full_name(const std::string& term, const bool is_rhs) {
  if (input_names.count(term)) {
    // string matching "term" was found to be an input to the module
    I(is_rhs);
    return absl::StrCat("$", term);
  } else if (output_names.count(term)) {
    return absl::StrCat("%", term);
  } else if (register_names.count(term)) {
    if (is_rhs) {
      auto q_pin_str_version = term;
      replace(q_pin_str_version.begin(), q_pin_str_version.end(), '.', '_');
      return absl::StrCat("___", q_pin_str_version, "__q_pin");
    } else {
      return absl::StrCat("#", term);
    }
  } else {
    // We add _. in front of temporary names
    if (term.substr(0, 2) == "_T") {
      return absl::StrCat("_.", term);
    } else if (term.substr(0, 4) == "_GEN") {
      return absl::StrCat("_.", term);
    } else {
      return term;
    }
    // return term;
  }
}

// If the bitwidth is specified, in LNAST we have to create a new variable which represents
//  the number of bits that a variable will have.
void Inou_firrtl::create_bitwidth_dot_node(Lnast& lnast, uint32_t bitwidth, Lnast_nid& parent_node, const std::string& _port_id) {
  std::string port_id{_port_id};  // FIXME: Instead of erase, use a string_view and change lenght (much faster, not need to do mem)

  if (bitwidth <= 0) {
    /* No need to make a bitwidth node, 0 means implicit bitwidth.
     * If -1, then that's how I specify that the "port_id" is not an
     * actual wire but instead the general vector name. */
    return;
  }

  if (port_id.find(".") == std::string::npos) {
    // No tuple/bundles used in this port, so no need for extra dot nodes.
    auto temp_var_name = create_temp_var(lnast);

    auto idx_dot = lnast.add_child(parent_node, Lnast_node::create_dot("dot"));
    lnast.add_child(idx_dot, Lnast_node::create_ref(temp_var_name));
    lnast.add_child(idx_dot, Lnast_node::create_ref(lnast.add_string(port_id)));
    lnast.add_child(idx_dot, Lnast_node::create_ref("__bits"));

    auto idx_asg = lnast.add_child(parent_node, Lnast_node::create_assign("asg"));
    lnast.add_child(idx_asg, Lnast_node::create_ref(temp_var_name));
    lnast.add_child(idx_asg, Lnast_node::create_const(lnast.add_string(to_string(bitwidth))));
  } else {
    /* This is a bundle so I need to create dot nodes to access the correct bundle member.
     * So instead what we do is take the bundle name and the part we want to access and
     * make a dot for that node, over and over until we finally reach an element, not a
     * bundle (which we then assign the __bits to). */
    auto tup_name = lnast.add_string(port_id.substr(0, port_id.find(".")));
    port_id.erase(0, port_id.find(".") + 1);
    std::string_view temp_var_name;
    while (port_id.find(".") != std::string::npos) {
      std::string tup_attr = port_id.substr(0, port_id.find("."));
      port_id.erase(0, port_id.find(".") + 1);
      temp_var_name = create_temp_var(lnast);

      auto idx_dot_i = lnast.add_child(parent_node, Lnast_node::create_dot("dot"));
      lnast.add_child(idx_dot_i, Lnast_node::create_ref(temp_var_name));
      lnast.add_child(idx_dot_i, Lnast_node::create_ref(tup_name));
      if (isdigit(tup_attr[0])) {
        lnast.add_child(idx_dot_i, Lnast_node::create_const(lnast.add_string(tup_attr)));
      } else {
        lnast.add_child(idx_dot_i, Lnast_node::create_ref(lnast.add_string(tup_attr)));
      }
      tup_name = temp_var_name;
    }

    temp_var_name = create_temp_var(lnast);
    auto idx_dot  = lnast.add_child(parent_node, Lnast_node::create_dot("dot"));
    lnast.add_child(idx_dot, Lnast_node::create_ref(temp_var_name));
    lnast.add_child(idx_dot, Lnast_node::create_ref(tup_name));
    if (isdigit(port_id[0])) {
      lnast.add_child(idx_dot, Lnast_node::create_const(lnast.add_string(port_id)));  // port_id is stripped down
    } else {
      lnast.add_child(idx_dot, Lnast_node::create_ref(lnast.add_string(port_id)));  // port_id is stripped down
    }

    // Now that we've gotten all the dot nodes to access the right thing, now we do the __bits.
    auto temp_var_name_b = create_temp_var(lnast);
    auto idx_dot_b       = lnast.add_child(parent_node, Lnast_node::create_dot("dot"));
    lnast.add_child(idx_dot_b, Lnast_node::create_ref(temp_var_name_b));
    lnast.add_child(idx_dot_b, Lnast_node::create_ref(temp_var_name));
    lnast.add_child(idx_dot_b, Lnast_node::create_ref("__bits"));

    auto idx_asg = lnast.add_child(parent_node, Lnast_node::create_assign("asg"));
    lnast.add_child(idx_asg, Lnast_node::create_ref(temp_var_name_b));
    lnast.add_child(idx_asg, Lnast_node::create_const(lnast.add_string(to_string(bitwidth))));
  }
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
      I(false);                                // FIXME: Not yet supported.
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
    default: cout << "Unknown port type." << endl; I(false);
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
      I(false);                                // FIXME: Unsure how to implement
      break;
    }
    default: {
      /* UInt SInt Clock Analog AsyncReset Reset Types*/
      auto wire_bits = get_bit_count(type);
      create_bitwidth_dot_node(lnast, wire_bits, parent_node, id);
    }
  }
}

/* When creating a register, we have to set the register's
 * clock, reset, and init values using "dot" nodes in the LNAST.
 * These functions create all of those when a reg is first declared. */
void Inou_firrtl::init_reg_dots(Lnast& lnast, const firrtl::FirrtlPB_Type& type, const std::string& id,
                                const std::string_view clock, const std::string_view reset, const std::string_view init,
                                Lnast_nid& parent_node) {
  // init_wire_dots(lnast, expr.type(), absl::StrCat("#", expr.id()), parent_node);
  switch (type.type_case()) {
    case firrtl::FirrtlPB_Type::kBundleType: {  // Bundle Type
      for (int i = 0; i < type.bundle_type().field_size(); i++) {
        init_reg_dots(lnast,
                      type.bundle_type().field(i).type(),
                      absl::StrCat(id, ".", type.bundle_type().field(i).id()),
                      clock,
                      reset,
                      init,
                      parent_node);
      }
      break;
    }
    case firrtl::FirrtlPB_Type::kVectorType: {  // Vector Type
      for (uint32_t i = 0; i < type.vector_type().size(); i++) {
        init_reg_dots(lnast, type.vector_type().type(), absl::StrCat(id, ".", i), clock, reset, init, parent_node);
      }
      break;
    }
    case firrtl::FirrtlPB_Type::kFixedType: {  // Fixed Point
      I(false);                                // FIXME: Unsure how to implement
      break;
    }
    default: {
      /* UInt SInt Clock Analog AsyncReset Reset Types*/
      auto reg_bits = get_bit_count(type);
      init_reg_ref_dots(lnast, id, clock, reset, init, reg_bits, parent_node);
    }
  }
}

// FIXME: Eventually add in other "dot" nodes when supported.
void Inou_firrtl::init_reg_ref_dots(Lnast& lnast, const std::string& _id, const std::string_view clock,
                                    const std::string_view reset, const std::string_view init, uint32_t bitwidth,
                                    Lnast_nid& parent_node) {
  std::string id{_id};  // FIXME: isntead pass string_view, change lenght/start no need to realloc (much faster) Code can be shared
                        // with port_id

  // Add register's name to the global list.
  register_names.insert(id.substr(1, id.length() - 1));  // Use substr to remove "#"
  fmt::print("put into register_names: {}\n", id.substr(1, id.length() - 1));

  // Save 'id' for later use with qpin.
  std::string id_for_qpin = id.substr(1, id.length() - 1);

  // The first step is to get a string that allows us to access the register.
  std::string_view accessor_name;
  if (id.find(".") == std::string::npos) {
    // No tuple/bundles used in this port, so no need for extra dot nodes.
    accessor_name = lnast.add_string(id);
  } else {
    /* This is a bundle so I need to create dot nodes to access the correct bundle member.
     * So instead what we do is take the bundle name and the part we want to access and
     * make a dot for that node, over and over until we finally reach an element, not a
     * bundle (which we then assign the __bits to). */
    auto tup_name = lnast.add_string(id.substr(0, id.find(".")));
    id.erase(0, id.find(".") + 1);
    std::string_view temp_var_name;
    while (id.find(".") != std::string::npos) {
      std::string tup_attr = id.substr(0, id.find("."));
      id.erase(0, id.find(".") + 1);
      temp_var_name = create_temp_var(lnast);

      auto idx_dot_i = lnast.add_child(parent_node, Lnast_node::create_dot("dot"));
      lnast.add_child(idx_dot_i, Lnast_node::create_ref(temp_var_name));
      lnast.add_child(idx_dot_i, Lnast_node::create_ref(tup_name));
      if (isdigit(tup_attr[0])) {
        lnast.add_child(idx_dot_i, Lnast_node::create_const(lnast.add_string(tup_attr)));
      } else {
        lnast.add_child(idx_dot_i, Lnast_node::create_ref(lnast.add_string(tup_attr)));
      }
      tup_name = temp_var_name;
    }

    temp_var_name = create_temp_var(lnast);
    auto idx_dot  = lnast.add_child(parent_node, Lnast_node::create_dot("dot"));
    lnast.add_child(idx_dot, Lnast_node::create_ref(temp_var_name));
    lnast.add_child(idx_dot, Lnast_node::create_ref(tup_name));
    if (isdigit(id[0])) {
      lnast.add_child(idx_dot, Lnast_node::create_const(lnast.add_string(id)));  // id is stripped down
    } else {
      lnast.add_child(idx_dot, Lnast_node::create_ref(lnast.add_string(id)));  // id is stripped down
    }

    accessor_name = temp_var_name;
  }

  /* Now that we have a name to access it by, we can create the
   * relevant dot nodes like: __clk_pin, __q_pin, __bits,
   * __reset_pin, and (init... how to implement?) */

  // Specify __clk_pin (all registers should have this set)
  /*I((std::string)clock != "");
  auto temp_var_name_c = create_temp_var(lnast);

  auto idx_dot_c = lnast.add_child(parent_node, Lnast_node::create_dot("dot"));
  lnast.add_child(idx_dot_c, Lnast_node::create_ref(temp_var_name_c));
  lnast.add_child(idx_dot_c, Lnast_node::create_ref(accessor_name));
  lnast.add_child(idx_dot_c, Lnast_node::create_ref("__clk_pin"));

  auto idx_asg_c = lnast.add_child(parent_node, Lnast_node::create_assign("asg"));
  lnast.add_child(idx_asg_c, Lnast_node::create_ref(temp_var_name_c));
  lnast.add_child(idx_asg_c, Lnast_node::create_ref(clock));*/

  // Specify __bits, if bitwidth is explicit
  if (bitwidth > 0) {
    auto temp_var_name_b = create_temp_var(lnast);

    auto idx_dot_b = lnast.add_child(parent_node, Lnast_node::create_dot("dot"));
    lnast.add_child(idx_dot_b, Lnast_node::create_ref(temp_var_name_b));
    lnast.add_child(idx_dot_b, Lnast_node::create_ref(accessor_name));
    lnast.add_child(idx_dot_b, Lnast_node::create_ref("__bits"));

    auto idx_asg_b = lnast.add_child(parent_node, Lnast_node::create_assign("asg"));
    lnast.add_child(idx_asg_b, Lnast_node::create_ref(temp_var_name_b));
    lnast.add_child(idx_asg_b, Lnast_node::create_const(lnast.add_string(to_string(bitwidth))));
  }

  // Specify init.. (how to?)
  // FIXME: Add this eventually... might have to use __reset (code to run when reset occurs)
  //
  //

  /* Since FIRRTL designs access register qpin, I need to do:
   * #reg_name.__q_pin. The name will always be ___reg_name__q_pin */
  auto qpin_var_name_temp = create_temp_var(lnast);
  ;
  replace(id_for_qpin.begin(), id_for_qpin.end(), '.', '_');

  auto idx_dot_qp = lnast.add_child(parent_node, Lnast_node::create_dot("dot"));
  lnast.add_child(idx_dot_qp, Lnast_node::create_ref(qpin_var_name_temp));
  lnast.add_child(idx_dot_qp, Lnast_node::create_ref(accessor_name));
  lnast.add_child(idx_dot_qp, Lnast_node::create_ref("__q_pin"));

  // Required to identify ___regname__q_pin as RHS.
  auto idx_asg_qp = lnast.add_child(parent_node, Lnast_node::create_assign("asg"));
  lnast.add_child(idx_asg_qp, Lnast_node::create_ref(lnast.add_string(absl::StrCat("___", id_for_qpin, "__q_pin"))));
  lnast.add_child(idx_asg_qp, Lnast_node::create_ref(qpin_var_name_temp));
}

/* When a module instance is created in FIRRTL, we need to do the same
 * in LNAST. Note that the instance command in FIRRTL does not hook
 * any input or outputs. */
void Inou_firrtl::create_module_inst(Lnast& lnast, const firrtl::FirrtlPB_Statement_Instance& inst, Lnast_nid& parent_node) {
  /*            dot                       assign                      fn_call
   *      /      |        \                / \                     /     |     \
   * ___F0 inp_[inst_name] __final_value  F1 ___F0  out_[inst_name] [mod_name]  F1 */
  auto temp_var_name  = create_temp_var(lnast);
  auto temp_var_name2 = lnast.add_string(absl::StrCat("F", std::to_string(temp_var_count)));
  temp_var_count++;
  auto inp_name       = lnast.add_string(absl::StrCat("inp_", inst.id()));
  auto out_name       = lnast.add_string(absl::StrCat("out_", inst.id()));

  auto idx_dot = lnast.add_child(parent_node, Lnast_node::create_dot(""));
  lnast.add_child(idx_dot, Lnast_node::create_ref(temp_var_name));
  lnast.add_child(idx_dot, Lnast_node::create_ref(inp_name));
  lnast.add_child(idx_dot, Lnast_node::create_ref("__final_value"));

  auto idx_asg = lnast.add_child(parent_node, Lnast_node::create_assign(""));
  lnast.add_child(idx_asg, Lnast_node::create_ref(temp_var_name2));
  lnast.add_child(idx_asg, Lnast_node::create_ref(temp_var_name));

  auto idx_fncall = lnast.add_child(parent_node, Lnast_node::create_func_call("fn_call"));
  lnast.add_child(idx_fncall, Lnast_node::create_ref(out_name));
  lnast.add_child(idx_fncall, Lnast_node::create_ref(lnast.add_string(inst.module_id())));
  lnast.add_child(idx_fncall, Lnast_node::create_ref(temp_var_name2));

  /* Also, I need to record this module instance in
   * a map that maps instance name to module name. */
  inst_to_mod_map[inst.id()] = inst.module_id();

  // Specify the bitwidth of all IO of the submodule.
  for (const auto& name_bw_tup : mod_to_io_map[inst.module_id()]) {
    auto port_name = std::get<0>(name_bw_tup);
    auto port_bw = std::get<1>(name_bw_tup);
    auto port_dir = std::get<2>(name_bw_tup);

    /* Check to see if this specific submodule IO is a part of a
     * bundle. If it is, we need to create DOT nodes to access
     * that nested field. Otherwise, just use field name. */
    std::string_view bund_name;
    if (port_name.find(".") == std::string::npos) {
      auto temp_var_name_i = create_temp_var(lnast);
      auto field_name = lnast.add_string(port_name);

      auto idx_dot_i = lnast.add_child(parent_node, Lnast_node::create_dot(""));
      lnast.add_child(idx_dot_i, Lnast_node::create_ref(temp_var_name_i));
      if (port_dir == 1) { // PORT_DIRECTION_IN
        lnast.add_child(idx_dot_i, Lnast_node::create_ref(inp_name));
      } else if (port_dir == 2) {
        lnast.add_child(idx_dot_i, Lnast_node::create_ref(out_name));
      } else {
        I(false);
      }
      lnast.add_child(idx_dot_i, Lnast_node::create_ref(field_name));
      bund_name = temp_var_name_i;

    } else {
      size_t last = 0;
      size_t next = 0;
      bool first = true;
      while ((next = port_name.find(".", last)) != std::string::npos) {
        auto field_name = lnast.add_string(port_name.substr(last,next-last));
        auto temp_var_name_i = create_temp_var(lnast);
        if (first) {
          auto idx_dot_i = lnast.add_child(parent_node, Lnast_node::create_dot(""));
          lnast.add_child(idx_dot_i, Lnast_node::create_ref(temp_var_name_i));
          if (port_dir == 1) { // PORT_DIRECTION_IN
            lnast.add_child(idx_dot_i, Lnast_node::create_ref(inp_name));
          } else if (port_dir == 2) {
            lnast.add_child(idx_dot_i, Lnast_node::create_ref(out_name));
          } else {
            I(false);
          }
          lnast.add_child(idx_dot_i, Lnast_node::create_ref(field_name));

        } else {
          auto idx_dot_i = lnast.add_child(parent_node, Lnast_node::create_dot(""));
          lnast.add_child(idx_dot_i, Lnast_node::create_ref(temp_var_name_i));
          lnast.add_child(idx_dot_i, Lnast_node::create_ref(bund_name));
          if(isdigit(field_name[0])) {
            lnast.add_child(idx_dot_i, Lnast_node::create_const(field_name));
          } else {
            lnast.add_child(idx_dot_i, Lnast_node::create_ref(field_name));
          }
        }
        bund_name = temp_var_name_i;
        last = next + 1;
      }
    }
    // At this point, bund_name is how we can access this specific submod IO

    auto temp_var_name_bw = create_temp_var(lnast);
    auto idx_dot_bw = lnast.add_child(parent_node, Lnast_node::create_dot(""));
    lnast.add_child(idx_dot_bw, Lnast_node::create_ref(temp_var_name_bw));
    lnast.add_child(idx_dot_bw, Lnast_node::create_ref(bund_name));
    lnast.add_child(idx_dot_bw, Lnast_node::create_ref("__bits"));

    auto idx_asg_bw = lnast.add_child(parent_node, Lnast_node::create_assign(""));
    lnast.add_child(idx_asg_bw, Lnast_node::create_ref(temp_var_name_bw));
    lnast.add_child(idx_asg_bw, Lnast_node::create_const(lnast.add_string(std::to_string(port_bw))));
  }
}

/* No mux node type exists in LNAST. To support FIRRTL muxes, we instead
 * map a mux to an if-else statement whose condition is the same condition
 * as the first argument (the condition) of the mux. */
void Inou_firrtl::HandleMuxAssign(Lnast& lnast, const firrtl::FirrtlPB_Expression& expr, Lnast_nid& parent_node,
                                  const std::string& lhs) {
  I(lnast.get_data(parent_node).type.is_stmts() || lnast.get_data(parent_node).type.is_cstmts());

  auto idx_mux_if = lnast.add_child(parent_node, Lnast_node::create_if("mux"));
  auto idx_cstmts = lnast.add_child(idx_mux_if, Lnast_node::create_cstmts(get_new_seq_name(lnast)));
  auto cond_str   = lnast.add_string(ReturnExprString(lnast, expr.mux().condition(), idx_cstmts, true));
  lnast.add_child(idx_mux_if, Lnast_node::create_cond(cond_str));

  auto idx_stmt_tr = lnast.add_child(idx_mux_if, Lnast_node::create_stmts(get_new_seq_name(lnast)));
  auto idx_stmt_f  = lnast.add_child(idx_mux_if, Lnast_node::create_stmts(get_new_seq_name(lnast)));

  InitialExprAdd(lnast, expr.mux().t_value(), idx_stmt_tr, lhs);
  InitialExprAdd(lnast, expr.mux().f_value(), idx_stmt_f, lhs);
}

/* ValidIfs get detected as the RHS of an assign statement and we can't have a child of
 * an assign be an if-typed node. Thus, we have to detect ahead of time if it is a validIf
 * if we're doing an assign. If that is the case, do this instead of using ListExprType().*/
void Inou_firrtl::HandleValidIfAssign(Lnast& lnast, const firrtl::FirrtlPB_Expression& expr, Lnast_nid& parent_node,
                                      const std::string& lhs) {
  I(lnast.get_data(parent_node).type.is_stmts() || lnast.get_data(parent_node).type.is_cstmts());

  auto idx_v_if   = lnast.add_child(parent_node, Lnast_node::create_if("validIf"));
  auto idx_cstmts = lnast.add_child(idx_v_if, Lnast_node::create_cstmts(get_new_seq_name(lnast)));
  auto cond_str   = lnast.add_string(ReturnExprString(lnast, expr.valid_if().condition(), idx_cstmts, true));
  lnast.add_child(idx_v_if, Lnast_node::create_cond(cond_str));

  auto idx_stmt_tr = lnast.add_child(idx_v_if, Lnast_node::create_stmts(get_new_seq_name(lnast)));
  auto idx_stmt_f  = lnast.add_child(idx_v_if, Lnast_node::create_stmts(get_new_seq_name(lnast)));

  InitialExprAdd(lnast, expr.valid_if().value(), idx_stmt_tr, lhs);

  // For validIf, if the condition is not met then what the LHS equals is undefined. We'll just use 0.
  Lnast_nid idx_asg_false;
  if (lhs.substr(0, 1) == "%") {
    idx_asg_false = lnast.add_child(idx_stmt_f, Lnast_node::create_dp_assign("dp_asg"));
  } else {
    idx_asg_false = lnast.add_child(idx_stmt_f, Lnast_node::create_assign("assign"));
  }
  lnast.add_child(idx_asg_false, Lnast_node::create_ref(lnast.add_string(lhs)));
  lnast.add_child(idx_asg_false, Lnast_node::create_const("0"));
}

/* We have to handle NEQ operations different than any other primitive op.
 * This is because NEQ has to be broken down into two sub-operations:
 * checking equivalence and then performing the not. */
void Inou_firrtl::HandleNEQOp(Lnast& lnast, const firrtl::FirrtlPB_Expression_PrimOp& op, Lnast_nid& parent_node,
                              const std::string& lhs) {
  /* x = neq(e1, e2) should take graph form:
   *     equal        ~
   *    /  |  \     /   \
   *___F0  e1 e2   x  ___F0  */
  I(lnast.get_data(parent_node).type.is_stmts() || lnast.get_data(parent_node).type.is_cstmts());
  I(op.arg_size() == 2);
  auto e1_str        = lnast.add_string(ReturnExprString(lnast, op.arg(0), parent_node, true));
  auto e2_str        = lnast.add_string(ReturnExprString(lnast, op.arg(1), parent_node, true));
  auto temp_var_name = create_temp_var(lnast);

  auto idx_eq = lnast.add_child(parent_node, Lnast_node::create_same("eq_neq"));
  lnast.add_child(idx_eq, Lnast_node::create_ref(temp_var_name));
  AttachExprStrToNode(lnast, e1_str, idx_eq);
  AttachExprStrToNode(lnast, e2_str, idx_eq);

  auto idx_asg = lnast.add_child(parent_node, Lnast_node::create_not("asg_neq"));
  lnast.add_child(idx_asg, Lnast_node::create_ref(lnast.add_string(lhs)));
  lnast.add_child(idx_asg, Lnast_node::create_ref(temp_var_name));
}

/* Unary operations are handled in a way where (currently) there is no LNAST
 * node type that supports unary ops. Instead, we would want to have an assign
 * node and have the "rhs" child of the assign node be "[op]temp". */
void Inou_firrtl::HandleUnaryOp(Lnast& lnast, const firrtl::FirrtlPB_Expression_PrimOp& op, Lnast_nid& parent_node,
                                const std::string& lhs) {
  /* x = not(e1) should take graph form: (xor_/and_/or_reduce all look same just different op)
   *     ~
   *   /   \
   *  x    e1  */
  I(lnast.get_data(parent_node).type.is_stmts() || lnast.get_data(parent_node).type.is_cstmts());
  I(op.arg_size() == 1);

  auto e1_str  = lnast.add_string(ReturnExprString(lnast, op.arg(0), parent_node, true));
  auto idx_not = lnast.add_child(parent_node, Lnast_node::create_not("not"));
  lnast.add_child(idx_not, Lnast_node::create_ref(lnast.add_string(lhs)));
  AttachExprStrToNode(lnast, e1_str, idx_not);
}

void Inou_firrtl::HandleAndReducOp(Lnast& lnast, const firrtl::FirrtlPB_Expression_PrimOp& op, Lnast_nid& parent_node,
                                   const std::string& lhs) {
  /* x = .andR(e1) is the same as e1 == -1
   *   same
   *  /  |  \
   * x  e1  -1  */
  I(lnast.get_data(parent_node).type.is_stmts() || lnast.get_data(parent_node).type.is_cstmts());
  I(op.arg_size() == 1);

  auto e1_str = lnast.add_string(ReturnExprString(lnast, op.arg(0), parent_node, true));
  auto idx_eq = lnast.add_child(parent_node, Lnast_node::create_same("andr_same"));
  lnast.add_child(idx_eq, Lnast_node::create_ref(lnast.add_string(lhs)));
  AttachExprStrToNode(lnast, e1_str, idx_eq);
  lnast.add_child(idx_eq, Lnast_node::create_const("-1"));
}

void Inou_firrtl::HandleOrReducOp(Lnast& lnast, const firrtl::FirrtlPB_Expression_PrimOp& op, Lnast_nid& parent_node,
                                  const std::string& lhs) {
  /* x = .orR(e1) is the same as e1 != 0
   *     same        ~
   *    /  |  \     / \
   *___F0  e1  0   x  ___F0*/
  I(lnast.get_data(parent_node).type.is_stmts() || lnast.get_data(parent_node).type.is_cstmts());
  I(op.arg_size() == 1);

  auto temp_var_name = create_temp_var(lnast);
  auto e1_str        = lnast.add_string(ReturnExprString(lnast, op.arg(0), parent_node, true));

  auto idx_eq = lnast.add_child(parent_node, Lnast_node::create_same("orr_same"));
  lnast.add_child(idx_eq, Lnast_node::create_ref(temp_var_name));
  AttachExprStrToNode(lnast, e1_str, idx_eq);
  lnast.add_child(idx_eq, Lnast_node::create_const("0"));

  auto idx_not = lnast.add_child(parent_node, Lnast_node::create_not("orr_not"));
  lnast.add_child(idx_not, Lnast_node::create_ref(lnast.add_string(lhs)));
  lnast.add_child(idx_not, Lnast_node::create_ref(temp_var_name));
}

void Inou_firrtl::HandleXorReducOp(Lnast& lnast, const firrtl::FirrtlPB_Expression_PrimOp& op, Lnast_nid& parent_node,
                                   const std::string& lhs) {
  /* x = .xorR(e1)
   *  parity_op
   *    / \
   *   x  e1 */
  // FIXME: Uncomment once node type is made
  /*I(lnast.get_data(parent_node).type.is_stmts() || lnast.get_data(parent_node).type.is_cstmts());
  I(op.arg_size() == 1);

  auto e1_str = lnast.add_string(ReturnExprString(lnast, op.arg(0), parent_node, true));
  auto idx_par = lnast.add_child(parent_node, Lnast_node::create_parity("par_xorR"));
  lnast.add_child(idx_par, Lnast_node::create_ref(lnast.add_string(lhs)));
  AttachExprStrToNode(lnast, e1_str, idx_par);*/
}

void Inou_firrtl::HandleNegateOp(Lnast& lnast, const firrtl::FirrtlPB_Expression_PrimOp& op, Lnast_nid& parent_node,
                                 const std::string& lhs) {
  /* x = negate(e1) should take graph form:
   *     minus
   *    /  |  \
   *   x   0   e1 */
  I(lnast.get_data(parent_node).type.is_stmts() || lnast.get_data(parent_node).type.is_cstmts());
  I(op.arg_size() == 1);

  auto e1_str  = lnast.add_string(ReturnExprString(lnast, op.arg(0), parent_node, true));
  auto idx_mns = lnast.add_child(parent_node, Lnast_node::create_minus("minus_negate"));
  lnast.add_child(idx_mns, Lnast_node::create_ref(lnast.add_string(lhs)));
  lnast.add_child(idx_mns, Lnast_node::create_const("0"));
  AttachExprStrToNode(lnast, e1_str, idx_mns);
}

void Inou_firrtl::HandleConvOp(Lnast& lnast, const firrtl::FirrtlPB_Expression_PrimOp& op, Lnast_nid& parent_node, const std::string& lhs) {
  /* x = cvt(e). x.__sign = true and if e was unsigned then need to add 1 bit to top
   *      dot            asg            [________if__________]
   *     / | \          /  \           /      /       \       \
   * ___F0 x __sign  ___F0 true   cstmts  cond,___F1  stmt      stmt
   *                                |                  |        |   \
   *                               dot                asg      shl   dp_asg
   *                             /  |  \              / \     / | \   / \
   *                          ___F1 e __sign         x   e   x  e  1  x  e
   */
  I(lnast.get_data(parent_node).type.is_stmts() || lnast.get_data(parent_node).type.is_cstmts());
  I(op.arg_size() == 1);

  auto temp_var_name0 = create_temp_var(lnast);
  auto temp_var_name1 = create_temp_var(lnast);
  auto lhs_sv = lnast.add_string(lhs);
  auto e_str  = lnast.add_string(ReturnExprString(lnast, op.arg(0), parent_node, true));

  auto idx_dot1 = lnast.add_child(parent_node, Lnast_node::create_dot(""));
  lnast.add_child(idx_dot1, Lnast_node::create_ref(temp_var_name0));
  lnast.add_child(idx_dot1, Lnast_node::create_ref(lhs_sv));
  lnast.add_child(idx_dot1, Lnast_node::create_ref("__sign"));

  auto idx_asg1 = lnast.add_child(parent_node, Lnast_node::create_assign(""));
  lnast.add_child(idx_asg1, Lnast_node::create_ref(temp_var_name0));
  lnast.add_child(idx_asg1, Lnast_node::create_ref("true"));

  auto idx_if = lnast.add_child(parent_node, Lnast_node::create_if("conv"));

  auto idx_csts = lnast.add_child(idx_if, Lnast_node::create_cstmts(get_new_seq_name(lnast)));
  auto idx_dot2 = lnast.add_child(parent_node, Lnast_node::create_dot(""));
  lnast.add_child(idx_dot2, Lnast_node::create_ref(temp_var_name1));
  AttachExprStrToNode(lnast, e_str, idx_dot2);
  lnast.add_child(idx_dot2, Lnast_node::create_ref("__sign"));

  lnast.add_child(idx_if, Lnast_node::create_cond(temp_var_name1));

  auto idx_sts1 = lnast.add_child(idx_if, Lnast_node::create_stmts(get_new_seq_name(lnast)));
  auto idx_asg2 = lnast.add_child(idx_sts1, Lnast_node::create_assign(""));
  lnast.add_child(idx_asg2, Lnast_node::create_ref(lhs_sv));
  AttachExprStrToNode(lnast, e_str, idx_asg2);

  auto idx_sts2 = lnast.add_child(idx_if, Lnast_node::create_stmts(get_new_seq_name(lnast)));
  auto idx_shl = lnast.add_child(idx_sts2, Lnast_node::create_shift_left(""));
  lnast.add_child(idx_shl, Lnast_node::create_ref(lhs_sv));
  AttachExprStrToNode(lnast, e_str, idx_shl);
  lnast.add_child(idx_shl, Lnast_node::create_const("1"));

  auto idx_asg3 = lnast.add_child(idx_sts2, Lnast_node::create_dp_assign(""));
  lnast.add_child(idx_asg3, Lnast_node::create_ref(lhs_sv));
  AttachExprStrToNode(lnast, e_str, idx_asg3);
}

/* The Extract Bits primitive op is invoked on some variable
 * and functions as you would expect in a language like Verilog.
 * We have to break this down into multiple statements so
 * LNAST can properly handle it (see diagram below).*/
void Inou_firrtl::HandleExtractBitsOp(Lnast& lnast, const firrtl::FirrtlPB_Expression_PrimOp& op, Lnast_nid& parent_node,
                                      const std::string& lhs) {
  /* x = bits(e1)(numH, numL) should take graph form:
   *      range                 bit_sel
   *    /   |   \             /   |   \
   *___F0 numL numH          x   e1 ___F0  */
  I(lnast.get_data(parent_node).type.is_stmts() || lnast.get_data(parent_node).type.is_cstmts());
  I(op.arg_size() == 1 && op.const__size() == 2);

  auto e1_str           = lnast.add_string(ReturnExprString(lnast, op.arg(0), parent_node, true));
  auto temp_var_name_f0 = create_temp_var(lnast);

  auto idx_range = lnast.add_child(parent_node, Lnast_node::create_range("range_EB"));
  lnast.add_child(idx_range, Lnast_node::create_ref(temp_var_name_f0));
  lnast.add_child(idx_range, Lnast_node::create_const(lnast.add_string(op.const_(1).value())));
  lnast.add_child(idx_range, Lnast_node::create_const(lnast.add_string(op.const_(0).value())));

  auto idx_bit_sel = lnast.add_child(parent_node, Lnast_node::create_bit_select("bit_sel_EB"));
  lnast.add_child(idx_bit_sel, Lnast_node::create_ref(lnast.add_string(lhs)));
  AttachExprStrToNode(lnast, e1_str, idx_bit_sel);
  lnast.add_child(idx_bit_sel, Lnast_node::create_ref(temp_var_name_f0));
}

/* The Head primitive op returns the n most-significant bits
 * from an expression. So if I had an 8-bit variable z and I
 * called head(z)(3), what would return is (in Verilog) z[7:5]. */
void Inou_firrtl::HandleHeadOp(Lnast& lnast, const firrtl::FirrtlPB_Expression_PrimOp& op, Lnast_nid& parent_node,
                               const std::string& lhs) {
  /* x = head(e1)(4) should take graph form: (like x = e1 >> (e1.__bits - 4))
   * Note: the parameter (4) has to be non-negative and l.e.q. the bitwidth of e1 in FIRRTL.
   *      dot               minus          shr
   *   /   |   \           /  |   \     /   |   \
   * ___F0 e1 __bits  ___F1 ___F0  4   x   e1  __F1 */
  I(lnast.get_data(parent_node).type.is_stmts() || lnast.get_data(parent_node).type.is_cstmts());
  I(op.arg_size() == 1 && op.const__size() == 1);

  auto e1_str           = lnast.add_string(ReturnExprString(lnast, op.arg(0), parent_node, true));
  auto temp_var_name_f0 = create_temp_var(lnast);
  auto temp_var_name_f1 = create_temp_var(lnast);

  auto idx_dot = lnast.add_child(parent_node, Lnast_node::create_dot("dot_head"));
  lnast.add_child(idx_dot, Lnast_node::create_ref(temp_var_name_f0));
  AttachExprStrToNode(lnast, e1_str, idx_dot);
  lnast.add_child(idx_dot, Lnast_node::create_ref("__bits"));

  auto idx_mns = lnast.add_child(parent_node, Lnast_node::create_minus("minus_head"));
  lnast.add_child(idx_mns, Lnast_node::create_ref(temp_var_name_f1));
  lnast.add_child(idx_mns, Lnast_node::create_ref(temp_var_name_f0));
  lnast.add_child(idx_mns, Lnast_node::create_const(lnast.add_string(op.const_(0).value())));

  auto idx_shr = lnast.add_child(parent_node, Lnast_node::create_shift_right("shr_head"));
  lnast.add_child(idx_shr, Lnast_node::create_ref(lnast.add_string(lhs)));
  AttachExprStrToNode(lnast, e1_str, idx_shr);
  lnast.add_child(idx_shr, Lnast_node::create_ref(temp_var_name_f1));
}

void Inou_firrtl::HandleTailOp(Lnast& lnast, const firrtl::FirrtlPB_Expression_PrimOp& op, Lnast_nid& parent_node,
                               const std::string& lhs) {
  /* x = tail(expr)(2) should take graph form:
   * NOTE: the shift right is only used to get correct # bits for :=
   *     shr         :=
   *   /  |   \      /  \
   *  x  expr  2    x  expr */
  I(lnast.get_data(parent_node).type.is_stmts() || lnast.get_data(parent_node).type.is_cstmts());
  I(op.arg_size() == 1 && op.const__size() == 1);
  auto lhs_str  = lnast.add_string(lhs);
  auto expr_str = lnast.add_string(ReturnExprString(lnast, op.arg(0), parent_node, true));

  auto idx_shr = lnast.add_child(parent_node, Lnast_node::create_shift_right("shr_tail"));
  // lnast.add_child(idx_shr, Lnast_node::create_ref(lhs_str));
  auto temp_var_name_f1 = create_temp_var(lnast);                      // FIXME: REMOVE ONCE DUMMY ASSIGNS
  lnast.add_child(idx_shr, Lnast_node::create_ref(temp_var_name_f1));  // FIXME: REMOVE ONCE DUMMY ASSIGNS
  AttachExprStrToNode(lnast, expr_str, idx_shr);
  lnast.add_child(idx_shr, Lnast_node::create_const(lnast.add_string(op.const_(0).value())));

  // FIXME: REMOVE ONCE DUMMY ASSIGNS ARE DEALT WITH-----------------------------------
  auto idx_asg = lnast.add_child(parent_node, Lnast_node::create_assign("asg_tail"));
  lnast.add_child(idx_asg, Lnast_node::create_ref(lhs_str));
  lnast.add_child(idx_asg, Lnast_node::create_ref(temp_var_name_f1));
  // FIXME: REMOVE ONCE DUMMY ASSIGNS ARE DEALT WITH-----------------------------------

  auto idx_dp_asg = lnast.add_child(parent_node, Lnast_node::create_dp_assign("dpasg_tail"));
  lnast.add_child(idx_dp_asg, Lnast_node::create_ref(lhs_str));
  AttachExprStrToNode(lnast, expr_str, idx_dp_asg);
}

void Inou_firrtl::HandleConcatOp(Lnast& lnast, const firrtl::FirrtlPB_Expression_PrimOp& op, Lnast_nid& parent_node,
                                 const std::string& lhs) {
  /* x = concat(e1, e2) is the same as Verilog's x = {e1, e2}
   * In LNAST this looks like x = (e1 << e2.__bits) | e2
   *      dot              shl           or
   *     / | \            / | \        /  |  \
   * ___F0 e2 __bits  ___F1 e1 ___F0  x ___F1 e2  */
  I(lnast.get_data(parent_node).type.is_stmts() || lnast.get_data(parent_node).type.is_cstmts());
  I(op.arg_size() == 2);

  auto e1_str           = lnast.add_string(ReturnExprString(lnast, op.arg(0), parent_node, true));
  auto e2_str           = lnast.add_string(ReturnExprString(lnast, op.arg(1), parent_node, true));
  auto temp_var_name_f0 = create_temp_var(lnast);
  auto temp_var_name_f1 = create_temp_var(lnast);

  auto idx_dot = lnast.add_child(parent_node, Lnast_node::create_dot("dot_concat"));
  lnast.add_child(idx_dot, Lnast_node::create_ref(temp_var_name_f0));
  AttachExprStrToNode(lnast, e2_str, idx_dot);
  lnast.add_child(idx_dot, Lnast_node::create_ref("__bits"));

  auto idx_shl = lnast.add_child(parent_node, Lnast_node::create_shift_left("shl_concat"));
  lnast.add_child(idx_shl, Lnast_node::create_ref(temp_var_name_f1));
  AttachExprStrToNode(lnast, e1_str, idx_shl);
  lnast.add_child(idx_shl, Lnast_node::create_ref(temp_var_name_f0));

  auto idx_or = lnast.add_child(parent_node, Lnast_node::create_or("or_concat"));
  lnast.add_child(idx_or, Lnast_node::create_ref(lnast.add_string(lhs)));
  lnast.add_child(idx_or, Lnast_node::create_ref(temp_var_name_f1));
  AttachExprStrToNode(lnast, e2_str, idx_or);
}

// FIXME: This function's "better" (bottom) solution doesn't work yet because
// I think LN->LG requires a temp var has to be the LHS of the node that
// comes immediately before its use. When this eventually works, use
// bottom solution. For now, top solution works (and will for all cases
// where bw(e) <= #.
void Inou_firrtl::HandlePadOp(Lnast& lnast, const firrtl::FirrtlPB_Expression_PrimOp& op, Lnast_nid& parent_node,
                              const std::string& lhs) {
  /* temp solution: just ignore arg and use const # as bitwidth
   *      dot          assign  assign
   *     / | \           /\      /\
   * ___F0 x __bits  ___F0 #    x  e */
  I(lnast.get_data(parent_node).type.is_stmts() || lnast.get_data(parent_node).type.is_cstmts());
  I(op.arg_size() == 1 && op.const__size() == 1);

  auto temp_var_name = create_temp_var(lnast);
  auto lhs_strv      = lnast.add_string(lhs);
  auto e_name        = lnast.add_string(ReturnExprString(lnast, op.arg(0), parent_node, true));
  auto c_name        = lnast.add_string(op.const_(0).value());

  auto idx_dot = lnast.add_child(parent_node, Lnast_node::create_dot("pad"));
  lnast.add_child(idx_dot, Lnast_node::create_ref(temp_var_name));
  lnast.add_child(idx_dot, Lnast_node::create_ref(lhs_strv));
  lnast.add_child(idx_dot, Lnast_node::create_ref("__bits"));

  auto idx_asg1 = lnast.add_child(parent_node, Lnast_node::create_assign("pad_bwasg"));
  lnast.add_child(idx_asg1, Lnast_node::create_ref(temp_var_name));
  lnast.add_child(idx_asg1, Lnast_node::create_const(c_name));

  Lnast_nid idx_asg2;
  if (lhs.substr(0, 1) == "%") {
    idx_asg2 = lnast.add_child(parent_node, Lnast_node::create_dp_assign("pad_dpasg"));
  } else {
    idx_asg2 = lnast.add_child(parent_node, Lnast_node::create_assign("pad_asg"));
  }
  lnast.add_child(idx_asg2, Lnast_node::create_ref(lhs_strv));
  lnast.add_child(idx_asg2, Lnast_node::create_ref(e_name));

  // FIXME: Fully correct solution below! Keep until LN->LG can correctly handle it.
  /* x = pad(e)(4) sets x = e and sets bw(x) = max(4, bw(e));
   *               [___________________________if_________________________________]                         asg
   *               /               /           /                                 \                         /   \
   *        [__ cstmts__]     cond, ___F1    stmts                     [________stmts________]            x     e
   *        /           \                   /      \                  /          |           \
   *      dot            lt, <           dot         asg          dot            dot           asg
   *    /  | \          /  |   \        / | \       /   \        / | \          / | \         /   \
   * ___F0 e __bits ___F1 ___F0 4   ___F2 x __bits ___F2 4   ___F3 x __bits ___F4 e __bits ___F3 ___F4 */
  /*I(lnast.get_data(parent_node).type.is_stmts() || lnast.get_data(parent_node).type.is_cstmts());
  I(op.arg_size() == 1 && op.const__size() == 1);

  auto temp_var_name_f0 = create_temp_var(lnast);
  auto temp_var_name_f1 = create_temp_var(lnast);
  auto temp_var_name_f2 = create_temp_var(lnast);
  auto temp_var_name_f3 = create_temp_var(lnast);
  auto temp_var_name_f4 = create_temp_var(lnast);
  auto e_name = lnast.add_string(ReturnExprString(lnast, op.arg(0), parent_node, true));
  auto c_name = lnast.add_string(op.const_(0).value());
  auto lhs_name = lnast.add_string(lhs);

  auto idx_if = lnast.add_child(parent_node, Lnast_node::create_if("if_pad"));

  auto idx_if_cstmts = lnast.add_child(idx_if, Lnast_node::create_cstmts(get_new_seq_name(lnast)));

  auto idx_dot1 = lnast.add_child(idx_if_cstmts, Lnast_node::create_dot("dot"));
  lnast.add_child(idx_dot1, Lnast_node::create_ref(temp_var_name_f0));
  AttachExprStrToNode(lnast, e_name, idx_dot1);
  lnast.add_child(idx_dot1, Lnast_node::create_ref("__bits"));

  auto idx_lt = lnast.add_child(idx_if_cstmts, Lnast_node::create_lt("lt"));
  lnast.add_child(idx_lt, Lnast_node::create_ref(temp_var_name_f1));
  lnast.add_child(idx_lt, Lnast_node::create_ref(temp_var_name_f0));
  lnast.add_child(idx_lt, Lnast_node::create_const(c_name));

  auto idx_if_cond = lnast.add_child(idx_if, Lnast_node::create_cond(temp_var_name_f1));


  auto idx_if_stmtT = lnast.add_child(idx_if, Lnast_node::create_stmts(get_new_seq_name(lnast)));

  auto idx_dot2 = lnast.add_child(idx_if_stmtT, Lnast_node::create_dot("dot"));
  lnast.add_child(idx_dot2, Lnast_node::create_ref(temp_var_name_f2));
  lnast.add_child(idx_dot2, Lnast_node::create_ref(lhs_name));
  lnast.add_child(idx_dot2, Lnast_node::create_ref("__bits"));

  auto idx_asgT = lnast.add_child(idx_if_stmtT, Lnast_node::create_assign("asg"));
  lnast.add_child(idx_asgT, Lnast_node::create_ref(temp_var_name_f2));
  lnast.add_child(idx_asgT, Lnast_node::create_const(c_name));


  auto idx_if_stmtF = lnast.add_child(idx_if, Lnast_node::create_stmts(get_new_seq_name(lnast)));

  auto idx_dot3 = lnast.add_child(idx_if_stmtF, Lnast_node::create_dot("dot"));
  lnast.add_child(idx_dot3, Lnast_node::create_ref(temp_var_name_f3));
  lnast.add_child(idx_dot3, Lnast_node::create_ref(lhs_name));
  lnast.add_child(idx_dot3, Lnast_node::create_ref("__bits"));

  auto idx_dot4 = lnast.add_child(idx_if_stmtF, Lnast_node::create_dot("dot"));
  lnast.add_child(idx_dot4, Lnast_node::create_ref(temp_var_name_f4));
  AttachExprStrToNode(lnast, e_name, idx_dot4);
  lnast.add_child(idx_dot4, Lnast_node::create_ref("__bits"));

  auto idx_asgF = lnast.add_child(idx_if_stmtF, Lnast_node::create_assign("asg"));
  lnast.add_child(idx_asgF, Lnast_node::create_ref(temp_var_name_f3));
  lnast.add_child(idx_asgF, Lnast_node::create_ref(temp_var_name_f4));


  auto idx_asg = lnast.add_child(parent_node, Lnast_node::create_assign("asg_pad"));
  lnast.add_child(idx_asg, Lnast_node::create_ref(lhs_name));
  AttachExprStrToNode(lnast, e_name, idx_asg);*/
}

/* This function creates the necessary LNAST nodes to express a
 * primitive operation which takes in two expression arguments
 * (dubbed as arguments in the FIRRTL spec, not parameters).
 * Note: NEQ is not handled here because no NEQ node exists in LNAST. */
void Inou_firrtl::HandleTwoExprPrimOp(Lnast& lnast, const firrtl::FirrtlPB_Expression_PrimOp& op, Lnast_nid& parent_node,
                                      const std::string& lhs) {
  I(lnast.get_data(parent_node).type.is_stmts() || lnast.get_data(parent_node).type.is_cstmts());
  I(op.arg_size() == 2);
  auto      e1_str = ReturnExprString(lnast, op.arg(0), parent_node, true);
  auto      e2_str = ReturnExprString(lnast, op.arg(1), parent_node, true);
  Lnast_nid idx_primop;

  switch (op.op()) {
    case firrtl::FirrtlPB_Expression_PrimOp_Op_OP_ADD: {
      idx_primop = lnast.add_child(parent_node, Lnast_node::create_plus("plus"));
      break;
    }
    case firrtl::FirrtlPB_Expression_PrimOp_Op_OP_SUB: {
      idx_primop = lnast.add_child(parent_node, Lnast_node::create_minus("minus"));
      break;
    }
    case firrtl::FirrtlPB_Expression_PrimOp_Op_OP_TIMES: {
      idx_primop = lnast.add_child(parent_node, Lnast_node::create_mult("mult"));
      break;
    }
    case firrtl::FirrtlPB_Expression_PrimOp_Op_OP_DIVIDE: {
      idx_primop = lnast.add_child(parent_node, Lnast_node::create_div("div"));
      break;
    }
    case firrtl::FirrtlPB_Expression_PrimOp_Op_OP_REM: {
      fmt::print("Error: Op_Rem not yet supported in LNAST.\n");  // FIXME...
      I(false);
      break;
    }
    case firrtl::FirrtlPB_Expression_PrimOp_Op_OP_DYNAMIC_SHIFT_LEFT: {
      idx_primop = lnast.add_child(parent_node, Lnast_node::create_shift_left("dshl"));
      break;
    }
    case firrtl::FirrtlPB_Expression_PrimOp_Op_OP_DYNAMIC_SHIFT_RIGHT: {
      idx_primop = lnast.add_child(parent_node, Lnast_node::create_shift_right("dshr"));
      break;
    }
    case firrtl::FirrtlPB_Expression_PrimOp_Op_OP_BIT_AND: {
      idx_primop = lnast.add_child(parent_node, Lnast_node::create_and("and"));
      break;
    }
    case firrtl::FirrtlPB_Expression_PrimOp_Op_OP_BIT_OR: {
      idx_primop = lnast.add_child(parent_node, Lnast_node::create_or("or"));
      break;
    }
    case firrtl::FirrtlPB_Expression_PrimOp_Op_OP_BIT_XOR: {
      idx_primop = lnast.add_child(parent_node, Lnast_node::create_xor("xor"));
      break;
    }
    case firrtl::FirrtlPB_Expression_PrimOp_Op_OP_LESS: {
      idx_primop = lnast.add_child(parent_node, Lnast_node::create_lt("lt"));
      break;
    }
    case firrtl::FirrtlPB_Expression_PrimOp_Op_OP_LESS_EQ: {
      idx_primop = lnast.add_child(parent_node, Lnast_node::create_le("le"));
      break;
    }
    case firrtl::FirrtlPB_Expression_PrimOp_Op_OP_GREATER: {
      idx_primop = lnast.add_child(parent_node, Lnast_node::create_lt("gt"));
      break;
    }
    case firrtl::FirrtlPB_Expression_PrimOp_Op_OP_GREATER_EQ: {
      idx_primop = lnast.add_child(parent_node, Lnast_node::create_ge("ge"));
      break;
    }
    case firrtl::FirrtlPB_Expression_PrimOp_Op_OP_EQUAL: {
      idx_primop = lnast.add_child(parent_node, Lnast_node::create_same("same"));
      break;
    }
    default: {
      fmt::print("Error: expression directed into HandleTwoExprPrimOp that shouldn't have been.\n");
      I(false);
    }
  }

  lnast.add_child(idx_primop, Lnast_node::create_ref(lnast.add_string(lhs)));
  AttachExprStrToNode(lnast, lnast.add_string(e1_str), idx_primop);
  AttachExprStrToNode(lnast, lnast.add_string(e2_str), idx_primop);
}

void Inou_firrtl::HandleStaticShiftOp(Lnast& lnast, const firrtl::FirrtlPB_Expression_PrimOp& op, Lnast_nid& parent_node,
                                      const std::string& lhs) {
  I(lnast.get_data(parent_node).type.is_stmts() || lnast.get_data(parent_node).type.is_cstmts());
  I(op.arg_size() == 1 || op.const__size() == 1);
  auto      e1_str = ReturnExprString(lnast, op.arg(0), parent_node, true);
  Lnast_nid idx_shift;

  switch (op.op()) {
    case firrtl::FirrtlPB_Expression_PrimOp_Op_OP_SHIFT_LEFT: {
      idx_shift = lnast.add_child(parent_node, Lnast_node::create_shift_left("shl"));
      break;
    }
    case firrtl::FirrtlPB_Expression_PrimOp_Op_OP_SHIFT_RIGHT: {
      idx_shift = lnast.add_child(parent_node, Lnast_node::create_shift_right("shr"));
      break;
    }
    default: {
      fmt::print("Error: expression directed into HandleStaticShiftOp that shouldn't have been.\n");
      I(false);
    }
  }

  lnast.add_child(idx_shift, Lnast_node::create_ref(lnast.add_string(lhs)));
  AttachExprStrToNode(lnast, lnast.add_string(e1_str), idx_shift);
  lnast.add_child(idx_shift, Lnast_node::create_const(lnast.add_string(op.const_(0).value())));
}

/* TODO:
 * May have to modify some of these? */
void Inou_firrtl::HandleTypeConvOp(Lnast& lnast, const firrtl::FirrtlPB_Expression_PrimOp& op, Lnast_nid& parent_node,
                                   const std::string& lhs) {
  if (op.op() == firrtl::FirrtlPB_Expression_PrimOp_Op_OP_AS_UINT || op.op() == firrtl::FirrtlPB_Expression_PrimOp_Op_OP_AS_UINT) {
    I(op.arg_size() == 1 && op.const__size() == 0);
    // Set lhs.__sign, then lhs = rhs
    auto lhs_ref       = lnast.add_string(lhs);
    auto e1_str        = lnast.add_string(ReturnExprString(lnast, op.arg(0), parent_node, true));
    auto temp_var_name = create_temp_var(lnast);

    auto idx_dot = lnast.add_child(parent_node, Lnast_node::create_dot("dot"));
    lnast.add_child(idx_dot, Lnast_node::create_ref(temp_var_name));
    lnast.add_child(idx_dot, Lnast_node::create_ref(lhs_ref));
    lnast.add_child(idx_dot, Lnast_node::create_ref("__sign"));

    auto idx_dot_a = lnast.add_child(parent_node, Lnast_node::create_assign("asg_d"));
    lnast.add_child(idx_dot_a, Lnast_node::create_ref(temp_var_name));
    if (op.op() == firrtl::FirrtlPB_Expression_PrimOp_Op_OP_AS_UINT) {
      lnast.add_child(idx_dot_a, Lnast_node::create_ref("false"));
    } else {
      lnast.add_child(idx_dot_a, Lnast_node::create_ref("true"));
    }

    auto idx_asg = lnast.add_child(parent_node, Lnast_node::create_assign("asg"));
    lnast.add_child(idx_asg, Lnast_node::create_ref(lhs_ref));
    lnast.add_child(idx_asg, Lnast_node::create_ref(e1_str));

  } else if (op.op() == firrtl::FirrtlPB_Expression_PrimOp_Op_OP_AS_CLOCK) {
    // FIXME?: Does anything need to be done here? Other than set lhs = rhs
    I(op.arg_size() == 1 && op.const__size() == 0);
    auto lhs_ref = lnast.add_string(lhs);
    auto e1_str  = lnast.add_string(ReturnExprString(lnast, op.arg(0), parent_node, true));

    auto idx_asg = lnast.add_child(parent_node, Lnast_node::create_assign("asg"));
    lnast.add_child(idx_asg, Lnast_node::create_ref(lhs_ref));
    lnast.add_child(idx_asg, Lnast_node::create_ref(e1_str));

  } else if (op.op() == firrtl::FirrtlPB_Expression_PrimOp_Op_OP_AS_FIXED_POINT) {
    fmt::print("Error: fixed point not yet supported\n");
    I(false);

  } else if (op.op() == firrtl::FirrtlPB_Expression_PrimOp_Op_OP_AS_ASYNC_RESET) {
    // FIXME?: Does anything need to be done here? Other than set lhs = rhs
    I(op.arg_size() == 1 && op.const__size() == 0);
    auto lhs_ref = lnast.add_string(lhs);
    auto e1_str  = lnast.add_string(ReturnExprString(lnast, op.arg(0), parent_node, true));

    auto idx_asg = lnast.add_child(parent_node, Lnast_node::create_assign("asg"));
    lnast.add_child(idx_asg, Lnast_node::create_ref(lhs_ref));
    lnast.add_child(idx_asg, Lnast_node::create_ref(e1_str));

  } else if (op.op() == firrtl::FirrtlPB_Expression_PrimOp_Op_OP_AS_INTERVAL) {
    fmt::print("Error: intervals not yet supported\n");
    I(false);
  } else {
    fmt::print("Error: unknown type conversion op in HandleTypeConvOp\n");
    I(false);
  }
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
 * where the string "___F1" would be returned by this function. */
std::string Inou_firrtl::HandleBundVecAcc(Lnast& ln, const firrtl::FirrtlPB_Expression expr, Lnast_nid& parent_node,
                                          const bool is_rhs) {
  // Create a list of each tuple + the element... So submoid.io.a becomes [submod, io, a]
  std::stack<std::string> names;
  std::string             full_str;

  if (expr.has_sub_field()) {
    auto sub_field = expr.sub_field();
    names.push(sub_field.field());
    auto flattened_str = absl::StrCat(CreateNameStack(ln, parent_node, sub_field.expression(), names), ".", sub_field.field());
    fmt::print("sf: flattened_str: {}, {}\n", flattened_str, is_rhs);
    full_str = get_full_name(flattened_str, is_rhs);
    fmt::print("\tfull_str: {}\n", full_str);

  } else if (expr.has_sub_index()) {
    auto sub_idx = expr.sub_index();
    names.push(sub_idx.index().value());
    auto flattened_str = absl::StrCat(CreateNameStack(ln, parent_node, sub_idx.expression(), names), ".", sub_idx.index().value());
    fmt::print("si: flattened_str: {}, {}\n", flattened_str, is_rhs);
    full_str = get_full_name(flattened_str, is_rhs);
    fmt::print("\tfull_str: {}\n", full_str);

  } else if (expr.has_sub_access()) {
    auto sub_access = expr.sub_access();
    /* FIXME: This has a pretty deeply rooted and complex problem. Registers on the rhs
     * are accessed as bundlename_fieldname__q_pin. If there was a vector of registers,
     * this would be like vectorname_0__q_pin, vectorname_1__q_pin, ... . However, this
     * won't work in here because we don't know the number (you know that for subindex,
     * not sub access). The closest that can be achieved is just by adding the "#" to the
     * front and hope that is close enough (it should be in MOST cases). */
    auto bund_name  = ReturnExprString(ln, sub_access.expression(), parent_node, is_rhs);
    auto field_name = ReturnExprString(ln, sub_access.index(), parent_node, is_rhs);

    auto temp_var_name = create_temp_var(ln);
    auto idx_dot       = ln.add_child(parent_node, Lnast_node::create_dot("tmp_hack"));  // FIXME: Should this be dot or select?
    ln.add_child(idx_dot, Lnast_node::create_ref(temp_var_name));
    ln.add_child(idx_dot, Lnast_node::create_ref(bund_name));
    if (isdigit(field_name[0])) {
      ln.add_child(idx_dot, Lnast_node::create_const(field_name));
    } else {
      ln.add_child(idx_dot, Lnast_node::create_ref(field_name));
    }
    return (std::string)temp_var_name;

  } else {
    I(false);
  }

  /*fmt::print("[");
  while(!names.empty()) {
    fmt::print("{}, ", names.top());
    names.pop();
  }
  fmt::print("]\n");*/

  // Create each dot node
  std::string bundle_accessor;
  if (full_str.substr(0, 1) == "$") {
    bundle_accessor = absl::StrCat("$inp_", names.top());
  } else if (full_str.substr(0, 1) == "%") {
    bundle_accessor = absl::StrCat("%out_", names.top());
  } else if (full_str.substr(0, 1) == "#") {
    bundle_accessor = absl::StrCat("#", names.top());
  } else if (full_str.substr(full_str.length() - 7, 7) == "__q_pin") {
    return full_str;
  } else if (inst_to_mod_map.count(names.top())) {
    // If wire is part of a module instance.
    auto module_name      = inst_to_mod_map[names.top()];
    auto str_without_inst = full_str.substr(full_str.find(".") + 1);
    auto dir              = mod_to_io_dir_map[std::make_pair(module_name, str_without_inst)];
    if (dir == 1) {  // PORT_DIRECTION_IN
      bundle_accessor = absl::StrCat("inp_", names.top());
    } else if (dir == 2) {
      bundle_accessor = absl::StrCat("out_", names.top());
    } else {
      fmt::print("Error: direction unknown of {}\n", full_str);
      I(false);
    }
  } else {
    bundle_accessor = names.top();
  }
  // fmt::print("Bundle accessor: {}\n", bundle_accessor);
  names.pop();

  do {
    auto temp_var_name = create_temp_var(ln);
    auto element_name  = names.top();
    names.pop();
    // fmt::print("dot: {} {} {}\n", temp_var_name, bundle_accessor, element_name);
    auto idx_dot = ln.add_child(parent_node, Lnast_node::create_dot(""));
    ln.add_child(idx_dot, Lnast_node::create_ref(temp_var_name));
    ln.add_child(idx_dot, Lnast_node::create_ref(ln.add_string(bundle_accessor)));
    if (isdigit(element_name[0])) {
      ln.add_child(idx_dot, Lnast_node::create_const(ln.add_string(element_name)));
    } else {
      ln.add_child(idx_dot, Lnast_node::create_ref(ln.add_string(element_name)));
    }

    bundle_accessor = temp_var_name;
  } while (!names.empty());

  // fmt::print("return {}\n", bundle_accessor);
  return bundle_accessor;
}

std::string Inou_firrtl::CreateNameStack(Lnast& ln, Lnast_nid& parent_node, const firrtl::FirrtlPB_Expression& expr,
                                         std::stack<std::string>& names) {
  if (expr.has_sub_field()) {
    names.push(expr.sub_field().field());
    return absl::StrCat(CreateNameStack(ln, parent_node, expr.sub_field().expression(), names), ".", expr.sub_field().field());

  } else if (expr.has_sub_access()) {
    auto idx_str = ReturnExprString(ln, expr.sub_access().index(), parent_node, true);
    names.push(idx_str);
    return absl::StrCat(CreateNameStack(ln, parent_node, expr.sub_access().expression(), names), ".", idx_str);

  } else if (expr.has_sub_index()) {
    names.push(expr.sub_index().index().value());
    return absl::StrCat(CreateNameStack(ln, parent_node, expr.sub_index().expression(), names),
                        ".",
                        expr.sub_index().index().value());

  } else if (expr.has_reference()) {
    names.push(expr.reference().id());
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
                                 std::vector<std::tuple<std::string, uint8_t, uint32_t>>& vec) {
  switch (type.type_case()) {
    case firrtl::FirrtlPB_Type::kUintType: {  // UInt type
      vec.push_back(std::make_tuple(port_id, dir, type.uint_type().width().value()));
      break;
    }
    case firrtl::FirrtlPB_Type::kSintType: {  // SInt type
      vec.push_back(std::make_tuple(port_id, dir, type.sint_type().width().value()));
      break;
    }
    case firrtl::FirrtlPB_Type::kClockType: {  // Clock type
      vec.push_back(std::make_tuple(port_id, dir, 1));
      break;
    }
    case firrtl::FirrtlPB_Type::kBundleType: {  // Bundle type
      const firrtl::FirrtlPB_Type_BundleType btype = type.bundle_type();
      for (int i = 0; i < type.bundle_type().field_size(); i++) {
        if (btype.field(i).is_flipped()) {
          uint8_t new_dir;
          if (dir == 1) { // PORT_DIRECTION_IN
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
    }
    case firrtl::FirrtlPB_Type::kVectorType: {  // Vector type
      for (uint32_t i = 0; i < type.vector_type().size(); i++) {
        vec.push_back(std::make_tuple(port_id, dir, 0));
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
    case firrtl::FirrtlPB_Type::kAsyncResetType: {      // AsyncReset type
      vec.push_back(std::make_tuple(port_id, dir, 1));  // FIXME: Anything else I need to do?
      break;
    }
    case firrtl::FirrtlPB_Type::kResetType: {  // Reset type
      vec.push_back(std::make_tuple(port_id, dir, 1));
      break;
    }
    default: cout << "Unknown port type." << endl; I(false);
  }
}

/* This function iterates over the IO of a module and
 * sets the bitwidth of each using a dot node in LNAST. */
void Inou_firrtl::ListPortInfo(Lnast& lnast, const firrtl::FirrtlPB_Port& port, Lnast_nid parent_node) {
  std::vector<std::tuple<std::string, uint8_t, uint32_t>> port_list;  // Terms are as follows: name, direction, # of bits.
  create_io_list(port.type(), port.direction(), port.id(), port_list);

  // fmt::print("Port_list:\n");
  for (auto val : port_list) {
    auto subfield_loc = std::get<0>(val).find(".");
    if (std::get<1>(val) == 1) {  // PORT_DIRECTION_IN
      input_names.insert(std::get<0>(val));
      if (std::get<2>(val) > 0) {
        if (subfield_loc != std::string::npos) {
          create_bitwidth_dot_node(lnast, std::get<2>(val), parent_node, absl::StrCat("$inp_", std::get<0>(val)));
        } else {
          create_bitwidth_dot_node(lnast, std::get<2>(val), parent_node, absl::StrCat("$", std::get<0>(val)));
        }
      }
    } else if (std::get<1>(val) == 2) {  // PORT_DIRECTION_OUT
      output_names.insert(std::get<0>(val));
      if (std::get<2>(val) > 0) {
        if (subfield_loc != std::string::npos) {
          create_bitwidth_dot_node(lnast, std::get<2>(val), parent_node, absl::StrCat("%out_", std::get<0>(val)));
        } else {
          create_bitwidth_dot_node(lnast, std::get<2>(val), parent_node, absl::StrCat("%", std::get<0>(val)));
        }
      }
    } else {
      I(false);  // PORT_DIRECTION_UNKNOWN. Not supported by LNAST.
    }
    // fmt::print("\tname:{} dir:{} bits:{}\n", std::get<0>(val), std::get<1>(val), std::get<2>(val));
  }
}

//-----------Primitive Operations---------------------
/* TODO:
 * Not yet implemented node types (?):
 *   Rem
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
    case firrtl::FirrtlPB_Expression_PrimOp_Op_OP_EQUAL: {
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
    case firrtl::FirrtlPB_Expression_PrimOp_Op_OP_NOT_EQUAL: {
      HandleNEQOp(lnast, op, parent_node, lhs);
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
      I(false);
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
      cout << "primOp: " << op.op() << " not yet supported (FloatingPoint)...\n";
      I(false);
      break;
    }
    case firrtl::FirrtlPB_Expression_PrimOp_Op_OP_WRAP:
    case firrtl::FirrtlPB_Expression_PrimOp_Op_OP_CLIP:
    case firrtl::FirrtlPB_Expression_PrimOp_Op_OP_SQUEEZE:
    case firrtl::FirrtlPB_Expression_PrimOp_Op_OP_AS_INTERVAL: {
      cout << "primOp: " << op.op() << " not yet supported (Intervals)...\n";
      I(false);
      break;
    }
    default: cout << "Unknown PrimaryOp\n"; I(false);
  }
}

//--------------Expressions-----------------------
/*TODO:
 * FixedLiteral
 */

/* */
void Inou_firrtl::InitialExprAdd(Lnast& lnast, const firrtl::FirrtlPB_Expression& expr, Lnast_nid& parent_node,
                                 const std::string& lhs_noprefixes) {
  // Note: here, parent_node is the "stmt" node above where this expression will go.
  I(lnast.get_data(parent_node).type.is_stmts() || lnast.get_data(parent_node).type.is_cstmts());
  auto lhs = get_full_name(lhs_noprefixes, false);
  switch (expr.expression_case()) {
    case firrtl::FirrtlPB_Expression::kReference: {  // Reference
      Lnast_nid idx_asg;
      if (lhs.substr(0, 1) == "%") {
        idx_asg = lnast.add_child(parent_node, Lnast_node::create_dp_assign("dp_asg"));
      } else {
        idx_asg = lnast.add_child(parent_node, Lnast_node::create_assign("asg"));
      }
      lnast.add_child(idx_asg, Lnast_node::create_ref(lnast.add_string(lhs)));
      auto full_name = get_full_name(expr.reference().id(), true);
      lnast.add_child(idx_asg, Lnast_node::create_ref(lnast.add_string(full_name)));  // expr.reference().id()));
      break;
    }
    case firrtl::FirrtlPB_Expression::kUintLiteral: {  // UIntLiteral
      Lnast_nid idx_asg;
      if (lhs.substr(0, 1) == "%") {
        idx_asg = lnast.add_child(parent_node, Lnast_node::create_dp_assign("dp_asg"));
      } else {
        idx_asg = lnast.add_child(parent_node, Lnast_node::create_assign("asg"));
      }
      lnast.add_child(idx_asg, Lnast_node::create_ref(lnast.add_string(lhs)));
      auto str_val = absl::StrCat(expr.uint_literal().value().value(), "u", expr.uint_literal().width().value(), "bits");
      lnast.add_child(idx_asg, Lnast_node::create_const(lnast.add_string(str_val)));
      break;
    }
    case firrtl::FirrtlPB_Expression::kSintLiteral: {  // SIntLiteral
      Lnast_nid idx_asg;
      if (lhs.substr(0, 1) == "%") {
        idx_asg = lnast.add_child(parent_node, Lnast_node::create_dp_assign("dp_asg"));
      } else {
        idx_asg = lnast.add_child(parent_node, Lnast_node::create_assign("asg"));
      }
      lnast.add_child(idx_asg, Lnast_node::create_ref(lnast.add_string(lhs)));
      auto str_val = absl::StrCat(expr.sint_literal().value().value(), "s", expr.sint_literal().width().value(), "bits");
      lnast.add_child(idx_asg, Lnast_node::create_const(lnast.add_string(str_val)));
      break;
    }
    case firrtl::FirrtlPB_Expression::kValidIf: {  // ValidIf
      HandleValidIfAssign(lnast, expr, parent_node, lhs);
      break;
    }
    case firrtl::FirrtlPB_Expression::kMux: {  // Mux
      HandleMuxAssign(lnast, expr, parent_node, lhs);
      break;
    }
    case firrtl::FirrtlPB_Expression::kSubField: {  // SubField
      std::string rhs = HandleBundVecAcc(lnast, expr, parent_node, true);

      Lnast_nid idx_asg;
      if (lhs.substr(0, 1) == "%") {
        idx_asg = lnast.add_child(parent_node, Lnast_node::create_dp_assign("dp_asg"));
      } else {
        idx_asg = lnast.add_child(parent_node, Lnast_node::create_assign("asg"));
      }
      lnast.add_child(idx_asg, Lnast_node::create_ref(lnast.add_string(lhs)));
      lnast.add_child(idx_asg, Lnast_node::create_ref(lnast.add_string(rhs)));
      break;
    }
    case firrtl::FirrtlPB_Expression::kSubIndex: {  // SubIndex
      auto expr_name = lnast.add_string(ReturnExprString(lnast, expr.sub_index().expression(), parent_node, true));

      auto idx_select = lnast.add_child(parent_node, Lnast_node::create_select("selectSI"));
      lnast.add_child(idx_select, Lnast_node::create_ref(lnast.add_string(lhs)));
      AttachExprStrToNode(lnast, expr_name, idx_select);
      lnast.add_child(idx_select, Lnast_node::create_const(lnast.add_string(expr.sub_index().index().value())));
      break;
    }
    case firrtl::FirrtlPB_Expression::kSubAccess: {  // SubAccess
      auto expr_name  = lnast.add_string(ReturnExprString(lnast, expr.sub_access().expression(), parent_node, true));
      auto index_name = lnast.add_string(ReturnExprString(lnast, expr.sub_access().index(), parent_node, true));

      auto idx_select = lnast.add_child(parent_node, Lnast_node::create_select("selectSA"));
      lnast.add_child(idx_select, Lnast_node::create_ref(lnast.add_string(lhs)));
      AttachExprStrToNode(lnast, expr_name, idx_select);
      AttachExprStrToNode(lnast, index_name, idx_select);
      break;
    }
    case firrtl::FirrtlPB_Expression::kPrimOp: {  // PrimOp
      ListPrimOpInfo(lnast, expr.prim_op(), parent_node, lhs);
      break;
    }
    case firrtl::FirrtlPB_Expression::kFixedLiteral: {  // FixedLiteral
      // FIXME: FixedPointLiteral not yet supported in LNAST
      I(false);
      break;
    }
    default: cout << "ERROR in InitialExprAdd ... unknown expression type: " << expr.expression_case() << endl; assert(false);
  }
}

/* This function is used when I need the string to access something.
 * If it's a Reference or a Const, we format them as a string and return.
 * If it's a SubField, we have to create dot nodes and get the variable
 * name that points to the right bundle element (see HandleBundVecAcc function). */
std::string Inou_firrtl::ReturnExprString(Lnast& lnast, const firrtl::FirrtlPB_Expression& expr, Lnast_nid& parent_node,
                                          const bool is_rhs) {
  I(lnast.get_data(parent_node).type.is_stmts() || lnast.get_data(parent_node).type.is_cstmts());

  std::string expr_string = "";
  switch (expr.expression_case()) {
    case firrtl::FirrtlPB_Expression::kReference: {  // Reference
      expr_string = get_full_name(expr.reference().id(), is_rhs);
      break;
    }
    case firrtl::FirrtlPB_Expression::kUintLiteral: {     // UIntLiteral
      expr_string = absl::StrCat(expr.uint_literal().value().value(), "u", expr.uint_literal().width().value(), "bits");
      break;
    }
    case firrtl::FirrtlPB_Expression::kSintLiteral: {     // SIntLiteral
      expr_string = absl::StrCat(expr.sint_literal().value().value(), "s", expr.sint_literal().width().value(), "bits");
      break;
    }
    case firrtl::FirrtlPB_Expression::kValidIf: {  // ValidIf
      expr_string = create_temp_var(lnast);
      HandleValidIfAssign(lnast, expr, parent_node, expr_string);
      break;
    }
    case firrtl::FirrtlPB_Expression::kMux: {  // Mux
      expr_string = create_temp_var(lnast);
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
      expr_string = create_temp_var(lnast);
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
      fmt::print("Failure: {}\n", expr.expression_case());
      I(false);
    }
  }
  return expr_string;
}

/* This function takes in a string and adds it into the LNAST as
 * a child of the provided "parent_node". Note: the access_str should
 * already have any $/%/#/__q_pin added to it before this is called. */
void Inou_firrtl::AttachExprStrToNode(Lnast& lnast, const std::string_view access_str, Lnast_nid& parent_node) {
  I(!lnast.get_data(parent_node).type.is_stmts() && !lnast.get_data(parent_node).type.is_cstmts());

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
/*TODO:
 * Memory
 * CMemory
 * Stop
 * Printf
 * PartialConnect
 * IsInvalid
 * MemoryPort
 * Attach
 */
void Inou_firrtl::ListStatementInfo(Lnast& lnast, const firrtl::FirrtlPB_Statement& stmt, Lnast_nid& parent_node) {
  // Print out statement
  switch (stmt.statement_case()) {
    case firrtl::FirrtlPB_Statement::kWire: {  // Wire
      init_wire_dots(lnast, stmt.wire().type(), stmt.wire().id(), parent_node);
      break;
    }
    case firrtl::FirrtlPB_Statement::kRegister: {  // Register
      register_names.insert(stmt.register_().id());
      auto clk_name  = lnast.add_string(ReturnExprString(lnast, stmt.register_().clock(), parent_node, true));
      auto rst_name  = lnast.add_string(ReturnExprString(lnast, stmt.register_().reset(), parent_node, true));
      auto init_name = lnast.add_string(ReturnExprString(lnast, stmt.register_().init(), parent_node, true));
      init_reg_dots(lnast,
                    stmt.register_().type(),
                    absl::StrCat("#", stmt.register_().id()),
                    clk_name,
                    rst_name,
                    init_name,
                    parent_node);
      break;
    }
    case firrtl::FirrtlPB_Statement::kMemory: {  // Memory
      cout << "mem " << stmt.memory().id() << " :\n\t";
      // ListTypeInfo(
      cout << "\tdepth => ";
      switch (stmt.memory().depth_case()) {
        case 0: {
          cout << "Depth not set, ERROR\n";
          break;
        }
        case 3: {
          cout << stmt.memory().uint_depth() << "\n";
          break;
        }
        case 9: {
          // FIXME: Not sure this case will work properly... More testing needed.
          std::string depth = stmt.memory().bigint_depth().value();  // 2s complement binary rep.
          cout << depth << "\n";
          break;
        }
        default: cout << "Memory depth error\n";
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
      switch (stmt.memory().read_under_write()) {
        case 0: cout << "undefined\n"; break;
        case 1: cout << "old\n"; break;
        case 2: cout << "new\n"; break;
        default: cout << "RUW Error...\n";
      }
      I(false);  // TODO: Memory not yet supported.
      break;
    }
    case firrtl::FirrtlPB_Statement::kCmemory: {  // CMemory
      I(false);                                   // TODO: Memory not yet supported.
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
      auto idx_when = lnast.add_child(parent_node, Lnast_node::create_if("when"));
      auto idx_csts = lnast.add_child(idx_when, Lnast_node::create_cstmts(get_new_seq_name(lnast)));
      auto cond_str = lnast.add_string(ReturnExprString(lnast, stmt.when().predicate(), idx_csts, true));
      lnast.add_child(idx_when, Lnast_node::create_cond(cond_str));

      auto idx_stmts_t = lnast.add_child(idx_when, Lnast_node::create_stmts(get_new_seq_name(lnast)));

      for (int i = 0; i < stmt.when().consequent_size(); i++) {
        ListStatementInfo(lnast, stmt.when().consequent(i), idx_stmts_t);
      }
      if (stmt.when().otherwise_size() > 0) {
        auto idx_stmts_f = lnast.add_child(idx_when, Lnast_node::create_stmts(get_new_seq_name(lnast)));
        for (int j = 0; j < stmt.when().otherwise_size(); j++) {
          ListStatementInfo(lnast, stmt.when().otherwise(j), idx_stmts_f);
        }
      }
      break;
    }
    case firrtl::FirrtlPB_Statement::kStop: {  // Stop
      //TODO
      I(false);
      break;
    }
    case firrtl::FirrtlPB_Statement::kPrintf: {  // Printf
      //TODO
      I(false);
      break;
    }
    case firrtl::FirrtlPB_Statement::kSkip: {  // Skip
      cout << "skip;\n";
      break;
    }
    case firrtl::FirrtlPB_Statement::kConnect: {  // Connect -- Must have form (female/bi-gender expression) <=
                                                  // (male/bi-gender/passive expression)
      std::string lhs_string = ReturnExprString(lnast, stmt.connect().location(), parent_node, false);
      InitialExprAdd(lnast, stmt.connect().expression(), parent_node, lhs_string);

      break;
    }
    case firrtl::FirrtlPB_Statement::kPartialConnect: {  // PartialConnect
      fmt::print("Error: PartialConnect not supported on FIRRTL interface, consider not using PartialConnect semantics.\n");
      I(false);
      break;
    }
    case firrtl::FirrtlPB_Statement::kIsInvalid: {  // IsInvalid
      //TODO
      I(false);
      break;
    }
    case firrtl::FirrtlPB_Statement::kMemoryPort: {  // MemoryPort
      //TODO
      I(false);
      break;
    }
    case firrtl::FirrtlPB_Statement::kAttach: {  // Attach
      cout << "Attach\n";
      I(false);
      break;
    }
    default:
      cout << "Unknown statement type." << endl;
      I(false);
      return;
  }

  // TODO: Attach source info into node creation (line #, col #).
}

//--------------Modules/Circuits--------------------
// Create basis of LNAST tree. Set root to "top" and have "stmts" be top's child.
void Inou_firrtl::ListUserModuleInfo(Eprp_var& var, const firrtl::FirrtlPB_Module& module, const std::string& file_name) {
  cout << "Module (user): " << module.user_module().id() << endl;
  std::unique_ptr<Lnast> lnast = std::make_unique<Lnast>(module.user_module().id(), file_name);

  const firrtl::FirrtlPB_Module_UserModule& user_module = module.user_module();

  lnast->set_root(Lnast_node(Lnast_ntype::create_top(), Token(0, 0, 0, 0, "top")));
  auto idx_stmts = lnast->add_child(lnast->get_root(), Lnast_node::create_stmts(get_new_seq_name(*lnast)));

  // Iterate over I/O of the module.
  for (int i = 0; i < user_module.port_size(); i++) {
    const firrtl::FirrtlPB_Port& port = user_module.port(i);
    ListPortInfo(*lnast, port, idx_stmts);
  }

  // Iterate over statements of the module.
  for (int j = 0; j < user_module.statement_size(); j++) {
    const firrtl::FirrtlPB_Statement& stmt = user_module.statement(j);
    ListStatementInfo(*lnast, stmt, idx_stmts);
    // lnast->dump();
  }
  // lnast->dump();
  var.add(std::move(lnast));
}

// TODO: External module handling.
void Inou_firrtl::ListModuleInfo(Eprp_var& var, const firrtl::FirrtlPB_Module& module, const std::string& file_name) {
  if (module.has_external_module()) {
    cout << "External module.\n";
    I(false);  // FIXME: not yet implemented
  } else if (module.has_user_module()) {
    ListUserModuleInfo(var, module, file_name);
  } else {
    cout << "Module not set.\n";
    I(false);
  }
}

void Inou_firrtl::PopulateAllModsIO(Eprp_var& var, const firrtl::FirrtlPB_Circuit& circuit, const std::string& file_name) {
  for (int i = 0; i < circuit.module_size(); i++) {
    // std::vector<std::pair<std::string, uint8_t>> vec;
    if (circuit.module(i).has_external_module()) {
      //FIXME: Support
      // mod_to_io_map[circuit.module(i).external_module().id()] = vec;
    } else if (circuit.module(i).has_user_module()) {
      //auto sub = AddModToLibrary(var, circuit.module(i).user_module().id(), file_name);
      //uint64_t inp_pos = 0;
      //uint64_t out_pos = 0;
      for (int j = 0; j < circuit.module(i).user_module().port_size(); j++) {
        auto port = circuit.module(i).user_module().port(j);
        AddPortToMap(circuit.module(i).user_module().id(), port.type(), port.direction(), port.id());//, sub, inp_pos, out_pos);
      }
    } else {
      cout << "Module not set.\n";
      I(false);
    }
  }
}

Sub_node Inou_firrtl::AddModToLibrary(Eprp_var& var, const std::string& mod_name, const std::string& file_name) {
  std::string fpath;
  if (var.has_label("path")) {
    fpath = var.get("path");
  } else {
    fpath = "lgdb";
    fmt::print("FIXME: Does this even happen ever due to having default?\n");
  }

  auto *library = Graph_library::instance(fpath);
  auto &sub = library->reset_sub(mod_name, "inou.lnast_dfg.tolg");//FIXME: change file name to file_name
  return sub;
}

/* Used to populate Sub_Nodes so that when LGraphs are constructed,
 * all the LGraphs will be able to populate regardless of order. */
void Inou_firrtl::AddPortToSub(Sub_node& sub, uint64_t& inp_pos, uint64_t& out_pos, const std::string& port_id, const uint8_t& dir) {
 if (dir == 1) { // PORT_DIRECTION_IN
   sub.add_input_pin(port_id);//, inp_pos);
   inp_pos++;
 } else {
   sub.add_output_pin(port_id);//, out_pos);
   out_pos++;
 }
}

void Inou_firrtl::AddPortToMap(const std::string& mod_id, const firrtl::FirrtlPB_Type& type, uint8_t dir, const std::string& port_id) {//,
                               //Sub_node& sub, const std::string& port_id, uint64_t& inp_pos, uint64_t& out_pos) {
  switch (type.type_case()) {
    case firrtl::FirrtlPB_Type::kUintType: { // UInt type
      //AddPortToSub(sub, inp_pos, out_pos, port_id, dir);
      mod_to_io_dir_map[std::make_pair(mod_id, port_id)] = dir;
      mod_to_io_map[mod_id].insert({port_id, type.uint_type().width().value(), dir});
      break;
    }
    case firrtl::FirrtlPB_Type::kSintType: { // SInt type
      //AddPortToSub(sub, inp_pos, out_pos, port_id, dir);
      mod_to_io_dir_map[std::make_pair(mod_id, port_id)] = dir;
      mod_to_io_map[mod_id].insert({port_id, type.sint_type().width().value(), dir});
      break;
    }
    case firrtl::FirrtlPB_Type::kClockType: { // Clock type
      //AddPortToSub(sub, inp_pos, out_pos, port_id, dir);
      mod_to_io_dir_map[std::make_pair(mod_id, port_id)] = dir;
      mod_to_io_map[mod_id].insert({port_id, 1, dir});
      break;
    }
    case firrtl::FirrtlPB_Type::kAsyncResetType: { // AsyncReset type
      //AddPortToSub(sub, inp_pos, out_pos, port_id, dir);
      mod_to_io_dir_map[std::make_pair(mod_id, port_id)] = dir;
      mod_to_io_map[mod_id].insert({port_id, 1, dir});
      break;
    }
    case firrtl::FirrtlPB_Type::kResetType: { // Reset type
      //AddPortToSub(sub, inp_pos, out_pos, port_id, dir);
      mod_to_io_dir_map[std::make_pair(mod_id, port_id)] = dir;
      mod_to_io_map[mod_id].insert({port_id, 1, dir});
      break;
    }
    case firrtl::FirrtlPB_Type::kBundleType: {  // Bundle type
      const firrtl::FirrtlPB_Type_BundleType btype = type.bundle_type();
      for (int i = 0; i < type.bundle_type().field_size(); i++) {
        if (btype.field(i).is_flipped()) {
          uint8_t new_dir;
          if (dir == 1) { // PORT_DIRECTION_IN
            new_dir = 2;
          } else if (dir == 2) {
            new_dir = 1;
          }
          AddPortToMap(mod_id, btype.field(i).type(), new_dir, port_id + "." + btype.field(i).id());//, sub, inp_pos, out_pos);
        } else {
          AddPortToMap(mod_id, btype.field(i).type(), dir, port_id + "." + btype.field(i).id());//, sub, inp_pos, out_pos);
        }
      }
      break;
    }
    case firrtl::FirrtlPB_Type::kVectorType: {  // Vector type
      //FIXME: How does mod_to_io_map interact with a vector?
      mod_to_io_dir_map[std::make_pair(mod_id, port_id)] = dir;
      for (uint32_t i = 0; i < type.vector_type().size(); i++) {
        AddPortToMap(mod_id, type.vector_type().type(), dir, absl::StrCat(port_id, ".", i));//, sub, inp_pos, out_pos);
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
    default: cout << "Unknown port type." << endl; I(false);
  }
}

void Inou_firrtl::IterateModules(Eprp_var& var, const firrtl::FirrtlPB_Circuit& circuit, const std::string& file_name) {
  if (circuit.top_size() > 1) {
    cout << "ERROR: More than 1 top module?\n";
    I(false);
  }

  // Create ModuleName to I/O Pair List
  PopulateAllModsIO(var, circuit, file_name);

  // For each module, create an LNAST pointer
  for (int i = 0; i < circuit.module_size(); i++) {
    ListModuleInfo(var, circuit.module(i), file_name);

    // Between modules, empty out the input/output/register lists.
    input_names.clear();
    output_names.clear();
    register_names.clear();
  }
}

// Iterate over every FIRRTL circuit (design), each circuit can contain multiple modules.
void Inou_firrtl::IterateCircuits(Eprp_var& var, const firrtl::FirrtlPB& firrtl_input, const std::string& file_name) {
  for (int i = 0; i < firrtl_input.circuit_size(); i++) {
    const firrtl::FirrtlPB_Circuit& circuit = firrtl_input.circuit(i);
    IterateModules(var, circuit, file_name);
  }
}

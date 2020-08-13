//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <math.h>

#include "inou_firrtl.hpp"

#define PRINT_DEBUG

/* Iterate over all nodes, looking to see if any
 * circuit components (wires, regs, IO, submodules)
 * can be found in the LNAST. If found, add to
 * Firrtl Module. */
void Inou_firrtl::FindCircuitComps(Lnast &ln, firrtl::FirrtlPB_Module_UserModule *umod) {
  fmt::print("FindCircuitComps\n");
  const auto top   = ln.get_root();
  const auto stmts = ln.get_first_child(top);
  for (const auto &lnidx : ln.children(stmts)) {
    SearchNode(ln, lnidx, umod);
  }
  /*for (auto iter = io_map.begin(); iter != io_map.end(); iter++) {
    fmt::print("io: {}\n", iter->first);
  }
  for (auto iter = reg_wire_map.begin(); iter != reg_wire_map.end(); iter++) {
    fmt::print("ri: {}\n", iter->first);
  }*/
}

/* Look at a specific LNAST node and search it plus
 * its children to see if there are any circuit
 * components. */
void Inou_firrtl::SearchNode(Lnast &ln, const Lnast_nid &parent_node, firrtl::FirrtlPB_Module_UserModule *umod) {
  const auto ntype = ln.get_data(parent_node).type;
  if (ntype.is_invalid()) {  // Note: may have to add more to this list
    return;
  } else if (ntype.is_ref()) {
    CheckRefForComp(ln, parent_node, umod);
  } else if (ntype.is_cond()) {
    // Cond, possibly, is a circuit component (or just a const/temp var)
    if (!isdigit(ln.get_name(parent_node)[0])) {
      CheckRefForComp(ln, parent_node, umod);
    }
  } else if (ntype.is_tuple()) {
    CheckTuple(ln, parent_node, umod);
  } else if (ntype.is_func_call()) {
    CreateSubmodInstance(ln, parent_node, umod);
  } else if (ntype.is_dot()) {
    return;
  } else {
    // If "regular" node
    for (const auto &lnidx : ln.children(parent_node)) {
      SearchNode(ln, lnidx, umod);
    }
  }
}

/* This function creates the specified type
 * and provides a pointer to that type. */
// FIXME: This only works for UInt type right now
firrtl::FirrtlPB_Type *Inou_firrtl::CreateTypeObject(uint32_t bitwidth) {
  auto uint_type = new firrtl::FirrtlPB_Type_UIntType();
  if (bitwidth > 0) {
    auto width = new firrtl::FirrtlPB_Width();
    width->set_value(bitwidth);

    uint_type->set_allocated_width(width);
  }

  auto type = new firrtl::FirrtlPB_Type();
  type->set_allocated_uint_type(uint_type);

  return type;
}

/* I assume that this is going to be used as
 * arguments to some func_call. Thus, ignore
 * LHS of tuple and LHS of each key-val assign. */
void Inou_firrtl::CheckTuple(Lnast &ln, const Lnast_nid &tup_node, firrtl::FirrtlPB_Module_UserModule *umod) {
  bool first = true;
  for (const auto &node : ln.children(tup_node)) {
    auto ntype = ln.get_data(node).type;
    if (first) {
      I(ntype.is_ref());
      first = false;
    } else {
      // Check each key-val assign node.
      I(ntype.is_assign()); //Maybe this should also include dp_assign?
      auto asg_lhs = ln.get_first_child(node);
      auto asg_rhs = ln.get_sibling_next(asg_lhs);
      if (ln.get_data(asg_rhs).type.is_ref()) {
        // If RHS is a ref, check if its a circuit component.
        CheckRefForComp(ln, asg_rhs, umod);
      }
    }
  }
}

/* Check to see if ref could be a wire/reg/IO.
 * If it is, then check to see if it was already
 * added. If it hasn't been added yet, then add it. */
void Inou_firrtl::CheckRefForComp(Lnast &ln, const Lnast_nid &ref_node, firrtl::FirrtlPB_Module_UserModule *umod) {
  auto name = ln.get_name(ref_node);
  if ((name.substr(0, 2) == "__") || (name.substr(0, 5) == "false") || (name.substr(0, 4) == "true")) {
    // __ = attribute, ___ = temp var
    return;

  } else if (name.substr(0, 1) == "$" || name.substr(0, 1) == "%") {
    // $ = input
    if (io_map.contains(name.substr(1)))
      return;
    auto port = umod->add_port();
    port->set_id((std::string)name.substr(1));
    //auto type = CreateTypeObject(ln.get_bitwidth(name.substr(1)));
    firrtl::FirrtlPB_Type *type;
    if (ln.is_in_bw_table(name.substr(1))) {
      type = CreateTypeObject(ln.get_bitwidth(name.substr(1)));
    } else {
      fmt::print("{}\n", name);
      I(!(name.substr(0,1) == "$")); // Inputs HAVE to have bw
      type = CreateTypeObject(0);
    }
    port->set_allocated_type(type);

    if (name.substr(0, 1) == "$")
      port->set_direction(firrtl::FirrtlPB_Port_Direction_PORT_DIRECTION_IN);
    else
      port->set_direction(firrtl::FirrtlPB_Port_Direction_PORT_DIRECTION_OUT);

    io_map[(std::string)name.substr(1)] = port;

  } else if (name.substr(0, 1) == "#") {
    // # = register
    if (reg_wire_map.contains(name.substr(1)))
      return;
    auto reg = new firrtl::FirrtlPB_Statement_Register();
    reg->set_id((std::string)name.substr(1));

    //auto type = CreateTypeObject(ln.get_bitwidth(name.substr(1)));  // FIXME: Just setting bits to implicit right now
    firrtl::FirrtlPB_Type *type;
    if (ln.is_in_bw_table(name.substr(1))) {
      type = CreateTypeObject(ln.get_bitwidth(name.substr(1)));
    } else {
      type = CreateTypeObject(0);
    }
    reg->set_allocated_type(type);

    /* Specify register reset and init as UInt(0). Clock isn't
     * specified here since a default doesn't work well. */
    reg->set_allocated_reset(CreateULitExpr(0));
    reg->set_allocated_init(CreateULitExpr(0));

    auto fstmt = umod->add_statement();
    fstmt->set_allocated_register_(reg);
    reg_wire_map[(std::string)name.substr(1)] = fstmt;

  } else if (name.substr(0, 3) == "_._") {
    // _._ = wire //FIXME: but change front to something else? currently changes _._ to _
    auto new_name = absl::StrCat("_", name.substr(3));
    if (reg_wire_map.contains(new_name))
      return;
    auto wire = new firrtl::FirrtlPB_Statement_Wire();
    wire->set_id(new_name);

    //auto type = CreateTypeObject(ln.get_bitwidth(name));  // FIXME: Just setting bits to implicit right now
    firrtl::FirrtlPB_Type *type;
    if (ln.is_in_bw_table(name)) {
      type = CreateTypeObject(ln.get_bitwidth(name));
    } else {
      type = CreateTypeObject(0);
    }
    wire->set_allocated_type(type);

    auto fstmt = umod->add_statement();
    fstmt->set_allocated_wire(wire);
    reg_wire_map[new_name] = fstmt;

  } else {
    // otherwise = wire
    if (reg_wire_map.contains(name))
      return;
    //if (wire_rename_map.contains(name)) // Ignore this, since it's a call to a submodule and not a wire.
    //  return;
    auto wire = new firrtl::FirrtlPB_Statement_Wire();
    wire->set_id((std::string)name);  // FIXME: Figure out best way to use renaming map to fix submodule input tuple names

    //auto type = CreateTypeObject(ln.get_bitwidth(name));  // FIXME: Just setting bits to implicit right now
    firrtl::FirrtlPB_Type *type;
    if (ln.is_in_bw_table(name)) {
      type = CreateTypeObject(ln.get_bitwidth(name));
    } else {
      type = CreateTypeObject(0);
    }
    wire->set_allocated_type(type);

    auto fstmt = umod->add_statement();
    fstmt->set_allocated_wire(wire);
    reg_wire_map[(std::string)name] = fstmt;
  }
}

firrtl::FirrtlPB_Expression *Inou_firrtl::CreateULitExpr(const uint32_t &val) {
  firrtl::FirrtlPB_Expression_IntegerLiteral *num = new firrtl::FirrtlPB_Expression_IntegerLiteral();
  num->set_value(std::to_string(val));
  firrtl::FirrtlPB_Width *width = new firrtl::FirrtlPB_Width();
  if (val == 0) {
    width->set_value(1);
  } else {
    width->set_value(log2(val) + 1);
  }

  firrtl::FirrtlPB_Expression_UIntLiteral *ulit = new firrtl::FirrtlPB_Expression_UIntLiteral();
  ulit->set_allocated_value(num);
  ulit->set_allocated_width(width);

  firrtl::FirrtlPB_Expression *expr = new firrtl::FirrtlPB_Expression();
  expr->set_allocated_uint_literal(ulit);

  return expr;
}

void Inou_firrtl::CreateSubmodInstance(Lnast &ln, const Lnast_nid &fcall_node, firrtl::FirrtlPB_Module_UserModule *umod) {
  auto func_out = ln.get_first_child(fcall_node);
  auto mod_name = ln.get_sibling_next(func_out);
  auto func_inp = ln.get_sibling_next(mod_name);

  // 1. Converge func_out name with func_inp name
  // 2. Create submodule instance with this name
  auto inst        = new firrtl::FirrtlPB_Statement_Instance();
  auto submod_name = ConvergeFCallNames(ln.get_name(func_out), ln.get_name(func_inp));
  inst->set_id((std::string)submod_name);
  inst->set_module_id((std::string)ln.get_name(mod_name));

  auto fstmt = umod->add_statement();
  fstmt->set_allocated_instance(inst);
}

/* Function calls in LNAST have a name for the input tuple and
 * a name for the output tuple. FIRRTL only allows for one name.
 * Thus, we must specify what name we will use. In general, take
 * output tuple name and make that the standard (means changing
 * input tuple names to match output tuple when seen in LNAST). */
// FIXME: This function needs some work...
std::string_view Inou_firrtl::ConvergeFCallNames(const std::string_view func_out, const std::string_view func_inp) {
  if (func_inp.substr(0, 4) == "inp_" && func_out.substr(0, 4) == "out_" && func_inp.substr(4) == func_out.substr(4)) {
    // Specific case from FIRRTL->LNAST translation.
    // some function call like out_foo = submodule(inp_foo) will get its bundle name set to foo
    wire_rename_map[(std::string)func_inp] = func_inp.substr(4);
    wire_rename_map[(std::string)func_out] = func_out.substr(4);
    return func_out.substr(4);;
  } else {
    // Change the map (which alters some wire names)
    wire_rename_map[(std::string)func_inp] = (std::string)func_out;
    return func_out;
  }
}

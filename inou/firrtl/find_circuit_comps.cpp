//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <math.h>

#include "inou_firrtl.hpp"

/* Iterate over all nodes, looking to see if any
 * circuit components (wires, regs, IO, submodules)
 * can be found in the LNAST. If found, add to
 * Firrtl Module. */
void Inou_firrtl::FindCircuitComps(Lnast &ln, firrtl::FirrtlPB_Module_UserModule *umod) {
  fmt::print("FindCircuitComps\n");
  const auto stmts = ln.get_first_child(mmap_lib::Tree_index::root());
  for (const auto &lnidx : ln.children(stmts)) {
    SearchNode(ln, lnidx, umod);
  }
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
#if 0
  } else if (ntype.is_tuple()) {
    CheckTuple(ln, parent_node, umod);
#endif
  } else if (ntype.is_func_call()) {
    CreateSubmodInst(ln, parent_node, umod);
#if 0
  } else if (ntype.is_select()) {
    return;
#endif
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
      I(ntype.is_assign());  // Maybe this should also include dp_assign?
      auto asg_lhs = ln.get_first_child(node);
      auto asg_rhs = ln.get_sibling_next(asg_lhs);
      if (ln.get_data(asg_rhs).type.is_ref()) {
        // If RHS is a ref, check if its a circuit component.
        CheckRefForComp(ln, asg_rhs, umod);
      }
    }
  }

  if (ln.get_name(tup_node).substr(0, 6) == "memory") {
    HandleMemTup(ln, tup_node, umod);
  }
}

/* FIXME: STRICTLY WIP RIGHT NOW. DO NOT EXPECT THIS TO WORK.
 * ----------------------------------------------------------
 * Due to being broken into multiple separate tuples, handling
 * Memory blocks can be difficult on this interface. To handle
 * them, this interface looks for all the tuples where port
 * attributes are defined in (memory1). Then it looks for where
 * they are combined all into one tuple (memory2). Finally, when
 * the tuple for the whole memory block is found (memory3), the
 * actual memory statement is made and all ports are specified. */
void Inou_firrtl::HandleMemTup(Lnast &ln, const Lnast_nid &tup_node, firrtl::FirrtlPB_Module_UserModule *umod) {
  auto tup_node_name = ln.get_name(tup_node);
  if (tup_node_name == "memory1") {
    // memory1 tuples store port attr (but not port name)
    auto lhs_name              = ln.get_name(ln.get_first_child(tup_node));
    pname_to_tup_map[lhs_name] = tup_node;

  } else if (tup_node_name == "memory2") {
    // memory2 tuple helps map memory1 temp names to port names
    auto             first = true;
    mmap_lib::str tup_name;
    for (const auto &child : ln.children(tup_node)) {
      if (first) {
        tup_name = ln.get_name(child);
        first    = false;
      } else {
        auto lhs_node  = ln.get_first_child(child);
        auto port_name = ln.get_name(lhs_node);
        auto temp_name = ln.get_name(ln.get_sibling_next(lhs_node));

        // Change from temp name to actual port name in map.
        auto tup_attr_node = pname_to_tup_map[temp_name];
        pname_to_tup_map.erase(temp_name);
        pname_to_tup_map[port_name] = tup_attr_node;

        mem_to_ports_lists[tup_name].insert(port_name);
      }
    }

  } else {  // should be memory3
    // memory3 tuple is the actual memory + attrs (.__port, .__size, ...)
    //std::string_view mem_name;
    Lnast_nid        ports_rhs, size_rhs;
    auto             first = true;
    for (const auto &child : ln.children(tup_node)) {
      if (first) {
        first = false;
      } else {
        I(ln.get_type(child).is_assign());
        auto lhs_asg = ln.get_first_child(child);
        auto rhs_asg = ln.get_sibling_next(lhs_asg);

        if (ln.get_name(lhs_asg) == "__port") {
          ports_rhs = rhs_asg;
        } else if (ln.get_name(lhs_asg) == "__size") {
          I(ln.get_type(rhs_asg).is_const());
          size_rhs = rhs_asg;
        } else {
          I(false);  // FIXME: Something came up in tuple that shouldn't be there?
        }
      }
    }
    //auto     size_str = Lconst::from_pyrope(ln.get_name(size_rhs)).to_firrtl();
    //uint32_t size_val = std::stoul(size_str);  // FIXME: Could cause problems for sizes >= 2^32 (can use BigInt in proto for this)
    //auto     size_str = Lconst::from_pyrope(ln.get_name(size_rhs)).to_firrtl();
    //uint32_t size_val = size_str.to_i();

    auto     size_val = Lconst::from_pyrope(ln.get_name(size_rhs)).to_i();

    // Actually create Memory statement + subexpressions
    auto type = CreateTypeObject(0);  // leave bw as implicit for now

    auto mem_stmt = new firrtl::FirrtlPB_Statement_Memory();
    //mem_stmt->set_id(mem_name.substr(1).to_s());
    mem_stmt->set_allocated_type(type);
    mem_stmt->set_uint_depth(size_val);

    auto fstmt = umod->add_statement();
    fstmt->set_allocated_memory(mem_stmt);

    for (const auto &tup_str : mem_to_ports_lists[ln.get_name(ports_rhs)]) {
      auto    port_tup_node = pname_to_tup_map[tup_str];
      uint8_t port_type     = 0;  // 0 = read, 1 = write, 2 = read-write
      for (const auto &child : ln.children(port_tup_node)) {
        // Do an initial pass over to see if this is a read/write/read-write port.
        auto lhs_asg = ln.get_first_child(child);
        auto rhs_asg = ln.get_sibling_next(lhs_asg);
        (void)rhs_asg;
        auto attr_str = ln.get_name(lhs_asg);
        (void)attr_str;
        /* FIXME: Need to identify right here if read/write/read-write.
         * Then use that in next for-loop. Maybe determine by some attr? */
        // set port_type here
      }

      // Now add to mem_stmt as read/write/read-write port as string. like mem_stmt->add_reader_id(...)
      // use variable "port_type" as condition

      // Create many assignments where all the attributes are specified for a port.
      for (const auto &child : ln.children(port_tup_node)) {
        // Iterate over each of the assign statements that set a port's attributes.
        auto lhs_asg  = ln.get_first_child(child);
        auto rhs_asg  = ln.get_sibling_next(lhs_asg);
        auto attr_str = ln.get_name(lhs_asg);
        if (attr_str == "__posedge") {
          Pass::warn("attribute __posedge used for a memory port tuple, but only posedge=true is supported in FIRRTL");
        } else if (attr_str == "__type") { // 0: async, 1:sync, 2:array
          // based on port_type, help determine read_lat and write_lat
        } else if (attr_str == "__fwd") {
          // help determine memory statement's read_under_write policy
        } else if (attr_str.substr(0, 2) == "__") {
          auto mem_id_ref = new firrtl::FirrtlPB_Expression_Reference();
          //mem_id_ref->set_id(mem_name.substr(1).to_s());
          auto mem_id_expr = new firrtl::FirrtlPB_Expression();
          mem_id_expr->set_allocated_reference(mem_id_ref);

          auto subfield_expr = new firrtl::FirrtlPB_Expression_SubField();
          subfield_expr->set_allocated_expression(mem_id_expr);
          auto lhs_expr = new firrtl::FirrtlPB_Expression();
          lhs_expr->set_allocated_sub_field(subfield_expr);

          // FIXME: Haven't yet specified all attributes for all cases.
          // RW need: wmode, rdata, wdata. W need: data, R need: data.
          if (attr_str == "__addr") {
            subfield_expr->set_field("addr");
          } else if (attr_str == "__clk_pin") {
            subfield_expr->set_field("clk");
          } else if (attr_str == "__enable") {
            subfield_expr->set_field("en");
          } else if (attr_str == "__wrmask") {
            if (port_type == 2) {  // read-writer
              subfield_expr->set_field("wmask");
            } else if (port_type == 1) {  // writer
              subfield_expr->set_field("mask");
            }
          } else {
            Pass::error("Specifying attribute {} in a memory tuple, but not yet supported on LN->FIR interface", attr_str);
          }
          // Now that we lhs_expr, just create a rhs expr and assignment statement.
          auto rhs_expr = new firrtl::FirrtlPB_Expression();
          add_refcon_as_expr(ln, rhs_asg, rhs_expr);

          auto fstmt2 = umod->add_statement();
          auto conn   = new firrtl::FirrtlPB_Statement_Connect();
          conn->set_allocated_location(lhs_expr);
          conn->set_allocated_expression(rhs_expr);
          fstmt2->set_allocated_connect(conn);
        } else {
          I(false);  // Should this be valid in the LNAST??
        }
      }
    }
    // mem_stmt->set_read_latency(read_lat);
    // mem_stmt->set_write_latency(write_lat);

    // Clear maps for this memory (since two mems can have same port names)
    pname_to_tup_map.clear();
    mem_to_ports_lists.clear();
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
    port->set_id(name.substr(1).to_s());
    // auto type = CreateTypeObject(ln.get_bitwidth(name.substr(1)));
    firrtl::FirrtlPB_Type *type;
    if (ln.is_in_bw_table(name.substr(1))) {
      type = CreateTypeObject(ln.get_bitwidth(name.substr(1)));
    } else {
      fmt::print("{}\n", name);
      I(!(name.substr(0, 1) == "$"));  // Inputs HAVE to have bw
      type = CreateTypeObject(0);
    }
    port->set_allocated_type(type);

    if (name.substr(0, 1) == "$")
      port->set_direction(firrtl::FirrtlPB_Port_Direction_PORT_DIRECTION_IN);
    else
      port->set_direction(firrtl::FirrtlPB_Port_Direction_PORT_DIRECTION_OUT);

    io_map[name.substr(1)] = port;

  } else if (name.substr(0, 1) == "#") {
    // # = register
    if (reg_wire_map.contains(name.substr(1)))
      return;
    auto reg = new firrtl::FirrtlPB_Statement_Register();
    reg->set_id(name.substr(1).to_s());

    // auto type = CreateTypeObject(ln.get_bitwidth(name.substr(1)));  // FIXME: Just setting bits to implicit right now
    firrtl::FirrtlPB_Type *type;
    if (ln.is_in_bw_table(name.substr(1))) {
      type = CreateTypeObject(ln.get_bitwidth(name.substr(1)));
    } else {
      type = CreateTypeObject(0);
    }
    reg->set_allocated_type(type);

    /* Specify register reset and init as UInt(0) as defaults.
     * Clock is given default value of "clock". */
    auto ref = new firrtl::FirrtlPB_Expression_Reference();
    ref->set_id("clock");
    auto clk_expr = new firrtl::FirrtlPB_Expression();
    clk_expr->set_allocated_reference(ref);
    reg->set_allocated_clock(clk_expr);
    reg->set_allocated_reset(CreateULitExpr(0));
    reg->set_allocated_init(CreateULitExpr(0));

    auto fstmt = umod->add_statement();
    fstmt->set_allocated_register_(reg);
    reg_wire_map[name.substr(1)] = fstmt;

  } else if (name.substr(0, 3) == "_._") {
    // _._ = wire
    auto new_name = name.substr(3).prepend('_');
    if (reg_wire_map.contains(new_name))
      return;
    auto wire = new firrtl::FirrtlPB_Statement_Wire();
    wire->set_id(new_name.to_s());

    // auto type = CreateTypeObject(ln.get_bitwidth(name));  // FIXME: Just setting bits to implicit right now
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
    // if (wire_rename_map.contains(name)) // Ignore this, since it's a call to a submodule and not a wire.
    //  return;
    auto wire = new firrtl::FirrtlPB_Statement_Wire();
    wire->set_id(name.to_s());  // FIXME: Figure out best way to use renaming map to fix submodule input tuple names

    // auto type = CreateTypeObject(ln.get_bitwidth(name));  // FIXME: Just setting bits to implicit right now
    firrtl::FirrtlPB_Type *type;
    if (ln.is_in_bw_table(name)) {
      type = CreateTypeObject(ln.get_bitwidth(name));
    } else {
      type = CreateTypeObject(0);
    }
    wire->set_allocated_type(type);

    auto fstmt = umod->add_statement();
    fstmt->set_allocated_wire(wire);
    reg_wire_map[name] = fstmt;
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

void Inou_firrtl::CreateSubmodInst(Lnast &ln, const Lnast_nid &fcall_node, firrtl::FirrtlPB_Module_UserModule *umod) {
  auto func_out = ln.get_first_child(fcall_node);
  auto mod_name = ln.get_sibling_next(func_out);
  auto func_inp = ln.get_sibling_next(mod_name);

  // 1. Converge func_out name with func_inp name
  // 2. Create submodule instance with this name
  auto inst        = new firrtl::FirrtlPB_Statement_Instance();
  auto submod_name = ConvergeFCallName(ln.get_name(func_out), ln.get_name(func_inp));
  inst->set_id(submod_name.to_s());
  inst->set_module_id(ln.get_name(mod_name).to_s());

  auto fstmt = umod->add_statement();
  fstmt->set_allocated_instance(inst);
}

/* Function calls in LNAST have a name for the input tuple and
 * a name for the output tuple. FIRRTL only allows for one name.
 * Thus, we must specify what name we will use. In general, take
 * output tuple name and make that the standard (means changing
 * input tuple names to match output tuple when seen in LNAST). */
mmap_lib::str Inou_firrtl::ConvergeFCallName(const mmap_lib::str &func_out, const mmap_lib::str &func_inp) {
  if (func_inp.substr(0, 4) == "inp_" && func_out.substr(0, 4) == "out_" && func_inp.substr(4) == func_out.substr(4)) {
    // Specific case from FIRRTL->LNAST translation.
    // some function call like out_foo = submodule(inp_foo) will get its bundle name set to foo
    wire_rename_map[func_inp] = func_inp.substr(4);
    wire_rename_map[func_out] = func_out.substr(4);
    return func_out.substr(4);
    ;
  } else {
    // Change the map (which alters some wire names)
    wire_rename_map[func_inp] = func_out;
    return func_out;
  }
}

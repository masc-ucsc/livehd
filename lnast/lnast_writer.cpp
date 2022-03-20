//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "lnast_writer.hpp"

#include "lnast_ntype.hpp"

#define WRITE_LNAST_NODE(type)                                                                                  \
  case Lnast_ntype::Lnast_ntype_##type: {                                                                       \
    write_##type();                                                                                             \
    break;                                                                                                      \
  }

Lnast_writer::Lnast_writer(std::ostream &_os, std::shared_ptr<Lnast> _lnast) : os(_os), lnast(_lnast) {
  has_file_output = !(_os.rdbuf() == std::cout.rdbuf());
}

void Lnast_writer::write_all() {
  depth = 0;
  write_metadata();
  current_nid = Lnast_nid::root();
  write_lnast();
}

void Lnast_writer::write_metadata() {
  print_line("/*\n");
  depth += 1;
  print_line(fmt::fg(fmt::color::green), ":name: {}\n", lnast->get_top_module_name());
  depth -= 1;
  print_line("*/\n");
}

void Lnast_writer::write_lnast() {
  I(!is_invalid());
  switch (get_raw_ntype()) {
    WRITE_LNAST_NODE(invalid)
    WRITE_LNAST_NODE(top)
    WRITE_LNAST_NODE(stmts)
    WRITE_LNAST_NODE(if)
    WRITE_LNAST_NODE(uif)
    WRITE_LNAST_NODE(for)
    WRITE_LNAST_NODE(while)
    WRITE_LNAST_NODE(func_call)
    WRITE_LNAST_NODE(func_def)
    WRITE_LNAST_NODE(assign)
    WRITE_LNAST_NODE(dp_assign)
    WRITE_LNAST_NODE(mut)
    WRITE_LNAST_NODE(bit_and)
    WRITE_LNAST_NODE(bit_or)
    WRITE_LNAST_NODE(bit_not)
    WRITE_LNAST_NODE(bit_xor)
    WRITE_LNAST_NODE(reduce_or)
    WRITE_LNAST_NODE(logical_and)
    WRITE_LNAST_NODE(logical_or)
    WRITE_LNAST_NODE(logical_not)
    WRITE_LNAST_NODE(plus)
    WRITE_LNAST_NODE(minus)
    WRITE_LNAST_NODE(mult)
    WRITE_LNAST_NODE(div)
    WRITE_LNAST_NODE(mod)
    WRITE_LNAST_NODE(shl)
    WRITE_LNAST_NODE(sra)
    WRITE_LNAST_NODE(sext)
    WRITE_LNAST_NODE(set_mask)
    WRITE_LNAST_NODE(get_mask)
    WRITE_LNAST_NODE(mask_and)
    WRITE_LNAST_NODE(mask_popcount)
    WRITE_LNAST_NODE(mask_xor)
    WRITE_LNAST_NODE(is)
    WRITE_LNAST_NODE(ne)
    WRITE_LNAST_NODE(eq)
    WRITE_LNAST_NODE(lt)
    WRITE_LNAST_NODE(le)
    WRITE_LNAST_NODE(gt)
    WRITE_LNAST_NODE(ge)
    WRITE_LNAST_NODE(ref)
    WRITE_LNAST_NODE(const)
    WRITE_LNAST_NODE(range)
    WRITE_LNAST_NODE(type_def)
    WRITE_LNAST_NODE(type_spec)
    WRITE_LNAST_NODE(none_type)
    WRITE_LNAST_NODE(prim_type_uint)
    WRITE_LNAST_NODE(prim_type_sint)
    WRITE_LNAST_NODE(prim_type_range)
    WRITE_LNAST_NODE(prim_type_string)
    WRITE_LNAST_NODE(prim_type_boolean)
    WRITE_LNAST_NODE(prim_type_type)
    WRITE_LNAST_NODE(prim_type_ref)
    WRITE_LNAST_NODE(comp_type_tuple)
    WRITE_LNAST_NODE(comp_type_array)
    WRITE_LNAST_NODE(comp_type_mixin)
    WRITE_LNAST_NODE(comp_type_lambda)
    WRITE_LNAST_NODE(expr_type)
    WRITE_LNAST_NODE(unknown_type)
    WRITE_LNAST_NODE(tuple_concat)
    WRITE_LNAST_NODE(tuple_add)
    WRITE_LNAST_NODE(tuple_get)
    WRITE_LNAST_NODE(tuple_set)
    WRITE_LNAST_NODE(attr_set)
    WRITE_LNAST_NODE(attr_get)
    WRITE_LNAST_NODE(err_flag)
    WRITE_LNAST_NODE(phi)
    WRITE_LNAST_NODE(hot_phi)
    WRITE_LNAST_NODE(last_invalid)
  }
}

void Lnast_writer::write_invalid() {
  print_line(fmt::fg(fmt::color::red), "invalid\n");
}

void Lnast_writer::write_top() {
  print_line("{{\n");
  ++depth;
  move_to_child();
  if (!is_invalid()) {
    write_lnast();
  }
  --depth;
  print_line("}}\n");
  
}

void Lnast_writer::write_stmts() {
  if (!move_to_child()) return;
  do {
    write_lnast();
  } while (move_to_sibling());
}

void Lnast_writer::write_if() {
  print_line(fmt::fg(fmt::color::purple) | fmt::emphasis::bold, "if");
  print(" (");
  move_to_child();
  write_lnast();
  print(") {{\n");
  ++depth;
  move_to_sibling();
  write_lnast();
  --depth;
  print_line("}}\n");
  move_to_parent();
}

void Lnast_writer::write_uif() { }
void Lnast_writer::write_for() { }
void Lnast_writer::write_while() { }

void Lnast_writer::write_func_call() {
  print_line("@");
  move_to_child();
  write_lnast();
  print("(");
  move_to_sibling();
  write_lnast();
  print(")\n");
  move_to_parent();
}

void Lnast_writer::write_func_def() {
  print_line("");
  move_to_child();
  write_lnast();
  print(" = {\n");
  ++depth;
  move_to_sibling();
  write_lnast();
  --depth;
  print_line("}\n");
  move_to_parent();
}

void Lnast_writer::write_assign() {
  print_line("");
  move_to_child();
  write_lnast();
  print(" = ");
  move_to_sibling();
  write_lnast();
  print("\n");
  move_to_parent();
}

void Lnast_writer::write_dp_assign() { }
void Lnast_writer::write_mut() { }

void Lnast_writer::write_binary(std::string_view op) {
  print_line("");
  move_to_child();
  write_lnast();
  print(" = ");
  print(fmt::fg(fmt::color::purple) | fmt::emphasis::bold, op);
  print("(");
  move_to_sibling();
  write_lnast();
  print(",");
  move_to_sibling();
  write_lnast();
  print(")\n");
  move_to_parent();
}

void Lnast_writer::write_unary(std::string_view op) {
  print_line("");
  move_to_child();
  write_lnast();
  print(" = ");
  print(fmt::fg(fmt::color::purple) | fmt::emphasis::bold, op);
  print("(");
  move_to_sibling();
  write_lnast();
  print(")\n");
  move_to_parent();
}

void Lnast_writer::write_bit_and()     { write_binary("bit_add");  }
void Lnast_writer::write_bit_or()      { write_binary("bit_or");   }
void Lnast_writer::write_bit_not()     { write_unary("bit_not");   }
void Lnast_writer::write_bit_xor()     { write_binary("bit_xor");  }
void Lnast_writer::write_reduce_or()   { write_unary("reduce_or"); }
void Lnast_writer::write_logical_and() { write_binary("and");      }
void Lnast_writer::write_logical_or()  { write_binary("or");       }
void Lnast_writer::write_logical_not() { write_unary("not ");      }
void Lnast_writer::write_plus()        { write_binary("add");      }
void Lnast_writer::write_minus()       { write_binary("sub");      }
void Lnast_writer::write_mult()        { write_binary("mul");      }
void Lnast_writer::write_div()         { write_binary("div");      }
void Lnast_writer::write_mod()         { write_binary("mod");      }
void Lnast_writer::write_shl()         { write_binary("shl");      }
void Lnast_writer::write_sra()         { write_binary("sra");      }
void Lnast_writer::write_is()          { write_binary("is");       }
void Lnast_writer::write_ne()          { write_binary("ne");       }
void Lnast_writer::write_eq()          { write_binary("eq");       }
void Lnast_writer::write_lt()          { write_binary("<");        }
void Lnast_writer::write_le()          { write_binary("lt");       }
void Lnast_writer::write_gt()          { write_binary("gt");       }
void Lnast_writer::write_ge()          { write_binary("ge");       }

void Lnast_writer::write_sext() { }
void Lnast_writer::write_set_mask() { }
void Lnast_writer::write_get_mask() { }
void Lnast_writer::write_mask_and() { }
void Lnast_writer::write_mask_popcount() { }
void Lnast_writer::write_mask_xor() { }

void Lnast_writer::write_ref() {
  print("%{{{}}}", current_text());
}

void Lnast_writer::write_const() {
  print(fmt::fg(fmt::color::light_blue), "{}", current_text());
}

void Lnast_writer::write_range() {
  print_line("");
  move_to_child();
  write_lnast();
  print(" = ");
  move_to_sibling();
  write_lnast();
  print(" ..< ");
  move_to_sibling();
  write_lnast();
  print("\n");
  move_to_parent();
}

void Lnast_writer::write_type_def() {
  print_line("");
  move_to_child();
  write_lnast();
  print(" = ");
  move_to_sibling();
  write_lnast();
  print("\n");
  move_to_parent();
}

void Lnast_writer::write_type_spec() {
  print_line("");
  move_to_child();
  write_lnast();
  print(" : ");
  move_to_sibling();
  write_lnast();
  print("\n");
  move_to_parent();
}

void Lnast_writer::write_none_type() { print("#none"); }

void Lnast_writer::write_prim_type_int(char sign) {
  print(fmt::fg(fmt::color::gold), "#{}", sign);
  move_to_child();
  if (is_invalid()) {
    print(fmt::fg(fmt::color::gold), "int");
  } else if (is_last_child()) {
    print(fmt::fg(fmt::color::gold), "(");
    write_lnast();
    print(fmt::fg(fmt::color::gold), ")");
  } else {
    print(fmt::fg(fmt::color::gold), "(");
    write_lnast();
    print(",");
    move_to_sibling();
    write_lnast();
    print(fmt::fg(fmt::color::gold), ")");
  }
  move_to_parent();
}

void Lnast_writer::write_prim_type_uint() { write_prim_type_int('u'); } 
void Lnast_writer::write_prim_type_sint() { write_prim_type_int('s'); } 

void Lnast_writer::write_prim_type_range()   { print(fmt::fg(fmt::color::gold), "#range"); }
void Lnast_writer::write_prim_type_string()  { print(fmt::fg(fmt::color::gold), "#string"); }
void Lnast_writer::write_prim_type_boolean() { print(fmt::fg(fmt::color::gold), "#boolean"); }
void Lnast_writer::write_prim_type_type()    { print(fmt::fg(fmt::color::gold), "#type"); }
void Lnast_writer::write_prim_type_ref()     { print(fmt::fg(fmt::color::gold), "#ref"); }

void Lnast_writer::write_comp_type_tuple() {
  print(fmt::fg(fmt::color::gold), "#tuple(");
  move_to_child();
  ++depth;
  while (!is_invalid()) {
    if (get_ntype().is_type()) {
      print_line("");
    }
    write_lnast();
    print("\n");
    move_to_sibling();
  }
  --depth;
  print_line(fmt::fg(fmt::color::gold), ")");
  move_to_parent();
}

void Lnast_writer::write_comp_type_array() {
  print(fmt::fg(fmt::color::gold), "#array(");
  move_to_child();
  write_lnast();
  print(",");
  move_to_sibling();
  write_lnast();
  print(fmt::fg(fmt::color::gold), ")");
  move_to_parent();
}

void Lnast_writer::write_comp_type_mixin() {
  print(fmt::fg(fmt::color::gold), "#mixin(");
  move_to_child();
  ++depth;
  while (!is_invalid()) {
    print_line("");
    write_lnast();
    print("\n");
    move_to_sibling();
  }
  --depth;
  print_line(fmt::fg(fmt::color::gold), ")");
  move_to_parent();
}

void Lnast_writer::write_comp_type_lambda() {
  print(fmt::fg(fmt::color::gold), "#lambda(");
  move_to_child();
  write_lnast();
  print(",");
  move_to_sibling();
  write_lnast();
  print(fmt::fg(fmt::color::gold), ")");
  move_to_parent();
}

void Lnast_writer::write_expr_type() {
  print(fmt::fg(fmt::color::gold), "#expr(");
  move_to_child();
  write_lnast();
  print(fmt::fg(fmt::color::gold), ")");
  move_to_parent();
}

void Lnast_writer::write_unknown_type() {
  print(fmt::fg(fmt::color::gold), "#unknown");
}

void Lnast_writer::write_tuple_concat() { }

void Lnast_writer::write_tuple_add() {
  print_line("");
  move_to_child();
  write_lnast();
  print(" = (\n");
  ++depth;
  while (move_to_sibling()) {
    auto ntype = get_raw_ntype();
    if (ntype == Lnast_ntype::Lnast_ntype_ref || ntype == Lnast_ntype::Lnast_ntype_const) {
      print_line("");
      write_lnast();
      print("\n");
    } else {
      write_lnast();
    }
  }
  --depth;
  print_line(")\n");
  move_to_parent();
}

void Lnast_writer::write_tuple_get() {
  print_line("");
  move_to_child();
  write_lnast();
  print(" = ");
  move_to_sibling();
  write_lnast();
  while (move_to_sibling()) {
    if (get_raw_ntype() == Lnast_ntype::Lnast_ntype_const) {
      print(".");
      write_lnast();
    } else {
      print("[");
      write_lnast();
      print("]");
    }
  }
  print("\n");
  move_to_parent();
}

void Lnast_writer::write_tuple_set() {
  print_line("");
  move_to_child();
  write_lnast();
  while (move_to_sibling() && !is_last_child()) {
    if (get_raw_ntype() == Lnast_ntype::Lnast_ntype_const) {
      print(".");
      write_lnast();
    } else {
      print("[");
      write_lnast();
      print("]");
    }
  }
  print(" = ");
  write_lnast();
  print("\n");
  move_to_parent();

}

void Lnast_writer::write_attr_set() {}

void Lnast_writer::write_attr_get() { }

void Lnast_writer::write_err_flag() { }
void Lnast_writer::write_phi() { }
void Lnast_writer::write_hot_phi() { }
void Lnast_writer::write_last_invalid() { }


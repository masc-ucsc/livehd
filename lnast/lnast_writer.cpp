//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "lnast_writer.hpp"

#include "lnast_ntype.hpp"

Lnast_writer::Lnast_writer(std::ostream &_os, std::shared_ptr<Lnast> _lnast) : os(_os), lnast(_lnast) {
  has_file_output = !(_os.rdbuf() == std::cout.rdbuf());
}

void Lnast_writer::write_all() {
  depth = 0;
  is_func_name = false;
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
#define LNAST_NODE(NAME, VERBAL)            \
    case Lnast_ntype::Lnast_ntype_##NAME: { \
      write_##NAME();                       \
      break;                                \
    }
#include "lnast_nodes.def"
    default:
      break;
  }
}

void Lnast_writer::write_invalid() {
  print(fmt::fg(fmt::color::red), "invalid");
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
    print_line("");
    write_lnast();
    print("\n");
  } while (move_to_sibling());
}

void Lnast_writer::write_if() {
  print(fmt::fg(fmt::color::purple) | fmt::emphasis::bold, "if");
  print(" (");
  move_to_child();
  write_lnast();
  print(") {{\n");
  ++depth;
  move_to_sibling();
  write_lnast();
  --depth;
  print_line("}}");
  move_to_parent();
}

void Lnast_writer::write_uif() { }
void Lnast_writer::write_for() { }
void Lnast_writer::write_while() { }

void Lnast_writer::write_func_call() {
  move_to_child();
  write_lnast();
  print(" = ");
  move_to_sibling();
  is_func_name = true;
  write_lnast();
  is_func_name = false;
  print("(");
  move_to_sibling();
  write_lnast();
  print(")");
  move_to_parent();
}

void Lnast_writer::write_func_def() {
  move_to_child();
  is_func_name = true;
  write_lnast();
  is_func_name = false;
  print(" = {\n");
  ++depth;
  move_to_sibling();
  write_lnast();
  --depth;
  print_line("}");
  move_to_parent();
}

void Lnast_writer::write_assign() {
  move_to_child();
  write_lnast();
  print(" = ");
  move_to_sibling();
  write_lnast();
  if (!is_last_child()) {
    move_to_sibling();
    print(" : ");
    write_lnast();
  }
  move_to_parent();
}

void Lnast_writer::write_dp_assign() { }
void Lnast_writer::write_mut() { }

void Lnast_writer::write_n_ary(std::string_view op) {
  move_to_child();
  write_lnast();
  print(" = ");
  print(fmt::fg(fmt::color::purple) | fmt::emphasis::bold, op);
  print("(");
  while (move_to_sibling()) {
    write_lnast();
    if (!is_last_child()) print(", ");
  }
  print(")");
  move_to_parent();
}

void Lnast_writer::write_bit_and()     { write_n_ary("bit_and");  }
void Lnast_writer::write_bit_or()      { write_n_ary("bit_or");   }
void Lnast_writer::write_bit_not()     { write_n_ary("bit_not");  }
void Lnast_writer::write_bit_xor()     { write_n_ary("bit_xor");  }
void Lnast_writer::write_reduce_or()   { write_n_ary("ror");      }
void Lnast_writer::write_logical_and() { write_n_ary("and");      }
void Lnast_writer::write_logical_or()  { write_n_ary("or");       }
void Lnast_writer::write_logical_not() { write_n_ary("not");      }
void Lnast_writer::write_plus()        { write_n_ary("add");      }
void Lnast_writer::write_minus()       { write_n_ary("sub");      }
void Lnast_writer::write_mult()        { write_n_ary("mul");      }
void Lnast_writer::write_div()         { write_n_ary("div");      }
void Lnast_writer::write_mod()         { write_n_ary("mod");      }
void Lnast_writer::write_shl()         { write_n_ary("shl");      }
void Lnast_writer::write_sra()         { write_n_ary("sra");      }
void Lnast_writer::write_is()          { write_n_ary("is");       }
void Lnast_writer::write_ne()          { write_n_ary("ne");       }
void Lnast_writer::write_eq()          { write_n_ary("eq");       }
void Lnast_writer::write_lt()          { write_n_ary("lt");       }
void Lnast_writer::write_le()          { write_n_ary("le");       }
void Lnast_writer::write_gt()          { write_n_ary("gt");       }
void Lnast_writer::write_ge()          { write_n_ary("ge");       }

void Lnast_writer::write_sext() { }
void Lnast_writer::write_set_mask() { }
void Lnast_writer::write_get_mask() { }
void Lnast_writer::write_mask_and() { }
void Lnast_writer::write_mask_popcount() { }
void Lnast_writer::write_mask_xor() { }

void Lnast_writer::write_ref() {
  if (is_func_name) {
    print("@{{{}}}", current_text());
  } else {
    print("%{{{}}}", current_text());
  }
}

void Lnast_writer::write_const() {
  print(fmt::fg(fmt::color::light_blue), "{}", current_text());
}

void Lnast_writer::write_range() { write_n_ary("range"); }

void Lnast_writer::write_type_def() {
  move_to_child();
  write_lnast();
  print(" = ");
  move_to_sibling();
  write_lnast();
  move_to_parent();
}

void Lnast_writer::write_type_spec() {
  move_to_child();
  write_lnast();
  print(" : ");
  move_to_sibling();
  write_lnast();
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
    print(", ");
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
    write_lnast();
    if (!is_last_child()) print(", ");
    move_to_sibling();
  }
  --depth;
  print(fmt::fg(fmt::color::gold), ")");
  move_to_parent();
}

void Lnast_writer::write_comp_type_array() {
  print(fmt::fg(fmt::color::gold), "#array(");
  move_to_child();
  write_lnast();
  print(", ");
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
    write_lnast();
    if (!is_last_child()) print(", ");
    move_to_sibling();
  }
  --depth;
  print(fmt::fg(fmt::color::gold), ")");
  move_to_parent();
}

void Lnast_writer::write_comp_type_lambda() {
  print(fmt::fg(fmt::color::gold), "#lambda(");
  move_to_child();
  write_lnast();
  print(", ");
  move_to_sibling();
  write_lnast();
  print(fmt::fg(fmt::color::gold), ")");
  move_to_parent();
}

void Lnast_writer::write_comp_type_enum() {
  // TODO: Implement this function
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
  move_to_child();
  write_lnast();
  print(" = (");
  ++depth;
  while (move_to_sibling()) {
    write_lnast();
    if (!is_last_child()) print(", ");
  }
  --depth;
  print(")");
  move_to_parent();
}

void Lnast_writer::write_tuple_get() {
  move_to_child();
  write_lnast();
  print(" = ");
  move_to_sibling();
  write_lnast();
  while (move_to_sibling()) {
    print("[");
    write_lnast();
    print("]");
  }
  move_to_parent();
}

void Lnast_writer::write_tuple_set() {
  move_to_child();
  write_lnast();
  while (move_to_sibling() && !is_last_child()) {
    print("[");
    write_lnast();
    print("]");
  }
  print(" = ");
  write_lnast();
  move_to_parent();
}

void Lnast_writer::write_attr_set() {}

void Lnast_writer::write_attr_get() { }

void Lnast_writer::write_err_flag() { }
void Lnast_writer::write_phi() { }
void Lnast_writer::write_hot_phi() { }


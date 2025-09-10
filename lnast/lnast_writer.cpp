//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "lnast_writer.hpp"

#include "lnast_ntype.hpp"

Lnast_writer::Lnast_writer(std::ostream& _os, const std::shared_ptr<Lnast>& _lnast) : os(_os), lnast(_lnast) {
  has_file_output = !(_os.rdbuf() == std::cout.rdbuf());
}

void Lnast_writer::write_all() {
  depth        = 0;
  is_func_name = false;
  write_metadata();
  nid_stack   = {};
  current_nid = Lnast_nid::root();
  write_lnast();
}

void Lnast_writer::write_nid(const Lnast_nid& nid) {
  depth        = 0;
  is_func_name = false;
  nid_stack    = {};
  current_nid  = nid;
  write_lnast();
}

void Lnast_writer::write_metadata() {
  print_line("/*\n");
  depth += 1;
  print_line(":name: {}\n", lnast->get_top_module_name());
  depth -= 1;
  print_line("*/\n");
}

void Lnast_writer::write_lnast() {
  I(!is_invalid());
  switch (get_raw_ntype()) {
#define LNAST_NODE(NAME, VERBAL)          \
  case Lnast_ntype::Lnast_ntype_##NAME: { \
    write_##NAME();                       \
    break;                                \
  }
#include "lnast_nodes.def"
    default: break;
  }
}

void Lnast_writer::write_invalid() { print("invalid"); }

void Lnast_writer::write_top() {
  print_line("{\n");
  ++depth;
  move_to_child();
  if (!is_invalid()) {
    write_lnast();
  }
  --depth;
  print_line("}\n");
}

void Lnast_writer::write_stmts() {
  if (move_to_child()) {
    do {
      print_line("");
      write_lnast();
      print("\n");
    } while (move_to_sibling());
  }
  move_to_parent();
}

void Lnast_writer::write_if() {
  print("if");
  print(" (");
  move_to_child();
  write_lnast();
  print(") {\n");
  ++depth;
  move_to_sibling();
  write_lnast();
  --depth;
  print_line("}");
  if (move_to_sibling()) {
    if (is_last_child()) {
      print(" ");
      print("else");
      print(" {\n");
      ++depth;
      write_lnast();
      --depth;
      print_line("}");
    } else {
      while (!is_last_child()) {
        print(" ");
        print("elif");
        print(" (");
        write_lnast();
        move_to_sibling();
        print(") {\n");
        ++depth;
        write_lnast();
        --depth;
        print_line("}");
        if (!move_to_sibling()) {
          break;
        }
        if (is_last_child()) {
          print(" ");
          print("else");
          print(" {\n");
          ++depth;
          write_lnast();
          --depth;
          print_line("}");
        }
      }
    }
  }
  move_to_parent();
}

void Lnast_writer::write_uif() {}
void Lnast_writer::write_for() {}
void Lnast_writer::write_while() {}

void Lnast_writer::write_func_call() {
  move_to_child();
  write_lnast();
  print(" = ");
  move_to_sibling();
  is_func_name = true;
  write_lnast();
  is_func_name = false;
  print("(");
  while (move_to_sibling()) {
    write_lnast();
    if (!is_last_child()) {
      print(", ");
    }
  }
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

void Lnast_writer::write_dp_assign() {}
void Lnast_writer::write_mut() {}

void Lnast_writer::write_n_ary(std::string_view op) {
  move_to_child();
  write_lnast();
  print(" = ");
  print(op);
  print("(");
  while (move_to_sibling()) {
    write_lnast();
    if (!is_last_child()) {
      print(", ");
    }
  }
  print(")");
  move_to_parent();
}

void Lnast_writer::write_bit_and() { write_n_ary("bit_and"); }
void Lnast_writer::write_bit_or() { write_n_ary("bit_or"); }
void Lnast_writer::write_bit_not() { write_n_ary("bit_not"); }
void Lnast_writer::write_bit_xor() { write_n_ary("bit_xor"); }
void Lnast_writer::write_log_and() { write_n_ary("log_and"); }
void Lnast_writer::write_log_or() { write_n_ary("log_or"); }
void Lnast_writer::write_log_not() { write_n_ary("log_not"); }
void Lnast_writer::write_plus() { write_n_ary("add"); }
void Lnast_writer::write_minus() { write_n_ary("sub"); }
void Lnast_writer::write_mult() { write_n_ary("mul"); }
void Lnast_writer::write_div() { write_n_ary("div"); }
void Lnast_writer::write_mod() { write_n_ary("mod"); }
void Lnast_writer::write_shl() { write_n_ary("shl"); }
void Lnast_writer::write_sra() { write_n_ary("sra"); }
void Lnast_writer::write_is() { write_n_ary("is"); }
void Lnast_writer::write_ne() { write_n_ary("ne"); }
void Lnast_writer::write_eq() { write_n_ary("eq"); }
void Lnast_writer::write_lt() { write_n_ary("lt"); }
void Lnast_writer::write_le() { write_n_ary("le"); }
void Lnast_writer::write_gt() { write_n_ary("gt"); }
void Lnast_writer::write_ge() { write_n_ary("ge"); }
void Lnast_writer::write_red_or() { write_n_ary("red_or"); }
void Lnast_writer::write_red_and() { write_n_ary("red_and"); }
void Lnast_writer::write_red_xor() { write_n_ary("red_xor"); }
void Lnast_writer::write_popcount() { write_n_ary("popcount"); }
void Lnast_writer::write_sext() { write_n_ary("sext"); }
void Lnast_writer::write_set_mask() { write_n_ary("set_mask"); }
void Lnast_writer::write_get_mask() { write_n_ary("get_mask"); }
void Lnast_writer::write_mask_and() { write_n_ary("mask_and"); }
void Lnast_writer::write_mask_popcount() { write_n_ary("mask_popcount"); }
void Lnast_writer::write_mask_xor() { write_n_ary("mask_xor"); }

void Lnast_writer::write_ref() {
  if (is_func_name) {
    os << std::format("!{{{}}}", current_text());
  } else {
    os << std::format("%{{{}}}", current_text());
  }
}

void Lnast_writer::write_const() {
  if (current_text().size() != 0 && isdigit(current_text()[0])) {
    print("{}", current_text());
  } else {
    print("\"{}\"", current_text());
  }
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
  print("#{}", sign);
  move_to_child();
  if (is_invalid()) {
    print("int");
  } else if (is_last_child()) {
    print("(");
    write_lnast();
    print(")");
  } else {
    print("(");
    write_lnast();
    print(", ");
    move_to_sibling();
    write_lnast();
    print(")");
  }
  move_to_parent();
}

void Lnast_writer::write_prim_type_uint() { write_prim_type_int('u'); }
void Lnast_writer::write_prim_type_sint() { write_prim_type_int('s'); }

void Lnast_writer::write_prim_type_range() { print("#range"); }
void Lnast_writer::write_prim_type_string() { print("#string"); }
void Lnast_writer::write_prim_type_boolean() { print("#boolean"); }
void Lnast_writer::write_prim_type_type() { print("#type"); }
void Lnast_writer::write_prim_type_ref() { print("#ref"); }

void Lnast_writer::write_comp_type_tuple() {
  print("#tuple(");
  move_to_child();
  ++depth;
  while (!is_invalid()) {
    write_lnast();
    if (!is_last_child()) {
      print(", ");
    }
    move_to_sibling();
  }
  --depth;
  print(")");
  move_to_parent();
}

void Lnast_writer::write_comp_type_array() {
  print("#array(");
  move_to_child();
  write_lnast();
  print(", ");
  move_to_sibling();
  write_lnast();
  print(")");
  move_to_parent();
}

void Lnast_writer::write_comp_type_mixin() {
  print("#mixin(");
  move_to_child();
  ++depth;
  while (!is_invalid()) {
    write_lnast();
    if (!is_last_child()) {
      print(", ");
    }
    move_to_sibling();
  }
  --depth;
  print(")");
  move_to_parent();
}

void Lnast_writer::write_comp_type_lambda() {
  print("#lambda(");
  move_to_child();
  write_lnast();
  print(", ");
  move_to_sibling();
  write_lnast();
  print(")");
  move_to_parent();
}

void Lnast_writer::write_comp_type_enum() {
  // TODO: Implement this function
}

void Lnast_writer::write_expr_type() {
  print("#expr(");
  move_to_child();
  write_lnast();
  print(")");
  move_to_parent();
}

void Lnast_writer::write_unknown_type() { print("#unknown"); }

void Lnast_writer::write_tuple_concat() {}

void Lnast_writer::write_tuple_add() {
  move_to_child();
  write_lnast();
  print(" = (");
  ++depth;
  while (move_to_sibling()) {
    write_lnast();
    if (!is_last_child()) {
      print(", ");
    }
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

void Lnast_writer::write_attr_set() {
  move_to_child();
  write_lnast();
  while (move_to_sibling()) {
    if (!is_last_child()) {
      print(".");
    } else {
      print(" = ");
    }
    write_lnast();
  }
  move_to_parent();
}

void Lnast_writer::write_attr_get() {
  move_to_child();
  write_lnast();
  print(" = ");
  move_to_sibling();
  write_lnast();
  while (move_to_sibling()) {
    print(".");
    write_lnast();
  }
  move_to_parent();
}

void Lnast_writer::write_cassert() {
  move_to_child();
  print("cassert(");
  write_lnast();
  print(")");
  move_to_parent();
}

void Lnast_writer::write_err_flag() {}
void Lnast_writer::write_phi() {}
void Lnast_writer::write_hot_phi() {}

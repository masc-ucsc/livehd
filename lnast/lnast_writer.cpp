//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "lnast_writer.hpp"

#include "lnast_ntype.hpp"

#define WRITE_LNAST_NODE(type)                                                                                  \
  case Lnast_ntype::Lnast_ntype_##type: {                                                                       \
    write_##type();                                                                                             \
    break;                                                                                                      \
  }

Lnast_writer::Lnast_writer(std::ofstream &_os, std::shared_ptr<Lnast> _lnast) : os(_os), lnast(_lnast) {
  has_file_output = true;
}

Lnast_writer::Lnast_writer(std::ostream &_os, std::shared_ptr<Lnast> _lnast) : os(_os), lnast(_lnast) {
  has_file_output = false;
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
  switch (get_ntype()) {
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
}

void Lnast_writer::write_top() {
  print_line("{{\n");
  ++depth;
  move_to_child();
  write_lnast();
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
  print_line("if (");
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

void Lnast_writer::write_func_call() { }  
void Lnast_writer::write_func_def() { }   

void Lnast_writer::write_assign() {
  print_line("");
  move_to_child();
  write_lnast();
  print(" = ");
  move_to_sibling();
  write_lnast();
  move_to_parent();
  print("\n");
}

void Lnast_writer::write_dp_assign() { }  
void Lnast_writer::write_mut() { }        

void Lnast_writer::write_binary(std::string_view op) {
  print_line("");
  move_to_child();
  write_lnast();
  print(" = ");
  move_to_sibling();
  write_lnast();
  print(" {} ", op);
  move_to_sibling();
  write_lnast();
  print("\n");
  move_to_parent();
}

void Lnast_writer::write_unary(std::string_view op) {
  print_line("");
  move_to_child();
  write_lnast();
  print(" = {} ", op);
  move_to_sibling();
  write_lnast();
  print("\n");
  move_to_parent();
}

void Lnast_writer::write_bit_and()     { write_binary("&");   }  
void Lnast_writer::write_bit_or()      { write_binary("|");   }   
void Lnast_writer::write_bit_not()     { write_unary("!");    }  
void Lnast_writer::write_bit_xor()     { write_binary("^");   }  
void Lnast_writer::write_reduce_or()   { write_unary("|");   }  
void Lnast_writer::write_logical_and() { write_binary("and"); }  
void Lnast_writer::write_logical_or()  { write_binary("or");  }   
void Lnast_writer::write_logical_not() { write_unary("not "); }  
void Lnast_writer::write_plus()        { write_binary("+");   }
void Lnast_writer::write_minus()       { write_binary("-");   }
void Lnast_writer::write_mult()        { write_binary("*");   }
void Lnast_writer::write_div()         { write_binary("/");   }
void Lnast_writer::write_mod()         { write_binary("%");   }
void Lnast_writer::write_shl()         { write_binary(">>");  }  
void Lnast_writer::write_sra()         { write_binary("<<");  }  
void Lnast_writer::write_is()          { write_binary("is");  }
void Lnast_writer::write_ne()          { write_binary("!=");  }
void Lnast_writer::write_eq()          { write_binary("==");  }
void Lnast_writer::write_lt()          { write_binary("<");   }
void Lnast_writer::write_le()          { write_binary("<=");  }
void Lnast_writer::write_gt()          { write_binary(">");   }
void Lnast_writer::write_ge()          { write_binary(">=");  }

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
  print("{}", current_text());
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

void Lnast_writer::write_tuple_concat() { }

void Lnast_writer::write_tuple_add() {
  print_line("");
  move_to_child();
  write_lnast();
  print(" = (\n");
  ++depth;
  while (move_to_sibling()) {
    auto ntype = get_ntype();
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
    if (get_ntype() == Lnast_ntype::Lnast_ntype_const) {
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
    if (get_ntype() == Lnast_ntype::Lnast_ntype_const) {
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


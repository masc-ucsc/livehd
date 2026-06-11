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
  current_nid = lnast->get_root();
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
  if (move_to_child()) {
    do {
      write_lnast();
    } while (move_to_sibling());
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

void Lnast_writer::write_if() { write_if_chain("if"); }

// unique_if: same child shape as if; the .ln keyword is `uif`.
void Lnast_writer::write_unique_if() { write_if_chain("uif"); }

void Lnast_writer::write_if_chain(std::string_view keyword) {
  print(keyword);
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

void Lnast_writer::write_io() { write_n_ary("io"); }

// Pseudo-function markers: emit `dst = name(args…)` so the textual form
// matches the legacy `func_call(dst, const(name), …)` shape.
void Lnast_writer::write_func_marker(std::string_view name) {
  move_to_child();
  write_lnast();  // dst
  print(" = ");
  print(name);
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

void Lnast_writer::write_func_does() { write_func_marker("does"); }
void Lnast_writer::write_func_equals() { write_func_marker("equals"); }
void Lnast_writer::write_func_in() { write_func_marker("in"); }
void Lnast_writer::write_func_has() { write_func_marker("has"); }
void Lnast_writer::write_func_case() { write_func_marker("case"); }
void Lnast_writer::write_func_break() { write_func_marker("break"); }
void Lnast_writer::write_func_continue() { write_func_marker("continue"); }
void Lnast_writer::write_func_return() { write_func_marker("return"); }

void Lnast_writer::write_dp_assign() {}

// store( var, level0..levelN, value ) — the unified write/bind node:
//   * 0 levels                → `var = value`        (old scalar assign)
//   * N levels                → `var[l0]…[lN] = value` (old tuple_set path)
//   * typed bind (exactly 3   → `name = value : type`  (old `assign` signature /
//     children, last a TYPE)    typed tuple-field payload `name=value:type`)
// The typed form keeps the value in the MIDDLE and the type LAST, so it cannot
// be distinguished from a field-path store by arity alone — detect it by a
// type-node last child.
void Lnast_writer::write_store() {
  // An io-entry store may carry a trailing `stages(min,max)`
  // annotation (identified by ntype). Exclude it from the value/type walk
  // and print it as a suffix.
  bool has_stages = false;
  bool typed      = false;
  {
    Lnast_nid last;
    size_t    n = 0;
    for (auto c = lnast->get_first_child(current_nid); !c.is_invalid(); c = lnast->get_sibling_next(c)) {
      last = c;
      ++n;
    }
    if (!last.is_invalid() && Lnast_ntype::is_stages(lnast->get_type(last))) {
      has_stages = true;
      --n;
    }
    if (n == 3) {
      // Third payload child a type node => typed bind `name = value : type`.
      auto c2 = lnast->get_sibling_next(lnast->get_sibling_next(lnast->get_first_child(current_nid)));
      typed   = Lnast_ntype::is_type(lnast->get_type(c2));
    }
  }
  auto is_payload_last = [&]() {
    if (is_last_child()) {
      return true;
    }
    auto next = lnast->get_sibling_next(current_nid);
    return has_stages && Lnast_ntype::is_stages(lnast->get_type(next));
  };
  move_to_child();
  write_lnast();  // var / name
  if (typed) {
    move_to_sibling();
    print(" = ");
    write_lnast();  // value
    move_to_sibling();
    print(" : ");
    write_lnast();  // type
  } else {
    while (move_to_sibling() && !is_payload_last()) {
      print("[");
      write_lnast();  // level (field key / index)
      print("]");
    }
    print(" = ");
    write_lnast();  // value (cursor already on the last payload child)
  }
  if (has_stages) {
    move_to_sibling();
    print(" ");
    write_lnast();  // stages(min, max)
  }
  move_to_parent();
}

// declare( var, type_decl, const(qualifier), [value] ).
void Lnast_writer::write_declare() {
  move_to_child();
  write_lnast();  // var
  print(" :: ");
  move_to_sibling();
  write_lnast();  // type_decl (#u(8) / #none / ...)
  print(" ");
  move_to_sibling();
  write_lnast();  // qualifier const ("mut" / "const" / "mut comptime" / ...)
  if (move_to_sibling()) {
    print(" = ");
    write_lnast();  // optional init value
  }
  move_to_parent();
}

void Lnast_writer::write_delay_assign() {
  move_to_child();
  write_lnast();
  print(" = delay_assign(");
  move_to_sibling();
  write_lnast();
  print(", ");
  move_to_sibling();
  write_lnast();
  print(")");
  move_to_parent();
}

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

// stages(min, max) pipeline annotation. Unlike the n-ary ops it has
// no destination child: both children are payload consts.
void Lnast_writer::write_stages() {
  print("stages(");
  move_to_child();
  write_lnast();  // min
  while (move_to_sibling()) {
    print(", ");
    write_lnast();  // max
  }
  print(")");
  move_to_parent();
}

// timecheck(ref, min, max): the `x@[N]` flop-free cycle-check
// record. A statement; children are the checked ref + two payload consts.
void Lnast_writer::write_timecheck() {
  print("timecheck(");
  move_to_child();
  write_lnast();  // ref
  while (move_to_sibling()) {
    print(", ");
    write_lnast();  // min, max
  }
  print(")");
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

void Lnast_writer::write_prim_type_none() { print("#none"); }

// The canonical integer type node (no-arg overload, invoked
// by the dispatch switch). Children are the optional (max, min) range consts;
// `#int` with none = unbounded, `#int(max)` = max only, `#int(max, min)` = both.
void Lnast_writer::write_prim_type_int() {
  print("#int");
  move_to_child();
  if (!is_invalid()) {
    if (is_last_child()) {
      print("(");
      write_lnast();
      print(")");
    } else {
      print("(");
      write_lnast();  // max
      print(", ");
      move_to_sibling();
      write_lnast();  // min
      print(")");
    }
  }
  move_to_parent();
}

void Lnast_writer::write_prim_type_range() { print("#range"); }
void Lnast_writer::write_prim_type_string() { print("#string"); }
void Lnast_writer::write_prim_type_bool() { print("#boolean"); }

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
  // Optional 2nd child: a comptime message string (cassert(cond, "msg")).
  if (move_to_sibling()) {
    print(", ");
    write_lnast();
  }
  print(")");
  move_to_parent();
}

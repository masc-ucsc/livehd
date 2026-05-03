//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "lnast_prp_writer.hpp"

#include <format>
#include <string>

// ── Constructor ───────────────────────────────────────────────────────────────

Lnast_prp_writer::Lnast_prp_writer(std::ostream& _os, std::shared_ptr<Lnast> _lnast) : os(_os), lnast(std::move(_lnast)) {}

// ── Public entry point ────────────────────────────────────────────────────────

void Lnast_prp_writer::write_all() {
  depth     = 0;
  nid_stack = {};
  cur       = lnast->get_root();
  write_node();
}

// ── Cursor helpers ────────────────────────────────────────────────────────────

bool Lnast_prp_writer::move_to_child() {
  auto child = lnast->get_child(cur);
  if (child.is_invalid()) {
    return false;
  }
  nid_stack.push(cur);
  cur = child;
  return true;
}

bool Lnast_prp_writer::move_to_sibling() {
  auto sib = lnast->get_sibling_next(cur);
  if (sib.is_invalid()) {
    return false;
  }
  cur = sib;
  return true;
}

void Lnast_prp_writer::move_to_parent() {
  cur = nid_stack.top();
  nid_stack.pop();
}

bool Lnast_prp_writer::is_last_child() const { return lnast->is_last_child(cur); }

std::string_view Lnast_prp_writer::current_text() const { return lnast->get_name(cur); }

Lnast_ntype::Lnast_ntype_int Lnast_prp_writer::current_ntype() const { return lnast->get_type(cur); }

// ── Output helpers ────────────────────────────────────────────────────────────

void Lnast_prp_writer::print(std::string_view s) { os << s; }

void Lnast_prp_writer::print_indent() {
  for (int i = 0; i < depth; ++i) {
    os << "  ";
  }
}

void Lnast_prp_writer::println(std::string_view s) {
  print_indent();
  os << s << "\n";
}

// ── Utilities ─────────────────────────────────────────────────────────────────

bool Lnast_prp_writer::is_tmp(std::string_view name) {
  return name.size() >= 3 && name[0] == '_' && name[1] == '_' && name[2] == '_';
}

std::string Lnast_prp_writer::take_decl_keyword(std::string_view lhs) {
  auto it = pending_decl_.find(std::string(lhs));
  if (it == pending_decl_.end()) {
    return {};
  }
  auto kw = it->second;
  pending_decl_.erase(it);
  return kw;
}

std::string_view Lnast_prp_writer::strip_prefix(std::string_view name) {
  if (!name.empty() && (name[0] == '%' || name[0] == '$')) {
    return name.substr(1);
  }
  return name;
}

// ── Main dispatch ─────────────────────────────────────────────────────────────

void Lnast_prp_writer::write_node() {
  using N = Lnast_ntype;
  switch (current_ntype()) {
    case N::Lnast_ntype_top: write_top(); break;
    case N::Lnast_ntype_stmts: write_stmts(); break;
    case N::Lnast_ntype_if: write_if(); break;
    case N::Lnast_ntype_assign: write_assign(); break;
    case N::Lnast_ntype_ref: write_ref(); break;
    case N::Lnast_ntype_const: write_const(); break;
    case N::Lnast_ntype_cassert: write_cassert(); break;
    case N::Lnast_ntype_func_call: write_func_call(); break;
    case N::Lnast_ntype_func_def: write_func_def(); break;
    case N::Lnast_ntype_tuple_add: write_tuple_add(); break;
    case N::Lnast_ntype_tuple_get: write_tuple_get(); break;
    case N::Lnast_ntype_tuple_set: write_tuple_set(); break;
    case N::Lnast_ntype_attr_set: write_attr_set(); break;
    case N::Lnast_ntype_attr_get: write_attr_get(); break;
    case N::Lnast_ntype_delay_assign: write_delay_assign(); break;
    case N::Lnast_ntype_plus: write_infix("+"); break;
    case N::Lnast_ntype_minus: write_infix("-"); break;
    case N::Lnast_ntype_mult: write_infix("*"); break;
    case N::Lnast_ntype_div: write_infix("/"); break;
    case N::Lnast_ntype_mod: write_infix("%"); break;
    case N::Lnast_ntype_shl: write_infix("<<"); break;
    case N::Lnast_ntype_sra: write_infix(">>"); break;
    case N::Lnast_ntype_sext: write_sext(); break;
    case N::Lnast_ntype_eq: write_infix("=="); break;
    case N::Lnast_ntype_ne: write_infix("!="); break;
    case N::Lnast_ntype_lt: write_infix("<"); break;
    case N::Lnast_ntype_le: write_infix("<="); break;
    case N::Lnast_ntype_gt: write_infix(">"); break;
    case N::Lnast_ntype_ge: write_infix(">="); break;
    case N::Lnast_ntype_log_and: write_infix("and"); break;
    case N::Lnast_ntype_log_or: write_infix("or"); break;
    case N::Lnast_ntype_log_not: write_prefix_unary("not "); break;
    case N::Lnast_ntype_bit_and: write_infix("&"); break;
    case N::Lnast_ntype_bit_or: write_infix("|"); break;
    case N::Lnast_ntype_bit_xor: write_infix("^"); break;
    case N::Lnast_ntype_bit_not: write_prefix_unary("~"); break;
    default: {
      // Unknown node — emit a comment so the output stays parseable.
      println(std::format("/* TODO: unhandled node type {} */", static_cast<int>(current_ntype())));
      break;
    }
  }
}

// ── Structural ────────────────────────────────────────────────────────────────

void Lnast_prp_writer::write_top() {
  // Bare-file modules have no enclosing comb/fun declaration in the source;
  // explicit function definitions are emitted by write_func_def() instead.
  if (move_to_child()) {
    write_node();
    move_to_parent();
  }
}

void Lnast_prp_writer::write_stmts() {
  if (move_to_child()) {
    do {
      print_indent();
      write_node();
      os << "\n";
    } while (move_to_sibling());
    move_to_parent();
  }
}

// ── if ────────────────────────────────────────────────────────────────────────

void Lnast_prp_writer::write_if() {
  if (!move_to_child()) {
    return;
  }

  // First child: condition (ref or const)
  print("if ");
  write_node();
  print(" {\n");
  ++depth;

  // Second child: then-stmts
  if (!move_to_sibling()) {
    --depth;
    println("}");
    move_to_parent();
    return;
  }
  write_node();
  --depth;
  print_indent();
  print("}");

  // Optional: else / elif chains
  while (move_to_sibling()) {
    if (is_last_child()) {
      // Bare else-stmts
      print(" else {\n");
      ++depth;
      write_node();
      --depth;
      print_indent();
      print("}");
    } else {
      // elif condition
      print(" elif ");
      write_node();
      print(" {\n");
      ++depth;
      if (!move_to_sibling()) {
        --depth;
        print_indent();
        print("}");
        break;
      }
      write_node();
      --depth;
      print_indent();
      print("}");
    }
  }

  move_to_parent();
}

// ── assign ────────────────────────────────────────────────────────────────────

void Lnast_prp_writer::write_assign() {
  if (!move_to_child()) {
    return;
  }
  auto lhs = strip_prefix(current_text());
  auto kw  = take_decl_keyword(lhs);
  if (!kw.empty()) {
    print(kw);
    print(" ");
  }
  print(lhs);
  print(" = ");
  move_to_sibling();
  write_node();
  move_to_parent();
}

// ── ref / const ───────────────────────────────────────────────────────────────

void Lnast_prp_writer::write_ref() { print(strip_prefix(current_text())); }

// File-local helper: escapes special characters inside a Pyrope string literal.
static std::string escape_string(std::string_view s) {
  std::string out;
  out.reserve(s.size());
  for (unsigned char c : s) {
    switch (c) {
      case '\\': out += "\\\\"; break;
      case '"': out += "\\\""; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (c < 0x20 || c == 0x7f) {
          out += std::format("\\x{:02x}", static_cast<unsigned>(c));
        } else {
          out += static_cast<char>(c);
        }
        break;
    }
  }
  return out;
}

void Lnast_prp_writer::write_const() {
  auto text = current_text();
  if (!text.empty() && (isdigit(static_cast<unsigned char>(text[0])) || text[0] == '-')) {
    print(text);
  } else if (text == "true" || text == "false" || text == "nil") {
    print(text);
  } else {
    os << std::format("\"{}\"", escape_string(text));
  }
}

// ── cassert ───────────────────────────────────────────────────────────────────

void Lnast_prp_writer::write_cassert() {
  if (!move_to_child()) {
    return;
  }
  print("cassert ");
  write_node();
  move_to_parent();
}

// ── func_call ─────────────────────────────────────────────────────────────────

void Lnast_prp_writer::write_func_call() {
  if (!move_to_child()) {
    return;
  }
  // LHS
  auto lhs = strip_prefix(current_text());
  auto kw  = take_decl_keyword(lhs);
  if (!kw.empty()) {
    print(kw);
    print(" ");
  }
  print(lhs);
  print(" = ");
  // function name
  move_to_sibling();
  print(current_text());
  print("(");
  // arguments — positional args are ref/const nodes; named args are assign
  // nodes (name = value) that must NOT carry the "mut" keyword in call context.
  bool first = true;
  while (move_to_sibling()) {
    if (!first) {
      print(", ");
    }
    if (current_ntype() == Lnast_ntype::Lnast_ntype_assign) {
      // Named actual: emit as  name = value  (no "mut").
      if (move_to_child()) {
        print(strip_prefix(current_text()));  // argument name
        print(" = ");
        if (move_to_sibling()) {
          write_node();  // argument value
        }
        move_to_parent();
      }
    } else {
      write_node();
    }
    first = false;
  }
  print(")");
  move_to_parent();
}

// ── func_def ──────────────────────────────────────────────────────────────────

void Lnast_prp_writer::write_func_def() {
  // Slice 4 not yet implemented — emit a TODO comment block.
  print("/* TODO: func_def — Slice 4 not yet implemented */");
}

// ── Tuples ────────────────────────────────────────────────────────────────────

void Lnast_prp_writer::write_tuple_add() {
  if (!move_to_child()) {
    return;
  }
  auto lhs = strip_prefix(current_text());
  auto kw  = take_decl_keyword(lhs);
  if (!kw.empty()) {
    print(kw);
    print(" ");
  }
  print(lhs);
  print(" = (");
  bool first = true;
  while (move_to_sibling()) {
    if (!first) {
      print(", ");
    }
    write_node();
    first = false;
  }
  print(")");
  move_to_parent();
}

void Lnast_prp_writer::write_tuple_get() {
  if (!move_to_child()) {
    return;
  }
  auto lhs = strip_prefix(current_text());
  auto kw  = take_decl_keyword(lhs);
  if (!kw.empty()) {
    print(kw);
    print(" ");
  }
  print(lhs);
  print(" = ");
  move_to_sibling();
  print(strip_prefix(current_text()));
  while (move_to_sibling()) {
    print("[");
    write_node();
    print("]");
  }
  move_to_parent();
}

void Lnast_prp_writer::write_tuple_set() {
  if (!move_to_child()) {
    return;
  }
  auto lhs = strip_prefix(current_text());
  take_decl_keyword(lhs);  // consume any pending decl so it doesn't leak
  print(lhs);
  while (move_to_sibling() && !is_last_child()) {
    print("[");
    write_node();
    print("]");
  }
  print(" = ");
  write_node();
  move_to_parent();
}

// ── Attributes ────────────────────────────────────────────────────────────────

void Lnast_prp_writer::write_attr_set() {
  if (!move_to_child()) {
    return;
  }
  auto var_name = strip_prefix(current_text());

  if (!move_to_sibling()) {
    move_to_parent();
    return;
  }

  // Suppress LNAST-internal type annotations: attr_set x type mut/reg/wire.
  // Record the storage-class keyword in pending_decl_ so the NEXT assignment
  // to this variable can emit "mut x = …" exactly once.
  if (current_text() == "type") {
    if (move_to_sibling()) {
      auto kw = current_text();
      if (kw == "mut" || kw == "reg" || kw == "wire") {
        pending_decl_[std::string(var_name)] = std::string(kw);
      }
    }
    move_to_parent();
    return;
  }

  // Generic attribute set: var.attr = value
  print(var_name);
  do {
    if (!is_last_child()) {
      print(".");
    } else {
      print(" = ");
    }
    write_node();
  } while (move_to_sibling());

  move_to_parent();
}

void Lnast_prp_writer::write_attr_get() {
  if (!move_to_child()) {
    return;
  }
  auto lhs = strip_prefix(current_text());
  auto kw  = take_decl_keyword(lhs);
  if (!kw.empty()) {
    print(kw);
    print(" ");
  }
  print(lhs);
  print(" = ");
  move_to_sibling();
  print(strip_prefix(current_text()));
  while (move_to_sibling()) {
    print(".");
    write_node();
  }
  move_to_parent();
}

// ── delay_assign ──────────────────────────────────────────────────────────────

void Lnast_prp_writer::write_delay_assign() {
  if (!move_to_child()) {
    return;
  }
  auto lhs = strip_prefix(current_text());
  take_decl_keyword(lhs);  // consume any pending decl so it doesn't leak
  print(lhs);
  print(" = #[");
  move_to_sibling();
  write_node();
  if (move_to_sibling()) {
    print(", ");
    write_node();
  }
  print("]");
  move_to_parent();
}

// ── Infix / prefix helpers ────────────────────────────────────────────────────

void Lnast_prp_writer::write_infix(std::string_view op) {
  if (!move_to_child()) {
    return;
  }

  // First child is the LHS (result variable).
  auto lhs = strip_prefix(current_text());
  auto kw  = take_decl_keyword(lhs);
  if (!kw.empty()) {
    print(kw);
    print(" ");
  }
  print(lhs);
  print(" = ");

  // Remaining siblings are the RHS operands.  Serialize each through
  // write_node() so nested non-leaf nodes are handled correctly.
  bool first = true;
  while (move_to_sibling()) {
    if (!first) {
      print(" ");
      print(op);
      print(" ");
    }
    write_node();
    first = false;
  }
  move_to_parent();
}

void Lnast_prp_writer::write_prefix_unary(std::string_view op) {
  if (!move_to_child()) {
    return;
  }
  auto lhs = strip_prefix(current_text());
  auto kw  = take_decl_keyword(lhs);
  if (!kw.empty()) {
    print(kw);
    print(" ");
  }
  print(lhs);
  print(" = ");
  print(op);
  if (move_to_sibling()) {
    write_node();
  }
  move_to_parent();
}

void Lnast_prp_writer::write_sext() {
  // No Pyrope 3.0 operator for sext — emit as a named call with comment.
  if (!move_to_child()) {
    return;
  }
  auto lhs = strip_prefix(current_text());
  move_to_sibling();
  auto val  = strip_prefix(current_text());
  auto bits = std::string{};
  if (move_to_sibling()) {
    bits = strip_prefix(current_text());
  }
  move_to_parent();

  auto kw = take_decl_keyword(lhs);
  if (!kw.empty()) {
    os << kw << " ";
  }
  os << std::format("{} = sext({}, {})  /* sign-extend */", lhs, val, bits);
}

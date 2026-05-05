//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "lnast_to_lgraph.hpp"

#include <string>

#include "pass.hpp"

Lnast_to_lgraph::Lnast_to_lgraph(Lgraph* lg, std::shared_ptr<Lnast> lnast)
    : lg_(lg), lnast_(std::move(lnast)) {}

void Lnast_to_lgraph::lower() {
  cur_ = lnast_->get_root();
  lower_node();
  wire_outputs();
}

bool Lnast_to_lgraph::move_to_child() {
  auto child = lnast_->get_child(cur_);
  if (child.is_invalid()) return false;
  nid_stack_.push(cur_);
  cur_ = child;
  return true;
}

bool Lnast_to_lgraph::move_to_sibling() {
  auto sib = lnast_->get_sibling_next(cur_);
  if (sib.is_invalid()) return false;
  cur_ = sib;
  return true;
}

void Lnast_to_lgraph::move_to_parent() {
  cur_ = nid_stack_.top();
  nid_stack_.pop();
}

bool Lnast_to_lgraph::is_last_child() const { return lnast_->is_last_child(cur_); }
std::string_view Lnast_to_lgraph::current_text() const { return lnast_->get_name(cur_); }
Lnast_ntype::Lnast_ntype_int Lnast_to_lgraph::current_ntype() const { return lnast_->get_type(cur_); }

std::string_view Lnast_to_lgraph::strip_prefix(std::string_view name) {
  if (!name.empty() && (name[0] == '%' || name[0] == '$')) return name.substr(1);
  return name;
}

bool Lnast_to_lgraph::is_output_port(std::string_view name) { return !name.empty() && name[0] == '%'; }
bool Lnast_to_lgraph::is_input_port(std::string_view name)  { return !name.empty() && name[0] == '$'; }

Node_pin Lnast_to_lgraph::resolve(std::string_view raw_name) {
  auto name = std::string(strip_prefix(raw_name));
  auto it = pin_map_.find(name);
  if (it != pin_map_.end()) return it->second;
  if (is_input_port(raw_name)) {
    // $-prefixed: auto-promote to a graph input.
    auto inp = lg_->add_graph_input(name, next_inp_pos_++, 0);
    pin_map_.emplace(name, inp);
    return inp;
  }
  // Unknown bare ref — likely a typo or unresolved intermediate.
  Pass::warn("lnast_to_lgraph: unresolved ref '{}' — wiring zero constant", name);
  auto zero = lg_->create_node_const(Lconst(0));
  auto pin  = zero.setup_driver_pin();
  pin_map_.emplace(name, pin);
  return pin;
}

void Lnast_to_lgraph::bind(std::string_view raw_name, Node_pin drv) {
  auto name = std::string(strip_prefix(raw_name));
  pin_map_.insert_or_assign(name, drv);
  if (is_output_port(raw_name)) {
    output_names_.insert(name);
  }
}

void Lnast_to_lgraph::wire_outputs() {
  for (const auto& name : output_names_) {
    auto it = pin_map_.find(name);
    if (it == pin_map_.end()) continue;
    auto& drv     = it->second;
    auto  out_pin = lg_->add_graph_output(name, next_out_pos_++, drv.get_bits());
    lg_->add_edge(drv, out_pin);
  }
}

void Lnast_to_lgraph::lower_node() {
  using N = Lnast_ntype;
  switch (current_ntype()) {
    case N::Lnast_ntype_top:      lower_top();    break;
    case N::Lnast_ntype_stmts:    lower_stmts();  break;
    case N::Lnast_ntype_assign:   lower_assign(); break;
    case N::Lnast_ntype_attr_set: lower_attr_set(); break;
    case N::Lnast_ntype_cassert:  lower_cassert(); break;
    // Arithmetic
    case N::Lnast_ntype_plus:  lower_infix(Ntype_op::Sum,  "A", "A"); break;
    case N::Lnast_ntype_minus: lower_infix(Ntype_op::Sum,  "A", "B"); break;
    case N::Lnast_ntype_mult:  lower_infix(Ntype_op::Mult, "A", "A"); break;
    case N::Lnast_ntype_div:   lower_infix(Ntype_op::Div,  "a", "b"); break;
    // Comparison
    case N::Lnast_ntype_eq: lower_infix(Ntype_op::EQ, "A", "B"); break;
    case N::Lnast_ntype_lt: lower_infix(Ntype_op::LT, "A", "B"); break;
    case N::Lnast_ntype_gt: lower_infix(Ntype_op::GT, "A", "B"); break;
    case N::Lnast_ntype_ne:
    case N::Lnast_ntype_le:
    case N::Lnast_ntype_ge:
      Pass::warn("lnast_to_lgraph: ne/le/ge not yet implemented — skipping");
      break;
    // Bitwise
    case N::Lnast_ntype_bit_and: lower_infix(Ntype_op::And, "A", "A"); break;
    case N::Lnast_ntype_bit_or:  lower_infix(Ntype_op::Or,  "A", "A"); break;
    case N::Lnast_ntype_bit_xor: lower_infix(Ntype_op::Xor, "A", "A"); break;
    case N::Lnast_ntype_bit_not: lower_not(); break;
    // Logical — log_and/log_or reduce to a single bit.
    // LGraph uses And/Or with multi-driver A pins; reduction is implicit.
    case N::Lnast_ntype_log_and: lower_infix(Ntype_op::And, "A", "A"); break;
    case N::Lnast_ntype_log_or:  lower_infix(Ntype_op::Ror, "A", "A"); break;
    case N::Lnast_ntype_log_not: lower_not(); break;
    // Shift
    case N::Lnast_ntype_shl: lower_infix(Ntype_op::SHL, "a", "amount"); break;
    case N::Lnast_ntype_sra: lower_infix(Ntype_op::SRA, "a", "b");      break;
    // TODO stubs
    case N::Lnast_ntype_if:
      Pass::warn("lnast_to_lgraph: if/mux not yet implemented");  break;
    case N::Lnast_ntype_func_def:
      Pass::warn("lnast_to_lgraph: func_def not yet implemented"); break;
    case N::Lnast_ntype_func_call:
      Pass::warn("lnast_to_lgraph: func_call not yet implemented"); break;
    case N::Lnast_ntype_tuple_add:
    case N::Lnast_ntype_tuple_get:
    case N::Lnast_ntype_tuple_set:
      Pass::warn("lnast_to_lgraph: tuple nodes not yet implemented"); break;
    default:
      Pass::warn("lnast_to_lgraph: unhandled node type {}", static_cast<int>(current_ntype()));
      break;
  }
}

void Lnast_to_lgraph::lower_top() {
  if (move_to_child()) { lower_node(); move_to_parent(); }
}

void Lnast_to_lgraph::lower_stmts() {
  if (move_to_child()) {
    do { lower_node(); } while (move_to_sibling());
    move_to_parent();
  }
}

void Lnast_to_lgraph::lower_assign() {
  if (!move_to_child()) return;
  auto lhs = current_text();
  if (!move_to_sibling()) { move_to_parent(); return; }
  auto drv = lower_leaf();
  move_to_parent();
  bind(lhs, drv);
}

void Lnast_to_lgraph::lower_attr_set() {}
void Lnast_to_lgraph::lower_cassert()  {}

void Lnast_to_lgraph::lower_infix(Ntype_op op, std::string_view a_pin, std::string_view b_pin) {
  if (!move_to_child()) return;
  auto result = current_text();
  if (!move_to_sibling()) { move_to_parent(); return; }
  auto op_a = lower_leaf();
  if (!move_to_sibling()) { move_to_parent(); return; }
  auto op_b = lower_leaf();
  move_to_parent();

  auto node = lg_->create_node(op);
  lg_->add_edge(op_a, node.setup_sink_pin(a_pin));
  lg_->add_edge(op_b, node.setup_sink_pin(b_pin));
  bind(result, node.setup_driver_pin("Y"));
}

void Lnast_to_lgraph::lower_not() {
  if (!move_to_child()) return;
  auto result = current_text();
  if (!move_to_sibling()) { move_to_parent(); return; }
  auto operand = lower_leaf();
  move_to_parent();

  auto node = lg_->create_node(Ntype_op::Not);
  lg_->add_edge(operand, node.setup_sink_pin("a"));
  bind(result, node.setup_driver_pin("Y"));
}

Node_pin Lnast_to_lgraph::lower_leaf() {
  if (current_ntype() == Lnast_ntype::Lnast_ntype_const) {
    auto cnode = lg_->create_node_const(Lconst::from_pyrope(current_text()));
    return cnode.setup_driver_pin();
  }
  return resolve(current_text());
}

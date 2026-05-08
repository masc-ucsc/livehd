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
  // Wire nil (0sb?) per lnast2lgraph.md §8 rather than a typed zero.
  Pass::warn("lnast_to_lgraph: unresolved ref '{}' — wiring nil (0sb?)", name);
  auto zero = lg_->create_node_const(Lconst::invalid());
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
    // Division: raw Div is not synthesizable. Per lnast2lgraph.md §9, lower to
    // SRA when divisor is a compile-time power-of-two; otherwise call a generic
    // sub-graph. For now emit a warning and wire a nil constant — TODO: emit Sub.
    case N::Lnast_ntype_div:
      Pass::warn("lnast_to_lgraph: division not synthesizable — TODO: lower to SRA or generic div Sub");
      break;
    // Comparison
    case N::Lnast_ntype_eq: lower_infix(Ntype_op::EQ, "A", "B"); break;
    case N::Lnast_ntype_lt: lower_infix(Ntype_op::LT, "A", "B"); break;
    case N::Lnast_ntype_gt: lower_infix(Ntype_op::GT, "A", "B"); break;
    // ne/le/ge: composed from existing primitives
    // ne(a,b) = Not(EQ(a,b))   le(a,b) = Not(GT(a,b))   ge(a,b) = Not(LT(a,b))
    case N::Lnast_ntype_ne: lower_negated_infix(Ntype_op::EQ, "A", "A"); break;
    case N::Lnast_ntype_le: lower_negated_infix(Ntype_op::GT, "A", "B"); break;
    case N::Lnast_ntype_ge: lower_negated_infix(Ntype_op::LT, "A", "B"); break;
    // Bitwise
    case N::Lnast_ntype_bit_and: lower_infix(Ntype_op::And, "A", "A"); break;
    case N::Lnast_ntype_bit_or:  lower_infix(Ntype_op::Or,  "A", "A"); break;
    case N::Lnast_ntype_bit_xor: lower_infix(Ntype_op::Xor, "A", "A"); break;
    case N::Lnast_ntype_bit_not: lower_not(); break;
    // Bitwise reductions (single input, all bits folded to 1)
    case N::Lnast_ntype_red_or:  lower_unary(Ntype_op::Ror,  "A"); break;
    case N::Lnast_ntype_red_and: lower_unary(Ntype_op::And,  "A"); break;
    case N::Lnast_ntype_red_xor: lower_unary(Ntype_op::Xor,  "A"); break;
    // Logical — booleans, map directly to And/Or (§8)
    case N::Lnast_ntype_log_and: lower_infix(Ntype_op::And, "A", "A"); break;
    case N::Lnast_ntype_log_or:  lower_infix(Ntype_op::Or,  "A", "A"); break;
    case N::Lnast_ntype_log_not: lower_not(); break;
    // Shift
    case N::Lnast_ntype_shl: lower_infix(Ntype_op::SHL, "a", "B"); break;
    case N::Lnast_ntype_sra: lower_infix(Ntype_op::SRA, "a", "b"); break;
    // Bit manipulation (§10)
    case N::Lnast_ntype_sext:     lower_infix(Ntype_op::Sext,     "a", "b");     break;
    case N::Lnast_ntype_get_mask: lower_infix(Ntype_op::Get_mask, "a", "mask");  break;
    case N::Lnast_ntype_set_mask: lower_set_mask(); break;
    // mod: same policy as div — not directly synthesizable (§9)
    case N::Lnast_ntype_mod:
      Pass::warn("lnast_to_lgraph: mod not synthesizable — TODO: lower to Get_mask or generic mod Sub");
      break;
    // popcount: no direct LGraph cell — TODO: lower to a Sub call
    case N::Lnast_ntype_popcount:
      Pass::warn("lnast_to_lgraph: popcount not yet implemented");
      break;
    // TODO stubs
    case N::Lnast_ntype_if: lower_if(); break;
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

Node_pin Lnast_to_lgraph::nil_pin() {
  return lg_->create_node_const(Lconst::invalid()).setup_driver_pin();
}

// Lower the stmts at the current cursor into a fresh scope.
// Returns what was written; restores pin_map_ afterwards.
Lnast_to_lgraph::WriteMap Lnast_to_lgraph::lower_branch() {
  auto saved = pin_map_;
  lower_node();  // descends into the stmts subtree, mutates pin_map_
  WriteMap writes;
  for (auto& [name, pin] : pin_map_) {
    auto it = saved.find(name);
    if (it == saved.end() || !(it->second == pin)) {
      writes.emplace(name, pin);
    }
  }
  pin_map_ = saved;  // restore (output_names_ kept — output ports are permanent)
  return writes;
}

// LNAST if-node children: cond, then-stmts, [cond, stmts]*, [else-stmts]
// Lowered to binary Mux chains per lnast2lgraph.md §11:
//   pin "0" = selector, pin "1" = false/else, pin "2" = true/then
void Lnast_to_lgraph::lower_if() {
  if (!move_to_child()) return;

  // ── Collect branches ─────────────────────────────────────────────────────
  struct Branch {
    bool     is_else{false};
    Node_pin cond;
    WriteMap writes;
  };
  std::vector<Branch> branches;

  // First child is the if-condition.
  Node_pin first_cond = lower_leaf();
  if (!move_to_sibling()) { move_to_parent(); return; }

  // Second child is the then-stmts.
  branches.push_back({false, first_cond, lower_branch()});

  // Walk the rest: elif (cond + stmts) pairs and an optional bare else-stmts.
  while (move_to_sibling()) {
    bool last = is_last_child();
    if (last && current_ntype() == Lnast_ntype::Lnast_ntype_stmts) {
      // Bare else-stmts — no condition.
      branches.push_back({true, {}, lower_branch()});
      break;
    }
    // elif: current child is the condition.
    Node_pin elif_cond = lower_leaf();
    if (!move_to_sibling()) break;
    branches.push_back({false, elif_cond, lower_branch()});
  }

  move_to_parent();

  // ── Collect all variables touched by any branch ───────────────────────────
  std::unordered_set<std::string> all_vars;
  for (auto& br : branches) {
    for (auto& [name, _] : br.writes) all_vars.insert(name);
  }

  // ── Build Mux chains per variable ────────────────────────────────────────
  // Start value = else-branch write (or current pin_map_ value, or nil).
  const WriteMap& else_writes = (!branches.empty() && branches.back().is_else)
                                    ? branches.back().writes
                                    : WriteMap{};

  for (const auto& var : all_vars) {
    // Default value when no branch writes: incoming value or nil.
    auto base_it = pin_map_.find(var);
    Node_pin cur_val = (base_it != pin_map_.end()) ? base_it->second : nil_pin();

    // Start from else-value (innermost false path).
    auto else_it = else_writes.find(var);
    if (else_it != else_writes.end()) cur_val = else_it->second;

    // Fold cond-branches right-to-left (skip the else entry at the back).
    int n = static_cast<int>(branches.size());
    int last_cond = (!branches.empty() && branches.back().is_else) ? n - 2 : n - 1;

    for (int i = last_cond; i >= 0; --i) {
      auto& br     = branches[i];
      auto  wr_it  = br.writes.find(var);
      // true-path: branch wrote it; false-path: carry cur_val through.
      Node_pin true_val = (wr_it != br.writes.end()) ? wr_it->second : cur_val;

      auto mux = lg_->create_node(Ntype_op::Mux);
      lg_->add_edge(br.cond,    mux.setup_sink_pin("0"));  // selector
      lg_->add_edge(cur_val,    mux.setup_sink_pin("1"));  // false / else
      lg_->add_edge(true_val,   mux.setup_sink_pin("2"));  // true / then
      cur_val = mux.setup_driver_pin("Y");
    }

    // Write final mux output back (preserve output_names_ membership).
    pin_map_.insert_or_assign(var, cur_val);
  }
}

// ne(a,b) = Not(EQ(a,b)), le(a,b) = Not(GT(a,b)), ge(a,b) = Not(LT(a,b))
void Lnast_to_lgraph::lower_negated_infix(Ntype_op op, std::string_view a_pin, std::string_view b_pin) {
  if (!move_to_child()) return;
  auto result = current_text();
  if (!move_to_sibling()) { move_to_parent(); return; }
  auto op_a = lower_leaf();
  if (!move_to_sibling()) { move_to_parent(); return; }
  auto op_b = lower_leaf();
  move_to_parent();

  auto inner = lg_->create_node(op);
  lg_->add_edge(op_a, inner.setup_sink_pin(a_pin));
  lg_->add_edge(op_b, inner.setup_sink_pin(b_pin));

  auto not_node = lg_->create_node(Ntype_op::Not);
  lg_->add_edge(inner.setup_driver_pin("Y"), not_node.setup_sink_pin("a"));
  bind(result, not_node.setup_driver_pin("Y"));
}

// Single-operand ops: red_or, red_and, red_xor — result = op(A)
void Lnast_to_lgraph::lower_unary(Ntype_op op, std::string_view a_pin) {
  if (!move_to_child()) return;
  auto result  = current_text();
  if (!move_to_sibling()) { move_to_parent(); return; }
  auto operand = lower_leaf();
  move_to_parent();

  auto node = lg_->create_node(op);
  lg_->add_edge(operand, node.setup_sink_pin(a_pin));
  bind(result, node.setup_driver_pin("Y"));
}

// set_mask has three inputs: a, mask, value — result = Set_mask(a, mask, value)
void Lnast_to_lgraph::lower_set_mask() {
  if (!move_to_child()) return;
  auto result = current_text();
  if (!move_to_sibling()) { move_to_parent(); return; }
  auto op_a    = lower_leaf();
  if (!move_to_sibling()) { move_to_parent(); return; }
  auto op_mask = lower_leaf();
  if (!move_to_sibling()) { move_to_parent(); return; }
  auto op_val  = lower_leaf();
  move_to_parent();

  auto node = lg_->create_node(Ntype_op::Set_mask);
  lg_->add_edge(op_a,    node.setup_sink_pin("a"));
  lg_->add_edge(op_mask, node.setup_sink_pin("mask"));
  lg_->add_edge(op_val,  node.setup_sink_pin("value"));
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

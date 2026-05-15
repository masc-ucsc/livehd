//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "lnast_to_lgraph.hpp"

#include <bit>
#include <cstdint>
#include <string>

#include "pass.hpp"

Lnast_to_lgraph::Lnast_to_lgraph(Lgraph* lg, std::shared_ptr<Lnast> lnast) : lg_(lg), lnast_(std::move(lnast)) {}

void Lnast_to_lgraph::lower() {
  cur_ = lnast_->get_root();
  lower_node();
  wire_outputs();
}

bool Lnast_to_lgraph::move_to_child() {
  auto child = lnast_->get_child(cur_);
  if (child.is_invalid()) {
    return false;
  }
  nid_stack_.push(cur_);
  cur_ = child;
  return true;
}

bool Lnast_to_lgraph::move_to_sibling() {
  auto sib = lnast_->get_sibling_next(cur_);
  if (sib.is_invalid()) {
    return false;
  }
  cur_ = sib;
  return true;
}

void Lnast_to_lgraph::move_to_parent() {
  cur_ = nid_stack_.top();
  nid_stack_.pop();
}

bool                         Lnast_to_lgraph::is_last_child() const { return lnast_->is_last_child(cur_); }
std::string_view             Lnast_to_lgraph::current_text() const { return lnast_->get_name(cur_); }
Lnast_ntype::Lnast_ntype_int Lnast_to_lgraph::current_ntype() const { return lnast_->get_type(cur_); }

std::string_view Lnast_to_lgraph::strip_prefix(std::string_view name) {
  if (!name.empty() && (name[0] == '%' || name[0] == '$')) {
    return name.substr(1);
  }
  return name;
}

bool Lnast_to_lgraph::is_output_port(std::string_view name) { return !name.empty() && name[0] == '%'; }
bool Lnast_to_lgraph::is_input_port(std::string_view name) { return !name.empty() && name[0] == '$'; }

Node_pin Lnast_to_lgraph::resolve(std::string_view raw_name) {
  auto name = std::string(strip_prefix(raw_name));
  auto it   = pin_map_.find(name);
  if (it != pin_map_.end()) {
    return it->second;
  }
  if (is_input_port(raw_name)) {
    // $-prefixed: auto-promote to a graph input.
    auto inp = lg_->add_graph_input(name, next_inp_pos_++, 0);
    apply_bw(inp, name);
    pin_map_.emplace(name, inp);
    return inp;
  }
  // Unknown bare ref — likely a typo or unresolved intermediate.
  // Wire nil (0sb?) per lnast2lgraph.md §8 rather than a typed zero.
  Pass::warn("lnast_to_lgraph: unresolved ref '{}' — wiring nil (0sb?)", name);
  auto zero = lg_->create_node_const(Dlop::invalid());
  auto pin  = zero.setup_driver_pin();
  pin_map_.emplace(name, pin);
  return pin;
}

void Lnast_to_lgraph::bind(std::string_view raw_name, Node_pin drv) {
  auto name = std::string(strip_prefix(raw_name));
  pin_map_.insert_or_assign(name, drv);
  if (!branch_writes_stack_.empty()) {
    branch_writes_stack_.back().insert_or_assign(name, drv);
  }
  if (is_output_port(raw_name)) {
    output_names_.insert(name);
  }
}

void Lnast_to_lgraph::wire_outputs() {
  for (const auto& name : output_names_) {
    auto it = pin_map_.find(name);
    if (it == pin_map_.end()) {
      continue;
    }
    auto& drv     = it->second;
    auto  out_pin = lg_->add_graph_output(name, next_out_pos_++, drv.get_bits());
    apply_bw(out_pin, name);
    lg_->add_edge(drv, out_pin);
  }
}

void Lnast_to_lgraph::lower_node() {
  using N = Lnast_ntype;
  switch (current_ntype()) {
    case N::Lnast_ntype_top: lower_top(); break;
    case N::Lnast_ntype_stmts: lower_stmts(); break;
    case N::Lnast_ntype_assign: lower_assign(); break;
    case N::Lnast_ntype_attr_set: lower_attr_set(); break;
    case N::Lnast_ntype_cassert: lower_cassert(); break;
    // Arithmetic
    case N::Lnast_ntype_plus: lower_infix(Ntype_op::Sum, "A", "A"); break;
    case N::Lnast_ntype_minus: lower_infix(Ntype_op::Sum, "A", "B"); break;
    case N::Lnast_ntype_mult: lower_infix(Ntype_op::Mult, "A", "A"); break;
    // Division: raw Div is not synthesizable. Per lnast2lgraph.md §9, lower to
    // SRA when divisor is a compile-time power-of-two; otherwise call a generic
    // sub-graph. For now emit a warning and wire a nil constant — TODO: emit Sub.
    case N::Lnast_ntype_div:
      Pass::warn("lnast_to_lgraph: division not synthesizable — TODO: lower to SRA or generic div Sub");
      break;
    // Comparison.
    // EQ is variadic on pin "A" — cell.cpp lines 107-117 group it with
    // Mult/And/Or/Xor/Ror; pin "B" does not exist.  See contract §7.
    case N::Lnast_ntype_eq: lower_infix(Ntype_op::EQ, "A", "A"); break;
    case N::Lnast_ntype_lt: lower_infix(Ntype_op::LT, "A", "B"); break;
    case N::Lnast_ntype_gt: lower_infix(Ntype_op::GT, "A", "B"); break;
    // ne/le/ge: composed from existing primitives
    // ne(a,b) = Not(EQ(a,b))   le(a,b) = Not(GT(a,b))   ge(a,b) = Not(LT(a,b))
    case N::Lnast_ntype_ne: lower_negated_infix(Ntype_op::EQ, "A", "A"); break;
    case N::Lnast_ntype_le: lower_negated_infix(Ntype_op::GT, "A", "B"); break;
    case N::Lnast_ntype_ge: lower_negated_infix(Ntype_op::LT, "A", "B"); break;
    // Bitwise
    case N::Lnast_ntype_bit_and: lower_infix(Ntype_op::And, "A", "A"); break;
    case N::Lnast_ntype_bit_or: lower_infix(Ntype_op::Or, "A", "A"); break;
    case N::Lnast_ntype_bit_xor: lower_infix(Ntype_op::Xor, "A", "A"); break;
    case N::Lnast_ntype_bit_not: lower_not(); break;
    // Bitwise reductions (single input, all bits folded to 1)
    case N::Lnast_ntype_red_or: lower_unary(Ntype_op::Ror, "A"); break;
    case N::Lnast_ntype_red_and: lower_unary(Ntype_op::And, "A"); break;
    case N::Lnast_ntype_red_xor: lower_unary(Ntype_op::Xor, "A"); break;
    // Logical — booleans, map directly to And/Or (§8)
    case N::Lnast_ntype_log_and: lower_infix(Ntype_op::And, "A", "A"); break;
    case N::Lnast_ntype_log_or: lower_infix(Ntype_op::Or, "A", "A"); break;
    case N::Lnast_ntype_log_not: lower_not(); break;
    // Shift
    case N::Lnast_ntype_shl: lower_infix(Ntype_op::SHL, "a", "B"); break;
    case N::Lnast_ntype_sra: lower_infix(Ntype_op::SRA, "a", "b"); break;
    // Bit manipulation (§10)
    case N::Lnast_ntype_sext: lower_infix(Ntype_op::Sext, "a", "b"); break;
    case N::Lnast_ntype_get_mask: lower_infix(Ntype_op::Get_mask, "a", "mask"); break;
    case N::Lnast_ntype_set_mask: lower_set_mask(); break;
    // mod: same policy as div — not directly synthesizable (§9)
    case N::Lnast_ntype_mod:
      Pass::warn("lnast_to_lgraph: mod not synthesizable — TODO: lower to Get_mask or generic mod Sub");
      break;
    // popcount: no direct LGraph cell — TODO: lower to a Sub call
    case N::Lnast_ntype_popcount: Pass::warn("lnast_to_lgraph: popcount not yet implemented"); break;
    // TODO stubs
    case N::Lnast_ntype_if: lower_if(); break;
    case N::Lnast_ntype_func_def: lower_func_def(); break;
    case N::Lnast_ntype_func_call: Pass::warn("lnast_to_lgraph: func_call not yet implemented"); break;
    case N::Lnast_ntype_tuple_add:
    case N::Lnast_ntype_tuple_get:
    case N::Lnast_ntype_tuple_set: Pass::warn("lnast_to_lgraph: tuple nodes not yet implemented"); break;
    default: Pass::warn("lnast_to_lgraph: unhandled node type {}", static_cast<int>(current_ntype())); break;
  }
}

// `top` has two known layouts:
//
//  (a) Pre-upass / direct from prp2lnast or hand-built tests:
//        top -> stmts             (with optional func_def inside stmts)
//      We simply descend into stmts and let lower_node() / lower_func_def()
//      do the work.
//
//  (b) Post-upass (after upass/func_extract — see upass_func_extract.cpp:128-160):
//        top -> [io, stmts]
//      where `io` has two ref children naming an `__input_tuple_ref` and
//      `__output_tuple_ref` (or `__empty_tuple` if absent), and the matching
//      `tuple_add(<name>) { assign(field, "nil"), ... }` nodes live as the
//      first statements of `stmts` (followed by the original body).
//
// In layout (b) we read I/O directly from the io ref-pair plus the
// tuple_adds; the body statements after the I/O tuple_adds are then lowered
// the same way as layout (a). 1-based pin numbering per contract §5.
void Lnast_to_lgraph::lower_top() {
  if (!move_to_child()) {
    return;
  }

  if (current_ntype() == Lnast_ntype::Lnast_ntype_io) {
    // Layout (b): post-upass with io node (ssa:0 path).
    lower_post_upass_top();
  } else if (!lnast_->io_meta().empty()) {
    // Layout (c): post-SSA-pass — io_meta populated, no io node, flat stmts.
    lower_from_io_meta();
  } else {
    // Layout (a): pre-upass or top-level module with no I/O metadata.
    lower_node();
  }
  move_to_parent();
}

// Walks the post-upass `top -> [io, stmts]` layout produced by
// upass/func_extract.  Cursor is at the `io` child on entry; on exit the
// cursor returns to the same `io` position so the caller's
// `move_to_parent()` lands on `top`.
//
// `io` carries two ref children — refs into `stmts` for the input and
// output tuples (or `__empty_tuple`).  We:
//   1. read the input/output tuple ref names from `io`,
//   2. walk stmts: for each top-level `tuple_add(<input-tuple-name>)`
//      register its assign-children as graph inputs (1-based positions);
//      for `tuple_add(<output-tuple-name>)` collect output field names;
//      skip `__empty_tuple` placeholders; lower every other statement as
//      regular body code,
//   3. wire collected output names to graph outputs after the body
//      finishes (so the body's bind() values are visible).
void Lnast_to_lgraph::lower_post_upass_top() {
  // ── Read io ref-pair to learn the input/output tuple ref names ───────────
  std::string input_tuple_name;
  std::string output_tuple_name;
  if (move_to_child()) {
    input_tuple_name = std::string(current_text());
    if (move_to_sibling()) {
      output_tuple_name = std::string(current_text());
    }
    move_to_parent();
  }

  // Sentinel name from upass/func_extract.cpp for absent tuples.
  constexpr std::string_view empty_tuple_name = "__empty_tuple";

  // ── Move to the sibling `stmts` and walk it once ─────────────────────────
  if (!move_to_sibling()) {
    return;  // no stmts? nothing to do
  }
  if (current_ntype() != Lnast_ntype::Lnast_ntype_stmts) {
    return;  // unexpected layout — bail safely
  }

  std::vector<std::string> out_names;
  if (move_to_child()) {
    do {
      if (current_ntype() == Lnast_ntype::Lnast_ntype_tuple_add) {
        // Determine which tuple this is by inspecting its first child ref.
        std::string tuple_ref_name;
        if (move_to_child()) {
          tuple_ref_name = std::string(current_text());
          // If this is the input tuple, register its assign children as inputs.
          if (!input_tuple_name.empty() && tuple_ref_name == input_tuple_name) {
            while (move_to_sibling()) {
              if (current_ntype() == Lnast_ntype::Lnast_ntype_assign && move_to_child()) {
                auto field = std::string(current_text());
                move_to_parent();
                auto inp = lg_->add_graph_input(field, next_inp_pos_++, 0);
                apply_bw(inp, field);
                pin_map_.emplace(field, inp);
              }
            }
          } else if (!output_tuple_name.empty() && tuple_ref_name == output_tuple_name) {
            while (move_to_sibling()) {
              if (current_ntype() == Lnast_ntype::Lnast_ntype_assign && move_to_child()) {
                out_names.emplace_back(current_text());
                move_to_parent();
              }
            }
          } else if (tuple_ref_name == empty_tuple_name) {
            // Placeholder; nothing to do.
          } else {
            // Body tuple_add (not I/O) — lower as a regular statement once
            // tuple lowering exists.  For now warn and skip.
            Pass::warn("lnast_to_lgraph: body tuple_add '{}' not lowered yet", tuple_ref_name);
          }
          move_to_parent();
        }
      } else {
        // Ordinary body statement.
        lower_node();
      }
    } while (move_to_sibling());
    move_to_parent();
  }

  // ── Wire collected output names to graph output ports ────────────────────
  for (const auto& name : out_names) {
    auto it = pin_map_.find(name);
    if (it == pin_map_.end()) {
      Pass::warn("lnast_to_lgraph: output '{}' not driven by body — wiring nil", name);
      auto nil     = nil_pin();
      auto out_pin = lg_->add_graph_output(name, next_out_pos_++, 0);
      lg_->add_edge(nil, out_pin);
      continue;
    }
    auto& drv     = it->second;
    auto  out_pin = lg_->add_graph_output(name, next_out_pos_++, drv.get_bits());
    apply_bw(out_pin, name);
    lg_->add_edge(drv, out_pin);
  }
}

// Lowers the post-SSA layout produced by uPass_ssa::run():
//   top → stmts (body only — no io node, no tuple_add I/O nodes)
// I/O metadata is read from lnast_->io_meta().
// Cursor is at `stmts` on entry and remains at `stmts` on exit; the caller's
// move_to_parent() returns to `top`.
void Lnast_to_lgraph::lower_from_io_meta() {
  const auto& meta = lnast_->io_meta();

  // Register graph inputs from io_meta. bw_meta (if populated by the bitwidth
  // pass) overrides the io_meta bits — io_meta carries Pyrope-declared widths,
  // bw_meta carries inferred ranges that may be tighter.
  for (const auto& entry : meta.inputs) {
    auto inp = lg_->add_graph_input(entry.name, next_inp_pos_++, entry.bits);
    apply_bw(inp, entry.name);
    pin_map_.emplace(entry.name, inp);
  }

  // Collect output names for wiring after the body runs.
  std::vector<std::string> out_names;
  out_names.reserve(meta.outputs.size());
  for (const auto& entry : meta.outputs) {
    out_names.push_back(entry.name);
  }

  // Lower the body (cursor is already at `stmts`).
  lower_stmts();

  // Wire outputs using the bindings the body established.
  for (const auto& name : out_names) {
    auto it = pin_map_.find(name);
    if (it == pin_map_.end()) {
      Pass::warn("lnast_to_lgraph(ssa): output '{}' not driven by body — wiring nil", name);
      auto nil     = nil_pin();
      auto out_pin = lg_->add_graph_output(name, next_out_pos_++, 0);
      lg_->add_edge(nil, out_pin);
      continue;
    }
    auto& drv     = it->second;
    auto  out_pin = lg_->add_graph_output(name, next_out_pos_++, drv.get_bits());
    apply_bw(out_pin, name);
    lg_->add_edge(drv, out_pin);
  }
}

void Lnast_to_lgraph::lower_stmts() {
  if (move_to_child()) {
    do {
      lower_node();
    } while (move_to_sibling());
    move_to_parent();
  }
}

void Lnast_to_lgraph::lower_assign() {
  if (!move_to_child()) {
    return;
  }
  auto lhs = current_text();
  if (!move_to_sibling()) {
    move_to_parent();
    return;
  }
  auto drv = lower_leaf();
  move_to_parent();
  apply_bw(drv, lhs);
  bind(lhs, drv);
}

void Lnast_to_lgraph::lower_attr_set() {}
void Lnast_to_lgraph::lower_cassert() {}

void Lnast_to_lgraph::lower_infix(Ntype_op op, std::string_view a_pin, std::string_view b_pin) {
  if (!move_to_child()) {
    return;
  }
  auto result = current_text();
  if (!move_to_sibling()) {
    move_to_parent();
    return;
  }
  auto op_a = lower_leaf();
  if (!move_to_sibling()) {
    move_to_parent();
    return;
  }
  auto op_b = lower_leaf();
  move_to_parent();

  auto node = lg_->create_node(op);
  lg_->add_edge(op_a, node.setup_sink_pin(a_pin));
  lg_->add_edge(op_b, node.setup_sink_pin(b_pin));
  auto drv = node.setup_driver_pin("Y");
  apply_bw(drv, result);
  bind(result, drv);
}

Node_pin Lnast_to_lgraph::nil_pin() { return lg_->create_node_const(Dlop::invalid()).setup_driver_pin(); }

void Lnast_to_lgraph::apply_bw(Node_pin& drv, std::string_view lhs_name) {
  const auto& meta = lnast_->bw_meta();
  if (meta.empty()) {
    return;
  }
  // bw_meta keys are stored unprefixed (the bitwidth pass strips %/$ port
  // sigils on write). Mirror that here so callers can pass either form.
  std::string_view key = lhs_name;
  if (!key.empty() && (key.front() == '%' || key.front() == '$')) {
    key.remove_prefix(1);
  }
  auto it = meta.ranges.find(std::string{key});
  if (it == meta.ranges.end()) {
    return;
  }
  const auto& e = it->second;
  if (e.is_unbounded()) {
    return;
  }

  // Compute signed bit count to represent [min, max] in two's complement.
  // Same formula as Lnast_range::sbits_for(v).
  auto sbits_for = [](int64_t v) -> int64_t {
    if (v >= 0) {
      return static_cast<int64_t>(std::bit_width(static_cast<uint64_t>(v))) + 1;
    }
    return static_cast<int64_t>(std::bit_width(static_cast<uint64_t>(-v - 1))) + 1;
  };
  int64_t bits = std::max(sbits_for(e.min), sbits_for(e.max));
  if (bits <= 0 || bits > 4096) {
    return;  // sanity guard
  }

  drv.set_bits(static_cast<uint32_t>(bits));
  bool is_signed = e.min < 0;
  if (is_signed) {
    drv.set_sign();
  } else {
    drv.set_unsign();
  }
}

// Lower the subtree at the current cursor into a fresh branch scope.
// Returns the names *bound* (via bind()) inside the branch.  Names that
// resolve() auto-promotes to graph inputs are NOT branch writes and are
// kept in pin_map_ across branches so siblings reuse the same input pin.
Lnast_to_lgraph::WriteMap Lnast_to_lgraph::lower_branch() {
  branch_writes_stack_.emplace_back();
  auto saved = pin_map_;  // snapshot the entries we may overwrite
  lower_node();
  auto writes = std::move(branch_writes_stack_.back());
  branch_writes_stack_.pop_back();
  // Roll back ONLY the names the branch actually wrote.  Anything resolve()
  // added (auto-promoted graph inputs) stays in pin_map_.
  for (auto& [name, _drv] : writes) {
    auto it = saved.find(name);
    if (it == saved.end()) {
      pin_map_.erase(name);
    } else {
      pin_map_[name] = it->second;
    }
  }
  return writes;
}

// LNAST if-node children: cond, then-stmts, [cond, stmts]*, [else-stmts]
// Lowered to binary Mux chains per lnast2lgraph.md §11:
//   pin "0" = selector, pin "1" = false/else, pin "2" = true/then
void Lnast_to_lgraph::lower_if() {
  if (!move_to_child()) {
    return;
  }

  // ── Collect branches ─────────────────────────────────────────────────────
  struct Branch {
    bool     is_else{false};
    Node_pin cond;
    WriteMap writes;
  };
  std::vector<Branch> branches;

  // First child is the if-condition.
  Node_pin first_cond = lower_leaf();
  if (!move_to_sibling()) {
    move_to_parent();
    return;
  }

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
    if (!move_to_sibling()) {
      break;
    }
    branches.push_back({false, elif_cond, lower_branch()});
  }

  move_to_parent();

  // ── Collect all variables touched by any branch ───────────────────────────
  std::unordered_set<std::string> all_vars;
  for (auto& br : branches) {
    for (auto& [name, _] : br.writes) {
      all_vars.insert(name);
    }
  }

  // ── Build Mux chains per variable ────────────────────────────────────────
  // Start value = else-branch write (or current pin_map_ value, or nil).
  const WriteMap& else_writes = (!branches.empty() && branches.back().is_else) ? branches.back().writes : WriteMap{};

  for (const auto& var : all_vars) {
    // Default value when no branch writes: incoming value or nil.
    auto     base_it = pin_map_.find(var);
    Node_pin cur_val = (base_it != pin_map_.end()) ? base_it->second : nil_pin();

    // Start from else-value (innermost false path).
    auto else_it = else_writes.find(var);
    if (else_it != else_writes.end()) {
      cur_val = else_it->second;
    }

    // Fold cond-branches right-to-left (skip the else entry at the back).
    int n         = static_cast<int>(branches.size());
    int last_cond = (!branches.empty() && branches.back().is_else) ? n - 2 : n - 1;

    for (int i = last_cond; i >= 0; --i) {
      auto& br    = branches[i];
      auto  wr_it = br.writes.find(var);
      // true-path: branch wrote it; false-path: carry cur_val through.
      Node_pin true_val = (wr_it != br.writes.end()) ? wr_it->second : cur_val;

      auto mux = lg_->create_node(Ntype_op::Mux);
      lg_->add_edge(br.cond, mux.setup_sink_pin("0"));   // selector
      lg_->add_edge(cur_val, mux.setup_sink_pin("1"));   // false / else
      lg_->add_edge(true_val, mux.setup_sink_pin("2"));  // true / then
      cur_val = mux.setup_driver_pin("Y");
    }

    // Apply bitwidth to the final Mux output (the merged value seen as `var`).
    // The intermediate muxes for the same var share its inferred range.
    apply_bw(cur_val, var);

    // Write final mux output back (preserve output_names_ membership).
    pin_map_.insert_or_assign(var, cur_val);
  }
}

// ne(a,b) = Not(EQ(a,b)), le(a,b) = Not(GT(a,b)), ge(a,b) = Not(LT(a,b))
void Lnast_to_lgraph::lower_negated_infix(Ntype_op op, std::string_view a_pin, std::string_view b_pin) {
  if (!move_to_child()) {
    return;
  }
  auto result = current_text();
  if (!move_to_sibling()) {
    move_to_parent();
    return;
  }
  auto op_a = lower_leaf();
  if (!move_to_sibling()) {
    move_to_parent();
    return;
  }
  auto op_b = lower_leaf();
  move_to_parent();

  auto inner = lg_->create_node(op);
  lg_->add_edge(op_a, inner.setup_sink_pin(a_pin));
  lg_->add_edge(op_b, inner.setup_sink_pin(b_pin));

  auto not_node = lg_->create_node(Ntype_op::Not);
  lg_->add_edge(inner.setup_driver_pin("Y"), not_node.setup_sink_pin("a"));
  auto drv = not_node.setup_driver_pin("Y");
  apply_bw(drv, result);
  bind(result, drv);
}

// Single-operand ops: red_or, red_and, red_xor — result = op(A)
void Lnast_to_lgraph::lower_unary(Ntype_op op, std::string_view a_pin) {
  if (!move_to_child()) {
    return;
  }
  auto result = current_text();
  if (!move_to_sibling()) {
    move_to_parent();
    return;
  }
  auto operand = lower_leaf();
  move_to_parent();

  auto node = lg_->create_node(op);
  lg_->add_edge(operand, node.setup_sink_pin(a_pin));
  auto drv = node.setup_driver_pin("Y");
  apply_bw(drv, result);
  bind(result, drv);
}

// set_mask has three inputs: a, mask, value — result = Set_mask(a, mask, value)
void Lnast_to_lgraph::lower_set_mask() {
  if (!move_to_child()) {
    return;
  }
  auto result = current_text();
  if (!move_to_sibling()) {
    move_to_parent();
    return;
  }
  auto op_a = lower_leaf();
  if (!move_to_sibling()) {
    move_to_parent();
    return;
  }
  auto op_mask = lower_leaf();
  if (!move_to_sibling()) {
    move_to_parent();
    return;
  }
  auto op_val = lower_leaf();
  move_to_parent();

  auto node = lg_->create_node(Ntype_op::Set_mask);
  lg_->add_edge(op_a, node.setup_sink_pin("a"));
  lg_->add_edge(op_mask, node.setup_sink_pin("mask"));
  lg_->add_edge(op_val, node.setup_sink_pin("value"));
  auto drv = node.setup_driver_pin("Y");
  apply_bw(drv, result);
  bind(result, drv);
}

// func_def children (per prp2lnast.cpp):
//   child0: ref <name>          — function name (matches lg_ name; skip)
//   child1: const <kind>        — "comb" / "seq" etc.; skip
//   child2: tuple_add           — generics (empty for Pyrope basics); skip
//   child3: tuple_add           — captures (empty for Pyrope basics); skip
//   child4: tuple_add of assign — input parameters; each assign: ref<name>, const"0"
//   child5: tuple_add of assign — output parameters; each assign: ref<name>, const"0"
//   child6: stmts               — body
//
// Variable names inside the body are bare (no $/% prefix). We:
//   (a) create graph inputs from child4 and register them in pin_map_,
//   (b) collect output names from child5,
//   (c) lower the body normally — resolve() finds inputs by name; bind() records results,
//   (d) wire output names to graph outputs after body lowering.
void Lnast_to_lgraph::lower_func_def() {
  if (!move_to_child()) {
    return;
  }

  // ── child0: name — skip ────────────────────────────────────────────────────
  if (!move_to_sibling()) {
    move_to_parent();
    return;
  }

  // ── child1: kind — skip ────────────────────────────────────────────────────
  if (!move_to_sibling()) {
    move_to_parent();
    return;
  }

  // ── child2: generics tuple_add — skip ─────────────────────────────────────
  if (!move_to_sibling()) {
    move_to_parent();
    return;
  }

  // ── child3: captures tuple_add — skip ─────────────────────────────────────
  if (!move_to_sibling()) {
    move_to_parent();
    return;
  }

  // ── child4: inputs tuple_add ──────────────────────────────────────────────
  if (move_to_child()) {
    do {
      if (current_ntype() == Lnast_ntype::Lnast_ntype_assign) {
        if (move_to_child()) {
          auto inp_name = std::string(current_text());
          move_to_parent();
          // Register the graph input pin so body references resolve correctly.
          auto inp = lg_->add_graph_input(inp_name, next_inp_pos_++, 0);
          apply_bw(inp, inp_name);
          pin_map_.emplace(inp_name, inp);
        }
      }
    } while (move_to_sibling());
    move_to_parent();
  }

  if (!move_to_sibling()) {
    move_to_parent();
    return;
  }

  // ── child5: outputs tuple_add — collect names ─────────────────────────────
  std::vector<std::string> out_names;
  if (move_to_child()) {
    do {
      if (current_ntype() == Lnast_ntype::Lnast_ntype_assign) {
        if (move_to_child()) {
          out_names.emplace_back(current_text());
          move_to_parent();
        }
      }
    } while (move_to_sibling());
    move_to_parent();
  }

  if (!move_to_sibling()) {
    move_to_parent();
    return;
  }

  // ── child6: body stmts ────────────────────────────────────────────────────
  lower_node();  // lower_stmts() handles Lnast_ntype_stmts

  move_to_parent();

  // ── Wire output names to graph output ports ────────────────────────────────
  for (const auto& name : out_names) {
    auto it = pin_map_.find(name);
    if (it == pin_map_.end()) {
      Pass::warn("lnast_to_lgraph: func_def output '{}' not written in body — wiring nil", name);
      auto nil     = nil_pin();
      auto out_pin = lg_->add_graph_output(name, next_out_pos_++, 0);
      lg_->add_edge(nil, out_pin);
      continue;
    }
    auto& drv     = it->second;
    auto  out_pin = lg_->add_graph_output(name, next_out_pos_++, drv.get_bits());
    apply_bw(out_pin, name);
    lg_->add_edge(drv, out_pin);
  }
}

void Lnast_to_lgraph::lower_not() {
  if (!move_to_child()) {
    return;
  }
  auto result = current_text();
  if (!move_to_sibling()) {
    move_to_parent();
    return;
  }
  auto operand = lower_leaf();
  move_to_parent();

  auto node = lg_->create_node(Ntype_op::Not);
  lg_->add_edge(operand, node.setup_sink_pin("a"));
  auto drv = node.setup_driver_pin("Y");
  apply_bw(drv, result);
  bind(result, drv);
}

Node_pin Lnast_to_lgraph::lower_leaf() {
  if (current_ntype() == Lnast_ntype::Lnast_ntype_const) {
    auto cnode = lg_->create_node_const(Dlop::from_pyrope(current_text()));
    return cnode.setup_driver_pin();
  }
  return resolve(current_text());
}

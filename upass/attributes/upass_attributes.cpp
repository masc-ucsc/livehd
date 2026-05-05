//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "upass_attributes.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "lnast_ntype.hpp"
#include "upass_attributes_sticky.hpp"

// Plugin registration. Name "attributes" is what pass_upass appends to the
// default upass order.
static upass::uPass_plugin plugin_attributes("attributes", upass::uPass_wrapper<uPass_attributes>::get_upass);

uPass_attributes::uPass_attributes(std::shared_ptr<upass::Lnast_manager>& _lm) : upass::uPass(_lm) {
  // Phase 1: register the sticky pattern handler. Later phases will register
  // additional exact-name handlers (bits/ubits/sbits/max/min, clock_pin,
  // reset_pin, …) and a default pass-through handler for category C/D.
  reg.register_sticky_pattern(std::make_shared<upass::attributes::Sticky_handler>());
  // TODO(phase 5+): register exact-name handlers for category-A attrs.
  // TODO(phase 6): register handlers for category-B wiring attrs.
  // TODO(phase 7): register the default pass-through handler.
}

void uPass_attributes::begin_iteration() {
  upass::uPass::begin_iteration();
  pending_arms.clear();
  active_arm_stack.clear();
  reg.for_each_handler([this](upass::attributes::Attribute_handler& h) { h.begin_iteration(*this); });
}

void uPass_attributes::end_run() {
  reg.for_each_handler([this](upass::attributes::Attribute_handler& h) { h.end_run(*this); });
}

std::string uPass_attributes::normalize_name(std::string_view name) {
  if (!name.empty() && (name.front() == '%' || name.front() == '$')) {
    return std::string{name.substr(1)};
  }
  return std::string{name};
}

uPass_attributes::Op_view uPass_attributes::scan_op() {
  // Cursor is at the op node. First child is LHS; remaining children are
  // operands. For an `assign` with exactly one ref operand, classify as
  // alias (direct ref aliasing per the spec).
  Op_view view;
  const bool is_assign = is_type(Lnast_ntype::Lnast_ntype_assign);

  if (!move_to_child()) {
    return view;
  }
  view.lhs = normalize_name(current_text());

  std::size_t ref_count = 0;
  while (move_to_sibling()) {
    if (Lnast_ntype::is_ref(get_raw_ntype())) {
      view.rhs_refs.emplace_back(normalize_name(current_text()));
      ++ref_count;
    }
  }
  move_to_parent();

  // Alias semantics: assign with a single ref RHS (e.g. `mut z = a`).
  // assign(ref, const) and any expression-shaped node fall through to
  // expression semantics (sticky-only migration).
  if (is_assign && view.rhs_refs.size() == 1 && ref_count == 1) {
    view.is_alias = true;
  }
  return view;
}

void uPass_attributes::dispatch_attr_set(std::string_view attr_name, std::string_view lhs, std::string_view value_text) {
  auto* h = reg.lookup(attr_name);
  if (!h) {
    return;
  }
  dispatch_bucket = upass::attributes::Sticky_handler::canonical_bucket(attr_name);
  h->on_attr_set(*this, lhs, value_text);
  dispatch_bucket.clear();
}

void uPass_attributes::dispatch_attr_get(std::string_view attr_name, std::string_view dst, std::string_view base) {
  auto* h = reg.lookup(attr_name);
  if (!h) {
    return;
  }
  dispatch_bucket = upass::attributes::Sticky_handler::canonical_bucket(attr_name);
  h->on_attr_get(*this, dst, base);
  dispatch_bucket.clear();
}

void uPass_attributes::on_assign_like(bool /*is_assign_node*/) {
  auto view = scan_op();
  if (view.lhs.empty()) {
    return;
  }
  if (view.is_alias) {
    reg.for_each_handler([&](upass::attributes::Attribute_handler& h) {
      h.on_alias_assign(*this, view.lhs, view.rhs_refs.front());
    });
    return;
  }
  std::vector<std::string_view> refs;
  refs.reserve(view.rhs_refs.size());
  for (const auto& r : view.rhs_refs) {
    refs.emplace_back(r);
  }
  reg.for_each_handler([&](upass::attributes::Attribute_handler& h) { h.on_expr_assign(*this, view.lhs, refs); });
}

void uPass_attributes::process_assign() { on_assign_like(/*is_assign_node=*/true); }

#define EXPR_PROCESS(NAME) \
  void uPass_attributes::process_##NAME() { on_assign_like(/*is_assign_node=*/false); }

EXPR_PROCESS(plus)
EXPR_PROCESS(minus)
EXPR_PROCESS(mult)
EXPR_PROCESS(div)
EXPR_PROCESS(mod)
EXPR_PROCESS(shl)
EXPR_PROCESS(sra)
EXPR_PROCESS(bit_and)
EXPR_PROCESS(bit_or)
EXPR_PROCESS(bit_not)
EXPR_PROCESS(bit_xor)
EXPR_PROCESS(log_and)
EXPR_PROCESS(log_or)
EXPR_PROCESS(log_not)
EXPR_PROCESS(red_or)
EXPR_PROCESS(red_and)
EXPR_PROCESS(red_xor)
EXPR_PROCESS(ne)
EXPR_PROCESS(eq)
EXPR_PROCESS(lt)
EXPR_PROCESS(le)
EXPR_PROCESS(gt)
EXPR_PROCESS(ge)
EXPR_PROCESS(sext)
EXPR_PROCESS(get_mask)
EXPR_PROCESS(set_mask)

#undef EXPR_PROCESS

void uPass_attributes::process_attr_set() {
  // Layout (see prp2lnast::emit_attribute_list):
  //   attr_set
  //     ref(target)
  //     const(attr_name)
  //     <value: const | ref>
  if (!move_to_child()) {
    return;
  }
  auto target = normalize_name(current_text());
  if (!move_to_sibling()) {
    move_to_parent();
    return;
  }
  std::string attr_name{current_text()};
  std::string value_text;
  bool        value_is_ref = false;
  if (move_to_sibling()) {
    value_text   = std::string{current_text()};
    value_is_ref = Lnast_ntype::is_ref(get_raw_ntype());
  }
  move_to_parent();

  // Phase 2 side-state writes — keep these BEFORE dispatch so handlers can
  // see the updated maps.
  if (!target.empty() && !attr_name.empty()) {
    if (attr_name == "type") {
      auto&     ti   = type_info_map[target];
      Decl_kind kind = Decl_kind::unknown;
      if (value_text == "mut") {
        kind = Decl_kind::mut_kind;
      } else if (value_text == "const") {
        kind = Decl_kind::const_kind;
      } else if (value_text == "reg") {
        kind = Decl_kind::reg_kind;
      } else if (value_text == "await") {
        kind = Decl_kind::await_kind;
      }
      if (kind != Decl_kind::unknown) {
        ti.decl = kind;
      }
    } else if (attr_name == "comptime") {
      auto& ti = type_info_map[target];
      if (value_text != "false" && value_text != "0") {
        ti.is_comptime = true;
      }
      // Still record the explicit value (true/false) so a `.[comptime]`
      // read returns the explicit answer.
      attr_set_values[target][attr_name] = (value_text == "false" || value_text == "0") ? Lconst(0) : Lconst(1);
    } else if (value_is_ref) {
      // Refs (e.g. range tmp, or a runtime wire ref for clock_pin) are
      // stored separately so derive_max can chain through range_bounds.
      attr_set_refs[target][attr_name] = normalize_name(value_text);
    } else {
      Lconst stored;
      if (value_text.empty() || value_text == "true") {
        stored = Lconst(1);
      } else {
        stored = Lconst::from_pyrope(value_text);
      }
      if (!stored.is_invalid()) {
        attr_set_values[target][attr_name] = stored;
      }
    }
  }

  dispatch_attr_set(attr_name, target, value_text);
}

void uPass_attributes::process_attr_get() {
  // Layout (see prp2lnast::attribute_read_to_node):
  //   attr_get
  //     ref(dst)
  //     ref(base)
  //     const(attr_name)
  if (!move_to_child()) {
    return;
  }
  auto dst = normalize_name(current_text());
  if (!move_to_sibling()) {
    move_to_parent();
    return;
  }
  std::string base_text{current_text()};
  auto        base = normalize_name(base_text);
  std::string attr_name;
  if (move_to_sibling()) {
    attr_name = std::string{current_text()};
  }
  move_to_parent();
  if (attr_name.empty()) {
    return;
  }

  // Phase 2 — compute and publish the folded value before per-name dispatch
  // so the sticky/per-attr handlers see the same observable state every
  // pass other consumer would.
  evaluate_attr_get(dst, base_text, base, attr_name);

  dispatch_attr_get(attr_name, dst, base);
}

void uPass_attributes::process_if() {
  // Pre-scan the if's children to associate each arm-stmts node with its
  // controlling cond. The runner walks the body next; process_stmts hooks
  // pop the recorded arm and notify handlers about the cond's sticky refs.
  //
  // Layout — alternating (cond, stmts) pairs with an optional trailing
  // else-stmts:
  //   if(cond1, stmts1, [cond2, stmts2, ...], [else_stmts])
  //
  // Cond is always a ref or const at the top (compound conditions are
  // lowered to tmps in prp2lnast).
  if (!move_to_child()) {
    return;
  }
  std::vector<std::string> running_cond_refs;
  while (true) {
    const auto t = get_raw_ntype();
    if (Lnast_ntype::is_stmts(t)) {
      // This stmts is an arm body (then/else/elif). Record the most-recent
      // cond's refs against this stmts nid.
      const auto nid_key = static_cast<uint64_t>(lm->get_current_nid().get_class_index().value);
      Pending_arm arm;
      arm.cond_refs = running_cond_refs;  // empty for bare-else stmts
      pending_arms.emplace(nid_key, std::move(arm));
      running_cond_refs.clear();
      if (!move_to_sibling()) {
        break;
      }
      continue;
    }
    if (Lnast_ntype::is_ref(t)) {
      running_cond_refs.emplace_back(normalize_name(current_text()));
    }
    // const cond contributes no refs; nothing to record.
    if (!move_to_sibling()) {
      break;
    }
  }
  move_to_parent();
}

void uPass_attributes::process_stmts() {
  const auto nid_key = static_cast<uint64_t>(lm->get_current_nid().get_class_index().value);
  auto it = pending_arms.find(nid_key);
  if (it == pending_arms.end()) {
    return;
  }
  std::vector<std::string_view> refs;
  refs.reserve(it->second.cond_refs.size());
  for (const auto& r : it->second.cond_refs) {
    refs.emplace_back(r);
  }
  std::vector<std::pair<std::string_view, std::string_view>> attr_reads;
  attr_reads.reserve(it->second.cond_attr_reads.size());
  for (const auto& [v, a] : it->second.cond_attr_reads) {
    attr_reads.emplace_back(v, a);
  }
  reg.for_each_handler(
      [&](upass::attributes::Attribute_handler& h) { h.on_if_arm_enter(*this, refs, attr_reads); });
  active_arm_stack.push_back(nid_key);
}

void uPass_attributes::process_stmts_post() {
  const auto nid_key = static_cast<uint64_t>(lm->get_current_nid().get_class_index().value);
  if (active_arm_stack.empty() || active_arm_stack.back() != nid_key) {
    return;  // not an arm stmts
  }
  reg.for_each_handler([this](upass::attributes::Attribute_handler& h) { h.on_if_arm_exit(*this); });
  active_arm_stack.pop_back();
  pending_arms.erase(nid_key);
}

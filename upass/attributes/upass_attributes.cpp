//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "upass_attributes.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "lnast_ntype.hpp"
#include "upass_attributes_phase5.hpp"
#include "upass_attributes_sticky.hpp"

// Plugin registration. Name "attributes" is what pass_upass appends to the
// default upass order.
static upass::uPass_plugin plugin_attributes("attributes", upass::uPass_wrapper<uPass_attributes>::get_upass);

// Defined in upass_attributes_phase6.cpp.
void uPass_attributes_register_phase6(uPass_attributes& self);

uPass_attributes::uPass_attributes(std::shared_ptr<upass::Lnast_manager>& _lm) : upass::uPass(_lm) {
  // Phase 1 — sticky `_*` / `debug` attribute pattern.
  reg.register_sticky_pattern(std::make_shared<upass::attributes::Sticky_handler>());

  // Phase 5 — category-A consumption.
  auto wrap_sat = std::make_shared<upass::attributes::Wrap_sat_handler>();
  reg.register_exact("wrap", wrap_sat);
  reg.register_exact("saturate", wrap_sat);
  reg.register_exact("sat", wrap_sat);
  reg.register_exact("type", std::make_shared<upass::attributes::Const_handler>());

  // Phase 6 — category-B LGraph-wiring handlers (clock_pin, reset_pin,
  // posclk, ...). Lives in its own translation unit so the table of
  // pin/mode names doesn't clutter constructor logic.
  uPass_attributes_register_phase6(*this);

  // Phase 7 — default pass-through handler. Unknown attribute names land
  // here. The handler is intentionally a no-op: process_attr_set already
  // records every value into attr_set_values regardless of name, so cat-D
  // attrs round-trip via the side-map for downstream LGraph generation.
  reg.register_default(std::make_shared<upass::attributes::Attribute_handler>());
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

void uPass_attributes::on_assign_like(bool is_assign_node) {
  auto view = scan_op();
  if (view.lhs.empty()) {
    return;
  }
  // Phase 5 — bookkeeping for any assignment-shaped op. The `nil` rvalue
  // is an explicit invalidation per spec and is not counted as a binding.
  // Detect by checking the single rhs ref or const text.
  bool                  rhs_is_nil = false;
  std::optional<Const> rhs_value;  // direct LNAST-source value, when scalar
  if (is_assign_node) {
    move_to_child();
    if (move_to_sibling()) {
      auto txt = current_text();
      if (txt == "nil") {
        rhs_is_nil = true;
      }
      if (Lnast_ntype::is_const(get_raw_ntype())) {
        auto v = Dlop::from_pyrope(txt);
        if (!v.is_invalid()) {
          rhs_value = v;
        }
      } else if (Lnast_ntype::is_ref(get_raw_ntype()) && runner_fold_fn) {
        auto folded = runner_fold_fn(txt);
        if (folded && !folded->is_invalid()) {
          rhs_value = *folded;
        }
      }
    }
    move_to_parent();
  }
  // Phase 5 — apply wrap/sat narrowing eagerly when we have a fresh RHS
  // value in hand. record_assign also tries this via runner_fold_fn, but
  // that lookup falls back to constprop's ST which still holds the
  // previous iteration's value at the moment our process_assign runs
  // (constprop's process_assign fires AFTER ours in pass order). Reading
  // the RHS directly from the LNAST gets us the right input.
  if (!view.lhs.empty() && is_assign_node && !rhs_is_nil) {
    // Drop any stale narrowed value from a previous assign so a per-
    // statement wrap/sat applied to the next attr_set picks up the fresh
    // constprop-stored RHS via runner_fold_fn instead of the previous
    // iteration's narrowed result.
    tmp_fold.erase(view.lhs);
  }
  if (rhs_value && !view.lhs.empty() && (has_wrap_policy(view.lhs) || has_sat_policy(view.lhs))) {
    Const out = narrow_for_lhs(view.lhs, *rhs_value);
    if (!out.is_invalid()) {
      auto [iter, inserted] = tmp_fold.emplace(view.lhs, out);
      if (!inserted && iter->second != out) {
        iter->second = out;
        mark_changed();
      } else if (inserted) {
        mark_changed();
      }
    }
  }
  record_assign(view.lhs, rhs_is_nil);

  if (view.is_alias) {
    // Phase 3 — shape / typename / range inheritance through direct aliases.
    // When `assign foo bar` and bar carries a tuple shape (or range), foo
    // takes on the same shape so aggregate reads `foo.[size]` etc. resolve.
    const auto& rhs = view.rhs_refs.front();
    bool        record_source = false;
    bool        gained_shape  = false;
    if (auto* sh = lookup_tuple_shape(rhs); sh) {
      auto&      slot    = tuple_shapes[view.lhs];
      const bool changed = slot.fields != sh->fields || slot.from_range != sh->from_range;
      slot               = *sh;
      if (changed) {
        mark_changed();
      }
      record_source = true;
      gained_shape  = true;
    } else if (lookup_range(rhs)) {
      // Range tmp aliased into a named var — preserve the shape-source
      // mapping so `h.[size]` can chain back to range_bounds[___16].
      record_source = true;
    }
    if (record_source) {
      auto [it, inserted] = shape_source.emplace(view.lhs, rhs);
      if (!inserted && it->second != rhs) {
        it->second = rhs;
        mark_changed();
      } else if (inserted) {
        mark_changed();
      }
    }
    // Phase 4 — alias attribute migration. Copy direct attrs and any
    // tuple_get_alias from rhs onto lhs so future `lhs.[attr]` reads chain
    // through the same path. Then, if lhs just gained a shape, materialize
    // any cat-D aggregate→field inheritance onto the per-field paths.
    migrate_alias(view.lhs, rhs);
    if (gained_shape) {
      migrate_aggregate_attrs_to_fields(view.lhs);
    }
    // Phase 5 — propagate any narrowed value from rhs to lhs so a downstream
    // `lhs == k` (e.g. after `sat z = 3000` then `const m = z`) sees the
    // narrowed value via runner_fold_fn instead of the raw bundle in
    // constprop's symbol table.
    if (auto it = tmp_fold.find(rhs); it != tmp_fold.end() && !it->second.is_invalid()) {
      auto [m_it, inserted] = tmp_fold.emplace(view.lhs, it->second);
      if (!inserted && m_it->second != it->second) {
        m_it->second = it->second;
        mark_changed();
      } else if (inserted) {
        mark_changed();
      }
    }
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
      attr_set_values[target][attr_name] = (value_text == "false" || value_text == "0") ? Dlop::create_integer(0) : Dlop::create_integer(1);
    } else if ((attr_name == "ubits" || attr_name == "sbits") && !value_is_ref) {
      // Phase 5 — `[ubits=N]` / `[sbits=N]` are alternative type-info entry
      // points (the user wrote `mut x::[ubits=12] = 0` rather than
      // `mut x:u12 = 0`). Mirror them into type_info_map so wrap/sat
      // narrowing has a width to clamp against. Dlop::from_pyrope on a
      // numeric literal yields a foldable Const whose to_i fits in
      // uint32_t for any realistic bit width.
      auto v = Dlop::from_pyrope(value_text);
      if (v->is_i()) {
        auto& ti = type_info_map[target];
        ti.bits  = static_cast<uint32_t>(v->to_i());
        if (attr_name == "ubits") {
          ti.kind = Numeric_kind::unsigned_int;
        } else {
          ti.kind = Numeric_kind::signed_int;
        }
      }
      attr_set_values[target][attr_name] = v;
    } else if (value_is_ref) {
      // Refs (e.g. range tmp, or a runtime wire ref for clock_pin) are
      // stored separately so derive_max can chain through range_bounds.
      attr_set_refs[target][attr_name] = normalize_name(value_text);
    } else {
      Const stored;
      if (value_text.empty() || value_text == "true") {
        stored = Dlop::create_integer(1);
      } else {
        stored = Dlop::from_pyrope(value_text);
      }
      if (!stored.is_invalid()) {
        attr_set_values[target][attr_name] = stored;
        // Phase 3 — when the target is a tuple_get tmp (per-field decl-site
        // attribute, e.g. `b::[poison=99]=2` lowered to `tuple_get tmp __1 b
        // ; attr_set tmp poison 99`), also store at the canonical
        // base.field_key path so a later `t.b.[poison]` read finds the
        // override after `assign t __1` migrates the shape.
        if (auto* a = lookup_get_alias(target); a) {
          std::string canonical = a->base + "." + a->field_key;
          attr_set_values[canonical][attr_name] = stored;
          if (!a->field_name.empty() && a->field_name != a->field_key) {
            std::string named = a->base + "." + a->field_name;
            attr_set_values[named][attr_name] = stored;
          }
        }
        // Phase 4 — when the target already has a tuple shape (i.e. a later
        // attr_set on an existing aggregate), re-materialize aggregate→field
        // inheritance so the new attr lands on every field path too.
        if (lookup_tuple_shape(target) != nullptr || shape_source.find(target) != shape_source.end()) {
          migrate_aggregate_attrs_to_fields(target);
        }
        // Typename-driven attribute inheritance — `const x:a = 100` lowers
        // to `attr_set x typename 'a'`, and per spec x takes on a's
        // attribute set. When the typename string matches a known variable
        // with tracked attrs, treat it as a direct alias for attribute
        // lookup: copy attrs through migrate_alias and notify handlers so
        // sticky buckets propagate too.
        if (attr_name == "typename" && value_text.size() >= 2 && value_text.front() == '\''
            && value_text.back() == '\'') {
          std::string src{value_text.substr(1, value_text.size() - 2)};
          if (!src.empty() && src != target
              && (attr_set_values.count(src) != 0 || type_info_map.count(src) != 0)) {
            migrate_alias(target, src);
            reg.for_each_handler(
                [&](upass::attributes::Attribute_handler& h) { h.on_alias_assign(*this, target, src); });
          }
        }
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

//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

// Category-A LNAST/upass attribute consumption.
//
// Implements:
//   * `wrap` / `saturate` / `sat` (alias) — declaration-site assignment
//     policy vs. statement-level prefix. The declaration form persists so
//     every assignment to the variable narrows; the statement form narrows
//     just the in-flight value and leaves no sticky attribute.
//   * `const` — single non-nil binding per cycle; rebind is upass::error.
//
// Both handlers run via the per-name dispatcher. The declaration-vs-
// statement distinction for wrap/sat looks at whether the target already
// had any assign by the time attr_set fires. prp2lnast emits the
// declaration form *before* the assignment but the statement-level form
// *after* the assignment, so this position check is sound.
//
// Narrowing math runs through tmp_fold (the same map the attr-read
// evaluator publishes derived attribute reads to) so cassert / eq
// consumers pick up the narrowed value via runner_fold_fn. constprop's
// current_prim_value consults runner_fold_fn before falling back to its
// own ST when no stored value exists; we don't need to fight constprop
// for primary binding.

#include "upass_attributes_wrap_sat.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "const.hpp"
#include "upass_attributes.hpp"
#include "upass_utils.hpp"
#include "wrap_sat.hpp"  // narrowing math now lives in //upass/bitwidth (T2 #7)

namespace {

// Wrap / saturate narrowing math moved to upass/bitwidth/wrap_sat.hpp.
// Re-export under the same names so the rest of this file's call sites
// stay unchanged and policy/trigger semantics here remain isolated from
// the value-math.
using upass::bitwidth::saturate_signed;
using upass::bitwidth::saturate_unsigned;
using upass::bitwidth::wrap_to_signed;
using upass::bitwidth::wrap_to_unsigned;

}  // namespace

Const uPass_attributes::narrow_for_lhs(std::string_view type_src, const Const& v, bool is_wrap, bool is_sat) const {
  if (!is_wrap && !is_sat) {
    return v;
  }
  const auto* ti = lookup_type_info(type_src);
  if (ti == nullptr || ti->bits == 0) {
    return v;
  }
  const bool is_signed = ti->kind == Numeric_kind::signed_int;
  if (is_wrap) {
    return is_signed ? wrap_to_signed(v, ti->bits) : wrap_to_unsigned(v, ti->bits);
  }
  return is_signed ? saturate_signed(v, ti->bits) : saturate_unsigned(v, ti->bits);
}

// Task 1t — `wrap`/`sat` lower to a library call
//   func_call(dst, ref("wrap"|"sat"), store(ref("v"), value), store(ref("type"), ref(lhs)))
// Narrow the value to the lhs's declared type (read via the `type=` arg) and
// publish it on the call's dst tmp; the following `store(lhs, dst)` then
// alias-propagates the narrowed value into lhs (consumers read it via
// runner_fold_fn). A no-op narrowing is left to constprop's copy-through.
void uPass_attributes::process_func_call() {
  if (!move_to_child()) {
    return;
  }
  std::string dst(current_text());
  if (!move_to_sibling()) {
    move_to_parent();
    return;
  }
  const auto callee  = current_text();
  const bool is_wrap = callee == "wrap";
  const bool is_sat  = callee == "sat" || callee == "saturate";
  if (!is_wrap && !is_sat) {
    move_to_parent();
    return;
  }

  std::optional<Const> value;
  std::string          type_src;
  while (move_to_sibling()) {
    if (!is_type(Lnast_ntype::Lnast_ntype_store)) {
      continue;
    }
    if (!move_to_child()) {
      continue;
    }
    const std::string key(current_text());
    if (move_to_sibling()) {
      if (key == "v") {
        if (Lnast_ntype::is_const(get_raw_ntype())) {
          auto v = Dlop::from_pyrope(current_text());
          if (!v->is_invalid()) {
            value = *v;
          }
        } else if (Lnast_ntype::is_ref(get_raw_ntype()) && runner_st != nullptr) {
          auto folded = runner_st->known_const_scalar(current_text());
          if (folded && !folded->is_invalid()) {
            value = *folded;
          }
        }
      } else if (key == "type" && Lnast_ntype::is_ref(get_raw_ntype())) {
        type_src = std::string(current_text());
      }
    }
    move_to_parent();
  }
  move_to_parent();

  if (!value || type_src.empty()) {
    return;
  }
  Const out = narrow_for_lhs(type_src, *value, is_wrap, is_sat);
  if (out.is_invalid() || out.same_repr(*value)) {
    return;  // narrowing is a no-op; constprop's copy-through carries the value
  }
  auto [it, inserted] = tmp_fold.emplace(dst, out);
  if (!inserted && !it->second.same_repr(out)) {
    it->second = out;
  }
  // The narrowed value IS the call dst's value; write it to the
  // binding too (the dst is a single-writer tmp constprop never folds for
  // wrap/sat calls), so table-only operand resolution sees it and the
  // runner_fold_fn pull seam can retire.
  if (runner_st != nullptr) {
    (void)runner_st->set(dst, out);
  }
}

void uPass_attributes::record_assign(std::string_view lhs, bool rhs_is_nil) {
  if (lhs.empty()) {
    return;
  }
  if (rhs_is_nil) {
    return;  // nil invalidations don't count as a real binding (per spec)
  }

  // record_assign's only remaining side effects need type_info for `lhs`:
  //   * the bound marker gates the unsigned first-write coercion in
  //     on_assign_like (only typed unsigned LHS reach it).
  //   * the const-single-bind check is inside the `decl == const_kind` branch.
  // When `lookup_type_info(lhs)` is null, both are dead — skip the work. This
  // is the dominant savings on bulk-arithmetic workloads (xx.prp: 1M plus + 1M
  // assign ops, mostly tmp LHS with no type_info).
  const auto* ti = lookup_type_info(lhs);
  if (ti == nullptr) {
    return;
  }

  // The bind-tracking state lives ON the binding as a NON-STICKY
  // residual attr ("vbound": assigned a non-nil value at least once). Scope
  // locality replaces the old per-clone counter reset: loop iterations and
  // inliner clones get fresh scopes/names, so their bindings start unmarked.
  // record_assign runs BEFORE constprop's table write at the same store
  // (pass order), so the first write reads its own declare-baked bundle.
  if (runner_st == nullptr) {
    return;
  }
  const auto root  = Bundle::get_first_level(lhs);
  const auto field = Bundle::get_all_but_first_level(lhs);
  auto       b     = runner_st->get_bundle_for_write(root);
  if (!b) {
    return;
  }
  const bool was_bound = field.empty() ? b->has_attr("vbound") : b->has_attr(field, "vbound");

  // Const single-bind enforcement. Skipped inside an init-construction
  // window: the runner's synthesized constructor stores (defaults bind +
  // ref-self write-back) are one logical binding, not user re-binds.
  if (ti->decl == Decl_kind::const_kind && init_construction_depth_ == 0 && was_bound) {
    upass::error("uPass_attributes: const `{}` rebind (assigned {} times)\n", lhs, 2);
  }
  if (!was_bound) {
    if (field.empty()) {
      b->set_attr("vbound", *Dlop::create_integer(1));
    } else {
      b->set_attr(field, "vbound", *Dlop::create_integer(1));
    }
  }
}

namespace upass {
namespace attributes {

void Const_handler::on_attr_set(uPass_attributes& owner, std::string_view lhs, std::string_view value_text) {
  // attr_set type=const is the canonical signal. The actual single-bind
  // check fires from record_assign when an assign lands on a const var.
  // This handler exists so the registry has an exact-name slot for "type"
  // (which dispatches here from process_attr_set when value_text=="const").
  // Other type values (mut/reg/await) go through the same handler but
  // require no policy action.
  (void)owner;
  (void)lhs;
  (void)value_text;
}

}  // namespace attributes
}  // namespace upass

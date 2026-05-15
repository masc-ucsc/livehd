//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

// Phase 5 — category-A LNAST/upass attribute consumption.
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
// Narrowing math runs through tmp_fold (the same map Phase 2 publishes
// derived attribute reads to) so cassert / eq consumers pick up the
// narrowed value via runner_fold_fn. constprop's current_prim_value
// consults runner_fold_fn before falling back to its own ST when no
// stored value exists; we don't need to fight constprop for primary
// binding.

#include "upass_attributes_phase5.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "const.hpp"
#include "upass_attributes.hpp"
#include "upass_utils.hpp"

namespace {

bool is_truthy(std::string_view v) {
  if (v.empty() || v == "true") {
    return true;
  }
  return v != "false" && v != "0";
}

// Wrap (modulo) and saturate clamps for the given numeric type. n==0 means
// the type's bit width is unknown and no narrowing is possible.
Const wrap_to_unsigned(const Const& v, uint32_t n) {
  if (n == 0 || v.is_invalid() || v.is_string() || v.has_unknowns() || v.is_nil()) {
    return v;
  }
  // Build a 2^n - 1 mask and apply.
  Const mask;
  mask = Dlop::get_mask_value(n);
  return *v.and_op(mask);
}

Const wrap_to_signed(const Const& v, uint32_t n) {
  if (n == 0 || v.is_invalid() || v.is_string() || v.has_unknowns() || v.is_nil()) {
    return v;
  }
  // Two's-complement wrap to n bits: take low n bits, then sign-extend if
  // the n-th-1 bit is set. Const's adjust_bits already implements this for
  // signed widths.
  return *v.adjust_bits(n);
}

Const saturate_unsigned(const Const& v, uint32_t n) {
  if (n == 0 || v.is_invalid() || v.is_string() || v.has_unknowns() || v.is_nil()) {
    return v;
  }
  Const lo;
  lo = Dlop::create_integer(0);
  Const hi;
  hi = Dlop::get_mask_value(n);
  if (v.is_negative()) {
    return lo;
  }
  // v > hi — compare via subtraction sign.
  if (v.sub_op(hi)->is_positive() && !v.same_repr(hi)) {
    return hi;
  }
  return v;
}

Const saturate_signed(const Const& v, uint32_t n) {
  if (n == 0 || v.is_invalid() || v.is_string() || v.has_unknowns() || v.is_nil()) {
    return v;
  }
  Const hi;
  hi = Dlop::get_mask_value(n - 1);
  Const lo;
  lo = Dlop::get_neg_mask_value(n - 1);
  if (v.sub_op(hi)->is_positive() && !v.same_repr(hi)) {
    return hi;
  }
  if (lo.sub_op(v)->is_positive() && !v.same_repr(lo)) {
    return lo;
  }
  return v;
}

}  // namespace

Const uPass_attributes::narrow_for_lhs(std::string_view lhs, const Const& v) const {
  const bool wrap = has_wrap_policy(lhs);
  const bool sat  = has_sat_policy(lhs);
  if (!wrap && !sat) {
    return v;
  }
  const auto* ti = lookup_type_info(lhs);
  if (ti == nullptr || ti->bits == 0) {
    return v;
  }
  const bool is_signed = ti->kind == Numeric_kind::signed_int;
  if (wrap) {
    return is_signed ? wrap_to_signed(v, ti->bits) : wrap_to_unsigned(v, ti->bits);
  }
  return is_signed ? saturate_signed(v, ti->bits) : saturate_unsigned(v, ti->bits);
}

void uPass_attributes::apply_narrowing(std::string_view lhs, bool is_wrap, bool is_sat) {
  if (!is_wrap && !is_sat) {
    return;
  }
  const auto* ti = lookup_type_info(lhs);
  if (ti == nullptr || ti->bits == 0) {
    return;
  }
  // Resolve the current value: tmp_fold first (so chained narrowings see
  // the previous narrowing), then runner_fold_fn (constprop's ST + every
  // other pass's fold contribution).
  std::optional<Const> cur;
  auto                 it = tmp_fold.find(std::string{lhs});
  if (it != tmp_fold.end() && !it->second.is_invalid()) {
    cur = it->second;
  } else if (runner_fold_fn) {
    auto v = runner_fold_fn(lhs);
    if (v && !v->is_invalid()) {
      cur = *v;
    }
  }
  if (!cur) {
    return;
  }
  const bool is_signed = ti->kind == Numeric_kind::signed_int;
  Const      out;
  if (is_wrap) {
    out = is_signed ? wrap_to_signed(*cur, ti->bits) : wrap_to_unsigned(*cur, ti->bits);
  } else {
    out = is_signed ? saturate_signed(*cur, ti->bits) : saturate_unsigned(*cur, ti->bits);
  }
  if (out.is_invalid() || out.same_repr(*cur)) {
    return;
  }
  auto [iter, inserted] = tmp_fold.emplace(std::string{lhs}, out);
  if (!inserted && !iter->second.same_repr(out)) {
    iter->second = out;
    mark_changed();
  } else if (inserted) {
    mark_changed();
  }
}

void uPass_attributes::record_assign(std::string_view lhs, bool rhs_is_nil) {
  if (lhs.empty()) {
    return;
  }
  if (rhs_is_nil) {
    return;  // nil invalidations don't count as a real binding (per spec)
  }
  // Track that the var has been assigned at least once. Used by Phase 5 to
  // distinguish declaration-site `:[wrap]` (attr_set BEFORE first assign)
  // from statement-level `wrap x = ...` (attr_set AFTER an assign).
  assigned_once.emplace(std::string{lhs});

  // Const single-bind enforcement.
  const auto* ti = lookup_type_info(lhs);
  if (ti != nullptr && ti->decl == Decl_kind::const_kind) {
    auto& count = const_assign_count[std::string{lhs}];
    ++count;
    if (count > 1) {
      upass::error("uPass_attributes: const `{}` rebind (assigned {} times)\n", lhs, count);
    }
  }

  // If a wrap/sat policy is in effect for this lhs, narrow the value the
  // moment it's stored so later reads via runner_fold_fn pick up the
  // narrowed result.
  if (has_wrap_policy(lhs)) {
    apply_narrowing(lhs, /*wrap=*/true, /*sat=*/false);
  } else if (has_sat_policy(lhs)) {
    apply_narrowing(lhs, /*wrap=*/false, /*sat=*/true);
  }
}

namespace upass {
namespace attributes {

void Wrap_sat_handler::on_attr_set(uPass_attributes& owner, std::string_view lhs, std::string_view value_text) {
  const bool         active  = is_truthy(value_text);
  const std::string& bucket  = owner.current_dispatch_bucket();
  const bool         is_wrap = (bucket == "wrap");
  const bool         is_sat  = (bucket == "saturate") || (bucket == "sat");
  if (!is_wrap && !is_sat) {
    return;
  }

  if (!active) {
    return;  // explicit false: present-but-inactive, no policy, no narrowing.
  }

  if (!owner.was_assigned(lhs)) {
    // Declaration-site form: persist as policy. Future assigns to lhs go
    // through narrowing in record_assign. The presence-true value is
    // already stored in attr_set_values by the dispatcher's caller, so
    // .[wrap] / .[saturate] reads return true.
    if (is_wrap) {
      owner.set_wrap_policy(lhs);
    } else {
      owner.set_sat_policy(lhs);
    }
    return;
  }

  // Statement-level form: narrow the in-flight value once and don't leave
  // a sticky policy. The dispatcher's caller already wrote the attr value
  // to attr_set_values; remove every wrap/saturate alias for the target so
  // a follow-up `.[wrap]` / `.[saturate]` read does not see the
  // pre-recorded `true`.
  owner.apply_narrowing(lhs, is_wrap, is_sat);
  owner.erase_attr_value(lhs, "wrap");
  owner.erase_attr_value(lhs, "saturate");
  owner.erase_attr_value(lhs, "sat");
}

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

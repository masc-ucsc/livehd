//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "upass_attributes_sticky.hpp"

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>

#include "absl/container/inlined_vector.h"
#include "upass_attributes.hpp"

namespace upass {
namespace attributes {

bool Sticky_handler::is_sticky_name(std::string_view name) {
  // `debug` and any name beginning with `_` (`_debug`, `_foo`, …).
  if (name == "debug") {
    return true;
  }
  return !name.empty() && name.front() == '_';
}

std::string Sticky_handler::canonical_bucket(std::string_view name) {
  // `debug` and `_debug` share a bucket; everything else keeps its own.
  if (name == "debug" || name == "_debug") {
    return "_debug";
  }
  return std::string{name};
}

void Sticky_handler::begin_iteration(uPass_attributes& owner) {
  // Sticky state is monotonic per attribute_todo.md §Phase 1 — it lives on
  // the bindings (canonical `_`-named residual attrs), so nothing to clear.
  // Only the per-arm control taint stack resets here.
  st_ = owner.shared_table();
  control_taint_stack.clear();
}

void Sticky_handler::end_run(uPass_attributes& /*owner*/) {}

namespace {
// Decide whether an attr_set value text should mark the target sticky.
// Presence-only (empty value) and any non-`false`/non-`0` literal mark
// sticky-active. `false` / `0` is present-but-inactive — we record nothing
// here and let Phase 2's read evaluate it as boolean false.
bool value_is_truthy(std::string_view v) {
  if (v.empty()) {
    return true;
  }
  std::string lower;
  lower.reserve(v.size());
  for (char c : v) {
    lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
  }
  if (lower == "false" || lower == "0") {
    return false;
  }
  return true;
}
}  // namespace

void Sticky_handler::on_attr_set(uPass_attributes& owner, std::string_view lhs, std::string_view value_text) {
  // The dispatcher already routed by name, so we know the attr matches the
  // sticky pattern. Look up which bucket via owner's last-attr-name context.
  // Here we mark using the bucket recorded on the dispatcher's behalf.
  //
  // We rely on owner publishing the canonical bucket for the in-flight
  // attr_set so the handler doesn't need to re-derive it.
  if (!value_is_truthy(value_text)) {
    return;  // present-but-inactive; do not mark sticky-active
  }
  mark(lhs, owner.current_dispatch_bucket());
}

void Sticky_handler::on_attr_get(uPass_attributes& owner, std::string_view dst, std::string_view base) {
  // Per attributes_spec §Phase 1: "If a sticky value can affect another
  // value Z in any way, then Z inherits that sticky attr." `tmp = base.[…]`
  // makes `tmp` depend on the whole of `base`, so every sticky bucket
  // already on `base` flows through. Without this, `if a.[_foo] { … }` —
  // which lowers to `tmp = a.[_foo]; if tmp { … }` — would carry only
  // `_foo` into the arm's control taint and lose `_debug` (and any other
  // sticky `_*` bucket on `a`).
  merge_from(dst, base);
  // The dispatched bucket (the attr name being read) is also marked
  // explicitly: a read can be the first surface where that bucket
  // appears on `dst`, independent of whether `base` carries it yet.
  const auto& bucket = owner.current_dispatch_bucket();
  if (!bucket.empty()) {
    mark(dst, bucket);
  }
  // Control taint from enclosing if-arms still applies.
  if (!control_taint_stack.empty()) {
    for (const auto& b : active_control_taint()) {
      mark(dst, b);
    }
  }
}

void Sticky_handler::on_alias_assign(uPass_attributes& /*owner*/, std::string_view lhs, std::string_view rhs) {
  // Fast path: with empty sticky state and no control taint there is
  // nothing to merge or mark. Returning here skips the find() probe on
  // every alias-assign visit (1M+ on the bulk-arithmetic xx.prp run).
  if (is_inert()) {
    return;
  }
  merge_from(lhs, rhs);
  if (!control_taint_stack.empty()) {
    for (const auto& bucket : active_control_taint()) {
      mark(lhs, bucket);
    }
  }
}

void Sticky_handler::on_expr_assign(uPass_attributes& /*owner*/, std::string_view lhs, std::span<const std::string_view> rhs_refs) {
  // Fast path: same skip rationale as on_alias_assign — when sticky is
  // empty, merge_from returns immediately for every operand and the
  // control-taint block is also empty.
  if (is_inert()) {
    return;
  }
  for (auto rhs : rhs_refs) {
    merge_from(lhs, rhs);
  }
  if (!control_taint_stack.empty()) {
    for (const auto& bucket : active_control_taint()) {
      mark(lhs, bucket);
    }
  }
}

void Sticky_handler::on_if_arm_enter(uPass_attributes& /*owner*/, std::span<const std::string_view> cond_refs) {
  // Build the taint set for this arm: any sticky bucket already on a cond ref
  // var (covers `if tmp {…}` where tmp came from `tmp = a.[_foo]`). Current
  // LNAST always lowers attr_gets to tmps before the if, so the cond is a ref.
  // Carry forward parent taint so nested ifs accumulate (per spec: "Nested
  // conditions OR their sticky contexts").
  std::set<std::string> arm_taint = active_control_taint();
  for (auto ref : cond_refs) {
    for (const auto& b : buckets_of(ref)) {
      arm_taint.insert(b);
    }
  }
  control_taint_stack.push_back(std::move(arm_taint));
}

void Sticky_handler::on_if_arm_exit(uPass_attributes& /*owner*/) {
  if (!control_taint_stack.empty()) {
    control_taint_stack.pop_back();
  }
}

std::set<std::string> Sticky_handler::buckets_of(std::string_view var) const {
  std::set<std::string> out;
  if (st_ == nullptr || var.empty()) {
    return out;
  }
  const auto root  = Bundle::get_first_level(var);
  const auto field = Bundle::get_all_but_first_level(var);
  const auto b     = st_->get_bundle(root);
  if (!b) {
    return out;
  }
  // The sticky subset of the binding's residual attrs (canonical leading-`_`
  // names). For a dotted var, the per-field attrs key as "field.bucket".
  for (const auto& [key, entry] : b->sticky_attributes()) {
    (void)entry;
    const auto prefix = Bundle::get_all_but_last_level(key);
    if (prefix == field || (field.empty() && prefix.empty())) {
      out.emplace(Bundle::get_last_level(key));
    }
  }
  return out;
}

bool Sticky_handler::has_sticky(std::string_view var, std::string_view bucket) const {
  if (st_ == nullptr || var.empty()) {
    return false;
  }
  const auto root  = Bundle::get_first_level(var);
  const auto field = Bundle::get_all_but_first_level(var);
  const auto b     = st_->get_bundle(root);
  if (!b) {
    return false;
  }
  return field.empty() ? b->has_attr(bucket) : b->has_attr(field, bucket);
}

void Sticky_handler::mark(std::string_view var, std::string_view bucket) {
  if (var.empty() || bucket.empty() || st_ == nullptr) {
    return;
  }
  const auto root  = Bundle::get_first_level(var);
  const auto field = Bundle::get_all_but_first_level(var);
  auto       b     = st_->get_bundle_for_write(root);
  if (!b) {
    return;  // unbound name (e.g. a tmp before its producer): nothing to carry
  }
  // 2b/E3e — explicit attr VALUES share the canonical sticky key
  // (`debug="trace"` and the propagation marker are both "_debug"): marking
  // must never clobber a present value; presence IS marked.
  if (field.empty()) {
    if (b->get_attr(bucket).is_invalid()) {
      b->set_attr(bucket, *Dlop::create_integer(1));
    }
  } else {
    if (b->get_attr(field, bucket).is_invalid()) {
      b->set_attr(field, bucket, *Dlop::create_integer(1));
    }
  }
  any_marked_ = true;
}

void Sticky_handler::merge_from(std::string_view dst, std::string_view src) {
  for (const auto& b : buckets_of(src)) {
    mark(dst, b);
  }
}

std::set<std::string> Sticky_handler::active_control_taint() const {
  std::set<std::string> out;
  for (const auto& frame : control_taint_stack) {
    for (const auto& b : frame) {
      out.insert(b);
    }
  }
  return out;
}

}  // namespace attributes
}  // namespace upass

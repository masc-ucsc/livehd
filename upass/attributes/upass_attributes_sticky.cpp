//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "upass_attributes_sticky.hpp"

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>

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

void Sticky_handler::begin_iteration(uPass_attributes& /*owner*/) {
  // `acquired` is monotonic across iterations per attribute_todo.md §Phase 1
  // implementation note: a variable that earned a sticky attr in iteration N
  // still has it in iteration N+1, so the runner's whole-tree fixed point
  // doesn't unwind sticky propagation. Only the per-arm taint stack resets,
  // since the runner re-walks every if-arm each sweep.
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
  for (const auto& b : active_control_taint()) {
    mark(dst, b);
  }
}

void Sticky_handler::on_alias_assign(uPass_attributes& /*owner*/, std::string_view lhs, std::string_view rhs) {
  merge_from(lhs, rhs);
  for (const auto& bucket : active_control_taint()) {
    mark(lhs, bucket);
  }
}

void Sticky_handler::on_expr_assign(uPass_attributes& /*owner*/, std::string_view lhs,
                                    const std::vector<std::string_view>& rhs_refs) {
  for (auto rhs : rhs_refs) {
    merge_from(lhs, rhs);
  }
  for (const auto& bucket : active_control_taint()) {
    mark(lhs, bucket);
  }
}

void Sticky_handler::on_if_arm_enter(uPass_attributes& /*owner*/, const std::vector<std::string_view>& cond_refs,
                                     const std::vector<std::pair<std::string_view, std::string_view>>& cond_attr_reads) {
  // Build the taint set for this arm:
  //   - any sticky bucket already on a cond ref var (covers `if tmp {…}` where
  //     tmp came from `tmp = a.[_foo]`),
  //   - any sticky bucket named directly by an attr-read on the cond
  //     (`if a.[_foo] {…}` adds bucket `_foo` — kept for completeness even
  //     though current LNAST always lowers attr_gets to tmps before the if).
  // Carry forward parent taint so nested ifs accumulate (per spec: "Nested
  // conditions OR their sticky contexts").
  std::set<std::string> arm_taint = active_control_taint();
  for (auto ref : cond_refs) {
    auto it = acquired.find(std::string{ref});
    if (it != acquired.end()) {
      for (const auto& b : it->second) {
        arm_taint.insert(b);
      }
    }
  }
  for (const auto& [_v, attr] : cond_attr_reads) {
    if (Sticky_handler::is_sticky_name(attr)) {
      arm_taint.insert(canonical_bucket(attr));
    }
  }
  control_taint_stack.push_back(std::move(arm_taint));
}

void Sticky_handler::on_if_arm_exit(uPass_attributes& /*owner*/) {
  if (!control_taint_stack.empty()) {
    control_taint_stack.pop_back();
  }
}

bool Sticky_handler::has_sticky(std::string_view var, std::string_view bucket) const {
  auto it = acquired.find(std::string{var});
  if (it == acquired.end()) {
    return false;
  }
  return it->second.find(std::string{bucket}) != it->second.end();
}

void Sticky_handler::mark(std::string_view var, std::string_view bucket) {
  if (var.empty() || bucket.empty()) {
    return;
  }
  acquired[std::string{var}].insert(std::string{bucket});
}

void Sticky_handler::merge_from(std::string_view dst, std::string_view src) {
  auto it = acquired.find(std::string{src});
  if (it == acquired.end()) {
    return;
  }
  for (const auto& b : it->second) {
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

//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "upass_attributes_sticky.hpp"

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
  // TODO: decide whether to clear `acquired` between iterations. Sticky is
  // monotonic per program point, but the runner re-walks the tree on each
  // iteration; design will likely keep state across iterations.
  control_taint_stack.clear();
}

void Sticky_handler::end_run(uPass_attributes& /*owner*/) {
  // TODO: nothing to finalize for Phase 1 unless we want a debug dump.
}

void Sticky_handler::on_attr_set(uPass_attributes& /*owner*/, std::string_view lhs, std::string_view value_text) {
  // TODO: presence-only or `=true` → mark sticky on lhs.
  //       explicit `=false` → record presence-but-inactive (Phase 2 read
  //       semantics) without marking sticky-active.
  (void)lhs;
  (void)value_text;
}

void Sticky_handler::on_attr_get(uPass_attributes& /*owner*/, std::string_view /*lhs*/) {
  // No state change on a read; Phase 2 (constprop) folds the value at use.
  // Sticky control taint via `if a.[_foo] { ... }` is recorded via
  // on_if_arm_enter's cond_attr_reads, not here.
}

void Sticky_handler::on_alias_assign(uPass_attributes& /*owner*/, std::string_view lhs, std::string_view rhs) {
  // TODO: copy every sticky bucket from rhs onto lhs (full attr migration
  // happens at the dispatcher level for non-sticky attrs handled by other
  // handlers; here we only own sticky buckets).
  merge_from(lhs, rhs, /*only_sticky=*/false);
  // Apply control taint if any.
  for (const auto& bucket : active_control_taint()) {
    mark(lhs, bucket);
  }
}

void Sticky_handler::on_expr_assign(uPass_attributes& /*owner*/, std::string_view lhs,
                                    const std::vector<std::string_view>& rhs_refs) {
  // TODO: OR every rhs_ref's sticky buckets into lhs. Non-sticky attrs are
  // dropped at the dispatcher level — this handler only owns sticky buckets,
  // so the merge is sticky-only by construction.
  for (auto rhs : rhs_refs) {
    merge_from(lhs, rhs, /*only_sticky=*/true);
  }
  for (const auto& bucket : active_control_taint()) {
    mark(lhs, bucket);
  }
}

void Sticky_handler::on_if_arm_enter(uPass_attributes& /*owner*/,
                                     const std::vector<std::string_view>& cond_refs,
                                     const std::vector<std::pair<std::string_view, std::string_view>>& cond_attr_reads) {
  // Build the taint set for this arm:
  //   - any sticky bucket already on a cond ref var,
  //   - any sticky bucket named directly by an attr-read on the cond
  //     (`if a.[_foo] {…}` adds bucket `_foo`).
  std::set<std::string> arm_taint;
  for (auto ref : cond_refs) {
    auto it = acquired.find(std::string{ref});
    if (it != acquired.end()) {
      for (const auto& b : it->second) {
        arm_taint.insert(b);
      }
    }
  }
  for (auto [/*var*/ _v, attr] : cond_attr_reads) {
    if (Sticky_handler::is_sticky_name(attr)) {
      arm_taint.insert(canonical_bucket(attr));
    }
  }
  control_taint_stack.push_back(std::move(arm_taint));
}

void Sticky_handler::on_if_arm_exit(uPass_attributes& /*owner*/) {
  // OR-merge handled by the runner-level join (the parent uPass_attributes
  // stitches arms together). Here we just pop the per-arm taint frame.
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
  acquired[std::string{var}].insert(std::string{bucket});
}

void Sticky_handler::merge_from(std::string_view dst, std::string_view src, bool /*only_sticky*/) {
  // For Phase 1 every bucket this handler owns is sticky by definition, so
  // only_sticky is informational. The dispatcher uses the same flag to
  // decide whether non-sticky handlers' state should also migrate.
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

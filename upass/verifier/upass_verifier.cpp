//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "upass_verifier.hpp"

#include <charconv>
#include <print>
#include <string>
#include <string_view>

// Registered once here (not in the header) to avoid the registration being
// dropped when no TU in the final binary includes upass_verifier.hpp.
static upass::uPass_plugin plugin_verifier("verifier", upass::uPass_wrapper<uPass_verifier>::get_upass);

namespace {
// Helper — parse an integer option, tolerating empty / absent values.
// Returns -1 on empty or invalid (the sentinel meaning "don't check").
int parse_expected(const upass::Options_map& opts, std::string_view key) {
  auto it = opts.find(std::string{key});
  if (it == opts.end() || it->second.empty()) {
    return -1;
  }
  int         value = -1;
  const auto* begin = it->second.data();
  const auto* end   = begin + it->second.size();
  auto        r     = std::from_chars(begin, end, value);
  if (r.ec != std::errc() || r.ptr != end) {
    return -1;
  }
  return value;
}
}  // namespace

void uPass_verifier::set_options(const upass::Options_map& opts) {
  expected_pass = parse_expected(opts, "verifier_pass");
  expected_fail = parse_expected(opts, "verifier_fail");
}

upass::Emit_decision uPass_verifier::classify_statement() {
  if (!is_type(Lnast_ntype::Lnast_ntype_cassert)) {
    return upass::Emit_decision::emit_node();
  }

  // Cassert has a single child — a ref or a const (post-Slice-1 fold).
  // Resolve it through the runner's aggregated fold_ref so we see whatever
  // constprop (or any future pass) knows.
  std::optional<Lconst> val;
  std::string           operand_text;
  bool                  got_child = move_to_child();
  if (got_child) {
    operand_text = std::string{current_text()};
    if (is_type(Lnast_ntype::Lnast_ntype_const)) {
      val = Lconst::from_pyrope(current_text());
    } else if (is_type(Lnast_ntype::Lnast_ntype_ref) && runner_fold_fn) {
      val = runner_fold_fn(current_text());
    }
  }
  move_to_parent();

  const bool known = val && !val->is_invalid() && !val->has_unknowns();
  if (!known) {
    // Count as unknown so end_run can surface which cassert(s) the test
    // failed to resolve when expected counts don't match up.
    ++unknown_count;
    unknown_operands.emplace_back(operand_text);
    return upass::Emit_decision::emit_node();  // keep for runtime
  }

  if (val->is_known_false()) {
    // Count it and drop from the output — deferring the error to end_run
    // lets a test assert "I expect exactly N false casserts" via
    // `verifier_fail:N`. The end_run handler raises if the tally doesn't
    // match expectations, so misaligned expectations still make rc != 0.
    ++fail_count;
    std::print(stderr, "uPass - verifier comptime cassert failed (operand evaluated to {})\n", val->to_pyrope());
    return upass::Emit_decision::drop();
  }

  // Known true — discharge the assertion.
  ++pass_count;
  return upass::Emit_decision::drop();
}

void uPass_verifier::end_run() {
  std::print(stderr, "uPass - verifier cassert counts: pass:{} fail:{} unknown:{}\n", pass_count, fail_count,
             unknown_count);

  bool mismatch = false;
  if (expected_pass >= 0 && static_cast<int>(pass_count) != expected_pass) {
    std::print(stderr,
               "uPass - verifier expected verifier_pass:{} but saw pass:{}\n",
               expected_pass,
               pass_count);
    mismatch = true;
  }
  // When verifier_fail is set, treat it as the allowed count: mismatch is
  // an error. When unset, any fail is unexpected — match expected_fail==0
  // as the default so a naked run still catches false casserts.
  const int fail_allowed = expected_fail >= 0 ? expected_fail : 0;
  if (static_cast<int>(fail_count) != fail_allowed) {
    std::print(stderr,
               "uPass - verifier expected verifier_fail:{} but saw fail:{}\n",
               fail_allowed,
               fail_count);
    mismatch = true;
  }

  if (mismatch) {
    if (!unknown_operands.empty()) {
      std::print(stderr, "uPass - verifier unresolved cassert operand(s):");
      for (const auto& s : unknown_operands) {
        std::print(stderr, " {}", s);
      }
      std::print(stderr, "\n");
    }
    upass::error("verifier cassert count mismatch\n");
  }
}

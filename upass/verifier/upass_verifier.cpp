//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "upass_verifier.hpp"

#include <charconv>
#include <print>
#include <string>
#include <string_view>

// Registered once here (not in the header) to avoid the registration being
// dropped when no TU in the final binary includes upass_verifier.hpp.
static upass::uPass_plugin plugin_verifier("verifier", upass::uPass_wrapper<uPass_verifier>::get_upass);

std::size_t                     uPass_verifier::aggregate_pass_count    = 0;
std::size_t                     uPass_verifier::aggregate_fail_count    = 0;
std::size_t                     uPass_verifier::aggregate_unknown_count = 0;
std::size_t                     uPass_verifier::aggregate_cputs_count  = 0;
std::vector<std::string>        uPass_verifier::aggregate_unknown_operands;
int                             uPass_verifier::aggregate_expected_pass = -1;
int                             uPass_verifier::aggregate_expected_fail = -1;
std::unordered_set<std::string> uPass_verifier::processed_cassert_keys;
std::unordered_set<std::string> uPass_verifier::processed_cputs_keys;

void uPass_verifier::reset_aggregate() {
  aggregate_pass_count    = 0;
  aggregate_fail_count    = 0;
  aggregate_unknown_count = 0;
  aggregate_cputs_count   = 0;
  aggregate_unknown_operands.clear();
  aggregate_expected_pass = -1;
  aggregate_expected_fail = -1;
  processed_cassert_keys.clear();
  processed_cputs_keys.clear();
}

void uPass_verifier::set_aggregate_expected(int expected_pass, int expected_fail) {
  aggregate_expected_pass = expected_pass;
  aggregate_expected_fail = expected_fail;
}

bool uPass_verifier::already_counted_cassert(std::string_view key) { return processed_cassert_keys.contains(std::string{key}); }

void uPass_verifier::mark_inlined_cassert_pass(std::string_view key) {
  if (processed_cassert_keys.insert(std::string{key}).second) {
    ++aggregate_pass_count;
  }
}

void uPass_verifier::mark_inlined_cassert_fail(std::string_view key) {
  if (processed_cassert_keys.insert(std::string{key}).second) {
    ++aggregate_fail_count;
  }
}

upass::Emit_decision uPass_verifier::classify_statement() {
  if (is_type(Lnast_ntype::Lnast_ntype_func_call)) {
    return classify_func_call();
  }
  if (!is_type(Lnast_ntype::Lnast_ntype_cassert)) {
    return upass::Emit_decision::emit_node();
  }

  // If constprop's try_eval_comb_call already proved this cassert at every
  // call site of the enclosing function, drop it without re-counting. The
  // key matches what mark_inlined_cassert_* uses (lnast top-name + nid).
  const auto cassert_nid = lm->get_current_nid();
  // class_index().value is unique per node within a tree, so module-qualifying
  // it gives a stable cassert key without paying for a parent walk.
  const auto dedup_key = std::format("{}:{}", lm->get_top_module_name(), cassert_nid.get_class_index().value);
  if (processed_cassert_keys.contains(dedup_key)) {
    return upass::Emit_decision::drop();
  }

  // Cassert has a single child — a ref or a const (post-Slice-1 fold).
  // Resolve it through the runner's aggregated fold_ref so we see whatever
  // constprop (or any future pass) knows.
  std::optional<Const> val;
  std::string          operand_text;
  bool                 got_child = move_to_child();
  if (got_child) {
    operand_text = std::string{current_text()};
    if (is_type(Lnast_ntype::Lnast_ntype_const)) {
      val = *Dlop::from_pyrope(current_text());
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

  // Comptime-resolved nil discharges the cassert per attributes_spec §Phase 2.
  // An unset attribute reads as nil (`b.[unset]`, `!b.[unset]`, or any
  // expression that propagated a nil through log_not / log_and). The user's
  // assertion is structurally "this attribute has the expected (un)set state";
  // resolving to nil means the upass observed no contradiction at compile time,
  // so we treat it as a pass rather than a failure. Must come before
  // `is_known_false`, since Type::Nil's empty bit-pattern is also `known_false`.
  if (val->is_nil()) {
    ++pass_count;
    return upass::Emit_decision::drop();
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

// Strip the surrounding single-quote wrappers that Lconst::to_pyrope adds
// around string values. Mirrors strip_pyrope_quotes in upass_constprop.hpp
// (kept inline here to avoid a header dependency on constprop).
static std::string strip_pyrope_quotes(std::string s) {
  if (s.size() >= 2 && s.front() == '\'' && s.back() == '\'') {
    s = s.substr(1, s.size() - 2);
  }
  return s;
}

upass::Emit_decision uPass_verifier::classify_func_call() {
  // Layout: func_call ref(___tmp) ref(<fname>) <arg0> <arg1> ...
  // Inspect the function-name child; if it isn't "cputs", restore the
  // cursor and let the runner emit the node unchanged.
  if (!move_to_child()) {
    return upass::Emit_decision::emit_node();
  }
  // First child is the dst tmp ref — skip past it.
  if (!move_to_sibling()) {
    move_to_parent();
    return upass::Emit_decision::emit_node();
  }
  if (!is_type(Lnast_ntype::Lnast_ntype_ref)) {
    move_to_parent();
    return upass::Emit_decision::emit_node();
  }
  const auto fname = std::string{current_text()};
  if (fname != "cputs") {
    move_to_parent();
    return upass::Emit_decision::emit_node();
  }

  // From here on this is a cputs call — any deviation from the expected
  // shape is a compile error rather than "leave it for runtime".

  // Dedup: a cputs inside a function body would otherwise reprint once
  // per spawned-lnast walk. Key matches the cassert dedup pattern.
  const auto fcall_nid = lm->get_current_nid();
  const auto dedup_key
      = std::format("{}:{}", lm->get_top_module_name(), fcall_nid.get_class_index().value);
  move_to_parent();  // restore to func_call before re-entering
  if (processed_cputs_keys.contains(dedup_key)) {
    return upass::Emit_decision::drop();
  }

  // Re-descend to read the single argument.
  if (!move_to_child()) {
    upass::error("cputs malformed func_call (no dst)\n");
  }
  if (!move_to_sibling()) {
    upass::error("cputs malformed func_call (no fname)\n");
  }
  if (!move_to_sibling()) {
    upass::error("cputs requires exactly one argument\n");
  }

  std::optional<Const> val;
  if (is_type(Lnast_ntype::Lnast_ntype_const)) {
    val = *Dlop::from_pyrope(current_text());
  } else if (is_type(Lnast_ntype::Lnast_ntype_ref) && runner_fold_fn) {
    val = runner_fold_fn(current_text());
  }

  // Reject named or multi-argument forms in this slice. A trailing
  // sibling means the user passed more than one operand.
  const bool extra_args = !is_last_child();
  move_to_parent();

  if (extra_args) {
    upass::error("cputs accepts exactly one argument\n");
  }
  if (!val || val->is_invalid() || val->has_unknowns()) {
    upass::error("cputs operand not comptime-known\n");
  }
  if (!val->is_string()) {
    upass::error("cputs operand must be a string\n");
  }

  processed_cputs_keys.insert(dedup_key);
  ++cputs_count;
  std::print(stderr, "\nprp:{}\n", strip_pyrope_quotes(val->to_pyrope()));
  return upass::Emit_decision::drop();
}

void uPass_verifier::end_run() {
  std::print(stderr,
             "uPass - verifier cassert counts: pass:{} fail:{} unknown:{} cputs:{}\n",
             pass_count,
             fail_count,
             unknown_count,
             cputs_count);

  // Roll local counts into the test-level aggregate. The mismatch check
  // moved to finalize_aggregate so it runs once across the whole program
  // (top lnast + any lnasts spawned by func_extract).
  aggregate_pass_count += pass_count;
  aggregate_fail_count += fail_count;
  aggregate_unknown_count += unknown_count;
  aggregate_cputs_count += cputs_count;
  for (auto& s : unknown_operands) {
    aggregate_unknown_operands.emplace_back(std::move(s));
  }
  unknown_operands.clear();
}

void uPass_verifier::finalize_aggregate() {
  std::print(stderr,
             "uPass - verifier aggregate cassert counts: pass:{} fail:{} unknown:{} cputs:{}\n",
             aggregate_pass_count,
             aggregate_fail_count,
             aggregate_unknown_count,
             aggregate_cputs_count);

  bool mismatch = false;
  if (aggregate_expected_pass >= 0 && static_cast<int>(aggregate_pass_count) != aggregate_expected_pass) {
    std::print(stderr,
               "uPass - verifier expected verifier_pass:{} but saw pass:{}\n",
               aggregate_expected_pass,
               aggregate_pass_count);
    mismatch = true;
  }
  // When verifier_fail is set, treat it as the allowed count: mismatch is
  // an error. When unset, any fail is unexpected — match expected_fail==0
  // as the default so a naked run still catches false casserts.
  const int fail_allowed = aggregate_expected_fail >= 0 ? aggregate_expected_fail : 0;
  if (static_cast<int>(aggregate_fail_count) != fail_allowed) {
    std::print(stderr, "uPass - verifier expected verifier_fail:{} but saw fail:{}\n", fail_allowed, aggregate_fail_count);
    mismatch = true;
  }

  if (mismatch) {
    if (!aggregate_unknown_operands.empty()) {
      std::print(stderr, "uPass - verifier unresolved cassert operand(s):");
      for (const auto& s : aggregate_unknown_operands) {
        std::print(stderr, " {}", s);
      }
      std::print(stderr, "\n");
    }
    upass::error("verifier cassert count mismatch\n");
  }
}

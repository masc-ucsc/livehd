//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "upass_bitwidth.hpp"

#include <algorithm>
#include <bit>
#include <charconv>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "diag.hpp"
#include "lnast.hpp"
#include "lnast_ntype.hpp"
#include "pass.hpp"

// ── Plugin registration ───────────────────────────────────────────────────────
// The static initializer fires at program startup so the runner discovers
// "bitwidth" via uPass_plugin::get_registry() without any explicit wiring.
static upass::uPass_plugin plugin_bitwidth("bitwidth", upass::uPass_wrapper<uPass_bitwidth>::get_upass);

static constexpr std::string_view call_ref_arg_marker = "__ref_arg";

namespace {
std::string_view ssa_base_name(std::string_view name) {
  const auto pos = name.find("___ssa_");
  if (pos == std::string_view::npos) {
    return name;
  }
  return name.substr(0, pos);
}

int64_t storage_bits_for_env(const Lnast_range& env) {
  if (env.is_unbounded()) {
    return 64;
  }
  if (env.min >= 0) {
    return env.max == 0 ? 0 : static_cast<int64_t>(std::bit_width(static_cast<uint64_t>(env.max)));
  }
  return env.get_sbits();
}

bool mask_touches_outside_bits(int64_t mask, int64_t storage_bits) {
  if (mask <= 0) {
    return false;
  }
  if (storage_bits <= 0) {
    return mask != 0;
  }
  if (storage_bits >= 63) {
    return false;
  }
  return (static_cast<uint64_t>(mask) >> storage_bits) != 0;
}
}  // namespace

// ── Constructor ──────────────────────────────────────────────────────────────

uPass_bitwidth::uPass_bitwidth(std::shared_ptr<upass::Lnast_manager>& lm) : upass::uPass(lm) {}

// ── Lifecycle ────────────────────────────────────────────────────────────────

void uPass_bitwidth::begin_iteration() {
  // Per-run setup hook (the runner does a single walk — no fixed-point loop).
  // Cross-invocation persistence: a fresh uPass_bitwidth instance starts with
  // an empty range_map_, but a previous pass.upass invocation may have left
  // ranges in lnast->bw_meta(). Seed range_map_ from bw_meta so a later
  // invocation can tighten what was unbounded before. Only seed once (when
  // range_map_ is empty); a re-entry with state already present leaves it be.
  if (range_map_.empty()) {
    const auto& meta = lm->get_lnast()->bw_meta();
    for (const auto& [name, e] : meta.ranges) {
      Lnast_range r;
      r.min       = e.min;
      r.max       = e.max;
      r.unbounded = e.unbounded;
      range_map_.emplace(name, r);
    }
  }
}

void uPass_bitwidth::end_run() {
  if (pending_overflow_msg_) {
    throw std::runtime_error(*pending_overflow_msg_);
  }

  // ── Goal 1n N4 — overflow capture (centralized in bitwidth) ────────────────
  // For each declared envelope, if the var's final value range provably escapes
  // it and the var carries no wrap/sat policy, it is a "does not fit" compile
  // error. Deferring to end_run (rather than checking at each store) lets a
  // per-statement `wrap`/`sat` marker emitted AFTER its store still exempt the
  // var. Semantics mirror the type model: an UNSIGNED envelope only flags a
  // value provably ABOVE max (a negative value is a legal unsigned reinterpret /
  // "force", not overflow); a SIGNED envelope is strict containment. Sorted
  // iteration so the first reported overflow is deterministic.
  {
    std::vector<std::string_view> names;
    names.reserve(decl_envelope_.size());
    for (const auto& [var, env] : decl_envelope_) {
      names.emplace_back(var);
    }
    std::sort(names.begin(), names.end());
    for (const auto& var : names) {
      // Scalars only: a dotted name is a tuple FIELD (e.g. `ar.y`, or an inlined
      // `inl1_ar.y`). Per-field envelope checking needs the comp_type_tuple
      // per-field types and the call-boundary cast semantics (binding an
      // out-of-range value to a param field is not necessarily an error) — that
      // is a future refinement (ties to 1t per-field types). Skip dotted names
      // so a field binding never trips the scalar overflow check.
      if (var.find('.') != std::string_view::npos) {
        continue;
      }
      auto it = range_map_.find(var);
      if (it == range_map_.end() || it->second.is_unbounded()) {
        continue;  // no tracked value / not provable
      }
      const Lnast_range& val             = it->second;
      const Lnast_range& env             = decl_envelope_.find(var)->second;
      const bool         is_unsigned_env = !env.is_signed();  // env.min >= 0
      // Does-not-fit on the ACTUAL inferred range: positive overflow (above
      // max) OR underflow (the range can dip below the declared min — for an
      // unsigned type that means it can go negative). `wrap`/`sat`/cast is the
      // remedy. The signed path's `contains` already checks both bounds.
      const bool         over            = is_unsigned_env ? (val.min > env.max || val.min < env.min) : !env.contains(val);
      if (over) {
        report_overflow(var, val, env);
      }
    }
  }

  // Flush range_map_ into lnast->bw_meta() so lnast_to_lgraph can read them.
  auto& meta = lm->get_lnast()->bw_meta();
  meta.ranges.clear();
  for (const auto& [name, rng] : range_map_) {
    BitwidthEntry e;
    e.min       = rng.min;
    e.max       = rng.max;
    e.unbounded = rng.unbounded;
    meta.ranges.emplace(name, e);
  }
}

// ── Helpers ───────────────────────────────────────────────────────────────────

Lnast_range uPass_bitwidth::read_range(std::string_view name) const {
  if (name.empty()) {
    return Lnast_range::make_unbounded();
  }
  // Heterogeneous lookup: absl::flat_hash_map's transparent hashing accepts
  // string_view without allocating a temporary std::string on every read.
  auto it = range_map_.find(name);
  if (it != range_map_.end()) {
    return it->second;
  }
  return Lnast_range::make_unbounded();
}

void uPass_bitwidth::set_range(std::string_view name, Lnast_range r) {
  if (name.empty()) {
    return;
  }
  check_declared_fit(name, r);
  // Probe with the string_view first to skip allocation on the common
  // already-present path. Only materialize a std::string when we insert.
  if (auto it = range_map_.find(name); it != range_map_.end()) {
    if (r.is_narrower_than(it->second)) {
      it->second = r;
    }
    return;
  }
  range_map_.emplace(std::string{name}, r);
}

void uPass_bitwidth::replace_range(std::string_view name, Lnast_range r) {
  if (name.empty()) {
    return;
  }
  check_declared_fit(name, r);
  auto it = range_map_.find(name);
  if (it != range_map_.end()) {
    const Lnast_range& cur = it->second;
    if (cur.min != r.min || cur.max != r.max || cur.unbounded != r.unbounded) {
      it->second = r;
    }
    return;
  }
  range_map_.emplace(std::string{name}, r);
}

// Try to parse a Pyrope constant text to int64_t.  Handles decimal,
// "0x..." (hex), "0b..." (binary).  Returns Lnast_range::make_unbounded() on
// failure (e.g. Pyrope unknown-bits constants like "0sb1?").
static Lnast_range range_from_const_text(std::string_view text) {
  if (text.empty()) {
    return Lnast_range::make_unbounded();
  }
  // Strip a leading minus sign.
  bool negative = (text[0] == '-');
  if (negative) {
    text.remove_prefix(1);
  }
  if (text.empty()) {
    return Lnast_range::make_unbounded();
  }

  uint64_t uval   = 0;
  bool     parsed = false;
  if (text.size() >= 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
    auto res = std::from_chars(text.data() + 2, text.data() + text.size(), uval, 16);
    parsed   = (res.ec == std::errc{} && res.ptr == text.data() + text.size());
  } else if (text.size() >= 2 && text[0] == '0' && (text[1] == 'b' || text[1] == 'B')) {
    auto res = std::from_chars(text.data() + 2, text.data() + text.size(), uval, 2);
    parsed   = (res.ec == std::errc{} && res.ptr == text.data() + text.size());
  } else {
    int64_t sval = 0;
    auto    res  = std::from_chars(text.data(), text.data() + text.size(), sval);
    if (res.ec == std::errc{} && res.ptr == text.data() + text.size()) {
      return Lnast_range::constant(negative ? -sval : sval);
    }
    return Lnast_range::make_unbounded();
  }

  if (!parsed) {
    return Lnast_range::make_unbounded();
  }
  // Unsigned value: fit into int64_t or give up.
  if (uval > static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) {
    return Lnast_range::make_unbounded();
  }
  int64_t val = static_cast<int64_t>(uval);
  return Lnast_range::constant(negative ? -val : val);
}

void uPass_bitwidth::clear_range(std::string_view name) {
  if (name.empty()) {
    return;
  }
  // Heterogeneous erase — no temporary std::string for the lookup.
  range_map_.erase(name);
}

void uPass_bitwidth::check_declared_fit(std::string_view name, const Lnast_range& r) {
  if (name.empty()) {
    return;
  }
  const std::string_view base = ssa_base_name(name);
  if (wrap_sat_exempt_.erase(name) != 0 || (base != name && wrap_sat_exempt_.erase(base) != 0)) {
    return;
  }
  if (base.find('.') != std::string_view::npos || r.is_unbounded()) {
    return;
  }
  const auto it = decl_envelope_.find(base);
  if (it == decl_envelope_.end()) {
    return;
  }

  const Lnast_range& env             = it->second;
  const bool         is_unsigned_env = !env.is_signed();
  const bool         over            = is_unsigned_env ? (r.min > env.max || r.min < env.min) : !env.contains(r);
  if (over) {
    record_overflow(base, r, env);
  }
}

void uPass_bitwidth::note_write_site(std::string_view name) {
  if (name.empty()) {
    return;
  }
  const auto& ln = lm->get_lnast();
  if (!ln) {
    return;
  }
  const auto nid = lm->get_current_nid();
  const auto loc = ln->get_loc(nid);
  if (loc.line == 0) {
    return;  // store carries no span (synthesized by SSA/inliner) — keep the last located one
  }
  livehd::diag::Span span;
  span.start_line = loc.line;
  if (auto fn = ln->get_fname(nid); !fn.empty()) {
    span.file = std::string{fn};
  }
  write_site_[std::string(ssa_base_name(name))] = std::move(span);
}

void uPass_bitwidth::record_overflow(std::string_view name, const Lnast_range& value, const Lnast_range& env) {
  auto msg = std::format("`{}` (value {}) does not fit its declared range [{}, {}]", name, value.min, env.min, env.max);
  if (!pending_overflow_msg_) {
    pending_overflow_msg_ = msg;
  }
  // The offending write's span, when the store carried one (the declare/store
  // loc-carry chain). Walk-time and end_run emissions resolve to the SAME span
  // through write_site_, so the sink's (code, span, message) dedup collapses
  // the double report into one located record.
  livehd::diag::Span span;
  if (auto it = write_site_.find(name); it != write_site_.end()) {
    span = it->second;
  }
  // Mirror upass::error: emit the structured diagnostic (so the JSONL /
  // error-test harness sees it). process_* exceptions are caught by the
  // runner, so end_run throws pending_overflow_msg_ on the non-swallowed path.
  // category=bitwidth (a source error the user must fix), not internal.
  livehd::diag::sink().emit(livehd::diag::Diagnostic{
      .severity = livehd::diag::Severity::error,
      .code     = "bitwidth-overflow",
      .category = "bitwidth",
      .pass     = "upass.bitwidth",
      .message  = msg,
      .span     = std::move(span),
      .hint     = "widen the declared type, force the bits with a bit-select (e.g. `x#[0..]`), or "
                  "apply a wrap/saturate policy",
  });
}

void uPass_bitwidth::report_overflow(std::string_view name, const Lnast_range& value, const Lnast_range& env) {
  record_overflow(name, value, env);
  const auto msg = *pending_overflow_msg_;
  throw std::runtime_error(msg);
}

uPass_bitwidth::Op_ranges uPass_bitwidth::scan_op() {
  Op_ranges out;

  if (!move_to_child()) {
    return out;
  }
  out.lhs = current_text();

  while (move_to_sibling()) {
    auto t = get_raw_ntype();
    if (Lnast_ntype::is_ref(t)) {
      out.rhs.push_back(read_range(current_text()));
    } else if (Lnast_ntype::is_const(t)) {
      out.rhs.push_back(range_from_const_text(current_text()));
    } else {
      out.rhs.push_back(Lnast_range::make_unbounded());
    }
  }
  move_to_parent();
  return out;
}

std::string uPass_bitwidth::scan_lhs_only() {
  if (!move_to_child()) {
    return {};
  }
  std::string lhs{current_text()};
  move_to_parent();
  return lhs;
}

// ── Process hooks ─────────────────────────────────────────────────────────────
//
// Pattern for assign / arithmetic ops:
//   1. scan_op() — consume children, get lhs + rhs ranges.
//   2. Compute result range.
//   3. set_range(lhs, result).
//
// Pattern for boolean / comparison ops:
//   result is always Lnast_range::boolean() regardless of inputs.
//
// Pattern for unary ops (bit_not, log_not, red_*):
//   one RHS operand; result is boolean for logical/reductions,
//   or join/negation for bitwise.

void uPass_bitwidth::process_assign() {
  auto [lhs, rhs] = scan_op();
  // Remember the write's source span (cursor is back on the store node after
  // scan_op) BEFORE the envelope check below can fire record_overflow.
  note_write_site(lhs);
  // Direct assignment to a mut var must REPLACE the lhs range, not narrow it.
  // set_range's monotonic-narrow policy is meant for iterative refinement of a
  // single producer's tmp; for mut reassignment (`v1 = 0` then `v1 = 0sb?`)
  // narrowing leaves a stale `{0,0}` range and fold_ref hands back the stale
  // constant to constprop's reads.
  replace_range(lhs, rhs.empty() ? Lnast_range::make_unbounded() : rhs[0]);
}

void uPass_bitwidth::process_func_call() {
  if (!move_to_child()) {
    return;
  }

  clear_range(current_text());  // call result is unknown unless another pass proves it
  if (!move_to_sibling()) {
    move_to_parent();
    return;
  }

  // Task 1t — a `wrap`/`sat` narrowing call. The `type=` arg names the target
  // var whose declared envelope is intentionally overflowed; exempt it from
  // the end_run does-not-fit check.
  const auto callee      = current_text();
  const bool is_wrap_sat = callee == "wrap" || callee == "sat" || callee == "saturate";

  while (move_to_sibling()) {
    if (!is_type(Lnast_ntype::Lnast_ntype_store)) {
      continue;
    }
    if (!move_to_child()) {
      continue;
    }
    const bool is_ref_arg  = current_text() == call_ref_arg_marker;
    const bool is_type_arg = is_wrap_sat && current_text() == "type";
    if (is_ref_arg && move_to_sibling() && is_type(Lnast_ntype::Lnast_ntype_ref)) {
      clear_range(current_text());
    } else if (is_type_arg && move_to_sibling() && is_type(Lnast_ntype::Lnast_ntype_ref)) {
      wrap_sat_exempt_.insert(std::string{current_text()});
    }
    move_to_parent();
  }

  move_to_parent();
}

void uPass_bitwidth::process_plus() {
  auto [lhs, rhs] = scan_op();
  if (rhs.empty()) {
    set_range(lhs, Lnast_range::make_unbounded());
    return;
  }
  Lnast_range result = rhs[0];
  for (std::size_t i = 1; i < rhs.size(); ++i) {
    result = result.add(rhs[i]);
  }
  set_range(lhs, result);
}

void uPass_bitwidth::process_minus() {
  auto [lhs, rhs] = scan_op();
  if (rhs.size() < 2) {
    set_range(lhs, Lnast_range::make_unbounded());
    return;
  }
  Lnast_range result = rhs[0].sub(rhs[1]);
  set_range(lhs, result);
}

void uPass_bitwidth::process_mult() {
  auto [lhs, rhs] = scan_op();
  if (rhs.empty()) {
    set_range(lhs, Lnast_range::make_unbounded());
    return;
  }
  Lnast_range result = rhs[0];
  for (std::size_t i = 1; i < rhs.size(); ++i) {
    result = result.mul(rhs[i]);
  }
  set_range(lhs, result);
}

void uPass_bitwidth::process_div() {
  // |a / d| <= |a| for any integer |d| >= 1 (Goal 1n N5).
  auto [lhs, rhs] = scan_op();
  if (rhs.size() < 2) {
    set_range(lhs, Lnast_range::make_unbounded());
    return;
  }
  set_range(lhs, rhs[0].div(rhs[1]));
}

void uPass_bitwidth::process_mod() {
  // |a % d| < |d| and <= |a|; sign follows the dividend (Goal 1n N5).
  auto [lhs, rhs] = scan_op();
  if (rhs.size() < 2) {
    set_range(lhs, Lnast_range::make_unbounded());
    return;
  }
  set_range(lhs, rhs[0].mod(rhs[1]));
}

void uPass_bitwidth::process_shl() {
  auto [lhs, rhs] = scan_op();
  if (rhs.size() < 2) {
    set_range(lhs, Lnast_range::make_unbounded());
    return;
  }
  set_range(lhs, rhs[0].shl(rhs[1]));
}

void uPass_bitwidth::process_sra() {
  auto [lhs, rhs] = scan_op();
  if (rhs.size() < 2) {
    set_range(lhs, Lnast_range::make_unbounded());
    return;
  }
  set_range(lhs, rhs[0].sra(rhs[1]));
}

// Bitwise ops — conservative: join of operand ranges (not tight).
void uPass_bitwidth::process_bit_and() {
  auto [lhs, rhs] = scan_op();
  if (rhs.empty()) {
    set_range(lhs, Lnast_range::make_unbounded());
    return;
  }
  Lnast_range result = rhs[0];
  for (std::size_t i = 1; i < rhs.size(); ++i) {
    result = result.join(rhs[i]);
  }
  set_range(lhs, result);
}

void uPass_bitwidth::process_bit_or() {
  auto [lhs, rhs] = scan_op();
  if (rhs.empty()) {
    set_range(lhs, Lnast_range::make_unbounded());
    return;
  }
  Lnast_range result = rhs[0];
  for (std::size_t i = 1; i < rhs.size(); ++i) {
    result = result.join(rhs[i]);
  }
  set_range(lhs, result);
}

void uPass_bitwidth::process_bit_xor() {
  auto [lhs, rhs] = scan_op();
  if (rhs.empty()) {
    set_range(lhs, Lnast_range::make_unbounded());
    return;
  }
  Lnast_range result = rhs[0];
  for (std::size_t i = 1; i < rhs.size(); ++i) {
    result = result.join(rhs[i]);
  }
  set_range(lhs, result);
}

void uPass_bitwidth::process_bit_not() {
  auto [lhs, rhs] = scan_op();
  // ~x = -x - 1; propagate negated range conservatively.
  if (rhs.empty()) {
    set_range(lhs, Lnast_range::make_unbounded());
    return;
  }
  set_range(lhs, rhs[0].neg());
}

// Logical ops — result is always boolean.
void uPass_bitwidth::process_log_and() {
  auto lhs = scan_lhs_only();
  set_range(lhs, Lnast_range::boolean());
}
void uPass_bitwidth::process_log_or() {
  auto lhs = scan_lhs_only();
  set_range(lhs, Lnast_range::boolean());
}
void uPass_bitwidth::process_log_not() {
  auto lhs = scan_lhs_only();
  set_range(lhs, Lnast_range::boolean());
}

// Reductions — single-bit boolean result.
void uPass_bitwidth::process_red_or() {
  auto lhs = scan_lhs_only();
  set_range(lhs, Lnast_range::boolean());
}
void uPass_bitwidth::process_red_and() {
  auto lhs = scan_lhs_only();
  set_range(lhs, Lnast_range::boolean());
}
void uPass_bitwidth::process_red_xor() {
  auto lhs = scan_lhs_only();
  set_range(lhs, Lnast_range::boolean());
}

// popcount (`a#+[..]`) — set-bit count, not boolean. Always in
// [0, sbits(input)]: an n-bit value has at most n set bits.
void uPass_bitwidth::process_popcount() {
  auto [lhs, rhs] = scan_op();
  if (rhs.empty()) {
    set_range(lhs, Lnast_range::make_unbounded());
    return;
  }
  Lnast_range result;
  result.min       = 0;
  result.max       = rhs[0].get_sbits();
  result.unbounded = false;
  set_range(lhs, result);
}

// Comparison ops — always boolean.
void uPass_bitwidth::process_eq() {
  auto lhs = scan_lhs_only();
  set_range(lhs, Lnast_range::boolean());
}
void uPass_bitwidth::process_ne() {
  auto lhs = scan_lhs_only();
  set_range(lhs, Lnast_range::boolean());
}
void uPass_bitwidth::process_lt() {
  auto lhs = scan_lhs_only();
  set_range(lhs, Lnast_range::boolean());
}
void uPass_bitwidth::process_le() {
  auto lhs = scan_lhs_only();
  set_range(lhs, Lnast_range::boolean());
}
void uPass_bitwidth::process_gt() {
  auto lhs = scan_lhs_only();
  set_range(lhs, Lnast_range::boolean());
}
void uPass_bitwidth::process_ge() {
  auto lhs = scan_lhs_only();
  set_range(lhs, Lnast_range::boolean());
}

// Bit-manipulation ops — conservative unbounded for now.
void uPass_bitwidth::process_sext() {
  // sext(value, sign_bit_pos): result in [-2^p, 2^p - 1] for a constant
  // position p (Goal 1n N5). rhs[1] is the sign-bit POSITION (0-indexed).
  auto [lhs, rhs] = scan_op();
  if (rhs.size() < 2 || !rhs[1].is_constant()) {
    set_range(lhs, Lnast_range::make_unbounded());
    return;
  }
  set_range(lhs, Lnast_range::sext_to(rhs[1].min));
}
// get_mask(base, mask) — the selected bits packed LSB-first as an UNSIGNED
// value (task 1b: this is the "force" operator). For a known non-negative mask
// of width w = popcount(mask), the result lies in [0, 2^w − 1]. A single
// selected bit yields the signed 1-bit value (-1/0), matching Dlop::get_mask_op
// and the `x#[i]` semantics. Negative (carve-out) or unknown masks stay
// conservative — their width depends on the source, unknown here.
void uPass_bitwidth::process_get_mask() {
  auto [lhs, rhs] = scan_op();
  if (rhs.size() < 2 || !rhs[1].is_constant() || rhs[1].min < 0) {
    set_range(lhs, Lnast_range::make_unbounded());
    return;
  }
  int w = std::popcount(static_cast<uint64_t>(rhs[1].min));
  if (w == 0) {
    set_range(lhs, Lnast_range::constant(0));
  } else if (w == 1) {
    set_range(lhs, Lnast_range::boolean());  // signed 1-bit [-1, 0]
  } else if (w >= 63) {
    set_range(lhs, Lnast_range::make_unbounded());
  } else {
    Lnast_range r;
    r.min       = 0;
    r.max       = (int64_t{1} << w) - 1;
    r.unbounded = false;
    set_range(lhs, r);
  }
}
void uPass_bitwidth::process_set_mask() {
  if (!move_to_child()) {
    return;
  }
  std::string lhs{current_text()};
  if (!move_to_sibling()) {
    move_to_parent();
    return;
  }

  std::string base_name;
  if (Lnast_ntype::is_ref(get_raw_ntype())) {
    base_name = current_text();
  }

  std::optional<Lnast_range> mask_range;
  if (move_to_sibling()) {
    if (Lnast_ntype::is_const(get_raw_ntype())) {
      mask_range = range_from_const_text(current_text());
    }
  }
  move_to_parent();

  if (!base_name.empty() && mask_range && mask_range->is_constant() && mask_range->min >= 0) {
    const auto base = ssa_base_name(base_name);
    if (auto env_it = decl_envelope_.find(base); env_it != decl_envelope_.end()) {
      if (mask_touches_outside_bits(mask_range->min, storage_bits_for_env(env_it->second))) {
        record_overflow(base, *mask_range, env_it->second);
      }
    }
  }

  set_range(lhs, Lnast_range::make_unbounded());
}

// ── Declared-envelope capture (Goal 1n N4) ───────────────────────────────────

std::optional<Lnast_range> uPass_bitwidth::read_prim_type_int_envelope() {
  // Cursor is ON the prim_type_int node: children are const(max), const(min).
  // A "nil" / non-integer / too-wide-for-int64 bound = unbounded side ⇒ no
  // envelope (we don't range-check against a half-known type today; that's the
  // exact-Dlop residual, todo/ 1n N3).
  auto to_i64 = [](const Const& v) -> std::optional<int64_t> {
    if (!v.is_integer() || v.get_bits() > 62) {
      return std::nullopt;
    }
    return v.to_i();
  };
  if (!move_to_child()) {
    return std::nullopt;
  }
  std::optional<int64_t> max_v;
  std::optional<int64_t> min_v;
  if (Lnast_ntype::is_const(get_raw_ntype())) {
    max_v = to_i64(*Dlop::from_pyrope(current_text()));
  }
  if (move_to_sibling() && Lnast_ntype::is_const(get_raw_ntype())) {
    min_v = to_i64(*Dlop::from_pyrope(current_text()));
  }
  move_to_parent();
  if (!max_v || !min_v) {
    return std::nullopt;
  }
  Lnast_range env;
  env.min       = *min_v;
  env.max       = *max_v;
  env.unbounded = false;
  return env;
}

void uPass_bitwidth::process_declare() {
  // declare(ref(var), TYPE, const(mode)). Record the prim_type_int envelope and
  // note a wrap/sat mode. The init value is a SEPARATE store node, so it is
  // range-checked later (at end_run, against this envelope).
  if (!move_to_child()) {
    return;
  }
  std::string var{current_text()};
  if (!move_to_sibling()) {
    move_to_parent();
    return;
  }
  if (Lnast_ntype::is_prim_type_int(get_raw_ntype())) {
    if (auto env = read_prim_type_int_envelope()) {
      decl_envelope_[var] = *env;
    }
  }
  move_to_parent();
}

void uPass_bitwidth::process_type_spec() {
  // type_spec(ref(var), TYPE) — same envelope capture as declare, no mode slot.
  if (!move_to_child()) {
    return;
  }
  std::string var{current_text()};
  if (move_to_sibling() && Lnast_ntype::is_prim_type_int(get_raw_ntype())) {
    if (auto env = read_prim_type_int_envelope()) {
      decl_envelope_[var] = *env;
    }
  }
  move_to_parent();
}

//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "upass_bitwidth.hpp"

#include <bit>
#include <charconv>
#include <limits>
#include <string>
#include <string_view>
#include <system_error>

#include "lnast.hpp"
#include "lnast_ntype.hpp"
#include "pass.hpp"

// ── Plugin registration ───────────────────────────────────────────────────────
// The static initializer fires at program startup so the runner discovers
// "bitwidth" via uPass_plugin::get_registry() without any explicit wiring.
static upass::uPass_plugin plugin_bitwidth("bitwidth", upass::uPass_wrapper<uPass_bitwidth>::get_upass);

static constexpr std::string_view call_ref_arg_marker = "__ref_arg";

// ── Constructor ──────────────────────────────────────────────────────────────

uPass_bitwidth::uPass_bitwidth(std::shared_ptr<upass::Lnast_manager>& lm) : upass::uPass(lm) {}

// ── Lifecycle ────────────────────────────────────────────────────────────────

void uPass_bitwidth::begin_iteration() {
  // Reset the changed flag only — ranges accumulate across iterations so a
  // second sweep can tighten what was unbounded in the first.
  changed = false;

  // Cross-invocation persistence: a fresh uPass_bitwidth instance starts
  // with empty range_map_, but the previous pass.upass call may have left
  // ranges in lnast->bw_meta(). Seed range_map_ from bw_meta so a second
  // sweep can tighten what was unbounded before. Only do this once per run
  // (when range_map_ is empty); subsequent begin_iteration calls inside the
  // same run leave the accumulated state alone.
  if (range_map_.empty()) {
    const auto& meta = lm->get_lnast()->bw_meta();
    for (const auto& [name, e] : meta.ranges) {
      Lnast_range r;
      r.min     = e.min;
      r.max     = e.max;
      r.neg_inf = e.neg_inf;
      r.pos_inf = e.pos_inf;
      range_map_.emplace(name, r);
    }
  }
}

void uPass_bitwidth::end_run() {
  // Flush range_map_ into lnast->bw_meta() so lnast_to_lgraph can read them.
  auto& meta = lm->get_lnast()->bw_meta();
  meta.ranges.clear();
  for (const auto& [name, rng] : range_map_) {
    BitwidthEntry e;
    e.min     = rng.min;
    e.max     = rng.max;
    e.neg_inf = rng.neg_inf;
    e.pos_inf = rng.pos_inf;
    meta.ranges.emplace(name, e);
  }
}

// ── Helpers ───────────────────────────────────────────────────────────────────

Lnast_range uPass_bitwidth::read_range(std::string_view name) const {
  if (name.empty()) {
    return Lnast_range::unbounded();
  }
  // Heterogeneous lookup: absl::flat_hash_map's transparent hashing accepts
  // string_view without allocating a temporary std::string on every read.
  auto it = range_map_.find(name);
  if (it != range_map_.end()) {
    return it->second;
  }
  return Lnast_range::unbounded();
}

void uPass_bitwidth::set_range(std::string_view name, Lnast_range r) {
  if (name.empty()) {
    return;
  }
  // Probe with the string_view first to skip allocation on the common
  // already-present path. Only materialize a std::string when we insert.
  if (auto it = range_map_.find(name); it != range_map_.end()) {
    if (r.is_narrower_than(it->second)) {
      it->second = r;
      mark_changed();
    }
    return;
  }
  range_map_.emplace(std::string{name}, r);
  if (!r.is_unbounded()) {
    mark_changed();
  }
}

void uPass_bitwidth::replace_range(std::string_view name, Lnast_range r) {
  if (name.empty()) {
    return;
  }
  auto it = range_map_.find(name);
  if (it != range_map_.end()) {
    const Lnast_range& cur = it->second;
    if (cur.min != r.min || cur.max != r.max || cur.neg_inf != r.neg_inf || cur.pos_inf != r.pos_inf) {
      it->second = r;
      mark_changed();
    }
    return;
  }
  range_map_.emplace(std::string{name}, r);
  if (!r.is_unbounded()) {
    mark_changed();
  }
}

// Try to parse a Pyrope constant text to int64_t.  Handles decimal,
// "0x..." (hex), "0b..." (binary).  Returns Lnast_range::unbounded() on
// failure (e.g. Pyrope unknown-bits constants like "0sb1?").
static Lnast_range range_from_const_text(std::string_view text) {
  if (text.empty()) {
    return Lnast_range::unbounded();
  }
  // Strip a leading minus sign.
  bool negative = (text[0] == '-');
  if (negative) {
    text.remove_prefix(1);
  }
  if (text.empty()) {
    return Lnast_range::unbounded();
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
    return Lnast_range::unbounded();
  }

  if (!parsed) {
    return Lnast_range::unbounded();
  }
  // Unsigned value: fit into int64_t or give up.
  if (uval > static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) {
    return Lnast_range::unbounded();
  }
  int64_t val = static_cast<int64_t>(uval);
  return Lnast_range::constant(negative ? -val : val);
}

void uPass_bitwidth::clear_range(std::string_view name) {
  if (name.empty()) {
    return;
  }
  // Heterogeneous erase — no temporary std::string for the lookup.
  if (range_map_.erase(name) != 0) {
    mark_changed();
  }
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
      out.rhs.push_back(Lnast_range::unbounded());
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

// ── fold_ref ─────────────────────────────────────────────────────────────────

std::optional<Const> uPass_bitwidth::fold_ref(std::string_view name) {
  auto it = range_map_.find(name);
  if (it == range_map_.end()) {
    return std::nullopt;
  }
  if (!it->second.is_constant()) {
    return std::nullopt;
  }
  return *Dlop::create_integer(it->second.min);
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
  // Direct assignment to a mut var must REPLACE the lhs range, not narrow it.
  // set_range's monotonic-narrow policy is meant for iterative refinement of a
  // single producer's tmp; for mut reassignment (`v1 = 0` then `v1 = 0sb?`)
  // narrowing leaves a stale `{0,0}` range and fold_ref hands back the stale
  // constant to constprop's reads.
  replace_range(lhs, rhs.empty() ? Lnast_range::unbounded() : rhs[0]);
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

  while (move_to_sibling()) {
    if (!is_type(Lnast_ntype::Lnast_ntype_store)) {
      continue;
    }
    if (!move_to_child()) {
      continue;
    }
    const bool is_ref_arg = current_text() == call_ref_arg_marker;
    if (is_ref_arg && move_to_sibling() && is_type(Lnast_ntype::Lnast_ntype_ref)) {
      clear_range(current_text());
    }
    move_to_parent();
  }

  move_to_parent();
}

void uPass_bitwidth::process_plus() {
  auto [lhs, rhs] = scan_op();
  if (rhs.empty()) {
    set_range(lhs, Lnast_range::unbounded());
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
    set_range(lhs, Lnast_range::unbounded());
    return;
  }
  Lnast_range result = rhs[0].sub(rhs[1]);
  set_range(lhs, result);
}

void uPass_bitwidth::process_mult() {
  auto [lhs, rhs] = scan_op();
  if (rhs.empty()) {
    set_range(lhs, Lnast_range::unbounded());
    return;
  }
  Lnast_range result = rhs[0];
  for (std::size_t i = 1; i < rhs.size(); ++i) {
    result = result.mul(rhs[i]);
  }
  set_range(lhs, result);
}

void uPass_bitwidth::process_div() {
  // Quotient range is conservative — don't try to invert divisor range.
  auto [lhs, rhs] = scan_op();
  (void)rhs;
  set_range(lhs, Lnast_range::unbounded());
}

void uPass_bitwidth::process_mod() {
  auto [lhs, rhs] = scan_op();
  (void)rhs;
  set_range(lhs, Lnast_range::unbounded());
}

void uPass_bitwidth::process_shl() {
  auto [lhs, rhs] = scan_op();
  if (rhs.size() < 2) {
    set_range(lhs, Lnast_range::unbounded());
    return;
  }
  set_range(lhs, rhs[0].shl(rhs[1]));
}

void uPass_bitwidth::process_sra() {
  auto [lhs, rhs] = scan_op();
  if (rhs.size() < 2) {
    set_range(lhs, Lnast_range::unbounded());
    return;
  }
  set_range(lhs, rhs[0].sra(rhs[1]));
}

// Bitwise ops — conservative: join of operand ranges (not tight).
void uPass_bitwidth::process_bit_and() {
  auto [lhs, rhs] = scan_op();
  if (rhs.empty()) {
    set_range(lhs, Lnast_range::unbounded());
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
    set_range(lhs, Lnast_range::unbounded());
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
    set_range(lhs, Lnast_range::unbounded());
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
    set_range(lhs, Lnast_range::unbounded());
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
    set_range(lhs, Lnast_range::unbounded());
    return;
  }
  Lnast_range result;
  result.min     = 0;
  result.max     = rhs[0].get_sbits();
  result.neg_inf = false;
  result.pos_inf = false;
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
  auto [lhs, rhs] = scan_op();
  (void)rhs;
  set_range(lhs, Lnast_range::unbounded());
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
    set_range(lhs, Lnast_range::unbounded());
    return;
  }
  int w = std::popcount(static_cast<uint64_t>(rhs[1].min));
  if (w == 0) {
    set_range(lhs, Lnast_range::constant(0));
  } else if (w == 1) {
    set_range(lhs, Lnast_range::boolean());  // signed 1-bit [-1, 0]
  } else if (w >= 63) {
    set_range(lhs, Lnast_range::unbounded());
  } else {
    Lnast_range r;
    r.min     = 0;
    r.max     = (int64_t{1} << w) - 1;
    r.neg_inf = false;
    r.pos_inf = false;
    set_range(lhs, r);
  }
}
void uPass_bitwidth::process_set_mask() {
  auto [lhs, rhs] = scan_op();
  (void)rhs;
  set_range(lhs, Lnast_range::unbounded());
}

// ── process_attr_set ─────────────────────────────────────────────────────────
//
// LNAST shape (mirrors uPass_attributes::process_attr_set):
//   attr_set
//     ref(target)
//     const(attr_name)        — "ubits" | "sbits" | "max" | "min" | …
//     <value: const | ref>
//
// For numeric bit/range attributes, derive an explicit Lnast_range and meet it
// with whatever the target currently holds. Conflict reporting is best-effort:
// we warn (not error) until wrap/saturate policy migrates to bitwidth (T2 #7).
// Non-bitwidth attributes (e.g. `comptime`, `type`, `wrap`, `saturate`) are
// ignored — uPass_attributes owns those.
void uPass_bitwidth::process_attr_set() {
  if (!move_to_child()) {
    return;
  }
  std::string target{current_text()};
  if (!move_to_sibling()) {
    move_to_parent();
    return;
  }
  std::string attr_name{current_text()};
  std::string value_text;
  bool        value_is_const = false;
  if (move_to_sibling()) {
    value_text     = std::string{current_text()};
    value_is_const = Lnast_ntype::is_const(get_raw_ntype());
  }
  move_to_parent();

  if (target.empty() || attr_name.empty() || !value_is_const) {
    return;
  }

  // Parse value text to int64_t (handles dec / 0x / 0b, returns unbounded on
  // unparseable inputs like Pyrope-unknown-bits constants).
  auto val_range = range_from_const_text(value_text);
  if (!val_range.is_constant()) {
    return;
  }
  int64_t n = val_range.min;

  // Build the implied range from this attribute.
  Lnast_range narrow;
  if (attr_name == "ubits") {
    if (n <= 0 || n >= 63) {
      return;  // sanity: 1..62 representable in int64_t
    }
    narrow.min     = 0;
    narrow.max     = (int64_t{1} << n) - 1;
    narrow.neg_inf = false;
    narrow.pos_inf = false;
  } else if (attr_name == "sbits") {
    if (n <= 1 || n >= 63) {
      return;  // sanity: 2..62 (1 bit = single sign)
    }
    narrow.min     = -(int64_t{1} << (n - 1));
    narrow.max     = (int64_t{1} << (n - 1)) - 1;
    narrow.neg_inf = false;
    narrow.pos_inf = false;
  } else if (attr_name == "max") {
    narrow.max     = n;
    narrow.neg_inf = true;  // only constrains upper bound
    narrow.pos_inf = false;
  } else if (attr_name == "min") {
    narrow.min     = n;
    narrow.neg_inf = false;  // only constrains lower bound
    narrow.pos_inf = true;
  } else {
    return;  // not a bitwidth-owned attribute
  }

  // Compute the meet of `current` and `narrow` manually. We can't use
  // Lnast_range::meet here: it short-circuits to the other operand whenever
  // either side is half-bounded (e.g. `(-inf, 10]`), which is too coarse for
  // attribute narrowing. Per-side merge: take the tighter of the two bounds,
  // respecting infinity flags.
  auto        current = read_range(target);
  Lnast_range merged;
  merged.neg_inf = current.neg_inf && narrow.neg_inf;
  merged.pos_inf = current.pos_inf && narrow.pos_inf;
  if (!merged.neg_inf) {
    int64_t lo = current.neg_inf ? narrow.min : (narrow.neg_inf ? current.min : std::max(current.min, narrow.min));
    merged.min = lo;
  }
  if (!merged.pos_inf) {
    int64_t hi = current.pos_inf ? narrow.max : (narrow.pos_inf ? current.max : std::min(current.max, narrow.max));
    merged.max = hi;
  }

  // Hard contradiction (e.g. `x = 100; x.[ubits] = 3` → meet is empty).
  if (!merged.neg_inf && !merged.pos_inf && merged.min > merged.max) {
    // Warn for now; once wrap/saturate policy migrates here (T2 #7) this
    // promotes to upass::error unless the target has [wrap] or [saturate].
    Pass::warn(
        std::format("uPass_bitwidth: explicit `[{}]` constraint on `{}` is unsatisfiable "
                    "(current range does not fit declared bounds)",
                    attr_name,
                    target));
    return;
  }

  // set_range only updates if `merged` is strictly narrower than `current`.
  set_range(target, merged);
}

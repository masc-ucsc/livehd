//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "upass_bitwidth.hpp"

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
static upass::uPass_plugin plugin_bitwidth("bitwidth",
                                           upass::uPass_wrapper<uPass_bitwidth>::get_upass);

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
  if (name.empty()) return Lnast_range::unbounded();
  auto it = range_map_.find(std::string{name});
  if (it != range_map_.end()) return it->second;
  return Lnast_range::unbounded();
}

void uPass_bitwidth::set_range(std::string_view name, Lnast_range r) {
  if (name.empty()) return;
  auto [it, inserted] = range_map_.emplace(std::string{name}, r);
  if (inserted) {
    // First time we've seen this name — only mark changed if we got a finite
    // range (unbounded-first is not a useful narrowing event).
    if (!r.is_unbounded()) mark_changed();
    return;
  }
  // Already exists: only update and mark changed if strictly narrower.
  if (r.is_narrower_than(it->second)) {
    it->second = r;
    mark_changed();
  }
}

// Try to parse a Pyrope constant text to int64_t.  Handles decimal,
// "0x..." (hex), "0b..." (binary).  Returns Lnast_range::unbounded() on
// failure (e.g. Pyrope unknown-bits constants like "0sb1?").
static Lnast_range range_from_const_text(std::string_view text) {
  if (text.empty()) return Lnast_range::unbounded();
  // Strip a leading minus sign.
  bool negative = (text[0] == '-');
  if (negative) text.remove_prefix(1);
  if (text.empty()) return Lnast_range::unbounded();

  uint64_t uval = 0;
  bool     parsed = false;
  if (text.size() >= 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
    auto res = std::from_chars(text.data() + 2, text.data() + text.size(), uval, 16);
    parsed = (res.ec == std::errc{} && res.ptr == text.data() + text.size());
  } else if (text.size() >= 2 && text[0] == '0' && (text[1] == 'b' || text[1] == 'B')) {
    auto res = std::from_chars(text.data() + 2, text.data() + text.size(), uval, 2);
    parsed = (res.ec == std::errc{} && res.ptr == text.data() + text.size());
  } else {
    int64_t sval = 0;
    auto    res  = std::from_chars(text.data(), text.data() + text.size(), sval);
    if (res.ec == std::errc{} && res.ptr == text.data() + text.size()) {
      return Lnast_range::constant(negative ? -sval : sval);
    }
    return Lnast_range::unbounded();
  }

  if (!parsed) return Lnast_range::unbounded();
  // Unsigned value: fit into int64_t or give up.
  if (uval > static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) return Lnast_range::unbounded();
  int64_t val = static_cast<int64_t>(uval);
  return Lnast_range::constant(negative ? -val : val);
}

// Strip a leading port sigil (`%` output, `$` input) so the bitwidth pass
// uses the same canonical key on both write and read sides — matches
// uPass_attributes::normalize_name. lnast_to_lgraph's apply_bw also uses
// the unprefixed name when looking up bw_meta.
static std::string normalize_name(std::string_view name) {
  if (!name.empty() && (name.front() == '%' || name.front() == '$')) {
    return std::string{name.substr(1)};
  }
  return std::string{name};
}

uPass_bitwidth::Op_ranges uPass_bitwidth::scan_op() {
  Op_ranges out;
  if (!move_to_child()) return out;

  // First child: LHS (always a ref whose text is the destination name).
  out.lhs = normalize_name(current_text());

  // Remaining children: RHS operands.
  while (move_to_sibling()) {
    auto t = get_raw_ntype();
    if (Lnast_ntype::is_ref(t)) {
      out.rhs.push_back(read_range(normalize_name(current_text())));
    } else if (Lnast_ntype::is_const(t)) {
      out.rhs.push_back(range_from_const_text(current_text()));
    } else {
      // Structural child — skip, treat as unknown operand.
      out.rhs.push_back(Lnast_range::unbounded());
    }
  }
  move_to_parent();
  return out;
}

std::string uPass_bitwidth::scan_lhs_only() {
  if (!move_to_child()) return {};
  std::string lhs = normalize_name(current_text());
  move_to_parent();
  return lhs;
}

// ── fold_ref ─────────────────────────────────────────────────────────────────

std::optional<Const> uPass_bitwidth::fold_ref(std::string_view name) {
  // Callers may pass either `%out` or `out`; normalize so we hit the same
  // key the pass stored under (see normalize_name() above).
  auto it = range_map_.find(normalize_name(name));
  if (it == range_map_.end()) return std::nullopt;
  if (!it->second.is_constant()) return std::nullopt;
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
  if (rhs.empty()) {
    set_range(lhs, Lnast_range::unbounded());
  } else {
    // Direct assignment propagates the range of the RHS operand.
    set_range(lhs, rhs[0]);
  }
}

void uPass_bitwidth::process_plus() {
  auto [lhs, rhs] = scan_op();
  if (rhs.empty()) { set_range(lhs, Lnast_range::unbounded()); return; }
  Lnast_range result = rhs[0];
  for (std::size_t i = 1; i < rhs.size(); ++i) result = result.add(rhs[i]);
  set_range(lhs, result);
}

void uPass_bitwidth::process_minus() {
  auto [lhs, rhs] = scan_op();
  if (rhs.size() < 2) { set_range(lhs, Lnast_range::unbounded()); return; }
  Lnast_range result = rhs[0].sub(rhs[1]);
  set_range(lhs, result);
}

void uPass_bitwidth::process_mult() {
  auto [lhs, rhs] = scan_op();
  if (rhs.empty()) { set_range(lhs, Lnast_range::unbounded()); return; }
  Lnast_range result = rhs[0];
  for (std::size_t i = 1; i < rhs.size(); ++i) result = result.mul(rhs[i]);
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
  if (rhs.size() < 2) { set_range(lhs, Lnast_range::unbounded()); return; }
  set_range(lhs, rhs[0].shl(rhs[1]));
}

void uPass_bitwidth::process_sra() {
  auto [lhs, rhs] = scan_op();
  if (rhs.size() < 2) { set_range(lhs, Lnast_range::unbounded()); return; }
  set_range(lhs, rhs[0].sra(rhs[1]));
}

// Bitwise ops — conservative: join of operand ranges (not tight).
void uPass_bitwidth::process_bit_and() {
  auto [lhs, rhs] = scan_op();
  if (rhs.empty()) { set_range(lhs, Lnast_range::unbounded()); return; }
  Lnast_range result = rhs[0];
  for (std::size_t i = 1; i < rhs.size(); ++i) result = result.join(rhs[i]);
  set_range(lhs, result);
}

void uPass_bitwidth::process_bit_or() {
  auto [lhs, rhs] = scan_op();
  if (rhs.empty()) { set_range(lhs, Lnast_range::unbounded()); return; }
  Lnast_range result = rhs[0];
  for (std::size_t i = 1; i < rhs.size(); ++i) result = result.join(rhs[i]);
  set_range(lhs, result);
}

void uPass_bitwidth::process_bit_xor() {
  auto [lhs, rhs] = scan_op();
  if (rhs.empty()) { set_range(lhs, Lnast_range::unbounded()); return; }
  Lnast_range result = rhs[0];
  for (std::size_t i = 1; i < rhs.size(); ++i) result = result.join(rhs[i]);
  set_range(lhs, result);
}

void uPass_bitwidth::process_bit_not() {
  auto [lhs, rhs] = scan_op();
  // ~x = -x - 1; propagate negated range conservatively.
  if (rhs.empty()) { set_range(lhs, Lnast_range::unbounded()); return; }
  set_range(lhs, rhs[0].neg());
}

// Logical ops — result is always boolean.
void uPass_bitwidth::process_log_and() {
  auto lhs = scan_lhs_only(); set_range(lhs, Lnast_range::boolean());
}
void uPass_bitwidth::process_log_or() {
  auto lhs = scan_lhs_only(); set_range(lhs, Lnast_range::boolean());
}
void uPass_bitwidth::process_log_not() {
  auto lhs = scan_lhs_only(); set_range(lhs, Lnast_range::boolean());
}

// Reductions — single-bit boolean result.
void uPass_bitwidth::process_red_or() {
  auto lhs = scan_lhs_only(); set_range(lhs, Lnast_range::boolean());
}
void uPass_bitwidth::process_red_and() {
  auto lhs = scan_lhs_only(); set_range(lhs, Lnast_range::boolean());
}
void uPass_bitwidth::process_red_xor() {
  auto lhs = scan_lhs_only(); set_range(lhs, Lnast_range::boolean());
}

// Comparison ops — always boolean.
void uPass_bitwidth::process_eq() {
  auto lhs = scan_lhs_only(); set_range(lhs, Lnast_range::boolean());
}
void uPass_bitwidth::process_ne() {
  auto lhs = scan_lhs_only(); set_range(lhs, Lnast_range::boolean());
}
void uPass_bitwidth::process_lt() {
  auto lhs = scan_lhs_only(); set_range(lhs, Lnast_range::boolean());
}
void uPass_bitwidth::process_le() {
  auto lhs = scan_lhs_only(); set_range(lhs, Lnast_range::boolean());
}
void uPass_bitwidth::process_gt() {
  auto lhs = scan_lhs_only(); set_range(lhs, Lnast_range::boolean());
}
void uPass_bitwidth::process_ge() {
  auto lhs = scan_lhs_only(); set_range(lhs, Lnast_range::boolean());
}

// Bit-manipulation ops — conservative unbounded for now.
void uPass_bitwidth::process_sext() {
  auto [lhs, rhs] = scan_op();
  (void)rhs;
  set_range(lhs, Lnast_range::unbounded());
}
void uPass_bitwidth::process_get_mask() {
  auto [lhs, rhs] = scan_op();
  (void)rhs;
  set_range(lhs, Lnast_range::unbounded());
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
  if (!move_to_child()) return;
  auto target = normalize_name(current_text());
  if (!move_to_sibling()) { move_to_parent(); return; }
  std::string attr_name{current_text()};
  std::string value_text;
  bool        value_is_const = false;
  if (move_to_sibling()) {
    value_text     = std::string{current_text()};
    value_is_const = Lnast_ntype::is_const(get_raw_ntype());
  }
  move_to_parent();

  if (target.empty() || attr_name.empty() || !value_is_const) return;

  // Parse value text to int64_t (handles dec / 0x / 0b, returns unbounded on
  // unparseable inputs like Pyrope-unknown-bits constants).
  auto val_range = range_from_const_text(value_text);
  if (!val_range.is_constant()) return;
  int64_t n = val_range.min;

  // Build the implied range from this attribute.
  Lnast_range narrow;
  if (attr_name == "ubits") {
    if (n <= 0 || n >= 63) return;  // sanity: 1..62 representable in int64_t
    narrow.min     = 0;
    narrow.max     = (int64_t{1} << n) - 1;
    narrow.neg_inf = false;
    narrow.pos_inf = false;
  } else if (attr_name == "sbits") {
    if (n <= 1 || n >= 63) return;  // sanity: 2..62 (1 bit = single sign)
    narrow.min     = -(int64_t{1} << (n - 1));
    narrow.max     = (int64_t{1} << (n - 1)) - 1;
    narrow.neg_inf = false;
    narrow.pos_inf = false;
  } else if (attr_name == "max") {
    narrow.max     = n;
    narrow.neg_inf = true;   // only constrains upper bound
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
  auto current = read_range(target);
  Lnast_range merged;
  merged.neg_inf = current.neg_inf && narrow.neg_inf;
  merged.pos_inf = current.pos_inf && narrow.pos_inf;
  if (!merged.neg_inf) {
    int64_t lo = current.neg_inf ? narrow.min
              : (narrow.neg_inf  ? current.min
                                 : std::max(current.min, narrow.min));
    merged.min = lo;
  }
  if (!merged.pos_inf) {
    int64_t hi = current.pos_inf ? narrow.max
              : (narrow.pos_inf  ? current.max
                                 : std::min(current.max, narrow.max));
    merged.max = hi;
  }

  // Hard contradiction (e.g. `x = 100; x.[ubits] = 3` → meet is empty).
  if (!merged.neg_inf && !merged.pos_inf && merged.min > merged.max) {
    // Warn for now; once wrap/saturate policy migrates here (T2 #7) this
    // promotes to upass::error unless the target has [wrap] or [saturate].
    Pass::warn(
        std::format("uPass_bitwidth: explicit `[{}]` constraint on `{}` is unsatisfiable "
                    "(current range does not fit declared bounds)",
                    attr_name, target));
    return;
  }

  // set_range only updates if `merged` is strictly narrower than `current`.
  set_range(target, merged);
}

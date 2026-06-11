//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "upass_bitwidth.hpp"

#include <algorithm>
#include <bit>
#include <limits>
#include <optional>
#include <string>
#include <string_view>

#include "diag.hpp"
#include "lnast.hpp"
#include "lnast_ntype.hpp"

// ── Plugin registration ───────────────────────────────────────────────────────
static upass::uPass_plugin plugin_bitwidth("bitwidth", upass::uPass_wrapper<uPass_bitwidth>::get_upass, {"constprop"});

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

uPass_bitwidth::uPass_bitwidth(std::shared_ptr<upass::Lnast_manager>& _lm) : upass::uPass(_lm) {}

// ── Lnast_range ↔ bundle-Entry conversion ────────────────────────────────────

std::optional<int64_t> uPass_bitwidth::const_to_i64(const Dlop& v) {
  if (v.is_invalid() || !v.is_integer() || v.has_unknowns() || v.get_bits() > 62) {
    return std::nullopt;
  }
  return v.to_just_i64();
}

Lnast_range uPass_bitwidth::range_from_entry(const Dlop& maxc, const Dlop& minc) {
  const auto mx = const_to_i64(maxc);
  const auto mn = const_to_i64(minc);
  if (!mx || !mn) {
    return Lnast_range::make_unbounded();
  }
  Lnast_range r;
  r.min       = *mn;
  r.max       = *mx;
  r.unbounded = false;
  return r;
}

Lnast_range uPass_bitwidth::range_of_operand(const upass::Operand& o) const {
  // CONST literals read their value off the throwaway bundle. REF operands
  // deliberately do NOT read the (constprop-folded) trivial: this pass does
  // not participate in const-folding — its ranges come from its OWN
  // derivation chain (Entry.bw_*), preserving the documented force/reinterpret
  // semantics (e.g. an unsigned slice stored to a signed var) that the value
  // chain would mis-flag as overflow.
  if (o.name.empty()) {
    if (o.pattern) {
      return Lnast_range::make_unbounded();  // bit-pattern literal: force semantics, no value range
    }
    if (auto v = o.bundle->scalar(); v) {
      if (auto i = const_to_i64(*v)) {
        return Lnast_range::constant(*i);
      }
      return Lnast_range::make_unbounded();  // non-integer / unknown-bits / too wide
    }
    return Lnast_range::make_unbounded();
  }
  // The bundle's derived range facts.
  const auto& e = o.bundle->get_entry("0");
  auto        r = range_from_entry(e.bw_max, e.bw_min);
  if (!r.is_unbounded()) {
    return r;
  }
  // Cross-invocation persistence: a previous pass.upass run left ranges in
  // bw_meta (this walk's bundles start empty).
  return read_range(o.name);
}

Lnast_range uPass_bitwidth::read_range(std::string_view name) const {
  if (name.empty()) {
    return Lnast_range::make_unbounded();
  }
  if (runner_st != nullptr) {
    if (auto b = runner_st->get_bundle(name); b) {
      const auto& e = b->get_entry("0");
      auto        r = range_from_entry(e.bw_max, e.bw_min);
      if (!r.is_unbounded()) {
        return r;
      }
    }
  }
  const auto& meta = lm->get_lnast()->bw_meta();
  if (auto it = meta.ranges.find(std::string(name)); it != meta.ranges.end()) {
    Lnast_range r;
    r.min       = it->second.min;
    r.max       = it->second.max;
    r.unbounded = it->second.unbounded;
    return r;
  }
  return Lnast_range::make_unbounded();
}

// ── Writes ───────────────────────────────────────────────────────────────────

void uPass_bitwidth::write_bw(std::string_view name, Bundle& dst, Lnast_range r, bool replace) {
  if (name.empty() || name.find('.') != std::string_view::npos) {
    return;  // scalar names only (per-field ranges are a 1t follow-up)
  }
  check_declared_fit(name, r);

  // Narrow-vs-replace against what the bundle already holds.
  const auto& e0  = dst.get_entry("0");
  const auto  cur = range_from_entry(e0.bw_max, e0.bw_min);
  if (!replace && !cur.is_unbounded() && !r.is_narrower_than(cur)) {
    return;
  }
  if (cur.min == r.min && cur.max == r.max && cur.is_unbounded() == r.is_unbounded()) {
    // bw fields unchanged; still write through to bw_meta below on first sight.
  } else if (dst.is_empty() || dst.has_trivial("0")) {
    Bundle::Entry e = dst.get_entry("0");
    e.immutable     = false;
    if (r.is_unbounded()) {
      e.bw_max = Bundle::invalid_lconst;
      e.bw_min = Bundle::invalid_lconst;
    } else {
      e.bw_max = *Dlop::create_integer(r.max);
      e.bw_min = *Dlop::create_integer(r.min);
    }
    dst.set("0", std::move(e));
  }

  // Write-through to lnast->bw_meta() — the tolg/LSP interface, and the
  // cross-invocation persistence store (replaces the old end_run flush).
  auto& meta = lm->get_lnast()->bw_meta();
  BitwidthEntry me;
  me.min                          = r.min;
  me.max                          = r.max;
  me.unbounded                    = r.is_unbounded();
  meta.ranges[std::string(name)]  = me;
}

void uPass_bitwidth::clear_range(std::string_view name) {
  if (name.empty()) {
    return;
  }
  if (runner_st != nullptr) {
    if (auto b = runner_st->get_bundle_for_write(name); b && (b->is_empty() || b->has_trivial("0"))) {
      Bundle::Entry e = b->get_entry("0");
      e.immutable     = false;
      e.bw_max        = Bundle::invalid_lconst;
      e.bw_min        = Bundle::invalid_lconst;
      b->set("0", std::move(e));
    }
  }
  lm->get_lnast()->bw_meta().ranges.erase(std::string(name));
}

// ── Declared envelope + fit check (at the offending node) ───────────────────

std::optional<Lnast_range> uPass_bitwidth::decl_envelope_of(std::string_view name) const {
  const std::string_view base = ssa_base_name(name);
  if (base.find('.') != std::string_view::npos || runner_st == nullptr) {
    return std::nullopt;
  }
  const auto b = runner_st->get_bundle(base);
  if (!b) {
    return std::nullopt;
  }
  const auto& e = b->get_entry("0");
  auto        r = range_from_entry(e.decl_max, e.decl_min);
  if (r.is_unbounded()) {
    return std::nullopt;
  }
  return r;
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
  const auto env = decl_envelope_of(base);
  if (!env) {
    return;
  }
  // An UNSIGNED envelope flags a value provably ABOVE max or BELOW min
  // (all negatives into unsigned are an error, judged on the inferred range —
  // bit-PATTERN literals never reach here, their range is unbounded). A
  // SIGNED envelope is strict containment.
  const bool is_unsigned_env = !env->is_signed();  // env.min >= 0
  const bool over            = is_unsigned_env ? (r.min > env->max || r.min < env->min) : !env->contains(r);
  if (over) {
    record_overflow(base, r, *env);
  }
}

void uPass_bitwidth::record_overflow(std::string_view name, const Lnast_range& value, const Lnast_range& env) {
  // The diagnostic is emitted AT the offending node: the cursor is on
  // the store/op during dispatch, so its SourceId (resolved through the
  // owning Lnast's locator, [[1f]]) is the span. An error-severity diag
  // fails the compile; no end_run throw, no deferral.
  livehd::diag::Span              span;
  std::vector<livehd::diag::Note> notes;
  if (const auto& ln = lm->get_lnast()) {
    span  = ln->span_of(lm->get_current_nid());
    notes = ln->notes_of(lm->get_current_nid(), "reached via this site");
  }
  livehd::diag::sink().emit(livehd::diag::Diagnostic{
      .severity = livehd::diag::Severity::error,
      .code     = "bitwidth-overflow",
      .category = "bitwidth",
      .pass     = "upass.bitwidth",
      .message  = std::format("`{}` (value {}) does not fit its declared range [{}, {}]", name, value.min, env.min, env.max),
      .span     = std::move(span),
      .hint     = "widen the declared type, force the bits with a bit-select (e.g. `x#[0..]`), or "
                  "apply a wrap/saturate policy",
  });
}

Lnast_range uPass_bitwidth::envelope_of_operand(const upass::Operand& o) const {
  // A never-written name (e.g. an input param) has no derived value range,
  // only a declared envelope — good enough to judge a shift amount's sign
  // (`n: s4` admits [-8, 7]). NOT used for the result stamp: the envelope is
  // a may-hold bound, and widening every read would change the overflow
  // checks' meaning.
  if (o.name.empty()) {
    return Lnast_range::make_unbounded();
  }
  if (auto env = decl_envelope_of(o.name)) {
    return *env;
  }
  // Params skip the declare bake; their declared width rides io_meta (SSA).
  const auto base = ssa_base_name(o.name);
  for (const auto& in : lm->get_lnast()->io_meta().inputs) {
    if (in.name == base && in.kind == Io_kind::integer && in.bits > 0 && in.bits <= 62) {
      if (in.is_signed) {
        return Lnast_range::sext_to(in.bits - 1);
      }
      Lnast_range r;
      r.min       = 0;
      r.max       = (int64_t{1} << in.bits) - 1;
      r.unbounded = false;
      return r;
    }
  }
  return Lnast_range::make_unbounded();
}

void uPass_bitwidth::check_shift_amount(const Lnast_range& amt) {
  if (amt.is_unbounded() || amt.min >= 0) {
    return;
  }
  // Skip deferred-template bodies (Lnast::is_template): unbound params fold
  // nil-derived placeholder values, so a negative amount there is an
  // optimization artifact — the realized copy at a real call site re-checks.
  if (const auto& ln = lm->get_lnast(); ln && ln->is_template()) {
    return;
  }
  const bool         always_negative = amt.max < 0;
  livehd::diag::Span span;
  if (const auto& ln = lm->get_lnast()) {
    span = ln->span_of(lm->get_current_nid());
  }
  livehd::diag::sink().emit(livehd::diag::Diagnostic{
      .severity = always_negative ? livehd::diag::Severity::error : livehd::diag::Severity::warning,
      .code     = "negative-shift",
      .category = "bitwidth",
      .pass     = "upass.bitwidth",
      .message  = always_negative ? std::format("shift amount is always negative (range [{}, {}])", amt.min, amt.max)
                                  : std::format("shift amount may be negative (range [{}, {}])", amt.min, amt.max),
      .span     = std::move(span),
      .hint     = "a shift / bit-select count must be >= 0",
  });
}

// ── Process hooks (push form) ────────────────────────────────────────────────

upass::Vote uPass_bitwidth::process_store(std::string_view dst_name, Bundle& dst, upass::Src_span src) {
  // Direct assignment to a mut var REPLACES the range (a stale narrow range
  // must not survive a reassignment — soundness: a binding's range must
  // contain its value).
  if (dst_name.empty() || src.empty() || dst_name.find('.') != std::string_view::npos) {
    return Vote::keep;
  }
  if (src.size() == 1) {
    write_bw(dst_name, dst, range_of_operand(src.front()), /*replace=*/true);
    return Vote::keep;
  }
  // Field-path store (selectors + value). `x[0] = v` on a SCALAR binding is
  // the scalar itself — stamp the value's range exactly. Any other path
  // invalidates the root's scalar range (per-field ranges are a follow-up;
  // an unbounded fallback is sound — the declared envelope still bounds it).
  const auto& sel = src.front();
  const bool  slot0
      = sel.name.empty() && sel.bundle && !sel.bundle->lone_trivial().is_invalid() && sel.bundle->lone_trivial().is_known_zero();
  if (src.size() == 2 && slot0) {
    write_bw(dst_name, dst, range_of_operand(src.back()), /*replace=*/true);
  } else {
    write_bw(dst_name, dst, Lnast_range::make_unbounded(), /*replace=*/true);
  }
  return Vote::keep;
}

// clang-format off
upass::Vote uPass_bitwidth::process_plus(std::string_view dst_name, Bundle& dst, upass::Src_span src) {
  if (src.empty()) { return stamp(dst_name, dst, Lnast_range::make_unbounded()); }
  Lnast_range result = range_of_operand(src[0]);
  for (std::size_t i = 1; i < src.size(); ++i) { result = result.add(range_of_operand(src[i])); }
  return stamp(dst_name, dst, result);
}

upass::Vote uPass_bitwidth::process_minus(std::string_view dst_name, Bundle& dst, upass::Src_span src) {
  if (src.size() < 2) { return stamp(dst_name, dst, Lnast_range::make_unbounded()); }
  return stamp(dst_name, dst, range_of_operand(src[0]).sub(range_of_operand(src[1])));
}

upass::Vote uPass_bitwidth::process_mult(std::string_view dst_name, Bundle& dst, upass::Src_span src) {
  if (src.empty()) { return stamp(dst_name, dst, Lnast_range::make_unbounded()); }
  Lnast_range result = range_of_operand(src[0]);
  for (std::size_t i = 1; i < src.size(); ++i) { result = result.mul(range_of_operand(src[i])); }
  return stamp(dst_name, dst, result);
}

upass::Vote uPass_bitwidth::process_div(std::string_view dst_name, Bundle& dst, upass::Src_span src) {
  // |a / d| <= |a| for any integer |d| >= 1 (Goal 1n N5).
  if (src.size() < 2) { return stamp(dst_name, dst, Lnast_range::make_unbounded()); }
  return stamp(dst_name, dst, range_of_operand(src[0]).div(range_of_operand(src[1])));
}

upass::Vote uPass_bitwidth::process_mod(std::string_view dst_name, Bundle& dst, upass::Src_span src) {
  // |a % d| < |d| and <= |a|; sign follows the dividend (Goal 1n N5).
  if (src.size() < 2) { return stamp(dst_name, dst, Lnast_range::make_unbounded()); }
  return stamp(dst_name, dst, range_of_operand(src[0]).mod(range_of_operand(src[1])));
}

upass::Vote uPass_bitwidth::process_shl(std::string_view dst_name, Bundle& dst, upass::Src_span src) {
  if (src.size() < 2) { return stamp(dst_name, dst, Lnast_range::make_unbounded()); }
  const auto amt = range_of_operand(src[1]);
  check_shift_amount(amt.is_unbounded() ? envelope_of_operand(src[1]) : amt);
  return stamp(dst_name, dst, range_of_operand(src[0]).shl(amt));
}

upass::Vote uPass_bitwidth::process_sra(std::string_view dst_name, Bundle& dst, upass::Src_span src) {
  if (src.size() < 2) { return stamp(dst_name, dst, Lnast_range::make_unbounded()); }
  const auto amt = range_of_operand(src[1]);
  check_shift_amount(amt.is_unbounded() ? envelope_of_operand(src[1]) : amt);
  return stamp(dst_name, dst, range_of_operand(src[0]).sra(amt));
}

// Bitwise ops — conservative: join of operand ranges (not tight).
upass::Vote uPass_bitwidth::process_bit_and(std::string_view dst_name, Bundle& dst, upass::Src_span src) {
  if (src.empty()) { return stamp(dst_name, dst, Lnast_range::make_unbounded()); }
  Lnast_range result = range_of_operand(src[0]);
  for (std::size_t i = 1; i < src.size(); ++i) { result = result.band(range_of_operand(src[i])); }
  return stamp(dst_name, dst, result);
}

upass::Vote uPass_bitwidth::process_bit_or(std::string_view dst_name, Bundle& dst, upass::Src_span src) {
  if (src.empty()) { return stamp(dst_name, dst, Lnast_range::make_unbounded()); }
  Lnast_range result = range_of_operand(src[0]);
  for (std::size_t i = 1; i < src.size(); ++i) { result = result.bor(range_of_operand(src[i])); }
  return stamp(dst_name, dst, result);
}

upass::Vote uPass_bitwidth::process_bit_xor(std::string_view dst_name, Bundle& dst, upass::Src_span src) {
  if (src.empty()) { return stamp(dst_name, dst, Lnast_range::make_unbounded()); }
  Lnast_range result = range_of_operand(src[0]);
  for (std::size_t i = 1; i < src.size(); ++i) { result = result.bxor(range_of_operand(src[i])); }
  return stamp(dst_name, dst, result);
}

upass::Vote uPass_bitwidth::process_bit_not(std::string_view dst_name, Bundle& dst, upass::Src_span src) {
  // ~x = -x - 1, exactly.
  if (src.empty()) { return stamp(dst_name, dst, Lnast_range::make_unbounded()); }
  return stamp(dst_name, dst, range_of_operand(src[0]).bnot());
}

// Logical ops / reductions / comparisons — result is always boolean.
upass::Vote uPass_bitwidth::process_log_and(std::string_view dst_name, Bundle& dst, upass::Src_span) { return stamp(dst_name, dst, Lnast_range::boolean()); }
upass::Vote uPass_bitwidth::process_log_or(std::string_view dst_name, Bundle& dst, upass::Src_span) { return stamp(dst_name, dst, Lnast_range::boolean()); }
upass::Vote uPass_bitwidth::process_log_not(std::string_view dst_name, Bundle& dst, upass::Src_span) { return stamp(dst_name, dst, Lnast_range::boolean()); }
upass::Vote uPass_bitwidth::process_red_or(std::string_view dst_name, Bundle& dst, upass::Src_span) { return stamp(dst_name, dst, Lnast_range::boolean()); }
upass::Vote uPass_bitwidth::process_red_and(std::string_view dst_name, Bundle& dst, upass::Src_span) { return stamp(dst_name, dst, Lnast_range::boolean()); }
upass::Vote uPass_bitwidth::process_red_xor(std::string_view dst_name, Bundle& dst, upass::Src_span) { return stamp(dst_name, dst, Lnast_range::boolean()); }
upass::Vote uPass_bitwidth::process_ne(std::string_view dst_name, Bundle& dst, upass::Src_span) { return stamp(dst_name, dst, Lnast_range::boolean()); }
upass::Vote uPass_bitwidth::process_eq(std::string_view dst_name, Bundle& dst, upass::Src_span) { return stamp(dst_name, dst, Lnast_range::boolean()); }
upass::Vote uPass_bitwidth::process_lt(std::string_view dst_name, Bundle& dst, upass::Src_span) { return stamp(dst_name, dst, Lnast_range::boolean()); }
upass::Vote uPass_bitwidth::process_le(std::string_view dst_name, Bundle& dst, upass::Src_span) { return stamp(dst_name, dst, Lnast_range::boolean()); }
upass::Vote uPass_bitwidth::process_gt(std::string_view dst_name, Bundle& dst, upass::Src_span) { return stamp(dst_name, dst, Lnast_range::boolean()); }
upass::Vote uPass_bitwidth::process_ge(std::string_view dst_name, Bundle& dst, upass::Src_span) { return stamp(dst_name, dst, Lnast_range::boolean()); }
// clang-format on

// popcount (`a#+[..]`) — set-bit count, not boolean. Always in
// [0, sbits(input)]: an n-bit value has at most n set bits.
upass::Vote uPass_bitwidth::process_popcount(std::string_view dst_name, Bundle& dst, upass::Src_span src) {
  if (src.empty()) {
    return stamp(dst_name, dst, Lnast_range::make_unbounded());
  }
  Lnast_range result;
  result.min       = 0;
  result.max       = range_of_operand(src[0]).get_sbits();
  result.unbounded = false;
  return stamp(dst_name, dst, result);
}

upass::Vote uPass_bitwidth::process_sext(std::string_view dst_name, Bundle& dst, upass::Src_span src) {
  // sext(value, sign_bit_pos): result in [-2^p, 2^p - 1] for a constant
  // position p (Goal 1n N5). src[1] is the sign-bit POSITION (0-indexed).
  if (src.size() < 2) {
    return stamp(dst_name, dst, Lnast_range::make_unbounded());
  }
  const auto pos = range_of_operand(src[1]);
  if (!pos.is_constant()) {
    return stamp(dst_name, dst, Lnast_range::make_unbounded());
  }
  return stamp(dst_name, dst, Lnast_range::sext_to(pos.min));
}

// get_mask(base, mask) — the selected bits packed LSB-first as an UNSIGNED
// value (task 1b: this is the "force" operator). For a known non-negative mask
// of width w = popcount(mask), the result lies in [0, 2^w − 1]. A single
// selected bit yields the signed 1-bit value (-1/0), matching Dlop::get_mask_op
// and the `x#[i]` semantics. Negative (carve-out) or unknown masks stay
// conservative — their width depends on the source, unknown here.
upass::Vote uPass_bitwidth::process_get_mask(std::string_view dst_name, Bundle& dst, upass::Src_span src) {
  if (src.size() < 2) {
    return stamp(dst_name, dst, Lnast_range::make_unbounded());
  }
  const auto mask = range_of_operand(src[1]);
  if (!mask.is_constant() || mask.min < 0) {
    return stamp(dst_name, dst, Lnast_range::make_unbounded());
  }
  int w = std::popcount(static_cast<uint64_t>(mask.min));
  if (w == 0) {
    return stamp(dst_name, dst, Lnast_range::constant(0));
  }
  if (w == 1) {
    return stamp(dst_name, dst, Lnast_range::boolean());  // signed 1-bit [-1, 0]
  }
  if (w >= 63) {
    return stamp(dst_name, dst, Lnast_range::make_unbounded());
  }
  Lnast_range r;
  r.min       = 0;
  r.max       = (int64_t{1} << w) - 1;
  r.unbounded = false;
  return stamp(dst_name, dst, r);
}

upass::Vote uPass_bitwidth::process_set_mask(std::string_view dst_name, Bundle& dst, upass::Src_span src) {
  // set_mask(base, mask, value) — writing bits outside the base's declared
  // storage is an overflow at this node.
  if (src.size() >= 2 && !src[0].name.empty()) {
    const auto mask = range_of_operand(src[1]);
    if (mask.is_constant() && mask.min >= 0) {
      const auto base = ssa_base_name(src[0].name);
      if (auto env = decl_envelope_of(base)) {
        if (mask_touches_outside_bits(mask.min, storage_bits_for_env(*env))) {
          record_overflow(base, mask, *env);
        }
      }
    }
  }
  return stamp(dst_name, dst, Lnast_range::make_unbounded());
}

// ── Nullary hooks ────────────────────────────────────────────────────────────

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
  // the does-not-fit check at its next write.
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

void uPass_bitwidth::process_type_spec() {
  // type_spec(ref(var), prim_type_int(max,min)) — the runner's declare
  // pre-step bakes envelopes only into EXISTING bindings; a bare type_spec
  // target (a tmp) is unbound in this pass's standalone runner (no constprop
  // creates it), so ensure the binding and bake the envelope here.
  if (runner_st == nullptr || !move_to_child()) {
    return;
  }
  const std::string var{current_text()};
  std::optional<Dlop> dmax;
  std::optional<Dlop> dmin;
  if (move_to_sibling() && Lnast_ntype::is_prim_type_int(get_raw_ntype())) {
    if (move_to_child()) {
      if (Lnast_ntype::is_const(get_raw_ntype())) {
        if (auto v = Dlop::from_pyrope(current_text()); v->is_integer()) {
          dmax = *v;
        }
      }
      if (move_to_sibling() && Lnast_ntype::is_const(get_raw_ntype())) {
        if (auto v = Dlop::from_pyrope(current_text()); v->is_integer()) {
          dmin = *v;
        }
      }
      move_to_parent();
    }
  }
  move_to_parent();

  if (var.empty() || var.find('.') != std::string::npos || !dmax || !dmin) {
    return;
  }
  if (!runner_st->has_known(var)) {
    (void)runner_st->set(var, std::make_shared<Bundle>(var));
  }
  auto b = runner_st->get_bundle_for_write(var);
  if (!b || (!b->is_empty() && !b->has_trivial("0"))) {
    return;
  }
  Bundle::Entry e = b->get_entry("0");
  e.immutable     = false;
  e.decl_max      = *dmax;
  e.decl_min      = *dmin;
  b->set("0", std::move(e));
}

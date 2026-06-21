// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

// Backend-agnostic combinational arithmetic expansion for pass.abc (2i-abc_arith).
//
// Non-trivial cells (Sum, comparators) are expanded into a network of trivial
// gates here, then handed to ABC's normal optimize+map flow (the chosen adder
// only seeds the AIG; ABC is free to re-synthesize it). The builders speak an
// abstract `Ops` bit-algebra so they carry no ABC dependency and can be unit
// tested against a plain software bit model (see abc_arith_test.cpp). abc_map
// instantiates them with Bit = Abc_Obj_t* and an Ops that emits Hop nodes.
//
// `Ops` is any type exposing (Bit is a small copyable handle):
//     Bit zero();  Bit one();  Bit inv(Bit);
//     Bit and_(Bit, Bit);  Bit or_(Bit, Bit);  Bit xor_(Bit, Bit);
//
// All bit vectors are LSB-first and equal length (== the operating width W).
//
// Extending to new architectures (e.g. Han-Carlson, Sklansky) or new complex
// cells (the barrel shifter is next, see todo) means adding a builder here plus
// a dispatch arm in abc_map.cpp; nothing else changes.

#include <algorithm>
#include <optional>
#include <string_view>
#include <vector>

namespace livehd::abc::arith {

enum class Adder_kind { rca, cska, cla };

inline std::optional<Adder_kind> parse_adder_kind(std::string_view s) {
  if (s == "rca") {
    return Adder_kind::rca;
  }
  if (s == "cska") {
    return Adder_kind::cska;
  }
  if (s == "cla") {
    return Adder_kind::cla;
  }
  return std::nullopt;
}

// Combinational multiplier architecture (mirrors Adder_kind). `array` is the
// simple single-cycle shift-and-add array multiplier (the default and, for now,
// the only reasonable option). The enum exists so future architectures (Booth,
// Wallace/Dadda tree) can be added with a new builder + dispatch arm, exactly as
// the adder library is extended — the partial-product summation already reuses
// the selected Adder_kind, so an `array` multiplier under any adder is available.
enum class Mult_kind { array };

inline std::optional<Mult_kind> parse_mult_kind(std::string_view s) {
  if (s == "array") {
    return Mult_kind::array;
  }
  return std::nullopt;
}

// Default CSKA skip-block / CLA lookahead-group width derived from the adder
// width W: a few wide blocks for big adders, halves for medium, a single block
// for small ones. Always >= 1. (RCA ignores block size.)
inline int default_block_size(int w) {
  int bs = w > 16 ? w / 4 : (w > 8 ? w / 2 : w);
  return bs < 1 ? 1 : bs;
}

template <class Bit>
struct Add_result {
  std::vector<Bit> sum;
  Bit              carry_out;
};

// sum = a ^ b ^ cin ; cout = majority(a,b,cin) = (a&b) | (cin & (a^b)).
template <class Bit, class Ops>
inline void full_adder(Ops& ops, Bit a, Bit b, Bit cin, Bit& sum, Bit& cout) {
  Bit axb = ops.xor_(a, b);
  sum     = ops.xor_(axb, cin);
  cout    = ops.or_(ops.and_(a, b), ops.and_(cin, axb));
}

template <class Bit, class Ops>
inline std::vector<Bit> bv_invert(Ops& ops, const std::vector<Bit>& v) {
  std::vector<Bit> r;
  r.reserve(v.size());
  for (const auto& b : v) {
    r.push_back(ops.inv(b));
  }
  return r;
}

// Ripple-carry: one full-adder per bit, carry chained LSB -> MSB.
template <class Bit, class Ops>
inline Add_result<Bit> rca_add(Ops& ops, const std::vector<Bit>& a, const std::vector<Bit>& b, Bit cin) {
  int             w = static_cast<int>(a.size());
  Add_result<Bit> r;
  r.sum.resize(w);
  Bit carry = cin;
  for (int i = 0; i < w; ++i) {
    Bit s;
    Bit c;
    full_adder(ops, a[i], b[i], carry, s, c);
    r.sum[i] = s;
    carry    = c;
  }
  r.carry_out = carry;
  return r;
}

// Carry-skip: blocks of block_size ripple internally; a block whose bits all
// propagate (p_i = a_i^b_i all 1) lets the incoming carry skip to the block
// output via `ripple_cout | (P_block & block_cin)`. Functionally identical to
// RCA (block_cout == ripple_cout); the skip OR is the structural distinction
// that seeds a faster carry path for ABC.
template <class Bit, class Ops>
inline Add_result<Bit> cska_add(Ops& ops, const std::vector<Bit>& a, const std::vector<Bit>& b, Bit cin, int block_size) {
  int w = static_cast<int>(a.size());
  if (block_size < 1) {
    block_size = 1;
  }
  Add_result<Bit> r;
  r.sum.resize(w);
  Bit block_cin = cin;
  for (int base = 0; base < w; base += block_size) {
    int  hi    = std::min(base + block_size, w);
    Bit  carry = block_cin;
    Bit  pall{};  // value-init: inner loop always runs (hi>base), so this is overwritten before use; silences -Wmaybe-uninitialized
    bool first = true;
    for (int i = base; i < hi; ++i) {
      Bit p = ops.xor_(a[i], b[i]);
      Bit s;
      Bit c;
      full_adder(ops, a[i], b[i], carry, s, c);
      r.sum[i] = s;
      carry    = c;
      pall     = first ? p : ops.and_(pall, p);
      first    = false;
    }
    // block carry-out = ripple carry-out OR (all-propagate AND block carry-in)
    block_cin = ops.or_(carry, ops.and_(pall, block_cin));
  }
  r.carry_out = block_cin;
  return r;
}

// Carry-lookahead: within each block of block_size, every carry is computed
// directly (flattened) from the block carry-in and the per-bit generate/
// propagate (g_i=a_i&b_i, p_i=a_i^b_i), so the in-block carry path is shallow;
// block carries ripple between blocks (single-level CLA).
template <class Bit, class Ops>
inline Add_result<Bit> cla_add(Ops& ops, const std::vector<Bit>& a, const std::vector<Bit>& b, Bit cin, int block_size) {
  int w = static_cast<int>(a.size());
  if (block_size < 1) {
    block_size = 1;
  }
  Add_result<Bit> r;
  r.sum.resize(w);
  Bit block_cin = cin;
  for (int base = 0; base < w; base += block_size) {
    int              hi = std::min(base + block_size, w);
    int              n  = hi - base;
    std::vector<Bit> g(n);
    std::vector<Bit> p(n);
    for (int k = 0; k < n; ++k) {
      g[k] = ops.and_(a[base + k], b[base + k]);
      p[k] = ops.xor_(a[base + k], b[base + k]);
    }
    Bit carry = block_cin;  // carry into the current bit (c_k)
    for (int k = 0; k < n; ++k) {
      r.sum[base + k] = ops.xor_(p[k], carry);
      // c_{k+1} = g_k | (p_k & g_{k-1}) | ... | (p_k..p_1 & g_0) | (p_k..p_0 & block_cin)
      Bit acc         = g[k];
      Bit prodp       = p[k];
      for (int j = k - 1; j >= 0; --j) {
        acc   = ops.or_(acc, ops.and_(prodp, g[j]));
        prodp = ops.and_(prodp, p[j]);
      }
      carry = ops.or_(acc, ops.and_(prodp, block_cin));
    }
    block_cin = carry;
  }
  r.carry_out = block_cin;
  return r;
}

// Dispatch on the selected architecture. `a`, `b` equal length; `cin` the
// incoming carry (one() for the +1 of a two's-complement subtract).
template <class Bit, class Ops>
inline Add_result<Bit> build_add(Adder_kind kind, int block_size, Ops& ops, const std::vector<Bit>& a, const std::vector<Bit>& b,
                                 Bit cin) {
  switch (kind) {
    case Adder_kind::cska: return cska_add(ops, a, b, cin, block_size);
    case Adder_kind::cla : return cla_add(ops, a, b, cin, block_size);
    case Adder_kind::rca : break;
  }
  return rca_add(ops, a, b, cin);
}

// a < b via the chosen subtractor: d = a + ~b + 1. Unsigned: a<b iff the
// subtract borrows (no carry-out). Signed: callers pass operands extended by
// one guard bit so a-b cannot overflow W bits, hence the sign bit d[W-1] is the
// comparison result (mixed sign works: an unsigned operand zero-extends into a
// non-negative signed value).
template <class Bit, class Ops>
inline Bit build_lt(Adder_kind kind, int block_size, Ops& ops, const std::vector<Bit>& a, const std::vector<Bit>& b,
                    bool is_unsigned) {
  auto diff = build_add(kind, block_size, ops, a, bv_invert(ops, b), ops.one());
  if (is_unsigned) {
    return ops.inv(diff.carry_out);
  }
  return diff.sum[a.size() - 1];
}

// Logical left shift `a << amount`, result truncated to `out_w` bits (out_w is
// the bitwidth-resolved result width, already grown to hold the shift — the
// cvc5 LEC encodes SHL the same way: fit `a` to W, BITVECTOR_SHL at W). `a` is
// LSB-first; `amount` is the LSB-first shift-count bit vector, read UNSIGNED (a
// shift count is a bit position; the negative-shift diagnostic lives in
// upass.bitwidth, so any amount reaching here is non-negative). Built as a
// barrel / log-shifter: one 2:1-mux level per amount bit k conditionally shifts
// the running data by 2^k. A shift of 2^k >= out_w pushes the value entirely
// out of the truncated result (=> 0), so high amount bits correctly force a 0
// result. Needs only zero/inv/and_/or_ from Ops (mux built inline). A constant
// amount works too (the muxes fold), but abc_map wires constants directly.
template <class Bit, class Ops>
inline std::vector<Bit> build_shl(Ops& ops, const std::vector<Bit>& a, const std::vector<Bit>& amount, int out_w) {
  std::vector<Bit> data(out_w);
  int              aw = static_cast<int>(a.size());
  for (int i = 0; i < out_w; ++i) {
    data[i] = i < aw ? a[i] : ops.zero();  // a, zero-extended to the result width
  }
  int nb = static_cast<int>(amount.size());
  for (int k = 0; k < nb; ++k) {
    // sh = 2^k, capped to out_w (any shift >= out_w shifts everything out). The
    // k >= 31 guard keeps 1<<k from overflowing int before the >= out_w compare.
    int              sh   = (k >= 31 || (int64_t{1} << k) >= out_w) ? out_w : static_cast<int>(int64_t{1} << k);
    Bit              sel  = amount[k];
    Bit              nsel = ops.inv(sel);
    std::vector<Bit> next(out_w);
    for (int i = 0; i < out_w; ++i) {
      Bit shifted = (i - sh) >= 0 ? data[i - sh] : ops.zero();                 // i-sh < out_w always (sh >= 1)
      next[i]     = ops.or_(ops.and_(sel, shifted), ops.and_(nsel, data[i]));  // sel ? shifted : data
    }
    data = std::move(next);
  }
  return data;
}

// Right shift `a >> amount`, operating at the full width W == a.size(), with
// `fill` shifted in from the top of every bit pulled past the MSB. Logical right
// shift passes fill = zero(); arithmetic (sign-replicating) right shift passes
// fill = a[W-1] (the sign bit). `a` is LSB-first; `amount` is the LSB-first
// shift-count bit vector, read UNSIGNED (a shift count is a bit position). Built
// as a barrel / log-shifter mirroring build_shl: one 2:1-mux level per amount bit
// k conditionally shifts the running data DOWN by 2^k. A shift of 2^k >= W pulls
// every bit past the MSB (=> all fill), so high amount bits correctly force an
// all-fill result. The caller fits `a` and `amount` to the LEC's shift width
// (cw = max(operand_width, output_width)) and truncates the result to the output
// width, matching cvc5's BITVECTOR_LSHR / BITVECTOR_ASHR at cw. Needs only
// zero/inv/and_/or_ from Ops (mux built inline).
template <class Bit, class Ops>
inline std::vector<Bit> build_shr(Ops& ops, const std::vector<Bit>& a, const std::vector<Bit>& amount, Bit fill) {
  int              w    = static_cast<int>(a.size());
  std::vector<Bit> data = a;
  int              nb   = static_cast<int>(amount.size());
  for (int k = 0; k < nb; ++k) {
    // sh = 2^k, capped to w (any shift >= w pulls everything past the MSB). The
    // k >= 31 guard keeps 1<<k from overflowing int before the >= w compare.
    int              sh   = (k >= 31 || (int64_t{1} << k) >= w) ? w : static_cast<int>(int64_t{1} << k);
    Bit              sel  = amount[k];
    Bit              nsel = ops.inv(sel);
    std::vector<Bit> next(w);
    for (int i = 0; i < w; ++i) {
      Bit shifted = (i + sh) < w ? data[i + sh] : fill;                       // bit i takes bit i+sh, or fill past the top
      next[i]     = ops.or_(ops.and_(sel, shifted), ops.and_(nsel, data[i]));  // sel ? shifted : data
    }
    data = std::move(next);
  }
  return data;
}

// Unsigned array multiplier `a * b`, result truncated to out_w bits: the sum of
// shifted partial products pp_k = (b_k ? a : 0) << k, accumulated with the chosen
// adder architecture (one add per b bit). Both operands are pre-extended to out_w
// by the caller (sign-extended for a signed operand, zero-extended otherwise), so
// the product is computed mod 2^out_w and its low out_w bits are correct for
// signed and unsigned operands alike — exactly what the cvc5 LEC encodes
// (fit each operand to W, then BITVECTOR_MULT, which is mod 2^W). The `kind`
// selects the multiplier architecture (only `array` today); `adder`/`block_size`
// pick the architecture of the internal partial-product additions.
template <class Bit, class Ops>
inline std::vector<Bit> build_mul(Mult_kind kind, Adder_kind adder, int block_size, Ops& ops, const std::vector<Bit>& a,
                                  const std::vector<Bit>& b, int out_w) {
  (void)kind;  // only `array` for now; the dispatch point for future architectures
  int              aw = static_cast<int>(a.size());
  int              bw = static_cast<int>(b.size());
  std::vector<Bit> acc(out_w, ops.zero());
  for (int k = 0; k < bw && k < out_w; ++k) {
    // partial product k = (b[k] ? a : 0) shifted up by k, within out_w bits
    std::vector<Bit> pp(out_w);
    for (int i = 0; i < out_w; ++i) {
      int j = i - k;
      pp[i] = (j >= 0 && j < aw) ? ops.and_(b[k], a[j]) : ops.zero();
    }
    acc = build_add(adder, block_size, ops, acc, pp, ops.zero()).sum;
  }
  return acc;
}

// All operands equal the first, bitwise (n-ary ==). Operands equal length.
template <class Bit, class Ops>
inline Bit build_eq(Ops& ops, const std::vector<std::vector<Bit>>& operands) {
  if (operands.size() <= 1) {
    return ops.one();
  }
  int w  = static_cast<int>(operands[0].size());
  Bit eq = ops.one();
  for (size_t k = 1; k < operands.size(); ++k) {
    for (int b = 0; b < w; ++b) {
      eq = ops.and_(eq, ops.inv(ops.xor_(operands[k][b], operands[0][b])));
    }
  }
  return eq;
}

}  // namespace livehd::abc::arith

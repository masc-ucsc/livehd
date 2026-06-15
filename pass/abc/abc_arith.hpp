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
    int hi    = std::min(base + block_size, w);
    Bit carry = block_cin;
    Bit pall;
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
      Bit acc   = g[k];
      Bit prodp = p[k];
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
inline Add_result<Bit> build_add(Adder_kind kind, int block_size, Ops& ops, const std::vector<Bit>& a,
                                 const std::vector<Bit>& b, Bit cin) {
  switch (kind) {
    case Adder_kind::cska: return cska_add(ops, a, b, cin, block_size);
    case Adder_kind::cla: return cla_add(ops, a, b, cin, block_size);
    case Adder_kind::rca: break;
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

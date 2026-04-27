// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// lconst_test2: newer-generation tests for the Lconst layer. Splits off
// from lconst_test.cpp to keep the legacy uint/sint parity suite focused
// on its original scope. This file collects:
//
//   • String semantics — packing, escape decoding, lsh/rsh shifts.
//   • Concat semantics — string++string vs bit-concat.
//   • Pyrope quote forms — `'…'` vs `"…"`.
//   • nil — the tagged unit value, propagation, comparisons.
//   • Unknowns (`0sb?…`) — arithmetic widening and comparison results.

#include <print>
#include <string>

#include "gtest/gtest.h"
#include "lconst.hpp"

class Lconst2_test : public ::testing::Test {};

// ─── String packing, lsh, rsh ───────────────────────────────────────────────

// from_string packs bytes little-endian: first char goes to LSB, so
// rsh_op(8) drops the leading character. Earlier the code called
// to_pyrope().substr(amount) — `amount` is in bits while substr counts
// characters, and to_pyrope wraps strings in `'…'`, so any small shift
// returned an empty/garbage string.
TEST_F(Lconst2_test, string_rsh_drops_leading_byte) {
  auto src = Lconst::from_string("cadena");
  EXPECT_EQ(src.rsh_op(8), Lconst::from_string("adena"));
  EXPECT_EQ(src.rsh_op(16), Lconst::from_string("dena"));
  EXPECT_EQ(src.rsh_op(40), Lconst::from_string("a"));
  // Both factory paths must round-trip identically.
  EXPECT_EQ(Lconst::from_pyrope("'cadena'").rsh_op(8), Lconst::from_string("adena"));
  // Shifting past the string length yields the empty/zero Lconst.
  EXPECT_EQ(src.rsh_op(48).get_bits(), 0u);
  EXPECT_EQ(src.rsh_op(64).get_bits(), 0u);
}

// lsh_op on a byte-aligned amount must prepend that many zero bytes (LSB
// is the first character in from_string's little-endian packing). The
// previous implementation derived the result's bit count from the numeric
// msb (calc_num_bits), so any string whose new top byte had a sub-byte
// msb under-counted the width and silently lost a leading zero byte.
TEST_F(Lconst2_test, string_lsh_prepends_zero_byte) {
  // Easy case: top byte of "ab" is 'a' (0x61, msb at bit 6) — calc_num_bits
  // happened to give the right width here, masking the bug.
  {
    auto r = Lconst::from_string("ab").lsh_op(8);
    EXPECT_TRUE(r.is_string());
    EXPECT_EQ(r.get_bits(), 24u);
    EXPECT_EQ(r, Lconst::from_string(std::string("\0ab", 3)));
  }
  // Regression case: top byte is 0x01 — bit-count would round down.
  {
    auto r = Lconst::from_string(std::string("\1\1", 2)).lsh_op(8);
    EXPECT_TRUE(r.is_string());
    EXPECT_EQ(r.get_bits(), 24u);
    EXPECT_EQ(r, Lconst::from_string(std::string("\0\1\1", 3)));
  }
  // Multiple-byte shift on a normal string.
  {
    auto r = Lconst::from_string("hi").lsh_op(16);
    EXPECT_EQ(r.get_bits(), 32u);
    EXPECT_EQ(r, Lconst::from_string(std::string("\0\0hi", 4)));
  }
}

// Sub-byte lsh_op on a string is well-defined: the result has
// bits = orig_bits + amount. The low byte holds (val << amount) & 0xFF
// and the partial high byte carries the bits that overflow past 0xFF.
// All inputs use printable ASCII to side-step a separate `from_string`
// bug: it sign-extends high-bit bytes via `char`'s signed default, so
// e.g. '\xFF' rolls in as -1 instead of 0xFF.
TEST_F(Lconst2_test, string_lsh_sub_byte_shift) {
  auto raw = [](const Lconst& v) { return static_cast<uint64_t>(v.get_raw_num()); };
  // 'a' = 0x61 (top bit clear). 'a' << 1 = 0xC2 in 9 bits.
  // Low byte = (97<<1) & 0xFF = 0xC2; high partial byte = 0.
  {
    auto r = Lconst::from_string("a").lsh_op(1);
    EXPECT_TRUE(r.is_string());
    EXPECT_EQ(r.get_bits(), 9u);
    EXPECT_EQ(raw(r), 0xC2u);
    EXPECT_EQ(raw(r) & 0xFF, 0xC2u);
    EXPECT_EQ(raw(r) >> 8, 0u);
  }
  // 'a' << 8: byte-aligned, 16 bits, num = 0x6100. Low byte = 0, high = 'a'.
  {
    auto r = Lconst::from_string("a").lsh_op(8);
    EXPECT_TRUE(r.is_string());
    EXPECT_EQ(r.get_bits(), 16u);
    EXPECT_EQ(raw(r), 0x6100u);
  }
  // 'a' << 9: 17 bits, num = 0x61 << 9 = 0xC200.
  {
    auto r = Lconst::from_string("a").lsh_op(9);
    EXPECT_TRUE(r.is_string());
    EXPECT_EQ(r.get_bits(), 17u);
    EXPECT_EQ(raw(r), 0xC200u);
  }
  // "ab" = 0x6261 (16 bits). << 1 = 0xC4C2 in 17 bits.
  {
    auto r = Lconst::from_string("ab").lsh_op(1);
    EXPECT_TRUE(r.is_string());
    EXPECT_EQ(r.get_bits(), 17u);
    EXPECT_EQ(raw(r), 0xC4C2u);
  }
}

// ─── Equality: int / string collisions ──────────────────────────────────────

// operator== compares (num, bits) and ignores explicit_str — a single-byte
// string whose value matches an integer with the same calc_num_bits compares
// equal. This is intentional at the Lconst level (see the concat/eq notes).
TEST_F(Lconst2_test, string_int_eq) {
  auto s = Lconst::from_string("a");
  auto i = Lconst(0x61);
  EXPECT_TRUE(s.is_string());
  EXPECT_FALSE(i.is_string());
  EXPECT_EQ(s, i);
}

// ─── Pyrope quote forms ─────────────────────────────────────────────────────

// Pyrope source allows both `'…'` and `"…"` as string literals. from_pyrope
// strips either form to the same packed-byte representation.
TEST_F(Lconst2_test, from_pyrope_double_quoted_string) {
  auto a1 = Lconst::from_pyrope("\"hi\"");
  auto a2 = Lconst::from_pyrope("\'hi\'");
  auto b  = Lconst::from_string("hi");
  EXPECT_EQ(a1, b);
  EXPECT_EQ(a2, b);
}

// ─── from_string escape decoding ────────────────────────────────────────────

// Escape decoding lives in from_string (not from_pyrope, which keeps the
// `'…'` raw form). Pyrope's `"…"` is expected to flow through from_string
// so each `\n`, `\xNN`, etc. becomes a single byte.
TEST_F(Lconst2_test, from_string_decodes_escape_sequences) {
  // \n → 0x0a
  EXPECT_EQ(Lconst::from_string("\\n"), Lconst::from_string("\n"));
  // \xNN hex byte
  EXPECT_EQ(Lconst::from_string("\\x68i"), Lconst::from_string("hi"));
  EXPECT_EQ(Lconst::from_string("hello\\x0a"), Lconst::from_string("hello\n"));
  // \\ → single backslash; result is 1 byte.
  EXPECT_EQ(Lconst::from_string("\\\\").get_bits(), 8u);
  // Common escapes.
  EXPECT_EQ(Lconst::from_string("\\'"), Lconst::from_string("'"));
  EXPECT_EQ(Lconst::from_string("\\\""), Lconst::from_string("\""));
  EXPECT_EQ(Lconst::from_string("a\\rb"), Lconst::from_string("a\rb"));
  EXPECT_EQ(Lconst::from_string("a\\tb"), Lconst::from_string("a\tb"));
  // \0 inside a string keeps width — the trailing NUL is part of the byte
  // pack even if to_string() round-trip drops it.
  EXPECT_EQ(Lconst::from_string("a\\0b").get_bits(), 24u);
  // Unknown escapes pass through literally (\z stays as backslash + z).
  EXPECT_EQ(Lconst::from_string("\\z").get_bits(), 16u);
  // Lone trailing backslash is kept literal.
  EXPECT_EQ(Lconst::from_string("\\").get_bits(), 8u);
}

// ─── Concat semantics ───────────────────────────────────────────────────────

// concat_op: string ++ string is text concat; everything else (int ++ int,
// string ++ int, int ++ string) is bit-concat producing an integer Lconst.
// Callers wanting "v=4" decimal text must stringify the int explicitly.
TEST_F(Lconst2_test, concat_string_with_int_is_bit_concat) {
  // Mixed string + int → int Lconst (bit-concat: this << o.bits | o.num).
  // "v=" has bits=16, num=0x3D76; Lconst(4) has bits=4, num=0x4.
  // Bit-concat: (0x3D76 << 4) | 4 = 0x3D764.
  auto r = Lconst::from_string("v=").concat_op(Lconst(4));
  EXPECT_FALSE(r.is_string());
  EXPECT_EQ(static_cast<uint64_t>(r.get_raw_num()), 0x3D764u);

  // String ++ string remains text concat: produces a string Lconst with
  // first operand at LSB (so left-to-right reading order is preserved).
  auto r2 = Lconst::from_string("hello").concat_op(Lconst::from_string(" world"));
  EXPECT_TRUE(r2.is_string());
  EXPECT_EQ(r2, Lconst::from_string("hello world"));

  // Decimal text concat is opt-in: stringify the int first.
  auto stringify = [](const Lconst& v) {
    return v.is_string() ? v : Lconst::from_string(std::to_string(v.to_i()));
  };
  auto r3 = Lconst::from_string("v=").concat_op(stringify(Lconst(4)));
  EXPECT_TRUE(r3.is_string());
  EXPECT_EQ(r3, Lconst::from_string("v=4"));
}

// ─── nil ────────────────────────────────────────────────────────────────────

// nil is its own type — distinct from int 0, false, invalid, and the
// 3-byte string "nil". Only `Lconst::nil()` and `from_pyrope("nil")` (no
// quotes) produce a nil; quoted forms remain a 3-byte string.
TEST_F(Lconst2_test, nil_basics) {
  auto n = Lconst::nil();
  EXPECT_TRUE(n.is_nil());
  EXPECT_FALSE(n.is_invalid());
  EXPECT_FALSE(n.is_string());
  EXPECT_FALSE(n.is_i());
  EXPECT_FALSE(n.is_negative());
  EXPECT_FALSE(n.is_positive());
  EXPECT_FALSE(n.is_known_true());
  EXPECT_FALSE(n.is_known_false());
  EXPECT_EQ(n.get_bits(), 0u);

  // Distinct from int 0 / false / invalid.
  EXPECT_FALSE(Lconst(0).is_nil());
  EXPECT_FALSE(Lconst::from_pyrope("false").is_nil());
  EXPECT_FALSE(Lconst::invalid().is_nil());
  EXPECT_FALSE(Lconst::from_string("nil").is_nil());  // 3-byte string

  // from_pyrope and to_pyrope round-trip the bareword form.
  EXPECT_TRUE(Lconst::from_pyrope("nil").is_nil());
  EXPECT_EQ(n.to_pyrope(), "nil");

  // `'nil'` and `"nil"` are still 3-byte strings, NOT nil.
  EXPECT_FALSE(Lconst::from_pyrope("'nil'").is_nil());
  EXPECT_TRUE(Lconst::from_pyrope("'nil'").is_string());
  EXPECT_FALSE(Lconst::from_pyrope("\"nil\"").is_nil());
  EXPECT_TRUE(Lconst::from_pyrope("\"nil\"").is_string());
}

// nil propagates through arithmetic and bitwise ops: any op with a nil
// operand returns nil. Equality is the deliberate exception (see below).
TEST_F(Lconst2_test, nil_propagates_through_ops) {
  auto n = Lconst::nil();
  auto i = Lconst(7);

  EXPECT_TRUE(n.add_op(i).is_nil());
  EXPECT_TRUE(i.add_op(n).is_nil());
  EXPECT_TRUE(n.sub_op(i).is_nil());
  EXPECT_TRUE(n.mult_op(i).is_nil());
  EXPECT_TRUE(n.div_op(i).is_nil());
  EXPECT_TRUE(n.and_op(i).is_nil());
  EXPECT_TRUE(n.or_op(i).is_nil());
  EXPECT_TRUE(n.not_op().is_nil());
  EXPECT_TRUE(n.neg_op().is_nil());
  EXPECT_TRUE(n.lsh_op(2).is_nil());
  EXPECT_TRUE(n.rsh_op(2).is_nil());
  EXPECT_TRUE(n.concat_op(i).is_nil());
  EXPECT_TRUE(i.concat_op(n).is_nil());
  EXPECT_TRUE(n.sext_op(4).is_nil());
  EXPECT_TRUE(n.get_mask_op().is_nil());
  EXPECT_TRUE(n.adjust_bits(4).is_nil());
}

// Equality is intentionally NOT nil-propagating — it is a decision-producing
// op. Pyrope code uses `uint("-1") == nil` to detect cast failures, which
// requires `nil == nil` to fold to true.
TEST_F(Lconst2_test, nil_equality) {
  auto n  = Lconst::nil();
  auto i7 = Lconst(7);

  // nil == nil → true (Lconst(-1)).
  auto eq_nn = n.eq_op(n);
  EXPECT_FALSE(eq_nn.is_nil());
  EXPECT_TRUE(eq_nn.is_known_true());

  // nil == 7 → false.
  auto eq_ni = n.eq_op(i7);
  EXPECT_FALSE(eq_ni.is_nil());
  EXPECT_TRUE(eq_ni.is_known_false());

  // 7 == nil → false (symmetric).
  auto eq_in = i7.eq_op(n);
  EXPECT_TRUE(eq_in.is_known_false());
}

// ─── Unknowns (0sb?…) arithmetic and comparison ─────────────────────────────

// Comparison with an unknown should yield a single-bit *unknown*, not nil:
// we don't know the answer, but the answer space is {0,1}. nil represents
// "no value" / "cast failure", which is a different concept.
TEST_F(Lconst2_test, unknowns_compare_returns_unknown_bit) {
  auto a = Lconst::from_pyrope("0sb?");
  auto r = a.eq_op(Lconst(1));
  // The expected result is a single unknown bit (representable as 0sb?
  // or 0b? — a 1-bit Lconst with the unknown flag), NOT a nil.
  EXPECT_FALSE(r.is_nil());
  EXPECT_TRUE(r.has_unknowns());
}

// Structural identity: comparing identically-shaped unknowns folds to
// known-true at comptime. `(v != 0) == 0sb?` relies on this — `v != 0`
// produces `0sb?`, and the user-visible cassert asserts the result IS
// the literal 0sb? bit pattern.
TEST_F(Lconst2_test, unknowns_compare_structural_identity) {
  auto a = Lconst::from_pyrope("0sb?");
  auto b = Lconst::from_pyrope("0sb?");
  EXPECT_TRUE(a.eq_op(b).is_known_true());

  auto c = Lconst::from_pyrope("0sb??");
  auto d = Lconst::from_pyrope("0sb??");
  EXPECT_TRUE(c.eq_op(d).is_known_true());
}

// Adding `1` to `0sb?` must widen the unknown by one bit while keeping
// signed semantics — the documented identity `0sb? + 1 == 0sb??`.
TEST_F(Lconst2_test, unknowns_add_one_to_0sb_q) {
  auto a = Lconst::from_pyrope("0sb?");
  auto r = a.add_op(Lconst(1));
  EXPECT_TRUE(r.has_unknowns());
  EXPECT_EQ(r, Lconst::from_pyrope("0sb??"));
}

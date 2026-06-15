// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// Functional verification of the pass.abc arithmetic builders (2i-abc_arith),
// independent of ABC/Liberty: instantiate the templates with a software bit
// model and check every adder architecture + comparator against reference
// arithmetic across a sweep of widths and block sizes.
//
// Bit = uint8_t (0/1) rather than bool: std::vector<bool> hands out a proxy
// reference that defeats the builders' template deduction; the real abc_map
// instantiation uses Bit = Abc_Obj_t* (a plain pointer), so uint8_t is the
// faithful stand-in.

#include "abc_arith.hpp"

#include <cstdint>
#include <vector>

#include "gtest/gtest.h"

using namespace livehd::abc::arith;

namespace {

using B = uint8_t;

// arith::Ops over 0/1 bytes.
struct ByteOps {
  B zero() { return 0; }
  B one() { return 1; }
  B inv(B a) { return a ? 0 : 1; }
  B and_(B a, B b) { return (a && b) ? 1 : 0; }
  B or_(B a, B b) { return (a || b) ? 1 : 0; }
  B xor_(B a, B b) { return ((a != 0) != (b != 0)) ? 1 : 0; }
};

std::vector<B> to_bits(uint64_t v, int w) {
  std::vector<B> r(w);
  for (int i = 0; i < w; ++i) {
    r[i] = static_cast<B>((v >> i) & 1U);
  }
  return r;
}

uint64_t from_bits(const std::vector<B>& v) {
  uint64_t r = 0;
  for (size_t i = 0; i < v.size(); ++i) {
    if (v[i]) {
      r |= (uint64_t{1} << i);
    }
  }
  return r;
}

constexpr Adder_kind kKinds[]  = {Adder_kind::rca, Adder_kind::cska, Adder_kind::cla};
constexpr int        kBlocks[] = {1, 2, 3, 4, 8};

}  // namespace

TEST(abc_arith, parse_and_default_block_size) {
  EXPECT_EQ(parse_adder_kind("rca").value(), Adder_kind::rca);
  EXPECT_EQ(parse_adder_kind("cska").value(), Adder_kind::cska);
  EXPECT_EQ(parse_adder_kind("cla").value(), Adder_kind::cla);
  EXPECT_FALSE(parse_adder_kind("nope").has_value());

  EXPECT_EQ(default_block_size(32), 8);  // >16 -> W/4
  EXPECT_EQ(default_block_size(24), 6);
  EXPECT_EQ(default_block_size(12), 6);  // >8 -> W/2
  EXPECT_EQ(default_block_size(9), 4);
  EXPECT_EQ(default_block_size(8), 8);  // <=8 -> W
  EXPECT_EQ(default_block_size(4), 4);
  EXPECT_GE(default_block_size(1), 1);
}

TEST(abc_arith, add_all_architectures) {
  ByteOps ops;
  for (int w : {1, 2, 4, 7, 8, 16, 32}) {
    uint64_t              mask    = (uint64_t{1} << w) - 1;
    std::vector<uint64_t> samples = {0, 1, 2, 3, mask, mask / 2, mask / 3, mask - 1};
    for (uint64_t a : samples) {
      for (uint64_t b : samples) {
        uint64_t A = a & mask;
        uint64_t B_ = b & mask;
        for (auto k : kKinds) {
          for (int bs : kBlocks) {
            for (int cin : {0, 1}) {
              auto     r        = build_add(k, bs, ops, to_bits(A, w), to_bits(B_, w), static_cast<B>(cin));
              uint64_t full     = A + B_ + static_cast<uint64_t>(cin);
              uint64_t exp_sum  = full & mask;
              B        exp_cout = static_cast<B>((full >> w) & 1U);
              EXPECT_EQ(from_bits(r.sum), exp_sum) << "w=" << w << " A=" << A << " B=" << B_ << " cin=" << cin
                                                   << " kind=" << static_cast<int>(k) << " bs=" << bs;
              EXPECT_EQ(r.carry_out, exp_cout) << "w=" << w << " A=" << A << " B=" << B_ << " cin=" << cin;
            }
          }
        }
      }
    }
  }
}

TEST(abc_arith, subtract_via_twos_complement) {
  ByteOps ops;
  for (int w : {4, 8, 16}) {
    uint64_t              mask    = (uint64_t{1} << w) - 1;
    std::vector<uint64_t> samples = {0, 1, 2, mask, mask / 2, mask / 3};
    for (uint64_t a : samples) {
      for (uint64_t b : samples) {
        uint64_t A  = a & mask;
        uint64_t B_ = b & mask;
        for (auto k : kKinds) {
          for (int bs : kBlocks) {
            auto r = build_add(k, bs, ops, to_bits(A, w), bv_invert(ops, to_bits(B_, w)), ops.one());
            EXPECT_EQ(from_bits(r.sum), (A - B_) & mask) << "w=" << w << " A=" << A << " B=" << B_;
          }
        }
      }
    }
  }
}

TEST(abc_arith, compare_unsigned) {
  ByteOps ops;
  for (int w : {1, 2, 4, 8}) {
    uint64_t hi = (uint64_t{1} << w) - 1;
    int      W  = w + 1;  // one guard bit
    for (uint64_t A = 0; A <= hi; ++A) {
      for (uint64_t B_ = 0; B_ <= hi; ++B_) {
        auto av = to_bits(A, W);
        auto bv = to_bits(B_, W);
        for (auto k : kKinds) {
          for (int bs : kBlocks) {
            EXPECT_EQ(build_lt(k, bs, ops, av, bv, /*is_unsigned=*/true) != 0, A < B_) << "A=" << A << " B=" << B_;
            EXPECT_EQ(build_lt(k, bs, ops, bv, av, /*is_unsigned=*/true) != 0, A > B_) << "A=" << A << " B=" << B_;
          }
        }
        EXPECT_EQ(build_eq<B>(ops, {to_bits(A, W), to_bits(B_, W)}) != 0, A == B_);
      }
    }
  }
}

TEST(abc_arith, compare_signed) {
  ByteOps ops;
  for (int w : {2, 4, 8}) {
    int64_t lo = -(int64_t{1} << (w - 1));
    int64_t hi = (int64_t{1} << (w - 1)) - 1;
    int     W  = w + 1;  // guard bit so a-b cannot overflow W bits
    for (int64_t A = lo; A <= hi; ++A) {
      for (int64_t B_ = lo; B_ <= hi; ++B_) {
        auto av = to_bits(static_cast<uint64_t>(A), W);  // two's complement, sign-extended
        auto bv = to_bits(static_cast<uint64_t>(B_), W);
        for (auto k : kKinds) {
          for (int bs : kBlocks) {
            EXPECT_EQ(build_lt(k, bs, ops, av, bv, /*is_unsigned=*/false) != 0, A < B_) << "A=" << A << " B=" << B_;
            EXPECT_EQ(build_lt(k, bs, ops, bv, av, /*is_unsigned=*/false) != 0, A > B_) << "A=" << A << " B=" << B_;
          }
        }
        EXPECT_EQ(build_eq<B>(ops, {av, bv}) != 0, A == B_);
      }
    }
  }
}

TEST(abc_arith, eq_nary) {
  ByteOps ops;
  EXPECT_NE(build_eq<B>(ops, {}), 0);                              // 0 operands -> true
  EXPECT_NE(build_eq<B>(ops, {to_bits(5, 8)}), 0);                 // 1 operand -> true
  EXPECT_NE(build_eq<B>(ops, {to_bits(5, 8), to_bits(5, 8)}), 0);  // equal pair
  EXPECT_EQ(build_eq<B>(ops, {to_bits(5, 8), to_bits(6, 8)}), 0);
  EXPECT_NE(build_eq<B>(ops, {to_bits(7, 8), to_bits(7, 8), to_bits(7, 8)}), 0);   // n-ary all equal
  EXPECT_EQ(build_eq<B>(ops, {to_bits(7, 8), to_bits(7, 8), to_bits(8, 8)}), 0);   // one differs
}

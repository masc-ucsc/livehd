// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include <cstdint>

#include "gtest/gtest.h"
#include "pass/abc/abc_arith.hpp"

namespace {
struct Bits {
  uint8_t zero() { return 0; }
  uint8_t one() { return 1; }
  uint8_t inv(uint8_t a) { return a ^ 1; }
  uint8_t and_(uint8_t a, uint8_t b) { return a & b; }
  uint8_t or_(uint8_t a, uint8_t b) { return a | b; }
  uint8_t xor_(uint8_t a, uint8_t b) { return a ^ b; }
};
std::vector<uint8_t> bits(uint64_t x, int w) {
  std::vector<uint8_t> r(w);
  for (int i = 0; i < w; ++i) {
    r[i] = (x >> i) & 1;
  }
  return r;
}
uint64_t value(const std::vector<uint8_t>& x) {
  uint64_t r = 0;
  for (size_t i = 0; i < x.size(); ++i) {
    r |= uint64_t{x[i]} << i;
  }
  return r;
}
}  // namespace

TEST(Division, ExhaustiveIndependentOperandSigns) {
  using namespace livehd::abc::arith;
  Bits ops;
  for (auto kind : {Adder_kind::rca, Adder_kind::cska, Adder_kind::cla}) {
    for (int w = 1; w <= 6; ++w) {
      const int limit = 1 << w;
      for (bool as : {false, true}) {
        for (bool bs : {false, true}) {
          for (int a = 0; a < limit; ++a) {
            const int av = as && a >= limit / 2 ? a - limit : a;
            for (int b = 0; b < limit; ++b) {
              const int  bv       = bs && b >= limit / 2 ? b - limit : b;
              const int  expected = bv == 0 ? (av < 0 ? 1 : -1) : av / bv;
              const auto q        = build_div(kind, 2, ops, bits(a, w), bits(b, w), as, bs);
              ASSERT_EQ(value(q), static_cast<uint64_t>(expected) & (limit - 1)) << "width=" << w << " a=" << av << " b=" << bv;
            }
          }
        }
      }
    }
  }
}

TEST(Division, FullWidthUnsignedAndSignedMinimum) {
  using namespace livehd::abc::arith;
  Bits ops;
  for (uint64_t a : {uint64_t{0}, uint64_t{1}, uint64_t{1} << 63, UINT64_MAX}) {
    for (uint64_t b : {uint64_t{1}, uint64_t{3}, uint64_t{1} << 63, UINT64_MAX}) {
      EXPECT_EQ(value(build_div(Adder_kind::rca, 4, ops, bits(a, 64), bits(b, 64), false, false)), a / b);
    }
  }
  EXPECT_EQ(value(build_div(Adder_kind::rca, 4, ops, bits(uint64_t{1} << 63, 64), bits(UINT64_MAX, 64), true, true)),
            uint64_t{1} << 63);
}

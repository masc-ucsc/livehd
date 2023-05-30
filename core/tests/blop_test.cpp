

#include "blop.hpp"

#include <cassert>
#include <functional>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <vector>

#include "dlop.hpp"
#include "fmt/format.h"
#include "gtest/gtest.h"
#include "lconst.hpp"
#include "lrand.hpp"

class Blop_test : public ::testing::Test {
protected:
public:
  void TearDown() override {}

  void SetUp() override{

  };
};

TEST_F(Blop_test, addition) {
  int64_t src1[3] = {-1, -1, 1};
  int64_t src2[3] = {-1, -1, 1};

  int64_t dst1[3];
  Blop::addn(dst1, 3, src1, src2);

  int64_t dst2[3];
  Blop::subn(dst2, 3, dst1, src2);

  fmt::print("dst1:{},{},{}\n", dst1[0], dst1[1], dst1[2]);
  fmt::print("dst2:{},{},{}\n", dst2[0], dst2[1], dst2[2]);
}

TEST_F(Blop_test, shift) {
  int64_t src1[3] = {-1, 2, -1};

  for (int64_t i = 0; i < 3; ++i) {
    int64_t dst1[3];
    Blop::shln(dst1, 3, src1, i);

    int64_t dst2[3];
    Blop::shrn(dst2, 3, dst1, i);

    fmt::print("<<{} dst1:{},{},{}\n", i, dst1[0], dst1[1], dst1[2]);
    fmt::print(">>{} dst2:{},{},{}\n", i, dst2[0], dst2[1], dst2[2]);

    EXPECT_EQ(dst2[0], src1[0]);
    EXPECT_EQ(dst2[1], src1[1]);
    EXPECT_EQ(dst2[2], src1[2]);
  }
}

TEST_F(Blop_test, add_op) {
  auto dlop = Dlop::from_pyrope("0xdeadbeef");
  dlop->dump();

  Dlop::from_pyrope("0xbee1_dea2_bee3_dea4_bee5_dea6_bee7_dea8_bee9_deaa_beeb_deec_beed_deaf")->dump();

  auto a = Dlop::from_pyrope("0b111000???");
  auto b = Dlop::from_pyrope("0b?10?10?10");
  a->dump();
  b->dump();

  auto sum1 = a->add_op(b);

  auto sum2 = Dlop::from_pyrope("0b??01?1????");

  sum1->dump();
  sum2->dump();

  // FIXME: EXPECT_EQ(sum1,sum2);
}

#if 0
// TODO(???) implement and/or in dlop.hpp with unknowns

TEST_F(Blop_test, and_or_op) {
  auto a = Dlop::from_pyrope("0b111000???");
  auto b = Dlop::from_pyrope("0b01?01?01?");
  a->dump();
  b->dump();

  auto and1 = a->add_op(b);
  auto and2 = Dlop::from_pyrope("0b01?0000??");

  EXPECT_EQ(and1,and2);

  and1->dump();
  and2->dump();

  auto or1 = a->or_op(b);
  auto or2 = Dlop::from_pyrope("0b11101??1?");

  or1->dump();
  or2->dump();

  EXPECT_EQ(or1,or2);
}
#endif

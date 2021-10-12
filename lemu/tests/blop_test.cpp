

#include <cassert>
#include <functional>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <vector>

#include "fmt/format.h"
#include "gtest/gtest.h"

#include "blop.hpp"
#include "dlop.hpp"
#include "lbench.hpp"
#include "lconst.hpp"
#include "lrand.hpp"
#include "mmap_map.hpp"


class Blop_test : public ::testing::Test {
protected:

public:
  void TearDown() override {
    mmap_lib::str::nuke();
  }

  void SetUp() override {
    mmap_lib::str::setup();

  };
};

TEST_F(Blop_test, addition) {

  int64_t src1[3] = { -1, -1,1};
  int64_t src2[3] = { -1, -1,1};

  int64_t dst1[3];
  Blop::addn(dst1, 3, src1, src2);

  int64_t dst2[3];
  Blop::subn(dst2, 3, dst1, src2);

  fmt::print("dst1:{},{},{}\n", dst1[0], dst1[1], dst1[2]);
  fmt::print("dst2:{},{},{}\n", dst2[0], dst2[1], dst2[2]);
}

TEST_F(Blop_test, shift) {

  int64_t src1[3] = { -1, 2,-1};

  for(int64_t i=0;i<3;++i) {
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

TEST_F(Blop_test, dlop_from_pyrope) {

  auto dlop = Dlop::from_pyrope("0xdeadbeef");
  dlop->dump();

  Dlop::from_pyrope("0xbee1_dea2_bee3_dea4_bee5_dea6_bee7_dea8_bee9_deaa_beeb_deec_beed_deaf")->dump();

  auto a = Dlop::from_pyrope("0b01110011");
  auto b = Dlop::from_pyrope("0b011100?1");
  a->dump();
  b->dump();

  auto c = a->add_op(b);

  c->dump();
}

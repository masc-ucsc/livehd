// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "cell.hpp"

#include "gtest/gtest.h"

TEST(Cell, InitialPinIsScopedToCellType) {
  EXPECT_EQ(Ntype::get_sink_pid(Ntype_op::Memory, "initial"), 11);
  EXPECT_EQ(Ntype::get_sink_name(Ntype_op::Memory, 11), "initial");
  for (auto op : {Ntype_op::Flop, Ntype_op::Latch, Ntype_op::Fflop}) {
    EXPECT_EQ(Ntype::get_sink_pid(op, "initial"), 1);
    EXPECT_EQ(Ntype::get_sink_name(op, 1), "initial");
  }
  EXPECT_EQ(Ntype::get_sink_pid(Ntype_op::Memory, "init"), livehd::Port_invalid);
  EXPECT_EQ(Ntype::get_sink_pid(Ntype_op::And, "initial"), livehd::Port_invalid);
}

TEST(Cell, SameLeadingCharacterUsesCellTypeLookup) {
  EXPECT_EQ(Ntype::get_sink_pid(Ntype_op::Flop, "posclk"), 6);
  EXPECT_EQ(Ntype::get_sink_pid(Ntype_op::Flop, "pipe_min"), 8);
  EXPECT_EQ(Ntype::get_sink_pid(Ntype_op::Flop, "pipe_max"), 9);
  EXPECT_EQ(Ntype::get_sink_pid(Ntype_op::Memory, "posclk"), 6);
  EXPECT_EQ(Ntype::get_sink_pid(Ntype_op::Memory, "pipe_min"), livehd::Port_invalid);
  EXPECT_EQ(Ntype::get_sink_pid(Ntype_op::Latch, "pipe_max"), livehd::Port_invalid);
}

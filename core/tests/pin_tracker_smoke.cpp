#include <string>

#include "gtest/gtest.h"
#include "pin_tracker.hpp"

TEST(PinTrackerSmoke, ArithmeticRightShiftTracksShiftedBitsAndOvershiftSign) {
  Pin_tracker<std::string> tracker{"zero"};

  tracker.add_input("src", 4);
  tracker.add_sra("shift2", "src", 4, *Dlop::create_integer(2));
  tracker.add_sra("overshift", "src", 4, *Dlop::create_integer(6));

  const auto& shifted = tracker.get_pin_vector("shift2");
  ASSERT_EQ(shifted.size(), 2);
  EXPECT_EQ(shifted[0].id, "src");
  EXPECT_EQ(shifted[0].pos, 2);
  EXPECT_EQ(shifted[1].id, "src");
  EXPECT_EQ(shifted[1].pos, 3);

  const auto& overshifted = tracker.get_pin_vector("overshift");
  ASSERT_EQ(overshifted.size(), 1);
  EXPECT_EQ(overshifted[0].id, "src");
  EXPECT_EQ(overshifted[0].pos, 3);
}

TEST(PinTrackerSmoke, ConstantBusStaysOnZeroArrivalThroughShift) {
  Pin_tracker<std::string> tracker{"zero"};

  tracker.add_constant("literal", 282);
  tracker.add_sra("bit281", "literal", 282, *Dlop::create_integer(281));

  const auto& bit = tracker.get_pin_vector("bit281");
  ASSERT_EQ(bit.size(), 1);
  EXPECT_EQ(bit[0].id, "zero");
  EXPECT_EQ(bit[0].pos, 0);
}

TEST(PinTrackerSmoke, EmptyTrackedSourceStaysZeroThroughShifts) {
  Pin_tracker<std::string> tracker{"zero"};

  tracker.add_input("src", 4);
  tracker.add_get_mask("empty", "src", 4, *Dlop::create_integer(16));
  ASSERT_TRUE(tracker.get_pin_vector("empty").empty());

  tracker.add_sra("sra", "empty", 4, *Dlop::create_integer(0));
  tracker.add_shl("shl", "empty", 4, *Dlop::create_integer(2));
  tracker.add_sext("sext", "empty", 4, *Dlop::create_integer(6));

  for (const auto* name : {"sra", "shl", "sext"}) {
    const auto& pins = tracker.get_pin_vector(name);
    ASSERT_FALSE(pins.empty());
    for (const auto& pin : pins) {
      EXPECT_EQ(pin.id, "zero");
      EXPECT_EQ(pin.pos, 0);
    }
  }
}

TEST(PinTrackerSmoke, EmptySetMaskValueWritesZero) {
  Pin_tracker<std::string> tracker{"zero"};

  tracker.add_input("src", 4);
  tracker.add_get_mask("empty", "src", 4, *Dlop::create_integer(16));
  ASSERT_TRUE(tracker.get_pin_vector("empty").empty());

  tracker.add_set_mask("set", "src", 4, *Dlop::create_integer(1), "empty");
  const auto& pins = tracker.get_pin_vector("set");
  ASSERT_EQ(pins.size(), 4);
  EXPECT_EQ(pins[0].id, "zero");
  EXPECT_EQ(pins[0].pos, 0);
  for (int32_t i = 1; i < 4; ++i) {
    EXPECT_EQ(pins[static_cast<size_t>(i)].id, "src");
    EXPECT_EQ(pins[static_cast<size_t>(i)].pos, i);
  }
}

TEST(PinTrackerSmoke, ScalarProducerReplacesProvisionalBus) {
  Pin_tracker<std::string> tracker{"zero"};

  tracker.add_input("cell_y", 7);
  tracker.add_scalar("cell_y", 7);

  const auto& pins = tracker.get_pin_vector("cell_y");
  ASSERT_EQ(pins.size(), 7);
  EXPECT_EQ(pins[0].id, "cell_y");
  EXPECT_EQ(pins[0].pos, 0);
  for (size_t i = 1; i < pins.size(); ++i) {
    EXPECT_EQ(pins[i].id, "zero");
    EXPECT_EQ(pins[i].pos, 0);
  }
}

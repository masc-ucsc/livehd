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

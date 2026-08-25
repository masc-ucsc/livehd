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
  EXPECT_EQ(shifted[0].id(), "src");
  EXPECT_EQ(shifted[0].pos, 2);
  EXPECT_EQ(shifted[1].id(), "src");
  EXPECT_EQ(shifted[1].pos, 3);

  const auto& overshifted = tracker.get_pin_vector("overshift");
  ASSERT_EQ(overshifted.size(), 1);
  EXPECT_EQ(overshifted[0].id(), "src");
  EXPECT_EQ(overshifted[0].pos, 3);
}

TEST(PinTrackerSmoke, ConstantBusStaysOnZeroArrivalThroughShift) {
  Pin_tracker<std::string> tracker{"zero"};

  tracker.add_constant("literal", 282);
  tracker.add_sra("bit281", "literal", 282, *Dlop::create_integer(281));

  const auto& bit = tracker.get_pin_vector("bit281");
  ASSERT_EQ(bit.size(), 1);
  EXPECT_EQ(bit[0].id(), "zero");
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
      EXPECT_EQ(pin.id(), "zero");
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
  EXPECT_EQ(pins[0].id(), "zero");
  EXPECT_EQ(pins[0].pos, 0);
  for (int32_t i = 1; i < 4; ++i) {
    EXPECT_EQ(pins[static_cast<size_t>(i)].id(), "src");
    EXPECT_EQ(pins[static_cast<size_t>(i)].pos, i);
  }
}

TEST(PinTrackerSmoke, ScalarProducerReplacesProvisionalBus) {
  Pin_tracker<std::string> tracker{"zero"};

  tracker.add_input("cell_y", 7);
  tracker.add_scalar("cell_y", 7);

  const auto& pins = tracker.get_pin_vector("cell_y");
  ASSERT_EQ(pins.size(), 7);
  EXPECT_EQ(pins[0].id(), "cell_y");
  EXPECT_EQ(pins[0].pos, 0);
  for (size_t i = 1; i < pins.size(); ++i) {
    EXPECT_EQ(pins[i].id(), "zero");
    EXPECT_EQ(pins[i].pos, 0);
  }
}

// The ordering invariant behind add_scalar_if_absent: a consumer that reaches a
// Liberty-cell output BEFORE the forward walk reaches the cell must see the
// same wiring the producer would have written. Without the seed, the tracker's
// add_input fallback invents a bus and the consumer COPIES it -- a copy the
// producer's later add_scalar can no longer undo.
TEST(PinTrackerSmoke, CellOutputSeededByConsumerMatchesProducerFirstOrder) {
  // Producer-first: the cell stamps its scalar shape, then the consumer reads bit 2.
  Pin_tracker<std::string> producer_first{"zero"};
  producer_first.add_scalar("cell", 4);  // stale 4-bit stamp on a 1-bit Liberty output
  producer_first.add_get_mask("read2", "cell", 4, *Dlop::create_integer(1 << 2));
  const auto& want = producer_first.get_pin_vector("read2");
  ASSERT_EQ(want.size(), 1u);
  EXPECT_EQ(want[0].id(), "zero");  // bit 2 of a scalar cell output is known zero
  EXPECT_EQ(want[0].pos, 0);

  // Consumer-first WITH the seed: identical result, and the producer's later
  // stamp is an idempotent rewrite.
  Pin_tracker<std::string> consumer_first{"zero"};
  EXPECT_TRUE(consumer_first.add_scalar_if_absent("cell", 4));
  consumer_first.add_get_mask("read2", "cell", 4, *Dlop::create_integer(1 << 2));
  EXPECT_FALSE(consumer_first.add_scalar_if_absent("cell", 4));  // producer arrives: no rewrite
  const auto& got = consumer_first.get_pin_vector("read2");
  ASSERT_EQ(got.size(), 1u);
  EXPECT_EQ(got[0].id(), want[0].id());
  EXPECT_EQ(got[0].pos, want[0].pos);

  // The defect being fixed: with NO seed the consumer keeps "cell" bit 2, which
  // becomes the OpenTimer net "cell.2" -- a net a scalar cell output never has.
  Pin_tracker<std::string> unseeded{"zero"};
  unseeded.add_get_mask("read2", "cell", 4, *Dlop::create_integer(1 << 2));
  const auto& bad = unseeded.get_pin_vector("read2");
  ASSERT_EQ(bad.size(), 1u);
  EXPECT_EQ(bad[0].id(), "cell");
  EXPECT_EQ(bad[0].pos, 2);
  unseeded.add_scalar("cell", 4);                         // the after-the-fact repair...
  EXPECT_EQ(unseeded.get_pin_vector("read2")[0].pos, 2);  // ...cannot undo the copy already taken
}

// add_or DROPS an untracked operand, so an unseeded cell output loses its whole
// timing arc and the Or result collapses onto the zero net.
TEST(PinTrackerSmoke, UnseededCellOperandDropsItsOrArc) {
  Pin_tracker<std::string> unseeded{"zero"};
  unseeded.add_or("y", "cell");
  EXPECT_TRUE(unseeded.get_pin_vector("y").empty());

  Pin_tracker<std::string> seeded{"zero"};
  seeded.add_scalar_if_absent("cell", 1);
  seeded.add_or("y", "cell");
  const auto& pv = seeded.get_pin_vector("y");
  ASSERT_EQ(pv.size(), 1u);
  EXPECT_EQ(pv[0].id(), "cell");
  EXPECT_EQ(pv[0].pos, 0);
}

// The contrast Pin_tracker::add_scalar's precondition now states: a genuinely
// wide opaque boundary keeps every bit's identity, while a Liberty cell output
// retires every bit above 0 to the zero-arrival net. A future edit must not
// quietly model one as the other.
TEST(PinTrackerSmoke, OpaqueBoundaryKeepsEveryBitIdentityUnlikeScalar) {
  Pin_tracker<std::string> tracker{"zero"};
  tracker.add_input("bbox_y", 8);
  tracker.add_opaque("bbox_y", 8);
  const auto& op = tracker.get_pin_vector("bbox_y");
  ASSERT_EQ(op.size(), 8u);
  for (int32_t i = 0; i < 8; ++i) {
    EXPECT_EQ(op[static_cast<size_t>(i)].id(), "bbox_y");
    EXPECT_EQ(op[static_cast<size_t>(i)].pos, i);
  }

  tracker.add_scalar("cell_y", 8);
  const auto& sc = tracker.get_pin_vector("cell_y");
  ASSERT_EQ(sc.size(), 8u);
  EXPECT_EQ(sc[0].id(), "cell_y");
  for (size_t i = 1; i < sc.size(); ++i) {
    EXPECT_EQ(sc[i].id(), "zero");
  }
}

// The crux of the ambiguous-Or degradation: has_ambiguous can be true while
// pv[0] is a perfectly good rename, which is exactly why pass.opentimer's
// pv[0]-only driver test used to silently DELETE the other fan-in cone. The
// opaque boundary replaces the whole vector, leaving no ambiguous residue.
TEST(PinTrackerSmoke, OverlappingOrMarksOnlyTheOverlappingBitAndOpaqueClearsIt) {
  Pin_tracker<std::string> tracker{"zero"};

  tracker.add_input("a", 4);
  tracker.add_input("b", 4);
  tracker.add_and("a_m", "a", *Dlop::create_integer(0b0011));  // live bits 0,1
  tracker.add_and("b_m", "b", *Dlop::create_integer(0b0110));  // live bits 1,2

  tracker.add_or("o", "a_m");
  tracker.add_or("o", "b_m");

  const auto& o = tracker.get_pin_vector("o");
  ASSERT_GE(o.size(), 3u);
  EXPECT_EQ(o[0].id(), "a");
  EXPECT_EQ(o[0].pos, 0);
  EXPECT_LT(o[1].pos, 0);  // only the OVERLAPPING bit is ambiguous
  EXPECT_EQ(o[2].id(), "b");
  EXPECT_EQ(o[2].pos, 2);
  EXPECT_TRUE(tracker.has_ambiguous("o"));
  EXPECT_GE(o[0].pos, 0);  // ambiguous WITH pv[0].pos >= 0

  tracker.add_opaque("o", 3);
  const auto& ob = tracker.get_pin_vector("o");
  ASSERT_EQ(ob.size(), 3u);
  for (int32_t i = 0; i < 3; ++i) {
    EXPECT_EQ(ob[static_cast<size_t>(i)].id(), "o");
    EXPECT_EQ(ob[static_cast<size_t>(i)].pos, i);
  }
  EXPECT_FALSE(tracker.has_ambiguous("o"));
}

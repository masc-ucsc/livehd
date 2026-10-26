// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "physical_rc.hpp"

#include <cmath>

#include "gtest/gtest.h"

TEST(PhysicalRc, ExactCheckpointReaderRoundTripsAllStoredDoubles) {
  livehd::physical::Parasitics original;
  original.identity = "checkpoint";
  original.nets.push_back({
      "d",
      {{"d", 0}, {"s", 0}},
      {{0, 1, 0}}
  });
  for (int exponent = -40; exponent <= 40; ++exponent) {
    const double value                       = std::ldexp(std::nextafter(1., 2.), exponent);
    original.nets[0].nodes[0].capacitance_ff = value;
    original.nets[0].nodes[1].capacitance_ff = value / 3;
    original.nets[0].edges[0].resistance_ohm = value * 7;
    original.nets[0].wire_length_um          = value / 7;
    const auto restored = livehd::physical::deserialize_parasitics_exact(livehd::physical::serialize_parasitics(original));
    EXPECT_EQ(restored.identity, original.identity);
    EXPECT_EQ(restored.nets, original.nets);
  }
}

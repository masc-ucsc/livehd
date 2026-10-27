// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "physical_rc.hpp"

#include <cmath>
#include <stdexcept>

#include "gtest/gtest.h"

TEST(PhysicalRc, PersistsGeometryAndEscapedNamesWithoutChangingSchema) {
  using namespace livehd::physical;
  Parasitics original;
  original.identity    = "quote\"\\\n";
  original.approximate = false;
  original.timing_cuts = {"reset:A"};
  original.cells       = {
      {"u", -1, 2, 3, "INV", 4, 5}
  };
  original.terminals = {
      {"a[0]", 6, 7}
  };
  original.nets = {
      {"u:Y", {{"u:Y", .5}, {"a[0]", .25}}, {{0, 1, 2}}, 3}
  };
  const auto encoded = serialize_parasitics(original);
  EXPECT_EQ(
      encoded,
      R"({"version":3,"identity":"quote\"\\\n","approximate":false,"timing_cuts":["reset:A"],"cells":[["u",-1,2,3,"INV",4,5]],"terminals":[["a[0]",6,7]],"nets":[{"driver":"u:Y","wire_length_um":3.0,"nodes":[["u:Y",0.5],["a[0]",0.25]],"edges":[[0,1,2.0]]}]})");
  const auto restored = deserialize_parasitics_exact(encoded);
  EXPECT_EQ(restored.identity, original.identity);
  EXPECT_EQ(restored.approximate, original.approximate);
  EXPECT_EQ(restored.timing_cuts, original.timing_cuts);
  EXPECT_EQ(restored.cells, original.cells);
  EXPECT_EQ(restored.terminals, original.terminals);
  EXPECT_EQ(restored.nets, original.nets);
}

TEST(PhysicalRc, ExactCheckpointReaderRoundTripsAllStoredDoubles) {
  livehd::physical::Parasitics original;
  original.identity    = "checkpoint";
  original.timing_cuts = {"d"};
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
    EXPECT_EQ(restored.timing_cuts, original.timing_cuts);
  }
}
TEST(PhysicalRc, ReadsOlderSnapshotsWithoutCutsAndRejectsMalformedCuts) {
  const std::string old = R"({"version":2,"identity":"old","approximate":true,"cells":[],"terminals":[],"nets":[]})";
  EXPECT_TRUE(livehd::physical::deserialize_parasitics(old).timing_cuts.empty());
  const std::string missing = R"({"version":3,"identity":"bad","approximate":true,"cells":[],"terminals":[],"nets":[]})";
  EXPECT_THROW(livehd::physical::deserialize_parasitics(missing), std::runtime_error);
  const std::string malformed
      = R"({"version":3,"identity":"bad","approximate":true,"cells":[],"terminals":[],"nets":[],"timing_cuts":[7]})";
  EXPECT_THROW(livehd::physical::deserialize_parasitics(malformed), std::runtime_error);
}

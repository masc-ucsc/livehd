// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "domino_liberty.hpp"

#include <stdexcept>

#include "domino_timing_fixture.hpp"
#include "gtest/gtest.h"

namespace livehd::domplace::timing {
namespace {
namespace model = liberty::timing;
const model::Library& base() {
  static const auto library = model::parse(test::kLiberty);
  return library;
}
Domino_timing_view view(const test::Sidecar& sidecar, const std::string& name) {
  std::string why;
  auto        read = read_domino_timing(sidecar.write(name), &why);
  EXPECT_TRUE(read) << why;
  return read.value_or(Domino_timing_view{});
}
double scalar_of(const std::optional<model::Table>& table) {
  EXPECT_TRUE(table);
  return table ? table->values.at(0) : -1;
}
}  // namespace

// The Liberty text parses back to the cells the stage timer characterizes, with
// the rail setup moved from the T/2 capture to the next clock edge.
TEST(DominoLiberty, RoundTripsTheCharacterizedCells) {
  test::Sidecar sidecar;
  const auto    v    = view(sidecar, "liberty_round_trip");
  const auto    text = domino_liberty(v, base(), 100);
  const auto    lib  = model::parse(text);
  ASSERT_EQ(lib.cells.size(), 2U);
  for (const auto& name : {test::kDomino, test::kQn}) {
    ASSERT_TRUE(lib.cells.contains(name)) << name;
    const auto& cell = lib.cells.at(name);
    EXPECT_TRUE(cell.sequential);
    ASSERT_TRUE(cell.pins.contains("CLK"));
    EXPECT_TRUE(cell.pins.at("CLK").clock);
    EXPECT_EQ(cell.pins.at("CLK").capacitance_ff[0], 4);
    for (const auto* rail : {"R0", "R1"}) {
      const auto& pin = cell.pins.at(rail);
      EXPECT_EQ(pin.capacitance_ff[0], 2);
      ASSERT_EQ(pin.arcs.size(), 1U);
      EXPECT_EQ(pin.arcs[0].type, "setup_rising");
      EXPECT_EQ(pin.arcs[0].related_pin, "CLK");
      // setup_eval 3 + uncertainty_first 1 + (1 - 1/2) * 100 ps.
      EXPECT_DOUBLE_EQ(scalar_of(pin.arcs[0].constraint[0]), 54);
      EXPECT_DOUBLE_EQ(scalar_of(pin.arcs[0].constraint[1]), 54);
    }
  }
  // Q copies the register's clock-to-Q arcs: FF 10/20 ps, FFQN's QN 12/22 ps.
  const auto& q = lib.cells.at(test::kDomino).pins.at("Q");
  ASSERT_EQ(q.arcs.size(), 1U);
  EXPECT_EQ(q.arcs[0].type, "rising_edge");
  EXPECT_DOUBLE_EQ(scalar_of(q.arcs[0].delay[0]), 10);
  EXPECT_DOUBLE_EQ(scalar_of(q.arcs[0].delay[1]), 20);
  EXPECT_DOUBLE_EQ(scalar_of(lib.cells.at(test::kQn).pins.at("Q").arcs.at(0).delay[0]), 12);
}

TEST(DominoLiberty, ShiftFollowsThePeriod) {
  test::Sidecar sidecar;
  const auto    v   = view(sidecar, "liberty_period");
  const auto    lib = model::parse(domino_liberty(v, base(), 700));
  EXPECT_DOUBLE_EQ(scalar_of(lib.cells.at(test::kDomino).pins.at("R0").arcs.at(0).constraint[0]), 3 + 1 + 350);
}

TEST(DominoLiberty, RefusesWhatItCannotTimeInsteadOfWritingAZero) {
  test::Sidecar sidecar;
  const auto    v = view(sidecar, "liberty_refuse");
  EXPECT_THROW(domino_liberty(v, base(), 0), std::runtime_error);
  test::Sidecar unmeasured;
  unmeasured.stage["setup_eval_ps"] = std::nullopt;
  const auto u                      = view(unmeasured, "liberty_unmeasured");
  EXPECT_THROW(domino_liberty(u, base(), 100), std::runtime_error);
}
}  // namespace livehd::domplace::timing

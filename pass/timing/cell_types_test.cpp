// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "cell_types.hpp"

#include <cmath>
#include <format>
#include <random>
#include <string>

#include "gtest/gtest.h"
#include "test_library.hpp"

namespace fast = livehd::timing::fast;
namespace lt   = livehd::liberty::timing;
using livehd::timing::fast::test::library_text;

TEST(CellTypes, LookupMatchesLibraryTables) {
  const auto         slow = lt::parse(library_text(2.0)), fastc = lt::parse(library_text(1.0));
  const lt::Library* libs[]  = {&fastc, &slow};
  const std::string  names[] = {"fast", "slow"};
  const auto         types   = fast::Cell_types::build(libs, names);
  EXPECT_EQ(types.corners(), 2u);
  // Only the falling-edge flip-flop is left out: its launch and its setup arc.
  EXPECT_EQ(types.ignored_arcs(), 2u);
  for (const auto& line : types.diagnostics()) {
    EXPECT_NE(line.find("NEGFF"), std::string::npos) << line;
  }
  // Two port types, then the library's cells in name order.
  ASSERT_EQ(types.types(), 2u + fastc.cells.size());
  EXPECT_EQ(types.type(fast::Cell_types::input_port_type).pin_count, 1);
  EXPECT_EQ(types.type(fast::Cell_types::output_port_type).pin_count, 1);

  std::mt19937                           random(7);
  std::uniform_real_distribution<double> value(0, 160);  // beyond the 0..100 axes: extrapolation too
  size_t                                 compared = 0;
  for (size_t k = 0; k < 2; ++k) {
    const auto& library = k == 0 ? fastc : slow;
    for (const auto& [name, cell] : library.cells) {
      if (name == "NEGFF") {
        continue;  // its arcs are not timed
      }
      const auto type = types.find(name);
      ASSERT_NE(type, fast::no_index) << name;
      for (const auto& [pin_name, pin] : cell.pins) {
        const auto local = types.find_pin(type, pin_name);
        ASSERT_NE(local, fast::no_index) << name << "/" << pin_name;
        EXPECT_NEAR(types.pin_cap(type, local, k, 0), pin.capacitance_ff[0].value_or(0), 1e-5) << name << "/" << pin_name;
        EXPECT_NEAR(types.pin_cap(type, local, k, 1), pin.capacitance_ff[1].value_or(0), 1e-5) << name << "/" << pin_name;
        const auto arcs = types.arcs_into(type, local);
        EXPECT_EQ(arcs.size(), pin.arcs.size()) << name << "/" << pin_name;
        for (size_t a = 0; a < arcs.size() && a < pin.arcs.size(); ++a) {
          for (unsigned group = 0; group < 3; ++group) {
            for (unsigned t = 0; t < 2; ++t) {
              const auto& reference = group == 0   ? pin.arcs[a].delay[t]
                                      : group == 1 ? pin.arcs[a].slew[t]
                                                   : pin.arcs[a].constraint[t];
              const auto  id        = types.table(arcs[a], k, group, t);
              ASSERT_EQ(id == fast::no_index, !reference.has_value()) << name << "/" << pin_name;
              if (!reference) {
                continue;
              }
              for (int i = 0; i < 40; ++i) {
                const double      s = value(random), l = value(random), r = value(random), c = value(random);
                const lt::Query   query{.input_slew_ps = s, .output_load_ff = l, .related_slew_ps = r, .constrained_slew_ps = c};
                const fast::Query mine{static_cast<float>(s), static_cast<float>(l), static_cast<float>(r), static_cast<float>(c)};
                const double      expected = reference->lookup(query);
                EXPECT_NEAR(types.lookup(id, mine), expected, 1e-3 + 1e-5 * std::abs(expected)) << name << "/" << pin_name;
                ++compared;
              }
            }
          }
        }
      }
    }
  }
  EXPECT_GT(compared, 1000u);
  // The axes are shared: far fewer axes than tables.
  EXPECT_LT(types.axis_count(), 10u);
  EXPECT_GT(types.table_count(), types.axis_count());
  EXPECT_GT(types.bytes(), 0u);
}

TEST(CellTypes, ArcKindsSensesAndConditions) {
  const auto         corner  = lt::parse(library_text(1.0));
  const lt::Library* libs[]  = {&corner};
  const std::string  names[] = {"typ"};
  const auto         types   = fast::Cell_types::build(libs, names);
  const auto         inv     = types.find("INV");
  ASSERT_NE(inv, fast::no_index);
  const auto y = types.find_pin(inv, "Y");
  ASSERT_EQ(types.arcs_into(inv, y).size(), 1u);
  EXPECT_EQ(types.arcs_into(inv, y)[0].kind, fast::Arc_kind::combinational);
  EXPECT_EQ(types.arcs_into(inv, y)[0].sense, fast::Sense::negative);
  EXPECT_EQ(types.arcs_into(inv, y)[0].from, types.find_pin(inv, "A"));
  EXPECT_EQ(types.pin(inv, y).direction, fast::Direction::output);
  EXPECT_EQ(types.max_capacitance(inv, y, 0), 5.f);
  EXPECT_TRUE(std::isinf(types.max_capacitance(inv, types.find_pin(inv, "A"), 0)));

  const auto xorc = types.find("XOR2");
  EXPECT_EQ(types.arcs_into(xorc, types.find_pin(xorc, "Y")).size(), 2u);
  EXPECT_EQ(types.arcs_into(xorc, types.find_pin(xorc, "Y"))[0].sense, fast::Sense::non_unate);

  const auto ff = types.find("FF");
  ASSERT_NE(ff, fast::no_index);
  EXPECT_TRUE(types.type(ff).sequential);
  EXPECT_TRUE(types.pin(ff, types.find_pin(ff, "CLK")).clock);
  EXPECT_EQ(types.arcs_into(ff, types.find_pin(ff, "Q"))[0].kind, fast::Arc_kind::launch_rise);
  const auto checks = types.arcs_into(ff, types.find_pin(ff, "D"));
  ASSERT_EQ(checks.size(), 2u);
  EXPECT_EQ(checks[0].kind == fast::Arc_kind::setup_rise || checks[1].kind == fast::Arc_kind::setup_rise, true);
  EXPECT_EQ(checks[0].kind == fast::Arc_kind::hold_rise || checks[1].kind == fast::Arc_kind::hold_rise, true);
  EXPECT_EQ(checks[0].from, types.find_pin(ff, "CLK"));
  // A falling-edge element and a pulse-width arc are not timed, and the first says so.
  const auto neg = types.find("NEGFF");
  ASSERT_NE(neg, fast::no_index);
  EXPECT_EQ(types.arcs_into(neg, types.find_pin(neg, "Q")).size(), 0u);
  EXPECT_GT(types.ignored_arcs(), 0u);
  bool said = false;
  for (const auto& line : types.diagnostics()) {
    said = said || line.find("falling-edge") != std::string::npos;
  }
  EXPECT_TRUE(said);
  // The state-dependent arc keeps its condition.
  const auto mux = types.find("MUXC");
  ASSERT_NE(mux, fast::no_index);
  const auto arcs = types.arcs_into(mux, types.find_pin(mux, "Y"));
  ASSERT_EQ(arcs.size(), 1u);
  ASSERT_NE(arcs[0].when, fast::no_index);
  EXPECT_EQ(types.condition(arcs[0].when), "S");
}

TEST(CellTypes, ACellOrArcMissingFromACornerUsesTheReference) {
  auto               text_fast = library_text(1.0);
  auto               text_slow = library_text(2.0, /*without=*/"XOR2");
  const auto         fast_lib = lt::parse(text_fast), slow_lib = lt::parse(text_slow);
  const lt::Library* libs[]  = {&fast_lib, &slow_lib};
  const std::string  names[] = {"fast", "slow"};
  const auto         types   = fast::Cell_types::build(libs, names);
  ASSERT_FALSE(types.diagnostics().empty());
  bool said = false;
  for (const auto& line : types.diagnostics()) {
    said = said || (line.find("XOR2") != std::string::npos && line.find("slow") != std::string::npos);
  }
  EXPECT_TRUE(said);
  const auto xorc = types.find("XOR2");
  ASSERT_NE(xorc, fast::no_index);
  const auto&       arc = types.arcs_into(xorc, types.find_pin(xorc, "Y"))[0];
  // The slow corner answers with the fast corner's tables.
  const fast::Query query{10, 10, 0, 0};
  EXPECT_FLOAT_EQ(types.lookup(types.table(arc, 1, 0, 0), query), types.lookup(types.table(arc, 0, 0, 0), query));
  EXPECT_THROW(fast::Cell_types::build({}, {}), std::invalid_argument);
}

// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "terminal_legalize.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <numeric>
#include <set>
#include <stdexcept>

#include "gtest/gtest.h"
#include "satlegalize.hpp"

namespace livehd::placement {
namespace {
struct Fixture {
  physical::Technology      tech;
  physical::Floorplan       fp;
  timing::Feedback          timing;
  Terminal_legalize_options options;
  Fixture(int width = 100, int height = 10) {
    tech.dbu = fp.dbu  = 100;
    tech.sites["site"] = {1, 1, true};
    auto& m            = tech.macros["cell"];
    m.width = m.height = 1;
    m.klass            = "CORE";
    m.site             = "site";
    m.symmetry_x       = true;
    m.pins["Y"]        = {"OUTPUT", "SIGNAL", {{"M1", {.4, .4, .6, .6}}}};
    m.pins["A"]        = {"INPUT", "SIGNAL", {{"M1", {.4, .4, .6, .6}}}};
    auto& l            = tech.layers["M1"];
    l.width            = .1;
    l.pitch_x = l.pitch_y = 1;
    l.offset_x = l.offset_y = .5;
    l.has_offset            = true;
    l.direction             = "HORIZONTAL";
    fp.die                  = {0, 0, double(width * 100), double(height * 100)};
    for (int y = 0; y < height; ++y) {
      fp.rows.push_back({"r" + std::to_string(y), "site", 0, y * 100, width, 100, physical::Orientation::N});
    }
    options.bins_x = 5;
    options.bins_y = 2;
  }
  size_t cell(int x, int y, std::string master = "cell") {
    const auto& m = tech.macros.at(master);
    Ann_place   p(x * 100, y * 100, std::llround(m.width * 100), std::llround(m.height * 100));
    p.status     = physical::Status::PLACED;
    const auto i = fp.components.size();
    fp.components.push_back({"c" + std::to_string(i), master, p});
    return i;
  }
  void net(size_t a, size_t b, double slack) {
    fp.nets.push_back({"n" + std::to_string(fp.nets.size()), {{fp.components[a].name, "Y"}, {fp.components[b].name, "A"}}});
    timing.pins[fp.components[b].name + ":A"].slack = slack;
    timing.pins[fp.components[a].name + ":Y"].slack = slack;
  }
  void check(const Terminal_legalize_report& r) {
    std::vector<unsigned> pads(fp.components.size(), options.padding_sites);
    for (const auto& c : r.cells) {
      pads[c.index] = c.padding_sites;
      EXPECT_EQ(c.padding_sites, c.critical ? options.critical_padding_sites : (c.padding_sites ? options.padding_sites : 0));
      if (!options.padding_max_extra_rows && !c.critical) {
        EXPECT_EQ(c.padding_sites, options.padding_sites);
      }
    }
    for (const auto& b : r.bins) {
      EXPECT_LE(b.after, options.max_density * b.capacity + 1e-6);
      double area = 0;
      for (const auto& c : fp.components) {
        if (tech.macros.at(c.macro).klass.starts_with("CORE")) {
          area += std::max(0., std::min(b.bounds.xh, double(c.place.x + c.place.width)) - std::max(b.bounds.xl, double(c.place.x)))
                  * std::max(0.,
                             std::min(b.bounds.yh, double(c.place.y + c.place.height)) - std::max(b.bounds.yl, double(c.place.y)));
        }
      }
      EXPECT_NEAR(area, b.after, 1e-6);
    }
    for (size_t i = 0; i < fp.components.size(); ++i) {
      const auto& a = fp.components[i].place;
      EXPECT_EQ(a.x % 100, 0);
      EXPECT_EQ(a.y % 100, 0);
      for (size_t j = 0; j < i; ++j) {
        const auto& b = fp.components[j].place;
        if (a.y < b.y + b.height && b.y < a.y + a.height) {
          EXPECT_TRUE(a.x + a.width <= b.x || b.x + b.width <= a.x);
          if (tech.macros.at(fp.components[i].macro).klass.starts_with("CORE")
              && tech.macros.at(fp.components[j].macro).klass.starts_with("CORE")) {
            EXPECT_TRUE(a.x + a.width + 100 * (pads[i] + pads[j]) <= b.x || b.x + b.width + 100 * (pads[i] + pads[j]) <= a.x);
          }
        }
      }
    }
  }
};
}  // namespace

TEST(TerminalLegalize, PaddedCellsUseWholeRowWithoutFragmentingRemainingCapacity) {
  Fixture f(9, 1);
  f.options.bins_x = f.options.bins_y = 1;
  // Three one-site cells plus two padding sites each fill the row exactly.
  // Starting in the middle must not leave two unusable fragments at its ends.
  for (int i = 0; i < 3; ++i) {
    f.cell(3, 0);
  }
  Terminal_legalize_report report;
  ASSERT_NO_THROW(report = terminal_legalize(f.tech, f.fp, f.timing, f.options));
  f.check(report);
  std::set<int64_t> positions;
  for (const auto& c : f.fp.components) {
    positions.insert(c.place.x);
  }
  EXPECT_EQ(positions, (std::set<int64_t>{100, 400, 700}));
}

TEST(TerminalLegalize, GapPackingPreservesPreviouslyLockedCriticalCells) {
  Fixture f(11, 1);
  f.options.bins_x = f.options.bins_y = 1;
  f.options.critical_fraction         = 1;
  f.options.critical_pair_limit       = 0;
  const auto a = f.cell(9, 0), b = f.cell(10, 0);
  f.net(a, b, -1);
  const auto locked_a = f.fp.components[a].place, locked_b = f.fp.components[b].place;
  for (int i = 0; i < 3; ++i) {
    f.cell(3, 0);
  }
  Terminal_legalize_report report;
  ASSERT_NO_THROW(report = terminal_legalize(f.tech, f.fp, f.timing, f.options));
  f.check(report);
  EXPECT_EQ(f.fp.components[a].place, locked_a);
  EXPECT_EQ(f.fp.components[b].place, locked_b);
  EXPECT_EQ(report.critical_cells, 2);
}

TEST(TerminalLegalize, FineGridOnSmallCoreKeepsCellsIndivisibleAndHardCap) {
  Fixture f(10, 2);
  f.options.bins_x = f.options.bins_y = 64;
  f.tech.macros["cell"].width         = 4;
  f.cell(0, 0);
  f.cell(0, 0);
  fit_terminal_legalize_grid(f.tech, f.fp, f.options);
  const auto report = terminal_legalize(f.tech, f.fp, f.timing, f.options);
  f.check(report);
  EXPECT_EQ(report.bins_x, 2);
  EXPECT_EQ(report.bins_y, 2);
  EXPECT_EQ(report.bins.size(), 4);
}
TEST(TerminalLegalize, DenseRankingPreservesCriticalCellsAndFinalGeometry) {
  Fixture named(11, 1);
  named.options.bins_x = named.options.bins_y = 1;
  named.options.critical_fraction             = 1;
  named.options.critical_pair_limit           = 0;
  const auto a = named.cell(9, 0), b = named.cell(10, 0);
  named.net(a, b, -1);
  for (int i = 0; i < 3; ++i) {
    named.cell(3, 0);
  }
  auto dense     = named;
  auto layout    = std::make_shared<timing::Feedback::Ranking::Layout>();
  layout->labels = {"c0:Y", "c1:A"};
  dense.timing.pins.clear();
  dense.timing.ranking_only = true;
  auto& rank                = dense.timing.ranking.emplace();
  rank.layout               = layout;
  rank.pins                 = {
      {-1, false},
      {-1, false}
  };
  const auto expected = terminal_legalize(named.tech, named.fp, named.timing, named.options);
  const auto actual   = terminal_legalize(dense.tech, dense.fp, dense.timing, dense.options);
  EXPECT_EQ(expected.critical_cells, 2U);
  EXPECT_EQ(actual.critical_cells, expected.critical_cells);
  EXPECT_EQ(dense.fp.components, named.fp.components);
  EXPECT_EQ(dense.fp.rows, named.fp.rows);
  dense.check(actual);
}

TEST(TerminalLegalize, ControlEdgesCannotConsumeCriticalBudgetOrContaminateCellSlack) {
  Fixture    f;
  const auto a = f.cell(3, 0), b = f.cell(6, 0), reset_buffer = f.cell(10, 0), reset_leaf = f.cell(13, 0);
  f.net(a, b, -1);
  f.net(reset_buffer, reset_leaf, -1000);
  f.options.prects.controls["cell"].insert("R");
  f.tech.macros["cell"].pins["R"]         = f.tech.macros["cell"].pins["A"];
  f.fp.nets.back().connections.back().pin = "R";
  f.timing.pins["c3:R"].slack             = -1000;
  // Buffered reset also touches a data-critical cell, on a separate pin.
  f.fp.nets.back().connections.push_back({"c0", "R"});
  f.timing.pins["c0:R"].slack     = -1000;
  f.options.critical_fraction     = 1;
  f.options.remainder_wire_weight = 4;
  const auto report               = terminal_legalize(f.tech, f.fp, f.timing, f.options);
  f.check(report);
  EXPECT_EQ(report.ignored_nets, 1);
  EXPECT_EQ(report.timed_nets, 1);
  EXPECT_EQ(report.critical_cells, 2);
  EXPECT_EQ(report.cells[a].slack_ps, -1000);  // feedback uses ns by default
  EXPECT_FALSE(report.cells[reset_buffer].timed);
  EXPECT_FALSE(report.cells[reset_leaf].critical);
  EXPECT_EQ(report.cells.size(), 4);  // control cells still occupy legal sites
}

TEST(TerminalLegalize, ClusterSpillsBeyondFourBinsWithMandatoryDensityAndSpacing) {
  Fixture f(100, 1);
  f.options.bins_x = 10;
  f.options.bins_y = 1;
  for (int i = 0; i < 25; ++i) {
    f.cell(1, 0);
  }
  const auto report = terminal_legalize(f.tech, f.fp, f.timing, f.options);
  f.check(report);
  EXPECT_GE(report.max_bin_distance, 6);
  EXPECT_EQ(report.cells.size(), 25);
}

TEST(TerminalLegalize, OptionalPaddingBudgetPreservesLocksAndRetainsAffordableWhitespace) {
  Fixture f(50, 1);
  f.options.bins_y              = 1;
  f.options.critical_fraction   = 1;
  f.options.critical_pair_limit = f.options.local_pair_limit = 0;
  const auto a = f.cell(9, 0), b = f.cell(11, 0), crowded = f.cell(10, 0), sparse = f.cell(30, 0);
  f.net(a, b, -10);
  auto       mandatory = f;
  const auto strict    = terminal_legalize(mandatory.tech, mandatory.fp, mandatory.timing, mandatory.options);
  mandatory.check(strict);
  EXPECT_EQ(strict.relaxed_padding_cells, 0);
  EXPECT_NE(mandatory.fp.components[crowded].place.x, 1000);
  f.options.padding_max_extra_rows = 1;
  const auto result                = terminal_legalize(f.tech, f.fp, f.timing, f.options);
  f.check(result);
  EXPECT_EQ(result.relaxed_padding_cells, 1);
  EXPECT_EQ(f.fp.components[a].place.x, 900);
  EXPECT_EQ(f.fp.components[b].place.x, 1100);
  EXPECT_EQ(f.fp.components[crowded].place.x, 1000);
  EXPECT_EQ(f.fp.components[sparse].place.x, 3000);
  EXPECT_EQ(result.cells[crowded].padding_sites, 0);
  EXPECT_EQ(result.cells[sparse].padding_sites, 1);
}

TEST(TerminalLegalize, OptionalPaddingBudgetStillEnforcesHardDensity) {
  Fixture f(100, 1);
  f.options.padding_sites          = 2;
  f.options.bins_x                 = 10;
  f.options.bins_y                 = 1;
  f.options.padding_max_extra_rows = 1;
  for (int i = 0; i < 25; ++i) {
    f.cell(1, 0);
  }
  const auto report = terminal_legalize(f.tech, f.fp, f.timing, f.options);
  f.check(report);
  EXPECT_GT(report.relaxed_padding_cells, 0);
  EXPECT_GT(report.crossed_cells, 0);
  EXPECT_LE(report.bins.front().after, .8 * report.bins.front().capacity);
}

TEST(TerminalLegalize, TwoRowCellsAreFixedObstaclesNeverMovableCells) {
  // The v1 contract of the domino anchor prepass (multirow.hpp): terminal
  // legalization packs only single-row cells. A FIXED two-row CORE cell is an
  // obstacle in both of its rows; a movable one has no candidate row.
  Fixture f(12, 2);
  auto    tall                         = f.tech.macros.at("cell");
  tall.width                           = 2;
  tall.height                          = 2;
  f.tech.macros["tall"]                = tall;
  f.options.bins_x                     = 1;
  f.options.bins_y                     = 1;
  f.options.local_pair_limit           = 0;
  auto       movable                   = f;
  const auto anchor                    = f.cell(5, 0, "tall");
  f.fp.components[anchor].place.status = physical::Status::FIXED;
  for (int i = 0; i < 4; ++i) {
    f.cell(5, i % 2);  // seeded on top of the anchor, in both of its rows
  }
  const auto               before = f.fp.components[anchor].place;
  Terminal_legalize_report report;
  ASSERT_NO_THROW(report = terminal_legalize(f.tech, f.fp, f.timing, f.options));
  f.check(report);
  EXPECT_EQ(f.fp.components[anchor].place, before);
  movable.cell(5, 0, "tall");
  movable.cell(1, 0);
  EXPECT_THROW(terminal_legalize(movable.tech, movable.fp, movable.timing, movable.options), std::runtime_error);
}

TEST(TerminalLegalize, WireCostChoosesConnectedSideAndDoesNotPullLegalCells) {
  Fixture f(60, 1);
  f.options.bins_y            = 1;
  f.options.padding_sites     = 0;
  f.options.critical_fraction = 0;
  f.options.local_pair_limit  = 0;
  const auto block = f.cell(20, 0), anchor = f.cell(50, 0);
  f.fp.components[block].place.status = f.fp.components[anchor].place.status = physical::Status::FIXED;
  const auto moving = f.cell(20, 0), retained = f.cell(30, 0);
  f.net(moving, anchor, -10);
  f.net(retained, anchor, -5);
  auto nearest                          = f;
  nearest.options.remainder_wire_weight = 0;
  terminal_legalize(nearest.tech, nearest.fp, nearest.timing, nearest.options);
  EXPECT_EQ(nearest.fp.components[moving].place.x, 1900);
  f.options.remainder_wire_weight = .5;
  const auto report               = terminal_legalize(f.tech, f.fp, f.timing, f.options);
  f.check(report);
  EXPECT_EQ(f.fp.components[moving].place.x, 2100);
  EXPECT_EQ(f.fp.components[retained].place.x, 3000);
  EXPECT_EQ(f.fp.components[anchor].place.x, 5000);
  EXPECT_EQ(report.wire_changed_cells, 1);
}

TEST(TerminalLegalize, WireCostMatchesExhaustiveSitesWithMirroredMultiPinNets) {
  for (double weight : {.5, 2., 10.}) {
    Fixture f(40, 4);
    f.options.bins_x                = 4;
    f.options.bins_y                = 1;
    f.options.padding_sites         = 0;
    f.options.critical_fraction     = 0;
    f.options.local_pair_limit      = 0;
    f.options.remainder_wire_weight = weight;
    for (size_t y = 0; y < f.fp.rows.size(); ++y) {
      f.fp.rows[y].orientation = y % 2 ? physical::Orientation::FS : physical::Orientation::N;
    }
    auto& m                    = f.tech.macros.at("cell");
    m.pins["Y"].shapes[0].rect = {.7, .1, .9, .3};
    m.pins["A"].shapes[0].rect = {.1, .7, .3, .9};
    const auto block = f.cell(10, 1), right = f.cell(30, 0), upper = f.cell(35, 3);
    for (auto i : {block, right, upper}) {
      auto& p       = f.fp.components[i].place;
      p.status      = physical::Status::FIXED;
      p.orientation = f.fp.rows[p.y / 100].orientation;
    }
    const auto moving = f.cell(10, 1);
    f.net(moving, right, -10);
    f.fp.nets.back().connections.push_back({f.fp.components[moving].name, "A"});
    f.net(moving, upper, -5);
    const auto cost = [&](int x, int y) {
      const double py = y * 100 + (y % 2 ? 80 : 20);
      const double ay = y * 100 + (y % 2 ? 20 : 80);
      const double px = x * 100 + 80, ax = x * 100 + 20;
      const double first
          = std::max({px, ax, 3020.}) - std::min({px, ax, 3020.}) + std::max({py, ay, 80.}) - std::min({py, ay, 80.});
      const double second = std::abs(px - 3520) + std::abs(py - 320);
      return 100. * (std::abs(x - 10) + std::abs(y - 1)) + weight * (first + second);
    };
    double best = INFINITY;
    for (int x = 0; x < 40; ++x) {
      for (int y = 0; y < 4; ++y) {
        if ((x == 10 && y == 1) || (x == 30 && y == 0) || (x == 35 && y == 3)) {
          continue;
        }
        best = std::min(best, cost(x, y));
      }
    }
    const auto report = terminal_legalize(f.tech, f.fp, f.timing, f.options);
    f.check(report);
    const auto& p = f.fp.components[moving].place;
    EXPECT_NEAR(cost(p.x / 100, p.y / 100), best, 1e-6) << "weight=" << weight;
  }
}

TEST(TerminalLegalize, CriticalPairSpansBinsAndRemainsFixedAsCrowdSpills) {
  Fixture f(100, 2);
  f.options.bins_y            = 1;
  f.options.critical_fraction = 1;
  f.options.local_pair_limit  = 0;
  const auto a = f.cell(5, 0), b = f.cell(65, 0);
  f.net(a, b, -10);
  for (int i = 0; i < 45; ++i) {
    f.cell(5, 0);
  }
  const auto report = terminal_legalize(f.tech, f.fp, f.timing, f.options);
  f.check(report);
  EXPECT_EQ(report.critical_pairs, 1);
  ASSERT_EQ(report.alignments.size(), 1);
  Satlegalize_report exported;
  exported.accepted = report.alignments;
  EXPECT_TRUE(satlegalize_preserved(f.tech, f.fp, exported));
  EXPECT_EQ(f.fp.components[a].place.x, 500);
  EXPECT_EQ(f.fp.components[b].place.x, 6500);
  EXPECT_EQ(f.fp.components[a].place.y, 0);
  EXPECT_EQ(f.fp.components[b].place.y, 0);
  EXPECT_EQ(f.fp.components[a].place.status, physical::Status::PLACED);
  EXPECT_EQ(report.critical_cells, 2);
}

TEST(TerminalLegalize, CriticalSelectionIsByNetAndFallbackLocksUnalignedCells) {
  Fixture f;
  f.options.critical_fraction   = .5;
  f.options.critical_pair_limit = 0;
  f.options.local_pair_limit    = 0;
  const auto a = f.cell(10, 0), b = f.cell(11, 0), c = f.cell(12, 0), d = f.cell(13, 0);
  f.net(a, b, -5);
  f.net(c, d, 2);
  const auto report = terminal_legalize(f.tech, f.fp, f.timing, f.options);
  f.check(report);
  EXPECT_EQ(report.critical_nets, 1);
  EXPECT_EQ(report.critical_cells, 2);
  EXPECT_TRUE(report.cells[a].critical);
  EXPECT_TRUE(report.cells[b].critical);
  EXPECT_FALSE(report.cells[c].critical);
  EXPECT_EQ(report.cells[a].stage, 2);
}

TEST(TerminalLegalize, MacroHaloAndFragmentedRowsReduceUsableCapacity) {
  Fixture f(40, 4);
  f.options.bins_x           = 4;
  f.options.bins_y           = 1;
  auto& m                    = f.tech.macros["macro"];
  m.width                    = 8;
  m.height                   = 2;
  m.klass                    = "BLOCK";
  const auto macro           = f.cell(10, 1, "macro");
  f.options.macro_halo_x_dbu = 100;
  f.options.macro_halo_y_dbu = 100;
  // Remove a row segment at the opposite edge.
  f.fp.rows[0].count         = 30;
  for (int i = 0; i < 20; ++i) {
    f.cell(11, 1);
  }
  const auto before = f.fp.components[macro];
  const auto report = terminal_legalize(f.tech, f.fp, f.timing, f.options);
  f.check(report);
  EXPECT_EQ(f.fp.components[macro], before);
  double capacity = 0;
  for (const auto& b : report.bins) {
    capacity += b.capacity;
  }
  EXPECT_DOUBLE_EQ(capacity, (40 * 4 - 10 - 10 * 4) * 10000.);
  for (const auto& c : report.cells) {
    EXPECT_TRUE(c.after.x + c.after.width <= 900 || c.after.x >= 1900);
  }
}

TEST(TerminalLegalize, FailureIsAtomicAndDoesNotRelaxPaddingOrDensity) {
  Fixture f(10, 1);
  f.options.bins_x = 1;
  f.options.bins_y = 1;
  for (int i = 0; i < 4; ++i) {
    f.cell(1, 0);
  }
  const auto before = f.fp;
  EXPECT_THROW(terminal_legalize(f.tech, f.fp, f.timing, f.options), std::runtime_error);
  EXPECT_EQ(f.fp.components, before.components);
  f.options.padding_sites = 0;
  const auto report       = terminal_legalize(f.tech, f.fp, f.timing, f.options);
  f.check(report);
}

TEST(TerminalLegalize, FixedCellsConsumeDensityAndArePreserved) {
  Fixture f(20, 1);
  f.options.bins_x                    = 2;
  f.options.bins_y                    = 1;
  f.options.padding_sites             = 0;
  const auto fixed                    = f.cell(0, 0);
  f.fp.components[fixed].place.status = physical::Status::FIXED;
  for (int i = 0; i < 12; ++i) {
    f.cell(0, 0);
  }
  const auto report = terminal_legalize(f.tech, f.fp, f.timing, f.options);
  f.check(report);
  EXPECT_EQ(f.fp.components[fixed].place.x, 0);
  EXPECT_EQ(f.fp.components[fixed].place.status, physical::Status::FIXED);
  EXPECT_LE(report.bins[0].after, 80000);
}

TEST(TerminalLegalize, FixedPhysicalInfrastructureReducesCapacityWithoutConsumingLogicDensity) {
  for (const auto* klass : {"CORE WELLTAP", "CORE SPACER"}) {
    SCOPED_TRACE(klass);
    Fixture f(20, 1);
    f.options.bins_x           = 2;
    f.options.bins_y           = 1;
    f.options.padding_sites    = 0;
    f.options.max_density      = .3;
    f.options.target_density   = .2;
    // Physical cells reserve their body, without inheriting a macro halo.
    f.options.macro_halo_x_dbu = f.options.macro_halo_y_dbu = 1000;
    auto& tap                                               = f.tech.macros["tap"];
    tap                                                     = f.tech.macros.at("cell");
    tap.width                                               = 8;
    tap.klass                                               = klass;
    tap.pins.clear();
    const auto i                    = f.cell(0, 0, "tap");
    f.fp.components[i].place.status = physical::Status::FIXED;
    const auto original             = f.fp.components[i];
    f.cell(0, 0);
    f.cell(0, 0);
    fit_terminal_legalize_grid(f.tech, f.fp, f.options);
    EXPECT_EQ(f.options.bins_x, 2);
    Terminal_legalize_report report;
    ASSERT_NO_THROW(report = terminal_legalize(f.tech, f.fp, f.timing, f.options));
    EXPECT_EQ(f.fp.components[i], original);
    ASSERT_EQ(report.bins.size(), 2);
    EXPECT_DOUBLE_EQ(report.bins[0].capacity, 20000);
    EXPECT_DOUBLE_EQ(report.bins[1].capacity, 100000);
    EXPECT_DOUBLE_EQ(report.bins[0].after, 0);
    EXPECT_DOUBLE_EQ(report.bins[1].after, 20000);
    EXPECT_EQ(report.cells.size(), 2);
    for (const auto& c : report.cells) {
      EXPECT_GE(c.after.x, 1000);
    }
    EXPECT_NE(f.fp.components[1].place.x, f.fp.components[2].place.x);
  }
}

TEST(TerminalLegalize, FixedLogicStillCannotExceedTheHardDensityCap) {
  Fixture f(40, 1);
  f.options.bins_x                = 2;
  f.options.bins_y                = 1;
  f.options.padding_sites         = 0;
  f.options.max_density           = .3;
  f.options.target_density        = .2;
  f.tech.macros["cell"].width     = 8;
  const auto i                    = f.cell(0, 0);
  f.fp.components[i].place.status = physical::Status::FIXED;
  const auto before               = f.fp.components;
  EXPECT_THROW(terminal_legalize(f.tech, f.fp, f.timing, f.options), std::runtime_error);
  EXPECT_EQ(f.fp.components, before);
}

TEST(TerminalLegalize, RejectsInvalidControls) {
  Fixture f;
  f.cell(2, 0);
  f.options.target_density = f.options.max_density;
  EXPECT_THROW(terminal_legalize(f.tech, f.fp, f.timing, f.options), std::invalid_argument);
  f.options.target_density    = .5;
  f.options.critical_fraction = std::numeric_limits<double>::quiet_NaN();
  EXPECT_THROW(terminal_legalize(f.tech, f.fp, f.timing, f.options), std::invalid_argument);
}

TEST(TerminalLegalize, FragmentationRetryPreservesCriticalLocks) {
  Fixture f(10, 1);
  f.options.bins_x = f.options.bins_y = 1;
  f.options.padding_sites             = 0;
  f.options.critical_fraction         = .5;
  f.options.local_pair_limit          = 0;
  f.tech.macros["wide"]               = f.tech.macros.at("cell");
  f.tech.macros["wide"].width         = 6;
  const auto locked = f.cell(9, 0), small = f.cell(5, 0), wide = f.cell(0, 0, "wide");
  for (const auto& [i, slack] : {std::pair{locked, -10.}, std::pair{small, 0.}}) {
    const auto& name = f.fp.components[i].name;
    f.fp.nets.push_back({"n" + name, {{"PIN", "in"}, {name, "A"}}});
    f.timing.pins[name + ":A"].slack = slack;
  }
  const auto report = terminal_legalize(f.tech, f.fp, f.timing, f.options);
  f.check(report);
  EXPECT_EQ(report.packing_retries, 1);
  EXPECT_EQ(f.fp.components[locked].place.x, 900);
  EXPECT_EQ(report.cells[locked].stage, 2);
  EXPECT_EQ(f.fp.components[wide].place.x, 0);
}

TEST(TerminalLegalize, NearbyDestinationAcrossBinBoundaryWins) {
  Fixture f(20, 1);
  f.options.bins_x            = 2;
  f.options.bins_y            = 1;
  f.options.padding_sites     = 0;
  f.options.critical_fraction = 0;
  f.options.local_pair_limit  = 0;
  // The old first-nonempty-bin search chose x=2 instead of adjacent x=10.
  for (int x = 3; x <= 9; ++x) {
    const auto i                    = f.cell(x, 0);
    f.fp.components[i].place.status = physical::Status::FIXED;
  }
  const auto moving = f.cell(9, 0);
  const auto report = terminal_legalize(f.tech, f.fp, f.timing, f.options);
  f.check(report);
  EXPECT_EQ(f.fp.components[moving].place.x, 1000);
}

TEST(TerminalLegalize, LaterObstructionCannotBlockAcceptedCriticalCorridor) {
  Fixture f(100, 2);
  f.options.bins_y            = 1;
  f.options.critical_fraction = 1;
  f.options.local_pair_limit  = 0;
  const auto a = f.cell(5, 0), b = f.cell(65, 0);
  f.net(a, b, -10);
  f.tech.macros["blocker"] = f.tech.macros.at("cell");
  f.tech.macros["blocker"].blocked_layers.insert("M1");
  f.tech.macros["blocker"].conservative_obstructions.insert("M1");
  const auto blocker = f.cell(35, 0, "blocker");
  const auto report  = terminal_legalize(f.tech, f.fp, f.timing, f.options);
  f.check(report);
  ASSERT_EQ(report.critical_pairs, 1);
  EXPECT_EQ(f.fp.components[a].place.x, 500);
  EXPECT_EQ(f.fp.components[b].place.x, 6500);
  EXPECT_EQ(f.fp.components[blocker].place.y, 100);
}

TEST(TerminalLegalize, CriticalSpacingCanBeTunedWithoutMovingAcceptedCellsLater) {
  Fixture f(20, 1);
  f.options.bins_x              = 2;
  f.options.bins_y              = 1;
  f.options.critical_fraction   = 1;
  f.options.critical_pair_limit = 0;
  const auto a = f.cell(5, 0), b = f.cell(6, 0);
  f.net(a, b, -10);
  for (int n = 0; n < 3; ++n) {
    f.cell(5, 0);
  }
  const auto report = terminal_legalize(f.tech, f.fp, f.timing, f.options);
  f.check(report);
  EXPECT_EQ(f.fp.components[a].place.x, 500);
  EXPECT_EQ(f.fp.components[b].place.x, 600);
}

TEST(TerminalLegalize, OptionalUntimedSideInputsProtectAllNetEndpointsButNotClocks) {
  Fixture f(100, 2);
  f.options.critical_fraction    = .5;
  f.options.critical_fanin_depth = 1;
  f.options.critical_pair_limit = f.options.local_pair_limit = 0;
  auto& master                                               = f.tech.macros["cell"];
  master.pins["B"]                                           = master.pins["A"];
  master.pins["CLK"]                                         = master.pins["A"];
  master.pins["CLK"].use                                     = "CLOCK";
  const auto a = f.cell(5, 0), b = f.cell(6, 0), u = f.cell(10, 0), v = f.cell(11, 0);
  const auto c = f.cell(20, 0), d = f.cell(21, 0), clock = f.cell(25, 0);
  f.net(a, b, -10);
  f.net(u, v, 0);
  const auto connection = [&](size_t i, const std::string& pin) { return physical::Connection{f.fp.components[i].name, pin}; };
  f.fp.nets.push_back({"side", {connection(c, "Y"), connection(a, "B"), connection(d, "A")}});
  f.fp.nets.push_back({"clock", {connection(clock, "Y"), connection(a, "CLK")}});
  const auto report = terminal_legalize(f.tech, f.fp, f.timing, f.options);
  f.check(report);
  EXPECT_EQ(report.critical_nets, 1);
  EXPECT_EQ(report.supporting_cells, 2);
  EXPECT_EQ(report.critical_cells, 4);
  EXPECT_EQ(report.cells[c].stage, 2);
  EXPECT_EQ(report.cells[d].stage, 2);
  EXPECT_FALSE(report.cells[clock].critical);
  EXPECT_EQ(f.fp.components[c].place.x, 2000);
  EXPECT_EQ(f.fp.components[d].place.x, 2100);
}

TEST(TerminalLegalize, InteriorSiteSplitsWideCellAcrossTwoCapacityLimitedBins) {
  Fixture f(10, 1);
  f.options.bins_x            = 2;
  f.options.bins_y            = 1;
  f.options.padding_sites     = 0;
  f.tech.macros["wide"]       = f.tech.macros.at("cell");
  f.tech.macros["wide"].width = 6;
  const auto wide             = f.cell(0, 0, "wide");
  // Both gap boundaries overload one bin. An interior site splits area 4:2.
  const auto report           = terminal_legalize(f.tech, f.fp, f.timing, f.options);
  f.check(report);
  EXPECT_EQ(report.full_site_scans, 1);
  EXPECT_EQ(report.packing_retries, 0);
  EXPECT_EQ(f.fp.components[wide].place.x, 100);
}
TEST(TerminalLegalize, JointSolveMovesEarlierPairToKeepConstrainedPair) {
  Fixture f(100, 2);
  f.options.bins_y            = 1;
  f.options.critical_fraction = 1;
  f.options.local_pair_limit  = 0;
  f.options.padding_sites     = 0;
  f.options.pair_move_sites   = 0;
  const auto a = f.cell(5, 0), b = f.cell(65, 0), c = f.cell(5, 0), d = f.cell(75, 0);
  const auto obstacle                    = f.cell(75, 1);
  f.fp.components[obstacle].place.status = physical::Status::FIXED;
  f.net(a, b, -10);
  f.net(c, d, -5);
  // Greedily fixing A/B at their nearest sites makes C/D impossible. SAT
  // must choose row 1 for A/B while keeping C/D in their only available row.
  const auto report = terminal_legalize(f.tech, f.fp, f.timing, f.options);
  f.check(report);
  ASSERT_EQ(report.critical_pairs, 2);
  EXPECT_EQ(report.joint_pairs, 1);
  EXPECT_EQ(report.anchored_pairs, 1);
  EXPECT_EQ(report.joint_dropped, 0);
  EXPECT_EQ(f.fp.components[a].place.y, 100);
  EXPECT_EQ(f.fp.components[b].place.y, 100);
  EXPECT_EQ(f.fp.components[c].place.y, 0);
  EXPECT_EQ(f.fp.components[d].place.y, 0);
  for (size_t cell : {a, b, c, d}) {
    EXPECT_EQ(report.cells[cell].stage, 1);
    EXPECT_EQ(report.cells[cell].bin_distance, 0);
  }
}

TEST(TerminalLegalize, SmallJointMenuRetainsAnAlternateRow) {
  Fixture f(100, 2);
  f.options.bins_y            = 1;
  f.options.critical_fraction = 1;
  f.options.local_pair_limit  = 0;
  f.options.padding_sites     = 0;
  f.options.candidates        = 2;
  f.options.pair_move_sites   = 2;
  const auto a = f.cell(5, 0), b = f.cell(65, 0), c = f.cell(5, 0), d = f.cell(75, 0);
  const auto obstacle                    = f.cell(75, 1);
  f.fp.components[obstacle].place.status = physical::Status::FIXED;
  f.net(a, b, -10);
  f.net(c, d, -5);
  // Two nearest poses are on row 0 (one site apart). Keeping an alternate
  // row lets the joint solve move A/B and preserve the constrained C/D pair.
  const auto report = terminal_legalize(f.tech, f.fp, f.timing, f.options);
  f.check(report);
  EXPECT_EQ(report.critical_pairs, 2);
  EXPECT_EQ(report.joint_dropped, 0);
  EXPECT_EQ(f.fp.components[a].place.y, 100);
  EXPECT_EQ(f.fp.components[b].place.y, 100);
  EXPECT_EQ(f.fp.components[c].place.y, 0);
  EXPECT_EQ(f.fp.components[d].place.y, 0);
}

TEST(TerminalLegalize, CrowdedBoxDropsOnlyItsLeastCriticalSelection) {
  Fixture f(20, 1);
  f.options.bins_x            = 2;
  f.options.bins_y            = 1;
  f.options.padding_sites     = 0;
  f.options.critical_fraction = 1;
  f.options.local_pair_limit  = 0;
  f.options.pair_move_sites = f.options.pair_move_rows = 0;
  for (int i = 0; i < 3; ++i) {
    const auto a = f.cell(2 * i, 0), b = f.cell(2 * i + 1, 0);
    f.net(a, b, -10 + i);
  }
  const auto remote_a = f.cell(12, 0), remote_b = f.cell(13, 0);
  f.net(remote_a, remote_b, -1);
  // Bin 0 can reserve five cells at target density, but six were selected.
  // Its least critical pair must be deferred, not the weaker remote net.
  const auto report = terminal_legalize(f.tech, f.fp, f.timing, f.options);
  f.check(report);
  EXPECT_EQ(report.joint_selected, 4);
  EXPECT_EQ(report.joint_dropped, 1);
  EXPECT_EQ(report.critical_pairs, 3);
  EXPECT_GT(report.joint_cuts, 0);
  EXPECT_EQ(report.cells[4].stage, 2);
  EXPECT_EQ(report.cells[5].stage, 2);
  EXPECT_EQ(report.cells[remote_a].stage, 1);
  EXPECT_EQ(report.cells[remote_b].stage, 1);
}

TEST(TerminalLegalize, InitialBudgetExhaustionRetainsAValidatedJointSubset) {
  Fixture f(100, 8);
  f.options.bins_y               = 1;
  f.options.padding_sites        = 0;
  f.options.critical_fraction    = 1;
  f.options.local_pair_limit     = 0;
  f.options.pair_move_sites      = 0;
  f.options.pair_move_rows       = 7;
  f.options.candidates           = 8;
  f.options.sat_conflicts        = 1;
  f.options.sat_milliseconds     = 1000;
  f.options.joint_minimize_total = true;
  for (int i = 0; i < 10; ++i) {
    const auto a = f.cell(5, 0), b = f.cell(65, 0);
    f.net(a, b, -10 + i);
  }
  // Ten independent paths compete for eight tracks. The small conflict
  // budget interrupts the coupled proof. Recovery must retain actual aligned
  // pairs, never an unchecked model or a half-installed pair.
  const auto report = terminal_legalize(f.tech, f.fp, f.timing, f.options);
  f.check(report);
  EXPECT_TRUE(report.joint_admission_fallback);
  EXPECT_GT(report.sat_unknown, 0);
  EXPECT_GT(report.critical_pairs, 0);
  EXPECT_LE(report.critical_pairs, 8);
  EXPECT_TRUE(report.joint_feasible);
  std::set<int64_t> tracks;
  size_t            accepted = 0;
  for (size_t a = 0; a < 20; a += 2) {
    const auto b = a + 1;
    EXPECT_EQ(report.cells[a].stage, report.cells[b].stage);
    if (report.cells[a].stage == 1) {
      ++accepted;
      EXPECT_EQ(f.fp.components[a].place.y, f.fp.components[b].place.y);
      EXPECT_TRUE(tracks.insert(f.fp.components[a].place.y).second);
      EXPECT_EQ(f.fp.components[a].place.x, 500);
      EXPECT_EQ(f.fp.components[b].place.x, 6500);
    }
  }
  EXPECT_EQ(accepted, report.critical_pairs);
}

TEST(TerminalLegalize, JointEndpointObstructionIsEncodedOnCommonTrackSegment) {
  Fixture f(100, 2);
  f.options.bins_y            = 1;
  f.options.padding_sites     = 0;
  f.options.critical_fraction = 1;
  f.options.local_pair_limit  = 0;
  f.options.pair_move_sites = f.options.pair_move_rows = 0;
  f.tech.layers["M2"]                                  = f.tech.layers.at("M1");
  f.tech.layers["M2"].direction                        = "VERTICAL";
  auto& blocker                                        = f.tech.macros["blocker"];
  blocker                                              = f.tech.macros.at("cell");
  blocker.pins["A"].shapes.front().layer               = "M2";
  blocker.pins["Y"].shapes.front().layer               = "M2";
  blocker.blocked_layers.insert("M1");
  blocker.conservative_obstructions.insert("M1");
  const auto a = f.cell(5, 0), b = f.cell(65, 0);
  const auto c = f.cell(40, 0, "blocker"), d = f.cell(40, 1, "blocker");
  f.net(a, b, -10);
  f.net(c, d, -5);
  // C's M1 obstruction crosses A/B's only track. Its own M2 connection is
  // individually feasible, but the two selected nets cannot coexist.
  const auto report = terminal_legalize(f.tech, f.fp, f.timing, f.options);
  f.check(report);
  EXPECT_EQ(report.critical_pairs, 1);
  EXPECT_EQ(report.joint_no_witness, 0);
  EXPECT_EQ(report.joint_conflict_dropped, 1);
  EXPECT_GT(report.joint_static_obstructions, 0);
  EXPECT_EQ(report.joint_obstruction_cuts, 0);
  EXPECT_EQ(report.cells[a].stage, 1);
  EXPECT_EQ(report.cells[b].stage, 1);
  EXPECT_EQ(report.cells[c].stage, 2);
  EXPECT_EQ(report.cells[d].stage, 2);
}

TEST(TerminalLegalize, OverlappingAlignmentIsNotAUsableWitness) {
  Fixture f(100, 1);
  f.options.bins_y            = 1;
  f.options.padding_sites     = 0;
  f.options.critical_fraction = 1;
  f.options.local_pair_limit  = 0;
  f.options.pair_move_sites = f.options.pair_move_rows = 0;
  const auto a = f.cell(5, 0), b = f.cell(5, 0);
  f.net(a, b, -10);
  auto input  = f.fp;
  auto report = terminal_legalize(f.tech, f.fp, f.timing, f.options);
  f.check(report);
  EXPECT_EQ(report.joint_no_witness, 1);
  EXPECT_EQ(report.joint_conflict_dropped, 0);
  EXPECT_EQ(report.critical_pairs, 0);
  EXPECT_EQ(report.cells[a].stage, 2);
  EXPECT_EQ(report.cells[b].stage, 2);
  // The original box has ample room. Expanding the allowed menu must recover
  // the alignment without anchoring either endpoint or crossing a bin boundary.
  f.fp                      = input;
  f.options.pair_move_sites = 2;
  report                    = terminal_legalize(f.tech, f.fp, f.timing, f.options);
  f.check(report);
  EXPECT_EQ(report.joint_no_witness, 0);
  EXPECT_EQ(report.joint_conflict_dropped, 0);
  EXPECT_EQ(report.critical_pairs, 1);
  EXPECT_EQ(report.cells[a].stage, 1);
  EXPECT_EQ(report.cells[b].stage, 1);
  EXPECT_EQ(report.cells[a].bin_distance, 0);
  EXPECT_EQ(report.cells[b].bin_distance, 0);
}

TEST(TerminalLegalize, JointMenusRetainAlignedPosesInsteadOfOnlyNearestRows) {
  for (unsigned radius : {1u, 4u}) {
    Fixture f(100, 10);
    f.options.bins_x = f.options.bins_y = 1;
    f.options.padding_sites             = 0;
    f.options.critical_fraction         = 1;
    f.options.local_pair_limit          = 0;
    f.options.candidates                = 2;
    f.options.pair_move_sites           = radius * 4;
    f.options.pair_move_rows            = radius;
    const auto a = f.cell(5, 4), b = f.cell(5, 4);
    f.net(a, b, -10);
    // Both endpoints have many legal rows, but putting both at the nearest X
    // on every row provides no nonoverlapping horizontal alignment. Two poses
    // per cell suffice when the menus are selected using actual pair support.
    const auto report = terminal_legalize(f.tech, f.fp, f.timing, f.options);
    f.check(report);
    EXPECT_EQ(report.joint_selected, 1);
    EXPECT_EQ(report.critical_pairs, 1);
    EXPECT_EQ(report.joint_no_witness, 0);
    EXPECT_EQ(report.joint_menu_recovered, 1);
    EXPECT_EQ(report.cells[a].stage, 1);
    EXPECT_EQ(report.cells[b].stage, 1);
    EXPECT_EQ(f.fp.components[a].place.y, f.fp.components[b].place.y);
    EXPECT_NE(f.fp.components[a].place.x, f.fp.components[b].place.x);
    EXPECT_EQ(report.cells[a].bin_distance, 0);
    EXPECT_EQ(report.cells[b].bin_distance, 0);
  }
}
TEST(TerminalLegalize, RecoveredMenuPropagatesThroughSharedCellWithoutPairwiseCommit) {
  Fixture f(100, 4);
  f.options.bins_x = f.options.bins_y            = 1;
  f.options.padding_sites                        = 0;
  f.options.critical_fraction                    = 1;
  f.options.local_pair_limit                     = 0;
  f.options.candidates                           = 2;
  f.tech.macros["cell"].pins["A"].shapes[0].rect = {.1, .4, .3, .6};
  f.tech.macros["cell"].pins["Y"].shapes[0].rect = {.7, .4, .9, .6};
  const auto a = f.cell(5, 1), b = f.cell(5, 1), c = f.cell(60, 1);
  f.net(a, b, -10);
  f.net(b, c, -5);
  // The recovered A/B menu shares B with an already supported B/C edge.
  // Retaining one B decision in global SAT must choose a compatible order.
  const auto report = terminal_legalize(f.tech, f.fp, f.timing, f.options);
  f.check(report);
  EXPECT_EQ(report.critical_pairs, 2);
  EXPECT_EQ(report.joint_menu_recovered, 1);
  EXPECT_EQ(report.cells[a].stage, 1);
  EXPECT_EQ(report.cells[b].stage, 1);
  EXPECT_EQ(report.cells[c].stage, 1);
  EXPECT_LT(f.fp.components[a].place.x, f.fp.components[b].place.x);
  EXPECT_LT(f.fp.components[b].place.x, f.fp.components[c].place.x);
}
TEST(TerminalLegalize, TwoChoiceMenuCanRecoverVerticalAlignmentAcrossSiteOffsets) {
  Fixture f(100, 6);
  f.options.bins_x = f.options.bins_y = 1;
  f.options.padding_sites             = 0;
  f.options.critical_fraction         = 1;
  f.options.local_pair_limit          = 0;
  f.options.candidates                = 2;
  f.tech.layers["M1"].direction       = "VERTICAL";
  const auto a = f.cell(5, 1), b = f.cell(7, 3);
  f.net(a, b, -10);
  // Two nearest X poses per row have disjoint access tracks. The allowed
  // domain includes a common track, which must survive the final two-choice cap.
  const auto report = terminal_legalize(f.tech, f.fp, f.timing, f.options);
  f.check(report);
  EXPECT_EQ(report.critical_pairs, 1);
  EXPECT_EQ(report.joint_menu_recovered, 1);
  EXPECT_EQ(f.fp.components[a].place.x, f.fp.components[b].place.x);
  EXPECT_NE(f.fp.components[a].place.y, f.fp.components[b].place.y);
}
TEST(TerminalLegalize, RouteDrcSkipsUnselectedJointEndpoints) {
  Fixture f(100, 10);
  f.tech.manufacturing_grid = .01;
  auto& layer               = f.tech.layers.at("M1");
  layer.rules.loaded        = true;
  layer.rules.spacing       = {{.1, {}, {}}};
  f.tech.macros["driver"]   = f.tech.macros.at("cell");
  f.tech.macros["driver"].pins.erase("A");
  f.tech.macros["sink"] = f.tech.macros.at("cell");
  f.tech.macros["sink"].pins.erase("Y");
  f.options.route_drc         = true;
  f.options.padding_sites     = 0;
  f.options.critical_fraction = 1;
  f.options.local_pair_limit  = 0;
  f.options.pair_move_sites = f.options.pair_move_rows = 0;
  const auto a = f.cell(5, 1, "driver"), b = f.cell(15, 1, "sink");
  const auto c = f.cell(45, 1, "driver"), d = f.cell(55, 3, "sink");
  f.net(a, b, -10);
  f.net(c, d, -5);
  const auto report = terminal_legalize(f.tech, f.fp, f.timing, f.options);
  f.check(report);
  EXPECT_EQ(report.joint_selected, 2);
  EXPECT_EQ(report.critical_pairs, 1);
  EXPECT_EQ(report.joint_dropped, 1);
  EXPECT_EQ(f.fp.components[c].place.y, 100);
  EXPECT_EQ(f.fp.components[d].place.y, 300);
}
TEST(TerminalLegalize, JointCandidatesCannotLeaveTheirOriginalBox) {
  Fixture f(100, 10);
  f.options.padding_sites     = 0;
  f.options.critical_fraction = 1;
  f.options.local_pair_limit  = 0;
  f.options.pair_move_sites   = 0;
  const auto a = f.cell(5, 4), b = f.cell(15, 5);
  f.net(a, b, -10);
  // Same vertical strip, but a horizontal M1 alignment would require crossing
  // a bin boundary. Defer the pair instead of moving either endpoint across.
  const auto report = terminal_legalize(f.tech, f.fp, f.timing, f.options);
  f.check(report);
  EXPECT_EQ(report.joint_selected, 1);
  EXPECT_EQ(report.critical_pairs, 0);
  EXPECT_EQ(report.joint_dropped, 1);
  EXPECT_EQ(f.fp.components[a].place.y, 400);
  EXPECT_EQ(f.fp.components[b].place.y, 500);
}

TEST(TerminalLegalize, MultiTerminalNetDefersToCriticalCellLegalization) {
  Fixture f;
  f.options.critical_fraction = 1;
  f.options.local_pair_limit  = 0;
  const auto a = f.cell(5, 0), b = f.cell(10, 0), c = f.cell(15, 0);
  f.net(a, b, -10);
  f.fp.nets.back().connections.push_back({f.fp.components[c].name, "A"});
  const auto report = terminal_legalize(f.tech, f.fp, f.timing, f.options);
  f.check(report);
  EXPECT_EQ(report.critical_cells, 3);
  EXPECT_EQ(report.joint_selected, 0);
  EXPECT_EQ(report.critical_pairs, 0);
  for (const auto& cell : report.cells) {
    EXPECT_EQ(cell.stage, 2);
  }
}

TEST(TerminalLegalize, TotalObjectiveAvoidsGratuitousMovementOnIndependentNet) {
  Fixture f(100, 2);
  f.options.bins_y               = 1;
  f.options.critical_fraction    = 1;
  f.options.local_pair_limit     = 0;
  f.options.padding_sites        = 0;
  f.options.pair_move_sites      = 0;
  f.options.joint_minimize_total = true;
  const auto a = f.cell(5, 0), b = f.cell(65, 0), c = f.cell(5, 0), d = f.cell(75, 0);
  const auto e = f.cell(85, 0), g = f.cell(95, 0), obstacle = f.cell(75, 1);
  f.fp.components[obstacle].place.status = physical::Status::FIXED;
  f.net(a, b, -10);
  f.net(c, d, -5);
  f.net(e, g, -1);
  // A/B must each move one row to admit C/D. The independent last pair has
  // no reason to move, even though the minimax bound permits it to do so.
  const auto report = terminal_legalize(f.tech, f.fp, f.timing, f.options);
  f.check(report);
  EXPECT_EQ(report.critical_pairs, 3);
  EXPECT_EQ(report.total_displacement_dbu, 200);
  EXPECT_TRUE(report.joint_feasible);
  EXPECT_TRUE(report.joint_max_optimized);
  EXPECT_TRUE(report.joint_total_optimized);
  EXPECT_EQ(f.fp.components[e].place.y, 0);
  EXPECT_EQ(f.fp.components[g].place.y, 0);
}

namespace {
void check_parallel_maze(unsigned lanes, unsigned width = 4) {
  Fixture f(60, 60);
  f.options.bins_x = f.options.bins_y = 6;
  f.options.critical_fraction         = 1;
  f.options.padding_sites             = 0;
  f.options.local_pair_limit          = 0;
  f.options.candidates                = 128;
  f.options.pair_move_sites = f.options.pair_move_rows = 8;
  f.options.joint_minimize_total                       = false;  // This fixture proves track feasibility, not sum optimality.
  f.options.sat_conflicts                              = 1000000;
  f.options.sat_milliseconds                           = 1000;
  auto& vertical                                       = f.tech.layers["M2"];
  vertical                                             = f.tech.layers.at("M1");
  vertical.direction                                   = "VERTICAL";
  // Every bottleneck has exactly width legal tracks, bounded by routing-blocked
  // walls. That cut gives an independent maximum number of parallel paths.
  const int  start = 9 - int(width), turn = 29 - int(width), finish = 49 - int(width);
  const auto channel = [=](int x, int y) {
    return (x >= start && x < 29 && y >= start && y < 9) || (x >= turn && x < 29 && y >= start && y < 29)
           || (x >= start && x < 29 && y >= turn && y < 29) || (x >= start && x < 9 && y >= turn && y < 49)
           || (x >= start && x < 49 && y >= finish && y < 49);
  };
  for (int y = 0; y < 60; ++y) {
    for (int x = 0; x < 60;) {
      if (channel(x, y)) {
        ++x;
        continue;
      }
      const int begin = x;
      while (x < 60 && !channel(x, y)) {
        ++x;
      }
      const std::string name           = "wall" + std::to_string(x - begin);
      auto&             wall           = f.tech.macros[name];
      wall.width                       = x - begin;
      wall.height                      = 1;
      wall.klass                       = "BLOCK";
      wall.blocked_layers              = {"M1", "M2"};
      wall.conservative_obstructions   = {"M1", "M2"};
      const auto id                    = f.cell(begin, y, name);
      f.fp.components[id].place.status = physical::Status::FIXED;
    }
  }
  const size_t                       wall_count = f.fp.components.size();
  const int                          base_x[]   = {start, turn, turn, start, start, finish};
  const int                          base_y[]   = {start, start, turn, turn, finish, finish};
  std::vector<std::vector<size_t>>   paths(lanes);
  std::vector<Satlegalize_alignment> edges;
  for (int corner = 0; corner < 6; ++corner) {
    const std::string name              = "corner" + std::to_string(corner);
    auto&             cell              = f.tech.macros[name];
    cell                                = f.tech.macros.at("cell");
    cell.pins["A"].shapes.front().layer = corner > 0 && corner % 2 == 0 ? "M2" : "M1";
    cell.pins["Y"].shapes.front().layer = corner % 2 == 1 && corner < 5 ? "M2" : "M1";
    for (unsigned lane = 0; lane < lanes; ++lane) {
      // Scramble each corner independently, retaining the original box.
      const int shuffled = (lane + corner) % width;
      paths[lane].push_back(
          f.cell(base_x[corner] + shuffled, base_y[corner] + ((2 * int(width) - 1 - int(lane) + corner) % int(width)), name));
    }
  }
  for (unsigned lane = 0; lane < lanes; ++lane) {
    for (int leg = 0; leg < 5; ++leg) {
      const auto a = paths[lane][leg], b = paths[lane][leg + 1];
      f.net(a, b, -100. + lane);
      edges.push_back({a, b, "Y", "A"});
    }
  }
  const auto before = f.fp;
  const auto report = terminal_legalize(f.tech, f.fp, f.timing, f.options);
  f.check(report);
  testing::Test::RecordProperty("channel_tracks", width);
  testing::Test::RecordProperty("requested_paths", lanes);
  testing::Test::RecordProperty("aligned_edges", report.critical_pairs);
  testing::Test::RecordProperty("dropped_edges", report.joint_dropped);
  ASSERT_EQ(report.joint_selected, 5 * lanes);
  if (lanes > width) {
    EXPECT_GT(report.joint_dropped, 0);
    EXPECT_LT(report.critical_pairs, 5 * lanes);
    EXPECT_EQ(report.sat_unknown, 0);
    return;
  }
  ASSERT_EQ(report.joint_dropped, 0);
  ASSERT_EQ(report.critical_pairs, 5 * lanes);
  ASSERT_EQ(report.critical_cells, 6 * lanes);
  EXPECT_EQ(report.sat_unknown, 0);
  std::vector<size_t> obstacles(f.fp.components.size());
  std::iota(obstacles.begin(), obstacles.end(), 0);
  std::vector<physical::Shape> corridors;
  for (const auto& edge : edges) {
    physical::Shape corridor;
    ASSERT_TRUE(satlegalize_aligned(f.tech,
                                    f.fp,
                                    edge,
                                    f.fp.components[edge.first].place,
                                    f.fp.components[edge.second].place,
                                    obstacles,
                                    &corridor));
    corridors.push_back(corridor);
  }
  for (size_t i = 0; i < corridors.size(); ++i) {
    for (size_t j = 0; j < i; ++j) {
      if (corridors[i].layer != corridors[j].layer) {
        continue;
      }
      const auto& a = corridors[i].rect;
      const auto& b = corridors[j].rect;
      EXPECT_TRUE(a.xh <= b.xl || b.xh <= a.xl || a.yh <= b.yl || b.yh <= a.yl);
    }
  }
  // Every leg uses each available track exactly once, not just a legal
  // subset; this is the independent capacity/saturation oracle.
  for (int leg = 0; leg < 5; ++leg) {
    std::set<int64_t> tracks;
    for (const auto& path : paths) {
      const auto& p = f.fp.components[path[leg]].place;
      const auto& q = f.fp.components[path[leg + 1]].place;
      if (leg % 2 == 0) {
        EXPECT_EQ(p.y, q.y);
        tracks.insert(p.y);
      } else {
        EXPECT_EQ(p.x, q.x);
        tracks.insert(p.x);
      }
    }
    EXPECT_EQ(tracks.size(), width);
  }
  for (const auto& cell : report.cells) {
    EXPECT_EQ(cell.stage, 1);
    EXPECT_EQ(cell.bin_distance, 0);
  }
  // Optional standalone artifact from the real test result, not a drawing of
  // the expected answer. Normal tests write only inside Bazel's output area.
  if (const char* output = std::getenv("TEST_UNDECLARED_OUTPUTS_DIR")) {
    std::ofstream svg(std::string(output) + "/parallel_maze.svg");
    svg << "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 1260 660'>"
           "<rect width='1260' height='660' fill='#0d1726'/>"
           "<style>text{font:18px sans-serif;fill:#eef4ff}</style>";
    const char* colors[] = {"#65d6ff", "#ffbd69", "#c394ff", "#70e0a2", "#ff7895", "#e9e480", "#8caaff", "#fafafa"};
    for (int panel = 0; panel < 2; ++panel) {
      const auto& frame = panel ? f.fp : before;
      svg << "<g transform='translate(" << 20 + 620 * panel << ",45)'>"
          << "<text y='-15'>" << (panel ? "Joint SAT: all connections aligned" : "Input: independently shuffled corner cells")
          << "</text>";
      for (size_t i = 0; i < wall_count; ++i) {
        const auto& p = frame.components[i].place;
        svg << "<rect x='" << p.x / 10. << "' y='" << 600 - (p.y + p.height) / 10. << "' width='" << p.width / 10. << "' height='"
            << p.height / 10. << "' fill='#253247'/>";
      }
      for (int corner = 0; corner < 6; ++corner) {
        svg << "<rect x='" << (base_x[corner] / 10) * 100 << "' y='" << 600 - (base_y[corner] / 10 + 1) * 100
            << "' width='100' height='100' fill='none' stroke='#8291aa' stroke-dasharray='4 4'/>";
      }
      for (unsigned lane = 0; lane < lanes; ++lane) {
        svg << "<polyline fill='none' stroke='" << colors[lane] << "' stroke-width='2' points='";
        for (size_t cell : paths[lane]) {
          const auto& p = frame.components[cell].place;
          svg << (p.x + 50) / 10. << ',' << 600 - (p.y + 50) / 10. << ' ';
        }
        svg << "'/>";
        for (size_t cell : paths[lane]) {
          const auto& p = frame.components[cell].place;
          svg << "<rect x='" << p.x / 10. + 1 << "' y='" << 600 - (p.y + 100) / 10. + 1 << "' width='8' height='8' fill='"
              << colors[lane] << "'/>";
        }
      }
      svg << "</g>";
    }
    svg << "</svg>";
    ASSERT_TRUE(svg.good());
  }
}

}  // namespace
TEST(TerminalLegalize, SaturatedParallelMazeRecoversAllSharedCornerConnections) { check_parallel_maze(4); }
TEST(TerminalLegalize, EightParallelMazePathsSaturateEveryLeg) { check_parallel_maze(8, 8); }
TEST(TerminalLegalize, FifthParallelMazePathExceedsFourTrackCutCapacity) { check_parallel_maze(5); }
}  // namespace livehd::placement

namespace livehd::placement {
TEST(TerminalLegalize, RemainderWarpKeepsCriticalLocksAndOriginalReport) {
  Fixture f(80, 20);
  f.options.bins_x                = 8;
  f.options.bins_y                = 4;
  f.options.remainder_wire_weight = 0;
  f.options.critical_fraction     = 1;
  f.options.critical_pair_limit   = 0;
  const auto a = f.cell(5, 4), b = f.cell(7, 4);
  f.net(a, b, -10);
  for (int y = 0; y < 10; ++y) {
    for (int x = 0; x < 12; ++x) {
      f.cell(10 + x / 3, y);
    }
  }
  const auto original                 = f.fp;
  auto       plain                    = f.fp;
  const auto baseline                 = terminal_legalize(f.tech, plain, f.timing, f.options);
  f.options.remainder_warp.enabled    = true;
  f.options.remainder_warp.bins       = 8;
  f.options.remainder_warp.iterations = 1;
  auto report                         = terminal_legalize(f.tech, f.fp, f.timing, f.options);
  f.check(report);
  ASSERT_EQ(report.warp_status, "applied") << report.warp_reason;
  EXPECT_EQ(report.warp_samples, 120);
  EXPECT_GT(report.warp_max_displacement_dbu, 100);
  EXPECT_EQ(f.fp.components[a].place, plain.components[a].place);
  EXPECT_EQ(f.fp.components[b].place, plain.components[b].place);
  size_t changed = 0;
  for (const auto& c : report.cells) {
    EXPECT_EQ(c.before, original.components[c.index].place);
    changed += c.after != plain.components[c.index].place;
  }
  EXPECT_GT(changed, 0);
  // Disabled/zero-weight field is bit-identical, including legalizer decisions.
  auto zero                       = original;
  f.options.remainder_warp.weight = 0;
  const auto skipped              = terminal_legalize(f.tech, zero, f.timing, f.options);
  EXPECT_EQ(skipped.warp_samples, 0);
  for (size_t i = 0; i < zero.components.size(); ++i) {
    EXPECT_EQ(zero.components[i].place, plain.components[i].place);
  }
}
}  // namespace livehd::placement

namespace livehd::placement {
TEST(TerminalLegalize, SoftChannelAllowsBufferButHardMasksAndSramRemainBlocked) {
  Fixture f(20, 4);
  f.options.bins_x = f.options.bins_y = 1;
  f.options.padding_sites             = 0;
  f.options.critical_fraction         = 0;
  auto& mask                          = f.tech.macros["mask"];
  mask.klass                          = "BLOCK";
  mask.width                          = 8;
  mask.height                         = 4;
  auto& soft                          = f.tech.macros["soft"];
  soft.klass                          = "BLOCK";
  soft.width                          = 4;
  soft.height                         = 4;
  const auto buffer                   = f.cell(9, 1);
  const auto left = f.cell(0, 0, "mask"), middle = f.cell(8, 0, "soft"), right = f.cell(12, 0, "mask");
  for (auto i : {left, middle, right}) {
    f.fp.components[i].place.status = physical::Status::FIXED;
  }
  f.fp.components[left].name   = "__lhd_frame_mask_hard_0";
  f.fp.components[middle].name = "__lhd_frame_mask_soft_0";
  f.fp.components[right].name  = "sram";
  const auto masks             = f.fp.components;
  const auto report            = terminal_legalize(f.tech, f.fp, f.timing, f.options);
  EXPECT_GE(f.fp.components[buffer].place.x, 800);
  EXPECT_LE(f.fp.components[buffer].place.x + 100, 1200);
  EXPECT_DOUBLE_EQ(report.bins[0].capacity, 4 * 4 * 10000);
  for (auto i : {left, middle, right}) {
    EXPECT_EQ(f.fp.components[i], masks[i]);
  }
  auto hard                    = f.fp;
  hard.components[middle].name = "__lhd_frame_mask_hard_1";
  EXPECT_THROW(terminal_legalize(f.tech, hard, f.timing, f.options), std::runtime_error);
}

}  // namespace livehd::placement

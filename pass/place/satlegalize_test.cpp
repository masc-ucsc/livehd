// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "satlegalize.hpp"

#include <algorithm>
#include <stdexcept>

#include "gtest/gtest.h"
namespace livehd::placement {
namespace {
struct Fixture {
  physical::Technology tech;
  physical::Floorplan  fp;
  timing::Feedback     timing;
  Fixture() {
    tech.dbu = fp.dbu  = 100;
    tech.sites["site"] = {1, 2, true};
    auto& layer        = tech.layers["M1"];
    layer.width        = .1;
    layer.pitch_x = layer.pitch_y = 1;
    layer.direction               = "HORIZONTAL";
    layer.has_offset              = true;
    layer.offset_x = layer.offset_y = .5;
    auto& macro                     = tech.macros["cell"];
    macro.width = macro.height = 2;
    macro.site                 = "site";
    macro.klass                = "CORE";
    macro.symmetry_x           = true;
    macro.pins["Y"]            = {"OUTPUT", "SIGNAL", {{"M1", {1.2, .2, 1.8, .8}}}};
    macro.pins["A"]            = {"INPUT", "SIGNAL", {{"M1", {.2, .2, .8, .8}}}};
    fp.die                     = {0, 0, 1400, 600};
    for (int y = 0; y < 600; y += 200) {
      fp.rows.push_back({"r" + std::to_string(y), "site", 0, y, 14, 100, physical::Orientation::N});
    }
    Ann_place a(200, 0, 200, 200), b(600, 200, 200, 200);
    a.status = b.status = physical::Status::PLACED;
    fp.components       = {
        {"driver", "cell", a},
        {  "sink", "cell", b}
    };
    fp.nets = {
        {"n", {{"driver", "Y"}, {"sink", "A"}}}
    };
    timing.pins["sink:A"].slack = -1;
  }
};
}  // namespace
TEST(Satlegalize, TrackAccessOffsetsModelsAndCompleteRollback) {
  for (const auto* backend : {"cadical", "cms-cnf", "cms-xor"}) {
    for (const auto* encoding : {"candidate", "binary"}) {
      Fixture             f;
      const auto          original = f.fp;
      Satlegalize_options options;
      options.backend         = backend;
      options.encoding        = encoding;
      options.max_pairs       = 1;
      options.boundary_growth = 1;
      auto report             = satlegalize(f.tech, f.fp, f.timing, options);
      ASSERT_EQ(report.accepted.size(), 1) << backend << ' ' << encoding;
      EXPECT_TRUE(satlegalize_preserved(f.tech, f.fp, report));
      EXPECT_EQ(f.fp.components[0].place.y, f.fp.components[1].place.y);
      satlegalize_restore(f.fp, report, 1);
      EXPECT_TRUE(satlegalize_preserved(f.tech, f.fp, report));
      f.fp.components[1].place.y += 200;
      EXPECT_FALSE(satlegalize_preserved(f.tech, f.fp, report));
      satlegalize_restore(f.fp, report);
      EXPECT_EQ(f.fp.components, original.components);
      EXPECT_EQ(f.fp.rows, original.rows);
      EXPECT_EQ(f.fp.nets, original.nets);
    }
  }
}
TEST(Satlegalize, MissingGridUnknownAndFixedSpaceFallBack) {
  Fixture             f;
  const auto          original = f.fp;
  Satlegalize_options options;
  f.tech.layers["M1"].has_offset = false;
  auto missing                   = satlegalize(f.tech, f.fp, f.timing, options);
  EXPECT_TRUE(missing.accepted.empty());
  EXPECT_EQ(f.fp.components, original.components);
  f.fp.tracks.push_back({"Y", "M1", 50, 6, 100});
  options.milliseconds = 0;
  auto unknown         = satlegalize(f.tech, f.fp, f.timing, options);
  EXPECT_TRUE(unknown.accepted.empty());
  EXPECT_GT(unknown.unknown, 0);
  EXPECT_EQ(f.fp.components, original.components);
  options.milliseconds = 50;
  for (auto& c : f.fp.components) {
    c.place.status = physical::Status::FIXED;
  }
  const auto fixed     = f.fp.components;
  auto       immutable = satlegalize(f.tech, f.fp, f.timing, options);
  EXPECT_TRUE(immutable.accepted.empty());
  EXPECT_EQ(f.fp.components, fixed);
}
TEST(Satlegalize, SharedClockNetIsProtectedEvenForDataSink) {
  Fixture f;
  f.timing.pins["driver:Y"].clock = true;
  const auto original             = f.fp.components;
  const auto report               = satlegalize(f.tech, f.fp, f.timing, Satlegalize_options{});
  EXPECT_TRUE(report.accepted.empty());
  EXPECT_EQ(f.fp.components, original);
}
TEST(Satlegalize, CompetingPairsKeepAcceptedPrefix) {
  Fixture f;
  f.fp.die = {0, 0, 400, 600};
  for (auto& row : f.fp.rows) {
    row.count = 4;
  }
  f.fp.components[0].place.x = 0;
  f.fp.components[1].place.x = 200;
  auto third                 = f.fp.components[1];
  third.name                 = "third";
  third.place.x              = 0;
  third.place.y              = 400;
  f.fp.components.push_back(third);
  f.fp.nets.front().connections.push_back({"third", "A"});
  f.timing.pins["sink:A"].slack  = -2;
  f.timing.pins["third:A"].slack = -1;
  Satlegalize_options options;
  options.move_rows       = 2;
  options.max_candidates  = 32;
  options.boundary_growth = 1;
  const auto report       = satlegalize(f.tech, f.fp, f.timing, options);
  EXPECT_EQ(report.accepted.size(), 1);
  EXPECT_GT(report.conflicts, 0);
  EXPECT_TRUE(satlegalize_preserved(f.tech, f.fp, report));
}
TEST(Satlegalize, FragmentedWhitespaceCannotFitCells) {
  Fixture f;
  for (int y = 0; y < 600; y += 200) {
    for (int x = 0; x < 1400; x += 200) {
      Ann_place obstacle(x, y, 100, 200);
      obstacle.status = physical::Status::FIXED;
      f.fp.components.push_back({"obstacle" + std::to_string(x) + "_" + std::to_string(y), "cell", obstacle});
    }
  }
  const auto original = f.fp.components;
  const auto report   = satlegalize(f.tech, f.fp, f.timing, Satlegalize_options{});
  EXPECT_TRUE(report.accepted.empty());
  EXPECT_EQ(f.fp.components, original);
}
TEST(Satlegalize, IncidentBoundaryNetsCanRejectAlignment) {
  Fixture f;
  f.fp.components[1].place.x = 400;
  auto& macro                = f.tech.macros.at("cell");
  for (unsigned p = 0; p < 2; ++p) {
    const std::string pin = "boundary" + std::to_string(p);
    macro.pins[pin]       = {"INPUT", "SIGNAL", {{"M1", {.2, .2, .8, .8}}}};
    for (size_t cell = 0; cell < 2; ++cell) {
      const auto&       c        = f.fp.components[cell];
      const std::string terminal = pin + "_" + c.name;
      f.fp.terminals.push_back(
          {terminal, terminal, "INPUT", c.place.x + 50, c.place.y + 50, physical::Orientation::N, physical::Status::FIXED, {}});
      f.fp.nets.push_back({
          terminal,
          {{"PIN", terminal}, {c.name, pin}}
      });
    }
  }
  const auto          original = f.fp.components;
  Satlegalize_options options;
  options.move_sites      = 0;
  options.boundary_growth = 0;
  const auto report       = satlegalize(f.tech, f.fp, f.timing, options);
  EXPECT_TRUE(report.accepted.empty());
  EXPECT_GT(report.boundary_rejected, 0);
  EXPECT_EQ(f.fp.components, original);
}
TEST(Satlegalize, OrientationAndObstructionRevalidation) {
  Fixture f;
  f.fp.components[1].place.y = 0;
  Satlegalize_options options;
  auto                report = satlegalize(f.tech, f.fp, f.timing, options);
  ASSERT_FALSE(report.accepted.empty());
  f.fp.components[1].place.orientation = physical::Orientation::FS;
  EXPECT_FALSE(satlegalize_preserved(f.tech, f.fp, report));
  satlegalize_restore(f.fp, report, report.accepted.size());
  f.tech.macros["cell"].blocked_layers.insert("M1");
  f.tech.macros["cell"].obstructions.push_back({
      "M1",
      {0, 0, 2, 2}
  });
  EXPECT_FALSE(satlegalize_preserved(f.tech, f.fp, report));
}
namespace {
Fixture overflow_fixture() {
  Fixture f;
  f.fp.die = {0, 0, 1200, 400};
  f.fp.rows.resize(2);
  for (auto& row : f.fp.rows) {
    row.count = 12;
  }
  f.fp.components[0].place = Ann_place(0, 0, 200, 200);
  f.fp.components[1].place = Ann_place(200, 200, 200, 200);
  for (auto& cell : f.fp.components) {
    cell.place.status = physical::Status::PLACED;
  }
  for (const auto& [x, y] : {
           std::pair{200,   0},
           std::pair{  0, 200},
           std::pair{100,   0}
  }) {
    const auto name = "filler" + std::to_string(f.fp.components.size());
    Ann_place  place(x, y, 200, 200);
    place.status = physical::Status::PLACED;
    f.fp.components.push_back({name, "cell", place});
    f.fp.nets.push_back({name, {{name, "Y"}}});
    f.timing.pins[name + ":Y"].slack = 1;
  }
  return f;
}
Satlegalize_options spill_options() {
  Satlegalize_options options;
  options.region_width_um = options.region_height_um = 4;
  options.spill_neighbors                            = true;
  options.max_cells                                  = 32;
  options.max_candidates                             = 12;
  options.move_sites                                 = 2;
  options.max_pairs                                  = 1;
  options.boundary_growth                            = 1;
  options.milliseconds                               = 2000;
  return options;
}
void expect_no_overlap(const physical::Floorplan& fp) {
  for (size_t a = 0; a < fp.components.size(); ++a) {
    const auto& x = fp.components[a].place;
    EXPECT_EQ(x.x % 100, 0);
    EXPECT_EQ(x.y % 200, 0);
    for (size_t b = a + 1; b < fp.components.size(); ++b) {
      const auto& y = fp.components[b].place;
      EXPECT_FALSE(x.x < y.x + y.width && y.x < x.x + x.width && x.y < y.y + y.height && y.y < x.y + x.height);
    }
  }
}
}  // namespace
TEST(Satlegalize, FixedRegionSpillsOverflowAndRestoresWholeCheckpoint) {
  auto       f            = overflow_fixture();
  auto       options      = spill_options();
  const auto original     = f.fp;
  options.spill_neighbors = false;
  const auto confined     = satlegalize(f.tech, f.fp, f.timing, options);
  EXPECT_TRUE(confined.accepted.empty());
  EXPECT_EQ(f.fp.components, original.components);
  options.spill_neighbors = true;
  const auto report       = satlegalize(f.tech, f.fp, f.timing, options);
  ASSERT_EQ(report.accepted.size(), 1);
  ASSERT_EQ(report.window_stats.size(), 1);
  EXPECT_EQ(report.sampled_windows, 1);
  EXPECT_EQ(report.window_stats[0].movable_cells, 5);
  EXPECT_DOUBLE_EQ(report.window_stats[0].usable_area, 160000);
  EXPECT_DOUBLE_EQ(report.window_stats[0].density_before, 1.25);
  EXPECT_LT(report.window_stats[0].density_after, report.window_stats[0].density_before);
  EXPECT_GT(report.spill_candidates, 0);
  EXPECT_GT(report.spilled_cells, 0);
  EXPECT_TRUE(satlegalize_preserved(f.tech, f.fp, report));
  EXPECT_LT(f.fp.components[0].place.x, 400);
  EXPECT_LT(f.fp.components[1].place.x, 400);
  expect_no_overlap(f.fp);
  const auto accepted = f.fp.components;
  satlegalize_restore(f.fp, report);
  EXPECT_EQ(f.fp.components, original.components);
  satlegalize_restore(f.fp, report, 1);
  EXPECT_EQ(f.fp.components, accepted);
  EXPECT_EQ(f.fp.rows, original.rows);
  EXPECT_EQ(f.fp.nets, original.nets);
}
TEST(Satlegalize, SpillRequiresCompletePositiveNonclockTimingInPicoseconds) {
  for (unsigned guard = 0; guard < 4; ++guard) {
    auto f       = overflow_fixture();
    auto options = spill_options();
    for (size_t i = 2; i < f.fp.components.size(); ++i) {
      const auto pin = f.fp.components[i].name + ":Y";
      if (guard == 0) {
        f.timing.pins.erase(pin);
      } else if (guard == 1) {
        f.timing.pins[pin].clock = true;
      } else if (guard == 2) {
        options.spill_slack_margin_ps = 1001;
      } else {
        f.timing.time_unit_ps = std::numeric_limits<double>::quiet_NaN();
      }
    }
    const auto before = f.fp.components;
    const auto report = satlegalize(f.tech, f.fp, f.timing, options);
    EXPECT_TRUE(report.accepted.empty()) << guard;
    EXPECT_EQ(report.spill_candidates, 0) << guard;
    EXPECT_EQ(f.fp.components, before) << guard;
  }
}
TEST(Satlegalize, RecipientAggregateDensityRejectsWholeModel) {
  auto       f    = overflow_fixture();
  const auto name = "another";
  Ann_place  place(100, 200, 200, 200);
  place.status = physical::Status::PLACED;
  f.fp.components.push_back({name, "cell", place});
  f.fp.nets.push_back({name, {{name, "Y"}}});
  f.timing.pins[std::string(name) + ":Y"].slack = 1;
  // Existing GP cells outside the region are immutable, including overlaps.
  // They occupy one geometric slot but count all three footprints in density.
  for (unsigned i = 0; i < 3; ++i) {
    place.x = 600;
    place.y = 0;
    f.fp.components.push_back({"outside" + std::to_string(i), "cell", place});
  }
  const auto original = f.fp.components;
  const auto report   = satlegalize(f.tech, f.fp, f.timing, spill_options());
  EXPECT_TRUE(report.accepted.empty());
  EXPECT_GT(report.density_rejected, 0);
  EXPECT_EQ(report.spilled_cells, 0);
  EXPECT_EQ(f.fp.components, original);
}
TEST(Satlegalize, CapacityUnionsDuplicateRowsAndOverlappingMacroKeepouts) {
  Fixture f;
  f.fp.rows.push_back(f.fp.rows.front());
  auto& block = f.tech.macros["block"];
  block.width = block.height = 2;
  block.klass                = "BLOCK";
  Ann_place fixed(0, 0, 200, 200);
  fixed.status = physical::Status::FIXED;
  f.fp.components.push_back({"fixed0", "cell", fixed});
  fixed.x = 100;
  f.fp.components.push_back({"fixed1", "cell", fixed});
  fixed.x = 200;
  f.fp.components.push_back({"macro", "block", fixed});
  auto options             = spill_options();
  options.region_width_um  = 14;
  options.region_height_um = 6;
  options.max_cells        = 1;
  options.macro_halo_x_dbu = 50;
  const auto original      = f.fp.components;
  const auto report        = satlegalize(f.tech, f.fp, f.timing, options);
  ASSERT_EQ(report.window_stats.size(), 1);
  EXPECT_EQ(report.window_stats[0].movable_cells, 2);
  EXPECT_TRUE(report.window_stats[0].oversized);
  EXPECT_DOUBLE_EQ(report.window_stats[0].usable_area, 750000);
  EXPECT_EQ(f.fp.components, original);
}
TEST(Satlegalize, FortyMicronRegionReportsEveryCellWithoutShrinking) {
  Fixture f;
  f.fp.die = {0, 0, 8000, 4000};
  for (size_t i = 0; i < 7; ++i) {
    auto cell    = f.fp.components.front();
    cell.name    = "extra" + std::to_string(i);
    cell.place.x = 100 + 200 * i;
    f.fp.components.push_back(cell);
  }
  auto options            = spill_options();
  options.region_width_um = options.region_height_um = 40;
  options.max_cells                                  = 2;
  const auto original                                = f.fp.components;
  const auto report                                  = satlegalize(f.tech, f.fp, f.timing, options);
  ASSERT_EQ(report.window_stats.size(), 1);
  EXPECT_EQ(report.window_stats[0].movable_cells, 9);
  EXPECT_EQ(report.oversized, 1);
  EXPECT_DOUBLE_EQ(report.window_stats[0].bounds.xh - report.window_stats[0].bounds.xl, 4000);
  EXPECT_DOUBLE_EQ(report.window_stats[0].bounds.yh - report.window_stats[0].bounds.yl, 4000);
  EXPECT_EQ(f.fp.components, original);
}
TEST(Satlegalize, RegionRejectsGappedRowsRatherThanCountingHolesAsCapacity) {
  Fixture f;
  f.fp.rows[0].step = 200;
  auto options      = spill_options();
  EXPECT_THROW(satlegalize(f.tech, f.fp, f.timing, options), std::invalid_argument);
  options.region_width_um = options.region_height_um = 0;
  options.spill_neighbors                            = false;
  EXPECT_NO_THROW(satlegalize(f.tech, f.fp, f.timing, options));
}
TEST(Satlegalize, ExplicitZeroRegionPreservesLegacyChoiceOrder) {
  for (const auto* encoding : {"candidate", "binary"}) {
    Fixture             f, g;
    Satlegalize_options options;
    options.max_pairs       = 1;
    options.boundary_growth = 1;
    options.encoding        = encoding;
    const auto baseline     = satlegalize(f.tech, f.fp, f.timing, options);
    options.region_width_um = options.region_height_um = 0;
    options.spill_neighbors                            = false;
    options.spill_slack_margin_ps                      = 20;
    options.macro_halo_x_dbu = options.macro_halo_y_dbu = 100;
    const auto repeat                                   = satlegalize(g.tech, g.fp, g.timing, options);
    EXPECT_EQ(f.fp.components, g.fp.components);
    EXPECT_EQ(baseline.accepted.size(), repeat.accepted.size());
    EXPECT_EQ(repeat.spill_candidates, 0);
    EXPECT_EQ(repeat.sampled_windows, 0);
  }
}
TEST(Satlegalize, MovableObstructionUsesChosenPoseInsteadOfFrozenGpPose) {
  Fixture f;
  f.fp.die = {0, 0, 2400, 200};
  f.fp.rows.resize(1);
  f.fp.rows[0].count         = 24;
  f.fp.components[0].place.x = 0;
  f.fp.components[1].place.x = 600;
  f.fp.components[1].place.y = 0;
  auto blocker               = f.tech.macros.at("cell");
  blocker.width              = 4;
  blocker.blocked_layers.insert("M1");
  blocker.conservative_obstructions.insert("M1");
  f.tech.macros["blocker"] = blocker;
  Ann_place place(200, 0, 400, 200);
  place.status = physical::Status::PLACED;
  f.fp.components.push_back({"blocker", "blocker", place});
  f.fp.nets.push_back({"blocker_data", {{"blocker", "Y"}}});
  f.timing.pins["blocker:Y"].slack = 1;
  auto options                     = spill_options();
  options.region_width_um          = 8;
  options.region_height_um         = 2;
  options.move_sites = options.move_rows = 0;
  options.max_candidates                 = 2;
  const auto before                      = f.fp.components;
  const auto report                      = satlegalize(f.tech, f.fp, f.timing, options);
  ASSERT_EQ(report.accepted.size(), 1);
  EXPECT_EQ(f.fp.components[0].place, before[0].place);
  EXPECT_EQ(f.fp.components[1].place, before[1].place);
  EXPECT_GE(f.fp.components[2].place.x, 800);
  EXPECT_EQ(report.window_stats[0].movable_cells, 3);
  EXPECT_EQ(report.spilled_cells, 1);
  EXPECT_TRUE(satlegalize_preserved(f.tech, f.fp, report));
  expect_no_overlap(f.fp);
}
TEST(Satlegalize, OffsiteBoundaryStraddlerGetsLegalLocalChoicesWithSameOwner) {
  Fixture   f;
  Ann_place straddler(750, 0, 200, 200);
  straddler.status = physical::Status::PLACED;
  f.fp.components.push_back({"straddler", "cell", straddler});
  auto options             = spill_options();
  options.region_width_um  = 8;
  options.region_height_um = 4;
  options.move_sites       = 1;
  const auto report        = satlegalize(f.tech, f.fp, f.timing, options);
  ASSERT_EQ(report.accepted.size(), 1);
  EXPECT_EQ(report.window_stats[0].movable_cells, 3);
  EXPECT_EQ(report.spill_candidates, 0);  // Missing timing permits no escape.
  const auto& chosen = f.fp.components[2].place;
  EXPECT_NE(chosen.x, straddler.x);
  EXPECT_EQ(chosen.x % 100, 0);
  EXPECT_EQ(chosen.y % 200, 0);
  EXPECT_GE(chosen.x + chosen.width / 2, 800);
  EXPECT_LT(chosen.x + chosen.width / 2, 1400);
  EXPECT_LE(std::abs(chosen.x - straddler.x), 150);
  EXPECT_TRUE(satlegalize_preserved(f.tech, f.fp, report));
  expect_no_overlap(f.fp);
}
TEST(Satlegalize, RecipientFreeGapsRespectResolvedMacroHalo) {
  for (const int64_t halo : {0, 100}) {
    auto  f      = overflow_fixture();
    auto& macro  = f.tech.macros["macro"];
    macro.klass  = "BLOCK";
    macro.width  = 2;
    macro.height = 4;
    Ann_place obstacle(600, 0, 200, 400);
    obstacle.status = physical::Status::FIXED;
    f.fp.components.push_back({"fixed_macro", "macro", obstacle});
    auto options             = spill_options();
    options.macro_halo_x_dbu = halo;
    const auto original      = f.fp.components;
    const auto report        = satlegalize(f.tech, f.fp, f.timing, options);
    if (!halo) {
      EXPECT_EQ(report.accepted.size(), 1);
    } else {
      EXPECT_TRUE(report.accepted.empty());
      EXPECT_EQ(report.spill_candidates, 0);
      EXPECT_EQ(f.fp.components, original);
    }
    EXPECT_EQ(f.fp.components.back(), original.back());
  }
}
TEST(Satlegalize, FixedRegionChoosesDensestAmongBoundedTimingSeeds) {
  Fixture f;
  f.fp.die.xh = 1600;
  for (auto& row : f.fp.rows) {
    row.count = 16;
  }
  auto a = f.fp.components[0], b = f.fp.components[1];
  a.name    = "other_driver";
  b.name    = "other_sink";
  a.place.x = 1000;
  b.place.x = 1400;
  f.fp.components.push_back(a);
  f.fp.components.push_back(b);
  f.fp.nets.push_back({
      "other",
      {{a.name, "Y"}, {b.name, "A"}}
  });
  f.timing.pins["sink:A"].slack      = -10;
  f.timing.pins[b.name + ":A"].slack = -1;
  for (unsigned i = 0; i < 3; ++i) {
    a.name    = "dense" + std::to_string(i);
    a.place.x = 1000 + 100 * i;
    a.place.y = 200;
    f.fp.components.push_back(a);
  }
  auto options             = spill_options();
  options.region_width_um  = 8;
  options.region_height_um = 6;
  options.max_pairs        = 2;
  options.max_windows      = 1;
  const auto report        = satlegalize(f.tech, f.fp, f.timing, options);
  EXPECT_EQ(report.sampled_windows, 2);
  ASSERT_EQ(report.window_stats.size(), 1);
  EXPECT_DOUBLE_EQ(report.window_stats[0].bounds.xl, 800);
  EXPECT_EQ(report.window_stats[0].movable_cells, 5);
}
TEST(Satlegalize, UsedRecipientMustBeLegalEvenWhenItsAggregateLoadFalls) {
  Fixture f;
  f.fp.die = {0, 0, 800, 1200};
  f.fp.rows.clear();
  for (int y = 0; y < 1200; y += 200) {
    f.fp.rows.push_back({"row" + std::to_string(y), "site", 0, y, 8, 100, physical::Orientation::N});
  }
  auto small             = f.tech.macros.at("cell");
  small.width            = 1;
  small.pins["Y"].shapes = {
      {"M1", {.6, .2, .9, .8}}
  };
  small.pins["A"].shapes = {
      {"M1", {.1, .2, .4, .8}}
  };
  f.tech.macros["small"] = small;
  auto large             = f.tech.macros.at("cell");
  large.width            = 3;
  f.tech.macros["large"] = large;
  f.fp.components.clear();
  const auto append = [&](const std::string& name,
                          const std::string& master,
                          int                x,
                          int                y,
                          int                width,
                          int                height,
                          physical::Status   status = physical::Status::PLACED) {
    Ann_place place(x, y, width, height);
    place.status = status;
    f.fp.components.push_back({name, master, place});
  };
  append("driver", "small", 0, 0, 100, 200);
  append("sink", "small", 100, 0, 100, 200);
  append("fixed", "small", 200, 0, 100, 200, physical::Status::FIXED);
  for (int y = 0; y < 600; y += 200) {
    // Offsite original center belongs to the right tile. The only local
    // grid choice at x=300 moves 9800DBU² back into the donor per straddler.
    append("straddler" + std::to_string(y), "cell", 349, y, 200, 200);
    // Immutable overlapping GP occupants are counted separately in load.
    append("outside0_" + std::to_string(y), "cell", 600, y, 200, 200);
    append("outside1_" + std::to_string(y), "cell", 600, y, 200, 200);
  }
  for (unsigned i = 0; i < 5; ++i) {
    const auto name = "large" + std::to_string(i);
    append(name, "large", i < 2 ? 0 : 100, i % 2 ? 400 : 200, 300, 200);
    f.fp.nets.push_back({name, {{name, "Y"}}});
    f.timing.pins[name + ":Y"].slack = 1;
  }
  append("escape", "small", 200, 200, 100, 200);
  f.fp.nets.push_back({"escape", {{"escape", "Y"}}});
  f.timing.pins["escape:Y"].slack = 1;
  physical::Macro block;
  block.klass             = "BLOCK";
  block.width             = 1;
  block.height            = 6;
  f.tech.macros["strip"]  = block;
  block.width             = 4;
  f.tech.macros["corner"] = block;
  append("top_strip", "strip", 300, 600, 100, 600, physical::Status::FIXED);
  append("top_right", "corner", 400, 600, 400, 600, physical::Status::FIXED);
  auto options             = spill_options();
  options.region_width_um  = 4;
  options.region_height_um = 6;
  options.move_sites = options.move_rows = 0;
  options.max_candidates                 = 24;
  const auto original                    = f.fp.components;
  const auto report                      = satlegalize(f.tech, f.fp, f.timing, options);
  // Five 300-wide cells fill exactly two donor slots and three top slots.
  // The small escape is forced into the right tile. Three straddlers reduce
  // its load329400→320000, but capacity240000 is still exceeded. Merely
  // checking recipients with increasing net load would accept this model.
  EXPECT_TRUE(report.accepted.empty());
  EXPECT_GT(report.density_rejected, 0);
  EXPECT_EQ(report.spilled_cells, 0);
  EXPECT_EQ(f.fp.components, original);
}
TEST(Satlegalize, CandidateFailureDiagnosticCountsWholeWindowAndProtectedEndpoint) {
  auto            f = overflow_fixture();
  physical::Macro block;
  block.klass = "BLOCK";
  block.width = block.height   = 2;
  f.tech.macros["fixed_macro"] = block;
  Ann_place obstacle(0, 0, 200, 200);
  obstacle.status = physical::Status::FIXED;
  f.fp.components.push_back({"obstacle", "fixed_macro", obstacle});
  f.timing.pins["driver:Y"].slack = -2;
  auto options                    = spill_options();
  options.move_sites = options.move_rows = 0;
  const auto original                    = f.fp.components;
  const auto report                      = satlegalize(f.tech, f.fp, f.timing, options);
  ASSERT_EQ(report.window_stats.size(), 1);
  const auto& window = report.window_stats.front();
  EXPECT_EQ(window.movable_cells, 5);
  EXPECT_EQ(window.candidate_cells, 1);
  EXPECT_EQ(window.retained_candidates, 0);
  EXPECT_EQ(window.spill_eligible_cells, 3);
  EXPECT_EQ(window.first_no_candidate_cell, "driver");
  EXPECT_EQ(window.first_no_candidate_master, "cell");
  EXPECT_EQ(window.first_no_candidate_place, original.front().place);
  EXPECT_EQ(window.first_no_candidate_spill_reason, "protected alignment endpoint");
  ASSERT_TRUE(window.first_no_candidate_slack_ps);
  EXPECT_DOUBLE_EQ(*window.first_no_candidate_slack_ps, -2000);
  EXPECT_EQ(report.trials, 0);
  EXPECT_EQ(f.fp.components, original);
}
TEST(Satlegalize, CandidateFailureDiagnosticExplainsSpillTimingEligibility) {
  for (unsigned reason = 0; reason < 4; ++reason) {
    auto f      = overflow_fixture();
    f.fp.die.xh = 400;
    for (auto& row : f.fp.rows) {
      row.count = 4;
    }
    std::rotate(f.fp.components.begin(), f.fp.components.begin() + 2, f.fp.components.begin() + 3);
    const auto      name = f.fp.components.front().name;
    const auto      pin  = name + ":Y";
    physical::Macro block;
    block.klass = "BLOCK";
    block.width = block.height   = 2;
    f.tech.macros["fixed_macro"] = block;
    Ann_place obstacle(200, 0, 200, 200);
    obstacle.status = physical::Status::FIXED;
    f.fp.components.push_back({"obstacle", "fixed_macro", obstacle});
    auto options       = spill_options();
    options.move_sites = options.move_rows = 0;
    options.spill_slack_margin_ps          = 20;
    if (reason == 0) {
      f.timing.pins.erase(pin);
    } else if (reason == 1) {
      f.timing.pins[pin].clock = true;
    } else if (reason == 2) {
      f.timing.pins[pin].slack = .01;
    }
    const auto original = f.fp.components;
    const auto report   = satlegalize(f.tech, f.fp, f.timing, options);
    ASSERT_EQ(report.window_stats.size(), 1);
    const auto& window = report.window_stats.front();
    EXPECT_EQ(window.candidate_cells, 1);
    EXPECT_EQ(window.retained_candidates, 0);
    EXPECT_EQ(window.spill_eligible_cells, reason == 3 ? 3 : 2);
    EXPECT_EQ(window.first_no_candidate_cell, name);
    EXPECT_EQ(window.first_no_candidate_spill_reason,
              reason == 0   ? "missing-or-nonfinite timing"
              : reason == 1 ? "clock"
              : reason == 2 ? "slack margin"
                            : "eligible");
    if (reason == 2) {
      ASSERT_TRUE(window.first_no_candidate_slack_ps);
      EXPECT_DOUBLE_EQ(*window.first_no_candidate_slack_ps, 10);
    }
    EXPECT_EQ(report.trials, 0);
    EXPECT_EQ(f.fp.components, original);
  }
}
namespace {
std::shared_ptr<Satlegalize_route_grid> route_grid(physical::Rect bounds, unsigned x, unsigned y) {
  auto result    = std::make_shared<Satlegalize_route_grid>();
  result->bounds = bounds;
  result->bins_x = x;
  result->bins_y = y;
  result->horizontal.assign(size_t(x) * y, 0);
  result->vertical.assign(size_t(x) * y, 0);
  result->pin_utilization.assign(size_t(x) * y, 0);
  return result;
}
Satlegalize_options grid_options(const std::shared_ptr<Satlegalize_route_grid>& grid) {
  auto options            = spill_options();
  options.region_width_um = options.region_height_um = 0;
  options.route_grid                                 = true;
  options.routing                                    = grid;
  return options;
}
}  // namespace
TEST(Satlegalize, RoutingGridUsesSharedRoundedRectangularCoreBoundariesAndFullMembership) {
  Fixture f;
  f.fp.die = {100, 100, 1710, 710};
  f.fp.rows.clear();
  for (int y = 100; y < 700; y += 200) {
    f.fp.rows.push_back({"row" + std::to_string(y), "site", 100, y, 16, 100, physical::Orientation::N});
  }
  f.fp.components[0].place.x = 200;
  f.fp.components[0].place.y = 100;
  f.fp.components[1].place.x = 400;
  f.fp.components[1].place.y = 100;
  auto straddler             = f.fp.components[0];
  straddler.name             = "straddler";
  straddler.place.x          = 630;
  f.fp.components.push_back(straddler);
  auto grid           = route_grid({100, 100, 1703, 701}, 3, 2);
  grid->horizontal[0] = 3;
  grid->vertical[2]   = 1;
  const auto original = grid->horizontal;
  const auto report   = satlegalize(f.tech, f.fp, f.timing, grid_options(grid));
  ASSERT_EQ(report.window_stats.size(), 1);
  const auto& window = report.window_stats.front();
  EXPECT_DOUBLE_EQ(window.bounds.xl, 100);
  EXPECT_DOUBLE_EQ(window.bounds.yl, 100);
  EXPECT_DOUBLE_EQ(window.bounds.xh, 634);
  EXPECT_DOUBLE_EQ(window.bounds.yh, 401);
  EXPECT_EQ(window.movable_cells, 3);  // Includes four DBU of the straddler.
  EXPECT_DOUBLE_EQ(window.route_utilization, 3);
  const auto right = std::find_if(window.neighbors.begin(), window.neighbors.end(), [](const auto& tile) {
    return tile.bounds.xl == 634 && tile.bounds.yl == 100;
  });
  ASSERT_NE(right, window.neighbors.end());
  EXPECT_DOUBLE_EQ(right->bounds.xh, 1169);  // Adjacent widths are534 then535.
  EXPECT_DOUBLE_EQ(right->bounds.yh, 401);
  EXPECT_DOUBLE_EQ(right->route_utilization, 1);
  EXPECT_EQ(grid->horizontal, original);
}
TEST(Satlegalize, RoutingGridRejectsInvalidOrAmbiguousSnapshots) {
  Fixture f;
  auto    valid   = route_grid(f.fp.die, 2, 1);
  auto    options = grid_options(valid);
  options.routing.reset();
  EXPECT_THROW(satlegalize(f.tech, f.fp, f.timing, options), std::invalid_argument);
  for (unsigned invalid = 0; invalid < 6; ++invalid) {
    auto grid = std::make_shared<Satlegalize_route_grid>(*valid);
    options   = grid_options(grid);
    if (invalid == 0) {
      grid->vertical.pop_back();
    } else if (invalid == 1) {
      grid->horizontal[0] = std::numeric_limits<double>::quiet_NaN();
    } else if (invalid == 2) {
      grid->pin_utilization[0] = -1;
    } else if (invalid == 3) {
      grid->bounds.xh = grid->bounds.xl;
    } else if (invalid == 4) {
      grid->bounds.xh = 1;
    } else {
      options.region_width_um = options.region_height_um = 40;
    }
    EXPECT_THROW(satlegalize(f.tech, f.fp, f.timing, options), std::invalid_argument) << invalid;
  }
}
TEST(Satlegalize, RoutingGuidancePrioritizesHottestMapBinWithMatchedDensityControl) {
  for (bool guidance : {false, true}) {
    Fixture f;
    f.fp.die.xh = 1600;
    for (auto& row : f.fp.rows) {
      row.count = 16;
    }
    auto a = f.fp.components[0], b = f.fp.components[1];
    a.name    = "other_driver";
    b.name    = "other_sink";
    a.place.x = 1000;
    b.place.x = 1400;
    f.fp.components.push_back(a);
    f.fp.components.push_back(b);
    f.fp.nets.push_back({
        "other",
        {{a.name, "Y"}, {b.name, "A"}}
    });
    f.timing.pins[b.name + ":A"].slack = -1;
    for (unsigned i = 0; i < 3; ++i) {
      a.name    = "dense" + std::to_string(i);
      a.place.x = 1000 + 100 * i;
      a.place.y = 200;
      f.fp.components.push_back(a);
    }
    auto grid              = route_grid(f.fp.die, 2, 1);
    grid->pin_utilization  = {2, .1};
    auto options           = grid_options(grid);
    options.route_guidance = guidance;
    options.max_pairs      = 2;
    options.max_windows    = 1;
    const auto report      = satlegalize(f.tech, f.fp, f.timing, options);
    ASSERT_EQ(report.window_stats.size(), 1);
    EXPECT_DOUBLE_EQ(report.window_stats[0].bounds.xl, guidance ? 0 : 800);
    EXPECT_DOUBLE_EQ(report.window_stats[0].route_utilization, guidance ? 2 : .1);
    EXPECT_EQ(report.sampled_windows, 2);
  }
}
TEST(Satlegalize, RoutingGuidedSpillRequiresLessCongestedNeighbor) {
  for (const bool lower : {false, true}) {
    auto f              = overflow_fixture();
    auto grid           = route_grid(f.fp.die, 3, 1);
    grid->horizontal    = {2, lower ? 1. : 3., 0};
    auto       options  = grid_options(grid);
    const auto original = f.fp.components;
    const auto report   = satlegalize(f.tech, f.fp, f.timing, options);
    if (lower) {
      EXPECT_EQ(report.accepted.size(), 1);
      EXPECT_GT(report.spilled_cells, 0);
    } else {
      EXPECT_TRUE(report.accepted.empty());
      EXPECT_EQ(report.spill_candidates, 0);
      EXPECT_EQ(f.fp.components, original);
    }
  }
  auto f                 = overflow_fixture();
  auto grid              = route_grid(f.fp.die, 3, 1);
  grid->horizontal       = {2, 3, 0};
  auto options           = grid_options(grid);
  options.route_guidance = false;
  EXPECT_EQ(satlegalize(f.tech, f.fp, f.timing, options).accepted.size(), 1);
}
TEST(Satlegalize, RoutingGridGapMenuMovesCriticalCellsWithinSourcePastMacroBody) {
  Fixture f;
  f.fp.die = {0, 0, 4000, 200};
  f.fp.rows.resize(1);
  f.fp.rows[0].count         = 40;
  f.fp.components[0].place.x = 1000;
  f.fp.components[1].place.x = 2000;
  f.fp.components[1].place.y = 0;
  physical::Macro macro;
  macro.klass  = "BLOCK";
  macro.width  = 32;
  macro.height = 2;
  macro.blocked_layers.insert("M1");
  macro.conservative_obstructions.insert("M1");
  f.tech.macros["macro"] = macro;
  Ann_place block(400, 0, 3200, 200);
  block.status = physical::Status::FIXED;
  f.fp.components.push_back({"macro", "macro", block});
  auto grid           = route_grid(f.fp.die, 1, 1);
  grid->horizontal[0] = 2;
  auto options        = grid_options(grid);
  options.move_sites = options.move_rows = 0;
  const auto original                    = f.fp.components;
  const auto report                      = satlegalize(f.tech, f.fp, f.timing, options);
  ASSERT_EQ(report.accepted.size(), 1);
  ASSERT_EQ(report.window_stats.size(), 1);
  EXPECT_EQ(report.window_stats[0].source_gap_cells, 2);
  EXPECT_GT(report.window_stats[0].source_gap_candidates, 0);
  EXPECT_EQ(report.spill_candidates, 0);  // Critical endpoints never escape.
  EXPECT_EQ(f.fp.components.back(), original.back());
  for (size_t i = 0; i < 2; ++i) {
    const auto& p = f.fp.components[i].place;
    EXPECT_GE(p.x, 0);
    EXPECT_LE(p.x + p.width, 4000);
    EXPECT_TRUE(p.x + p.width <= 400 || p.x >= 3600);
  }
  EXPECT_TRUE(satlegalize_preserved(f.tech, f.fp, report));
  expect_no_overlap(f.fp);
}
TEST(Satlegalize, RoutingGridCountsCrossBinPairsWithoutShrinkingRegions) {
  Fixture f;
  f.fp.components[1].place.x = 1000;
  auto       grid            = route_grid(f.fp.die, 2, 1);
  auto       options         = grid_options(grid);
  const auto original        = f.fp.components;
  const auto report          = satlegalize(f.tech, f.fp, f.timing, options);
  EXPECT_EQ(report.cross_grid_pairs, 1);
  EXPECT_EQ(report.sampled_windows, 0);
  EXPECT_EQ(report.trials, 0);
  EXPECT_TRUE(report.window_stats.empty());
  EXPECT_EQ(f.fp.components, original);
}
TEST(Satlegalize, PerWindowTrialCapReachesAnotherCongestionBin) {
  for (unsigned per_window : {0, 1}) {
    Fixture f;
    f.fp.die = {0, 0, 1600, 200};
    f.fp.rows.resize(1);
    f.fp.rows[0].count         = 16;
    f.fp.components[0].place.x = 0;
    f.fp.components[1].place.x = 200;
    f.fp.components[1].place.y = 0;
    auto third                 = f.fp.components[1];
    third.name                 = "third";
    third.place.x              = 400;
    f.fp.components.push_back(third);
    f.fp.nets.front().connections.push_back({third.name, "A"});
    auto driver = f.fp.components[0], sink = f.fp.components[1];
    driver.name    = "other_driver";
    sink.name      = "other_sink";
    driver.place.x = 800;
    sink.place.x   = 1000;
    f.fp.components.push_back(driver);
    f.fp.components.push_back(sink);
    f.fp.nets.push_back({
        "other",
        {{driver.name, "Y"}, {sink.name, "A"}}
    });
    f.timing.pins["sink:A"].slack       = -3;
    f.timing.pins["third:A"].slack      = -2;
    f.timing.pins["other_sink:A"].slack = -1;
    auto grid                           = route_grid(f.fp.die, 2, 1);
    grid->horizontal                    = {2, 1};
    auto options                        = grid_options(grid);
    options.spill_neighbors             = false;
    options.move_sites = options.move_rows = 0;
    options.max_candidates                 = 1;
    options.max_windows = options.max_pairs = 2;
    options.max_pairs_per_window            = per_window;
    const auto report                       = satlegalize(f.tech, f.fp, f.timing, options);
    EXPECT_EQ(report.trials, 2);
    ASSERT_EQ(report.accepted.size(), 2);
    ASSERT_EQ(report.window_stats.size(), per_window ? 2 : 1);
    EXPECT_EQ(report.window_stats.front().trials, per_window ? 1 : 2);
    EXPECT_EQ(report.accepted.back().first, per_window ? 3 : 0);
    EXPECT_TRUE(satlegalize_preserved(f.tech, f.fp, report));
    expect_no_overlap(f.fp);
  }
}
TEST(Satlegalize, PackingProbeSeparatesMenuInfeasibilityFromAlignmentAndTimeout) {
  for (const auto* backend : {"cadical", "cms-cnf", "cms-xor"}) {
    for (unsigned scenario : {0, 1, 2}) {
      Fixture f;
      auto    options         = grid_options(route_grid(f.fp.die, 1, 1));
      options.backend         = backend;
      options.check_packing   = true;
      options.spill_neighbors = false;
      options.move_sites = options.move_rows = 0;
      options.max_candidates                 = 1;
      if (scenario == 0) {
        f.fp.components[1].place = f.fp.components[0].place;
      } else if (scenario == 2) {
        options.milliseconds = 0;
      }
      const auto original = f.fp.components;
      const auto report   = satlegalize(f.tech, f.fp, f.timing, options);
      ASSERT_EQ(report.window_stats.size(), 1);
      EXPECT_EQ(report.window_stats.front().packing_status, scenario == 0 ? "unsat" : scenario == 1 ? "sat" : "unknown");
      EXPECT_EQ(report.trials, scenario == 1 ? 1 : 0);
      EXPECT_EQ(report.conflicts, scenario == 1 ? 1 : 0);
      EXPECT_EQ(report.unknown, 0);  // Probe exhaustion is not an alignment timeout.
      EXPECT_TRUE(report.accepted.empty());
      EXPECT_EQ(f.fp.components, original);
    }
  }
}
TEST(Satlegalize, RoutingGridRejectsSecondaryCrossBinPairButKeepsBoundaryMembership) {
  Fixture f;
  f.fp.die = {0, 0, 1600, 200};
  f.fp.rows.resize(1);
  f.fp.rows[0].count         = 16;
  f.fp.components[0].place.x = 0;
  f.fp.components[1].place.x = 200;
  f.fp.components[1].place.y = 0;
  auto driver = f.fp.components[0], sink = f.fp.components[1];
  driver.name    = "other_driver";
  sink.name      = "other_sink";
  driver.place.x = 400;
  sink.place.x   = 700;
  f.fp.components.push_back(driver);
  f.fp.components.push_back(sink);
  f.fp.nets.push_back({
      "other",
      {{driver.name, "Y"}, {sink.name, "A"}}
  });
  f.timing.pins["sink:A"].slack         = -2;
  f.timing.pins[sink.name + ":A"].slack = -1;
  auto obstacle_master                  = f.tech.macros.at("cell");
  obstacle_master.width                 = 1;
  obstacle_master.pins.clear();
  f.tech.macros["obstacle"] = obstacle_master;
  Ann_place obstacle(600, 0, 100, 200);
  obstacle.status = physical::Status::FIXED;
  f.fp.components.push_back({"obstacle", "obstacle", obstacle});
  auto options            = grid_options(route_grid(f.fp.die, 2, 1));
  options.spill_neighbors = false;
  options.move_sites = options.move_rows = 0;
  options.max_candidates                 = 1;
  options.max_pairs                      = 2;
  options.max_windows                    = 1;
  const auto original                    = f.fp.components;
  const auto report                      = satlegalize(f.tech, f.fp, f.timing, options);
  ASSERT_EQ(report.window_stats.size(), 1);
  EXPECT_EQ(report.window_stats[0].movable_cells, 4);
  EXPECT_EQ(report.window_stats[0].candidate_cells, 4);
  EXPECT_EQ(report.cross_grid_pairs, 1);
  EXPECT_EQ(report.trials, 1);
  ASSERT_EQ(report.accepted.size(), 1);
  EXPECT_EQ(report.accepted[0].first, 0);
  EXPECT_EQ(report.accepted[0].second, 1);
  EXPECT_EQ(f.fp.components, original);
  EXPECT_TRUE(satlegalize_preserved(f.tech, f.fp, report));
  expect_no_overlap(f.fp);
}
}  // namespace livehd::placement

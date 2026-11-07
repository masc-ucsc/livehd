// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "macro_dead_space.hpp"

#include <stdexcept>

#include "gtest/gtest.h"

namespace pl = livehd::placement;

namespace {
// A 1000 x 1000 core on a 10 DBU raster: channel 20, notches below 100 wide.
pl::Dead_space_limits core() {
  pl::Dead_space_limits l;
  l.core_xh = l.core_yh = 1000;
  l.pitch               = 10;
  l.channel_x = l.channel_y = 20;
  l.narrow_x = l.narrow_y = 100;
  l.pocket_fraction       = 0.1;
  return l;
}
pl::Dead_space_rect   rect(int64_t x, int64_t y, int64_t w, int64_t h) { return {x, y, x + w, y + h}; }
pl::Dead_space_object macro(int64_t x, int64_t y, int64_t w, int64_t h, bool movable = true) {
  return {x, y, x + w, y + h, movable, true, 0};
}
pl::Dead_space_collapse_limits collapse_core() {
  pl::Dead_space_collapse_limits l;
  l.metric  = core();
  l.space_x = l.space_y = 20;
  l.grid                = 1;
  l.wl_move_budget = l.wl_total_budget = 1e18;
  return l;
}
void expect_legal(const std::vector<pl::Dead_space_object>& o, const pl::Dead_space_collapse_limits& l) {
  for (size_t i = 0; i < o.size(); ++i) {
    EXPECT_GE(o[i].xl - l.halo_x, l.metric.core_xl);
    EXPECT_GE(o[i].yl - l.halo_y, l.metric.core_yl);
    EXPECT_LE(o[i].xh + l.halo_x, l.metric.core_xh);
    EXPECT_LE(o[i].yh + l.halo_y, l.metric.core_yh);
    for (size_t j = i + 1; j < o.size(); ++j) {
      const auto &a = o[i], &b = o[j];
      EXPECT_TRUE(a.xh + l.space_x <= b.xl || b.xh + l.space_x <= a.xl || a.yh + l.space_y <= b.yl || b.yh + l.space_y <= a.yl)
          << i << " " << j;
    }
  }
}
}  // namespace

TEST(MacroDeadSpace, LargeOpenRegionIsNotDead) {
  // One macro in the middle: every free run is wide, nothing is dead.
  const auto r = pl::measure_dead_space({rect(400, 400, 200, 200)}, core());
  EXPECT_EQ(r.free_area, 1000 * 1000 - 200 * 200);
  EXPECT_EQ(r.dead_area, 0);
  EXPECT_TRUE(r.components.empty());
  // Two macros a channel apart: the channel is free, not dead.
  const auto c = pl::measure_dead_space({rect(300, 300, 200, 400), rect(520, 300, 200, 400)}, core());
  EXPECT_EQ(c.dead_area, 0);
}

TEST(MacroDeadSpace, CornerPocketBehindAChannel) {
  // M1 and M2 wall off the low-right corner; their only opening is the 20 wide channel between them.
  const auto r = pl::measure_dead_space({rect(650, 0, 100, 200), rect(650, 220, 350, 100)}, core(), true);
  EXPECT_EQ(r.pocket_area, 250 * 220);
  EXPECT_EQ(r.dead_area, 250 * 220);
  ASSERT_EQ(r.components.size(), 1u);
  EXPECT_EQ(r.components[0].kind, pl::Dead_kind::pocket);
  EXPECT_EQ(r.components[0].bbox.xl, 750);
  EXPECT_EQ(r.components[0].bbox.yh, 220);
  int64_t outline = 0;
  for (const auto& q : r.components[0].rects) {
    outline += (q.xh - q.xl) * (q.yh - q.yl);
  }
  EXPECT_EQ(outline, r.components[0].area);
  // A wide opening (200) instead of the channel: the corner joins the main region.
  const auto open = pl::measure_dead_space({rect(650, 0, 100, 200), rect(650, 400, 350, 100)}, core());
  EXPECT_EQ(open.pocket_area, 0);
}

TEST(MacroDeadSpace, EnclosedPocketAndIsland) {
  // A ring of four macros around a 150 x 150 hole, with channel gaps at the ring corners.
  std::vector<pl::Dead_space_rect> ring{rect(300, 300, 370, 100),
                                        rect(300, 420, 100, 250),
                                        rect(570, 420, 100, 250),
                                        rect(420, 570, 130, 100)};
  auto                             r = pl::measure_dead_space(ring, core());
  // The hole x 400..570, y 400..570 minus the channels along its walls.
  EXPECT_GT(r.pocket_area, 0);
  EXPECT_LE(r.dead_area, 170 * 170);
  ASSERT_FALSE(r.components.empty());
  EXPECT_EQ(r.components[0].kind, pl::Dead_kind::pocket);
  EXPECT_GE(r.components[0].bbox.xl, 400);
  EXPECT_LE(r.components[0].bbox.xh, 570);
  // Sealed and smaller than the island area: dead even where it is a channel.
  auto limits     = core();
  limits.min_area = 200 * 200;
  ring[3]         = rect(400, 570, 170, 100);
  ring[1]         = rect(300, 400, 100, 270);
  ring[2]         = rect(570, 400, 100, 270);
  r               = pl::measure_dead_space(ring, limits);
  EXPECT_EQ(r.island_area, 170 * 170);
}

TEST(MacroDeadSpace, SliverAlongEdgeAndBetweenMacros) {
  // 40 wide between the left edge and a macro: a sliver (wider than the 20 channel, narrower than 100).
  auto r = pl::measure_dead_space({rect(40, 300, 200, 400)}, core());
  EXPECT_EQ(r.notch_area, 40 * 400);
  EXPECT_EQ(r.dead_area, 40 * 400);
  ASSERT_EQ(r.components.size(), 1u);
  EXPECT_EQ(r.components[0].kind, pl::Dead_kind::notch);
  // 60 between two macros is a sliver too; at 120 it is open.
  r = pl::measure_dead_space({rect(300, 300, 200, 400), rect(560, 300, 200, 400)}, core());
  EXPECT_EQ(r.notch_area, 60 * 400);
  r = pl::measure_dead_space({rect(300, 300, 200, 400), rect(620, 300, 200, 400)}, core());
  EXPECT_EQ(r.dead_area, 0);
}

TEST(MacroDeadSpace, CollapseSlidesColumnIntoEdgeSliver) {
  // A three-macro column 80 from the right edge: the sliver closes by sliding the whole column.
  auto                               l = collapse_core();
  std::vector<pl::Dead_space_object> objects{macro(820, 0, 100, 100),
                                             macro(820, 120, 100, 100),
                                             macro(820, 240, 100, 100),
                                             macro(100, 600, 100, 100, false)};
  const auto                         r = pl::collapse_dead_space(objects, l, nullptr);
  EXPECT_GT(r.before.dead_area, 0);
  EXPECT_EQ(r.after.dead_area, 0);
  EXPECT_EQ(r.moves, 1);
  for (int i = 0; i < 3; ++i) {
    EXPECT_EQ(objects[i].xh, 1000) << i;
    EXPECT_EQ(objects[i].yl, 120 * i) << i;  // internal channel spacing preserved
  }
  EXPECT_EQ(objects[3].xl, 100);  // fixed macro stays
  expect_legal(objects, l);
}

TEST(MacroDeadSpace, CollapseKeepsHaloChannelAndBudget) {
  auto l   = collapse_core();
  l.halo_x = l.halo_y = 5;
  l.space_x = l.space_y = 24;
  // A column 80 (plus the halo) from the right edge, already a channel apart, plus a fixed neighbor on its left.
  std::vector<pl::Dead_space_object> objects{macro(815, 5, 100, 100),
                                             macro(815, 129, 100, 100),
                                             macro(815, 253, 100, 100),
                                             macro(691, 5, 100, 300, false)};
  auto                               copy = objects;
  const auto                         r    = pl::collapse_dead_space(objects, l, nullptr);
  EXPECT_LT(r.after.dead_area, r.before.dead_area);
  for (int i = 0; i < 3; ++i) {
    EXPECT_EQ(objects[i].xh, 995) << i;  // the halo stays inside the core
    EXPECT_EQ(objects[i].yl, 5 + 124 * i) << i;
  }
  expect_legal(objects, l);
  // A wirelength budget of zero against a cost that always grows: nothing moves.
  l.wl_move_budget = l.wl_total_budget = 0;
  const auto none = pl::collapse_dead_space(copy, l, [](const std::vector<size_t>&, int64_t, int64_t) { return 1.0; });
  EXPECT_EQ(none.moves, 0);
  EXPECT_EQ(none.after.dead_area, none.before.dead_area);
  EXPECT_EQ(copy[0].xl, 815);
}

TEST(MacroDeadSpace, CollapseRelocatesRimMacroIntoWalledPocket) {
  // Fixed M1/M2 wall off the low-right corner; a free macro of the main region fills it instead.
  auto                               l = collapse_core();
  std::vector<pl::Dead_space_object> objects{macro(650, 0, 100, 200, false),
                                             macro(650, 220, 350, 100, false),
                                             macro(300, 600, 230, 200)};
  const auto                         r = pl::collapse_dead_space(objects, l, nullptr);
  EXPECT_EQ(r.before.pocket_area, 250 * 220);
  EXPECT_LE(r.after.dead_area, 4 * 10 * 10);  // at most raster corners where two channels meet
  EXPECT_LE(r.after.stranded_area, 4 * 10 * 10);
  EXPECT_EQ(r.moves, 1);
  EXPECT_EQ(objects[2].xl, 770);
  EXPECT_EQ(objects[2].yl, 0);
  expect_legal(objects, l);
}

TEST(MacroDeadSpace, OptionsParse) {
  EXPECT_FALSE(pl::parse_dead_space_options("off"));
  EXPECT_FALSE(pl::parse_dead_space_options(""));
  EXPECT_EQ(*pl::parse_dead_space_options("auto"), pl::Dead_space_options{});
  EXPECT_FALSE(pl::parse_dead_space_options("metric")->collapse);
  const auto o = pl::parse_dead_space_options("narrow=50, pocket=0.2,hpwl=0.01,moves=8");
  EXPECT_TRUE(o->collapse);
  EXPECT_EQ(o->narrow_um, 50);
  EXPECT_EQ(o->pocket_fraction, 0.2);
  EXPECT_EQ(o->hpwl_total, 0.01);
  EXPECT_EQ(o->max_moves, 8);
  EXPECT_THROW(pl::parse_dead_space_options("bogus"), std::invalid_argument);
  EXPECT_THROW(pl::parse_dead_space_options("pocket=2"), std::invalid_argument);
  EXPECT_THROW(pl::parse_dead_space_options("narrow=-1"), std::invalid_argument);
  EXPECT_THROW(pl::parse_dead_space_options("moves=1.5"), std::invalid_argument);
  EXPECT_THROW(pl::parse_dead_space_options("grid=8.5"), std::invalid_argument);
  EXPECT_THROW(pl::parse_dead_space_options("moves=1e100"), std::invalid_argument);
  EXPECT_THROW(pl::parse_dead_space_options("grid=1e100"), std::invalid_argument);
  EXPECT_EQ(pl::parse_dead_space_options("moves=0,grid=8")->max_moves, 0);
  EXPECT_THROW(pl::measure_dead_space({}, pl::Dead_space_limits{}), std::invalid_argument);
}

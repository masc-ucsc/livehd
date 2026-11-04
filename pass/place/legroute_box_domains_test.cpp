// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "gtest/gtest.h"
#include "legroute_internal.hpp"

namespace livehd::placement::lr {
namespace {
struct Fixture {
  physical::Technology                      tech;
  physical::Floorplan                       fp;
  Legroute_options                          options;
  Design                                    design{tech, fp, options};
  Grid                                      grid;
  Boxes                                     boxes;
  std::vector<int>                          ids, tiles;
  std::vector<char>                         pinned;
  std::unordered_map<int, std::vector<int>> allowed;
  explicit Fixture(int columns = 6) {
    options.window_tiles = 0;
    grid.cols            = columns;
    grid.rows = grid.regions = 1;
    grid.site = grid.row_h = 1;
    grid.tile_w = grid.sites = 10;
    grid.tile_h              = 2;
    grid.region.assign(grid.size(), 0);
    grid.blocked.assign(grid.size(), 0);
    grid.key.assign(grid.size(), 0);
    grid.keys = {
        {{0, 0, 10}, {1, 0, 10}},
        { {0, 0, 4},  {1, 0, 4}}
    };
    pinned.assign(grid.size(), 0);
    for (int t = 0; t < int(grid.size()); ++t) {
      tiles.push_back(t);
    }
  }
  void box(int tile = 0, int width = 6) {
    const int id = int(boxes.boxes.size());
    Box       box;
    box.cells = {id};
    box.tile  = tile;
    box.cx    = grid.cx(grid.col(tile));
    box.cy    = grid.cy(grid.row(tile));
    boxes.boxes.push_back(box);
    ids.push_back(id);
    design.width.push_back(width);
    design.tall.push_back(false);
  }
  Box_domains prepare() const {
    return prepare_box_domains(design, options, grid, 0, ids, tiles, boxes, allowed, pinned, [] {});
  }
  std::vector<int> domain(const Box_domains& result, size_t i) const {
    return box_domain_tiles(design, grid, ids[i], result.windows[i], boxes, allowed, result.lifted, pinned, tiles, [] {});
  }
};
}  // namespace

TEST(LegrouteBoxDomains, InitialWindowUsesActualFitsAndPreservesFixedAndPinnedRules) {
  Fixture f;
  f.box();
  f.box(5);
  f.boxes.boxes[1].fixed = true;
  f.grid.key[1]          = 1;  // a six-site cell cannot use these four-site segments
  f.pinned[2] = f.pinned[5] = 1;
  const auto result         = f.prepare();
  ASSERT_EQ(result.windows.size(), 2u);
  EXPECT_EQ(f.domain(result, 0), (std::vector<int>{0, 3, 4}));
  EXPECT_EQ(f.domain(result, 1), (std::vector<int>{5}));
  ASSERT_EQ(result.matching.size(), 2u);
  EXPECT_EQ(result.matching[1], 5);
  EXPECT_TRUE(result.lifted.empty());
}

TEST(LegrouteBoxDomains, JointMatchingExpandsCrowdedWindowsWithoutChangingBoxCenters) {
  Fixture f;
  for (int i = 0; i < 6; ++i) {
    f.box();
  }
  const auto before = f.boxes;
  const auto result = f.prepare();
  ASSERT_EQ(result.matching.size(), 6u);
  EXPECT_GT(result.relaxed_windows, 0u);
  const std::set<int> assigned(result.matching.begin(), result.matching.end());
  EXPECT_EQ(assigned, (std::set<int>{0, 1, 2, 3, 4, 5}));
  for (size_t i = 0; i < result.matching.size(); ++i) {
    const auto domain = f.domain(result, i);
    EXPECT_NE(std::ranges::find(domain, result.matching[i]), domain.end());
    EXPECT_EQ(f.boxes.boxes[i].tile, before.boxes[i].tile);
    EXPECT_EQ(f.boxes.boxes[i].cx, before.boxes[i].cx);
    EXPECT_EQ(f.boxes.boxes[i].cy, before.boxes[i].cy);
  }
}

TEST(LegrouteBoxDomains, MatchingLiftsOnlyTheDeficientAllowedTrackDomain) {
  Fixture f(3);
  f.box();
  f.box();
  f.allowed = {
      {0, {0}},
      {1, {0}}
  };
  const auto result = f.prepare();
  EXPECT_EQ(result.lifted, (std::set<int>{1}));
  EXPECT_EQ(result.relaxed_windows, 1u);
  EXPECT_EQ(f.domain(result, 0), (std::vector<int>{0}));
  EXPECT_EQ(f.domain(result, 1), (std::vector<int>{0, 1}));
  EXPECT_EQ(result.matching, (std::vector<int>{0, 1}));
  EXPECT_EQ(f.allowed.at(1), (std::vector<int>{0}));
}

TEST(LegrouteBoxDomains, OrderedMeshAndIncompleteMatchingStayExplicit) {
  Fixture f(4);
  f.tiles = {3, 0};  // exact caller order, with a hole between the region tiles
  f.box();
  f.box();
  f.box();
  const auto result = f.prepare();
  EXPECT_TRUE(result.matching.empty());
  ASSERT_EQ(result.windows.size(), 3u);
  for (size_t i = 0; i < result.windows.size(); ++i) {
    EXPECT_EQ(f.domain(result, i), (std::vector<int>{3, 0}));
  }
  for (const auto& box : f.boxes.boxes) {
    EXPECT_EQ(box.tile, 0);
  }
}

TEST(LegrouteBoxDomains, FullUnmatchedWindowReleasesMatchedBoxesInItsHallComponent) {
  Fixture f(6);
  f.grid.key[3] = f.grid.key[4] = f.grid.key[5] = 1;
  for (int i = 0; i < 4; ++i) {
    f.box(0, 2);  // original four-fit window is columns0..3
  }
  f.box(0, 6);  // full region window, but only columns0..2 fit this wide cell
  const auto result = f.prepare();
  ASSERT_EQ(result.matching.size(), 5u);
  EXPECT_EQ(result.windows[4].c1, 5);
  EXPECT_EQ(f.domain(result, 4), (std::vector<int>{0, 1, 2}));
  EXPECT_LE(result.matching[4], 2);
  std::set<int> assigned(result.matching.begin(), result.matching.end());
  EXPECT_EQ(assigned.size(), 5u);
  EXPECT_TRUE(assigned.contains(4));  // a matched narrow box yields a wide-cell slot
  for (size_t i = 0; i < 4; ++i) {
    EXPECT_EQ(result.windows[i].c1, 4);
  }
  for (const auto& box : f.boxes.boxes) {
    EXPECT_EQ(box.tile, 0);
  }
}

TEST(LegrouteBoxDomains, FixedCollisionsCannotGrowOrAcquireAnotherTile) {
  Fixture f(4);
  f.box();
  f.box();
  for (auto& box : f.boxes.boxes) {
    box.fixed = true;
  }
  f.allowed[0] = {0, 1, 2, 3};  // a track hull cannot widen an immutable placement
  const auto result = f.prepare();
  EXPECT_TRUE(result.matching.empty());
  EXPECT_EQ(result.relaxed_windows, 0u);
  for (size_t i = 0; i < 2; ++i) {
    EXPECT_EQ(f.domain(result, i), (std::vector<int>{0}));
    EXPECT_EQ(result.windows[i].c0, 0);
    EXPECT_EQ(result.windows[i].c1, 0);
    const Box_window wide{0, 3, 0, 0};
    EXPECT_EQ(box_domain_tiles(f.design, f.grid, int(i), wide, f.boxes, f.allowed, result.lifted, f.pinned, f.tiles, [] {}),
              (std::vector<int>{0}));
  }
}

TEST(LegrouteBoxDomains, DeadlineCallbackAbortsWithoutMutatingInputOrPublishingPartialResult) {
  Fixture f;
  for (int i = 0; i < 6; ++i) {
    f.box();
  }
  struct Expired {};
  size_t calls = 0;
  EXPECT_THROW(prepare_box_domains(f.design,
                                   f.options,
                                   f.grid,
                                   0,
                                   f.ids,
                                   f.tiles,
                                   f.boxes,
                                   f.allowed,
                                   f.pinned,
                                   [&] {
                                     if (++calls == 10) {
                                       throw Expired{};
                                     }
                                   }),
               Expired);
  EXPECT_EQ(calls, 10u);
  for (const auto& box : f.boxes.boxes) {
    EXPECT_EQ(box.tile, 0);
  }
  EXPECT_EQ(f.prepare().matching.size(), 6u);
}
}  // namespace livehd::placement::lr

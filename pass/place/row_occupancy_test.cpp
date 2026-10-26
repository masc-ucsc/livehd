// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "row_occupancy.hpp"

#include <stdexcept>
#include <string>
#include <utility>

#include "gtest/gtest.h"

namespace livehd::placement {
namespace {
namespace ph = physical;
using Gaps   = std::map<int64_t, int64_t>;
std::string text(const Gaps& gaps) {
  std::string result;
  for (const auto& [lo, hi] : gaps) {
    result += (result.empty() ? "" : " ") + std::to_string(lo) + "-" + std::to_string(hi);
  }
  return result;
}
ph::Technology technology() {
  ph::Technology tech;
  tech.dbu           = 100;
  tech.sites["site"] = {1, 1, true};
  for (const char* klass : {"CORE", "CORE WELLTAP", "BLOCK"}) {
    auto& m = tech.macros[klass];
    m.width = m.height = 2;
    m.klass            = klass;
    m.site             = "site";
  }
  return tech;
}
ph::Component component(std::string name, std::string macro, ph::Status status) {
  Ann_place p(300, 100, 200, 200);
  p.status = status;
  return {std::move(name), std::move(macro), p};
}
}  // namespace

TEST(RowOccupancy, SubtractGapsSplitsTrimsAndIgnoresDisjointRanges) {
  Gaps gaps;
  gaps.emplace(0, 10);
  gaps.emplace(12, 20);
  subtract_gaps(gaps, 5, 14);
  EXPECT_EQ(text(gaps), "0-5 14-20");
  subtract_gaps(gaps, 0, 5);
  EXPECT_EQ(text(gaps), "14-20");
  subtract_gaps(gaps, 20, 25);
  subtract_gaps(gaps, 10, 14);
  EXPECT_EQ(text(gaps), "14-20");
}

TEST(RowOccupancy, RowsClipInwardToTheDieSortAndRejectMalformedFrames) {
  const auto    tech = technology();
  ph::Floorplan fp;
  fp.dbu  = 100;
  fp.die  = {50, 0, 1000, 300};
  fp.rows = {
      {   "top", "site", 0, 200, 8, 100,  ph::Orientation::N},
      {"bottom", "site", 0,   0, 8, 100,  ph::Orientation::N},
      { "above", "site", 0, 300, 8, 100, ph::Orientation::FS},
  };
  const auto rows = row_gaps(tech, fp, "owner");
  ASSERT_EQ(rows.size(), 2);
  EXPECT_EQ(rows[0].source, 1);
  EXPECT_EQ(rows[1].source, 0);
  EXPECT_EQ(rows[0].height, 100);
  EXPECT_EQ(text(rows[0].free), "1-8");  // site 0 straddles the die edge
  EXPECT_EQ(gap_rect(rows[1], 1, 8), (ph::Rect{100, 200, 800, 300}));
  EXPECT_EQ(gaps_extent(rows), (ph::Rect{100, 0, 800, 300}));

  auto unknown          = fp;
  unknown.rows[0].site  = "missing";
  auto overlapping      = fp;
  overlapping.rows[0].y = 50;
  auto outside          = fp;
  outside.die           = {0, 0, 1000, 50};
  for (const auto& [frame, message] : {
           std::pair{    &unknown,                    "owner requires valid row sites"},
           std::pair{&overlapping, "owner does not accept overlapping row definitions"},
           std::pair{    &outside,                          "owner has no usable rows"},
  }) {
    try {
      row_gaps(tech, *frame, "owner");
      ADD_FAILURE() << message;
    } catch (const std::invalid_argument& e) {
      EXPECT_STREQ(e.what(), message);
    }
  }
}

TEST(RowOccupancy, ReservationCoversEveryOverlappedRowAndPartialSite) {
  const auto    tech = technology();
  ph::Floorplan fp;
  fp.dbu = 100;
  fp.die = {0, 0, 1000, 300};
  for (int y = 0; y < 3; ++y) {
    fp.rows.push_back({"r" + std::to_string(y), "site", 0, y * 100, 10, 100, ph::Orientation::N});
  }
  auto rows = row_gaps(tech, fp, "owner");
  reserve_gaps(rows, {250, 50, 420, 200});
  EXPECT_EQ(text(rows[0].free), "0-2 5-10");
  EXPECT_EQ(text(rows[1].free), "0-2 5-10");
  EXPECT_EQ(text(rows[2].free), "0-10");
}

TEST(RowOccupancy, FrameRolesAndReservedRectangles) {
  auto tech                 = technology();
  tech.macros["mask"].klass = "BLOCK";
  ph::Component soft{"__lhd_frame_mask_soft_0", "mask", Ann_place(0, 0, 100, 100)};
  soft.place.status = ph::Status::FIXED;
  auto hard         = soft;
  hard.name         = "__lhd_frame_mask_hard_0";
  EXPECT_EQ(frame_role(tech, soft), Frame_role::ignored);
  EXPECT_EQ(frame_role(tech, hard), Frame_role::obstacle);
  EXPECT_EQ(obstacle_rect(tech, hard, 30, 40), (ph::Rect{0, 0, 100, 100}));  // masks carry no halo

  const auto macro = component("m", "BLOCK", ph::Status::FIXED);
  EXPECT_EQ(frame_role(tech, macro), Frame_role::obstacle);
  EXPECT_EQ(obstacle_rect(tech, macro, 30, 40), (ph::Rect{270, 60, 530, 340}));
  const auto tap = component("t", "CORE WELLTAP", ph::Status::FIXED);
  EXPECT_TRUE(fixed_physical_cell(tap, tech.macros.at("CORE WELLTAP")));
  EXPECT_EQ(frame_role(tech, tap), Frame_role::obstacle);
  EXPECT_EQ(obstacle_rect(tech, tap, 30, 40), (ph::Rect{300, 100, 500, 300}));
  EXPECT_EQ(frame_role(tech, component("u", "CORE WELLTAP", ph::Status::PLACED)), Frame_role::movable);

  const auto fixed = component("f", "CORE", ph::Status::LOCKED);
  EXPECT_EQ(frame_role(tech, fixed), Frame_role::fixed_logic);
  EXPECT_EQ(padded_rect(tech, fixed, 2), (ph::Rect{100, 100, 700, 300}));
  EXPECT_EQ(frame_role(tech, component("p", "CORE", ph::Status::PLACED)), Frame_role::movable);
  tech.macros["CORE"].site = "missing";
  EXPECT_THROW(padded_rect(tech, fixed, 1), std::invalid_argument);
}
}  // namespace livehd::placement

// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "pin_adjust.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <stdexcept>

#include "gtest/gtest.h"

namespace livehd::placement {
namespace dp = DreamPlace::native;
namespace {
struct Fixture {
  dp::Design               d;
  physical::Floorplan      fp;
  std::vector<std::string> bus;
  std::vector<int>         bits;
  explicit Fixture(int n = 5) {
    fp.die        = {0, 0, 100, 100};
    d.layout      = {0, 0, 100, 100, 1, 1};
    d.num_movable = n;
    for (int i = 0; i < 2 * n; ++i) {
      d.node_x.push_back(i < n ? 85 : 0);
      d.node_y.push_back(10 + 15 * (i % n));
      d.node_size_x.push_back(i < n ? 1 : 0);
      d.node_size_y.push_back(i < n ? 1 : 0);
    }
    d.flat_net2pin_start.push_back(0);
    for (int i = 0; i < n; ++i) {
      d.pin2node.insert(d.pin2node.end(), {i, n + i});
      d.pin_offset_x.insert(d.pin_offset_x.end(), {0., 0.});
      d.pin_offset_y.insert(d.pin_offset_y.end(), {0., 0.});
      d.flat_net2pin.insert(d.flat_net2pin.end(), {2 * i, 2 * i + 1});
      d.flat_net2pin_start.push_back(2 * i + 2);
      physical::Terminal t;
      t.name   = "data[" + std::to_string(i) + "]";
      t.status = physical::Status::PLACED;
      t.x      = 0;
      t.y      = 10 + 15 * i;
      fp.terminals.push_back(t);
      bus.push_back("data");
      bits.push_back(i);
    }
    d.build_maps();
  }
  double perimeter(int i) const {
    const auto& p = fp.terminals[i];
    if (p.x <= 5) {  // Includes the inset fixed-pin fixture below.
      return p.y;
    }
    if (p.y == 100) {
      return 100 + p.x;
    }
    if (p.x == 100) {
      return 300 - p.y;
    }
    EXPECT_EQ(p.y, 0);
    return 400 - p.x;
  }
  void check() const {
    for (size_t i = 0; i < bits.size(); ++i) {
      EXPECT_EQ(d.node_x[d.num_movable + i], fp.terminals[i].x);
      EXPECT_EQ(d.node_y[d.num_movable + i], fp.terminals[i].y);
      for (size_t j = 0; j < i; ++j) {
        EXPECT_GE(std::abs(fp.terminals[i].x - fp.terminals[j].x) + std::abs(fp.terminals[i].y - fp.terminals[j].y), 5);
      }
    }
    std::vector<int> order(bits.size());
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(), [&](int a, int b) { return perimeter(a) < perimeter(b); });
    for (const auto& name : bus) {
      std::vector<int> lanes;
      for (int i : order) {
        if (bus[i] == name) {
          lanes.push_back(bits[i]);
        }
      }
      if (lanes.size() < 3) {
        continue;
      }
      std::rotate(lanes.begin(), std::min_element(lanes.begin(), lanes.end()), lanes.end());
      bool up = std::is_sorted(lanes.begin(), lanes.end());
      std::reverse(lanes.begin() + 1, lanes.end());
      EXPECT_TRUE(up || std::is_sorted(lanes.begin(), lanes.end()));
    }
  }
};
}  // namespace

TEST(PinAdjust, OrderedInitializationAndMovementPreserveConnectivity) {
  Fixture f;
  // Deliberately scrambled original positions; use declaration lanes, not names.
  std::swap(f.fp.terminals[1].y, f.fp.terminals[3].y);
  const auto pins = f.d.pin2node, nets = f.d.flat_net2pin;
  adjust_fp_pins(f.d, f.fp, f.bus, f.bits, 5, .05, .001, true);
  f.check();
  for (int step = 0; step < 4; ++step) {
    for (int i = 0; i < f.d.num_movable; ++i) {
      f.d.node_x[i] = 10 + step * 20;
    }
    const auto before = f.fp.terminals;
    auto       stats  = adjust_fp_pins(f.d, f.fp, f.bus, f.bits, 5, .05, .001, false);
    EXPECT_LE(stats.after, stats.before + 1e-10);
    for (size_t i = 0; i < before.size(); ++i) {
      EXPECT_LE(std::abs(before[i].x - f.fp.terminals[i].x) + std::abs(before[i].y - f.fp.terminals[i].y), 20);
    }
    f.check();
  }
  EXPECT_EQ(f.d.pin2node, pins);
  EXPECT_EQ(f.d.flat_net2pin, nets);
}

TEST(PinAdjust, PartialFixedBusAndInterleavedScalarKeepPositions) {
  Fixture f;
  // LEF/DEF pin centers can be half a shape inside the die, not on its edge.
  f.fp.terminals[2].x = f.d.node_x[f.d.num_movable + 2] = 2;
  f.fp.terminals[2].status                              = physical::Status::FIXED;
  f.fp.terminals[4].status                              = physical::Status::FIXED;
  f.bus[4]                                              = "scalar";
  f.bits[4]                                             = 0;
  const auto fixed                                      = f.fp.terminals;
  adjust_fp_pins(f.d, f.fp, f.bus, f.bits, 5, .5, 0, true);
  for (int i = 0; i < 3; ++i) {
    adjust_fp_pins(f.d, f.fp, f.bus, f.bits, 5, .5, 0, false);
    EXPECT_EQ(f.fp.terminals[2].x, fixed[2].x);
    EXPECT_EQ(f.fp.terminals[2].y, fixed[2].y);
    EXPECT_EQ(f.fp.terminals[4].x, fixed[4].x);
    EXPECT_EQ(f.fp.terminals[4].y, fixed[4].y);
    f.check();
  }
}

TEST(PinAdjust, WholeBusReversalAndCornerMovement) {
  Fixture                                f(4);
  // Two pins on either side of the top-left corner; reverse their desired
  // order. Interior lanes cannot individually cross, but a whole bus can flip.
  const std::vector<std::pair<int, int>> xy{
      { 0,  80},
      { 0,  90},
      {10, 100},
      {20, 100}
  };
  for (int i = 0; i < 4; ++i) {
    f.fp.terminals[i].x = f.d.node_x[4 + i] = xy[i].first;
    f.fp.terminals[i].y = f.d.node_y[4 + i] = xy[i].second;
    f.d.node_x[i]                           = xy[3 - i].first;
    f.d.node_y[i]                           = xy[3 - i].second;
  }
  const auto stats = adjust_fp_pins(f.d, f.fp, f.bus, f.bits, 5, .5, 0, false);
  EXPECT_LT(stats.after, stats.before);
  EXPECT_GE(stats.reversals, 1);
  f.check();
}

TEST(PinAdjust, RejectsInvalidMetadataAndLeavesFixedInteriorPins) {
  Fixture f;
  EXPECT_THROW(adjust_fp_pins(f.d, f.fp, {}, f.bits, 5, .1, 0, true), std::invalid_argument);
  EXPECT_THROW(adjust_fp_pins(f.d, f.fp, f.bus, f.bits, 0, .1, 0, true), std::invalid_argument);
  for (auto& p : f.fp.terminals) {
    p.status = physical::Status::FIXED;
    p.x      = 50;
  }
  const auto before = f.fp.terminals;
  adjust_fp_pins(f.d, f.fp, f.bus, f.bits, 5, .1, 0, true);
  for (size_t i = 0; i < before.size(); ++i) {
    EXPECT_EQ(f.fp.terminals[i].x, before[i].x);
    EXPECT_EQ(f.fp.terminals[i].y, before[i].y);
  }
}
}  // namespace livehd::placement

namespace livehd::placement {
namespace {
// A single ideal-clock terminal, with a broadly spread or concentrated set of
// sink access points. Clock nets remain masked from cell GP's HPWL objective.
static std::pair<int64_t, int64_t> check_clock_io(bool concentrated, bool fixed, bool tall) {
  dp::Design          d;
  physical::Floorplan fp;
  fp.die        = tall ? physical::Rect{0, 0, 100, 200} : physical::Rect{0, 0, 200, 100};
  d.layout      = {fp.die.xl, fp.die.yl, fp.die.xh, fp.die.yh, 1, 1};
  d.num_movable = 4;
  d.node_x      = concentrated ? std::vector<double>{10, 15, 20, 15, 0} : std::vector<double>{20, 180, 20, 180, 0};
  d.node_y      = concentrated ? std::vector<double>{10, 15, 20, 15, 0} : std::vector<double>{20, 20, 80, 80, 0};
  if (tall && !concentrated) {
    for (size_t i = 0; i < 4; ++i) {
      std::swap(d.node_x[i], d.node_y[i]);
    }
  }
  d.node_size_x = d.node_size_y = {1, 1, 1, 1, 0};
  d.pin2node                    = {4, 0, 1, 2, 3};
  d.pin_offset_x = d.pin_offset_y = {0, 0, 0, 0, 0};
  d.flat_net2pin                  = {0, 1, 2, 3, 4};
  d.flat_net2pin_start            = {0, 5};
  d.net_mask                      = {0};
  d.net_weights                   = {1};
  d.build_maps();
  physical::Terminal clock;
  clock.name      = "clk";
  clock.direction = "INPUT";
  clock.status    = fixed ? physical::Status::FIXED : physical::Status::PLACED;
  fp.terminals.push_back(clock);
  const auto connectivity = d.pin2node;
  adjust_fp_pins(d, fp, {"clk"}, {0}, 5, .5, 0, true);
  if (fixed) {
    EXPECT_EQ(fp.terminals[0].x, 0);
    EXPECT_EQ(fp.terminals[0].y, 0);
  }
  EXPECT_EQ(d.pin2node, connectivity);
  const auto before = std::pair{fp.terminals[0].x, fp.terminals[0].y};
  // Reposition every clock sink. A masked network must create no IO attraction.
  for (size_t i = 0; i < 4; ++i) {
    d.node_x[i] = 5;
    d.node_y[i] = 95;
  }
  adjust_fp_pins(d, fp, {"clk"}, {0}, 5, .5, 0, false);
  EXPECT_EQ((std::pair{fp.terminals[0].x, fp.terminals[0].y}), before);
  return before;
}
TEST(PinAdjust, MaskedClockSinkLocationsDoNotAffectIoAssignment) {
  EXPECT_EQ(check_clock_io(false, false, false), check_clock_io(true, false, false));
  EXPECT_EQ(check_clock_io(false, false, true), check_clock_io(true, false, true));
  EXPECT_EQ(check_clock_io(false, true, false), (std::pair<int64_t, int64_t>{0, 0}));
}
}  // namespace
TEST(PinAdjust, CircularNeighborsHandleSmallBusesAndWraparound) {
  for (int lanes : {2, 3, 4, 9}) {
    Fixture    f(lanes);
    const auto point = [](double t) -> std::pair<int, int> {
      if (t < 100) {
        return {0, std::lround(t)};
      }
      if (t < 200) {
        return {std::lround(t - 100), 100};
      }
      if (t < 300) {
        return {100, std::lround(300 - t)};
      }
      return {std::lround(400 - t), 0};
    };
    for (int i = 0; i < lanes; ++i) {
      const auto [x, y]   = point((i + .5) * 400 / lanes);
      f.fp.terminals[i].x = f.d.node_x[lanes + i] = x;
      f.fp.terminals[i].y = f.d.node_y[lanes + i] = y;
    }
    f.fp.terminals[1].status = physical::Status::FIXED;
    const auto anchor        = f.fp.terminals[1];
    for (int step = 0; step < 4; ++step) {
      // Preferred locations cross existing lanes and the perimeter origin.
      for (int i = 0; i < lanes; ++i) {
        const auto [x, y] = point(std::fmod((lanes - i + step + .5) * 400 / lanes, 400));
        f.d.node_x[i]     = x;
        f.d.node_y[i]     = y;
      }
      const auto stats = adjust_fp_pins(f.d, f.fp, f.bus, f.bits, 5, .5, 0, false);
      EXPECT_LE(stats.after, stats.before + 1e-10);
      EXPECT_EQ(f.fp.terminals[1].x, anchor.x);
      EXPECT_EQ(f.fp.terminals[1].y, anchor.y);
      f.check();
    }
  }
}

}  // namespace livehd::placement

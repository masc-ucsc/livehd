// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// plan_buffer_choices on a recorded net (HighTide vortex, a 534-load stream-buffer enable, dumped with
// LHD_STEINER_DUMP): one critical sink dominates the driver's required time, so every alternative scores within a few
// ps. The planner used to keep the 0.2 ps "winner": a 324-buffer, 108-deep daisy chain at 4x the area of a balanced
// tree, whose far sinks then became the design's worst path. Required times within tolerance are a tie now, and the
// cheapest of them wins.
#include <fstream>
#include <functional>
#include <string>
#include <vector>

#include "gtest/gtest.h"
#include "steiner_repair.hpp"

namespace livehd::placement {
namespace {
double number(std::istream& in) {
  std::string word;
  in >> word;
  return std::stod(word);
}
Buffer_drive read_drive(std::istream& in) {
  Buffer_drive d;
  d.input_ff         = number(in);
  d.max_cap_ff       = number(in);
  d.max_distance_dbu = number(in);
  d.max_fanout       = static_cast<unsigned>(number(in));
  d.intrinsic_ps     = number(in);
  d.resistance_ohm   = number(in);
  d.area             = number(in);
  return d;
}
}  // namespace

TEST(SteinerChoice, RequiredTimeTieDoesNotPickADaisyChain) {
  std::ifstream in("pass/place/tests/steiner_chain_net.txt");
  ASSERT_TRUE(in);
  std::string word;
  size_t      n_loads = 0, n_drives = 0;
  in >> word >> n_loads >> n_drives;
  ASSERT_EQ(word, "plan");
  Point root;
  in >> word;
  root.first  = number(in);
  root.second = number(in);
  in >> word;
  const double r = number(in), c = number(in);
  in >> word;
  Buffer_limits limits;
  limits.cells = static_cast<size_t>(number(in));
  limits.area  = number(in);
  in >> word;
  const auto driver = read_drive(in);
  in >> word;
  std::vector<Buffer_drive> drives;
  for (size_t i = 0; i < n_drives; ++i) {
    drives.push_back(read_drive(in));
  }
  in >> word;
  std::vector<Point>  loads(n_loads);
  std::vector<double> caps(n_loads), required(n_loads);
  for (size_t i = 0; i < n_loads; ++i) {
    loads[i].first  = number(in);
    loads[i].second = number(in);
    caps[i]         = number(in);
    required[i]     = number(in);
  }
  const auto plan = plan_buffer_choices(root, loads, caps, required, drives, driver, r, c, limits);
  std::function<size_t(const std::vector<Buffer_tree::Load>&)> depth = [&](const std::vector<Buffer_tree::Load>& children) {
    size_t d = 0;
    for (const auto& child : children) {
      if (child.buffer) {
        d = std::max(d, 1 + depth(plan.buffers[child.index].loads));
      }
    }
    return d;
  };
  EXPECT_LE(depth(plan.root), 17U);
  EXPECT_LE(plan.buffers.size(), 64U);
}
}  // namespace livehd::placement

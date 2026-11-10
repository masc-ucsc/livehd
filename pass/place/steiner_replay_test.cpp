// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
// Manual harness: replay the planner on a net dumped by LHD_STEINER_DUMP (see steiner_repair.cpp).
//   LHD_STEINER_REPLAY=/tmp/dump.txt LHD_STEINER_TRACE=1 bazel test //pass/place:steiner_replay_test
//       with --test_env=LHD_STEINER_REPLAY --test_env=LHD_STEINER_TRACE --test_output=all
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "gtest/gtest.h"
#include "steiner_repair.hpp"

namespace livehd::placement {
namespace {
// Doubles in a dump can be "inf" (an unlimited distance or area): operator>> refuses them, strtod does not.
double number(std::istream& in) {
  std::string text;
  in >> text;
  return std::strtod(text.c_str(), nullptr);
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

TEST(SteinerReplay, DumpedNets) {
  const char* path = std::getenv("LHD_STEINER_REPLAY");
  if (path == nullptr) {
    GTEST_SKIP() << "set LHD_STEINER_REPLAY to a dump written with LHD_STEINER_DUMP";
  }
  std::ifstream in(path);
  ASSERT_TRUE(in) << path;
  std::string word;
  int         nets = 0;
  while (in >> word) {
    ASSERT_EQ(word, "plan");
    size_t n_loads = 0, n_drives = 0;
    in >> n_loads >> n_drives;
    Point         root;
    double        r = 0, c = 0, area = 0;
    size_t        cells = 0;
    Buffer_limits limits;
    in >> word;
    root.first  = number(in);
    root.second = number(in);
    in >> word;
    r = number(in);
    c = number(in);
    in >> word;
    cells = static_cast<size_t>(number(in));
    area  = number(in);
    limits.cells = cells;
    limits.area  = area;
    in >> word;  // driver
    const auto               driver = read_drive(in);
    in >> word;  // drives
    std::vector<Buffer_drive> drives;
    for (size_t i = 0; i < n_drives; ++i) {
      drives.push_back(read_drive(in));
    }
    in >> word;  // loads
    std::vector<Point>  loads(n_loads);
    std::vector<double> caps(n_loads), required(n_loads);
    for (size_t i = 0; i < n_loads; ++i) {
      loads[i].first  = number(in);
      loads[i].second = number(in);
      caps[i]         = number(in);
      required[i]     = number(in);
    }
    in >> word;  // end
    std::cout << "net " << ++nets << ": " << n_loads << " loads, " << n_drives << " drives, wire r=" << r << " c=" << c
              << " growth cells<=" << cells << " area<=" << area << "\n  driver: cap<=" << driver.max_cap_ff << " fanout<=" << driver.max_fanout
              << " R=" << driver.resistance_ohm << "\n";
    for (size_t i = 0; i < drives.size() && i < 12; ++i) {
      std::cout << "  drive " << i << ": in=" << drives[i].input_ff << " cap<=" << drives[i].max_cap_ff << " fanout<=" << drives[i].max_fanout
                << " dist<=" << drives[i].max_distance_dbu << " area=" << drives[i].area << "\n";
    }
    try {
      const auto plan = plan_buffer_choices(root, loads, caps, required, drives, driver, r, c, limits);
      size_t     widest = plan.root.size();
      for (const auto& b : plan.buffers) {
        widest = std::max(widest, b.loads.size());
      }
      std::cout << "  -> " << plan.buffers.size() << " buffers, root drives " << plan.root.size() << ", widest driver " << widest << "\n";
    } catch (const std::exception& e) {
      std::cout << "  -> " << e.what() << "\n";
    }
  }
}
}  // namespace
}  // namespace livehd::placement

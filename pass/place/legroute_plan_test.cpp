// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>

#include "gtest/gtest.h"
#include "legroute_test_frame.hpp"
#include "rapidjson/document.h"

namespace livehd::placement {
using Fixture = Legroute_test_frame;

TEST(LegroutePlan, AcceptsOverlappingCellsAndLeavesThePlacementUnchanged) {
  Fixture    f(64, 4);
  const auto a = f.cell("inv", 100, 0), b = f.cell("inv", 100, 0), c = f.cell("inv", 2500, 250);
  f.net({
      {a, "Y"},
      {c, "A"}
  });
  f.net({
      {b, "Y"},
      {c, "Y"}
  });
  Legroute_options opt;
  opt.clock_charge  = false;
  const auto before = f.fp.components;
  const auto plan   = legroute_plan(f.tech, f.fp, f.timing, opt);
  EXPECT_TRUE(plan.pg_missing);
  EXPECT_EQ(plan.signals, 2u);
  EXPECT_EQ(plan.timed_signals, 2u);
  EXPECT_EQ(plan.unrouted, 0u);
  EXPECT_EQ(plan.overflow_tracks, 0u);
  EXPECT_EQ(plan.residual(), 0u);
  EXPECT_EQ(plan.hot_tiles, 0u);
  EXPECT_TRUE(std::ranges::all_of(plan.component_hot_distance,
                                  [](unsigned distance) { return distance == std::numeric_limits<unsigned>::max(); }));
  for (size_t i = 0; i < before.size(); ++i) {
    EXPECT_EQ(f.fp.components[i].place.x, before[i].place.x);
    EXPECT_EQ(f.fp.components[i].place.y, before[i].place.y);
    EXPECT_DOUBLE_EQ(plan.component_pressure[i], 1);
  }
}

TEST(LegroutePlan, CongestionPressuresMovableCellsWithoutInflatingFixedCells) {
  Fixture    f(200, 16);
  const auto fixed                    = f.cell("inv", 1400, 750);
  f.fp.components[fixed].place.status = ph::Status::FIXED;
  const auto resident                 = f.cell("inv", 100, 0);  // no signal endpoint: transit pressure still identifies this cell
  const auto distant                  = f.cell("inv", 9000, 3500);
  for (int i = 0; i < 12; ++i) {
    const auto a = f.cell("inv", 100, 0), b = f.cell("inv", 2500, 0);
    f.net({
        {a, "Y"},
        {b, "A"}
    });
  }
  Legroute_options opt;
  opt.clock_charge   = false;
  opt.capacity_share = .1;
  opt.global_rounds  = 2;
  const auto plan    = legroute_plan(f.tech, f.fp, f.timing, opt);
  EXPECT_GT(plan.overflow_tracks + plan.unrouted, 0u);
  EXPECT_GT(plan.residual(), 0u);
  EXPECT_EQ(plan.overflow_edges, plan.overflow_edges_h + plan.overflow_edges_v);
  EXPECT_EQ(plan.overflow_tracks, plan.overflow_tracks_h + plan.overflow_tracks_v);
  EXPECT_EQ(plan.component_pressure[fixed], 1);
  EXPECT_GT(plan.hot_tiles, 0u);
  EXPECT_EQ(plan.component_hot_distance[fixed], std::numeric_limits<unsigned>::max());
  EXPECT_EQ(plan.component_hot_distance[resident], 0u);
  EXPECT_GT(plan.component_hot_distance[distant], 2u);
  EXPECT_LT(plan.component_hot_distance[distant], std::numeric_limits<unsigned>::max());
  EXPECT_GT(plan.tile_width_dbu, 0);
  EXPECT_GT(plan.tile_height_dbu, 0);
  EXPECT_TRUE(std::ranges::any_of(plan.component_pressure, [](double value) { return value > 1; }));
  // Search exhaustion is recorded, but does not manufacture inflation pressure.
  const auto limited = legroute_plan(f.tech, f.fp, f.timing, opt, {}, nullptr, 1);
  EXPECT_EQ(limited.search_limited, 12u);
  EXPECT_EQ(limited.unrouted, 12u);
  EXPECT_GT(limited.hot_tiles, 0u);  // movement feedback, while bounded search still creates no inflation pressure
  EXPECT_TRUE(std::ranges::all_of(limited.component_pressure, [](double value) { return value == 1; }));
}

TEST(LegroutePlan, ReportsOverflowPerDirectionAndCapacityPerLayer) {
  Fixture f(200, 16);
  f.cell("dff", 0, 3500);         // a flop sets the tile width
  for (int i = 0; i < 12; ++i) {  // twelve nets along one tile row: horizontal demand only
    const auto a = f.cell("inv", 100, 0), b = f.cell("inv", 2500, 0);
    f.net({
        {a, "Y"},
        {b, "A"}
    });
  }
  Legroute_options opt;
  opt.clock_charge   = false;
  opt.capacity_share = .1;
  opt.global_rounds  = 0;  // one pass: a full edge is closed, so nothing overflows and nets are left unrouted
  const auto closed  = legroute_plan(f.tech, f.fp, f.timing, opt);
  EXPECT_EQ(closed.overflow_tracks, 0u);
  EXPECT_GT(closed.unrouted, 0u);
  EXPECT_EQ(closed.residual(), closed.unrouted);
  opt.global_rounds = 2;
  opt.global_margin = 0;  // the nets cannot leave their row: the negotiation ends with horizontal overflow
  Legroute_congestion map;
  const auto          plan = legroute_plan(f.tech, f.fp, f.timing, opt, {}, nullptr, 100000, &map);
  EXPECT_EQ(plan.unrouted, 0u);
  EXPECT_GT(plan.overflow_tracks_h, 0u);
  EXPECT_EQ(plan.overflow_tracks_v, 0u);
  EXPECT_EQ(plan.overflow_edges_v, 0u);
  EXPECT_EQ(plan.overflow_tracks, plan.overflow_tracks_h);
  EXPECT_EQ(plan.residual(), plan.overflow_tracks + plan.overflow_edges);
  EXPECT_EQ(map.cols, plan.cols);
  EXPECT_EQ(map.rows, plan.rows);
  EXPECT_DOUBLE_EQ(map.admission_share, .1);
  EXPECT_GT(*std::ranges::max_element(map.horizontal), 1);
  // The routed stack and what each layer gives over 13 x 8 tiles. A track keeps one pitch inside the die:
  // 198 M3 and 98 M5 tracks cross each of the 8 tile rows, 78 M4 and 38 M6 tracks each of the 13 columns.
  ASSERT_EQ(plan.layers.size(), 4u);
  EXPECT_EQ(plan.cols, 13);
  EXPECT_EQ(plan.rows, 8);
  EXPECT_EQ(plan.layers[0].name, "M3");
  EXPECT_FALSE(plan.layers[0].horizontal);
  EXPECT_EQ(plan.layers[0].capacity_tracks, 198u * 8);
  EXPECT_EQ(plan.layers[1].name, "M4");
  EXPECT_TRUE(plan.layers[1].horizontal);
  EXPECT_EQ(plan.layers[1].capacity_tracks, 78u * 13);
  EXPECT_EQ(plan.layers[2].capacity_tracks, 98u * 8);
  EXPECT_EQ(plan.layers[3].capacity_tracks, 38u * 13);
  rapidjson::Document json;
  json.Parse(legroute_plan_json(plan).c_str());
  ASSERT_FALSE(json.HasParseError());
  EXPECT_EQ(json["overflow_tracks"].GetUint64(), plan.overflow_tracks);
  EXPECT_EQ(json["overflow"]["horizontal"]["tracks"].GetUint64(), plan.overflow_tracks_h);
  EXPECT_EQ(json["overflow"]["vertical"]["edges"].GetUint64(), 0u);
  EXPECT_EQ(json["residual"].GetUint64(), plan.residual());
  ASSERT_EQ(json["layers"].Size(), 4u);
  EXPECT_STREQ(json["layers"][1]["name"].GetString(), "M4");
  EXPECT_STREQ(json["layers"][1]["direction"].GetString(), "horizontal");
  EXPECT_EQ(json["layers"][1]["capacity_tracks"].GetUint64(), 78u * 13);
}

TEST(LegroutePlan, ClockChargeAndPowerMetalComeOffTheCapacity) {
  Fixture f(64, 8);
  f.random_design(6, 3);
  Legroute_options opt;
  opt.global_rounds = 0;
  Legroute_congestion with_clock, without_clock, with_power;
  (void)legroute_plan(f.tech, f.fp, f.timing, opt, {}, nullptr, 100000, &with_clock);
  opt.clock_charge = false;
  (void)legroute_plan(f.tech, f.fp, f.timing, opt, {}, nullptr, 100000, &without_clock);
  ASSERT_EQ(with_clock.limit_v.size(), without_clock.limit_v.size());
  bool less = false;
  for (size_t k = 0; k < with_clock.limit_v.size(); ++k) {
    EXPECT_LE(with_clock.limit_v[k], without_clock.limit_v[k]);
    EXPECT_LE(with_clock.limit_h[k], without_clock.limit_h[k]);
    less |= with_clock.limit_v[k] < without_clock.limit_v[k];
  }
  EXPECT_TRUE(less);  // a spine track per tile at least
  // a planned power stripe (not yet in the floorplan) takes its M4 track across the die
  Legroute_power power;
  power.obstacles.push_back({
      "M4",
      {0, 1065, 3200, 1085}
  });
  ph::Net vdd{"VDD", {}};
  vdd.use = "POWER";
  power.power_nets.push_back(vdd);
  const auto plan = legroute_plan(f.tech, f.fp, f.timing, opt, power, nullptr, 100000, &with_power);
  EXPECT_FALSE(plan.pg_missing);
  size_t fewer = 0;
  for (size_t k = 0; k < with_power.limit_h.size(); ++k) {
    EXPECT_LE(with_power.limit_h[k], without_clock.limit_h[k]);
    fewer += with_power.limit_h[k] < without_clock.limit_h[k];
  }
  EXPECT_GT(fewer, 0u);
  EXPECT_EQ(with_power.limit_v, without_clock.limit_v);
}

TEST(LegroutePlan, PatternSeedsReportTheirWork) {
  Fixture    f(64, 4);
  const auto a = f.cell("dff", 100, 0), b = f.cell("dff", 2500, 500);
  f.net({
      {a, "Q"},
      {b, "D"}
  });
  Legroute_options opt;
  opt.clock_charge = false;
  const auto maze  = legroute_plan(f.tech, f.fp, f.timing, opt);
  EXPECT_EQ(maze.pattern_attempts, 0u);
  EXPECT_GT(maze.search_pops, 0u);
  opt.global_pattern = true;
  const auto plan    = legroute_plan(f.tech, f.fp, f.timing, opt);
  EXPECT_EQ(plan.residual(), 0u);
  EXPECT_EQ(plan.pattern_attempts, 1u);
  EXPECT_EQ(plan.pattern_completed, 1u);
  EXPECT_EQ(plan.pattern_fallbacks, 0u);
  EXPECT_GT(plan.pattern_work, 0u);
  EXPECT_EQ(plan.search_pops, 0u);
}

TEST(LegroutePlan, ReportsTopologyDisconnectionSeparatelyFromBudgetExhaustion) {
  Fixture    f(100, 8);
  const auto a = f.cell("inv", 0, 0), b = f.cell("inv", 3000, 0), c = f.cell("inv", 4500, 1000);
  f.net({
      {a, "Y"},
      {b, "A"}
  });
  f.net({
      {b, "Y"},
      {c, "A"}
  });
  Legroute_options options;
  options.clock_charge   = false;
  options.capacity_share = .001;  // Every shared edge rounds down to zero capacity.
  options.global_rounds  = 4;
  const auto plan        = legroute_plan(f.tech, f.fp, f.timing, options, {}, nullptr, 1);
  EXPECT_EQ(plan.signals, 2u);
  EXPECT_EQ(plan.unrouted, 2u);
  EXPECT_EQ(plan.topology_disconnected, 2u);
  EXPECT_EQ(plan.search_limited, 0u);
  EXPECT_EQ(plan.search_pops, 0u);
  ASSERT_EQ(plan.failures.size(), 2u);
  for (const auto& failure : plan.failures) {
    EXPECT_TRUE(failure.topology_disconnected);
    EXPECT_FALSE(failure.search_limited);
    EXPECT_EQ(failure.last_attempt_pops, 0u);
    EXPECT_EQ(failure.search_pops, 0u);
    EXPECT_EQ(failure.search_attempts, 1u);
  }
  // an unresolved net marks the tiles of its cells, and pushes them apart (it is not a budget stop)
  EXPECT_EQ(plan.component_hot_distance[a], 0u);
  EXPECT_EQ(plan.component_hot_distance[c], 0u);
  EXPECT_DOUBLE_EQ(plan.component_pressure[a], 1.5);
  const auto path = std::filesystem::path(testing::TempDir()) / "legroute_plan_topology.tsv";
  write_legroute_plan_failures(plan, path.string());
  std::ifstream file(path);
  std::string   line;
  ASSERT_TRUE(bool(std::getline(file, line)));
  EXPECT_TRUE(line.ends_with("search_limited\ttopology_disconnected\tlast_attempt_pops\tsearch_pops\tsearch_attempts"));
  for (size_t k = 0; k < plan.failures.size(); ++k) {
    ASSERT_TRUE(bool(std::getline(file, line)));
    EXPECT_TRUE(line.ends_with("\t0\t1\t0\t0\t1"));
  }
  EXPECT_FALSE(bool(std::getline(file, line)));
  std::filesystem::remove(path);
}

TEST(LegroutePlan, FailuresKeepOriginalNetIdsAndActualRoutingEndpoints) {
  Fixture    f(100, 8);
  const auto ff = f.cell("dff", 4500, 1000);
  const auto a = f.cell("inv", 0, 0), nearby = f.cell("inv", 100, 0), far = f.cell("inv", 3000, 0);
  f.net(
      {
          {  a, "Y"},
          {far, "A"}
  },
      1,
      "POWER");
  f.net(
      {
          { a,   "Y"},
          {ff, "CLK"}
  },
      1,
      "CLOCK");
  f.net({
      {     a, "Y"},
      {nearby, "A"}
  });  // one endpoint tile, no search needed
  f.net({
      {   far, "Y"},
      {     a, "A"},
      {nearby, "A"}
  });
  f.net({
      {  a, "Y"},
      {far, "A"}
  });
  f.fp.nets[3].name = "hard_a";
  f.fp.nets[4].name = "hard_b";
  Legroute_options options;
  options.clock_charge  = false;
  options.global_rounds = 0;
  const auto plan       = legroute_plan(f.tech, f.fp, f.timing, options, {}, nullptr, 1);
  EXPECT_EQ(plan.signals, 3u);
  EXPECT_EQ(plan.unrouted, 2u);
  ASSERT_EQ(plan.failures.size(), plan.unrouted);
  for (size_t k = 0; k < plan.failures.size(); ++k) {
    const auto& failure = plan.failures[k];
    EXPECT_EQ(failure.net_index, k + 3);
    EXPECT_EQ(failure.net_name, f.fp.nets[k + 3].name);
    EXPECT_EQ(failure.pin_count, k ? 2u : 3u);
    EXPECT_EQ(failure.endpoint_tiles, 2u);
    EXPECT_GT(failure.bbox_width_tiles, 0);
    EXPECT_EQ(failure.bbox_height_tiles, 0);
    EXPECT_TRUE(failure.search_limited);
    EXPECT_FALSE(failure.topology_disconnected);
    EXPECT_EQ(failure.last_attempt_pops, 1u);
    EXPECT_EQ(failure.search_pops, 1u);
    EXPECT_EQ(failure.search_attempts, 1u);
  }
  const auto path = std::filesystem::path(testing::TempDir()) / "legroute_plan_failures.tsv";
  write_legroute_plan_failures(plan, path.string());
  std::ifstream            file(path);
  std::vector<std::string> lines;
  for (std::string line; std::getline(file, line);) {
    lines.push_back(std::move(line));
  }
  ASSERT_EQ(lines.size(), 3u);
  EXPECT_TRUE(lines[0].starts_with("net_index\tnet_name\tpin_count\tendpoint_tiles\t"));
  EXPECT_TRUE(lines[1].starts_with("3\thard_a\t3\t2\t"));
  EXPECT_TRUE(lines[2].starts_with("4\thard_b\t2\t2\t"));
  std::filesystem::remove(path);
}

TEST(LegroutePlan, RejectsInvalidOptionsAndUnsupportedFrames) {
  Fixture f(64, 4);
  f.cell("dff", 0, 0);
  for (double share : {0., -1., 1.5, double(NAN)}) {
    Legroute_options options;
    options.capacity_share = share;
    EXPECT_THROW(validate_legroute_options(options), std::invalid_argument);
    EXPECT_THROW(legroute_plan(f.tech, f.fp, f.timing, options), std::invalid_argument);
  }
  Legroute_options options;
  options.rib_tiles = 0;
  EXPECT_THROW(validate_legroute_options(options), std::invalid_argument);
  options = {};
  // a stack with one direction above the escape layer cannot be routed
  ph::Tech_profile profile;
  profile.route.access        = {"M1", "M4", "V3"};
  profile.route.signal_layers = {"M4", "M5"};
  EXPECT_THROW(legroute_plan(f.tech, f.fp, f.timing, options, {}, &profile), std::invalid_argument);
  auto no_rows = f.fp;
  no_rows.rows.clear();
  EXPECT_THROW(legroute_plan(f.tech, no_rows, f.timing, options), std::invalid_argument);
}
}  // namespace livehd::placement

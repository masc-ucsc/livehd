// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include <filesystem>
#include <fstream>

#include "gtest/gtest.h"
#include "legroute_internal.hpp"
#include "prects_networks.hpp"

namespace livehd::placement::lr {
namespace {
struct Fixture {
  physical::Technology tech;
  physical::Floorplan  fp;
  Legroute_options     options;
  timing::Feedback     timing;
  Design               d{tech, fp, options};
  Grid                 g;
  Fixture(unsigned columns = 2) {
    fp.design                  = "critical_cellbox_fixture";
    fp.dbu                     = 1;
    options.critical_cellboxes = true;
    options.cellbox_sites      = 22;
    timing.time_unit_ps        = 1;
    g.cols                     = int(columns);
    g.rows                     = 1;
    g.rows_per_tile            = 4;
    g.sites = g.tile_w = 22;
    g.row_h = g.site = 1;
    g.tile_h         = 4;
    g.key.assign(columns, 0);
    g.keys = {
        {{0, 0, 22}, {1, 0, 22}, {2, 0, 22}, {3, 0, 22}}
    };
    g.blocked.assign(columns, 0);
    g.box_short.assign(columns, 0);
    g.row_orient.assign(4, physical::Orientation::N);
    g.row_present.assign(4, 1);
    g.regions = 1;
    g.region.assign(columns, 0);
    for (const auto& name : {"M2", "M3"}) {
      Track_resource_layer layer;
      layer.name = name;
      layer.bands.resize(1);
      for (int i = 0; i < 20; ++i) {
        layer.bands[0].push_back(i);
      }
      layer.tiles.resize(columns);
      for (auto& tile : layer.tiles) {
        tile.cellbox_budget = 20;
      }
      g.resources.layers.push_back(std::move(layer));
    }
  }
  int cell(std::string name, int width, int x = 0) {
    auto& macro      = tech.macros[name];
    macro.width      = width;
    macro.height     = 1;
    macro.klass      = "CORE";
    macro.symmetry_x = macro.symmetry_y = true;
    const int c                         = int(fp.components.size());
    Ann_place pose(x, 0, width, 1);
    pose.status = physical::Status::PLACED;
    fp.components.push_back({name, name, pose});
    d.macro.push_back(&macro);
    d.movable.push_back(c);
    d.cell.push_back(c);
    d.width.push_back(width);
    d.tall.push_back(false);
    d.flop.push_back(false);
    d.nets_of.emplace_back();
    return c;
  }
  void net(std::initializer_list<int> cells, bool outside = false) {
    const int n = int(d.nets.size());
    Net       net;
    net.frame = fp.nets.size();
    physical::Net source;
    source.name = std::to_string(n);
    unsigned p  = 0;
    for (int c : cells) {
      net.pins.push_back({.comp = c});
      source.connections.push_back({fp.components[size_t(c)].name, "p" + std::to_string(p++)});
      if (d.nets_of[size_t(c)].empty() || d.nets_of[size_t(c)].back() != n) {
        d.nets_of[size_t(c)].push_back(n);
      }
    }
    if (outside) {
      net.pins.push_back({.term = 0});
      source.connections.push_back({"PIN", "io"});
    }
    net.routed = net.pins.size() >= 2;
    d.nets.push_back(net);
    fp.nets.push_back(source);
  }
};
}  // namespace

TEST(LegrouteAdmission, PhysicalMultiplicityAndInternalization) {
  Fixture f;
  f.cell("a", 5);
  f.cell("b", 5);
  f.net({0, 0, 1}, true);
  auto a = admit_critical_cellboxes(f.d, f.g, f.timing);
  ASSERT_EQ(a.slots.size(), 1u);
  EXPECT_EQ(a.slots[0].external_pins, 3u);
  EXPECT_EQ(a.slots[0].connections, 2u);
  f.d.nets[0].pins.pop_back();
  f.fp.nets[0].connections.pop_back();
  a = admit_critical_cellboxes(f.d, f.g, f.timing);
  EXPECT_EQ(a.slots[0].external_pins, 0u);
  EXPECT_EQ(a.slots[0].connections, 2u);
  EXPECT_EQ(a.slots[0].internal_nets, 1u);
}

TEST(LegrouteAdmission, RowFitIsNotAreaAndPressureResets) {
  Fixture f;
  f.options.cellbox_critical_choices = 1;
  for (int i = 0; i < 5; ++i) {
    f.cell(std::to_string(i), 12);
  }
  const auto a = admit_critical_cellboxes(f.d, f.g, f.timing);
  ASSERT_EQ(a.unassigned.size(), 1u);
  EXPECT_EQ(a.slots[0].cells.size(), 4u);
  for (double p : a.component_pressure) {
    EXPECT_GT(p, 1);
  }
  f.fp.components[4].place.x = 25;
  const auto b               = admit_critical_cellboxes(f.d, f.g, f.timing);
  EXPECT_TRUE(b.unassigned.empty());
  EXPECT_EQ(b.slots.size(), 2u);
  for (double p : b.component_pressure) {
    EXPECT_EQ(p, 1);
  }
  EXPECT_EQ(f.d.width, (std::vector<int64_t>{12, 12, 12, 12, 12}));
}

TEST(LegrouteAdmission, NearestChoicesDoNotSkipPopulatedBlockers) {
  Fixture f(5);
  for (int i = 0; i < 17; ++i) {
    f.cell(std::to_string(i), 22);
  }
  const auto a = admit_critical_cellboxes(f.d, f.g, f.timing);
  ASSERT_EQ(a.unassigned.size(), 1u);
  EXPECT_EQ(a.slots.size(), 4u);
  EXPECT_TRUE(std::ranges::all_of(a.slots, [](const auto& b) { return b.tile < 4; }));
  EXPECT_TRUE(std::ranges::all_of(a.unknown, [](char x) { return x; }));
}

TEST(LegrouteAdmission, WideSingletonConsumesTwoExistingSlots) {
  Fixture f(3);
  f.cell("wide", 30);
  f.cell("small", 5);
  const auto a = admit_critical_cellboxes(f.d, f.g, f.timing);
  ASSERT_TRUE(a.unassigned.empty());
  ASSERT_EQ(a.slots.size(), 2u);
  EXPECT_EQ(a.wide_exceptions, 1u);
  EXPECT_EQ(a.slots[0].span, 2);
  EXPECT_EQ(a.slots[0].cells.size(), 1u);
  EXPECT_EQ(a.slots[1].tile, 2);
  EXPECT_EQ(f.g.tile_w, 22);
}

TEST(LegrouteAdmission, MissingNativeTracksAndWideHorizontalIntersectionRejectAdmission) {
  Fixture f;
  f.cell("wide", 30);
  // Each half has twenty nominal H coordinates, but the second half loses one
  // physical track. A wide reservation cannot promise that H across both.
  f.g.resources.layers[0].exclude(1, 0, Track_exclusion::missing_def_track);
  const auto a = admit_critical_cellboxes(f.d, f.g, f.timing);
  EXPECT_EQ(a.unassigned.size(), 1u);
  EXPECT_TRUE(a.slots.empty());
  EXPECT_GT(a.rejected_resources, 0u);
}

TEST(LegrouteAdmission, TimingOrderRebuiltFromExactSnapshot) {
  Fixture f;
  f.cell("a", 5);
  f.cell("b", 5);
  f.cell("zero", 5);
  f.net({0}, true);
  f.net({1}, true);
  f.timing.pins["a:p0"].slack = -1;
  f.timing.pins["b:p0"].slack = -2;
  auto a                      = admit_critical_cellboxes(f.d, f.g, f.timing);
  EXPECT_EQ(a.order[0], 1);
  EXPECT_FALSE(a.unknown[0]);
  EXPECT_TRUE(a.unknown[2]);
  f.timing.pins["a:p0"].slack = -3;
  a                           = admit_critical_cellboxes(f.d, f.g, f.timing);
  EXPECT_EQ(a.order[0], 0);
  EXPECT_EQ(a.boxes.box_of.size(), 3u);
}

TEST(LegrouteAdmission, WholeBodyPressureIncludesCriticalOverlapButNotEdgeTouchOrFixedCells) {
  Fixture f(3);
  f.cell("wide", 30, 0);  // center is in tile 0; body also overlaps tile 1
  f.cell("critical", 5, 24);
  f.cell("edge_only", 5, 44);
  f.cell("fixed", 5, 25);
  f.fp.components[3].place.status = physical::Status::FIXED;
  f.net({1}, true);
  f.d.nets[0].critical = true;
  const auto original  = f.fp.components;
  auto       pressure  = cellbox_body_pressure(f.d, f.g, {1, 1.4, 1});
  EXPECT_DOUBLE_EQ(pressure[0], 1.4);
  EXPECT_DOUBLE_EQ(pressure[1], 1.4);
  EXPECT_DOUBLE_EQ(pressure[2], 1);
  EXPECT_DOUBLE_EQ(pressure[3], 1);
  pressure = cellbox_body_pressure(f.d, f.g, {1, 1, 1});
  for (double p : pressure) {
    EXPECT_DOUBLE_EQ(p, 1);
  }
  EXPECT_EQ(f.fp.components, original);
}

TEST(LegrouteAdmission, FinalRetryFarFallbackPreservesMembershipAndExtendsNearestFirst) {
  Fixture f(5);
  for (int i = 0; i < 17; ++i) {
    f.cell(std::to_string(i), 22);
  }
  const auto near = admit_critical_cellboxes(f.d, f.g, f.timing);
  ASSERT_EQ(near.unassigned.size(), 1u);
  f.options.cellbox_far_fallback = true;
  const auto early               = admit_critical_cellboxes(f.d, f.g, f.timing);
  EXPECT_EQ(early.unassigned, near.unassigned);
  EXPECT_FALSE(early.fallback_attempted);
  f.options.cellbox_final_retry = true;
  const auto far                = admit_critical_cellboxes(f.d, f.g, f.timing);
  EXPECT_TRUE(far.unassigned.empty());
  EXPECT_TRUE(far.fallback_attempted);
  EXPECT_EQ(far.fallback_assigned, 1u);
  EXPECT_EQ(far.regular_unassigned, near.unassigned);
  ASSERT_EQ(far.regular_slots.size(), near.slots.size());
  for (size_t i = 0; i < near.slots.size(); ++i) {
    EXPECT_EQ(far.regular_slots[i].cells, near.slots[i].cells);
  }
  for (size_t m = 0; m < near.boxes.box_of.size(); ++m) {
    if (near.boxes.box_of[m] >= 0) {
      EXPECT_EQ(far.boxes.box_of[m], near.boxes.box_of[m]);
    }
  }
  const size_t m = size_t(near.unassigned[0]);
  EXPECT_EQ(far.near_limit[m], 4u);
  EXPECT_EQ(far.candidate_rank[m], 5u);
  EXPECT_EQ(far.slots[size_t(far.boxes.box_of[m])].tile, 4);
  EXPECT_GT(far.candidate_distance[m], 0);
  for (double p : far.component_pressure) {
    EXPECT_DOUBLE_EQ(p, 1);  // no stale near-failure pressure
  }
  EXPECT_EQ(f.d.width, (std::vector<int64_t>(17, 22)));
}

TEST(LegrouteAdmission, FarFallbackKeepsPolicyAndNativeReservationChecks) {
  Fixture f(5);
  for (int i = 0; i < 17; ++i) {
    f.cell(std::to_string(i), 22);
  }
  f.options.cellbox_far_fallback = f.options.cellbox_final_retry = true;
  f.g.resources.layers[0].exclude(4, 0, Track_exclusion::missing_def_track);
  auto failed = admit_critical_cellboxes(f.d, f.g, f.timing);
  EXPECT_EQ(failed.unassigned.size(), 1u);
  EXPECT_GT(failed.rejected_resources, failed.regular_rejected_resources);
  Fixture p(5);
  p.cell("external", 5);
  p.net({0}, true);
  p.options.cellbox_external_pins = 0;
  p.options.cellbox_far_fallback = p.options.cellbox_final_retry = true;
  failed                                                         = admit_critical_cellboxes(p.d, p.g, p.timing);
  EXPECT_EQ(failed.unassigned.size(), 1u);
  EXPECT_TRUE(failed.slots.empty());
  EXPECT_GT(failed.rejected_policy, failed.regular_rejected_policy);
}

TEST(LegrouteAdmission, CompletedFarAdmissionStillRunsAndCanFailGlobalRouting) {
  Fixture f(5);
  for (int i = 0; i < 17; ++i) {
    f.cell("cell_" + std::to_string(i), 22);
  }
  // Keep the same integer geometry while using supported physical DEF units.
  f.fp.dbu = f.tech.dbu = 1000;
  for (auto& [name, macro] : f.tech.macros) {
    (void)name;
    macro.width  /= 1000;
    macro.height /= 1000;
  }
  f.fp.die             = {0, 0, 110, 4};
  f.tech.sites["site"] = {.001, .001, true};
  for (int row = 0; row < 4; ++row) {
    f.fp.rows.push_back({"r" + std::to_string(row), "site", 0, row, 110, 1});
  }
  const auto near = admit_critical_cellboxes(f.d, f.g, f.timing);
  ASSERT_EQ(near.unassigned.size(), 1u);
  f.net({0, near.unassigned[0]});
  f.fp.nets.back().name          = "connected";
  f.options.cellbox_connections  = 0;
  f.options.cellbox_far_fallback = f.options.cellbox_final_retry = true;
  f.g.layers.push_back({"M4", "", true, 1, 0, 1});
  f.g.cap = {
      {0, 0, 0, 0, 0}
  };
  const auto directory = std::filesystem::path(::testing::TempDir()) / "legroute_far_admission";
  std::filesystem::remove_all(directory);
  f.options.output_dir = directory.string();
  const auto blocked   = critical_cellbox_plan(f.d, f.g, f.timing, 100, nullptr);
  EXPECT_TRUE(blocked.fit_matched);
  EXPECT_TRUE(blocked.route_attempted);
  EXPECT_EQ(blocked.unrouted, 1u);
  EXPECT_TRUE(std::ranges::any_of(blocked.component_pressure, [](double p) { return p > 1; }));
  EXPECT_TRUE(std::filesystem::exists(directory / "critical_cellboxes_before_fallback.json"));
  const auto witness = physical::read_def((directory / "assigned_witness.def").string());
  EXPECT_EQ(witness.components.size(), f.fp.components.size());
  for (const auto& c : witness.components) {
    EXPECT_LE(c.place.x, 88);
  }
  f.options.output_dir.clear();
  f.g.cap = {
      {3, 3, 3, 3, 3}
  };
  const auto clear = critical_cellbox_plan(f.d, f.g, f.timing, 100, nullptr);
  EXPECT_TRUE(clear.route_attempted);
  EXPECT_EQ(clear.unrouted, 0u);
  for (double p : clear.component_pressure) {
    EXPECT_DOUBLE_EQ(p, 1);
  }
  std::filesystem::remove_all(directory);
}

TEST(LegrouteAdmission, PlanUsesResidualCapacityAndRejectsRankingOnly) {
  Fixture f;
  f.cell("a", 5);
  f.cell("b", 5);
  f.net({0, 1});
  f.options.cellbox_connections = 0;
  f.g.layers.push_back({"M4", "", true, 1, 0, 1});
  f.g.cap = {
      {3, 3}
  };
  const auto good = critical_cellbox_plan(f.d, f.g, f.timing, 100, nullptr);
  EXPECT_TRUE(good.route_attempted);
  EXPECT_EQ(good.unrouted, 0u);
  EXPECT_TRUE(std::ranges::all_of(good.component_pressure, [](double p) { return p == 1; }));
  f.g.cap = {
      {0, 0}
  };
  const auto blocked = critical_cellbox_plan(f.d, f.g, f.timing, 100, nullptr);
  EXPECT_TRUE(blocked.route_attempted);
  EXPECT_EQ(blocked.unrouted, 1u);
  EXPECT_TRUE(std::ranges::all_of(blocked.component_pressure, [](double p) { return p > 1; }));
  f.timing.ranking_only = true;
  EXPECT_THROW(critical_cellbox_plan(f.d, f.g, f.timing, 100, nullptr), std::invalid_argument);
}

TEST(LegrouteAdmission, NativeControlTraceExcludesBufferTrunksButPreservesCellsAndData) {
  Fixture f;
  f.cell("buffer", 5);
  f.cell("inverter", 5);
  f.cell("logic", 5);
  f.tech.sites["site"] = {1, 1, true};
  for (int row = 0; row < 4; ++row) {
    f.fp.rows.push_back({"r" + std::to_string(row), "site", 0, row, 44, 1});
  }
  for (auto& [name, macro] : f.tech.macros) {
    (void)name;
    macro.pins["A"].direction = "INPUT";
    macro.pins["Y"].direction = "OUTPUT";
    macro.pins["D"].direction = "INPUT";
  }
  physical::Terminal ctrl, data;
  ctrl.name      = "control";
  ctrl.direction = "INPUT";
  data.name      = "data";
  data.direction = "INPUT";
  f.fp.terminals = {ctrl, data};
  f.fp.nets      = {
      {    "root", {{"PIN", "control"}, {"buffer", "A"}}},
      {  "middle",  {{"buffer", "Y"}, {"inverter", "A"}}},
      {    "leaf",   {{"inverter", "Y"}, {"logic", "A"}}},
      {"ordinary",     {{"PIN", "data"}, {"logic", "D"}}},
  };
  Prects_library roles;
  roles.override_controls = true;
  roles.ports.insert("control");
  roles.links["buffer"] = {
      {"A", "Y"}
  };
  roles.links["inverter"] = {
      {"A", "Y"}
  };
  f.options.cellbox_ignored_nets = prects_networks(f.tech, f.fp, roles).ignored;
  EXPECT_EQ(f.options.cellbox_ignored_nets, (std::vector<bool>{true, true, true, false}));
  const auto d = build_design(f.tech, f.fp, f.timing, f.options, nullptr);
  const auto a = admit_critical_cellboxes(d, f.g, f.timing);
  ASSERT_TRUE(a.unassigned.empty());
  ASSERT_EQ(a.slots.size(), 1u);
  EXPECT_EQ(a.slots[0].cells.size(), 3u);
  EXPECT_EQ(a.slots[0].external_pins, 1u);
  EXPECT_EQ(a.slots[0].connections, 0u);
  EXPECT_EQ(d.nets.size(), 4u);
  EXPECT_FALSE(d.nets[0].routed);
  EXPECT_TRUE(d.nets[3].routed);
}
}  // namespace livehd::placement::lr

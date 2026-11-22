// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "clock_hold.hpp"

#include "gtest/gtest.h"

namespace livehd::placement {
TEST(ClockHold, FullPinAccessBinReservesOnlyIntersectingSites) {
  std::vector<Row_gaps> rows{
      {0, 0, 0, 1, 1, 6, {{0, 6}}},
      {1, 0, 1, 1, 1, 6, {{0, 6}}}
  };
  Satlegalize_route_grid access;
  access.bounds = {0, 0, 6, 2};
  access.bins_x = access.bins_y = 2;
  access.pin_utilization        = {0.9, 0, 1, 0.5};
  reserve_hold_pin_access(rows, access);
  EXPECT_EQ(rows[0].free.at(0), 3);
  EXPECT_EQ(rows[1].free.at(0), 6);
}

TEST(ClockHold, RanksRecoverableHoldPerPhysicalAreaAcrossSharedLoads) {
  physical::Technology tech;
  for (const auto& name : {"BUF", "DELAY"}) {
    auto& m = tech.macros[name];
    m.width = m.height    = 1;
    m.pins["A"].direction = "INPUT";
  }
  tech.macros.at("DELAY").width = 2;
  physical::Floorplan fp;
  fp.components = {
      {"x", "BUF", {}},
      {"y", "BUF", {}},
      {"z", "BUF", {}}
  };
  fp.nets = {
      {"single",             {{"PIN", "a"}, {"x", "A"}}},
      {"shared", {{"PIN", "b"}, {"y", "A"}, {"z", "A"}}}
  };
  pcrs::Clock_driver fast{"BUF", "A", "Y"}, slow{"DELAY", "A", "Y"};
  fast.intrinsic_ps = 10;
  slow.intrinsic_ps = 30;
  liberty::timing::Library  timing;
  const Hold_repair_options opts{
      2,
      timing,
      {&fast, &slow}
  };
  Endpoint_hold hold{
      {"x:A", -100},
      {"y:A",  -27},
      {"z:A",  -19}
  };
  auto choices = rank_hold_delays(fp, tech, opts, hold, 2);
  ASSERT_EQ(choices.size(), 4u);
  EXPECT_EQ(choices.front().net, 1u);
  EXPECT_EQ(choices.front().driver, &slow);
  EXPECT_DOUBLE_EQ(choices.front().gain_per_area, 23);
  const Endpoint_hold setup{
      {"x:A", 90},
      {"y:A",  0},
      {"z:A",  0}
  };
  auto ranked = rank_hold_delays(fp, tech, opts, hold, 2, &setup);
  ASSERT_EQ(ranked.size(), 2u);
  EXPECT_EQ(ranked.front().net, 0u);
  auto refill = rank_hold_delays(fp,
                                 tech,
                                 opts,
                                 hold,
                                 2,
                                 &setup,
                                 {
                                     {0, ranked.front().driver->master}
  });
  ASSERT_EQ(refill.size(), 1u);
  EXPECT_NE(refill.front().driver, ranked.front().driver);
  choices = rank_hold_delays(fp, tech, opts, hold, 1);
  ASSERT_EQ(choices.size(), 2u);
  EXPECT_EQ(choices.front().driver, &fast);
  fp.nets[1].use  = "CLOCK";
  slow.max_fanout = 1;
  choices         = rank_hold_delays(fp, tech, opts, hold, 2);
  ASSERT_EQ(choices.size(), 2u);
  EXPECT_EQ(choices.front().net, 0u);
  EXPECT_TRUE(rank_hold_delays(fp, tech, opts, hold, .5).empty());
}
TEST(ClockHold, InsertsIntoFreeLegalSiteAndMovesOnlyFailingLoads) {
  physical::Technology tech;
  tech.dbu           = 100;
  tech.sites["core"] = {1, 1, true};
  auto& master       = tech.macros["BUF"];
  master.width = master.height = 1;
  master.site                  = "core";
  master.klass                 = "CORE";
  master.pins["A"].direction   = "INPUT";
  master.pins["Y"].direction   = "OUTPUT";
  master.pins["A"].shapes.push_back({
      "M1",
      {.4, .4, .6, .6}
  });
  physical::Floorplan fp;
  fp.dbu  = 100;
  fp.die  = {0, 0, 500, 200};
  fp.rows = {
      {"bottom", "core", 0,   0, 5, 100,  physical::Orientation::N},
      {   "top", "core", 0, 100, 5, 100, physical::Orientation::FS}
  };
  // Zero footprint fields are typical after DEF import. LEF supplies the master dimensions.
  for (const auto& [name, x] : std::vector<std::pair<std::string, int>>{
           {  "drive",   0},
           {"failing", 100},
           {  "clean", 200}
  }) {
    Ann_place pose(x, 0, 0, 0);
    pose.status = physical::Status::PLACED;
    fp.components.push_back({name, "BUF", pose});
  }
  fp.blockages.push_back({false, {}, {{300, 0, 400, 100}}});
  fp.nets.push_back({
      "data",
      {{"drive", "Y"}, {"failing", "A"}, {"clean", "A"}}
  });
  fp.snapshot_ndr.net_rules["data"] = "wide";
  pcrs::Clock_driver buffer{"BUF", "A", "Y"};
  Endpoint_hold      hold{
           {"failing:A", -2},
           {  "clean:A",  4}
  };
  auto edit = plan_hold_delay(fp, tech, buffer, hold, 0);
  ASSERT_TRUE(edit);
  EXPECT_EQ(edit->cell.place.x, 400);
  EXPECT_EQ(edit->cell.place.y, 0);
  EXPECT_DOUBLE_EQ(edit->area_um2, 1);
  EXPECT_DOUBLE_EQ(edit->hpwl_delta_um, 5);
  insert_hold_delay(fp, *edit);
  EXPECT_EQ(fp.components.size(), 4);
  EXPECT_EQ(fp.nets.size(), 2);
  EXPECT_EQ(fp.nets[0].connections,
            (std::vector<physical::Connection>{
                {        "drive", "Y"},
                {        "clean", "A"},
                {edit->cell.name, "A"}
  }));
  EXPECT_EQ(fp.nets[1].connections,
            (std::vector<physical::Connection>{
                {edit->cell.name, "Y"},
                {      "failing", "A"}
  }));
  EXPECT_EQ(fp.snapshot_ndr.net_rules.at(edit->new_net), "wide");
  EXPECT_FALSE(plan_hold_delay(fp, tech, buffer, hold, 1));  // Remaining free row has illegal orientation.
  master.symmetry_x = true;
  fp.nets[1].use    = "CLOCK";
  EXPECT_FALSE(plan_hold_delay(fp, tech, buffer, hold, 1));
}
}  // namespace livehd::placement

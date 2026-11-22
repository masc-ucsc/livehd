// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "gui_clock.hpp"

#include <cmath>
#include <string>

#include "gtest/gtest.h"
#include "gui_render.hpp"
#include "gui_wire.hpp"
#include "rapidjson/document.h"

using namespace livehd::placement::gui;

namespace {
// clk port -> root buffer -> two leaf buffers; leaf A drives two flops, leaf B one flop and one that sits far away.
// Node order: 0 clk (io), 1 root, 2 leafA, 3 leafB, 4 fa0, 5 fa1, 6 fb0, 7 fb1, 8 a data cell.
Scene tree_scene() {
  Scene s;
  s.dbu_per_um    = 1000;
  s.die           = {0, 0, 100000, 100000};
  s.phase         = "final";
  const auto node = [&](const std::string& name, Kind kind, const std::string& master, bool seq) {
    Node n;
    n.name   = name;
    n.kind   = kind;
    n.master = master;
    n.seq    = seq;
    n.w = n.h = 0;  // positions below are centres
    s.nodes.push_back(n);
  };
  node("clk", Kind::io, "PIN", false);
  node("root", Kind::cell, "BUFx4", false);
  node("leafA", Kind::cell, "BUFx2", false);
  node("leafB", Kind::cell, "BUFx2", false);
  node("fa0", Kind::cell, "DFF", true);
  node("fa1", Kind::cell, "DFF", true);
  node("fb0", Kind::cell, "DFF", true);
  node("fb1", Kind::cell, "DFF", true);
  node("logic", Kind::cell, "NAND2", false);
  const auto net = [&](const std::string& name, int driver, std::vector<int> sinks) {
    Clock_net n;
    n.name   = name;
    n.driver = driver;
    n.sinks  = std::move(sinks);
    s.clock_nets.push_back(n);
  };
  net("leafA", 2, {4, 5});  // deliberately not in tree order
  net("clk", 0, {1});
  net("root", 1, {2, 3});
  net("leafB", 3, {6, 7});
  s.build_index();
  return s;
}
Frame tree_frame() {
  Frame f;
  f.id = 7;
  f.x  = {50000, 50000, 20000, 80000, 10000, 30000, 80000, 80000, 50000};
  f.y  = {0, 50000, 50000, 50000, 50000, 60000, 60000, 90000, 10000};
  return f;
}
const char* kHandoff = R"({"mode":"tree","leaf_schedule":{"fb0:CLK":1},"networks":[{"source_instance":"PIN","source_pin":"clk",
 "branches":[
  {"instance":"leafA","master":"BUFx2","leaf":true,"drive_steps":0,"children":["fa0:CLK","fa1:CLK"],
   "estimated_sink_arrivals_ps":[40.0,41.5],"load_ff":4.0,"estimated_stage_delay_ps":20.0,"estimated_far_slew_ps":30.0},
  {"instance":"leafB","master":"BUFx2","leaf":true,"drive_steps":1,"children":["fb0:CLK","fb1:CLK"],
   "estimated_sink_arrivals_ps":[44.0,52.0],"load_ff":6.0,"estimated_stage_delay_ps":24.0,"estimated_far_slew_ps":55.0},
  {"instance":"root","master":"BUFx4","leaf":false,"children":["leafA","leafB"],"load_ff":9.0,
   "estimated_stage_delay_ps":15.0,"estimated_far_slew_ps":12.0},
  {"instance":"gone","master":"BUFx2","leaf":true,"children":["nobody:CLK"]}]}]})";

rapidjson::Document ask(const Scene& s, const Frame& f, const std::string& request) {
  rapidjson::Document q, reply;
  q.Parse(request.c_str());
  reply.Parse(clock_query(s, f, q).c_str());
  EXPECT_FALSE(reply.HasParseError()) << request;
  return reply;
}
}  // namespace

TEST(GuiClock, TreeIsDerivedFromNetsInAnyOrder) {
  const auto s    = tree_scene();
  const auto tree = clock_tree(s);
  EXPECT_EQ(tree.parent, (std::vector<int>{2, -1, 1, 2}));
  EXPECT_EQ(tree.level, (std::vector<int>{2, 0, 1, 2}));
  EXPECT_EQ(tree.root, (std::vector<int>{1, 1, 1, 1}));
  EXPECT_EQ(tree.driven[1], 2);    // the root buffer drives net "root"
  EXPECT_EQ(tree.driven[4], -1);   // a flop is an endpoint
  EXPECT_EQ(tree.load_of[8], -1);  // a data cell is not on the clock
}

TEST(GuiClock, TopologyAloneReportsWireAndSaysEstimatesAreMissing) {
  const auto s = tree_scene();
  const auto r = ask(s, tree_frame(), R"({"cmd":"clock"})");
  ASSERT_TRUE(r["ok"].GetBool());
  EXPECT_TRUE(r["built"].GetBool());
  ASSERT_EQ(r["domains"].Size(), 1u);
  const auto& d = r["domains"][0];
  EXPECT_STREQ(d["root"].GetString(), "clk");
  EXPECT_EQ(d["buffers"].GetUint(), 3u);
  EXPECT_EQ(d["endpoints"].GetUint(), 4u);
  EXPECT_EQ(d["leaves"].GetUint(), 2u);
  EXPECT_EQ(d["depth"]["min"].GetInt(), 2);
  EXPECT_EQ(d["depth"]["max"].GetInt(), 2);
  // clk->root 50, root->leaves 30+30, leafA 10+(10+10), leafB (0+10)+(0+40): micrometres at the frame.
  EXPECT_DOUBLE_EQ(d["star_um"].GetDouble(), 50 + 60 + 30 + 50);
  EXPECT_TRUE(d["insertion"]["skew_ps"].IsNull());
  EXPECT_EQ(d["insertion"]["known"].GetUint(), 0u);
  EXPECT_NE(std::string(r["note"].GetString()).find("topology only"), std::string::npos);
}

TEST(GuiClock, HandoffEstimatesSurviveTheProcessBoundary) {
  auto s = tree_scene();
  EXPECT_EQ(annotate_clock(s, kHandoff, "/w/pcrs_clock.json"), 3u);  // "gone" names nothing in the scene
  EXPECT_EQ(s.clock_schedule.at("fb0:CLK"), 1);
  const auto frame = tree_frame();
  // The flow and the controller are separate processes: what the agent reads is the decoded copy.
  const auto bytes = wire::pack(s);
  Scene      copy  = wire::read<Scene>(bytes);
  ASSERT_EQ(copy.clock_nets.size(), 4u);
  EXPECT_EQ(copy.clock_report, "/w/pcrs_clock.json");
  EXPECT_TRUE(std::isnan(copy.clock_nets[1].arrival_ps[0]));  // the port's load has no estimate: unknown, not zero

  const auto  r = ask(copy, frame, R"({"cmd":"clock"})");
  const auto& d = r["domains"][0];
  EXPECT_DOUBLE_EQ(d["insertion"]["min_ps"].GetDouble(), 40);
  EXPECT_DOUBLE_EQ(d["insertion"]["max_ps"].GetDouble(), 52);
  EXPECT_DOUBLE_EQ(d["insertion"]["skew_ps"].GetDouble(), 12);
  EXPECT_EQ(d["insertion"]["known"].GetUint(), 4u);
  EXPECT_DOUBLE_EQ(d["worst_slew_ps"].GetDouble(), 55);
  EXPECT_STREQ(r["worst_nets"][0]["driver"].GetString(), "leafB");

  const auto late = ask(copy, frame, R"({"cmd":"clock","what":"sinks","limit":1})");
  EXPECT_STREQ(late["sinks"][0]["pin"].GetString(), "fb1:CLK");
  EXPECT_DOUBLE_EQ(late["sinks"][0]["driver_distance_um"].GetDouble(), 40);

  const auto nets = ask(copy, frame, R"({"cmd":"clock","what":"nets","sort":"skew","limit":1})");
  EXPECT_STREQ(nets["nets"][0]["driver"].GetString(), "leafB");
  EXPECT_DOUBLE_EQ(nets["nets"][0]["arrival"]["skew_ps"].GetDouble(), 8);
  EXPECT_FALSE(ask(copy, frame, R"({"cmd":"clock","what":"nets","sort":"bogus"})")["ok"].GetBool());
  EXPECT_FALSE(ask(copy, frame, R"({"cmd":"clock","what":"nets","root":"nope"})")["ok"].GetBool());
}

TEST(GuiClock, PairNamesTheSharedStageAndTheSignOfSkew) {
  auto s = tree_scene();
  annotate_clock(s, kHandoff, "h");
  const auto frame = tree_frame();
  const auto cross = ask(s, frame, R"({"cmd":"clock","what":"pair","a":"fa0","b":"fb1:CLK"})");
  ASSERT_TRUE(cross["ok"].GetBool());
  EXPECT_STREQ(cross["common_driver"].GetString(), "root");
  EXPECT_EQ(cross["a"]["private_stages"].GetInt(), 1);
  EXPECT_EQ(cross["b"]["private_stages"].GetInt(), 1);
  EXPECT_DOUBLE_EQ(cross["skew_ps"].GetDouble(), 12);  // b later than a
  const auto same = ask(s, frame, R"({"cmd":"clock","what":"pair","a":"#5","b":"fa0"})");
  EXPECT_STREQ(same["common_driver"].GetString(), "leafA");
  EXPECT_EQ(same["a"]["private_stages"].GetInt(), 0);
  EXPECT_DOUBLE_EQ(same["skew_ps"].GetDouble(), -1.5);
  EXPECT_FALSE(ask(s, frame, R"({"cmd":"clock","what":"pair","a":"logic","b":"fa0"})")["ok"].GetBool());
}

TEST(GuiClock, SkewMapIsLocalAndEmptyBinsAreNull) {
  auto s = tree_scene();
  annotate_clock(s, kHandoff, "h");
  // Two bins per axis: fa0 (10,50), fa1 (30,60), fb0 (80,60) and fb1 (80,90) all land in the upper half.
  const auto m = ask(s, tree_frame(), R"({"cmd":"clock","what":"map","metric":"sinks","bins":2})");
  ASSERT_TRUE(m["ok"].GetBool());
  ASSERT_EQ(m["v"].Size(), 4u);
  EXPECT_TRUE(m["v"][0].IsNull());             // x low, y low: no endpoint
  EXPECT_DOUBLE_EQ(m["v"][1].GetDouble(), 2);  // x low, y high: fa0, fa1
  EXPECT_TRUE(m["v"][2].IsNull());
  EXPECT_DOUBLE_EQ(m["v"][3].GetDouble(), 2);
  const auto skew = ask(s, tree_frame(), R"({"cmd":"clock","what":"map","bins":2})");
  EXPECT_DOUBLE_EQ(skew["max"].GetDouble(), 12);  // neighbours count: 40 next to 52
  EXPECT_TRUE(skew["v"][0].IsNull());
  // 20 um bins: the two leaf-B flops are neighbours (44 against 52); leaf A is three bins away and does not count.
  const auto fine = ask(s, tree_frame(), R"({"cmd":"clock","what":"map","bins":5})");
  EXPECT_DOUBLE_EQ(fine["max"].GetDouble(), 8);
  // 10 um bins: no two endpoints are neighbours any more, so no bin sees a spread.
  EXPECT_DOUBLE_EQ(ask(s, tree_frame(), R"({"cmd":"clock","what":"map","bins":10})")["max"].GetDouble(), 0);
}

TEST(GuiClock, EditsStageExistingInputsAndApplyExtendsTheBuiltSchedule) {
  auto s = tree_scene();
  annotate_clock(s, kHandoff, "h");
  Clock_edits         edits;
  bool                changed = false;
  rapidjson::Document q, r;
  const auto          edit = [&](const std::string& request) {
    q.Parse(request.c_str());
    r.Parse(clock_edit(s, edits, q, changed).c_str());
    return r["ok"].GetBool();
  };
  EXPECT_TRUE(edit(R"({"cmd":"clock_edit","op":"drive","leaf":"leafA","step":2})"));
  EXPECT_TRUE(changed);
  EXPECT_EQ(edits.drive,
            (std::map<std::string, int>{
                {"fa0:CLK", 2}
  }));  // one pin stands for the leaf
  // A second request on the same leaf, named by another of its pins, replaces the first: no conflict is ever staged.
  EXPECT_TRUE(edit(R"({"cmd":"clock_edit","op":"drive","pins":["fa1"],"step":1})"));
  EXPECT_EQ(edits.drive,
            (std::map<std::string, int>{
                {"fa0:CLK", 1}
  }));
  EXPECT_FALSE(edit(R"({"cmd":"clock_edit","op":"drive","leaf":"root","step":1})"));     // a trunk is not scheduled
  EXPECT_FALSE(edit(R"({"cmd":"clock_edit","op":"drive","pins":["logic"],"step":1})"));  // not on the clock
  EXPECT_FALSE(edit(R"({"cmd":"clock_edit","op":"drive","leaf":"leafA","step":-1})"));
  EXPECT_FALSE(changed);
  EXPECT_TRUE(edit(R"({"cmd":"clock_edit","op":"arrival","pins":["fb1:CLK"],"wanted_ps":-8})"));
  EXPECT_NE(std::string(r["note"].GetString()).find("balance"), std::string::npos);  // says what else it needs
  EXPECT_TRUE(edit(R"({"cmd":"clock_edit","op":"option","key":"synth.place.pcrs.balance","value":true})"));
  EXPECT_FALSE(edit(R"({"cmd":"clock_edit","op":"option","key":"clock","value":false})"));  // not a tree knob
  EXPECT_FALSE(edit(R"({"cmd":"clock_edit","op":"option","key":"leaf_fanout","value":"1 --set x=y"})"));

  const auto restart = clock_restart(s, edits, "/w/place");
  EXPECT_EQ(restart.set.at("synth.place.pcrs.balance"), "true");
  EXPECT_EQ(restart.set.at("synth.place.pcrs.leaf_schedule"), "/w/place/gui_clock_leaf_schedule.json");
  EXPECT_EQ(restart.set.at("synth.place.pcrs.arrival_targets"), "/w/place/gui_clock_arrival_targets.json");
  ASSERT_EQ(restart.files.size(), 2u);
  rapidjson::Document schedule, targets;
  schedule.Parse(restart.files[0].second.c_str());
  EXPECT_EQ(schedule["fa0:CLK"].GetUint(), 1u);
  EXPECT_EQ(schedule["fb0:CLK"].GetUint(), 1u);  // the schedule the tree was built with stays
  targets.Parse(restart.files[1].second.c_str());
  EXPECT_DOUBLE_EQ(targets["fb1:CLK"]["wanted_ps"].GetDouble(), -8);

  // Rescheduling a leaf the built schedule already names replaces that entry, whichever pin stood for it.
  EXPECT_TRUE(edit(R"({"cmd":"clock_edit","op":"drive","pins":["fb1"],"step":3})"));
  schedule.Parse(clock_restart(s, edits, "/w").files[0].second.c_str());
  EXPECT_EQ(schedule["fb0:CLK"].GetUint(), 3u);
  EXPECT_FALSE(schedule.HasMember("fb1:CLK"));

  EXPECT_TRUE(edit(R"({"cmd":"clock_edit","op":"remove","leaf":"leafB"})"));
  EXPECT_TRUE(edits.wanted_ps.empty());
  EXPECT_TRUE(edit(R"({"cmd":"clock_edit","op":"clear"})"));
  EXPECT_TRUE(edits.empty());
}

TEST(GuiClock, NoTreeIsAnAnswerNotAFailure) {
  Scene s;
  s.dbu_per_um = 1000;
  s.die        = {0, 0, 1000, 1000};
  const Frame f;
  const auto  r = ask(s, f, R"({"cmd":"clock"})");
  EXPECT_TRUE(r["ok"].GetBool());
  EXPECT_FALSE(r["built"].GetBool());
  EXPECT_FALSE(ask(s, f, R"({"cmd":"clock","what":"sinks"})")["ok"].GetBool());
  Clock_edits         edits;
  bool                changed = false;
  rapidjson::Document q;
  q.Parse(R"({"op":"drive","leaf":0,"step":1})");
  EXPECT_NE(clock_edit(s, edits, q, changed).find("\"ok\":false"), std::string::npos);
  // A cycle in a malformed scene must not hang the controller.
  Clock_net a, b;
  a.driver = 0;
  a.sinks  = {1};
  b.driver = 1;
  b.sinks  = {0};
  s.nodes.resize(2);
  s.clock_nets = {a, b};
  EXPECT_EQ(clock_tree(s).parent, (std::vector<int>{1, 0}));
}

TEST(GuiClock, ScreenshotLayerDrawsTheTree) {
  auto s = tree_scene();
  annotate_clock(s, kHandoff, "h");
  Render_options plain;
  plain.width = plain.height = 200;
  plain.comb = plain.regs = plain.io = plain.centroids = false;
  auto with_clock                                      = parse_layers("clock", plain);
  EXPECT_TRUE(with_clock.clock);
  EXPECT_NE(render_png(s, tree_frame(), {}, plain), render_png(s, tree_frame(), {}, with_clock));
}

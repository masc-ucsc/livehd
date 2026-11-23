// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "centroid_record.hpp"

#include <limits>
#include <map>

#include "dreamplace/native/placement.hpp"
#include "gtest/gtest.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"
namespace dp = DreamPlace::native;
using namespace livehd::placement;
namespace {
Centroid parse(const char* text, const Centroid_names& names = {}) {
  rapidjson::Document doc;
  doc.Parse(text);
  EXPECT_FALSE(doc.HasParseError());
  return parse_centroid(doc, names);
}
std::string dump(const Centroid& c) {
  rapidjson::Document doc;
  write_centroid(c, doc, doc.GetAllocator());
  rapidjson::StringBuffer                    buffer;
  rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
  doc.Accept(writer);
  return buffer.GetString();
}
// Six nodes of 4x2 on a 1000x1000 die: names f0..f5, centers spread so a layout has something to move.
struct Fixture {
  dp::Design                 d;
  std::map<std::string, int> index;
  Centroid_compile_options   options;
  Fixture() {
    d.layout      = {0, 0, 1000, 1000, 1, 1};
    d.num_movable = 6;
    for (int i = 0; i < 6; ++i) {
      d.node_x.push_back(100 + 150 * i);
      d.node_y.push_back(200 + 40 * (i % 2));
      d.node_size_x.push_back(4);
      d.node_size_y.push_back(2);
      index["f" + std::to_string(i)] = i;
    }
    d.build_maps();
    d.validate();
    options.dbu              = 1;
    options.default_strength = 1;
    options.resolve          = [this](const std::string& name) {
      const auto it = index.find(name);
      return it == index.end() ? -1 : it->second;
    };
  }
};
Centroid centroid(std::string id, std::vector<std::string> flops, std::vector<std::string> nested = {}) {
  Centroid c;
  c.id = std::move(id);
  for (auto& f : flops) {
    c.members.push_back({std::move(f), false});
  }
  for (auto& n : nested) {
    c.members.push_back({std::move(n), true});
  }
  return c;
}
}  // namespace

TEST(CentroidRecord, ParseWriteRoundTrip) {
  const auto c = parse(R"({"id":"g","kind":"centroid","members":["u/a","u/b",{"centroid":"h"}],
    "position":{"mode":"relative","to":"h","dx_um":3,"dy_um":-2},
    "layout":{"kind":"array","columns":2,"gap_x_um":0.5,"gap_y_um":0.25},"strength":0.5,
    "clock":{"drive":"BUFx4","arrival_ps":120},"generated":true,"origin":"pcrs"})");
  EXPECT_EQ(c.members.size(), 3u);
  EXPECT_TRUE(c.members[2].centroid);
  EXPECT_EQ(c.layout, Centroid_layout_kind::array);
  EXPECT_EQ(c.columns, 2);
  EXPECT_EQ(c.to, "h");
  EXPECT_DOUBLE_EQ(*c.clock.arrival_ps, 120);
  EXPECT_TRUE(c.generated);
  EXPECT_EQ(parse(dump(c).c_str()), c);
}
TEST(CentroidRecord, ParseRejectsMalformedRecords) {
  for (const char* bad : {R"({"kind":"centroid","members":["a"]})",
                          R"({"id":"g","members":[]})",
                          R"({"id":"g","members":[7]})",
                          R"({"id":"g","members":["a"],"strength":-1})",
                          R"({"id":"g","members":["a"],"position":{"mode":"at"}})",
                          R"({"id":"g","members":["a"],"layout":{"kind":"spiral"}})",
                          R"({"id":"g","members":["a"],"position":{"mode":"relative"}})",
                          R"({"id":"g","members":["../a"]})",
                          R"({"id":"g","members":[{"centroid":""}]})"}) {
    EXPECT_THROW(parse(bad), std::invalid_argument) << bad;
  }
}
TEST(CentroidRecord, QualifiesMembersAndIdsThroughTheScope) {
  Centroid_names names;
  names.member   = [](const std::vector<std::string>& p) { return "bank0." + p.back(); };
  names.centroid = [](const std::string& id) { return "t:bank0:" + id; };
  const auto c   = parse(R"({"id":"g","members":["x/f0",{"centroid":"h"}],"position":{"mode":"relative","to":"h"}})", names);
  EXPECT_EQ(c.id, "t:bank0:g");
  EXPECT_EQ(c.members[0].name, "bank0.f0");
  EXPECT_EQ(c.members[1].name, "t:bank0:h");
  EXPECT_EQ(c.to, "t:bank0:h");
}
TEST(CentroidSet, ValidationNamesEveryProblem) {
  Centroid_set set;
  ASSERT_TRUE(set.add(centroid("a", {"f0", "f1"}, {"b"})));
  ASSERT_TRUE(set.add(centroid("b", {"f2"})));
  EXPECT_TRUE(set.validate().empty());
  ASSERT_TRUE(set.add(centroid("dup", {"f2"})));  // f2 belongs to b
  ASSERT_TRUE(set.add(centroid("lost", {"f4"}, {"nowhere"})));
  auto cycle = centroid("c1", {"f5"}, {"c2"});
  ASSERT_TRUE(set.add(std::move(cycle)));
  ASSERT_TRUE(set.add(centroid("c2", {}, {"c1"})));
  auto moved        = centroid("p", {"f3"});
  moved.position    = Centroid_position_mode::at;
  Centroid_set more = set;
  ASSERT_TRUE(more.add(std::move(moved)));
  std::map<std::string, std::string> reasons;
  for (const auto& issue : more.validate()) {
    reasons[issue.id] = issue.reason;
  }
  EXPECT_TRUE(reasons.contains("dup"));
  EXPECT_TRUE(reasons.contains("lost"));
  EXPECT_TRUE(reasons.contains("c1") || reasons.contains("c2"));
  EXPECT_FALSE(reasons.contains("a"));
  EXPECT_FALSE(reasons.contains("b"));
  EXPECT_FALSE(reasons.contains("p"));
  // A position on a nested centroid, a relative position cycle and a nested centroid with two parents.
  Centroid_set tricky;
  tricky.add(centroid("top", {"f0"}, {"kid"}));
  auto kid     = centroid("kid", {"f1"});
  kid.position = Centroid_position_mode::at;
  tricky.add(kid);
  tricky.add(centroid("other", {"f2"}, {"kid"}));
  auto r1     = centroid("r1", {"f3"});
  r1.position = Centroid_position_mode::relative;
  r1.to       = "r2";
  auto r2     = centroid("r2", {"f4"});
  r2.position = Centroid_position_mode::relative;
  r2.to       = "r1";
  tricky.add(r1);
  tricky.add(r2);
  reasons.clear();
  for (const auto& issue : tricky.validate()) {
    reasons[issue.id] = issue.reason;
  }
  EXPECT_TRUE(reasons.contains("kid"));
  EXPECT_TRUE(reasons.contains("other"));
  EXPECT_TRUE(reasons.contains("r1"));
}
TEST(CentroidSet, EditsMoveMembersAndAdoptGeneratedRecords) {
  Centroid_set set;
  auto         gen = centroid("pcrs:0", {"f0", "f1"});
  gen.generated    = true;
  gen.origin       = "pcrs";
  ASSERT_TRUE(set.add(gen));
  ASSERT_TRUE(set.add(centroid("mine", {"f2"})));
  // Editing a generated centroid makes it authored and keeps its members.
  ASSERT_TRUE(set.add_member("pcrs:0", {"f3", false}));
  EXPECT_FALSE(set.find("pcrs:0")->generated);
  EXPECT_EQ(set.find("pcrs:0")->members.size(), 3u);
  // A member lives in one centroid: moving removes it from the previous one, and an emptied centroid goes away.
  ASSERT_TRUE(set.move_member("mine", "pcrs:0", {"f2", false}));
  EXPECT_EQ(set.find("mine"), nullptr);
  EXPECT_FALSE(set.move_member("pcrs:0", "nowhere", {"f2", false}));
  std::string reason;
  EXPECT_FALSE(set.remove_member("pcrs:0", {"zzz", false}, &reason));
  EXPECT_FALSE(reason.empty());
  ASSERT_TRUE(set.remove_member("pcrs:0", {"f3", false}));
  EXPECT_EQ(set.find("pcrs:0")->members.size(), 3u);
}
TEST(CentroidSet, GeneratedRecordsYieldToAuthoredOnes) {
  Centroid_set set;
  ASSERT_TRUE(set.add(centroid("mine", {"f0", "f1"})));
  auto old_gen      = centroid("pcrs:old", {"f4"});
  old_gen.generated = true;
  old_gen.origin    = "pcrs";
  ASSERT_TRUE(set.add(old_gen));
  const auto dropped
      = set.replace_generated("pcrs",
                              {centroid("pcrs:a", {"f0", "f2", "f3"}), centroid("mine", {"f5"}), centroid("pcrs:b", {"f1"})});
  EXPECT_EQ(set.find("pcrs:old"), nullptr);
  ASSERT_NE(set.find("pcrs:a"), nullptr);
  EXPECT_EQ(set.find("pcrs:a")->members.size(), 2u);  // f0 stayed with the authored centroid
  EXPECT_TRUE(set.find("pcrs:a")->generated);
  EXPECT_EQ(set.find("pcrs:b"), nullptr);  // its only member was authored elsewhere
  EXPECT_EQ(dropped.size(), 2u);
  EXPECT_FALSE(set.find("mine")->generated);
  EXPECT_EQ(set.find("mine")->members.size(), 2u);
}
TEST(CentroidSet, ClockFieldsAreInheritedFromTheContainingCentroid) {
  Centroid_set set;
  auto         outer   = centroid("outer", {"f0"}, {"mid"});
  outer.clock          = {"BUFx4", 100};
  auto mid             = centroid("mid", {"f1"}, {"leaf"});
  mid.clock.arrival_ps = 140;
  set.add(outer);
  set.add(mid);
  set.add(centroid("leaf", {"f2"}));
  const auto leaf = set.effective_clock("leaf");
  EXPECT_EQ(leaf.drive, "BUFx4");
  EXPECT_DOUBLE_EQ(*leaf.arrival_ps, 140);
  EXPECT_DOUBLE_EQ(*set.effective_clock("outer").arrival_ps, 100);
  std::vector<std::string> names;
  for (const auto& m : set.leaves("outer")) {
    names.push_back(m.name);
  }
  EXPECT_EQ(names, (std::vector<std::string>{"f0", "f1", "f2"}));
  EXPECT_EQ(set.parent_of("leaf"), "mid");
}
TEST(CentroidCompile, FloatingGroupIsPlainCohesion) {
  Fixture      f;
  Centroid_set set;
  set.add(centroid("g", {"f0", "f1", "f2"}));
  const auto out = compile_centroids(set, f.d, f.options);
  ASSERT_EQ(out.groups.size(), 1u);
  const auto& g = out.groups[0];
  EXPECT_EQ(g.nodes, (std::vector<int>{0, 1, 2}));
  EXPECT_DOUBLE_EQ(g.weight, 1);
  for (const auto& o : g.offsets) {
    EXPECT_EQ(o, (std::pair<double, double>{0, 0}));
  }
  EXPECT_FALSE(g.has_target);
  f.d.cohesion_groups = out.groups;
  f.d.validate();
}
TEST(CentroidCompile, RowColumnAndArrayLayoutsAreSlotsAroundTheCentroid) {
  Fixture      f;
  Centroid_set set;
  auto         row = centroid("row", {"f0", "f1", "f2"});
  row.layout       = Centroid_layout_kind::row;
  row.gap_x_um     = 6;
  auto col         = centroid("col", {"f3", "f4"});
  col.layout       = Centroid_layout_kind::column;
  col.gap_y_um     = 4;
  auto arr         = centroid("arr", {"f0", "f1", "f2", "f3"});
  arr.layout       = Centroid_layout_kind::array;
  arr.columns      = 2;
  arr.gap_x_um     = 1;
  arr.gap_y_um     = 1;
  Fixture g;  // separate fixture: the array reuses f0..f3, which the row owns
  set.add(row);
  set.add(col);
  auto out = compile_centroids(set, f.d, f.options);
  ASSERT_EQ(out.groups.size(), 2u);
  // Row: slots are 10 apart (4 wide + 6 gap) and centered; offsets are the negated slots.
  EXPECT_DOUBLE_EQ(out.groups[0].offsets[1].first - out.groups[0].offsets[0].first, -10);
  EXPECT_NEAR(out.groups[0].offsets[0].first + out.groups[0].offsets[1].first + out.groups[0].offsets[2].first, 0, 1e-9);
  for (const auto& o : out.groups[0].offsets) {
    EXPECT_DOUBLE_EQ(o.second, 0);
  }
  // Column: slots are 6 apart (2 tall + 4 gap).
  EXPECT_DOUBLE_EQ(out.groups[1].offsets[1].second - out.groups[1].offsets[0].second, -6);
  EXPECT_GT(out.layout_residual.at("row"), 0);
  Centroid_set only_array;
  only_array.add(arr);
  out = compile_centroids(only_array, g.d, g.options);
  ASSERT_EQ(out.groups.size(), 1u);
  const auto& o = out.groups[0].offsets;
  ASSERT_EQ(o.size(), 4u);
  EXPECT_DOUBLE_EQ(o[0].second, o[1].second);  // same row
  EXPECT_DOUBLE_EQ(o[0].first, o[2].first);    // same column
  EXPECT_DOUBLE_EQ(o[1].first - o[0].first, -5);
  EXPECT_DOUBLE_EQ(o[2].second - o[0].second, -3);
  g.d.cohesion_groups = out.groups;
  g.d.validate();
}
TEST(CentroidCompile, StrengthFallsBackToTheStageDefaultAndZeroEmitsNothing) {
  Fixture      f;
  Centroid_set set;
  auto         weak = centroid("weak", {"f0", "f1"});
  weak.strength     = .25;
  auto none         = centroid("none", {"f2", "f3"});
  none.strength     = 0;
  set.add(weak);
  set.add(none);
  set.add(centroid("dflt", {"f4", "f5"}));
  const auto out = compile_centroids(set, f.d, f.options);
  ASSERT_EQ(out.groups.size(), 2u);
  EXPECT_DOUBLE_EQ(out.groups[0].weight, .25);
  EXPECT_DOUBLE_EQ(out.groups[1].weight, 1);
  for (const auto& r : out.results) {
    if (r.id == "none") {
      EXPECT_EQ(r.group, -1);
      EXPECT_FALSE(r.reason.empty());
    }
  }
}
TEST(CentroidCompile, PositionAtPullsTheCentroidAndRelativeKeepsTwoBlocksApart) {
  Fixture f;
  f.options.dbu = 2;
  Centroid_set set;
  auto         a = centroid("a", {"f0", "f1"});
  a.position     = Centroid_position_mode::at;
  a.x_um         = 100;
  a.y_um         = 50;
  auto b         = centroid("b", {"f2", "f3"});
  b.position     = Centroid_position_mode::relative;
  b.to           = "a";
  b.dx_um        = 30;
  b.dy_um        = -10;
  set.add(a);
  set.add(b);
  const auto out = compile_centroids(set, f.d, f.options);
  ASSERT_EQ(out.groups.size(), 3u);  // a, b, and the pair
  EXPECT_TRUE(out.groups[0].has_target);
  EXPECT_DOUBLE_EQ(out.groups[0].target_x, 200);
  EXPECT_DOUBLE_EQ(out.groups[0].target_y, 100);
  EXPECT_FALSE(out.groups[1].has_target);
  const auto& pair = out.groups[2];
  EXPECT_EQ(pair.nodes, (std::vector<int>{2, 3, 0, 1}));
  // This centroid (b) sits D = (60, -20) DBU from a: the offsets of an a leaf and a b leaf differ by D when both blocks keep
  // the same shape (the two flops of each block are the same distance apart).
  EXPECT_NEAR(pair.offsets[2].first - pair.offsets[0].first, 60, 1e-9);
  EXPECT_NEAR(pair.offsets[2].second - pair.offsets[0].second, -20, 1e-9);
  f.d.cohesion_groups = out.groups;
  f.d.validate();
}
TEST(CentroidCompile, NestedCentroidIsABlockInItsParentsLayout) {
  Fixture      f;
  Centroid_set set;
  auto         parent = centroid("parent", {"f4"}, {"child"});
  parent.layout       = Centroid_layout_kind::row;
  parent.gap_x_um     = 10;
  set.add(parent);
  set.add(centroid("child", {"f0", "f1"}));
  const auto out = compile_centroids(set, f.d, f.options);
  ASSERT_EQ(out.groups.size(), 2u);
  ASSERT_EQ(out.results.size(), 2u);
  EXPECT_EQ(out.results[0].id, "parent");
  EXPECT_EQ(out.results[0].members, 2u);
  EXPECT_EQ(out.results[0].group, 1);
  // The child emits its own group first, the parent's covers all three leaves; both are legal together.
  EXPECT_EQ(out.groups[0].nodes.size(), 2u);
  EXPECT_EQ(out.groups[1].nodes.size(), 3u);
  f.d.cohesion_groups = out.groups;
  EXPECT_NO_THROW(f.d.validate());
  // Both child leaves keep the child's present shape (they are 150 apart) inside the parent's slot.
  const auto& o = out.groups[1].offsets;
  EXPECT_NEAR(o[1].first - o[2].first, 150, 1e-9);
}
TEST(CentroidCompile, UnresolvedMembersAndBadCentroidsAreReportedNotCompiled) {
  Fixture      f;
  Centroid_set set;
  set.add(centroid("partial", {"f0", "ghost", "f1"}));
  set.add(centroid("empty", {"ghost", "ghost2"}));
  set.add(centroid("broken", {"f2"}, {"missing"}));
  const auto out = compile_centroids(set, f.d, f.options);
  ASSERT_EQ(out.groups.size(), 1u);
  EXPECT_EQ(out.groups[0].nodes, (std::vector<int>{0, 1}));
  std::map<std::string, Centroid_result> by_id;
  for (const auto& r : out.results) {
    by_id[r.id] = r;
  }
  EXPECT_EQ(by_id["partial"].unresolved, 1u);
  EXPECT_EQ(by_id["empty"].group, -1);
  EXPECT_FALSE(by_id["empty"].reason.empty());
  EXPECT_EQ(by_id["broken"].group, -1);
}
TEST(CentroidCompile, ARowPullsMembersIntoALineInTheNativePlacer) {
  Fixture      f;
  Centroid_set set;
  auto         row = centroid("row", {"f0", "f1", "f2"});
  row.layout       = Centroid_layout_kind::row;
  row.gap_x_um     = 2;
  row.strength     = 1;
  set.add(row);
  auto out          = compile_centroids(set, f.d, f.options);
  auto d            = f.d;
  d.cohesion_groups = out.groups;
  d.build_maps();
  d.validate();
  dp::Options o;
  o.max_iterations = 200;
  o.bins_x = o.bins_y = 16;
  o.add_fillers       = false;
  o.legalize = o.detailed_place = false;
  o.initial_density_weight      = 1e-8;
  o.cohesion_weight             = 1;
  const double before           = std::abs(d.node_y[0] - d.node_y[1]) + std::abs(d.node_x[1] - d.node_x[0] - 150);
  dp::place(d, o);
  const double after = std::abs(d.node_y[0] - d.node_y[1]) + std::abs(d.node_y[1] - d.node_y[2]);
  EXPECT_LT(after, before);
  // Neighbors end near the slot pitch (4 wide + 2 gap) rather than where they started.
  EXPECT_LT(std::abs((d.node_x[1] - d.node_x[0]) - 6), std::abs(150 - 6));
}
TEST(CentroidSet, GeneratedRecordsBoundToNodesCompareAgainstAuthoredNames) {
  Fixture      f;
  Centroid_set set;
  ASSERT_TRUE(set.add(centroid("mine", {"f0"})));
  Centroid fresh;
  fresh.id = "pcrs:0";
  for (const int n : {0, 1, 2}) {
    Centroid_member m;
    m.node = n;
    fresh.members.push_back(m);
  }
  const auto dropped = set.replace_generated("pcrs", {fresh}, f.options.resolve);
  EXPECT_TRUE(dropped.empty());
  ASSERT_NE(set.find("pcrs:0"), nullptr);
  EXPECT_EQ(set.find("pcrs:0")->members.size(), 2u);  // node 0 is f0, owned by the authored centroid
  EXPECT_TRUE(set.validate().empty());
  // Node-bound members compile without a resolver, and the record exports with names from the caller.
  auto options    = f.options;
  options.resolve = nullptr;
  Centroid_set generated;
  generated.add(*set.find("pcrs:0"));
  const auto out = compile_centroids(generated, f.d, options);
  ASSERT_EQ(out.groups.size(), 1u);
  EXPECT_EQ(out.groups[0].nodes, (std::vector<int>{1, 2}));
  rapidjson::Document doc;
  write_centroid(*set.find("pcrs:0"), doc, doc.GetAllocator(), [](int node) { return "f" + std::to_string(node); });
  EXPECT_STREQ(doc["members"][0].GetString(), "f1");
  EXPECT_TRUE(doc["generated"].GetBool());
}
TEST(CentroidCompile, ShareScalesAMembersAreaInThePull) {
  Fixture      f;
  Centroid_set set;
  Centroid     c     = centroid("g", {"f0", "f1"});
  c.members[0].share = .25;
  set.add(c);
  const auto out = compile_centroids(set, f.d, f.options);
  ASSERT_EQ(out.groups.size(), 1u);
  EXPECT_DOUBLE_EQ(out.groups[0].areas[0], .25 * 8);
  EXPECT_DOUBLE_EQ(out.groups[0].areas[1], 8);
  rapidjson::Document doc;
  write_centroid(c, doc, doc.GetAllocator());
  EXPECT_DOUBLE_EQ(doc["members"][0]["share"].GetDouble(), .25);
  EXPECT_EQ(parse(dump(c).c_str()), c);
}
TEST(CentroidCompile, ALeafGroupInstalledAtACheckpointPullsWithoutTouchingTheNets) {
  dp::Design initial;
  initial.layout      = {0, 0, 100, 100, 1, 10};
  initial.num_movable = 4;
  initial.node_x      = {10, 30, 70, 90};
  initial.node_y.assign(4, 10);
  initial.node_size_x.assign(4, 2);
  initial.node_size_y.assign(4, 2);
  initial.pin2node = {0, 2};
  initial.pin_offset_x.assign(2, 1);
  initial.pin_offset_y.assign(2, 1);
  initial.flat_net2pin       = {0, 1};
  initial.flat_net2pin_start = {0, 2};
  initial.net_weights        = {4};
  initial.net_mask           = {1};
  initial.build_maps();
  // The flops 1 and 3 share a leaf with gate 2, which counts for half of its area; the whole group is a generated record.
  Centroid leaf;
  leaf.id        = "pcrs:1";
  leaf.generated = true;
  leaf.origin    = "pcrs";
  leaf.strength  = 4;
  for (const auto& [node, share] : {
           std::pair{1, 1.},
           std::pair{3, 1.},
           std::pair{2, .5}
  }) {
    Centroid_member m;
    m.node  = node;
    m.share = share;
    leaf.members.push_back(m);
  }
  Centroid_set set;
  ASSERT_TRUE(set.add(leaf));
  Centroid_compile_options compile;
  compile.default_strength = 1;
  const auto groups        = compile_centroids(set, initial, compile).groups;
  ASSERT_EQ(groups.size(), 1u);
  dp::Options options;
  options.max_iterations = options.min_iterations = 28;
  options.bins_x = options.bins_y = 8;
  options.add_fillers             = false;
  options.legalize = options.detailed_place = false;
  options.initial_density_weight            = 1e-8;
  options.checkpoint_interval               = 4;
  options.checkpoint_after_density          = false;
  options.cohesion_weight                   = 1;
  auto plain = initial, pulled = initial;
  options.checkpoint = [](const dp::Design&, int) { return dp::GpUpdate{}; };
  dp::place(plain, options);
  bool set_once = false, retained = false;
  options.checkpoint = [&](const dp::Design& view, int) {
    dp::GpUpdate update;
    if (!set_once) {
      update.cohesion_groups = groups;
      set_once               = true;
    } else if (view.cohesion_groups.size() == 1 && view.cohesion_groups[0].nodes == groups[0].nodes) {
      retained = true;
    }
    return update;
  };
  dp::place(pulled, options);
  EXPECT_TRUE(retained);
  EXPECT_LT(std::abs(pulled.node_x[1] - pulled.node_x[3]), std::abs(plain.node_x[1] - plain.node_x[3]));
  EXPECT_EQ(pulled.flat_net2pin, initial.flat_net2pin);
  EXPECT_EQ(pulled.net_weights, initial.net_weights);
}

TEST(CentroidSet, RemovingTheLastLeafAlsoRepairsIndirectReferences) {
  Centroid_set set;
  set.add(centroid("grandparent", {}, {"parent"}));
  set.add(centroid("parent", {}, {"leaf"}));
  set.add(centroid("leaf", {"f0"}));
  auto survivor     = centroid("survivor", {"f1"});
  survivor.position = Centroid_position_mode::relative;
  survivor.to       = "grandparent";
  set.add(survivor);
  ASSERT_TRUE(set.erase(set.find("leaf")->id));
  ASSERT_EQ(set.size(), 1u);
  EXPECT_EQ(set.items().front().position, Centroid_position_mode::free);
  EXPECT_TRUE(set.validate().empty());
}

TEST(CentroidSet, RejectsDuplicateChildInOneParent) {
  Centroid_set set;
  set.add(centroid("leaf", {"f0"}));
  set.add(centroid("parent", {}, {"leaf", "leaf"}));
  const auto issues = set.validate();
  ASSERT_EQ(issues.size(), 1u);
  EXPECT_EQ(issues.front().id, "parent");
}

TEST(CentroidCompile, ReloadKeepsBothLeafGroupsAndCountsTheSharedGateOnce) {
  Fixture         f;
  Centroid_set    set;
  auto            left  = centroid("pcrs:left", {"f0", "f3"});
  auto            right = centroid("pcrs:right", {"f1"});
  Centroid_member gate;
  gate.node  = 2;
  gate.share = .5;
  left.members.push_back(gate);
  right.members.push_back(gate);
  ASSERT_TRUE(set.replace_generated("pcrs", {left, right}).empty());
  Centroid_set authored;
  authored.add(centroid("manual", {"f3", "f4"}));
  set.reload_authored(authored, f.options.resolve);
  EXPECT_TRUE(set.validate().empty());
  const auto out = compile_centroids(set, f.d, f.options);
  ASSERT_EQ(out.groups.size(), 3u);
  EXPECT_EQ(out.groups[1].nodes, (std::vector<int>{0, 2}));
  EXPECT_EQ(out.groups[2].nodes, (std::vector<int>{1, 2}));
  EXPECT_DOUBLE_EQ(out.groups[1].areas[1] + out.groups[2].areas[1], 8.);
  right.members.back().share = .75;
  set.replace_generated("pcrs", {left, right}, f.options.resolve);
  EXPECT_FALSE(set.validate().empty());
}

TEST(CentroidCompile, UnusedArrayColumnsDoNotAllocateSlots) {
  Fixture      f;
  Centroid_set set;
  auto         row = centroid("row", {"f0", "f1"});
  row.layout       = Centroid_layout_kind::array;
  row.columns      = std::numeric_limits<int>::max();
  set.add(row);
  const auto out = compile_centroids(set, f.d, f.options);
  ASSERT_EQ(out.groups.size(), 1u);
  const auto& offsets = out.groups[0].offsets;
  ASSERT_EQ(offsets.size(), 2u);
  EXPECT_DOUBLE_EQ(offsets[0].second, offsets[1].second);
  EXPECT_DOUBLE_EQ(offsets[0].first - offsets[1].first, 4.);
}
TEST(CentroidClock, FieldsOfAContainingCentroidReachItsInstancesAndTheNearestWins) {
  Centroid_set set;
  auto         outer = centroid("outer", {"f0"}, {"inner"});
  outer.clock.arrival_ps  = 100;
  outer.clock.drive_steps = 1;
  auto inner              = centroid("inner", {"f1", "f2"});
  inner.clock.arrival_ps  = 140;  // the nearest setting wins; drive_steps is inherited
  set.add(outer);
  set.add(inner);
  set.add(centroid("free", {"f3"}));  // no clock fields: not listed
  const auto leaves = set.clock_leaves();
  ASSERT_EQ(leaves.size(), 3u);
  std::map<std::string, Centroid_clock> by_name;
  for (const auto& [m, k] : leaves) {
    by_name[m.name] = k;
  }
  EXPECT_DOUBLE_EQ(*by_name["f0"].arrival_ps, 100);
  EXPECT_DOUBLE_EQ(*by_name["f1"].arrival_ps, 140);
  EXPECT_EQ(*by_name["f2"].drive_steps, 1u);
  // As the clock builder's inputs: by the label of the clock pin, with explicit inputs winning.
  const auto labels = [](const std::string& name) { return name == "f2" ? std::string{} : name + ":CLK"; };
  const auto inputs = centroid_clock_inputs(set, labels, {{"f0:CLK", {std::nullopt, 5}}}, {});
  EXPECT_EQ(inputs.arrival.size(), 1u);  // f1; f0's arrival is explicit
  EXPECT_DOUBLE_EQ(inputs.arrival.at("f1:CLK").second, 140);
  EXPECT_FALSE(inputs.arrival.at("f1:CLK").first.has_value());
  EXPECT_EQ(inputs.schedule.size(), 2u);  // f0 and f1 get step 1; f2 has no clock pin
  EXPECT_EQ(inputs.unmatched, 1u);
  EXPECT_EQ(inputs.overridden, 1u);
  rapidjson::Document doc;
  write_centroid(outer, doc, doc.GetAllocator());
  EXPECT_EQ(doc["clock"]["drive_steps"].GetUint(), 1u);
  EXPECT_EQ(parse(dump(outer).c_str()), outer);
  EXPECT_THROW(parse(R"({"id":"g","members":["a"],"clock":{"drive_steps":-1}})"), std::invalid_argument);
}

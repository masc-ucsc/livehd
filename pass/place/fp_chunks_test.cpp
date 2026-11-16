// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "fp_chunks.hpp"

#include <filesystem>

#include "chunk_geometry.hpp"
#include "chunk_measure.hpp"
#include "chunk_refine.hpp"
#include "chunk_templates.hpp"
#include "gtest/gtest.h"

namespace livehd::placement {
namespace {
Chunk_plan tiled(std::initializer_list<Chunk_box> boxes) {
  Chunk_plan p;
  for (auto b : boxes) {
    int   j    = p.chunks.size();
    auto& c    = p.chunks.emplace_back();
    c.id       = std::to_string(j);
    c.box      = b;
    c.objects  = {j};
    c.reserved = 100.;
  }
  return p;
}
}  // namespace
TEST(FpChunks, CopyInterfacesRespectSignalIdentityAndOnePhysicalOrientation) {
  auto run = [](std::string orientation) {
    auto p                = tiled({
        { 0, 0, 20, 20},
        {40, 0, 60, 20}
    });
    p.chunks[0].partition = "left";
    p.chunks[1].partition = "right";
    std::vector<Interface_net> nets{
        {        "a", 1,   Chunk_point{0, 5}, {{0, "pin:D"}}, {}},
        {        "b", 2,  Chunk_point{70, 5}, {{1, "pin:D"}}, {}},
        {"unmatched", 3, Chunk_point{50, 30}, {{1, "pin:E"}}, {}}
    };
    std::vector<Chunk_object> objects(2);
    refine_templates(p,
                     nets,
                     objects,
                     {
    },
                     {0, 0, 60, 20},
                     {1, 1},
                     false,
                     {},
                     {{"0", "1", orientation}});
    return std::pair{std::move(p), std::move(nets)};
  };
  auto [natural, n] = run("N");
  auto [best, b]    = run("auto");
  const auto& c     = *best.chunks[1].interface_copy;
  EXPECT_EQ(c.orientation, "FN");
  EXPECT_EQ(c.matched, 1);
  EXPECT_EQ(c.source_ports, 1);
  EXPECT_EQ(c.target_ports, 2);
  EXPECT_EQ(b[1].contacts[0].copy_source_net, 1u);
  EXPECT_EQ(b[2].contacts[0].copy_source_net, SIZE_MAX);
  EXPECT_LT(best.port_hpwl, natural.port_hpwl);
  EXPECT_EQ(b[2].contacts[0].xy, n[2].contacts[0].xy);
  EXPECT_EQ(b[0].contacts[0].xy, n[0].contacts[0].xy);
  EXPECT_EQ(b[1].contacts[0].xy, (Chunk_point{60, 5}));
  EXPECT_EQ(best.chunks[1].box, natural.chunks[1].box);
}

TEST(FpChunks, MoveAndRetileRespectAreaAndLockedAspect) {
  auto                      original = tiled({
      { 0,  0,  50,  50},
      { 0, 50,  50, 100},
      {50,  0, 100, 100}
  });
  std::vector<Chunk_object> objects(3);
  objects[1].macro = true;
  objects[1].fixed = original.chunks[1].box;
  Chunk_edit move{
      .move = "0",
      .to   = {50, 50}
  };
  auto result = original;
  edit_chunk_boxes(result, {move}, objects, {0, 0, 100, 100}, {1, 1});
  EXPECT_EQ(result.chunks[0].box, (Chunk_box{50, 50, 100, 100}));
  EXPECT_EQ(result.chunks[1].box, original.chunks[1].box);
  EXPECT_EQ(result.chunks[2].box, (Chunk_box{0, 0, 100, 50}));
  EXPECT_TRUE(result.overlaps.empty());
  auto       locks_only = original;
  Chunk_edit all{
      .locks = {"0", "1", "2"},
      .lock  = true
  };
  EXPECT_THROW(edit_chunk_boxes(locks_only, {all}, objects, {0, 0, 100, 100}, {1, 1}), std::invalid_argument);
  locks_only = original;
  edit_chunk_boxes(locks_only, {Chunk_edit{.all_but = "1"}}, objects, {0, 0, 100, 100}, {1, 1});
  EXPECT_FALSE(locks_only.chunks[1].aspect_locked);
  EXPECT_TRUE(locks_only.chunks[0].aspect_locked && locks_only.chunks[2].aspect_locked);
  Chunk_edit lock{.locks = {"2"}, .lock = true};
  EXPECT_THROW(edit_chunk_boxes(original, {lock, move}, objects, {0, 0, 100, 100}, {1, 1}), std::invalid_argument);
}
TEST(FpChunks, OverlapRingUsesOnlyPhysicalAreaAndRejectsExcessDemand) {
  auto                      base = tiled({
      { 0,  0,  50,  50},
      {50,  0, 100,  50},
      {50, 50, 100, 100},
      { 0, 50,  50, 100}
  });
  std::vector<Chunk_object> objects(4);
  Chunk_edits               ring{
                    {.move = "0",   .to = {5, 0}, .overlap = .2},
                    {.move = "1",  .to = {50, 5}, .overlap = .2},
                    {.move = "2", .to = {45, 50}, .overlap = .2},
                    {.move = "3",  .to = {0, 45}, .overlap = .2}
  };
  auto plan = base;
  edit_chunk_boxes(plan, ring, objects, {0, 0, 100, 100}, {1, 1});
  ASSERT_EQ(plan.overlaps.size(), 4);
  EXPECT_EQ(plan.overlap_cycles, 1);
  double effective = 0;
  for (const auto& c : plan.chunks) {
    EXPECT_DOUBLE_EQ(c.shared_dbu2, 450);
    effective += 2500 - .5 * c.shared_dbu2;
  }
  EXPECT_DOUBLE_EQ(effective, 9100);
  // A unit-density probe covering the core should count the union, not all four
  // rectangles, both in the plan and after placement.
  Chunk_model model;
  model.objects = objects;
  plan.density.assign(4, 1);
  plan.density_source.resize(4);
  for (const auto& c : plan.chunks) {
    plan.seeds.push_back(c.box);
  }
  Chunk_measure occupancy(model.record(plan, "ring"));
  occupancy.cell({0, 0, 100, 100}, false, "");
  auto measured = occupancy.record("ring");
  for (auto& c : measured["chunks"].GetArray()) {
    EXPECT_DOUBLE_EQ(c["fill"].GetDouble(), 1);
    EXPECT_DOUBLE_EQ(c["cell_area_dbu2"].GetDouble(), 2275);
  }
  base.chunks[0].reserved = 2450;
  EXPECT_THROW(edit_chunk_boxes(base, {ring.front()}, objects, {0, 0, 100, 100}, {1, 1}), std::invalid_argument);
}
TEST(FpChunks, WholeColorsAndMacroMemory) {
  auto run = [](double width) {
    Chunk_model m;
    for (int i = 0; i < 10; ++i) {
      m.cell("large", "", "", false, 2, 1);
    }
    m.cell("small", "", "", false, 2, 1);
    m.cell("ram", "", "", true, width, 8);
    std::vector<int> net{0, 0, 1, 2};
    m.net(net, true);
    return m.plan({0, 0, 1000, 1000}, 1, 1, 30000, .7, nullptr, "top");
  };
  auto a = run(32), b = run(350);
  ASSERT_EQ(a.chunks.size(), 2);
  const auto& big = a.chunks[a.home[0]];
  EXPECT_EQ(big.objects.size(), 1);
  EXPECT_GT(big.bytes, 30000);
  EXPECT_EQ(big.allowance, big.bytes);
  EXPECT_EQ(a.home[1], a.home[2]);
  EXPECT_EQ(a.chunks[a.home[2]].allowance, 30000);
  EXPECT_EQ(a.chunks[a.home[2]].bytes, b.chunks[b.home[2]].bytes);
  EXPECT_GE(b.seeds[2][2] - b.seeds[2][0], 350);
  EXPECT_GT(b.chunks[b.home[2]].reserved, a.chunks[a.home[2]].reserved);
}
TEST(FpChunks, DenseHomeKeepsMacroSeedsDisjointWhenLogicWhitespaceIsDisconnected) {
  Chunk_model model;
  model.add_summary({.id = "logic", .cells = 3000, .area = 3000, .width = 1, .height = 1});
  auto       a = model.cell("ram_a", "", "", true, 60, 60);
  auto       b = model.cell("ram_b", "", "", true, 40, 80);
  // Both SRAMs fit side by side. The remaining 3200 sites are L-shaped:
  // splitting off 3000 as a single rectangle makes the SRAM tiling impossible.
  const auto p = model.plan({0, 0, 100, 100}, 1, 1, 0, 1, nullptr, "top");
  ASSERT_EQ(p.chunks.size(), 1);
  const auto& l = p.seeds[a];
  const auto& r = p.seeds[b];
  EXPECT_TRUE(l[2] <= r[0] || r[2] <= l[0] || l[3] <= r[1] || r[3] <= l[1]);
  for (auto i : {a, b}) {
    const auto& box = p.seeds[i];
    const auto& o   = model.objects[i];
    EXPECT_GE(box[0], 0);
    EXPECT_GE(box[1], 0);
    EXPECT_LE(box[2], 100);
    EXPECT_LE(box[3], 100);
    EXPECT_GE(box[2] - box[0], o.width);
    EXPECT_GE(box[3] - box[1], o.height);
  }
}
TEST(FpChunks, MultiTerminalNetCountsFullSpanOnce) {
  Chunk_model      model;
  std::vector<int> pins{model.cell("c", "", "", false, 1, 1)};
  model.net(pins, true, std::array{50., 50.}, "external", Chunk_box{0, 10, 100, 90});
  auto plan = model.plan({0, 0, 100, 100}, 1, 1, 0, .6, nullptr, "top");
  EXPECT_EQ(plan.crossing_nets, 1);
  EXPECT_EQ(plan.center_hpwl, 180);
  EXPECT_EQ(plan.port_hpwl, 180);
}
TEST(FpChunks, RepeatedCoresAndScopedGuidanceReplay) {
  Chunk_model m;
  for (std::string core : {"a", "b"}) {
    for (std::string color : {"x", "y"}) {
      m.cell(core + "." + color, core, "cpu", false, 10, 2);
    }
  }
  std::vector<int> net{0, 1, 2};
  m.net(net, true);
  auto run = [&](const metadata::Bundle* hints) { return m.plan({10, 20, 510, 320}, 1, 2, 30000, .6, hints, "top"); };
  EXPECT_EQ(m.plan({10, 20, 510, 320}, 1, 2, 100000, .6, nullptr, "top").chunks.size(), 1);
  auto base = run(nullptr);
  ASSERT_EQ(base.chunks.size(), 2);
  EXPECT_EQ(base.home[0], base.home[1]);
  EXPECT_NE(base.home[0], base.home[2]);
  EXPECT_EQ(base.chunks[0].partition, base.chunks[1].partition);
  EXPECT_NE(base.chunks[0].box, base.chunks[1].box);
  metadata::Bundle bundle(R"([{"class":"livehd.placement.chunk_guidance","design":"top","id":"explore","targets":[
    {"kind":"partition","selector":"3:cpu0","density":0.55},{"kind":"template_color","selector":"3:cpux","density":0.4},
    {"kind":"color","selector":"a.x","density":0.2}]},{"class":"other.pass","id":"keep"}])");
  auto             guided = run(&bundle);
  EXPECT_DOUBLE_EQ(guided.density[0], .2);
  EXPECT_DOUBLE_EQ(guided.density[2], .4);
  EXPECT_DOUBLE_EQ(guided.chunks[0].density, .55);
  EXPECT_DOUBLE_EQ(guided.chunks[1].density, .55);
  EXPECT_GT(guided.chunks[0].reserved, guided.chunks[1].reserved);
  auto r = m.record(guided, "top");
  bundle.replace(r);
  metadata::Bundle saved(bundle.json());
  EXPECT_EQ(metadata::canonical(r), metadata::canonical(m.record(run(&saved), "top")));
  EXPECT_EQ(saved.select("other.pass", "top").size(), 1);
  auto single = m.plan({10, 20, 510, 320}, 1, 2, 0, .6, nullptr, "top");
  EXPECT_EQ(single.chunks.size(), 1);
}
TEST(FpChunks, ReusesDetailedCoreAndMeasuresCrossingCellArea) {
  Chunk_model m;
  for (std::string core : {"a", "b"}) {
    for (int i = 0; i < 2; ++i) {
      m.cell(core + ".x", core, "cpu", false, 2, 1);
    }
  }
  auto plan = m.plan({0, 0, 80, 20}, 1, 1, 30000, .5, nullptr, "top");
  ASSERT_EQ(plan.chunks.size(), 2);
  DreamPlace::native::Design d;
  d.layout             = {0, 0, 80, 20, 1, 1};
  d.num_movable        = 4;
  d.node_size_x        = {2, 2, 2, 2};
  d.node_size_y        = {1, 1, 1, 1};
  const auto& a        = plan.chunks[plan.home[0]].box;
  const auto& b        = plan.chunks[plan.home[1]].box;
  d.node_x             = {a[0] + 1, a[0] + 4, b[0] + 1, b[0] + 4};
  d.node_y             = {1, 1, 1, 1};
  d.pin2node           = {0, 1, 2, 3};
  d.flat_net2pin       = {0, 1, 2, 3};
  d.flat_net2pin_start = {0, 2, 4};
  d.pin_offset_x = d.pin_offset_y = {0, 0, 0, 0};
  d.build_maps();
  std::vector<int>         owners{0, 0, 1, 1};
  std::vector<std::string> names{"inv", "buf", "inv", "buf"};
  auto                     stats = refine_chunk_seeds(d, m, plan, owners, names, 2, 1);
  EXPECT_EQ(stats.solved, 1);
  EXPECT_EQ(stats.reused, 1);
  EXPECT_EQ(stats.deferred, 0);
  EXPECT_DOUBLE_EQ(d.node_x[0] - a[0], d.node_x[2] - b[0]);
  EXPECT_DOUBLE_EQ(d.node_y[1] - a[1], d.node_y[3] - b[1]);

  metadata::Bundle policy(R"([{"class":"livehd.placement.chunk_guidance","id":"test","implementations":[
      {"partition":"3:cpu0","mode":"separate"}]}])");
  auto             separate = m.plan({0, 0, 80, 20}, 1, 1, 30000, .5, &policy, "top");
  auto             per_copy = refine_chunk_seeds(d, m, separate, owners, names, 2, 1);
  EXPECT_EQ(per_copy.solved, 2);
  EXPECT_EQ(per_copy.reused, 0);
  Chunk_measure measure(m.record(plan, "top"));
  measure.cell({39, 0, 41, 1}, false, "a.x");
  measure.cell({0, 3, 10, 5}, true, "");
  auto result = measure.record("top");
  for (const auto& c : result["chunks"].GetArray()) {
    EXPECT_DOUBLE_EQ(c["cell_area_dbu2"].GetDouble(), 1);
  }
  EXPECT_DOUBLE_EQ(result["colors"][0]["outside_home_dbu2"].GetDouble(), 1);
}

TEST(FpChunks, SmallPeripheralWrappersShareAHomeWithoutLosingTheirColor) {
  Chunk_model model;
  for (int i = 0; i < 1500; ++i) {
    model.cell("logic", "", "", false, 2, 1);
  }
  for (const char* scope : {"u0", "u1"}) {
    for (int i = 0; i < 4; ++i) {
      model.cell(std::string(scope) + ".buffer", scope, "buf", false, 1, 1);
    }
  }
  const auto plan = model.plan({0, 0, 800, 200}, 1, 1, 100000000, .5, nullptr, "top");
  ASSERT_EQ(plan.chunks.size(), 1);
  EXPECT_EQ(plan.chunks.front().objects.size(), 3);
  EXPECT_EQ(plan.home, (std::vector<int>{0, 0, 0}));
}

TEST(FpChunks, LogicHasUsableAreaBetweenFixedMacrosAndReplaysItsIslands) {
  Chunk_model m;
  for (int i = 0; i < 200; ++i) {
    m.cell("logic", "", "", false, 2, 2);
  }
  m.cell("logic", "", "", false, 1, 1, Chunk_box{0, 0, 1, 1});
  m.cell("logic", "", "", false, 1, 1, Chunk_box{99, 99, 100, 100});
  for (int i = 0; i < 4; ++i) {
    const double x = i & 1 ? 70 : 0, y = i & 2 ? 70 : 0;
    m.cell("ram" + std::to_string(i), "", "", true, 30, 30, Chunk_box{x, y, x + 30, y + 30});
  }
  const auto run  = [&](const metadata::Bundle* source) { return m.plan({0, 0, 100, 100}, 1, 1, 100000, .5, source, "top"); };
  const auto plan = run(nullptr);
  ASSERT_TRUE(plan.macro_islands);
  const auto& box = plan.chunks[plan.home[0]].box;
  EXPECT_GE((box[2] - box[0]) * (box[3] - box[1]), m.objects.front().area);
  for (size_t i = 1; i < m.objects.size(); ++i) {
    const auto& fixed = *m.objects[i].fixed;
    const auto& home  = plan.chunks[plan.home[i]].box;
    EXPECT_LE(home[0], fixed[0]);
    EXPECT_LE(home[1], fixed[1]);
    EXPECT_GE(home[2], fixed[2]);
    EXPECT_GE(home[3], fixed[3]);
  }
  metadata::Bundle saved;
  auto             record = m.record(plan, "top");
  saved.replace(record);
  EXPECT_EQ(metadata::canonical(record), metadata::canonical(m.record(run(&saved), "top")));
}

TEST(FpChunks, UpperEnvelopeBoundsSamplesAndRefusesUnsupportedMix) {
  std::vector<Memory_sample> train{
      {"1",   {1000, 2, 5000, 32}, 30000000,  80000000},
      {"2", {200, 90, 1000, 9000}, 24000000, 135000000},
      {"3",  {2000, 1, 10000, 18}, 32000000, 245000000}
  };
  auto fit = Chunk_predictor::fit("tech/flow", train);
  for (const auto& s : train) {
    EXPECT_GE(fit.peak(s.counts), s.peak * 1.25);
  }
  // Pin-rich macros consume more budget even with the logic population held constant.
  EXPECT_GT(fit.peak({200, 90, 1000, 9000}), fit.peak({200, 1, 1000, 18}));
  EXPECT_TRUE(fit.covers(train[0].counts));
  EXPECT_FALSE(fit.covers({2000, 90, 10000, 9000}));
  Chunk_model m;
  m.add_summary({.id       = "logic",
                 .instance = "",
                 .module   = "",
                 .local    = "",
                 .macro    = false,
                 .cells    = 2000,
                 .pins     = 6000,
                 .nets     = 4000,
                 .area     = 4000,
                 .width    = 2,
                 .height   = 1});
  auto p = m.plan({0, 0, 200, 100}, 1, 1, 10000000, .5, nullptr, "top", fit);
  EXPECT_EQ(p.chunks.size(), 1);
  EXPECT_TRUE(m.record(p, "top")["memory_prediction"]["baseline_exhausts_budget"].GetBool());
}
TEST(FpChunks, MemoryHistorySeparatesContextsAndMergesWithCollateral) {
  const auto dir = std::filesystem::path(::testing::TempDir()) / "fp_chunk_history";
  std::filesystem::create_directories(dir);
  const auto       history = (dir / "profile.json").string(), bundle = (dir / "bundle.json").string();
  metadata::Bundle keep(R"([{"class":"other.pass","id":"keep","n":9007199254740993}])");
  keep.write(history);
  keep.write(bundle);
  const Memory_counts small{100, 1, 100, 8}, large{3000, 0, 5000, 0};
  save_chunk_sample("techA", small, 10000, 50000, history, bundle);
  save_chunk_sample("techB", large, 12000, 200000, history, bundle);
  auto exported = metadata::Bundle::read(bundle);
  for (auto* name : {"techA", "techB"}) {
    auto fit = load_chunk_predictor(name, history, &exported);
    EXPECT_EQ(fit.samples, 1U);  // Same observation may arrive via both routes.
    EXPECT_EQ(fit.covers(large), std::string_view{name} == "techB");
  }
  EXPECT_EQ(load_chunk_predictor("unseen", history, nullptr).samples, 0U);
  EXPECT_EQ(metadata::Bundle::read(history).select("other.pass", "").size(), 1U);
  EXPECT_EQ(exported.select("other.pass", "").size(), 1U);
}
TEST(FpChunks, OutdatedSelectorsAreDiagnosed) {
  Chunk_model m;
  m.cell("color", "", "", false, 1, 1);
  metadata::Bundle b(
      R"([{"class":"livehd.placement.chunk_guidance","id":"edit","targets":[{"kind":"color","selector":"gone","density":0.5}]}])");
  EXPECT_THROW(m.plan({0, 0, 100, 100}, 1, 1, 10000, .5, &b, "top"), std::invalid_argument);
}
}  // namespace livehd::placement

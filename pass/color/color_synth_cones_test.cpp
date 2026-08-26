//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// The cone-seeded synthesis coloring, `pass.color synth --set synth_alg=cones`
// (todo/livehd/2c-color-synthcones.html). Every test below pins one of the
// rulings the algorithm is built on, so a change that breaks one is a change to
// the design and not to an implementation detail.
//
// Sizes are in PREDICTED generic-AIG score (graph/predict_abc_size.hpp), which
// is what `max_gate` is expressed in, so the fixtures state their arithmetic:
//   And/Or  N bits, 2 operands  -> N
//   Xor     N bits, 2 operands  -> 3N
//   Flop    no enable/reset     -> 0
//   Not / constant mask         -> 0

#include "color_common.hpp"
#include "color_synth.hpp"
#include "graph_library_singleton.hpp"
#include "gtest/gtest.h"
#include "hhds/graph.hpp"
#include "hlop/dlop.hpp"
#include "node_util.hpp"

using livehd::color::Color_opts;
using livehd::color::Color_synth;
using livehd::color::is_partitionable;
using livehd::graph_util::create_const;
using livehd::graph_util::create_typed_node;
using livehd::graph_util::node_color_of;
using livehd::graph_util::set_bits;

namespace {

// RAW cones: the walk and the first-wins ownership only. max_gate = 0 is INERT
// by contract (color_common.hpp) -- no walk budget and no merge -- so a test
// that wants to pin the WALK is not silently pinning the shipped 30k policy.
Color_opts raw_opts() {
  Color_opts o;
  o.hier    = false;
  o.compact = true;
  o.min_ge  = 0;  // the GE window does not shape cones; pinned here, not assumed
  o.max_ge  = 0;
  return o;
}

Color_opts capped_opts(uint64_t max_gate) {
  Color_opts o = raw_opts();
  o.max_gate   = max_gate;
  return o;
}

// A flop whose `din` is driven by `d`. Q width is stamped so the predictor sees
// a real register rather than the unknown-width degradation.
hhds::Node_class make_flop(hhds::Graph& g, const hhds::Pin_class& d, int32_t bits) {
  auto f = create_typed_node(g, Ntype_op::Flop, bits);
  d.connect_sink(f.create_sink_pin(3));  // din
  return f;
}

// Distinct colors actually written on `g` (0 / NO_COLOR excluded).
size_t color_count(hhds::Graph* g) {
  absl::flat_hash_set<int32_t> ids;
  for (auto n : g->body().nodes(hhds::Node_order::forward)) {
    if (is_partitionable(n) && node_color_of(n) != livehd::color::NO_COLOR) {
      ids.insert(node_color_of(n));
    }
  }
  return ids.size();
}

size_t uncolored_count(hhds::Graph* g) {
  size_t k = 0;
  for (auto n : g->body().nodes(hhds::Node_order::forward)) {
    if (is_partitionable(n) && node_color_of(n) == livehd::color::NO_COLOR) {
      ++k;
    }
  }
  return k;
}

// flopA <- xorA <- shared -> andB -> flopB. `shared` belongs to whichever cone
// reaches it first; the other one walks THROUGH it and records the overlap.
struct Share_fixture {
  std::shared_ptr<hhds::Graph> g;
  hhds::Node_class             shared, xor_a, and_b, flop_a, flop_b;
};

Share_fixture two_flops_sharing(const char* dir) {
  auto& lib = livehd::Hhds_graph_library::instance(dir);
  auto  gio = lib.create_io("cones_share");
  gio->add_input("i0", 8);
  gio->add_input("i1", 8);
  auto g = gio->create_graph();
  set_bits(g->get_input_pin("i0"), 8);
  set_bits(g->get_input_pin("i1"), 8);

  auto shared = create_typed_node(*g, Ntype_op::Xor, 8);  // 3*8 = 24
  g->get_input_pin("i0").connect_sink(shared.create_sink_pin(0));
  g->get_input_pin("i1").connect_sink(shared.create_sink_pin(0));

  auto xor_a = create_typed_node(*g, Ntype_op::Xor, 8);  // 24
  shared.create_driver_pin(0).connect_sink(xor_a.create_sink_pin(0));
  g->get_input_pin("i0").connect_sink(xor_a.create_sink_pin(0));

  auto and_b = create_typed_node(*g, Ntype_op::And, 8);  // 8
  shared.create_driver_pin(0).connect_sink(and_b.create_sink_pin(0));
  g->get_input_pin("i1").connect_sink(and_b.create_sink_pin(0));

  auto flop_a = make_flop(*g, xor_a.create_driver_pin(0), 8);
  auto flop_b = make_flop(*g, and_b.create_driver_pin(0), 8);
  return {g, shared, xor_a, and_b, flop_a, flop_b};
}

}  // namespace

// Ruling 1 (walk-through) and ruling 4 (no floor). Raw, the shared sub-cone
// belongs to the FIRST cone in forward order and the second one still walks
// through it -- so there are exactly two colors, and the register lands in the
// color of the data it registers.
TEST(ColorSynthCones, TwoFlopsShareSubconeRaw) {
  auto f = two_flops_sharing("lgdb_cones_share_raw");
  Color_synth(raw_opts(), "cones").label(f.g.get());

  EXPECT_EQ(color_count(f.g.get()), 2u);
  EXPECT_EQ(uncolored_count(f.g.get()), 0u);
  EXPECT_EQ(node_color_of(f.xor_a), node_color_of(f.flop_a)) << "the stage is the logic plus the register it writes";
  EXPECT_EQ(node_color_of(f.and_b), node_color_of(f.flop_b));
  EXPECT_NE(node_color_of(f.xor_a), node_color_of(f.and_b));
  EXPECT_TRUE(node_color_of(f.shared) == node_color_of(f.xor_a) || node_color_of(f.shared) == node_color_of(f.and_b))
      << "first-wins: the shared cone goes to one of the two, never to a third color";
}

// Ruling 6/ruling 4 together: the recorded overlap is what merges the pair, and
// only while the union fits. A(xorA 24 + shared 24) + B(andB 8) = 56.
TEST(ColorSynthCones, ThresholdBoundary) {
  {
    auto f = two_flops_sharing("lgdb_cones_share_fits");
    Color_synth(capped_opts(56), "cones").label(f.g.get());
    EXPECT_EQ(color_count(f.g.get()), 1u) << "56 <= max_gate: the overlapping pair merges";
  }
  {
    auto f = two_flops_sharing("lgdb_cones_share_tight");
    Color_synth(capped_opts(55), "cones").label(f.g.get());
    EXPECT_EQ(color_count(f.g.get()), 2u) << "one predicted gate under the union: refused, and refused for good";
  }
}

// Ruling 2. `din` seeds one color and `enable` a SEPARATE one, and the flop
// itself carries the DIN color. Welding the two is the regression that put
// 99.4% of a flat dino in one region under the forward algorithm.
TEST(ColorSynthCones, EnableIsItsOwnCone) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_cones_enable");
  auto  gio = lib.create_io("cones_en");
  gio->add_input("a", 8);
  gio->add_input("b", 8);
  auto g = gio->create_graph();
  set_bits(g->get_input_pin("a"), 8);
  set_bits(g->get_input_pin("b"), 8);

  auto data = create_typed_node(*g, Ntype_op::Xor, 8);
  g->get_input_pin("a").connect_sink(data.create_sink_pin(0));
  g->get_input_pin("b").connect_sink(data.create_sink_pin(0));

  auto ctrl = create_typed_node(*g, Ntype_op::And, 1);
  g->get_input_pin("a").connect_sink(ctrl.create_sink_pin(0));
  g->get_input_pin("b").connect_sink(ctrl.create_sink_pin(0));

  auto flop = make_flop(*g, data.create_driver_pin(0), 8);
  ctrl.create_driver_pin(0).connect_sink(flop.create_sink_pin(4));  // enable

  Color_synth(raw_opts(), "cones").label(g.get());

  EXPECT_NE(node_color_of(data), node_color_of(ctrl)) << "din and enable are separate cone seeds";
  EXPECT_EQ(node_color_of(flop), node_color_of(data)) << "the register carries the DIN color";
}

// Ruling 11. Coloring is NODE-granular: a 64-bit register gets exactly the same
// one din cone (plus one enable cone) as an 8-bit one, and its storage width is
// not a merge guard of its own.
TEST(ColorSynthCones, WideFlopIsNodeGranular) {
  const auto build = [](const char* dir, int32_t bits) {
    auto& lib = livehd::Hhds_graph_library::instance(dir);
    auto  gio = lib.create_io("cones_wide");
    gio->add_input("a", bits);
    gio->add_input("b", bits);
    auto g = gio->create_graph();
    set_bits(g->get_input_pin("a"), bits);
    set_bits(g->get_input_pin("b"), bits);
    auto data = create_typed_node(*g, Ntype_op::Xor, bits);
    g->get_input_pin("a").connect_sink(data.create_sink_pin(0));
    g->get_input_pin("b").connect_sink(data.create_sink_pin(0));
    (void)make_flop(*g, data.create_driver_pin(0), bits);
    Color_synth(raw_opts(), "cones").label(g.get());
    return color_count(g.get());
  };
  EXPECT_EQ(build("lgdb_cones_w8", 8), build("lgdb_cones_w512", 512));
}

// Ruling 2, second half. Clock and reset never seed and are never colored
// THROUGH the register: the reset tree is a separate cone the leftover sweep
// picks up, sharing no logic with the data cone.
TEST(ColorSynthCones, ClockResetNeverSeed) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_cones_reset");
  auto  gio = lib.create_io("cones_rst");
  gio->add_input("a", 8);
  gio->add_input("b", 8);
  gio->add_input("r", 1);
  auto g = gio->create_graph();
  set_bits(g->get_input_pin("a"), 8);
  set_bits(g->get_input_pin("b"), 8);
  set_bits(g->get_input_pin("r"), 1);

  auto data = create_typed_node(*g, Ntype_op::Xor, 8);
  g->get_input_pin("a").connect_sink(data.create_sink_pin(0));
  g->get_input_pin("b").connect_sink(data.create_sink_pin(0));

  auto rst = create_typed_node(*g, Ntype_op::Or, 1);  // the reset tree
  g->get_input_pin("r").connect_sink(rst.create_sink_pin(0));
  g->get_input_pin("a").connect_sink(rst.create_sink_pin(0));

  auto flop = make_flop(*g, data.create_driver_pin(0), 8);
  rst.create_driver_pin(0).connect_sink(flop.create_sink_pin(7));  // reset_pin

  Color_synth(raw_opts(), "cones").label(g.get());

  EXPECT_NE(node_color_of(rst), livehd::color::NO_COLOR) << "the sweep colors it";
  EXPECT_NE(node_color_of(rst), node_color_of(data)) << "reset logic is not claimed through the flop";
  EXPECT_EQ(uncolored_count(g.get()), 0u);
}

// Ruling 1, second half. A cone stops expanding once its traversed predicted
// score passes max_gate; the tail it never reached is a sink of the uncolored
// subgraph and the leftover sweep roots it. Nothing is left at NO_COLOR.
TEST(ColorSynthCones, BudgetStopsWalk) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_cones_budget");
  auto  gio = lib.create_io("cones_budget");
  gio->add_input("a", 8);
  gio->add_input("b", 8);
  auto g = gio->create_graph();
  set_bits(g->get_input_pin("a"), 8);
  set_bits(g->get_input_pin("b"), 8);

  // c1 <- c2 <- c3, 24 predicted each.
  auto c3 = create_typed_node(*g, Ntype_op::Xor, 8);
  g->get_input_pin("a").connect_sink(c3.create_sink_pin(0));
  g->get_input_pin("b").connect_sink(c3.create_sink_pin(0));
  auto c2 = create_typed_node(*g, Ntype_op::Xor, 8);
  c3.create_driver_pin(0).connect_sink(c2.create_sink_pin(0));
  g->get_input_pin("a").connect_sink(c2.create_sink_pin(0));
  auto c1 = create_typed_node(*g, Ntype_op::Xor, 8);
  c2.create_driver_pin(0).connect_sink(c1.create_sink_pin(0));
  g->get_input_pin("a").connect_sink(c1.create_sink_pin(0));
  auto flop = make_flop(*g, c1.create_driver_pin(0), 8);

  // Budget 30: c1 puts the walk at 24 (fits), c2 at 48 (over) -- so c2 is
  // claimed and counted, nothing already queued is visited after the crossing,
  // and c3 is never reached by this cone.
  Color_synth(capped_opts(30), "cones").label(g.get());

  EXPECT_EQ(uncolored_count(g.get()), 0u) << "the truncated tail is picked up by the sweep";
  EXPECT_EQ(node_color_of(c1), node_color_of(flop));
  EXPECT_NE(node_color_of(c3), node_color_of(c1)) << "the walk stopped before c3";
}

// Ruling 7. Mult/Div and wide Sum keep the `synth` arithmetic cut: each is a
// root of its OWN color, pre-owned before any walk, and a consumer cone records
// contact overlap with it instead of claiming it.
TEST(ColorSynthCones, ArithCutIsOwnRoot) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_cones_cut");
  auto  gio = lib.create_io("cones_cut");
  gio->add_input("a", 8);
  gio->add_input("b", 8);
  auto g = gio->create_graph();
  set_bits(g->get_input_pin("a"), 8);
  set_bits(g->get_input_pin("b"), 8);

  auto mul = create_typed_node(*g, Ntype_op::Mult, 16);
  g->get_input_pin("a").connect_sink(mul.create_sink_pin(0));
  g->get_input_pin("b").connect_sink(mul.create_sink_pin(0));

  auto use = create_typed_node(*g, Ntype_op::Xor, 16);
  mul.create_driver_pin(0).connect_sink(use.create_sink_pin(0));
  g->get_input_pin("a").connect_sink(use.create_sink_pin(0));
  auto flop = make_flop(*g, use.create_driver_pin(0), 16);

  Color_synth(raw_opts(), "cones").label(g.get());

  EXPECT_NE(node_color_of(mul), livehd::color::NO_COLOR);
  EXPECT_NE(node_color_of(mul), node_color_of(use)) << "an arithmetic cut owns its color outright";
  EXPECT_EQ(node_color_of(use), node_color_of(flop));
}

// A graph output is a root like a register: output-only logic gets a cone
// rather than falling through to the totality fallback as singletons.
TEST(ColorSynthCones, OutputsAreRoots) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_cones_out");
  auto  gio = lib.create_io("cones_out");
  gio->add_input("a", 8);
  gio->add_input("b", 8);
  gio->add_output("y", 8);
  auto g = gio->create_graph();
  set_bits(g->get_input_pin("a"), 8);
  set_bits(g->get_input_pin("b"), 8);

  auto deep = create_typed_node(*g, Ntype_op::Xor, 8);
  g->get_input_pin("a").connect_sink(deep.create_sink_pin(0));
  g->get_input_pin("b").connect_sink(deep.create_sink_pin(0));
  auto top = create_typed_node(*g, Ntype_op::And, 8);
  deep.create_driver_pin(0).connect_sink(top.create_sink_pin(0));
  g->get_input_pin("a").connect_sink(top.create_sink_pin(0));
  top.create_driver_pin(0).connect_sink(g->get_output_pin("y"));

  Color_synth(raw_opts(), "cones").label(g.get());

  EXPECT_EQ(uncolored_count(g.get()), 0u);
  EXPECT_EQ(color_count(g.get()), 1u) << "one output root, one cone";
  EXPECT_EQ(node_color_of(deep), node_color_of(top));
}

// A Memory is ONE hard-stop node with ONE anchor color; its per-port roots color
// the logic AROUND it. Two ports whose address logic is disjoint therefore give
// two different cones, and nothing is left uncolored.
TEST(ColorSynthCones, MemoryPortsAreRoots) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_cones_mem");
  auto  gio = lib.create_io("cones_mem");
  gio->add_input("a0", 8);
  gio->add_input("a1", 8);
  gio->add_input("d", 8);
  auto g = gio->create_graph();
  set_bits(g->get_input_pin("a0"), 8);
  set_bits(g->get_input_pin("a1"), 8);
  set_bits(g->get_input_pin("d"), 8);

  auto addr0 = create_typed_node(*g, Ntype_op::Xor, 8);
  g->get_input_pin("a0").connect_sink(addr0.create_sink_pin(0));
  g->get_input_pin("d").connect_sink(addr0.create_sink_pin(0));
  auto addr1 = create_typed_node(*g, Ntype_op::Xor, 8);
  g->get_input_pin("a1").connect_sink(addr1.create_sink_pin(0));
  g->get_input_pin("d").connect_sink(addr1.create_sink_pin(0));

  auto mem = create_typed_node(*g, Ntype_op::Memory);
  addr0.create_driver_pin(0).connect_sink(mem.create_sink_pin(0));   // port 0 addr
  g->get_input_pin("d").connect_sink(mem.create_sink_pin(3));        // port 0 din
  const auto stride = static_cast<hhds::Port_id>(Ntype::Memory_port_stride);
  addr1.create_driver_pin(0).connect_sink(mem.create_sink_pin(stride));  // port 1 addr

  Color_synth(raw_opts(), "cones").label(g.get());

  EXPECT_EQ(uncolored_count(g.get()), 0u);
  EXPECT_NE(node_color_of(mem), livehd::color::NO_COLOR) << "the memory node itself carries one color";
  EXPECT_NE(node_color_of(addr0), node_color_of(addr1)) << "each logical port seeds its own cone";
}

// Determinism is a property of the ROOT ORDER and the renumber, not of hash
// iteration: two runs must agree exactly, ids must be dense from 1, and the
// first partitionable node in forward order must be color 1.
TEST(ColorSynthCones, DeterministicDenseIds) {
  auto f1 = two_flops_sharing("lgdb_cones_det1");
  Color_synth(capped_opts(1000), "cones").label(f1.g.get());
  std::vector<int32_t> first;
  for (auto n : f1.g->body().nodes(hhds::Node_order::forward)) {
    if (is_partitionable(n)) {
      first.emplace_back(node_color_of(n));
    }
  }

  Color_synth(capped_opts(1000), "cones").label(f1.g.get());  // label twice
  std::vector<int32_t> second;
  for (auto n : f1.g->body().nodes(hhds::Node_order::forward)) {
    if (is_partitionable(n)) {
      second.emplace_back(node_color_of(n));
    }
  }
  EXPECT_EQ(first, second);

  auto f2 = two_flops_sharing("lgdb_cones_det2");
  Color_synth(raw_opts(), "cones").label(f2.g.get());
  int32_t                      max_id = 0;
  absl::flat_hash_set<int32_t> ids;
  bool                         saw_first = false;
  for (auto n : f2.g->body().nodes(hhds::Node_order::forward)) {
    if (!is_partitionable(n)) {
      continue;
    }
    const auto c = node_color_of(n);
    if (!saw_first) {
      EXPECT_EQ(c, 1) << "the first partitionable node in forward order is color 1";
      saw_first = true;
    }
    ids.insert(c);
    max_id = std::max(max_id, c);
  }
  EXPECT_EQ(static_cast<size_t>(max_id), ids.size()) << "ids are dense 1..k";
}

// Source-seeded (block-attribute) nodes are the user's. They are a WALL: never
// traversed, never claimed, never in a pair, and the algorithm's own ids land
// above the max seeded id.
TEST(ColorSynthCones, SeededIsAWall) {
  constexpr int32_t kSeededId = 7;

  auto& lib = livehd::Hhds_graph_library::instance("lgdb_cones_seeded");
  auto  gio = lib.create_io("cones_seeded");
  gio->add_input("a", 8);
  gio->add_input("b", 8);
  auto g = gio->create_graph();
  set_bits(g->get_input_pin("a"), 8);
  set_bits(g->get_input_pin("b"), 8);

  auto deep = create_typed_node(*g, Ntype_op::Xor, 8);
  g->get_input_pin("a").connect_sink(deep.create_sink_pin(0));
  g->get_input_pin("b").connect_sink(deep.create_sink_pin(0));
  auto wall = create_typed_node(*g, Ntype_op::And, 8);
  deep.create_driver_pin(0).connect_sink(wall.create_sink_pin(0));
  g->get_input_pin("a").connect_sink(wall.create_sink_pin(0));
  auto near = create_typed_node(*g, Ntype_op::Or, 8);
  wall.create_driver_pin(0).connect_sink(near.create_sink_pin(0));
  g->get_input_pin("b").connect_sink(near.create_sink_pin(0));
  auto flop = make_flop(*g, near.create_driver_pin(0), 8);

  livehd::graph_util::set_color(wall, kSeededId);
  livehd::color::set_coloring_info(g.get(), R"({"schema_version":1,"algorithm":"block-attr","params":{}})");
  ASSERT_TRUE(livehd::color::has_seeded_coloring(g.get()));

  Color_synth(capped_opts(1000), "cones").label(g.get());

  EXPECT_EQ(node_color_of(wall), kSeededId) << "the seeded region keeps its exact id";
  EXPECT_GT(node_color_of(near), kSeededId) << "algorithm ids shift above the max seeded id";
  EXPECT_GT(node_color_of(deep), kSeededId);
  EXPECT_NE(node_color_of(near), node_color_of(deep)) << "the wall was never traversed, so the two never merged";
  EXPECT_EQ(node_color_of(near), node_color_of(flop));
}

// A register fed by ANOTHER register opens a color of its own. Joining it would
// weld two pipeline stages through exactly the boundary this algorithm places,
// and which of the two forward order happened to reach first would decide it --
// the same reason Color_synth::data_cone_id refuses a cut driver.
TEST(ColorSynthCones, RegisterFedByRegisterIsItsOwnColor) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_cones_chain");
  auto  gio = lib.create_io("cones_chain");
  gio->add_input("a", 8);
  gio->add_input("b", 8);
  auto g = gio->create_graph();
  set_bits(g->get_input_pin("a"), 8);
  set_bits(g->get_input_pin("b"), 8);

  auto data = create_typed_node(*g, Ntype_op::Xor, 8);
  g->get_input_pin("a").connect_sink(data.create_sink_pin(0));
  g->get_input_pin("b").connect_sink(data.create_sink_pin(0));

  auto f1 = make_flop(*g, data.create_driver_pin(0), 8);
  auto f2 = make_flop(*g, f1.create_driver_pin(0), 8);  // straight shift stage
  auto f3 = make_flop(*g, f2.create_driver_pin(0), 8);

  Color_synth(capped_opts(100000), "cones").label(g.get());

  EXPECT_EQ(node_color_of(f1), node_color_of(data)) << "the first stage is its logic plus the register it writes";
  EXPECT_NE(node_color_of(f2), node_color_of(f1)) << "a register fed by a register never joins it";
  EXPECT_NE(node_color_of(f3), node_color_of(f2));
  EXPECT_EQ(uncolored_count(g.get()), 0u);
}

// A hub cone shared by several register cones merges with them LARGEST OVERLAP
// FIRST. With a cap that admits exactly one merge, the heaviest sharer wins and
// the rest keep their boundary to the hub.
TEST(ColorSynthCones, HubMergesInOverlapOrder) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_cones_hub");
  auto  gio = lib.create_io("cones_hub");
  gio->add_input("a", 8);
  gio->add_input("b", 8);
  auto g = gio->create_graph();
  set_bits(g->get_input_pin("a"), 8);
  set_bits(g->get_input_pin("b"), 8);

  // The hub cone: hub2 (3*8=24) behind hub1 (8). Whoever roots first owns both.
  auto hub2 = create_typed_node(*g, Ntype_op::Xor, 8);  // 24
  g->get_input_pin("a").connect_sink(hub2.create_sink_pin(0));
  g->get_input_pin("b").connect_sink(hub2.create_sink_pin(0));
  auto hub1 = create_typed_node(*g, Ntype_op::And, 8);  // 8
  hub2.create_driver_pin(0).connect_sink(hub1.create_sink_pin(0));
  g->get_input_pin("a").connect_sink(hub1.create_sink_pin(0));

  // Flop H owns the hub (created first => rooted first). Flop S shares BOTH hub
  // nodes (overlap 32); flop T only reaches hub1 (overlap 8).
  auto flop_h = make_flop(*g, hub1.create_driver_pin(0), 8);

  auto s_use = create_typed_node(*g, Ntype_op::Or, 8);  // 8
  hub1.create_driver_pin(0).connect_sink(s_use.create_sink_pin(0));
  g->get_input_pin("b").connect_sink(s_use.create_sink_pin(0));
  auto flop_s = make_flop(*g, s_use.create_driver_pin(0), 8);

  auto t_use = create_typed_node(*g, Ntype_op::Or, 8);  // 8
  hub1.create_driver_pin(0).connect_sink(t_use.create_sink_pin(0));
  g->get_input_pin("a").connect_sink(t_use.create_sink_pin(0));
  auto flop_t = make_flop(*g, t_use.create_driver_pin(0), 8);

  // hub = 8 + 24 = 32, S = 8, T = 8. Both sharers see overlap 32 (they walk the
  // whole hub cone), so the tie breaks on the smaller id -- S, rooted first.
  // 32 + 8 = 40 fits; a second merge would be 40 + 8 = 48 and does not.
  Color_synth(capped_opts(40), "cones").label(g.get());

  EXPECT_EQ(node_color_of(s_use), node_color_of(hub1)) << "the first-ranked sharer merged into the hub";
  EXPECT_NE(node_color_of(t_use), node_color_of(hub1)) << "the cap refused the second merge";
  EXPECT_EQ(node_color_of(flop_h), node_color_of(hub1));
  EXPECT_EQ(node_color_of(flop_t), node_color_of(t_use));
  EXPECT_EQ(uncolored_count(g.get()), 0u);
}

// A cones color is NOT a connected component, which is why the pass records
// "packed":true unconditionally: here the merge of A and B produces one id
// spanning two clouds whose only graph path runs through a node color C owns.
// Without the flag pass.partition's component split would shred it back apart.
TEST(ColorSynthCones, MergedColorMaySpanTwoClouds) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_cones_packed");
  auto  gio = lib.create_io("cones_packed");
  gio->add_input("a", 32);
  gio->add_input("b", 32);
  auto g = gio->create_graph();
  set_bits(g->get_input_pin("a"), 32);
  set_bits(g->get_input_pin("b"), 32);

  auto w = create_typed_node(*g, Ntype_op::Xor, 32);  // 96
  g->get_input_pin("a").connect_sink(w.create_sink_pin(0));
  g->get_input_pin("b").connect_sink(w.create_sink_pin(0));
  auto u = create_typed_node(*g, Ntype_op::And, 8);  // 8
  w.create_driver_pin(0).connect_sink(u.create_sink_pin(0));
  g->get_input_pin("a").connect_sink(u.create_sink_pin(0));
  auto m = create_typed_node(*g, Ntype_op::Xor, 32);  // 96
  w.create_driver_pin(0).connect_sink(m.create_sink_pin(0));
  g->get_input_pin("a").connect_sink(m.create_sink_pin(0));
  auto v = create_typed_node(*g, Ntype_op::Or, 8);  // 8
  m.create_driver_pin(0).connect_sink(v.create_sink_pin(0));
  g->get_input_pin("a").connect_sink(v.create_sink_pin(0));

  // Creation order fixes the root order: A first (claims u and w), then C
  // (claims m, records overlap 96 on w), then B (claims v, records 96 on m and
  // 96 on w). A=104, C=96, B=8.
  auto flop_a = make_flop(*g, u.create_driver_pin(0), 8);
  auto flop_c = make_flop(*g, m.create_driver_pin(0), 32);
  auto flop_b = make_flop(*g, v.create_driver_pin(0), 8);

  // A+C = 200 and C+B = 104+... are both over 150; A+B = 112 fits.
  Color_synth(capped_opts(150), "cones").label(g.get());

  EXPECT_EQ(node_color_of(u), node_color_of(v)) << "A and B merged on their shared sub-cone";
  EXPECT_NE(node_color_of(m), node_color_of(u)) << "the node between them stayed in its own color";
  EXPECT_EQ(node_color_of(w), node_color_of(u));
  EXPECT_EQ(node_color_of(flop_a), node_color_of(u));
  EXPECT_EQ(node_color_of(flop_b), node_color_of(v));
  EXPECT_EQ(node_color_of(flop_c), node_color_of(m));
  EXPECT_EQ(uncolored_count(g.get()), 0u);
}

// A COMBINATIONAL CYCLE has no sink in the uncolored subgraph -- every member
// still has an uncolored consumer -- so the leftover sweep cannot see it. It is
// what the totality fallback exists for, and what walk_root's epoch stamp has to
// terminate on. Without either, this hangs or leaves NO_COLOR nodes that
// pass.partition warns about and pass.analyze flags.
TEST(ColorSynthCones, IsolatedCombinationalCycleIsColored) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_cones_cycle");
  auto  gio = lib.create_io("cones_cycle");
  gio->add_input("a", 8);
  auto g = gio->create_graph();
  set_bits(g->get_input_pin("a"), 8);

  // x -> y -> x, reachable from nothing that a register cone roots on.
  auto x = create_typed_node(*g, Ntype_op::And, 8);
  auto y = create_typed_node(*g, Ntype_op::Or, 8);
  x.create_driver_pin(0).connect_sink(y.create_sink_pin(0));
  y.create_driver_pin(0).connect_sink(x.create_sink_pin(0));
  g->get_input_pin("a").connect_sink(x.create_sink_pin(0));

  // An ordinary register cone alongside it, so the cycle is not the whole graph.
  auto data = create_typed_node(*g, Ntype_op::Xor, 8);
  g->get_input_pin("a").connect_sink(data.create_sink_pin(0));
  auto flop = make_flop(*g, data.create_driver_pin(0), 8);

  Color_synth(raw_opts(), "cones").label(g.get());

  EXPECT_EQ(uncolored_count(g.get()), 0u) << "the totality fallback must reach an isolated comb cycle";
  EXPECT_NE(node_color_of(x), livehd::color::NO_COLOR);
  EXPECT_NE(node_color_of(y), livehd::color::NO_COLOR);
  EXPECT_EQ(node_color_of(flop), node_color_of(data));
}

// pass.abc builds a runtime right shift's barrel only to the width its
// CONSTANT-mask consumers demand, and only while the two share a region
// (synthesis_cost.hpp ColorSize.WideSraUsesNarrowSliceDemand). claim_sra_group
// keeps them together where ownership permits -- without it the slice lands in
// another color and abc rebuilds the full-width barrel.
TEST(ColorSynthCones, RuntimeSraKeepsItsConstantMaskSlice) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_cones_sra");
  auto  gio = lib.create_io("cones_sra");
  gio->add_input("a", 64);
  gio->add_input("amt", 6);
  auto g = gio->create_graph();
  set_bits(g->get_input_pin("a"), 64);
  set_bits(g->get_input_pin("amt"), 6);

  auto sra = create_typed_node(*g, Ntype_op::SRA, 64);
  g->get_input_pin("a").connect_sink(sra.create_sink_pin(0));    // a
  g->get_input_pin("amt").connect_sink(sra.create_sink_pin(1));  // b: RUNTIME amount
  auto sra_d = sra.create_driver_pin(0);
  set_bits(sra_d, 64);

  // A constant-mask Get_mask over the shift: the narrow slice abc would build a
  // prefix for. `mask` is sink pid 2 (cell.cpp), NOT 1.
  auto slice = create_typed_node(*g, Ntype_op::Get_mask, 8);
  sra_d.connect_sink(slice.create_sink_pin(0));
  create_const(*g, *Dlop::create_integer(0xff)).connect_sink(slice.create_sink_pin(2));
  auto slice_d = slice.create_driver_pin(0);
  set_bits(slice_d, 8);

  // The slice feeds ONE register; the shift itself feeds a SECOND, so first-wins
  // could otherwise split the pair across the two cones.
  auto other = create_typed_node(*g, Ntype_op::And, 64);
  sra_d.connect_sink(other.create_sink_pin(0));
  g->get_input_pin("a").connect_sink(other.create_sink_pin(0));
  auto flop_wide   = make_flop(*g, other.create_driver_pin(0), 64);
  auto flop_narrow = make_flop(*g, slice_d, 8);

  Color_synth(raw_opts(), "cones").label(g.get());

  EXPECT_EQ(uncolored_count(g.get()), 0u);
  EXPECT_EQ(node_color_of(slice), node_color_of(sra)) << "a runtime SRA keeps its constant-mask slice";
  EXPECT_EQ(node_color_of(flop_wide), node_color_of(other));
  EXPECT_NE(node_color_of(flop_narrow), livehd::color::NO_COLOR);
}

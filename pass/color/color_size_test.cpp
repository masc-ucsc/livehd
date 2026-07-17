// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// The size window (todo/livehd/2c-color-size.html subtask C). These tests drive
// apply_size_window directly with a hand-built Node2Id rather than through
// Color_synth: the engine's contract is "any coloring in, a windowed coloring
// out", and a hand-built map is the only way to pin the two shapes that matter --
// N singleton regions (XSCore had 35,065) and one monster (XSCore had 3.48M
// nodes) -- without also re-testing the cut rules.

#include "color_size.hpp"

#include <cstdint>
#include <vector>

#include "color_common.hpp"
#include "graph_library_singleton.hpp"
#include "gtest/gtest.h"
#include "hhds/graph.hpp"
#include "node_util.hpp"

using livehd::color::apply_size_window;
using livehd::color::Node2Id;
using livehd::color::Size_window_stats;
using livehd::graph_util::create_typed_node;
using livehd::graph_util::ge_weight;
using livehd::graph_util::set_bits;

namespace {

struct Chain {
  std::shared_ptr<hhds::Graph>  g;
  std::vector<hhds::Node_class> nodes;
};

// A straight chain of `n` And nodes, each driving `bits` wide. Every node weighs
// exactly `bits` GE, so a test can state its expectations in round numbers.
Chain make_chain(const char* dir, const char* name, int n, int bits) {
  auto& lib = livehd::Hhds_graph_library::instance(dir);
  auto  gio = lib.create_io(name);
  gio->add_input("a", 0);
  gio->add_output("y", 1);
  auto g = gio->create_graph();

  auto in = g->get_input_pin("a");
  set_bits(in, bits);

  Chain c;
  c.g          = g;
  auto     prev = in;
  for (int i = 0; i < n; ++i) {
    auto node = create_typed_node(*g, Ntype_op::And);
    prev.connect_sink(node.create_sink_pin(0));
    in.connect_sink(node.create_sink_pin(1));
    auto d = node.create_driver_pin(0);
    set_bits(d, bits);
    c.nodes.emplace_back(node);
    prev = d;
  }
  prev.connect_sink(g->get_output_pin("y"));
  return c;
}

// Every node its own region: the singleton shape the merge half exists for.
Node2Id all_singletons(const Chain& c) {
  Node2Id m;
  int     id = 1;
  for (const auto& n : c.nodes) {
    m[n] = id++;
  }
  return m;
}

// Every node in one region: the monster shape the split half exists for.
Node2Id one_region(const Chain& c) {
  Node2Id m;
  for (const auto& n : c.nodes) {
    m[n] = 1;
  }
  return m;
}

// region id -> total GE, computed from the returned map.
absl::flat_hash_map<int, uint64_t> region_ge(const Node2Id& m) {
  absl::flat_hash_map<int, uint64_t> out;
  for (const auto& [n, id] : m) {
    out[id] += ge_weight(n);
  }
  return out;
}

}  // namespace

// 0/0 is inert: the engine still normalizes to connected components (that is its
// contract, and pass.partition's) but changes no sizes.
TEST(ColorSize, DisabledWindowOnlySplitsComponents) {
  auto c = make_chain("lgdb_cs_off", "off", 6, 8);
  auto m = all_singletons(c);

  Size_window_stats st;
  auto              out = apply_size_window(c.g.get(), m, 0, 0, &st);

  EXPECT_EQ(out.size(), c.nodes.size()) << "every node keeps a region";
  EXPECT_EQ(st.merges, 0u);
  EXPECT_EQ(st.splits, 0u);
  EXPECT_EQ(region_ge(out).size(), 6u) << "six singleton regions in, six out";
}

// The 35k-singleton half. A chain of 8-GE singletons under a 40-GE floor must
// come back as regions that each clear the floor.
TEST(ColorSize, MergesSingletonsUpToMin) {
  auto c = make_chain("lgdb_cs_merge", "merge", 20, 8);  // 20 nodes * 8 GE = 160 GE
  auto m = all_singletons(c);

  Size_window_stats st;
  auto              out = apply_size_window(c.g.get(), m, 40, 0, &st);

  const auto ge = region_ge(out);
  EXPECT_GT(st.merges, 0u);
  EXPECT_LT(ge.size(), 20u) << "singletons must have been absorbed";
  for (const auto& [id, w] : ge) {
    EXPECT_GE(w, 40u) << "region " << id << " is still under min with neighbours available";
  }
  EXPECT_EQ(st.left_under, 0u);
}

// The 3.48M-node half. One region of 320 GE under a 100-GE ceiling must come
// back as several, none over the ceiling.
TEST(ColorSize, SplitsMonsterUnderMax) {
  auto c = make_chain("lgdb_cs_split", "split", 40, 8);  // 40 * 8 = 320 GE
  auto m = one_region(c);

  Size_window_stats st;
  auto              out = apply_size_window(c.g.get(), m, 0, 100, &st);

  const auto ge = region_ge(out);
  EXPECT_EQ(st.splits, 1u);
  EXPECT_GE(ge.size(), 4u) << "320 GE cannot fit in fewer than 4 regions of 100";
  for (const auto& [id, w] : ge) {
    EXPECT_LE(w, 100u) << "region " << id << " is over max";
  }
  EXPECT_EQ(st.left_over, 0u);
}

// Both halves at once: the window is satisfied on both ends simultaneously,
// which is the whole point of splitting before merging.
TEST(ColorSize, BothEndsSatisfiedTogether) {
  auto c = make_chain("lgdb_cs_both", "both", 60, 8);  // 480 GE
  auto m = all_singletons(c);

  Size_window_stats st;
  auto              out = apply_size_window(c.g.get(), m, 50, 120, &st);

  const auto ge = region_ge(out);
  ASSERT_FALSE(ge.empty());
  for (const auto& [id, w] : ge) {
    EXPECT_GE(w, 50u) << "region " << id << " under min";
    EXPECT_LE(w, 120u) << "region " << id << " over max";
  }
  EXPECT_EQ(st.left_under, 0u);
  EXPECT_EQ(st.left_over, 0u);
}

// A merge must never breach the ceiling to satisfy the floor. With min just
// above max/2, a naive merger would happily fuse two legal regions into an
// illegal one.
TEST(ColorSize, MergeNeverBreachesMax) {
  auto c = make_chain("lgdb_cs_cap", "cap", 30, 8);  // 240 GE
  auto m = all_singletons(c);

  Size_window_stats st;
  auto              out = apply_size_window(c.g.get(), m, 30, 40, &st);

  for (const auto& [id, w] : region_ge(out)) {
    EXPECT_LE(w, 40u) << "region " << id << " was merged past max";
  }
  EXPECT_EQ(st.left_over, 0u);
}

// Every input node must survive with exactly one region. A node the engine drops
// is a node pass.partition never emits -- a silent hole in the netlist.
TEST(ColorSize, EveryNodeKeepsExactlyOneRegion) {
  auto c = make_chain("lgdb_cs_total", "total", 25, 8);
  auto m = all_singletons(c);

  auto out = apply_size_window(c.g.get(), m, 30, 60, nullptr);

  ASSERT_EQ(out.size(), m.size());
  for (const auto& n : c.nodes) {
    ASSERT_TRUE(out.contains(n)) << "node dropped by the window";
    EXPECT_NE(out.at(n), livehd::color::NO_COLOR) << "0 is NO_COLOR, never a region id";
  }
}

// Ids are minted in forward_class() first-encounter order, so they are a function
// of the graph rather than of hash iteration. This is what lets a caller (and
// pass.partition's `<def>__c<id>` module names) be reproducible run to run.
TEST(ColorSize, IdsAreDeterministicAndDense) {
  auto c = make_chain("lgdb_cs_det", "det", 12, 8);
  auto m = all_singletons(c);

  auto a = apply_size_window(c.g.get(), m, 20, 0, nullptr);
  auto b = apply_size_window(c.g.get(), m, 20, 0, nullptr);

  ASSERT_EQ(a.size(), b.size());
  for (const auto& [n, id] : a) {
    EXPECT_EQ(b.at(n), id) << "the same input produced two different colorings";
  }

  // Dense 1..k, and the first region encountered walking forward is id 1.
  const auto ge = region_ge(a);
  for (const auto& [id, w] : ge) {
    (void)w;
    EXPECT_GE(id, 1);
    EXPECT_LE(id, static_cast<int>(ge.size()));
  }
  for (auto n : c.g->forward_class()) {
    if (a.contains(n)) {
      EXPECT_EQ(a.at(n), 1) << "ids are minted in forward_class order";
      break;
    }
  }
}

// A def whose whole body is below min has nothing to merge with. It must be
// REPORTED (left_under), not silently accepted as if the window held: this is
// exactly the case the cross-hierarchy absorb pass exists to fix.
TEST(ColorSize, DefBoundRegionIsReportedNotHidden) {
  auto c = make_chain("lgdb_cs_bound", "bound", 3, 8);  // 24 GE total, min is 1000
  auto m = all_singletons(c);

  Size_window_stats st;
  auto              out = apply_size_window(c.g.get(), m, 1000, 200000, &st);

  EXPECT_EQ(region_ge(out).size(), 1u) << "the whole def merges into one region";
  EXPECT_EQ(st.left_under, 1u) << "and that region is still under min -- say so";
}

// An indivisible node heavier than max cannot be split by anything. It must be
// counted, not dropped and not looped on forever.
TEST(ColorSize, IndivisibleOversizeNodeIsCountedNotDropped) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_cs_huge");
  auto  gio = lib.create_io("huge");
  gio->add_input("a", 0);
  gio->add_output("y", 1);
  auto g = gio->create_graph();

  auto in = g->get_input_pin("a");
  set_bits(in, 64);
  auto mult = create_typed_node(*g, Ntype_op::Mult);  // 64*64 = 4096 GE in one node
  in.connect_sink(mult.create_sink_pin(0));
  in.connect_sink(mult.create_sink_pin(1));
  auto d = mult.create_driver_pin(0);
  set_bits(d, 64);
  d.connect_sink(g->get_output_pin("y"));

  Node2Id m;
  m[mult] = 1;
  ASSERT_EQ(ge_weight(mult), 4096u);

  Size_window_stats st;
  auto              out = apply_size_window(g.get(), m, 0, 100, &st);

  EXPECT_EQ(out.size(), 1u) << "the node survives";
  EXPECT_EQ(st.left_over, 1u) << "an unsplittable region must be reported";
}

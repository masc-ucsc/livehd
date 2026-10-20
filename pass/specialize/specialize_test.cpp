// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// specialize::inherit_colors computes the sweep-to-fixpoint coloring in
// O(V + E). It must give every node exactly the color the plain sweep gives
// (the reference below), and a long chain colored from its far end -- one
// sweep per link for the plain loop, O(L^2) -- must stay linear.
#include <chrono>
#include <cstdint>
#include <random>
#include <vector>

#include "absl/container/flat_hash_set.h"
#include "fold_engine.hpp"
#include "gtest/gtest.h"
#include "node_util.hpp"
#include "specialize.hpp"

namespace {
namespace gu = livehd::graph_util;

// The sweep inherit_colors replaced: visit every open node in body order, take
// the first colored non-constant driver's color, else the first colored
// reader's, repeat until a sweep colors nothing.
void reference_inherit_colors(hhds::Graph& g, const absl::flat_hash_set<uint64_t>* existed) {
  std::vector<hhds::Node_class> open;
  bool                          colored = false;
  for (auto n : g.body().nodes()) {
    if (gu::has_color(n)) {
      colored = true;
    } else if (existed == nullptr || !existed->contains(static_cast<uint64_t>(n.get_debug_nid()))) {
      open.push_back(n);
    }
  }
  if (!colored) {
    return;
  }
  for (bool grew = true; grew && !open.empty();) {
    grew = false;
    std::erase_if(open, [&](const hhds::Node_class& n) {
      for (auto sink : n.inp_sorted_pins()) {
        for (auto drv : sink.get_driver_pins()) {
          if (!drv.is_const() && gu::has_color(drv.get_master_node())) {
            gu::set_color(n, gu::color_of(drv.get_master_node()));
            grew = true;
            return true;
          }
        }
      }
      for (const auto& e : n.out_edges()) {
        if (gu::has_color(e.sink.get_master_node())) {
          gu::set_color(n, gu::color_of(e.sink.get_master_node()));
          grew = true;
          return true;
        }
      }
      return false;
    });
  }
}

struct Random_design {
  hhds::GraphLibrary              library;
  std::shared_ptr<hhds::Graph>    graph;
  std::vector<hhds::Node_class>   nodes;
  absl::flat_hash_set<uint64_t>   existed;

  // The same seed builds the same graph: nodes, edges (forward and backward,
  // some from constants or graph inputs), initial colors and the `existed` set.
  explicit Random_design(uint32_t seed, int count) {
    std::mt19937 rng(seed);
    auto         io = library.create_io("top");
    io->add_input("a", 1);
    io->set_bits("a", 8);
    io->add_output("y", 2);
    io->set_bits("y", 8);
    graph = io->create_graph();
    for (int i = 0; i < count; ++i) {
      auto node = gu::create_typed_node(*graph, (rng() & 1U) != 0 ? Ntype_op::And : Ntype_op::Or);
      gu::set_ubits(node.create_driver_pin(0), 8);
      nodes.push_back(node);
    }
    for (int i = 0; i < count; ++i) {
      const int fanin = 1 + static_cast<int>(rng() % 3);
      for (int k = 0; k < fanin; ++k) {
        auto       sink = nodes[i].create_sink_pin(static_cast<hhds::Port_id>(k));
        const auto pick = rng() % 10;
        if (pick == 0) {
          sink.connect_driver(gu::create_const(*graph, *Dlop::create_integer(static_cast<int64_t>(rng() % 7))));
        } else if (pick == 1) {
          sink.connect_driver(graph->get_input_pin("a"));
        } else {
          sink.connect_driver(nodes[rng() % count].get_driver_pin(0));
        }
      }
    }
    nodes[rng() % count].get_driver_pin(0).connect_sink(graph->get_output_pin("y"));
    for (int i = 0; i < count; ++i) {
      const auto pick = rng() % 12;
      if (pick < 2) {
        gu::set_color(nodes[i], static_cast<int>(1 + rng() % 4));
      } else if (pick == 2) {
        existed.insert(static_cast<uint64_t>(nodes[i].get_debug_nid()));
      }
    }
  }
  std::vector<int> colors() const {
    std::vector<int> result;
    for (const auto& node : nodes) {
      result.push_back(gu::has_color(node) ? gu::color_of(node) : -1);
    }
    return result;
  }
};

TEST(InheritColors, MatchesTheSweepOnRandomGraphs) {
  size_t inherited = 0, conflicts = 0;
  for (uint32_t seed = 1; seed <= 200; ++seed) {
    const int     count = 5 + static_cast<int>(seed % 60);
    Random_design reference(seed, count);
    Random_design fast(seed, count);
    const auto    before      = reference.colors();
    const bool    use_existed = (seed % 3) != 0;
    reference_inherit_colors(*reference.graph, use_existed ? &reference.existed : nullptr);
    livehd::specialize::inherit_colors(*fast.graph, use_existed ? &fast.existed : nullptr);
    const auto after = reference.colors();
    ASSERT_EQ(after, fast.colors()) << "seed " << seed;
    absl::flat_hash_set<int> seen;
    for (size_t i = 0; i < after.size(); ++i) {
      inherited += before[i] < 0 && after[i] >= 0 ? 1 : 0;
      seen.insert(after[i]);
    }
    conflicts += seen.size() > 2 ? 1 : 0;
  }
  // Not vacuous: many nodes inherit, and most graphs mix several colors (the
  // order in which neighbors are seen decides which one a node takes).
  EXPECT_GT(inherited, 2000U);
  EXPECT_GT(conflicts, 100U);
}

// A chain whose only colored cell is its LAST: in body order every link reads
// the one after it, so the sweep colors one link per pass.
TEST(InheritColors, BackwardChainIsLinear) {
  hhds::GraphLibrary library;
  auto               io = library.create_io("top");
  io->add_input("a", 1);
  io->set_bits("a", 8);
  io->add_output("y", 2);
  io->set_bits("y", 8);
  auto                          graph = io->create_graph();
  constexpr int                 kLinks = 50000;
  std::vector<hhds::Node_class> chain;
  for (int i = 0; i < kLinks; ++i) {
    auto node = gu::create_typed_node(*graph, Ntype_op::Or);
    gu::set_ubits(node.create_driver_pin(0), 8);
    chain.push_back(node);
  }
  for (int i = 0; i + 1 < kLinks; ++i) {
    chain[i].create_sink_pin(0).connect_driver(chain[i + 1].get_driver_pin(0));
    chain[i].create_sink_pin(1).connect_driver(graph->get_input_pin("a"));
  }
  chain.back().create_sink_pin(0).connect_driver(graph->get_input_pin("a"));
  chain.front().get_driver_pin(0).connect_sink(graph->get_output_pin("y"));
  gu::set_color(chain.back(), 3);
  const auto start = std::chrono::steady_clock::now();
  livehd::specialize::inherit_colors(*graph, nullptr);
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
  for (const auto& node : chain) {
    ASSERT_TRUE(gu::has_color(node));
    ASSERT_EQ(gu::color_of(node), 3);
  }
  // The sweep needed kLinks passes over the chain (kLinks^2 / 2 node visits);
  // linear is milliseconds at -c opt and well under the bound in dbg.
  EXPECT_LT(ms, 10000) << "inherit_colors took " << ms << " ms on a " << kLinks << "-link chain";
}

}  // namespace

// refold_private_body is the fold site cgen_sim (post-splice, sim cost), the synthesis state sweep (synth cost)
// and specialize_constants share: it follows the process-wide fold engine and counts under its site.
TEST(SpecializeRefold, FollowsTheProcessWideFoldEngine) {
  for (const auto engine : {livehd::Fold_engine::classic, livehd::Fold_engine::aegraph, livehd::Fold_engine::aegraph_classic}) {
    livehd::fold_counters_reset();
    livehd::set_fold_engine(engine);
    hhds::GraphLibrary lib;
    auto               io = lib.create_io("refold_site");
    io->add_output("o0", 0);
    io->set_bits("o0", 8);
    io->set_unsign("o0", true);
    auto g   = io->create_graph();
    auto sum = gu::create_typed_node(*g, Ntype_op::Sum);
    gu::create_const(*g, *Dlop::create_integer(20)).connect_sink(gu::setup_sink_by_name(sum, "as"));
    gu::create_const(*g, *Dlop::create_integer(22)).connect_sink(gu::setup_sink_by_name(sum, "as"));
    sum.create_driver_pin(0).connect_sink(g->get_output_pin("o0"));
    livehd::specialize::refold_private_body(g, livehd::specialize::Refold_cost::sim, "test.refold");
    EXPECT_GE(livehd::fold_count("test.refold", engine), 1u) << livehd::fold_engine_name(engine);
    for (auto node : g->body().nodes()) {
      EXPECT_NE(gu::type_op_of(node), Ntype_op::Sum) << livehd::fold_engine_name(engine) << ": the constant sum folds";
    }
  }
  livehd::set_fold_engine(livehd::Fold_engine::classic);
}

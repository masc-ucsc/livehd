//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "upass_runner_lgraph.hpp"

#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "graph_library_singleton.hpp"
#include "gtest/gtest.h"
#include "hhds/graph.hpp"
#include "lgraph_manager.hpp"
#include "node_util.hpp"
#include "upass_lgraph_core.hpp"

namespace {

struct Test_lgraph_cycle_a : public upass::uPass_lgraph {
  using upass::uPass_lgraph::uPass_lgraph;
};

struct Test_lgraph_cycle_b : public upass::uPass_lgraph {
  using upass::uPass_lgraph::uPass_lgraph;
};

struct Test_lgraph_missing_dep : public upass::uPass_lgraph {
  using upass::uPass_lgraph::uPass_lgraph;
};

static upass::uPass_lgraph_plugin plugin_lgraph_cycle_a("__upass_lgraph_cycle_a",
                                                        upass::uPass_lgraph_wrapper<Test_lgraph_cycle_a>::get_upass,
                                                        {"__upass_lgraph_cycle_b"});

static upass::uPass_lgraph_plugin plugin_lgraph_cycle_b("__upass_lgraph_cycle_b",
                                                        upass::uPass_lgraph_wrapper<Test_lgraph_cycle_b>::get_upass,
                                                        {"__upass_lgraph_cycle_a"});

static upass::uPass_lgraph_plugin plugin_lgraph_missing_dep("__upass_lgraph_missing_dep",
                                                            upass::uPass_lgraph_wrapper<Test_lgraph_missing_dep>::get_upass,
                                                            {"__upass_lgraph_dep_not_defined"});

class Exposed_runner_lgraph : public uPass_runner_lgraph {
public:
  Exposed_runner_lgraph(std::shared_ptr<upass::Lgraph_manager> _gm, const std::vector<std::string>& names = {})
      : uPass_runner_lgraph(std::move(_gm), names) {}

  std::vector<std::string> expose_resolve(const std::vector<std::string>& names) const { return resolve_order(names); }
};

std::shared_ptr<hhds::Graph> make_graph(std::string_view path, std::string_view name) {
  auto& lib = livehd::Hhds_graph_library::instance(path);
  auto  gio = lib.create_io(name);
  return gio->create_graph();
}

// Builds a graph with two constant drivers feeding a Sum, plus a downstream Xor
// keeping the fold result alive so consumers exist.
std::shared_ptr<hhds::Graph> make_simple_fold_graph(std::string_view path, std::string_view name) {
  auto g  = make_graph(path, name);
  auto c0 = livehd::graph_util::create_const(*g, *Dlop::create_integer(7));
  auto c1 = livehd::graph_util::create_const(*g, *Dlop::create_integer(11));
  auto s0 = livehd::graph_util::create_typed_node(*g, Ntype_op::Sum);
  auto x0 = livehd::graph_util::create_typed_node(*g, Ntype_op::Xor);
  c0.connect_sink(s0.create_sink_pin(0));
  c1.connect_sink(s0.create_sink_pin(1));
  c0.connect_sink(x0.create_sink_pin(0));
  return g;
}

TEST(UpassRunnerLgraph, VisitsFastNodesAndCollectsTypes) {
  auto g  = make_simple_fold_graph("lgdb_upass_runner_lgraph_test", "upass_runner_lgraph_test");
  auto gm = std::make_shared<upass::Lgraph_manager>(g);

  auto ss = gm->scan_fold_candidates();
  // Note: with the HHDS migration, constants are pins on the CONST_NODE
  // singleton — fast_class() skips that node, so const_nodes counts only
  // legacy Nconst-typed regular nodes (which we no longer create).
  EXPECT_GE(ss.arithmetic_nodes, 2U);
  EXPECT_GE(ss.fold_candidate_nodes, 1U);

  uPass_runner_lgraph runner(gm);
  EXPECT_GE(runner.visit_fast(), 2U);
  runner.run(2);
}

TEST(UpassRunnerLgraphResolve, DetectCycle) {
  auto                  g  = make_graph("lgdb_upass_runner_lgraph_cycle", "upass_runner_lgraph_cycle");
  auto                  gm = std::make_shared<upass::Lgraph_manager>(g);
  Exposed_runner_lgraph runner(gm, {});
  auto                  ordered = runner.expose_resolve({"__upass_lgraph_cycle_a"});
  EXPECT_TRUE(ordered.empty());
}

TEST(UpassRunnerLgraphResolve, DetectMissingDependency) {
  auto                  g  = make_graph("lgdb_upass_runner_lgraph_missing", "upass_runner_lgraph_missing");
  auto                  gm = std::make_shared<upass::Lgraph_manager>(g);
  Exposed_runner_lgraph runner(gm, {});
  auto                  ordered = runner.expose_resolve({"__upass_lgraph_missing_dep"});
  EXPECT_TRUE(ordered.empty());
}

TEST(UpassRunnerLgraphResolve, SharedNoopResolvesAndRuns) {
  auto                  g  = make_graph("lgdb_upass_runner_lgraph_shared_noop", "upass_runner_lgraph_shared_noop");
  (void)livehd::graph_util::create_const(*g, *Dlop::create_integer(1));
  auto                  gm = std::make_shared<upass::Lgraph_manager>(g);
  Exposed_runner_lgraph runner(gm, {"noop_shared"});
  auto                  ordered = runner.expose_resolve({"noop_shared"});
  ASSERT_EQ(ordered.size(), 1U);
  EXPECT_EQ(ordered[0], "noop_shared");
  EXPECT_FALSE(runner.has_configuration_error());
  runner.run(1);
}

TEST(UpassRunnerLgraphResolve, SharedDecideResolvesAndRuns) {
  auto                  g  = make_simple_fold_graph("lgdb_upass_runner_lgraph_shared_decide", "upass_runner_lgraph_shared_decide");
  auto                  gm = std::make_shared<upass::Lgraph_manager>(g);
  Exposed_runner_lgraph runner(gm, {"decide_shared"});
  auto                  ordered = runner.expose_resolve({"decide_shared"});
  ASSERT_EQ(ordered.size(), 1U);
  EXPECT_EQ(ordered[0], "decide_shared");
  EXPECT_FALSE(runner.has_configuration_error());
  runner.run(1);
}

TEST(UpassRunnerLgraph, FoldSumConstMutation) {
  auto g  = make_simple_fold_graph("lgdb_upass_runner_lgraph_fold_sum_const", "upass_runner_lgraph_fold_sum_const");
  auto gm = std::make_shared<upass::Lgraph_manager>(g);

  auto ss = gm->fold_sum_const();
  EXPECT_EQ(ss.folded_nodes, 1U);
  EXPECT_GE(ss.new_const_nodes, 1U);
  EXPECT_EQ(ss.deleted_nodes, 1U);

  std::size_t sum_count = 0;
  for (const auto& n : g->fast_class()) {
    if (livehd::graph_util::type_op_of(n) == Ntype_op::Sum) {
      ++sum_count;
    }
  }
  EXPECT_EQ(sum_count, 0U);
}

TEST(UpassRunnerLgraph, FoldDceRemovesIsolatedNode) {
  auto g  = make_graph("lgdb_upass_runner_lgraph_dce", "upass_runner_lgraph_dce");
  // Create a Sum node with no consumers and no input edges — pure dead.
  (void)livehd::graph_util::create_typed_node(*g, Ntype_op::Sum);
  auto gm = std::make_shared<upass::Lgraph_manager>(g);

  auto s0 = gm->fold_dce();
  EXPECT_EQ(s0.dead_nodes_removed, 1U);

  std::size_t sum_count = 0;
  for (const auto& n : g->fast_class()) {
    if (livehd::graph_util::type_op_of(n) == Ntype_op::Sum) {
      ++sum_count;
    }
  }
  EXPECT_EQ(sum_count, 0U);
}

TEST(UpassRunnerLgraph, FoldSubConstSelfCancellation) {
  auto g  = make_graph("lgdb_upass_runner_lgraph_sub_self", "upass_runner_lgraph_sub_self");
  auto c0 = livehd::graph_util::create_const(*g, *Dlop::create_integer(5));
  auto s0 = livehd::graph_util::create_typed_node(*g, Ntype_op::Sum);
  // Wire same driver into both A (pid=0) and B (pid=1) — should fold to 0.
  c0.connect_sink(s0.create_sink_pin(0));
  c0.connect_sink(s0.create_sink_pin(1));
  // Add a downstream consumer so the new const is kept (not DCE'd).
  auto x0 = livehd::graph_util::create_typed_node(*g, Ntype_op::Xor);
  s0.create_driver_pin(0).connect_sink(x0.create_sink_pin(0));

  auto gm = std::make_shared<upass::Lgraph_manager>(g);
  auto s  = gm->fold_sub_const();
  EXPECT_EQ(s.sub_self_simplified, 1U);
  EXPECT_EQ(s.deleted_nodes, 1U);
}

TEST(UpassRunnerLgraph, TagFoldCandidates) {
  auto g  = make_simple_fold_graph("lgdb_upass_runner_lgraph_tag", "upass_runner_lgraph_tag");
  auto gm = std::make_shared<upass::Lgraph_manager>(g);
  auto s  = gm->tag_fold_candidates();
  EXPECT_GE(s.tagged_nodes, 1U);
}

}  // namespace

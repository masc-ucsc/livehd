//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "file_utils.hpp"
#include "gtest/gtest.h"
#include "lgraph.hpp"
#include "lgraph_manager.hpp"
#include "upass_lgraph_core.hpp"
#include "upass_runner_lgraph.hpp"

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

static upass::uPass_lgraph_plugin plugin_lgraph_cycle_a(
    "__upass_lgraph_cycle_a",
    upass::uPass_lgraph_wrapper<Test_lgraph_cycle_a>::get_upass,
    {"__upass_lgraph_cycle_b"});

static upass::uPass_lgraph_plugin plugin_lgraph_cycle_b(
    "__upass_lgraph_cycle_b",
    upass::uPass_lgraph_wrapper<Test_lgraph_cycle_b>::get_upass,
    {"__upass_lgraph_cycle_a"});

static upass::uPass_lgraph_plugin plugin_lgraph_missing_dep(
    "__upass_lgraph_missing_dep",
    upass::uPass_lgraph_wrapper<Test_lgraph_missing_dep>::get_upass,
    {"__upass_lgraph_dep_not_defined"});

class Exposed_runner_lgraph : public uPass_runner_lgraph {
public:
  Exposed_runner_lgraph(std::shared_ptr<upass::Lgraph_manager> gm, const std::vector<std::string> &names = {})
      : uPass_runner_lgraph(std::move(gm), names) {}

  std::vector<std::string> expose_resolve(const std::vector<std::string> &names) const { return resolve_order(names); }
};

TEST(UpassRunnerLgraph, VisitsFastNodesAndCollectsTypes) {
  constexpr std::string_view kDbPath = "lgdb_upass_runner_lgraph_test";

  file_utils::clean_dir(kDbPath);
  auto *lib = Graph_library::instance(kDbPath);
  ASSERT_NE(lib, nullptr);

  auto *lg = lib->create_lgraph("top", "upass_runner_lgraph_test");
  ASSERT_NE(lg, nullptr);

  auto c0 = lg->create_node_const(7);
  auto c1 = lg->create_node_const(11);
  auto s0 = lg->create_node(Ntype_op::Sum);
  auto x0 = lg->create_node(Ntype_op::Xor);
  (void)lg->create_node(Ntype_op::CompileErr);

  lg->add_edge(c0.get_driver_pin(), s0.setup_sink_pin("A"));
  lg->add_edge(c1.get_driver_pin(), s0.setup_sink_pin("B"));
  lg->add_edge(c0.get_driver_pin(), x0.setup_sink_pin("A"));

  auto gm = std::make_shared<upass::Lgraph_manager>(lg);
  auto ss = gm->scan_fold_candidates();
  EXPECT_EQ(ss.visited_nodes, 5U);
  EXPECT_EQ(ss.const_nodes, 2U);
  EXPECT_EQ(ss.arithmetic_nodes, 2U);
  EXPECT_EQ(ss.fold_candidate_nodes, 2U);

  uPass_runner_lgraph runner(gm);
  auto                types = runner.collect_type_names();

  ASSERT_EQ(types.size(), 5U);
  EXPECT_EQ(types[0], "const");
  EXPECT_EQ(types[1], "const");
  EXPECT_EQ(types[2], "sum");
  EXPECT_EQ(types[3], "xor");
  EXPECT_EQ(types[4], "compile_err");
  EXPECT_EQ(runner.visit_fast(), 5U);
  runner.run(2);
  EXPECT_EQ(runner.get_last_visited_count(), 5U);
  auto rs = runner.get_last_scan_summary();
  EXPECT_EQ(rs.fold_candidate_nodes, 2U);
  EXPECT_FALSE(s0.has_color());
  EXPECT_FALSE(x0.has_color());

  uPass_runner_lgraph tag_runner(gm, {"fold_tag"});
  tag_runner.run(3);
  EXPECT_TRUE(s0.has_color());
  EXPECT_TRUE(x0.has_color());
  EXPECT_EQ(s0.get_color(), 7);
  EXPECT_EQ(x0.get_color(), 7);
}

TEST(UpassRunnerLgraphResolve, DetectCycle) {
  constexpr std::string_view kDbPath = "lgdb_upass_runner_lgraph_cycle";
  file_utils::clean_dir(kDbPath);
  auto *lib = Graph_library::instance(kDbPath);
  ASSERT_NE(lib, nullptr);
  auto *lg = lib->create_lgraph("top", "upass_runner_lgraph_cycle");
  ASSERT_NE(lg, nullptr);

  auto gm = std::make_shared<upass::Lgraph_manager>(lg);
  Exposed_runner_lgraph runner(gm, {});
  auto                 ordered = runner.expose_resolve({"__upass_lgraph_cycle_a"});
  EXPECT_TRUE(ordered.empty());
}

TEST(UpassRunnerLgraphResolve, DetectMissingDependency) {
  constexpr std::string_view kDbPath = "lgdb_upass_runner_lgraph_missing";
  file_utils::clean_dir(kDbPath);
  auto *lib = Graph_library::instance(kDbPath);
  ASSERT_NE(lib, nullptr);
  auto *lg = lib->create_lgraph("top", "upass_runner_lgraph_missing");
  ASSERT_NE(lg, nullptr);

  auto gm = std::make_shared<upass::Lgraph_manager>(lg);
  Exposed_runner_lgraph runner(gm, {});
  auto                 ordered = runner.expose_resolve({"__upass_lgraph_missing_dep"});
  EXPECT_TRUE(ordered.empty());
}

TEST(UpassRunnerLgraphResolve, SharedNoopResolvesAndRuns) {
  constexpr std::string_view kDbPath = "lgdb_upass_runner_lgraph_shared_noop";
  file_utils::clean_dir(kDbPath);
  auto *lib = Graph_library::instance(kDbPath);
  ASSERT_NE(lib, nullptr);
  auto *lg = lib->create_lgraph("top", "upass_runner_lgraph_shared_noop");
  ASSERT_NE(lg, nullptr);
  (void)lg->create_node_const(1);

  auto gm = std::make_shared<upass::Lgraph_manager>(lg);
  Exposed_runner_lgraph runner(gm, {"noop_shared"});
  auto                 ordered = runner.expose_resolve({"noop_shared"});
  ASSERT_EQ(ordered.size(), 1U);
  EXPECT_EQ(ordered[0], "noop_shared");
  EXPECT_FALSE(runner.has_configuration_error());
  runner.run(1);
}

TEST(UpassRunnerLgraphResolve, SharedDecideResolvesAndRuns) {
  constexpr std::string_view kDbPath = "lgdb_upass_runner_lgraph_shared_decide";
  file_utils::clean_dir(kDbPath);
  auto *lib = Graph_library::instance(kDbPath);
  ASSERT_NE(lib, nullptr);
  auto *lg = lib->create_lgraph("top", "upass_runner_lgraph_shared_decide");
  ASSERT_NE(lg, nullptr);
  auto c0 = lg->create_node_const(3);
  auto c1 = lg->create_node_const(5);
  auto s0 = lg->create_node(Ntype_op::Sum);
  lg->add_edge(c0.get_driver_pin(), s0.setup_sink_pin_raw(0));
  lg->add_edge(c1.get_driver_pin(), s0.setup_sink_pin_raw(1));

  auto gm = std::make_shared<upass::Lgraph_manager>(lg);
  Exposed_runner_lgraph runner(gm, {"decide_shared"});
  auto                 ordered = runner.expose_resolve({"decide_shared"});
  ASSERT_EQ(ordered.size(), 1U);
  EXPECT_EQ(ordered[0], "decide_shared");
  EXPECT_FALSE(runner.has_configuration_error());
  runner.run(1);
}

TEST(UpassRunnerLgraph, FoldSumConstMutation) {
  constexpr std::string_view kDbPath = "lgdb_upass_runner_lgraph_fold_sum_const";
  file_utils::clean_dir(kDbPath);
  auto *lib = Graph_library::instance(kDbPath);
  ASSERT_NE(lib, nullptr);
  auto *lg = lib->create_lgraph("top", "upass_runner_lgraph_fold_sum_const");
  ASSERT_NE(lg, nullptr);

  auto c0   = lg->create_node_const(5);
  auto c1   = lg->create_node_const(8);
  auto s0   = lg->create_node(Ntype_op::Sum);
  auto s1   = lg->create_node(Ntype_op::Sum);
  auto out0 = lg->add_graph_output("o_sum0", 2, 9);
  auto out1 = lg->add_graph_output("o_sum1", 3, 13);

  auto s0_out = s0.setup_driver_pin();
  auto s1_out = s1.setup_driver_pin();
  s0_out.set_bits(9);
  s0_out.set_sign();
  s1_out.set_bits(13);
  s1_out.set_unsign();

  lg->add_edge(c0.get_driver_pin(), s0.setup_sink_pin_raw(0));
  lg->add_edge(c1.get_driver_pin(), s0.setup_sink_pin_raw(1));
  lg->add_edge(c0.get_driver_pin(), s1.setup_sink_pin_raw(0));
  lg->add_edge(s0_out, s1.setup_sink_pin_raw(1));
  lg->add_edge(s0_out, out0);
  lg->add_edge(s1_out, out1);

  auto gm = std::make_shared<upass::Lgraph_manager>(lg);
  uPass_runner_lgraph runner(gm, {"fold_sum_const"});
  auto                ss = gm->fold_sum_const();
  EXPECT_EQ(ss.folded_nodes, 2U);
  EXPECT_GE(ss.rewired_edges, 1U);
  EXPECT_EQ(ss.new_const_nodes, 2U);
  EXPECT_EQ(ss.deleted_nodes, 2U);
  // Rebuild the same graph for runner path checks after direct mutation call.
  file_utils::clean_dir(kDbPath);
  lib = Graph_library::instance(kDbPath);
  ASSERT_NE(lib, nullptr);
  lg = lib->create_lgraph("top", "upass_runner_lgraph_fold_sum_const");
  ASSERT_NE(lg, nullptr);
  c0   = lg->create_node_const(5);
  c1   = lg->create_node_const(8);
  s0   = lg->create_node(Ntype_op::Sum);
  s1   = lg->create_node(Ntype_op::Sum);
  out0 = lg->add_graph_output("o_sum0", 2, 9);
  out1 = lg->add_graph_output("o_sum1", 3, 13);
  s0_out = s0.setup_driver_pin();
  s1_out = s1.setup_driver_pin();
  s0_out.set_bits(9);
  s0_out.set_sign();
  s1_out.set_bits(13);
  s1_out.set_unsign();
  lg->add_edge(c0.get_driver_pin(), s0.setup_sink_pin_raw(0));
  lg->add_edge(c1.get_driver_pin(), s0.setup_sink_pin_raw(1));
  lg->add_edge(c0.get_driver_pin(), s1.setup_sink_pin_raw(0));
  lg->add_edge(s0_out, s1.setup_sink_pin_raw(1));
  lg->add_edge(s0_out, out0);
  lg->add_edge(s1_out, out1);
  gm = std::make_shared<upass::Lgraph_manager>(lg);
  uPass_runner_lgraph runner2(gm, {"fold_sum_const"});
  runner2.run(3);

  auto out0_dpin = lg->get_graph_output("o_sum0").get_driver_pin();
  auto out1_dpin = lg->get_graph_output("o_sum1").get_driver_pin();
  EXPECT_EQ(out0_dpin.get_type_op(), Ntype_op::Const);
  EXPECT_EQ(out0_dpin.get_bits(), 9);
  EXPECT_FALSE(out0_dpin.is_unsign());
  EXPECT_EQ(out1_dpin.get_type_op(), Ntype_op::Const);
  EXPECT_EQ(out1_dpin.get_bits(), 13);
  EXPECT_TRUE(out1_dpin.is_unsign());

  std::size_t sum_count   = 0;
  std::size_t const_count = 0;
  for (const auto &n : lg->fast()) {
    if (n.get_type_op() == Ntype_op::Sum) {
      ++sum_count;
    } else if (n.get_type_op() == Ntype_op::Const) {
      ++const_count;
    }
  }

  EXPECT_EQ(sum_count, 0U);
  EXPECT_GE(const_count, 4U);
}

TEST(UpassRunnerLgraph, FoldNeutralMutation) {
  constexpr std::string_view kDbPath = "lgdb_upass_runner_lgraph_fold_neutral";
  file_utils::clean_dir(kDbPath);
  auto *lib = Graph_library::instance(kDbPath);
  ASSERT_NE(lib, nullptr);
  auto *lg = lib->create_lgraph("top", "upass_runner_lgraph_fold_neutral");
  ASSERT_NE(lg, nullptr);

  auto in_a = lg->add_graph_input("a", 1, 8);
  auto in_b = lg->add_graph_input("b", 9, 1);
  auto out0 = lg->add_graph_output("o_sum", 2, 8);
  auto out1 = lg->add_graph_output("o_and", 3, 8);
  auto out2 = lg->add_graph_output("o_mul", 4, 8);
  auto out3 = lg->add_graph_output("o_or", 5, 8);
  auto out4 = lg->add_graph_output("o_xor", 6, 8);
  auto out5 = lg->add_graph_output("o_mul0", 7, 8);
  auto out6 = lg->add_graph_output("o_and_id", 8, 8);
  auto out7 = lg->add_graph_output("o_and1", 10, 1);
  auto out8 = lg->add_graph_output("o_or1", 11, 1);
  auto c0   = lg->create_node_const(0);
  auto c1   = lg->create_node_const(1);
  auto s0   = lg->create_node(Ntype_op::Sum);
  auto a0   = lg->create_node(Ntype_op::And);
  auto m0   = lg->create_node(Ntype_op::Mult);
  auto o0   = lg->create_node(Ntype_op::Or);
  auto x0   = lg->create_node(Ntype_op::Xor);
  auto m1   = lg->create_node(Ntype_op::Mult);
  auto a1   = lg->create_node(Ntype_op::And);
  auto a2   = lg->create_node(Ntype_op::And);
  auto o1   = lg->create_node(Ntype_op::Or);

  auto s0_out = s0.setup_driver_pin();
  auto a0_out = a0.setup_driver_pin();
  auto m0_out = m0.setup_driver_pin();
  auto o0_out = o0.setup_driver_pin();
  auto x0_out = x0.setup_driver_pin();
  auto m1_out = m1.setup_driver_pin();
  auto a1_out = a1.setup_driver_pin();
  auto a2_out = a2.setup_driver_pin();
  auto o1_out = o1.setup_driver_pin();

  s0_out.set_bits(8);
  s0_out.set_unsign();
  a0_out.set_bits(8);
  a0_out.set_sign();
  m0_out.set_bits(8);
  m0_out.set_unsign();
  o0_out.set_bits(8);
  o0_out.set_unsign();
  x0_out.set_bits(8);
  x0_out.set_sign();
  m1_out.set_bits(8);
  m1_out.set_sign();
  a1_out.set_bits(8);
  a1_out.set_unsign();
  a2_out.set_bits(1);
  a2_out.set_sign();
  o1_out.set_bits(1);
  o1_out.set_sign();

  lg->add_edge(in_a, s0.setup_sink_pin_raw(0));
  lg->add_edge(c0.get_driver_pin(), s0.setup_sink_pin_raw(1));
  lg->add_edge(s0_out, out0);

  lg->add_edge(in_a, a0.setup_sink_pin_raw(0));
  lg->add_edge(c0.get_driver_pin(), a0.setup_sink_pin_raw(0));
  lg->add_edge(a0_out, out1);

  lg->add_edge(in_a, m0.setup_sink_pin_raw(0));
  lg->add_edge(c1.get_driver_pin(), m0.setup_sink_pin_raw(0));
  lg->add_edge(m0_out, out2);

  lg->add_edge(in_a, o0.setup_sink_pin_raw(0));
  lg->add_edge(in_a, o0.setup_sink_pin_raw(0));
  lg->add_edge(o0_out, out3);

  lg->add_edge(in_a, x0.setup_sink_pin_raw(0));
  lg->add_edge(in_a, x0.setup_sink_pin_raw(0));
  lg->add_edge(x0_out, out4);

  lg->add_edge(in_a, m1.setup_sink_pin_raw(0));
  lg->add_edge(c0.get_driver_pin(), m1.setup_sink_pin_raw(0));
  lg->add_edge(m1_out, out5);

  lg->add_edge(in_a, a1.setup_sink_pin_raw(0));
  lg->add_edge(in_a, a1.setup_sink_pin_raw(0));
  lg->add_edge(a1_out, out6);

  lg->add_edge(in_b, a2.setup_sink_pin_raw(0));
  lg->add_edge(c1.get_driver_pin(), a2.setup_sink_pin_raw(0));
  lg->add_edge(a2_out, out7);

  lg->add_edge(in_b, o1.setup_sink_pin_raw(0));
  lg->add_edge(c1.get_driver_pin(), o1.setup_sink_pin_raw(0));
  lg->add_edge(o1_out, out8);

  auto gm = std::make_shared<upass::Lgraph_manager>(lg);
  auto ns = gm->fold_neutral_const();
  EXPECT_EQ(ns.simplified_to_driver + ns.simplified_to_const_zero + ns.simplified_to_const_one, 9U);
  EXPECT_GE(ns.simplified_to_const_one, 1U);
  EXPECT_GE(ns.rewired_edges, 9U);
  EXPECT_GE(ns.new_const_nodes, 3U);
  EXPECT_EQ(ns.deleted_nodes, 9U);
  // Rebuild graph for runner path checks after direct mutation call.
  file_utils::clean_dir(kDbPath);
  lib = Graph_library::instance(kDbPath);
  ASSERT_NE(lib, nullptr);
  lg = lib->create_lgraph("top", "upass_runner_lgraph_fold_neutral");
  ASSERT_NE(lg, nullptr);
  in_a = lg->add_graph_input("a", 1, 8);
  in_b = lg->add_graph_input("b", 9, 1);
  out0 = lg->add_graph_output("o_sum", 2, 8);
  out1 = lg->add_graph_output("o_and", 3, 8);
  out2 = lg->add_graph_output("o_mul", 4, 8);
  out3 = lg->add_graph_output("o_or", 5, 8);
  out4 = lg->add_graph_output("o_xor", 6, 8);
  out5 = lg->add_graph_output("o_mul0", 7, 8);
  out6 = lg->add_graph_output("o_and_id", 8, 8);
  out7 = lg->add_graph_output("o_and1", 10, 1);
  out8 = lg->add_graph_output("o_or1", 11, 1);
  c0   = lg->create_node_const(0);
  c1   = lg->create_node_const(1);
  s0   = lg->create_node(Ntype_op::Sum);
  a0   = lg->create_node(Ntype_op::And);
  m0   = lg->create_node(Ntype_op::Mult);
  o0   = lg->create_node(Ntype_op::Or);
  x0   = lg->create_node(Ntype_op::Xor);
  m1   = lg->create_node(Ntype_op::Mult);
  a1   = lg->create_node(Ntype_op::And);
  a2   = lg->create_node(Ntype_op::And);
  o1   = lg->create_node(Ntype_op::Or);
  s0_out = s0.setup_driver_pin();
  a0_out = a0.setup_driver_pin();
  m0_out = m0.setup_driver_pin();
  o0_out = o0.setup_driver_pin();
  x0_out = x0.setup_driver_pin();
  m1_out = m1.setup_driver_pin();
  a1_out = a1.setup_driver_pin();
  a2_out = a2.setup_driver_pin();
  o1_out = o1.setup_driver_pin();

  s0_out.set_bits(8);
  s0_out.set_unsign();
  a0_out.set_bits(8);
  a0_out.set_sign();
  m0_out.set_bits(8);
  m0_out.set_unsign();
  o0_out.set_bits(8);
  o0_out.set_unsign();
  x0_out.set_bits(8);
  x0_out.set_sign();
  m1_out.set_bits(8);
  m1_out.set_sign();
  a1_out.set_bits(8);
  a1_out.set_unsign();
  a2_out.set_bits(1);
  a2_out.set_sign();
  o1_out.set_bits(1);
  o1_out.set_sign();

  lg->add_edge(in_a, s0.setup_sink_pin_raw(0));
  lg->add_edge(c0.get_driver_pin(), s0.setup_sink_pin_raw(1));
  lg->add_edge(s0_out, out0);
  lg->add_edge(in_a, a0.setup_sink_pin_raw(0));
  lg->add_edge(c0.get_driver_pin(), a0.setup_sink_pin_raw(0));
  lg->add_edge(a0_out, out1);
  lg->add_edge(in_a, m0.setup_sink_pin_raw(0));
  lg->add_edge(c1.get_driver_pin(), m0.setup_sink_pin_raw(0));
  lg->add_edge(m0_out, out2);
  lg->add_edge(in_a, o0.setup_sink_pin_raw(0));
  lg->add_edge(in_a, o0.setup_sink_pin_raw(0));
  lg->add_edge(o0_out, out3);
  lg->add_edge(in_a, x0.setup_sink_pin_raw(0));
  lg->add_edge(in_a, x0.setup_sink_pin_raw(0));
  lg->add_edge(x0_out, out4);
  lg->add_edge(in_a, m1.setup_sink_pin_raw(0));
  lg->add_edge(c0.get_driver_pin(), m1.setup_sink_pin_raw(0));
  lg->add_edge(m1_out, out5);
  lg->add_edge(in_a, a1.setup_sink_pin_raw(0));
  lg->add_edge(in_a, a1.setup_sink_pin_raw(0));
  lg->add_edge(a1_out, out6);
  lg->add_edge(in_b, a2.setup_sink_pin_raw(0));
  lg->add_edge(c1.get_driver_pin(), a2.setup_sink_pin_raw(0));
  lg->add_edge(a2_out, out7);
  lg->add_edge(in_b, o1.setup_sink_pin_raw(0));
  lg->add_edge(c1.get_driver_pin(), o1.setup_sink_pin_raw(0));
  lg->add_edge(o1_out, out8);
  gm = std::make_shared<upass::Lgraph_manager>(lg);
  uPass_runner_lgraph runner(gm, {"fold_neutral"});
  runner.run(3);

  auto out1_dpin = lg->get_graph_output("o_and").get_driver_pin();
  auto out4_dpin = lg->get_graph_output("o_xor").get_driver_pin();
  auto out8_dpin = lg->get_graph_output("o_or1").get_driver_pin();
  EXPECT_EQ(out1_dpin.get_type_op(), Ntype_op::Const);
  EXPECT_EQ(out1_dpin.get_bits(), 8);
  EXPECT_FALSE(out1_dpin.is_unsign());
  EXPECT_EQ(out4_dpin.get_type_op(), Ntype_op::Const);
  EXPECT_EQ(out4_dpin.get_bits(), 8);
  EXPECT_FALSE(out4_dpin.is_unsign());
  EXPECT_EQ(out8_dpin.get_type_op(), Ntype_op::Const);
  EXPECT_EQ(out8_dpin.get_bits(), 1);
  EXPECT_FALSE(out8_dpin.is_unsign());

  std::size_t sum_count = 0;
  std::size_t and_count = 0;
  std::size_t mul_count = 0;
  std::size_t or_count  = 0;
  std::size_t xor_count = 0;
  for (const auto &n : lg->fast()) {
    if (n.get_type_op() == Ntype_op::Sum) {
      ++sum_count;
    } else if (n.get_type_op() == Ntype_op::And) {
      ++and_count;
    } else if (n.get_type_op() == Ntype_op::Mult) {
      ++mul_count;
    } else if (n.get_type_op() == Ntype_op::Or) {
      ++or_count;
    } else if (n.get_type_op() == Ntype_op::Xor) {
      ++xor_count;
    }
  }
  EXPECT_EQ(sum_count, 0U);
  EXPECT_EQ(and_count, 0U);
  EXPECT_EQ(mul_count, 0U);
  EXPECT_EQ(or_count, 0U);
  EXPECT_EQ(xor_count, 0U);

  std::size_t const_count = 0;
  for (const auto &n : lg->fast()) {
    if (n.get_type_op() == Ntype_op::Const) {
      ++const_count;
    }
  }
  EXPECT_GE(const_count, 1U);
}

TEST(UpassRunnerLgraph, FoldShiftDivMutation) {
  constexpr std::string_view kDbPath = "lgdb_upass_runner_lgraph_fold_shift_div";
  file_utils::clean_dir(kDbPath);
  auto *lib = Graph_library::instance(kDbPath);
  ASSERT_NE(lib, nullptr);
  auto *lg = lib->create_lgraph("top", "upass_runner_lgraph_fold_shift_div");
  ASSERT_NE(lg, nullptr);

  auto in_a = lg->add_graph_input("a", 1, 8);
  auto out0 = lg->add_graph_output("o_div1", 2, 8);
  auto out1 = lg->add_graph_output("o_shl0", 3, 8);
  auto out2 = lg->add_graph_output("o_sra0", 4, 8);
  auto out3 = lg->add_graph_output("o_div0num", 5, 8);
  auto out4 = lg->add_graph_output("o_divxx", 6, 8);
  auto out5 = lg->add_graph_output("o_div00", 8, 8);
  auto out6 = lg->add_graph_output("o_div62", 9, 8);
  auto c0   = lg->create_node_const(0);
  auto c1   = lg->create_node_const(1);
  auto c2   = lg->create_node_const(2);
  auto c6   = lg->create_node_const(6);
  auto c5   = lg->create_node_const(5);
  auto d0   = lg->create_node(Ntype_op::Div);
  auto s0   = lg->create_node(Ntype_op::SHL);
  auto r0   = lg->create_node(Ntype_op::SRA);
  auto d1   = lg->create_node(Ntype_op::Div);
  auto d2   = lg->create_node(Ntype_op::Div);
  auto d3   = lg->create_node(Ntype_op::Div);
  auto d4   = lg->create_node(Ntype_op::Div);

  auto d0_out = d0.setup_driver_pin();
  auto s0_out = s0.setup_driver_pin();
  auto r0_out = r0.setup_driver_pin();
  auto d1_out = d1.setup_driver_pin();
  auto d2_out = d2.setup_driver_pin();
  auto d3_out = d3.setup_driver_pin();
  auto d4_out = d4.setup_driver_pin();

  d0_out.set_bits(8);
  d0_out.set_unsign();
  s0_out.set_bits(8);
  s0_out.set_unsign();
  r0_out.set_bits(8);
  r0_out.set_unsign();
  d1_out.set_bits(8);
  d1_out.set_sign();
  d2_out.set_bits(8);
  d2_out.set_sign();
  d3_out.set_bits(8);
  d3_out.set_unsign();
  d4_out.set_bits(8);
  d4_out.set_unsign();

  lg->add_edge(in_a, d0.setup_sink_pin_raw(0));
  lg->add_edge(c1.get_driver_pin(), d0.setup_sink_pin_raw(1));
  lg->add_edge(d0_out, out0);

  lg->add_edge(in_a, s0.setup_sink_pin_raw(0));
  lg->add_edge(c0.get_driver_pin(), s0.setup_sink_pin_raw(1));
  lg->add_edge(s0_out, out1);

  lg->add_edge(in_a, r0.setup_sink_pin_raw(0));
  lg->add_edge(c0.get_driver_pin(), r0.setup_sink_pin_raw(1));
  lg->add_edge(r0_out, out2);

  lg->add_edge(c0.get_driver_pin(), d1.setup_sink_pin_raw(0));
  lg->add_edge(c2.get_driver_pin(), d1.setup_sink_pin_raw(1));
  lg->add_edge(d1_out, out3);

  lg->add_edge(c5.get_driver_pin(), d2.setup_sink_pin_raw(0));
  lg->add_edge(c5.get_driver_pin(), d2.setup_sink_pin_raw(1));
  lg->add_edge(d2_out, out4);

  lg->add_edge(c0.get_driver_pin(), d3.setup_sink_pin_raw(0));
  lg->add_edge(c0.get_driver_pin(), d3.setup_sink_pin_raw(1));
  lg->add_edge(d3_out, out5);
  lg->add_edge(c6.get_driver_pin(), d4.setup_sink_pin_raw(0));
  lg->add_edge(c2.get_driver_pin(), d4.setup_sink_pin_raw(1));
  lg->add_edge(d4_out, out6);

  auto gm = std::make_shared<upass::Lgraph_manager>(lg);
  auto ms = gm->fold_shift_div_const();
  EXPECT_EQ(ms.simplified_to_driver, 3U);
  EXPECT_EQ(ms.simplified_to_const_zero, 1U);
  EXPECT_EQ(ms.simplified_to_const_one, 1U);
  EXPECT_EQ(ms.simplified_to_const_other, 1U);
  EXPECT_GE(ms.rewired_edges, 5U);
  EXPECT_GE(ms.new_const_nodes, 3U);
  EXPECT_EQ(ms.deleted_nodes, 6U);

  file_utils::clean_dir(kDbPath);
  lib = Graph_library::instance(kDbPath);
  ASSERT_NE(lib, nullptr);
  lg = lib->create_lgraph("top", "upass_runner_lgraph_fold_shift_div");
  ASSERT_NE(lg, nullptr);

  in_a = lg->add_graph_input("a", 1, 8);
  out0 = lg->add_graph_output("o_div1", 2, 8);
  out1 = lg->add_graph_output("o_shl0", 3, 8);
  out2 = lg->add_graph_output("o_sra0", 4, 8);
  out3 = lg->add_graph_output("o_div0num", 5, 8);
  out4 = lg->add_graph_output("o_divxx", 6, 8);
  out5 = lg->add_graph_output("o_div00", 8, 8);
  out6 = lg->add_graph_output("o_div62", 9, 8);
  c0   = lg->create_node_const(0);
  c1   = lg->create_node_const(1);
  c2   = lg->create_node_const(2);
  c6   = lg->create_node_const(6);
  c5   = lg->create_node_const(5);
  d0   = lg->create_node(Ntype_op::Div);
  s0   = lg->create_node(Ntype_op::SHL);
  r0   = lg->create_node(Ntype_op::SRA);
  d1   = lg->create_node(Ntype_op::Div);
  d2   = lg->create_node(Ntype_op::Div);
  d3   = lg->create_node(Ntype_op::Div);
  d4   = lg->create_node(Ntype_op::Div);

  d0_out = d0.setup_driver_pin();
  s0_out = s0.setup_driver_pin();
  r0_out = r0.setup_driver_pin();
  d1_out = d1.setup_driver_pin();
  d2_out = d2.setup_driver_pin();
  d3_out = d3.setup_driver_pin();
  d4_out = d4.setup_driver_pin();

  d0_out.set_bits(8);
  d0_out.set_unsign();
  s0_out.set_bits(8);
  s0_out.set_unsign();
  r0_out.set_bits(8);
  r0_out.set_unsign();
  d1_out.set_bits(8);
  d1_out.set_sign();
  d2_out.set_bits(8);
  d2_out.set_sign();
  d3_out.set_bits(8);
  d3_out.set_unsign();
  d4_out.set_bits(8);
  d4_out.set_unsign();

  lg->add_edge(in_a, d0.setup_sink_pin_raw(0));
  lg->add_edge(c1.get_driver_pin(), d0.setup_sink_pin_raw(1));
  lg->add_edge(d0_out, out0);
  lg->add_edge(in_a, s0.setup_sink_pin_raw(0));
  lg->add_edge(c0.get_driver_pin(), s0.setup_sink_pin_raw(1));
  lg->add_edge(s0_out, out1);
  lg->add_edge(in_a, r0.setup_sink_pin_raw(0));
  lg->add_edge(c0.get_driver_pin(), r0.setup_sink_pin_raw(1));
  lg->add_edge(r0_out, out2);
  lg->add_edge(c0.get_driver_pin(), d1.setup_sink_pin_raw(0));
  lg->add_edge(c2.get_driver_pin(), d1.setup_sink_pin_raw(1));
  lg->add_edge(d1_out, out3);
  lg->add_edge(c5.get_driver_pin(), d2.setup_sink_pin_raw(0));
  lg->add_edge(c5.get_driver_pin(), d2.setup_sink_pin_raw(1));
  lg->add_edge(d2_out, out4);
  lg->add_edge(c0.get_driver_pin(), d3.setup_sink_pin_raw(0));
  lg->add_edge(c0.get_driver_pin(), d3.setup_sink_pin_raw(1));
  lg->add_edge(d3_out, out5);
  lg->add_edge(c6.get_driver_pin(), d4.setup_sink_pin_raw(0));
  lg->add_edge(c2.get_driver_pin(), d4.setup_sink_pin_raw(1));
  lg->add_edge(d4_out, out6);

  gm = std::make_shared<upass::Lgraph_manager>(lg);
  uPass_runner_lgraph runner(gm, {"fold_shift_div"});
  runner.run(3);

  auto out3_dpin = lg->get_graph_output("o_div0num").get_driver_pin();
  auto out4_dpin = lg->get_graph_output("o_divxx").get_driver_pin();
  auto out6_dpin = lg->get_graph_output("o_div62").get_driver_pin();
  EXPECT_EQ(out3_dpin.get_type_op(), Ntype_op::Const);
  EXPECT_EQ(out3_dpin.get_bits(), 8);
  EXPECT_FALSE(out3_dpin.is_unsign());
  EXPECT_EQ(out4_dpin.get_type_op(), Ntype_op::Const);
  EXPECT_EQ(out4_dpin.get_bits(), 8);
  EXPECT_FALSE(out4_dpin.is_unsign());
  EXPECT_EQ(out6_dpin.get_type_op(), Ntype_op::Const);
  EXPECT_EQ(out6_dpin.get_bits(), 8);
  EXPECT_TRUE(out6_dpin.is_unsign());

  std::size_t div_count = 0;
  std::size_t shl_count = 0;
  std::size_t sra_count = 0;
  for (const auto &n : lg->fast()) {
    if (n.get_type_op() == Ntype_op::Div) {
      ++div_count;
    } else if (n.get_type_op() == Ntype_op::SHL) {
      ++shl_count;
    } else if (n.get_type_op() == Ntype_op::SRA) {
      ++sra_count;
    }
  }
  EXPECT_EQ(div_count, 1U);  // 0/0 is intentionally not rewritten
  EXPECT_EQ(shl_count, 0U);
  EXPECT_EQ(sra_count, 0U);
}

TEST(UpassRunnerLgraph, FoldShiftDivDryRunNoMutation) {
  constexpr std::string_view kDbPath = "lgdb_upass_runner_lgraph_fold_shift_div_dry";
  file_utils::clean_dir(kDbPath);
  auto *lib = Graph_library::instance(kDbPath);
  ASSERT_NE(lib, nullptr);
  auto *lg = lib->create_lgraph("top", "upass_runner_lgraph_fold_shift_div_dry");
  ASSERT_NE(lg, nullptr);

  auto in_a = lg->add_graph_input("a", 1, 8);
  auto out0 = lg->add_graph_output("o_div1", 2, 8);
  auto c1   = lg->create_node_const(1);
  auto d0   = lg->create_node(Ntype_op::Div);

  lg->add_edge(in_a, d0.setup_sink_pin_raw(0));
  lg->add_edge(c1.get_driver_pin(), d0.setup_sink_pin_raw(1));
  lg->add_edge(d0.setup_driver_pin(), out0);

  auto gm = std::make_shared<upass::Lgraph_manager>(lg);
  uPass_runner_lgraph runner(gm, {"fold_shift_div"}, true);
  runner.run(3);

  std::size_t div_count = 0;
  for (const auto &n : lg->fast()) {
    if (n.get_type_op() == Ntype_op::Div) {
      ++div_count;
    }
  }
  EXPECT_EQ(div_count, 1U);
}

TEST(UpassRunnerLgraph, GuardSkipsFoldSumConstWhenNoCandidates) {
  constexpr std::string_view kDbPath = "lgdb_upass_runner_lgraph_guard_skip";
  file_utils::clean_dir(kDbPath);
  auto *lib = Graph_library::instance(kDbPath);
  ASSERT_NE(lib, nullptr);
  auto *lg = lib->create_lgraph("top", "upass_runner_lgraph_guard_skip");
  ASSERT_NE(lg, nullptr);

  auto in_a = lg->add_graph_input("a", 1, 8);
  auto in_b = lg->add_graph_input("b", 2, 8);
  auto out0 = lg->add_graph_output("o", 3, 8);
  auto s0   = lg->create_node(Ntype_op::Sum);
  lg->add_edge(in_a, s0.setup_sink_pin_raw(0));
  lg->add_edge(in_b, s0.setup_sink_pin_raw(1));
  lg->add_edge(s0.setup_driver_pin(), out0);

  auto gm = std::make_shared<upass::Lgraph_manager>(lg);
  uPass_runner_lgraph runner(gm, {"fold_sum_const"});

  testing::internal::CaptureStdout();
  runner.run(1);
  auto out = testing::internal::GetCapturedStdout();
  EXPECT_NE(out.find("uPass(lgraph) - skip fold_sum_const (no fold candidates)"), std::string::npos);

  std::size_t sum_count = 0;
  for (const auto &n : lg->fast()) {
    if (n.get_type_op() == Ntype_op::Sum) {
      ++sum_count;
    }
  }
  EXPECT_EQ(sum_count, 1U);
}

// ─────────────────────────────────────────────────────────────────────────────
// fold_sub_const tests
// Pattern legend (LGraph Sum port semantics):
//   setup_sink_pin_raw(0) → port A  (positive / addend)
//   setup_sink_pin_raw(1) → port B  (negative / subtrahend)
// ─────────────────────────────────────────────────────────────────────────────

// a - 0 → a   (neutral element: subtracting zero leaves the addend unchanged)
TEST(UpassRunnerLgraph, FoldSubConstSubZero) {
  constexpr std::string_view kDbPath = "lgdb_upass_lgraph_fold_sub_zero";
  file_utils::clean_dir(kDbPath);
  auto *lib = Graph_library::instance(kDbPath);
  ASSERT_NE(lib, nullptr);
  auto *lg = lib->create_lgraph("top", "upass_lgraph_fold_sub_zero");
  ASSERT_NE(lg, nullptr);

  auto in_a = lg->add_graph_input("a", 1, 8);
  auto out0  = lg->add_graph_output("o_sub_zero", 2, 8);
  auto c0    = lg->create_node_const(0);
  auto s0    = lg->create_node(Ntype_op::Sum);
  // a - 0:  A=in_a, B=const(0)
  lg->add_edge(in_a, s0.setup_sink_pin_raw(0));
  lg->add_edge(c0.get_driver_pin(), s0.setup_sink_pin_raw(1));
  lg->add_edge(s0.setup_driver_pin(), out0);

  auto gm = std::make_shared<upass::Lgraph_manager>(lg);

  // Dry-run: Sum node must survive untouched.
  const auto dry = gm->fold_sub_const(false, true);
  EXPECT_EQ(dry.sub_zero_simplified, 1U);
  EXPECT_EQ(dry.deleted_nodes, 0U);
  std::size_t sum_count = 0;
  for (const auto &n : lg->fast()) {
    if (n.get_type_op() == Ntype_op::Sum) ++sum_count;
  }
  EXPECT_EQ(sum_count, 1U);

  // Live run: Sum node must be eliminated; output must be wired directly to in_a.
  uPass_runner_lgraph runner(gm, {"fold_sub_const"});
  runner.run(2);

  sum_count = 0;
  for (const auto &n : lg->fast()) {
    if (n.get_type_op() == Ntype_op::Sum) ++sum_count;
  }
  EXPECT_EQ(sum_count, 0U);
}

// a - a → 0   (self-cancellation)
TEST(UpassRunnerLgraph, FoldSubConstSubSelf) {
  constexpr std::string_view kDbPath = "lgdb_upass_lgraph_fold_sub_self";
  file_utils::clean_dir(kDbPath);
  auto *lib = Graph_library::instance(kDbPath);
  ASSERT_NE(lib, nullptr);
  auto *lg = lib->create_lgraph("top", "upass_lgraph_fold_sub_self");
  ASSERT_NE(lg, nullptr);

  auto in_a = lg->add_graph_input("a", 1, 8);
  auto out0  = lg->add_graph_output("o_sub_self", 2, 8);
  auto s0    = lg->create_node(Ntype_op::Sum);
  // a - a:  A=in_a, B=in_a  (same driver on both ports)
  lg->add_edge(in_a, s0.setup_sink_pin_raw(0));
  lg->add_edge(in_a, s0.setup_sink_pin_raw(1));
  lg->add_edge(s0.setup_driver_pin(), out0);

  auto gm = std::make_shared<upass::Lgraph_manager>(lg);

  // Dry-run: Sum node must survive; one self-cancellation reported.
  const auto dry = gm->fold_sub_const(false, true);
  EXPECT_EQ(dry.sub_self_simplified, 1U);
  EXPECT_EQ(dry.deleted_nodes, 0U);
  std::size_t sum_count = 0;
  for (const auto &n : lg->fast()) {
    if (n.get_type_op() == Ntype_op::Sum) ++sum_count;
  }
  EXPECT_EQ(sum_count, 1U);

  // Live run: Sum is replaced by const(0).
  uPass_runner_lgraph runner(gm, {"fold_sub_const"});
  runner.run(2);

  sum_count = 0;
  for (const auto &n : lg->fast()) {
    if (n.get_type_op() == Ntype_op::Sum) ++sum_count;
  }
  EXPECT_EQ(sum_count, 0U);

  // Output driver must now be a Const node with value 0.
  auto out_dpin = lg->get_graph_output("o_sub_self").get_driver_pin();
  EXPECT_EQ(out_dpin.get_type_op(), Ntype_op::Const);
  EXPECT_EQ(out_dpin.get_type_const().to_i(), 0);
}

// c1 - c2 → (c1 - c2)   (fully-constant subtraction)
TEST(UpassRunnerLgraph, FoldSubConstConstSub) {
  constexpr std::string_view kDbPath = "lgdb_upass_lgraph_fold_const_sub";
  file_utils::clean_dir(kDbPath);
  auto *lib = Graph_library::instance(kDbPath);
  ASSERT_NE(lib, nullptr);
  auto *lg = lib->create_lgraph("top", "upass_lgraph_fold_const_sub");
  ASSERT_NE(lg, nullptr);

  auto out0 = lg->add_graph_output("o_const_sub", 2, 8);
  auto c7   = lg->create_node_const(7);
  auto c2   = lg->create_node_const(2);
  auto s0   = lg->create_node(Ntype_op::Sum);
  // 7 - 2:  A=const(7), B=const(2)  → expected result = 5
  lg->add_edge(c7.get_driver_pin(), s0.setup_sink_pin_raw(0));
  lg->add_edge(c2.get_driver_pin(), s0.setup_sink_pin_raw(1));
  auto s0_out = s0.setup_driver_pin();
  s0_out.set_bits(8);
  lg->add_edge(s0_out, out0);

  auto gm = std::make_shared<upass::Lgraph_manager>(lg);

  // Dry-run: Sum survives; one const-sub reported.
  const auto dry = gm->fold_sub_const(false, true);
  EXPECT_EQ(dry.const_sub_folded, 1U);
  EXPECT_EQ(dry.deleted_nodes, 0U);
  std::size_t sum_count = 0;
  for (const auto &n : lg->fast()) {
    if (n.get_type_op() == Ntype_op::Sum) ++sum_count;
  }
  EXPECT_EQ(sum_count, 1U);

  // Live run: Sum replaced by const(5).
  uPass_runner_lgraph runner(gm, {"fold_sub_const"});
  runner.run(3);

  sum_count = 0;
  for (const auto &n : lg->fast()) {
    if (n.get_type_op() == Ntype_op::Sum) ++sum_count;
  }
  EXPECT_EQ(sum_count, 0U);

  auto out_dpin = lg->get_graph_output("o_const_sub").get_driver_pin();
  EXPECT_EQ(out_dpin.get_type_op(), Ntype_op::Const);
  EXPECT_EQ(out_dpin.get_type_const().to_i(), 5);
}

// Guard: fold_sub_const is skipped when there are no fold candidates (non-const inputs)
TEST(UpassRunnerLgraph, FoldSubConstGuardSkipsWhenNoCandidates) {
  constexpr std::string_view kDbPath = "lgdb_upass_lgraph_fold_sub_guard";
  file_utils::clean_dir(kDbPath);
  auto *lib = Graph_library::instance(kDbPath);
  ASSERT_NE(lib, nullptr);
  auto *lg = lib->create_lgraph("top", "upass_lgraph_fold_sub_guard");
  ASSERT_NE(lg, nullptr);

  // Two signal inputs — no constants, so nothing is foldable.
  auto in_a = lg->add_graph_input("a", 1, 8);
  auto in_b = lg->add_graph_input("b", 2, 8);
  auto out0  = lg->add_graph_output("o", 3, 8);
  auto s0    = lg->create_node(Ntype_op::Sum);
  lg->add_edge(in_a, s0.setup_sink_pin_raw(0));
  lg->add_edge(in_b, s0.setup_sink_pin_raw(1));
  lg->add_edge(s0.setup_driver_pin(), out0);

  auto gm = std::make_shared<upass::Lgraph_manager>(lg);
  uPass_runner_lgraph runner(gm, {"fold_sub_const"});

  testing::internal::CaptureStdout();
  runner.run(1);
  auto out_str = testing::internal::GetCapturedStdout();
  EXPECT_NE(out_str.find("uPass(lgraph) - skip fold_sub_const (no fold candidates)"), std::string::npos);

  std::size_t sum_count = 0;
  for (const auto &n : lg->fast()) {
    if (n.get_type_op() == Ntype_op::Sum) ++sum_count;
  }
  EXPECT_EQ(sum_count, 1U);  // Sum survives — nothing folded.
}

}  // namespace

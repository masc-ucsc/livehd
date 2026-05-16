//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <memory>
#include <string_view>

#include "graph_library_singleton.hpp"
#include "gtest/gtest.h"
#include "hhds/graph.hpp"
#include "lgraph_manager.hpp"
#include "lnast.hpp"
#include "lnast_manager.hpp"
#include "node_util.hpp"
#include "upass_shared.hpp"

namespace {

std::shared_ptr<hhds::Graph> make_graph(std::string_view path, std::string_view name) {
  auto& lib = livehd::Hhds_graph_library::instance(path);
  auto  gio = lib.create_io(name);
  return gio->create_graph();
}

TEST(UpassSharedFoldParity, FoldSumConstAcrossLnastAndGraph) {
  auto ln       = std::make_shared<Lnast>("top");
  auto root_nid = ln->set_root(Lnast_ntype::create_top());
  auto st       = ln->add_child(root_nid, Lnast_ntype::create_stmts());
  auto pl       = ln->add_child(st, Lnast_ntype::create_plus());
  ln->add_child(pl, Lnast_node::create_const(2));
  ln->add_child(pl, Lnast_node::create_const(3));

  auto lm        = std::make_shared<upass::Lnast_manager>(ln);
  auto lnast_rep = upass::run_fold_sum_const_shared(*lm, "test-lnast", false);
  EXPECT_EQ(lnast_rep.folded_nodes, 1U);
  EXPECT_EQ(lnast_rep.rewired_edges, 0U);
  EXPECT_EQ(lnast_rep.new_const_nodes, 0U);
  EXPECT_EQ(lnast_rep.deleted_nodes, 0U);
  EXPECT_TRUE(Lnast_ntype::is_const(ln->get_type(pl)));
  EXPECT_EQ(ln->get_name(pl), "5");

  auto g  = make_graph("lgdb_upass_shared_fold_parity", "upass_shared_fold_parity");
  auto c0 = livehd::graph_util::create_const(*g, *Dlop::create_integer(2));
  auto c1 = livehd::graph_util::create_const(*g, *Dlop::create_integer(3));
  auto s0 = livehd::graph_util::create_typed_node(*g, Ntype_op::Sum);
  c0.connect_sink(s0.create_sink_pin(0));
  c1.connect_sink(s0.create_sink_pin(1));

  auto gm         = std::make_shared<upass::Lgraph_manager>(g);
  auto graph_rep  = upass::run_fold_sum_const_shared(*gm, "test-graph", false);
  EXPECT_EQ(graph_rep.folded_nodes, 1U);
  EXPECT_EQ(graph_rep.rewired_edges, 0U);
  EXPECT_EQ(graph_rep.new_const_nodes, 1U);
  EXPECT_EQ(graph_rep.deleted_nodes, 1U);

  std::size_t sum_count = 0;
  for (const auto& n : g->fast_class()) {
    if (livehd::graph_util::type_op_of(n) == Ntype_op::Sum) {
      ++sum_count;
    }
  }
  EXPECT_EQ(sum_count, 0U);
}

TEST(UpassSharedFoldParity, FoldSumConstDryRunNoMutation) {
  auto ln       = std::make_shared<Lnast>("top");
  auto root_nid = ln->set_root(Lnast_ntype::create_top());
  auto st       = ln->add_child(root_nid, Lnast_ntype::create_stmts());
  auto pl       = ln->add_child(st, Lnast_ntype::create_plus());
  ln->add_child(pl, Lnast_node::create_const(2));
  ln->add_child(pl, Lnast_node::create_const(3));

  auto lm        = std::make_shared<upass::Lnast_manager>(ln);
  auto lnast_rep = upass::run_fold_sum_const_shared(*lm, "test-lnast", true);
  EXPECT_EQ(lnast_rep.folded_nodes, 1U);
  EXPECT_EQ(lnast_rep.rewired_edges, 0U);
  EXPECT_EQ(lnast_rep.new_const_nodes, 0U);
  EXPECT_EQ(lnast_rep.deleted_nodes, 0U);
  EXPECT_TRUE(Lnast_ntype::is_plus(ln->get_type(pl)));

  auto g  = make_graph("lgdb_upass_shared_fold_parity_dry_run", "upass_shared_fold_parity_dry_run");
  auto c0 = livehd::graph_util::create_const(*g, *Dlop::create_integer(2));
  auto c1 = livehd::graph_util::create_const(*g, *Dlop::create_integer(3));
  auto s0 = livehd::graph_util::create_typed_node(*g, Ntype_op::Sum);
  c0.connect_sink(s0.create_sink_pin(0));
  c1.connect_sink(s0.create_sink_pin(1));

  auto gm        = std::make_shared<upass::Lgraph_manager>(g);
  auto graph_rep = upass::run_fold_sum_const_shared(*gm, "test-graph", true);
  EXPECT_EQ(graph_rep.folded_nodes, 1U);
  EXPECT_EQ(graph_rep.rewired_edges, 0U);
  EXPECT_EQ(graph_rep.new_const_nodes, 1U);
  EXPECT_EQ(graph_rep.deleted_nodes, 1U);

  std::size_t sum_count = 0;
  for (const auto& n : g->fast_class()) {
    if (livehd::graph_util::type_op_of(n) == Ntype_op::Sum) {
      ++sum_count;
    }
  }
  EXPECT_EQ(sum_count, 1U);
}

}  // namespace

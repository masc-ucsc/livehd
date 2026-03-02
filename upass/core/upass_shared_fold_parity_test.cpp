//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <memory>
#include <string_view>

#include "file_utils.hpp"
#include "gtest/gtest.h"
#include "lgraph.hpp"
#include "lnast.hpp"
#include "lgraph_manager.hpp"
#include "lnast_manager.hpp"
#include "upass_shared.hpp"

namespace {

TEST(UpassSharedFoldParity, FoldSumConstAcrossLnastAndLgraph) {
  auto ln = std::make_shared<Lnast>("top");
  ln->set_root(Lnast_node::create_top("top"));
  auto st = ln->add_child(Lnast_nid::root(), Lnast_node::create_stmts("stmts"));
  auto pl = ln->add_child(st, Lnast_node::create_plus("plus"));
  ln->add_child(pl, Lnast_node::create_const(2));
  ln->add_child(pl, Lnast_node::create_const(3));

  auto lm         = std::make_shared<upass::Lnast_manager>(ln);
  auto lnast_rep  = upass::run_fold_sum_const_shared(*lm, "test-lnast", false);
  EXPECT_EQ(lnast_rep.folded_nodes, 1U);
  EXPECT_EQ(lnast_rep.rewired_edges, 0U);
  EXPECT_EQ(lnast_rep.new_const_nodes, 0U);
  EXPECT_EQ(lnast_rep.deleted_nodes, 0U);
  EXPECT_TRUE(ln->get_type(pl).is_const());
  EXPECT_EQ(ln->get_data(pl).token.get_text(), "5");

  constexpr std::string_view kDbPath = "lgdb_upass_shared_fold_parity";
  file_utils::clean_dir(kDbPath);
  auto *                     lib     = Graph_library::instance(kDbPath);
  ASSERT_NE(lib, nullptr);
  auto *lg = lib->create_lgraph("top", "upass_shared_fold_parity");
  ASSERT_NE(lg, nullptr);

  auto c0 = lg->create_node_const(2);
  auto c1 = lg->create_node_const(3);
  auto s0 = lg->create_node(Ntype_op::Sum);
  lg->add_edge(c0.get_driver_pin(), s0.setup_sink_pin_raw(0));
  lg->add_edge(c1.get_driver_pin(), s0.setup_sink_pin_raw(1));

  auto gm         = std::make_shared<upass::Lgraph_manager>(lg);
  auto lgraph_rep = upass::run_fold_sum_const_shared(*gm, "test-lgraph", false);
  EXPECT_EQ(lgraph_rep.folded_nodes, 1U);
  EXPECT_EQ(lgraph_rep.rewired_edges, 0U);
  EXPECT_EQ(lgraph_rep.new_const_nodes, 1U);
  EXPECT_EQ(lgraph_rep.deleted_nodes, 1U);

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
  EXPECT_GE(const_count, 3U);
}

TEST(UpassSharedFoldParity, FoldSumConstDryRunNoMutation) {
  auto ln = std::make_shared<Lnast>("top");
  ln->set_root(Lnast_node::create_top("top"));
  auto st = ln->add_child(Lnast_nid::root(), Lnast_node::create_stmts("stmts"));
  auto pl = ln->add_child(st, Lnast_node::create_plus("plus"));
  ln->add_child(pl, Lnast_node::create_const(2));
  ln->add_child(pl, Lnast_node::create_const(3));

  auto lm        = std::make_shared<upass::Lnast_manager>(ln);
  auto lnast_rep = upass::run_fold_sum_const_shared(*lm, "test-lnast", true);
  EXPECT_EQ(lnast_rep.folded_nodes, 1U);
  EXPECT_EQ(lnast_rep.rewired_edges, 0U);
  EXPECT_EQ(lnast_rep.new_const_nodes, 0U);
  EXPECT_EQ(lnast_rep.deleted_nodes, 0U);
  EXPECT_TRUE(ln->get_type(pl).is_plus());

  constexpr std::string_view kDbPath = "lgdb_upass_shared_fold_parity_dry_run";
  file_utils::clean_dir(kDbPath);
  auto *                     lib     = Graph_library::instance(kDbPath);
  ASSERT_NE(lib, nullptr);
  auto *lg = lib->create_lgraph("top", "upass_shared_fold_parity_dry_run");
  ASSERT_NE(lg, nullptr);

  auto c0 = lg->create_node_const(2);
  auto c1 = lg->create_node_const(3);
  auto s0 = lg->create_node(Ntype_op::Sum);
  lg->add_edge(c0.get_driver_pin(), s0.setup_sink_pin_raw(0));
  lg->add_edge(c1.get_driver_pin(), s0.setup_sink_pin_raw(1));

  auto gm         = std::make_shared<upass::Lgraph_manager>(lg);
  auto lgraph_rep = upass::run_fold_sum_const_shared(*gm, "test-lgraph", true);
  EXPECT_EQ(lgraph_rep.folded_nodes, 1U);
  EXPECT_EQ(lgraph_rep.rewired_edges, 0U);
  EXPECT_EQ(lgraph_rep.new_const_nodes, 1U);
  EXPECT_EQ(lgraph_rep.deleted_nodes, 1U);

  std::size_t sum_count = 0;
  for (const auto &n : lg->fast()) {
    if (n.get_type_op() == Ntype_op::Sum) {
      ++sum_count;
    }
  }
  EXPECT_EQ(sum_count, 1U);
}

}  // namespace

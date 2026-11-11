// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// graph/shared_text.hpp: a color group or placement block is one text per graph and one small id per node.

#include "shared_text.hpp"

#include <string>
#include <vector>

#include "attrs.hpp"
#include "cell.hpp"
#include "graph_library_singleton.hpp"
#include "gtest/gtest.h"
#include "hhds/graph.hpp"
#include "node_util.hpp"

namespace gu = livehd::graph_util;
namespace la = livehd::attrs;

namespace {
struct Two_graphs {
  std::shared_ptr<hhds::Graph> a, b;
  explicit Two_graphs(const std::string& tag) {
    auto& lib = livehd::Hhds_graph_library::instance("lgdb_shared_text_" + tag);
    a         = lib.create_io("a_" + tag)->create_graph();
    b         = lib.create_io("b_" + tag)->create_graph();
  }
};
}  // namespace

TEST(SharedText, OneTextPerGraphOneIdPerNode) {
  Two_graphs g("basic");
  auto       n1 = gu::create_typed_node(*g.a, Ntype_op::Or);
  auto       n2 = gu::create_typed_node(*g.a, Ntype_op::Or);
  auto       n3 = gu::create_typed_node(*g.a, Ntype_op::Or);
  EXPECT_FALSE(gu::Color_group::has(n1));
  EXPECT_TRUE(gu::Color_group::text(n1).empty());

  gu::Color_group::set(n1, "top.u1:color:3");
  gu::Color_group::set(n2, "top.u1:color:3");
  gu::Color_group::set(n3, "top.u2:color:1");
  EXPECT_EQ(gu::Color_group::text(n1), "top.u1:color:3");
  EXPECT_EQ(gu::Color_group::text(n3), "top.u2:color:1");
  // Same graph: the same text is the same id, and the table holds it once.
  EXPECT_EQ(gu::Color_group::id_of(n1), gu::Color_group::id_of(n2));
  EXPECT_NE(gu::Color_group::id_of(n1), gu::Color_group::id_of(n3));
  ASSERT_NE(gu::Color_group::table_of(g.a.get()), nullptr);
  EXPECT_EQ(gu::Color_group::table_of(g.a.get())->size(), 2U);
  // The two kinds do not share a table.
  EXPECT_FALSE(gu::Placement_block::has(n1));
  EXPECT_EQ(gu::Placement_block::table_of(g.a.get()), nullptr);

  // An empty text is a value, not "absent".
  gu::Placement_block::set(n1, "");
  EXPECT_TRUE(gu::Placement_block::has(n1));
  EXPECT_TRUE(gu::Placement_block::text(n1).empty());
  gu::Placement_block::clear(n1);
  EXPECT_FALSE(gu::Placement_block::has(n1));
}

TEST(SharedText, CopyKeepsTheTextAcrossGraphs) {
  Two_graphs g("copy");
  auto       from  = gu::create_typed_node(*g.a, Ntype_op::Or);
  auto       same  = gu::create_typed_node(*g.a, Ntype_op::Or);
  // Give the other graph a different table first, so equal ids would mean different texts.
  auto       other = gu::create_typed_node(*g.b, Ntype_op::Or);
  auto       to    = gu::create_typed_node(*g.b, Ntype_op::Or);
  gu::Placement_block::set(other, "unrelated");
  gu::Placement_block::set(from, "ram");

  gu::Placement_block::copy(from, same);
  EXPECT_EQ(gu::Placement_block::id_of(same), gu::Placement_block::id_of(from));
  gu::Placement_block::copy(from, to);
  EXPECT_EQ(gu::Placement_block::text(to), "ram");
  EXPECT_EQ(gu::Placement_block::text(other), "unrelated");
  EXPECT_NE(gu::Placement_block::id_of(to), gu::Placement_block::id_of(from));

  // A node without one copies nothing and leaves the target as it was.
  auto none = gu::create_typed_node(*g.a, Ntype_op::Or);
  gu::Placement_block::copy(none, to);
  EXPECT_EQ(gu::Placement_block::text(to), "ram");

  // The id of a node that is gone still names its text in the same graph.
  const auto id = gu::Placement_block::id_of(from);
  from.del_node();
  auto later = gu::create_typed_node(*g.a, Ntype_op::Or);
  gu::Placement_block::set_id(later, id);
  EXPECT_EQ(gu::Placement_block::text(later), "ram");
  gu::Placement_block::set_id(later, 0);  // "the owner had none": no change
  EXPECT_EQ(gu::Placement_block::text(later), "ram");
}

TEST(SharedText, WriterAndTranslator) {
  Two_graphs g("writer");
  gu::Color_group::set(gu::create_typed_node(*g.b, Ntype_op::Or), "already:there");
  std::vector<hhds::Node_class> source;
  for (int i = 0; i < 100; ++i) {
    source.push_back(gu::create_typed_node(*g.a, Ntype_op::Or));
    gu::Color_group::set(source.back(), "unit:color:" + std::to_string(i % 4));
  }
  {
    gu::Color_group::Writer     writer(g.b.get());
    gu::Color_group::Translator scoped(writer, "top.u7.");
    EXPECT_EQ(writer.id("already:there"), 1U);
    for (const auto& from : source) {
      auto to = gu::create_typed_node(*g.b, Ntype_op::Or);
      scoped.copy(from, to);
      // Under a writer the text is read through it: the stored table is behind until flush().
      EXPECT_EQ(writer.text(to), "top.u7." + std::string(gu::Color_group::text(from)));
    }
    EXPECT_EQ(gu::Color_group::table_of(g.b.get())->size(), 1U);
    writer.flush();
  }
  const auto* table = gu::Color_group::table_of(g.b.get());
  ASSERT_NE(table, nullptr);
  EXPECT_EQ(table->size(), 5U);  // the one that was there, and four scoped groups for a hundred nodes
  size_t scoped_nodes = 0;
  for (auto node : g.b->body().nodes()) {
    scoped_nodes += gu::Color_group::text(node).starts_with("top.u7.unit:color:");
  }
  EXPECT_EQ(scoped_nodes, 100U);

  // A writer refuses to store over a table that changed behind it.
  gu::Color_group::Writer stale(g.b.get());
  (void)stale.id("from the writer");
  gu::Color_group::set(gu::create_typed_node(*g.b, Ntype_op::Or), "behind its back");
  EXPECT_THROW(stale.flush(), std::logic_error);
}

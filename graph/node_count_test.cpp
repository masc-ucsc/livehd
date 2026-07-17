//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// Tests the design-size estimators in node_util.hpp used by the memory-admission
// size gate (pass.color warns, pass.abc/pass.lec refuse above the threshold).
// The point of these helpers is to count a flattened design WITHOUT materializing
// the O(flat-nodes) hierarchical walk, and to multiply a shared def's cost by its
// instance count -- both are checked here.

#include <cstdint>
#include <cstdlib>

#include "graph_library_singleton.hpp"
#include "gtest/gtest.h"
#include "hhds/graph.hpp"
#include "node_util.hpp"

using livehd::graph_util::body_node_count;
using livehd::graph_util::create_typed_node;
using livehd::graph_util::flat_node_count;
using livehd::graph_util::large_design_node_threshold;

namespace {

// Never resolves a subnode: turns flat_node_count into "top body only".
hhds::Graph* no_resolve(hhds::Gid /*gid*/) { return nullptr; }

}  // namespace

// A flat def: body_node_count == the number of nodes actually created, and
// flat_node_count with no subnodes reduces to exactly that.
TEST(NodeCount, FlatBodyCount) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_node_count_flat");
  auto  gio = lib.create_io("flat");
  gio->add_input("a", 8);
  gio->add_input("b", 8);
  gio->add_input("c", 8);
  gio->add_output("y", 8);
  auto g = gio->create_graph();

  auto s0 = create_typed_node(*g, Ntype_op::Sum);
  g->get_input_pin("a").connect_sink(s0.create_sink_pin(0));
  g->get_input_pin("b").connect_sink(s0.create_sink_pin(1));
  auto s1 = create_typed_node(*g, Ntype_op::Sum);
  s0.create_driver_pin(0).connect_sink(s1.create_sink_pin(0));
  g->get_input_pin("c").connect_sink(s1.create_sink_pin(1));
  s1.create_driver_pin(0).connect_sink(g->get_output_pin("y"));

  EXPECT_EQ(body_node_count(g.get()), 2u) << "two Sum nodes in the body";
  EXPECT_EQ(flat_node_count(g.get(), no_resolve), 2u) << "no subnodes: flat == body";
}

// A parent instantiating the SAME child twice: the flattened count is the
// parent's own nodes plus the child body counted once PER instance.
TEST(NodeCount, HierarchyMultipliesByInstanceCount) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_node_count_hier");

  auto cio = lib.create_io("child");
  cio->add_input("x", 1);
  cio->add_input("y", 1);
  cio->add_output("o", 1);
  auto cg = cio->create_graph();
  auto ca = create_typed_node(*cg, Ntype_op::And);
  cg->get_input_pin("x").connect_sink(ca.create_sink_pin(0));
  cg->get_input_pin("y").connect_sink(ca.create_sink_pin(0));
  ca.create_driver_pin(0).connect_sink(cg->get_output_pin("o"));
  const uint64_t child_nodes = body_node_count(cg.get());  // 1 (And)

  auto pio = lib.create_io("parent");
  pio->add_input("a", 1);
  pio->add_input("b", 1);
  pio->add_output("z", 1);
  auto pg = pio->create_graph();
  // One real parent node plus two instances of the child.
  auto inv = create_typed_node(*pg, Ntype_op::Not);
  pg->get_input_pin("a").connect_sink(inv.create_sink_pin(0));
  auto sub0 = create_typed_node(*pg, Ntype_op::Sub);
  sub0.set_subnode(cio);
  inv.create_driver_pin(0).connect_sink(sub0.create_sink_pin(0));
  pg->get_input_pin("b").connect_sink(sub0.create_sink_pin(1));
  auto sub1 = create_typed_node(*pg, Ntype_op::Sub);
  sub1.set_subnode(cio);
  pg->get_input_pin("a").connect_sink(sub1.create_sink_pin(0));
  pg->get_input_pin("b").connect_sink(sub1.create_sink_pin(1));
  sub1.create_driver_pin(0).connect_sink(pg->get_output_pin("z"));

  const uint64_t parent_body = body_node_count(pg.get());  // Not + 2 Sub instances

  auto resolve = [&lib](hhds::Gid gid) -> hhds::Graph* {
    auto g = lib.get_graph(gid);
    return g ? g.get() : nullptr;
  };
  const uint64_t flat = flat_node_count(pg.get(), resolve);

  // Flattened = the parent's own nodes + the child body once per instance.
  EXPECT_EQ(flat, parent_body + 2 * child_nodes)
      << "each of the two child instances contributes the child body";

  // Without a resolver the subnodes are opaque leaves: only the parent body.
  EXPECT_EQ(flat_node_count(pg.get(), no_resolve), parent_body);
}

TEST(NodeCount, NullTopIsZero) {
  EXPECT_EQ(body_node_count(nullptr), 0u);
  EXPECT_EQ(flat_node_count(nullptr, no_resolve), 0u);
}

// The pruned counter (used by lec) treats an opaque child as a leaf: with the
// child marked opaque, only the parent body counts; without, it descends.
TEST(NodeCount, PrunedSkipsOpaqueChild) {
  using livehd::graph_util::flat_node_count_pruned;
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_node_count_pruned");

  auto cio = lib.create_io("pchild");
  cio->add_input("x", 1);
  cio->add_output("o", 1);
  auto cg = cio->create_graph();
  auto n0 = create_typed_node(*cg, Ntype_op::Not);
  auto n1 = create_typed_node(*cg, Ntype_op::Not);  // two-node child body
  cg->get_input_pin("x").connect_sink(n0.create_sink_pin(0));
  n0.create_driver_pin(0).connect_sink(n1.create_sink_pin(0));
  n1.create_driver_pin(0).connect_sink(cg->get_output_pin("o"));

  auto pio = lib.create_io("pparent");
  pio->add_input("a", 1);
  pio->add_output("z", 1);
  auto pg  = pio->create_graph();
  auto sub = create_typed_node(*pg, Ntype_op::Sub);
  sub.set_subnode(cio);
  pg->get_input_pin("a").connect_sink(sub.create_sink_pin(0));
  sub.create_driver_pin(0).connect_sink(pg->get_output_pin("z"));
  const uint64_t parent_body = body_node_count(pg.get());  // Not is optimized? no: just the Sub here

  auto resolve = [&lib](hhds::Gid gid) -> hhds::Graph* {
    auto g = lib.get_graph(gid);
    return g ? g.get() : nullptr;
  };
  const auto child_name = std::string{cio->get_name()};

  // Opaque: child is a leaf -> parent body only.
  const uint64_t opaque = flat_node_count_pruned(
      pg.get(), resolve, [&](const hhds::Graph* g) { return std::string{g->get_name()} == child_name; });
  EXPECT_EQ(opaque, parent_body) << "an opaque child must not be descended into";

  // Not opaque: descend -> parent body + child body.
  const uint64_t descended
      = flat_node_count_pruned(pg.get(), resolve, [](const hhds::Graph* /*g*/) { return false; });
  EXPECT_EQ(descended, parent_body + body_node_count(cg.get()));
  EXPECT_GT(descended, opaque);
}

// The env override drives the gate: an explicit value is honored, "0" disables
// the gate (returns the max so no count can exceed it), and an unset/garbage
// value falls back to the ~1M default.
TEST(NodeCount, ThresholdEnvOverride) {
  ::setenv("LIVEHD_LARGE_DESIGN_NODES", "5", 1);
  EXPECT_EQ(large_design_node_threshold(), 5u);

  ::setenv("LIVEHD_LARGE_DESIGN_NODES", "0", 1);
  EXPECT_EQ(large_design_node_threshold(), UINT64_MAX) << "0 disables the gate";

  ::setenv("LIVEHD_LARGE_DESIGN_NODES", "not-a-number", 1);
  EXPECT_EQ(large_design_node_threshold(), livehd::graph_util::large_design_nodes_default)
      << "garbage falls back to the default";

  ::unsetenv("LIVEHD_LARGE_DESIGN_NODES");
  EXPECT_EQ(large_design_node_threshold(), livehd::graph_util::large_design_nodes_default);
}

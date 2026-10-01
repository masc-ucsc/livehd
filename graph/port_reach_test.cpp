// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// port_reach summaries on hand-built graphs: the per-output input cones, the
// record a definition carries for its callers (attrs::comb_reach, written by
// lnast.tolg), and the callee hook a client uses to splice a stored or a
// conservative summary instead of walking a callee body.

#include "port_reach.hpp"

#include <memory>

#include "cell.hpp"
#include "graph_library_singleton.hpp"
#include "gtest/gtest.h"
#include "hhds/graph.hpp"
#include "node_util.hpp"

namespace gu = livehd::graph_util;
namespace pr = livehd::port_reach;

namespace {

using Pids = absl::flat_hash_set<uint32_t>;

// child(a:0, b:1) -> (x:2 = a & b, y:3 = flop(b)): `x` is combinational in both
// inputs, `y` in none (the flop cuts it).
std::shared_ptr<hhds::GraphIO> build_child(hhds::GraphLibrary& lib) {
  auto io = lib.create_io("child");
  io->add_input("a", 0);
  io->add_input("b", 1);
  io->add_output("x", 2);
  io->add_output("y", 3);
  auto g = io->create_graph();

  auto andn = gu::create_typed_node(*g, Ntype_op::And);
  g->get_input_pin("a").connect_sink(andn.create_sink_pin(0));
  g->get_input_pin("b").connect_sink(andn.create_sink_pin(1));
  andn.create_driver_pin(0).connect_sink(g->get_output_pin("x"));

  auto flop = gu::create_typed_node(*g, Ntype_op::Flop);
  g->get_input_pin("b").connect_sink(gu::setup_sink_by_name(flop, "din"));
  flop.create_driver_pin(0).connect_sink(g->get_output_pin("y"));
  return io;
}

// parent(p:0, q:1) -> (o:2 = child(a=p, b=q).y)
std::shared_ptr<hhds::Graph> build_parent(hhds::GraphLibrary& lib, const std::shared_ptr<hhds::GraphIO>& child_io) {
  auto io = lib.create_io("parent");
  io->add_input("p", 0);
  io->add_input("q", 1);
  io->add_output("o", 2);
  auto g = io->create_graph();

  auto sub = gu::create_typed_node(*g, Ntype_op::Sub);
  sub.set_subnode(child_io);
  g->get_input_pin("p").connect_sink(sub.create_sink_pin(0));
  g->get_input_pin("q").connect_sink(sub.create_sink_pin(1));
  sub.create_driver_pin(3).connect_sink(g->get_output_pin("o"));
  return g;
}

}  // namespace

TEST(PortReach, FlopCutsTheCone) {
  auto& lib   = livehd::Hhds_graph_library::instance("lgdb_port_reach_cut");
  auto  child = build_child(lib)->get_graph();

  pr::Cache   cache;
  const auto& r = cache.of(child);
  EXPECT_EQ(r.out2ins.at(2), (Pids{0, 1}));
  EXPECT_TRUE(r.input_independent(3)) << "a flop read depends on no input";
}

TEST(PortReach, StampRoundTrips) {
  auto& lib   = livehd::Hhds_graph_library::instance("lgdb_port_reach_stamp");
  auto  child = build_child(lib)->get_graph();

  EXPECT_FALSE(pr::stamped(*child).has_value()) << "an unstamped body reads as absent, not as independent";

  pr::Cache cache;
  pr::stamp(*child, cache.of(child));
  const auto back = pr::stamped(*child);
  ASSERT_TRUE(back.has_value());
  EXPECT_EQ(back->out2ins.at(2), (Pids{0, 1}));
  EXPECT_TRUE(back->input_independent(3));

  const auto all = pr::crossbar(*child);
  EXPECT_EQ(all.out2ins.at(2), (Pids{0, 1}));
  EXPECT_EQ(all.out2ins.at(3), (Pids{0, 1}));
}

TEST(PortReach, CalleeHookReplacesTheCalleeWalk) {
  auto& lib      = livehd::Hhds_graph_library::instance("lgdb_port_reach_hook");
  auto  child_io = build_child(lib);
  auto  parent   = build_parent(lib, child_io);

  pr::Cache walked;
  EXPECT_TRUE(walked.of(parent).input_independent(2)) << "the walk splices the child's cut `y`";

  // A client that cannot trust the child body substitutes a crossbar: `y`
  // then depends on both instance inputs, and so does the parent's `o`.
  pr::Cache hooked([](const std::shared_ptr<hhds::Graph>& g) { return std::optional<pr::Def_reach>(pr::crossbar(*g)); });
  EXPECT_EQ(hooked.of(parent).out2ins.at(2), (Pids{0, 1}));
  EXPECT_EQ(hooked.callee_of(child_io->get_graph()).out2ins.at(3), (Pids{0, 1}));
  EXPECT_TRUE(hooked.of(child_io->get_graph()).input_independent(3)) << "of() walks the graph it is handed";
}

// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "gtest/gtest.h"
#include "node_util.hpp"
#include "query.hpp"
#include "satopt_stages.hpp"

namespace {
namespace gu = livehd::graph_util;
using namespace livehd::satopt;
using Pin = hhds::Pin_class;
struct Fixture {
  hhds::GraphLibrary           lib;
  std::shared_ptr<hhds::Graph> graph;
  Pin                          x, a, b, first, second;
  Fixture(bool overlap = false, bool extra_use = false, Ntype_op op = Ntype_op::Mult) {
    auto io = lib.create_io("arithmetic_share");
    for (const auto& [name, pid] : {
             std::pair{"x", 1},
             {"a", 2},
             {"b", 3}
    }) {
      io->add_input(name, pid);
      io->set_bits(name, 4);
      io->set_unsign(name, true);
    }
    for (const auto& [name, pid] : {
             std::pair{    "out", 4},
             {"observe", 5}
    }) {
      io->add_output(name, pid);
      io->set_bits(name, 8);
      io->set_unsign(name, true);
    }
    graph = io->create_graph();
    x     = graph->get_input_pin("x");
    a     = graph->get_input_pin("a");
    b     = graph->get_input_pin("b");
    for (auto pin : {x, a, b}) {
      gu::set_ubits(pin, 4);
    }
    auto expression = [&](Pin data, int k) {
      auto n = gu::create_typed_node(*graph, op);
      gu::setup_sink_pid(n, 0).connect_driver(data);
      gu::setup_sink_pid(n, op == Ntype_op::Mult ? 0 : 1).connect_driver(c(k));
      gu::set_ubits(n.create_driver_pin(0), 8);
      return n.create_driver_pin(0);
    };
    first      = expression(a, 3);
    second     = expression(b, 5);
    auto left  = mux(cmp(Ntype_op::LT, 8), c(11), first);
    auto right = mux(cmp(overlap ? Ntype_op::LT : Ntype_op::GT, overlap ? 10 : 7), c(13), second);
    left.connect_sink(graph->get_output_pin("out"));
    right.connect_sink(graph->get_output_pin("observe"));
    if (extra_use) {
      auto observer = gu::create_typed_node(*graph, Ntype_op::Sum);
      gu::setup_sink_pid(observer, 0).connect_driver(first);
      gu::setup_sink_pid(observer, 0).connect_driver(x);
    }
  }
  Pin c(int n) { return gu::create_const(*graph, *Dlop::create_integer(n)); }
  Pin cmp(Ntype_op op, int n) {
    auto node = gu::create_typed_node(*graph, op);
    gu::setup_sink_pid(node, 0).connect_driver(x);
    gu::setup_sink_pid(node, 1).connect_driver(c(n));
    gu::set_ubits(node.create_driver_pin(0), 1);
    return node.create_driver_pin(0);
  }
  Pin mux(Pin s, Pin f, Pin t) {
    auto n = gu::create_typed_node(*graph, Ntype_op::Mux);
    n.create_sink_pin(0).connect_driver(s);
    n.create_sink_pin(1).connect_driver(f);
    n.create_sink_pin(2).connect_driver(t);
    gu::set_ubits(n.create_driver_pin(0), 8);
    return n.create_driver_pin(0);
  }
  Stage_report run(Budget budget = {}) {
    Options opts;
    opts.stages = {Stage::share};
    opts.budget = budget;
    return livehd::satopt::run(graph.get(), opts).at(Stage::share);
  }
};

TEST(SatoptShare, ExclusiveArithmeticSharesAcrossDifferentMuxRegions) {
  for (auto op : {Ntype_op::Mult, Ntype_op::Div, Ntype_op::Rem}) {
    Fixture            f(false, false, op);
    hhds::GraphLibrary refs;
    ASSERT_TRUE(refs.copy_from(f.lib, "arithmetic_share"));
    const auto r = f.run();
    EXPECT_EQ(r.applied, 1);
    EXPECT_EQ(r.proven, 1);
    EXPECT_EQ(r.nodes_removed, 1);
    size_t count = 0;
    for (auto n : f.graph->body().nodes()) {
      count += gu::type_op_of(n) == op;
    }
    EXPECT_EQ(count, 1);
    livehd::lec::Lec_options options;
    options.cones     = "false";
    options.timeout   = 5;
    auto       ref    = refs.find_io("arithmetic_share")->get_graph();
    const auto result = livehd::lec::prove_equal(ref.get(), f.graph.get(), options);
    EXPECT_EQ(result.verdict, livehd::lec::Verdict::Proven) << result.detail;
  }
}

TEST(SatoptShare, OverlapAdditionalUsesAndFailedProofsNeverRewrite) {
  for (int mode = 0; mode < 4; ++mode) {
    Fixture f(mode == 0, mode == 1);
    Budget  budget;
    if (mode == 2) {
      budget.queries = 0;
    }
    if (mode == 3) {
      budget.cone_max = 1;
    }
    const auto before = f.graph->body_epoch();
    const auto r      = f.run(budget);
    EXPECT_EQ(r.applied, 0);
    EXPECT_EQ(f.graph->body_epoch(), before);
    if (mode == 3) {
      EXPECT_GT(r.unknown, 0);
    }
  }
}

TEST(SatoptShare, ActivationDependencyOnReplacedOutputRejectsCycle) {
  Fixture    f;
  // first -> its private mux -> control of the second private mux. Sharing
  // first into second would create a cycle if that activation were chosen.
  const auto parent = f.graph->get_output_pin("out").get_driver_pin();
  const auto other  = f.graph->get_output_pin("observe").get_driver_pin().get_master_node();
  const auto sink   = other.get_sink_pin(0);
  sink.get_driver_pin().del_sink(sink);
  parent.connect_sink(sink);
  const auto before = f.graph->body_epoch();
  EXPECT_EQ(f.run().applied, 0);
  EXPECT_EQ(f.graph->body_epoch(), before);
}
}  // namespace

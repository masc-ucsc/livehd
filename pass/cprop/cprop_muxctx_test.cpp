// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "cprop_muxctx.hpp"

#include "cprop.hpp"
#include "cprop_value.hpp"
#include "gtest/gtest.h"
#include "query.hpp"

namespace {
namespace gu               = livehd::graph_util;
namespace ctx              = livehd::muxctx;
using Pin                  = hhds::Pin_class;
using Node                 = hhds::Node_class;
const ctx::Decode identity = [](Pin p) { return ctx::Bool_condition{p, true}; };

struct Fixture {
  hhds::GraphLibrary           lib;
  std::shared_ptr<hhds::Graph> graph;
  Pin                          x, a, b;
  Fixture() {
    auto io = lib.create_io("mux_context");
    for (const auto& [name, pid] : {
             std::pair{"x", 1},
             {"a", 2},
             {"b", 3}
    }) {
      io->add_input(name, pid);
      io->set_bits(name, 8);
      io->set_unsign(name, false);
    }
    io->add_output("out", 4);
    io->set_bits("out", 8);
    io->set_unsign("out", false);
    io->add_output("observe", 5);
    io->set_bits("observe", 8);
    io->set_unsign("observe", false);
    graph = io->create_graph();
    x     = graph->get_input_pin("x");
    a     = graph->get_input_pin("a");
    b     = graph->get_input_pin("b");
    for (auto pin : {x, a, b}) {
      gu::set_sbits(pin, 8);
    }
    c(0).connect_sink(graph->get_output_pin("observe"));
  }
  Pin c(int n) { return gu::create_const(*graph, *Dlop::create_integer(n)); }
  Pin eq(Pin p, int n) {
    auto node = gu::create_typed_node(*graph, Ntype_op::EQ);
    gu::setup_sink_pid(node, 0).connect_driver(p);
    gu::setup_sink_pid(node, 0).connect_driver(c(n));
    gu::set_ubits(node.create_driver_pin(0), 1);
    return node.create_driver_pin(0);
  }
  Pin mux(Pin s, Pin f, Pin t) {
    auto node = gu::create_typed_node(*graph, Ntype_op::Mux);
    for (const auto& [pid, pin] : {
             std::pair{0, s},
             {1, f},
             {2, t}
    }) {
      gu::setup_sink_pid(node, pid).connect_driver(pin);
    }
    gu::set_sbits(node.create_driver_pin(0), 8);
    return node.create_driver_pin(0);
  }
  Pin hot(Pin c0, Pin v0, Pin c1, Pin v1, Pin d) {
    auto     node = gu::create_typed_node(*graph, Ntype_op::Hotmux);
    unsigned pid  = 0;
    for (auto p : {c0, v0, c1, v1, d}) {
      gu::setup_sink_pid(node, pid++).connect_driver(p);
    }
    gu::set_sbits(node.create_driver_pin(0), 8);
    return node.create_driver_pin(0);
  }
  size_t prune_and_prove() {
    hhds::GraphLibrary reflib;
    EXPECT_TRUE(reflib.copy_from(lib, "mux_context"));
    auto                     ref     = reflib.find_io("mux_context")->get_graph();
    const auto               changes = ctx::prune(*graph, identity, livehd::cprop_value::is_bool01);
    livehd::lec::Lec_options options;
    options.cones     = "false";
    options.timeout   = 5;
    const auto result = livehd::lec::prove_equal(ref.get(), graph.get(), options);
    EXPECT_EQ(result.verdict, livehd::lec::Verdict::Proven) << result.detail;
    return changes;
  }
};

TEST(CpropMuxContext, ExactEqualityContradictionsAndSiblingRollback) {
  Fixture f;
  auto    e3 = f.eq(f.x, 3), e5 = f.eq(f.x, 5);
  auto    left = f.mux(e3, f.a, f.b), right = f.mux(e5, f.a, f.b);
  auto    root = f.mux(e3, left, right);
  root.connect_sink(f.graph->get_output_pin("out"));
  EXPECT_EQ(f.prune_and_prove(), 2);
  EXPECT_EQ(root.get_master_node().get_sink_pin(1).get_driver_pin(), f.a);
  EXPECT_EQ(root.get_master_node().get_sink_pin(2).get_driver_pin(), f.a);
}

TEST(CpropMuxContext, WideTruthIsNotOneAndBooleanDataIs) {
  for (bool boolean : {false, true}) {
    Fixture f;
    auto    s    = boolean ? f.eq(f.x, 3) : f.x;
    auto    root = f.mux(s, f.b, s);
    root.connect_sink(f.graph->get_output_pin("out"));
    EXPECT_EQ(f.prune_and_prove(), boolean ? 1 : 0);
    auto t = root.get_master_node().get_sink_pin(2).get_driver_pin();
    EXPECT_EQ(t.is_const(), boolean);
  }
}

TEST(CpropMuxContext, SharedNamedColoredAndCheckedInteriorsAreBoundaries) {
  for (int boundary = 0; boundary < 4; ++boundary) {
    Fixture f;
    auto    inner = f.mux(f.x, f.a, f.b);
    if (boundary == 0) {
      auto sink = f.graph->get_output_pin("observe");
      sink.get_driver_pin().del_sink(sink);
      inner.connect_sink(sink);
    } else if (boundary == 1) {
      gu::set_pin_name(inner, "addressable");
    } else if (boundary == 2) {
      gu::set_color(inner.get_master_node(), 1);
    } else {
      gu::set_runtime_check(inner.get_master_node(), gu::kFormalOnehot);
    }
    auto root = f.mux(f.x, f.a, inner);
    root.connect_sink(f.graph->get_output_pin("out"));
    EXPECT_EQ(ctx::prune(*f.graph, identity, livehd::cprop_value::is_bool01), 0);
    EXPECT_EQ(root.get_master_node().get_sink_pin(2).get_driver_pin(), inner);
  }
}

TEST(CpropMuxContext, ExclusiveHotmuxDefaultSuppliesAllFalseFacts) {
  Fixture f;
  auto    e3 = f.eq(f.x, 3), e5 = f.eq(f.x, 5);
  auto    d    = f.mux(e5, f.mux(e3, f.a, f.b), f.b);
  auto    root = f.hot(e3, f.b, e5, f.b, d);
  root.connect_sink(f.graph->get_output_pin("out"));
  EXPECT_EQ(f.prune_and_prove(), 2);
  EXPECT_EQ(root.get_master_node().get_sink_pin(4).get_driver_pin(), f.a);
}

TEST(CpropMuxContext, FactBudgetCountsDisequalitiesAndDropsOnlyKnowledge) {
  Fixture         f;
  ctx::Path_facts facts;
  for (int i = 0; i < 32; ++i) {
    facts.assume(f.eq(f.x, i), false, identity);
  }
  EXPECT_EQ(facts.size(), 16);
  EXPECT_EQ(facts.truth(f.eq(f.x, 7), identity), false);
  EXPECT_FALSE(facts.truth(f.eq(f.x, 20), identity).has_value());
  EXPECT_TRUE(facts.reachable());
  auto sibling = facts;
  sibling.assume(f.eq(f.x, 7), true, identity);
  EXPECT_FALSE(sibling.reachable());
  EXPECT_TRUE(facts.reachable());
}

TEST(CpropMuxContext, DeepPrivateRegionUsesIterativeWalk) {
  Fixture f;
  auto    root = f.a;
  for (int i = 0; i < 2048; ++i) {
    root = f.mux(f.x, f.b, root);
  }
  root.connect_sink(f.graph->get_output_pin("out"));
  EXPECT_EQ(ctx::prune(*f.graph, identity, livehd::cprop_value::is_bool01), 2047);
  EXPECT_EQ(root.get_master_node().get_sink_pin(2).get_driver_pin(), f.a);
}

TEST(CpropHotmuxCse, PairOrderDefaultPairingAndObligations) {
  for (int variant = 0; variant < 5; ++variant) {
    Fixture f;
    auto    c0     = variant == 3 ? f.a : f.eq(f.x, 3);
    auto    c1     = variant == 3 ? f.b : f.eq(f.x, 5);
    auto    first  = f.hot(c0, f.a, c1, f.b, f.c(7));
    auto    second = f.hot(c1, variant == 2 ? f.a : f.b, c0, variant == 2 ? f.b : f.a, f.c(variant == 1 ? 8 : 7));
    if (variant == 4) {
      gu::set_runtime_check(second.get_master_node(), gu::kFormalOnehot);
    }
    first.connect_sink(f.graph->get_output_pin("out"));
    auto sink = f.graph->get_output_pin("observe");
    sink.get_driver_pin().del_sink(sink);
    second.connect_sink(sink);
    Cprop{}.do_trans(f.graph);
    EXPECT_EQ(f.graph->get_output_pin("out").get_driver_pin() == sink.get_driver_pin(), variant == 0);
  }
}

TEST(CpropHotmuxCse, IdenticalValuesDoNotEraseUnprovenOverlap) {
  Fixture f;
  auto    root = f.hot(f.a, f.x, f.b, f.x, f.x);
  root.connect_sink(f.graph->get_output_pin("out"));
  Cprop{}.do_trans(f.graph);
  EXPECT_FALSE(root.get_master_node().is_invalid());
  EXPECT_EQ(gu::type_op_of(root.get_master_node()), Ntype_op::Hotmux);
  EXPECT_FALSE(gu::has_proven(root.get_master_node()));
}
}  // namespace

// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "cprop.hpp"
#include "gtest/gtest.h"
#include "node_util.hpp"
#include "prove.hpp"
#include "query.hpp"
#include "satopt_stages.hpp"

namespace {
namespace gu = livehd::graph_util;
using namespace livehd::satopt;
using Pin = hhds::Pin_class;
struct Fixture {
  hhds::GraphLibrary           lib;
  std::shared_ptr<hhds::Graph> graph;
  Pin                          x, a, b, inner, outer;
  Fixture(bool empty = false, bool wide = false) {
    auto io = lib.create_io("context_satopt");
    for (const auto& [name, pid] : {
             std::pair{"x", 1},
             {"a", 2},
             {"b", 3}
    }) {
      io->add_input(name, pid);
      io->set_bits(name, 8);
      io->set_unsign(name, true);
    }
    io->add_output("out", 4);
    io->set_bits("out", 8);
    io->set_unsign("out", true);
    graph = io->create_graph();
    x     = graph->get_input_pin("x");
    a     = graph->get_input_pin("a");
    b     = graph->get_input_pin("b");
    for (auto pin : {x, a, b}) {
      gu::set_ubits(pin, 8);
    }
    inner = mux(wide ? x : compare(Ntype_op::LT, 16), a, b);
    outer = mux(compare(empty ? Ntype_op::EQ : wide ? Ntype_op::GT : Ntype_op::LT, empty ? 123 : wide ? 8 : 8), a, inner);
    outer.connect_sink(graph->get_output_pin("out"));
  }
  Pin c(int n) { return gu::create_const(*graph, *Dlop::create_integer(n)); }
  Pin compare(Ntype_op op, int bound) {
    auto n = gu::create_typed_node(*graph, op);
    gu::setup_sink_pid(n, 0).connect_driver(x);
    gu::setup_sink_pid(n, op == Ntype_op::EQ ? 0 : 1).connect_driver(c(bound));
    gu::set_ubits(n.create_driver_pin(0), 1);
    return n.create_driver_pin(0);
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
    opts.stages = {Stage::muxtree};
    opts.budget = budget;
    return livehd::satopt::run(graph.get(), opts).at(Stage::muxtree);
  }
};

TEST(SatoptMuxTree, CorrelatedSelectAndWideNonzeroAreProvenInContext) {
  for (bool wide : {false, true}) {
    Fixture f(false, wide);
    // A2 cannot infer either arithmetic inequality implication.
    Cprop{}.do_trans(f.graph);
    ASSERT_FALSE(f.inner.get_master_node().get_sink_pin(0).get_driver_pin().is_const());
    hhds::GraphLibrary ref_lib;
    ASSERT_TRUE(ref_lib.copy_from(f.lib, "context_satopt"));
    const auto r = f.run();
    EXPECT_EQ(r.applied, 1);
    EXPECT_EQ(r.proven, 1);
    EXPECT_EQ(r.state, Stage_state::completed);
    EXPECT_TRUE(f.inner.get_master_node().get_sink_pin(0).get_driver_pin().is_const());
    livehd::lec::Lec_options options;
    options.cones     = "false";
    options.timeout   = 5;
    auto       ref    = ref_lib.find_io("context_satopt")->get_graph();
    const auto result = livehd::lec::prove_equal(ref.get(), f.graph.get(), options);
    EXPECT_EQ(result.verdict, livehd::lec::Verdict::Proven) << result.detail;
  }
}

TEST(SatoptMuxTree, NoSamplesNoEvidenceAndBudgetOrUnknownDoNotRewrite) {
  for (int mode = 0; mode < 3; ++mode) {
    Fixture f(mode == 0);
    Budget  budget;
    if (mode == 0) {
      budget.samples = 7;
    }
    if (mode == 1) {
      budget.queries = 0;
    }
    if (mode == 2) {
      budget.cone_max = 1;
    }
    const auto before = f.graph->body_epoch();
    const auto r      = f.run(budget);
    EXPECT_EQ(r.applied, 0);
    EXPECT_EQ(before, f.graph->body_epoch());
    if (mode == 0) {
      EXPECT_GT(r.sim_rejects, 0);
      EXPECT_EQ(r.queries, 0);
    }
    if (mode == 1) {
      EXPECT_EQ(r.state, Stage_state::exhausted);
    }
    if (mode == 2) {
      EXPECT_GT(r.unknown, 0);
    }
  }
}

TEST(SatoptMuxTree, NamesAndSharedValuesAreNotContextualTargets) {
  for (bool named : {false, true}) {
    Fixture f;
    if (named) {
      gu::set_pin_name(f.inner, "visible");
    } else {
      auto observer = gu::create_typed_node(*f.graph, Ntype_op::Sum);
      gu::setup_sink_pid(observer, 0).connect_driver(f.inner);
      gu::setup_sink_pid(observer, 0).connect_driver(f.x);
    }
    EXPECT_EQ(f.run().applied, 0);
  }
}

TEST(SatoptMuxTree, QueryPremisesNeverLeakToLaterQueries) {
  Fixture                f;
  livehd::formal::Prover prover(f.graph.get());
  EXPECT_EQ(prover
                .truth_when(f.x,
                            true,
                            {
                                {f.x, true}
  })
                .verdict,
            livehd::formal::Verdict::Proven);
  EXPECT_EQ(prover.is_true(f.x).verdict, livehd::formal::Verdict::Refuted);
  EXPECT_EQ(prover
                .truth_when(f.x,
                            false,
                            {
                                {f.x, false}
  })
                .verdict,
            livehd::formal::Verdict::Proven);
  EXPECT_EQ(prover
                .truth_when(f.x,
                            true,
                            {
                                {f.x, false}
  })
                .verdict,
            livehd::formal::Verdict::Refuted);
  EXPECT_EQ(prover
                .truth_when(f.x,
                            true,
                            {
                                {f.x, false},
                                {f.x,  true}
  })
                .verdict,
            livehd::formal::Verdict::Proven);
  EXPECT_EQ(prover.is_false(f.x).verdict, livehd::formal::Verdict::Refuted);
}

TEST(SatoptMuxTree, ExclusiveHotmuxControlsKeepGlobalProofAndUnprovenChecks) {
  for (bool proven : {false, true}) {
    Fixture    f;
    const auto sink = f.outer.get_master_node().get_sink_pin(2);
    f.inner.del_sink(sink);
    f.inner.get_master_node().del_node();
    auto       hot = gu::create_typed_node(*f.graph, Ntype_op::Hotmux);
    const auto c0 = f.compare(Ntype_op::LT, 16), c1 = f.compare(Ntype_op::GT, 31);
    hot.create_sink_pin(0).connect_driver(c0);
    hot.create_sink_pin(1).connect_driver(f.a);
    hot.create_sink_pin(2).connect_driver(c1);
    hot.create_sink_pin(3).connect_driver(f.b);
    hot.create_sink_pin(4).connect_driver(f.c(7));
    gu::set_ubits(hot.create_driver_pin(0), 8);
    hot.create_driver_pin(0).connect_sink(sink);
    if (proven) {
      livehd::formal::Prover prover(f.graph.get(), {.min_rlimit = 1'000'000});
      ASSERT_EQ(prover.are_exclusive({c0, c1}).verdict, livehd::formal::Verdict::Proven);
      gu::set_proven(hot, gu::kFormalOnehot);
    }
    const auto r = f.run();
    EXPECT_EQ(r.applied, proven ? 1 : 0);
    EXPECT_EQ(hot.get_sink_pin(0).get_driver_pin(), c0);  // never globally assert contextual true
    EXPECT_EQ(hot.get_sink_pin(2).get_driver_pin().is_const(), proven);
    livehd::formal::Prover after(f.graph.get(), {.min_rlimit = 1'000'000});
    EXPECT_EQ(after.are_exclusive({hot.get_sink_pin(0).get_driver_pin(), hot.get_sink_pin(2).get_driver_pin()}).verdict,
              livehd::formal::Verdict::Proven);
  }
}
}  // namespace

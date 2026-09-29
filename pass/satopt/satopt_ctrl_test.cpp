// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "satopt_ctrl.hpp"

#include "gtest/gtest.h"
#include "node_util.hpp"
#include "prove.hpp"
#include "satopt_sim.hpp"

namespace {
namespace gu = livehd::graph_util;
using Pin    = hhds::Pin_class;
using Node   = hhds::Node_class;
using namespace livehd::satopt;
struct Fixture {
  hhds::GraphLibrary           lib;
  std::shared_ptr<hhds::Graph> g;
  Pin                          a, b, c, data;
  Fixture(std::string_view graph_name) {
    auto io = lib.create_io(graph_name);
    int  id = 1;
    for (auto name : {"a", "b", "c", "data"}) {
      io->add_input(name, id++);
      io->set_bits(name, name == std::string_view("data") ? 8 : 1);
      io->set_unsign(name, true);
    }
    io->add_output("o", 5);
    io->add_output("p", 6);
    g    = io->create_graph();
    a    = g->get_input_pin("a");
    b    = g->get_input_pin("b");
    c    = g->get_input_pin("c");
    data = g->get_input_pin("data");
    for (auto p : {a, b, c}) {
      gu::set_ubits(p, 1);
    }
    gu::set_ubits(data, 8);
  }
  Pin cn(int v) { return gu::create_const(*g, *Dlop::create_integer(v)); }
  Pin op(Ntype_op kind, std::initializer_list<Pin> inputs, int bits = 1) {
    auto n = gu::create_typed_node(*g, kind);
    for (const auto& p : inputs) {
      p.connect_sink(gu::setup_sink_pid(n, 0));
    }
    auto out = n.create_driver_pin(0);
    gu::set_ubits(out, bits);
    return out;
  }
  Pin majority() {
    auto ab = op(Ntype_op::And, {a, b});
    auto ac = op(Ntype_op::And, {a, c});
    auto bc = op(Ntype_op::And, {b, c});
    return op(Ntype_op::Or, {ab, ac, bc});
  }
  Pin  redundant() { return op(Ntype_op::Or, {op(Ntype_op::And, {a, b}), op(Ntype_op::And, {a, op(Ntype_op::Xor, {b, cn(1)})})}); }
  Node consumer(Pin ctrl, Ntype_op type = Ntype_op::Mux, int enable = 4) {
    auto n = gu::create_typed_node(*g, type);
    if (type == Ntype_op::Mux) {
      ctrl.connect_sink(n.create_sink_pin(0));
      data.connect_sink(n.create_sink_pin(1));
      cn(7).connect_sink(n.create_sink_pin(2));
    } else if (type == Ntype_op::Hotmux) {
      ctrl.connect_sink(n.create_sink_pin(0));
      data.connect_sink(n.create_sink_pin(1));
      cn(1).connect_sink(n.create_sink_pin(2));  // intentionally NOT exclusive
      cn(7).connect_sink(n.create_sink_pin(3));
      data.connect_sink(n.create_sink_pin(4));  // default, never a control
    } else {
      ctrl.connect_sink(n.create_sink_pin(enable));
      data.connect_sink(n.create_sink_pin(3));
    }
    auto out = n.create_driver_pin(0);
    gu::set_ubits(out, 8);
    out.connect_sink(g->get_output_pin("o"));
    return n;
  }
  Stage_report run(Budget budget = Budget{}) {
    Options opts;
    opts.stages = {Stage::simp_ctrl};
    opts.budget = budget;
    return livehd::satopt::run(g.get(), opts).at(Stage::simp_ctrl);
  }
  int count() {
    int count = 0;
    for (const auto n : g->body().nodes()) {
      count += !gu::is_builtin_node(n);
    }
    return count;
  }
  void check(Pin value, uint8_t truth) {
    // Exhaustive concrete evaluation, independent of the proof query used by
    // the optimizer. Each test input is a primary leaf in the simulator.
    for (int row = 0; row < 8; ++row) {
      detail::Word_sim      sim({.samples = 1});
      livehd::formal::Model model;
      int                   i = 0;
      for (const auto& p : {a, b, c}) {
        model.push_back({{}, p, *Dlop::create_integer((row >> i++) & 1)});
      }
      ASSERT_TRUE(sim.add_model(model));
      const auto* values = sim.values(value);
      ASSERT_NE(values, nullptr);
      EXPECT_EQ(values->back().bit_test(0), ((truth >> row) & 1) != 0) << row;
    }
  }
};
}  // namespace

TEST(SatoptCtrl, DerivesThreeInputFunctionForMuxSelect) {
  Fixture    f("ctrl_majority");
  auto       m      = f.consumer(f.majority());
  const int  before = f.count();
  const auto r      = f.run();
  EXPECT_EQ(r.applied, 1u);
  EXPECT_LT(f.count(), before);
  f.check(m.create_sink_pin(0).get_driver_pin(), 0xe8);
  EXPECT_EQ(m.create_sink_pin(1).get_driver_pin(), f.data);
  EXPECT_EQ(f.run().applied, 0u);
}

TEST(SatoptCtrl, RegisterAndBothMemoryEnableKinds) {
  int index = 0;
  for (auto type : {Ntype_op::Flop, Ntype_op::Memory}) {
    for (int pid : {4, 20, 13}) {
      if (type == Ntype_op::Flop && pid != 4) {
        continue;
      }
      Fixture f(std::string("ctrl_enable_") + std::to_string(index++));
      auto    n = f.consumer(f.redundant(), type, pid);
      EXPECT_EQ(f.run().applied, 1u);
      EXPECT_EQ(n.create_sink_pin(pid).get_driver_pin(), f.a);
      EXPECT_EQ(n.create_sink_pin(3).get_driver_pin(), f.data);
    }
  }
}

TEST(SatoptCtrl, HotmuxObligationAndDefaultSurvive) {
  Fixture f("ctrl_hotmux");
  auto    n = f.consumer(f.redundant(), Ntype_op::Hotmux);
  EXPECT_EQ(f.run().applied, 1u);
  EXPECT_EQ(gu::type_op_of(n), Ntype_op::Hotmux);
  EXPECT_EQ(n.create_sink_pin(0).get_driver_pin(), f.a);
  EXPECT_EQ(n.create_sink_pin(4).get_driver_pin(), f.data);
  livehd::formal::Prover prover(f.g.get());
  EXPECT_EQ(prover.are_exclusive({n.create_sink_pin(0).get_driver_pin(), n.create_sink_pin(2).get_driver_pin()}).verdict,
            livehd::formal::Verdict::Refuted);
}

TEST(SatoptCtrl, DoesNotTouchDataOnlyOrSharedDataOrWideSelect) {
  for (int mode = 0; mode < 3; ++mode) {
    Fixture f(std::string("ctrl_skip_") + std::to_string(mode));
    auto    p = mode == 2 ? f.op(Ntype_op::And, {f.data, f.cn(7)}, 8) : f.redundant();
    if (mode != 0) {
      f.consumer(p);
    }
    if (mode != 2) {
      p.connect_sink(f.g->get_output_pin("p"));
    }
    const auto epoch = f.g->body_epoch();
    EXPECT_EQ(f.run().applied, 0u);
    EXPECT_EQ(f.g->body_epoch(), epoch);
  }
}

TEST(SatoptCtrl, CounterexamplesRefineSparseSamples) {
  Fixture f("ctrl_cex");
  auto    m = f.consumer(f.majority());
  Budget  budget;
  budget.samples = 1;
  const auto r   = f.run(budget);
  EXPECT_GT(r.refuted, 0u);
  EXPECT_EQ(r.applied, 1u);
  f.check(m.create_sink_pin(0).get_driver_pin(), 0xe8);
}

TEST(SatoptCtrl, BudgetAndUnsupportedConesLeaveGraphIntact) {
  for (int mode = 0; mode < 3; ++mode) {
    Fixture f(std::string("ctrl_budget_") + std::to_string(mode));
    auto    ctrl = f.redundant();
    f.consumer(ctrl);
    Budget budget;
    if (mode == 0) {
      budget.work = 1;
    }
    if (mode == 1) {
      budget.queries = 0;
    }
    if (mode == 2) {
      budget.cone_max = 1;
    }
    const auto epoch = f.g->body_epoch();
    const auto r     = f.run(budget);
    EXPECT_EQ(r.applied, 0u);
    EXPECT_EQ(f.g->body_epoch(), epoch);
    if (mode < 2) {
      EXPECT_EQ(r.state, Stage_state::exhausted);
    }
    if (mode == 2) {
      EXPECT_GT(r.unknown, 0u);
    }
  }
}

TEST(SatoptCtrl, ReusesNeighborWithoutRebuildingItsDataCone) {
  Fixture    f("ctrl_neighbor");
  const auto ab = f.op(Ntype_op::And, {f.a, f.b});
  ab.connect_sink(f.g->get_output_pin("p"));
  const auto na = f.op(Ntype_op::Xor, {f.a, f.cn(1)});
  const auto nb = f.op(Ntype_op::Xor, {f.b, f.cn(1)});
  auto       m  = f.consumer(f.op(Ntype_op::Or, {na, nb}));
  const auto r  = f.run();
  EXPECT_EQ(r.applied, 1u);
  EXPECT_EQ(f.g->get_output_pin("p").get_driver_pin(), ab);
  const auto ctrl = m.create_sink_pin(0).get_driver_pin();
  f.check(ctrl, 0x77);
  bool reused = false;
  for (const auto& s : ctrl.get_master_node().inp_sorted_pins()) {
    for (const auto& d : s.get_driver_pins()) {
      reused |= d == ab;
    }
  }
  EXPECT_TRUE(reused);
}

TEST(SatoptCtrl, RealizationsMatchTruthTablesExhaustively) {
  // Exercise constants, parity, majority, mux and asymmetric functions. Each
  // starts as an unminimized Shannon tree. Check all eight assignments after
  // rewriting, including patterns absent from the simulation filter.
  for (uint8_t truth : {0x00, 0xff, 0x96, 0xe8, 0xca, 0x80, 0xfe, 0x66, 0x3c, 0x17, 0x69, 0x81}) {
    Fixture          f(std::string("ctrl_truth_") + std::to_string(truth));
    std::vector<Pin> rows;
    for (int i = 0; i < 8; ++i) {
      rows.push_back(f.cn((truth >> i) & 1));
    }
    for (auto select : {f.a, f.b, f.c}) {
      for (size_t i = 0; i < rows.size() / 2; ++i) {
        auto n = gu::create_typed_node(*f.g, Ntype_op::Mux);
        select.connect_sink(n.create_sink_pin(0));
        rows[2 * i].connect_sink(n.create_sink_pin(1));
        rows[2 * i + 1].connect_sink(n.create_sink_pin(2));
        auto out = n.create_driver_pin(0);
        gu::set_ubits(out, 1);
        rows[i] = out;
      }
      rows.resize(rows.size() / 2);
    }
    auto       m      = f.consumer(rows[0]);
    const auto before = f.count();
    EXPECT_GT(f.run().applied, 0u) << static_cast<int>(truth);
    EXPECT_LT(f.count(), before);
    f.check(m.create_sink_pin(0).get_driver_pin(), truth);
  }
}

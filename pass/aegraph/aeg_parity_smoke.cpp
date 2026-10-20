//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// Engine parity (todo/livehd/2a-aegraph.html, milestone C): the value-oracle
// cprop cases of pass/cprop/cprop_test.cpp, run under BOTH constant-propagation
// engines through one parameterized fixture. Each case builds a small cone,
// optimizes it with the engine under test, and checks the result against the
// shared test-only evaluators (pass/cprop/cprop_test_eval.hpp) over every input
// combination. Only values are checked: the cases that pin a classic-only SHAPE
// (how many Mux survive, which cell a rewrite mints) stay in cprop_test.cpp and
// the shape assertions were left out here, so a correct engine that picks
// another equal-value form passes.
//
// "aegraph" is exactly pass.aegraph's per-body path (livehd::aegraph::optimize_body:
// the engine, then the classic post steps).
#include <memory>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

#include "absl/container/flat_hash_set.h"
#include "bitwidth.hpp"
#include "cprop.hpp"
#include "cprop_test_eval.hpp"
#include "enableopt.hpp"
#include "graph_library_singleton.hpp"
#include "gtest/gtest.h"
#include "hlop/dlop.hpp"
#include "node_util.hpp"
#include "pass_aegraph.hpp"

namespace {
namespace gu = livehd::graph_util;
using livehd::cprop_testing::build_guard_chain;
using livehd::cprop_testing::Guard_chain;
using livehd::cprop_testing::Mux_graph;
using livehd::cprop_testing::mux_eval;
using livehd::cprop_testing::Test_pin;
using livehd::cprop_testing::Test_values;

enum class Engine { classic, aegraph };

class Aeg_parity : public testing::TestWithParam<Engine> {
protected:
  void SetUp() override { livehd::cprop_testing::mux_graph_library = lib_name("lgdb_aeg_parity_mux"); }

  [[nodiscard]] std::string lib_name(const std::string& base) const {
    return base + (GetParam() == Engine::classic ? "_classic" : "_aegraph");
  }

  // One engine sweep over a graph body, as the compile schedule runs it.
  void run(Cprop& cp, const std::shared_ptr<hhds::Graph>& g) const {
    if (GetParam() == Engine::classic) {
      cp.do_trans(g);
    } else {
      (void)livehd::aegraph::optimize_body(cp, g, livehd::aegraph::Options{.check = livehd::aegraph::Options::Check::full});
    }
  }
  void run(const std::shared_ptr<hhds::Graph>& g) const {
    Cprop cp{};
    run(cp, g);
  }

  // The compile schedule: cprop, bitwidth, enableopt, cprop, bitwidth.
  void optimize_state(const std::shared_ptr<hhds::Graph>& graph) const {
    run(graph);
    Bitwidth{10}.do_trans(graph);
    Enableopt{}.do_trans(graph);
    run(graph);
    Bitwidth{10}.do_trans(graph);
  }
  void optimize_lanes(const std::shared_ptr<hhds::Graph>& graph, const Cprop::Node_scope& guard_scope = {}) const {
    Cprop cprop{true};
    cprop.set_guard_scope(guard_scope);
    run(cprop, graph);
    Bitwidth{10}.do_trans(graph);
    Enableopt{}.do_trans(graph);
    run(cprop, graph);
    Bitwidth{10}.do_trans(graph);
  }
};

TEST_P(Aeg_parity, CpropMuxSharing_PriorityTreePreservesEveryControlCombination) {
  for (bool sign : {false, true}) {
    Mux_graph f(sign ? "priority_signed" : "priority_unsigned", 6, 32, sign, true);
    auto      root = f.b;
    for (int i = 5; i >= 0; --i) {
      root = f.mux(f.controls[i], root, i % 2 ? f.b : f.a);
    }
    root.connect_sink(f.graph->get_output_pin("out"));
    run(f.graph);
    // The innermost identical-arm mux folds. Rebuilding the remaining chain
    // as a select tree is equal-cost, so A3 leaves it alone.
    for (uint64_t mask = 0; mask < 64; ++mask) {
      for (int64_t av : {0, 1, 127}) {
        const int64_t bv       = sign ? -117 : 219;
        int64_t       expected = bv;
        for (int i = 0; i < 6; ++i) {
          if (mask & (uint64_t{1} << i)) {
            expected = i % 2 ? bv : av;
            break;
          }
        }
        auto values = f.inputs(mask, av, bv, true, true);
        EXPECT_EQ(mux_eval(f.output(), values), expected) << mask;
      }
    }
    run(f.graph);
  }
}

TEST_P(Aeg_parity, CpropMuxSharing_HotmuxGroupsOnlyEstablishedExclusiveControls) {
  for (int proof : {0, 1, 2, 3}) {
    for (bool fallback : {false, true}) {
      Mux_graph f("hot_group_" + std::to_string(proof) + (fallback ? "_default" : "_zero"), 4);
      auto      n = f.node(Ntype_op::Hotmux);
      for (int i = 0; i < 4; ++i) {
        // Proof 1 is a decode; proof 2 is an existing formal certificate;
        // proof 3 deliberately repeats a decoded constant and must be refused.
        auto c = proof == 1 || proof == 3 ? f.eq(f.a, proof == 3 ? i % 2 : i) : f.controls[i];
        n.create_sink_pin(2 * i).connect_driver(c);
        n.create_sink_pin(2 * i + 1).connect_driver(i % 2 ? f.b : f.constant(17));
      }
      if (fallback) {
        n.create_sink_pin(8).connect_driver(f.constant(99));
      }
      if (proof == 2) {
        gu::set_proven(n, gu::kFormalOnehot);
      }
      n.create_driver_pin(0).connect_sink(f.graph->get_output_pin("out"));
      run(f.graph);
      if (proof == 0 || proof == 3) {
        EXPECT_FALSE(gu::has_proven(n));
        continue;
      }
      for (uint64_t selection = 0; selection < 5; ++selection) {
        auto          values   = f.inputs(selection < 4 ? uint64_t{1} << selection : 0, selection, 211);
        const int64_t expected = selection == 4 ? (fallback ? 99 : 0) : (selection % 2 ? 211 : 17);
        EXPECT_EQ(mux_eval(f.output(), values), expected);
      }
    }
  }
}

TEST_P(Aeg_parity, CpropMuxSharing_DistributedHoldExtractsEnableOnlyForSingleStagePrivateFlop) {
  for (int variant : {0, 1, 2}) {
    Mux_graph f("distributed_hold_" + std::to_string(variant), 6);
    auto      flop = f.node(Ntype_op::Flop);
    auto      q    = flop.create_driver_pin(0);
    gu::setup_sink_by_name(flop, "enable").connect_driver(f.en);
    gu::setup_sink_by_name(flop, "clock_pin").connect_driver(f.clock);
    gu::setup_sink_by_name(flop, "posclk").connect_driver(f.constant(1));
    gu::setup_sink_by_name(flop, "reset_pin").connect_driver(f.controls[5]);
    gu::setup_sink_by_name(flop, "initial").connect_driver(f.constant(37));
    if (variant == 1) {
      gu::setup_sink_by_name(flop, "pipe_min").connect_driver(f.constant(2));
    }
    auto root = q;
    for (int i = 4; i >= 0; --i) {
      root = f.mux(f.controls[i], root, i % 2 ? q : f.a);
    }
    root.connect_sink(gu::setup_sink_by_name(flop, "din"));
    if (variant == 2) {
      root.connect_sink(f.graph->get_output_pin("observe"));
    }
    q.connect_sink(f.graph->get_output_pin("out"));
    optimize_state(f.graph);
    const auto enable = gu::get_driver_of_sink_name(flop, "enable");
    const auto data   = gu::get_driver_of_sink_name(flop, "din");
    EXPECT_EQ(enable == f.en, variant != 0);
    EXPECT_EQ(gu::get_driver_of_sink_name(flop, "reset_pin"), f.controls[5]);
    EXPECT_EQ(gu::const_of(gu::get_driver_of_sink_name(flop, "initial")).to_just_i64(), 37);
    for (uint64_t mask = 0; mask < 64; ++mask) {
      for (bool enabled : {false, true}) {
        auto values                       = f.inputs(mask, 73, 211, enabled);
        values[q.get_class_index().value] = 149;
        int64_t expected                  = 149;
        if (enabled) {
          for (int i = 0; i < 5; ++i) {
            if (mask & (uint64_t{1} << i)) {
              expected = i % 2 ? 149 : 73;
              break;
            }
          }
        }
        // The single-stage transition includes reset, whose priority must stay
        // outside the newly derived enable. For a pipeline compare the input
        // and enable separately, rather than pretending its last Q is stage 1.
        const auto actual = mux_eval(enable, values) ? mux_eval(data, values) : 149;
        EXPECT_EQ(actual, expected);
        if (variant == 0) {
          EXPECT_EQ(mask & 32 ? 37 : actual, mask & 32 ? 37 : expected);
        }
      }
    }
  }
}

TEST_P(Aeg_parity, CpropMuxSharing_SharedSubconeRemainsAnOpaqueTerminal) {
  Mux_graph f("shared_boundary", 6);
  auto      shared = f.mux(f.controls[0], f.a, f.b);
  shared.connect_sink(f.graph->get_output_pin("observe"));
  auto root = shared;
  for (int i = 5; i >= 1; --i) {
    root = f.mux(f.controls[i], root, i % 2 ? shared : f.a);
  }
  root.connect_sink(f.graph->get_output_pin("out"));
  run(f.graph);
  EXPECT_FALSE(shared.is_invalid());
  EXPECT_EQ(gu::type_op_of(shared.get_master_node()), Ntype_op::Mux);
  for (uint64_t mask = 0; mask < 64; ++mask) {
    auto values          = f.inputs(mask, 71, 193);
    auto expected_shared = mask & 1 ? 193 : 71;
    auto expected        = expected_shared;
    for (int i = 1; i < 6; ++i) {
      if (mask & (uint64_t{1} << i)) {
        expected = i % 2 ? expected_shared : 71;
        break;
      }
    }
    EXPECT_EQ(mux_eval(f.output(), values), expected);
    EXPECT_EQ(mux_eval(shared, values), expected_shared);
  }
}

// The per-bit `if (rst) b <= 1; else if (upd) b <= gi ? 0 : gj ? 1 : q;` nest
// (bedrock's LRU matrix under BR_REGLI) and tolg's shadow enable chain next to
// it. As Mux trees they reached mux sharing, which rebuilt each as a Hotmux
// over path conjunctions; they must become And/Or logic instead, value-exact
// for every input.
TEST_P(Aeg_parity, CpropBool_OneBitIfChainsBecomeAndOrLogic) {
  Mux_graph f("bool_if_chain", 4, 1);
  auto      rst  = f.not_zero(f.controls[0]);
  auto      upd  = f.not_zero(f.controls[1]);
  auto      gi   = f.not_zero(f.controls[2]);
  auto      gj   = f.not_zero(f.controls[3]);
  auto      q    = f.not_zero(f.a);
  auto      en   = f.mux(rst, f.mux(upd, f.constant(0), f.constant(1)), f.constant(1));
  auto      next = f.mux(gi, f.mux(gj, q, f.constant(1)), f.constant(0));
  auto      d    = f.mux(rst, f.mux(upd, q, next), f.constant(1));
  en.connect_sink(f.graph->get_output_pin("out"));
  d.connect_sink(f.graph->get_output_pin("observe"));
  optimize_state(f.graph);
  // Left: the `upd ? q : next` hold (no flop here to make it dead), and the
  // negated-selector swap's Mux(x,1,0), which scalar_mux keeps on purpose.
  for (uint64_t mask = 0; mask < 16; ++mask) {
    for (int64_t qv : {0, 1}) {
      const bool    r = mask & 1, u = mask & 2, i = mask & 4, j = mask & 8;
      const int64_t nxt    = i ? 0 : (j ? 1 : qv);
      auto          values = f.inputs(mask, qv, 0);
      EXPECT_EQ(mux_eval(f.output(), values), r || u ? 1 : 0) << mask;
      auto values2 = f.inputs(mask, qv, 0);
      EXPECT_EQ(mux_eval(f.observed(), values2), r ? 1 : (u ? nxt : qv)) << mask << " q=" << qv;
    }
  }
}

// A flop enabled by `rst | upd` samples din only when one of them holds. After
// the reset arm is peeled into an Or, the per-lane hold `upd ? next : q[k]` is
// dead: rst false and the enable true force upd. canonicalize_flop_hold only
// sees a Mux directly on din; this one sits inside a Set_mask lane.
TEST_P(Aeg_parity, CpropBool_EnableMakesLaneHoldMuxDead) {
  auto& lib = livehd::Hhds_graph_library::instance(lib_name("lgdb_cprop_flop_enable_lane"));
  auto  io  = lib.create_io("lane_hold");
  io->add_input("rst", 1);
  io->set_bits("rst", 1);
  io->add_input("upd", 2);
  io->set_bits("upd", 1);
  io->add_input("g", 3);
  io->set_bits("g", 1);
  io->add_input("clock", 4);
  io->set_bits("clock", 1);
  io->add_output("q", 5);
  io->set_bits("q", 4);
  auto g        = io->create_graph();
  auto constant = [&](int64_t v) { return gu::create_const(*g, *Dlop::create_integer(v)); };
  auto not_zero = [&](Test_pin x) {
    auto e = gu::create_typed_node(*g, Ntype_op::EQ, 1);
    gu::setup_sink_pid(e, 0).connect_driver(x);
    gu::setup_sink_pid(e, 0).connect_driver(constant(0));
    gu::set_ubits(e.create_driver_pin(0), 1);
    auto n = gu::create_typed_node(*g, Ntype_op::Xor, 1);
    gu::setup_sink_pid(n, 0).connect_driver(e.create_driver_pin(0));
    gu::setup_sink_pid(n, 0).connect_driver(constant(1));
    gu::set_ubits(n.create_driver_pin(0), 1);
    return n.create_driver_pin(0);
  };
  auto mux = [&](Test_pin s, Test_pin f, Test_pin t) {
    auto n = gu::create_typed_node(*g, Ntype_op::Mux, 1);
    n.create_sink_pin(0).connect_driver(s);
    n.create_sink_pin(1).connect_driver(f);
    n.create_sink_pin(2).connect_driver(t);
    gu::set_ubits(n.create_driver_pin(0), 1);
    return n.create_driver_pin(0);
  };
  auto rst  = not_zero(g->get_input_pin("rst"));
  auto upd  = not_zero(g->get_input_pin("upd"));
  auto next = not_zero(g->get_input_pin("g"));
  auto flop = gu::create_typed_node(*g, Ntype_op::Flop, 4);
  auto q    = flop.create_driver_pin(0);
  gu::set_ubits(q, 4);
  q.connect_sink(g->get_output_pin("q"));
  g->get_input_pin("clock").connect_sink(gu::setup_sink_by_name(flop, "clock_pin"));
  auto enable = gu::create_typed_node(*g, Ntype_op::Or, 1);
  gu::setup_sink_pid(enable, 0).connect_driver(rst);
  gu::setup_sink_pid(enable, 0).connect_driver(upd);
  gu::set_ubits(enable.create_driver_pin(0), 1);
  enable.create_driver_pin(0).connect_sink(gu::setup_sink_by_name(flop, "enable"));
  auto lane_read = gu::create_typed_node(*g, Ntype_op::Get_mask);
  gu::connect_mask_operands(lane_read,
                            q,
                            livehd::graph_util::mask_window(livehd::graph_util::const_of(constant(2))).first,
                            livehd::graph_util::mask_window(livehd::graph_util::const_of(constant(2))).second);
  gu::set_ubits(lane_read.create_driver_pin(0), 1);
  auto lane = mux(rst, mux(upd, lane_read.create_driver_pin(0), next), constant(1));
  auto din  = gu::create_set_mask(*g,
                                  q,
                                  lane,
                                  livehd::graph_util::mask_window(livehd::graph_util::const_of(constant(2))).first,
                                  livehd::graph_util::mask_window(livehd::graph_util::const_of(constant(2))).second);
  gu::set_ubits(din.create_driver_pin(0), 4);
  din.create_driver_pin(0).connect_sink(gu::setup_sink_by_name(flop, "din"));
  optimize_state(g);
  optimize_state(g);
  size_t muxes = 0;
  for (auto n : g->body().nodes()) {
    muxes += gu::type_op_of(n) == Ntype_op::Mux;
  }
  EXPECT_EQ(muxes, 0);
  // din's lane is now rst | next: check it against every rst/g with the
  // enable held (upd forced when rst is low).
  auto lane_now = gu::get_driver_of_sink_name(flop, "din");
  ASSERT_FALSE(lane_now.is_invalid());
  for (int64_t r : {0, 1}) {
    for (int64_t gv : {0, 1}) {
      Test_values values;
      values.emplace(g->get_input_pin("rst").get_class_index().value, r);
      values.emplace(g->get_input_pin("upd").get_class_index().value, 1);
      values.emplace(g->get_input_pin("g").get_class_index().value, gv);
      values.emplace(q.get_class_index().value, 0);
      EXPECT_EQ((mux_eval(lane_now, values) >> 1) & 1, r ? 1 : gv) << r << gv;
    }
  }
}

// bedrock's pairwise arbiter row, can[i] = AND_{j != i} (!req[j] | prio[i][j]),
// arrives as one And of per-bit Or terms. Every term is the same expression
// over single-bit slices at a shifted position, so the And is one word test
// under a mask; likewise an Or of shifted per-bit Ands.
TEST_P(Aeg_parity, CpropBool_BitSliceReductionsBecomeWordTests) {
  Mux_graph f("bool_reduce", 1, 16);
  auto      bit = [&](Test_pin src, int pos) {
    auto n   = gu::create_get_mask(*f.graph, src, pos, pos + 1);
    auto out = n.create_driver_pin(0);
    gu::set_ubits(out, 1);
    return out;
  };
  auto can = f.node(Ntype_op::And, 1);
  for (int j = 0; j < 6; ++j) {
    if (j != 2) {
      auto term = f.logic(Ntype_op::Or, {f.eq(bit(f.a, j), 0), bit(f.b, 8 + j)});
      gu::setup_sink_pid(can, 0).connect_driver(term);
    }
  }
  auto any = f.node(Ntype_op::Or, 1);
  for (int j = 0; j < 5; ++j) {
    gu::setup_sink_pid(any, 0).connect_driver(f.logic(Ntype_op::And, {bit(f.a, j), bit(f.b, j + 3)}));
  }
  can.create_driver_pin(0).connect_sink(f.graph->get_output_pin("out"));
  any.create_driver_pin(0).connect_sink(f.graph->get_output_pin("observe"));
  optimize_state(f.graph);
  // One compare per family instead of a gate per bit.
  uint64_t rng = 0x9E3779B97F4A7C15ULL;
  for (int sample = 0; sample < 3000; ++sample) {
    rng                        ^= rng << 13;
    rng                        ^= rng >> 7;
    rng                        ^= rng << 17;
    const int64_t av            = static_cast<int64_t>(rng & 0xFFFF);
    const int64_t bv            = static_cast<int64_t>((rng >> 16) & 0xFFFF);
    int64_t       expected_can  = 1;
    for (int j = 0; j < 6; ++j) {
      if (j != 2) {
        expected_can &= (((av >> j) & 1) == 0 || ((bv >> (8 + j)) & 1) != 0) ? 1 : 0;
      }
    }
    int64_t expected_any = 0;
    for (int j = 0; j < 5; ++j) {
      expected_any |= ((av >> j) & (bv >> (j + 3)) & 1);
    }
    auto values = f.inputs(0, av, bv);
    EXPECT_EQ(mux_eval(f.output(), values), expected_can) << av << " " << bv;
    auto values2 = f.inputs(0, av, bv);
    EXPECT_EQ(mux_eval(f.observed(), values2), expected_any) << av << " " << bv;
  }
}

// mask(a op b, W) pushes the window into a private bitwise op when at most one
// operand needs a new window: `mask((a << 1) ^ k, 16)` keeps every step at 16
// bits instead of computing a 17-bit value only to cut it back.
TEST_P(Aeg_parity, CpropMasks_LowWindowNarrowsPrivateFold) {
  Mux_graph f("mask_narrow_fold", 1, 16);
  auto      shl = f.node(Ntype_op::SHL, 17);
  gu::setup_sink_by_name(shl, "a").connect_driver(f.a);
  gu::setup_sink_by_name(shl, "b").connect_driver(f.constant(1));
  auto fold = f.logic(Ntype_op::Xor, {shl.create_driver_pin(0), f.constant(0x1A5A5)});
  gu::set_ubits(fold, 17);
  auto cut = gu::create_get_mask(*f.graph, fold, 0, 16);
  gu::set_ubits(cut.create_driver_pin(0), 16);
  cut.create_driver_pin(0).connect_sink(f.graph->get_output_pin("out"));
  optimize_state(f.graph);
  for (int64_t av : {0, 1, 0x7FFF, 0x8000, 0xFFFF, 0x1234}) {
    auto values = f.inputs(0, av, 0);
    EXPECT_EQ(mux_eval(f.output(), values), ((av << 1) ^ 0x1A5A5) & 0xFFFF) << av;
  }
}

// Width-free Sext/Get_mask folds on the shape a Verilog reader emits for a
// signed 1-bit write (`sext(x, 12)[0,1)` re-signed by `sext(.., 1)`): a Sext
// keeping b bits reads no bit of a low [0,w) window above b <= w, and a [lo,hi)
// window with hi <= b reads no bit of the Sext above its kept bits.
TEST_P(Aeg_parity, CpropMasks_SextAndLowWindowChainsCollapse) {
  Mux_graph f("sext_window_chain", 1, 12, true);
  auto      s1 = f.node(Ntype_op::Sext);
  gu::setup_sink_by_name(s1, "a").connect_driver(f.a);
  gu::setup_sink_by_name(s1, "b").connect_driver(f.constant(12));
  auto m1 = gu::create_get_mask(*f.graph, s1.create_driver_pin(0), 0, 1);
  gu::set_ubits(m1.create_driver_pin(0), 1);
  auto s2 = f.node(Ntype_op::Sext, 1);
  gu::set_sbits(s2.create_driver_pin(0), 1);
  gu::setup_sink_by_name(s2, "a").connect_driver(m1.create_driver_pin(0));
  gu::setup_sink_by_name(s2, "b").connect_driver(f.constant(1));
  auto wide = gu::create_get_mask(*f.graph, s2.create_driver_pin(0), 0, 8);
  gu::set_ubits(wide.create_driver_pin(0), 8);
  wide.create_driver_pin(0).connect_sink(f.graph->get_output_pin("out"));
  auto bit = gu::create_get_mask(*f.graph, s2.create_driver_pin(0), 0, 1);
  gu::set_ubits(bit.create_driver_pin(0), 1);
  bit.create_driver_pin(0).connect_sink(f.graph->get_output_pin("observe"));
  run(f.graph);
  for (int64_t av : {0, 1, 2, 3, -1, -2, 0x7FF, -0x800}) {
    auto values = f.inputs(0, av, 0);
    EXPECT_EQ(mux_eval(f.output(), values), (av & 1) ? 0xFF : 0) << av;
    EXPECT_EQ(mux_eval(f.observed(), values), av & 1) << av;
  }
}

// ...but windowing EVERY operand splits word-level logic into bit extracts:
// br_enc_gray2bin's `bit0((y >>> 1) ^ y)` became y[1] ^ y[0] per output bit
// (67 -> 167 Get_masks, a 1.25x slower sim). It must stay one word-level Xor.
TEST_P(Aeg_parity, CpropMasks_LowWindowDoesNotSplitWordLogicIntoBits) {
  Mux_graph f("mask_no_bit_split", 1, 16);
  auto      shift = [&](Ntype_op op, Test_pin x, int amount) {
    auto n = f.node(op, 16);
    gu::setup_sink_by_name(n, "a").connect_driver(x);
    gu::setup_sink_by_name(n, "b").connect_driver(f.constant(amount));
    return n.create_driver_pin(0);
  };
  auto y = f.logic(Ntype_op::Xor, {f.a, shift(Ntype_op::SRA, f.a, 2)});
  gu::set_ubits(y, 16);
  auto top = f.logic(Ntype_op::Xor, {shift(Ntype_op::SRA, y, 1), y});
  gu::set_ubits(top, 16);
  auto cut = gu::create_get_mask(*f.graph, top, 0, 1);
  gu::set_ubits(cut.create_driver_pin(0), 1);
  cut.create_driver_pin(0).connect_sink(f.graph->get_output_pin("out"));
  optimize_state(f.graph);
  for (int64_t av : {0, 1, 2, 3, 5, 0x7FFF, 0x8000, 0xFFFF, 0x1234}) {
    const int64_t yv     = av ^ (av >> 2);
    auto          values = f.inputs(0, av, 0);
    EXPECT_EQ(mux_eval(f.output(), values), ((yv >> 1) ^ yv) & 1) << av;
  }
}

// Sharing a nest of PROVEN 0/1 values buys 1-bit selects with 1-bit predicate
// gates (one conjunction per path); the nest must stay Muxes, value-exact.
TEST_P(Aeg_parity, CpropMuxSharing_ZeroOneSelectionsStayMuxes) {
  Mux_graph f("share_bool01", 5, 1);
  auto      x  = f.not_zero(f.controls[2]);
  auto      y  = f.not_zero(f.controls[3]);
  auto      z  = f.not_zero(f.controls[4]);
  auto      s0 = f.not_zero(f.controls[0]);
  auto      s1 = f.not_zero(f.controls[1]);
  f.mux(s0, f.mux(s1, x, y), f.mux(s1, z, y)).connect_sink(f.graph->get_output_pin("out"));
  run(f.graph);
  for (uint64_t mask = 0; mask < 32; ++mask) {
    const bool b0 = mask & 1, b1 = mask & 2, bx = mask & 4, by = mask & 8, bz = mask & 16;
    const bool expected = b0 ? (b1 ? by : bz) : (b1 ? by : bx);
    auto       values   = f.inputs(mask, 0, 0);
    EXPECT_EQ(mux_eval(f.output(), values), expected ? 1 : 0) << mask;
  }
}

TEST_P(Aeg_parity, CpropMuxSharing_TwoGroupSelectTreePreservesWideSignedControlsAndBooleanData) {
  for (bool boolean_data : {false, true}) {
    Mux_graph f("two_group_" + std::to_string(boolean_data), 3, 8, true, true);
    auto      a = f.a, b = f.b;
    if (boolean_data) {
      a = gu::create_get_mask(*f.graph, a, 0, 1).create_driver_pin(0);
      b = gu::create_get_mask(*f.graph, b, 0, 1).create_driver_pin(0);
    }
    f.mux(f.controls[0], f.mux(f.controls[1], a, b), f.mux(f.controls[2], a, b)).connect_sink(f.graph->get_output_pin("out"));
    run(f.graph);
    for (int s : {-2, 0, 1, 4}) {
      for (int c : {-2, 0, 1, 4}) {
        for (int d : {-2, 0, 1, 4}) {
          auto values                                   = f.inputs(0, -7, 26);
          values[f.controls[0].get_class_index().value] = s;
          values[f.controls[1].get_class_index().value] = c;
          values[f.controls[2].get_class_index().value] = d;
          const auto expected                           = (s ? d : c) ? 26 : -7;
          EXPECT_EQ(mux_eval(f.output(), values), boolean_data ? expected & 1 : expected);
        }
      }
    }
  }
}

TEST_P(Aeg_parity, CpropMuxSharing_TwoGroupHotmuxPreservesDefaultAndUnprovenObligation) {
  for (bool exclusive : {false, true}) {
    for (bool default_is_b : {false, true}) {
      Mux_graph f("two_group_hot_" + std::to_string(exclusive) + std::to_string(default_is_b), 3);
      auto      root = f.node(Ntype_op::Hotmux);
      root.create_sink_pin(0).connect_driver(exclusive ? f.eq(f.controls[0], 0) : f.controls[0]);
      root.create_sink_pin(1).connect_driver(f.mux(f.controls[1], f.a, f.b));
      root.create_sink_pin(2).connect_driver(exclusive ? f.eq(f.controls[0], 1) : f.controls[1]);
      root.create_sink_pin(3).connect_driver(f.mux(f.controls[2], f.a, f.b));
      root.create_sink_pin(4).connect_driver(default_is_b ? f.b : f.a);
      root.create_driver_pin(0).connect_sink(f.graph->get_output_pin("out"));
      livehd::share_mux_regions(*f.graph, false);
      if (!exclusive) {
        EXPECT_EQ(gu::type_op_of(root), Ntype_op::Hotmux);
        EXPECT_FALSE(gu::has_proven(root));
        continue;
      }
      for (unsigned mask = 0; mask < 8; ++mask) {
        for (int select : {0, 1, 2}) {
          auto values                                   = f.inputs(mask, 37, 91);
          values[f.controls[0].get_class_index().value] = select;
          const bool choose_b = select == 0 ? bool(mask & 2) : select == 1 ? bool(mask & 4) : default_is_b;
          EXPECT_EQ(mux_eval(f.output(), values), choose_b ? 91 : 37);
        }
      }
    }
  }
}

// Indexed reads may resolve a shared writer, but must preserve both the
// narrow observation and the complete word, including lane truncation.
TEST_P(Aeg_parity, CpropMasks_StraddlingSlicePreservesSharedWord) {
  auto& lib    = livehd::Hhds_graph_library::instance(lib_name("lgdb_cprop_gather_shared"));
  auto  io     = lib.create_io("gather_shared");
  io->add_input("d", 1);
  io->set_bits("d", 1);
  io->add_output("q", 2);
  io->set_bits("q", 2);
  io->add_output("w", 3);
  io->set_bits("w", 8);
  auto g     = io->create_graph();
  auto value = gu::create_const(*g, *Dlop::create_integer(0));
  for (int i = 0; i < 8; ++i) {
    auto write = gu::create_typed_node(*g, Ntype_op::Set_mask);
    gu::setup_sink_by_name(write, "a").connect_driver(value);
    livehd::graph_util::connect_bit_range(write, i, i + 1);
    gu::setup_sink_by_name(write, "value").connect_driver(g->get_input_pin("d"));
    gu::set_ubits(write.create_driver_pin(0), 8);
    value = write.create_driver_pin(0);
  }
  auto read = gu::create_get_mask(*g, value, 3, 5);
  gu::set_ubits(read.create_driver_pin(0), 2);
  read.create_driver_pin(0).connect_sink(g->get_output_pin("q"));
  value.connect_sink(g->get_output_pin("w"));

  run(g);

  auto q = g->get_output_pin("q").get_driver_pin();
  ASSERT_FALSE(q.is_invalid());
  const auto word = g->get_output_pin("w").get_driver_pin();
  ASSERT_FALSE(word.is_invalid());
  for (int64_t data : {-2, -1, 0, 1, 2, 3, 255, 256}) {
    Test_values values{
        {static_cast<uint64_t>(g->get_input_pin("d").get_class_index().value), data}
    };
    EXPECT_EQ(mux_eval(q, values), (data & 1) ? 3 : 0);
    EXPECT_EQ(mux_eval(word, values), (data & 1) ? 255 : 0);
  }
}

// Each old version must retain its own overwritten value after later writes.
TEST_P(Aeg_parity, CpropMasks_SharedOverwriteVersionsPreserveSignedSlices) {
  Mux_graph f("shared_overwrites", 0, 8, true);
  auto      write = [&](Test_pin base, Test_pin value) {
    return gu::create_set_mask(*f.graph,
                               base,
                               value,
                               livehd::graph_util::mask_window(livehd::graph_util::const_of(f.constant(6))).first,
                               livehd::graph_util::mask_window(livehd::graph_util::const_of(f.constant(6))).second)
        .create_driver_pin(0);
  };
  auto first = write(f.a, f.b);
  auto last  = write(first, f.constant(1));
  first.connect_sink(f.graph->get_output_pin("observe"));
  last.connect_sink(f.graph->get_output_pin("out"));
  run(f.graph);
  for (int64_t a : {-128, -3, -1, 0, 5, 127}) {
    for (int64_t b : {-9, -1, 0, 1, 2, 3, 8}) {
      auto v1 = f.inputs(0, a, b), v2 = v1;
      EXPECT_EQ(mux_eval(f.observed(), v1), (a & ~6LL) | ((b & 3) << 1));
      EXPECT_EQ(mux_eval(f.output(), v2), (a & ~6LL) | 2);
    }
  }
}

// A private conjunction specialized under its Or parent's condition is no
// longer the expression entered into CSE at its earlier forward visit.
TEST_P(Aeg_parity, CpropBool_SpecializedProducerInvalidatesItsCseEntry) {
  Mux_graph f("specialized_cse", 2, 1);
  auto      x     = f.not_zero(f.controls[0]);
  auto      nx    = f.eq(f.controls[0], 0);
  auto      y     = f.not_zero(f.controls[1]);
  auto      first = f.logic(Ntype_op::And, {nx, y});
  f.logic(Ntype_op::Or, {x, first}).connect_sink(f.graph->get_output_pin("out"));
  f.logic(Ntype_op::And, {nx, y}).connect_sink(f.graph->get_output_pin("observe"));
  optimize_state(f.graph);
  for (uint64_t mask = 0; mask < 4; ++mask) {
    auto a = f.inputs(mask, 0, 0), b = a;
    EXPECT_EQ(mux_eval(f.output(), a), bool(mask & 1) || bool(mask & 2));
    EXPECT_EQ(mux_eval(f.observed(), b), !bool(mask & 1) && bool(mask & 2));
  }
}

TEST_P(Aeg_parity, CpropBool_XorInversionRequiresStructuralBooleanValue) {
  for (bool boolean : {false, true}) {
    for (bool bitwise_not : {false, true}) {
      Mux_graph f("inversion_domain_" + std::to_string(boolean) + std::to_string(bitwise_not), 1, 8, true, true);
      auto      source = f.controls[0];
      if (boolean) {
        source = gu::create_get_mask(*f.graph, source, 0, 1).create_driver_pin(0);
      } else {
        gu::set_ubits(source, 1);  // A hint is not a bool01 proof.
      }
      auto inversion = f.node(bitwise_not ? Ntype_op::Not : Ntype_op::Xor);
      gu::setup_sink_pid(inversion, 0).connect_driver(source);
      if (!bitwise_not) {
        gu::setup_sink_pid(inversion, 0).connect_driver(f.constant(1));
      }
      f.mux(inversion.create_driver_pin(0), f.a, f.b).connect_sink(f.graph->get_output_pin("out"));
      run(f.graph);
      for (int64_t input : {-2, -1, 0, 1, 2, 127, 128}) {
        auto values                                   = f.inputs(0, -7, 26);
        values[f.controls[0].get_class_index().value] = input;
        const auto value                              = boolean ? input & 1 : input;
        const auto select                             = bitwise_not ? ~value : value ^ 1;
        EXPECT_EQ(mux_eval(f.output(), values), select ? 26 : -7);
      }
    }
  }
}

TEST_P(Aeg_parity, CpropOpSharing_PositionalAndBankedOperatorsPreserveValues) {
  for (auto op :
       {Ntype_op::Sum,  Ntype_op::Mult, Ntype_op::And,      Ntype_op::Or,       Ntype_op::Xor,      Ntype_op::EQ,    Ntype_op::LT,
        Ntype_op::GT,   Ntype_op::Ror,  Ntype_op::Not,      Ntype_op::SHL,      Ntype_op::SRA,      Ntype_op::Div,   Ntype_op::Rem,
        Ntype_op::Sext, Ntype_op::Rxor, Ntype_op::Popcount, Ntype_op::Get_mask, Ntype_op::Set_mask, Ntype_op::Concat}) {
    SCOPED_TRACE(std::string{Ntype::get_name(op)});
    Mux_graph  f("opshare_" + std::string{Ntype::get_name(op)}, 2, 8, false, true);
    const auto expr = [&](Test_pin value) {
      auto n = f.node(op);
      if (op == Ntype_op::Get_mask || op == Ntype_op::Set_mask) {
        gu::connect_mask_operands(n,
                                  value,
                                  livehd::graph_util::mask_window(livehd::graph_util::const_of(f.constant(15))).first,
                                  livehd::graph_util::mask_window(livehd::graph_util::const_of(f.constant(15))).second,
                                  op == Ntype_op::Set_mask ? f.controls[1] : Test_pin{});
      } else if (op == Ntype_op::Concat) {
        auto lane = gu::create_get_mask(*f.graph, value, 0, 8);
        n.create_sink_pin(0).connect_driver(lane.create_driver_pin(0));
        n.create_sink_pin(1).connect_driver(f.constant(8));
        n.create_sink_pin(2).connect_driver(f.controls[1]);
        n.create_sink_pin(3).connect_driver(f.constant(8));
        gu::set_ubits(n.create_driver_pin(0), 16);
      } else {
        gu::setup_sink_pid(n, 0).connect_driver(value);
        if (Ntype::sink_bank_count(op)) {
          gu::setup_sink_pid(n, Ntype::sink_bank_count(op) == 2 && op != Ntype_op::Sum ? 1 : 0).connect_driver(f.controls[1]);
        } else if (op != Ntype_op::Not) {
          n.create_sink_pin(1).connect_driver(f.constant(op == Ntype_op::SHL || op == Ntype_op::SRA ? 2 : 3));
        }
      }
      return n.create_driver_pin(0);
    };
    auto root = f.mux(f.controls[0], expr(f.a), expr(f.b));
    root.connect_sink(f.graph->get_output_pin("out"));
    std::vector<int64_t> expected;
    for (unsigned mask = 0; mask < 4; ++mask) {
      for (int a : {1, 7, 15, 31}) {
        auto inputs = f.inputs(mask, a, 5, true, true);
        expected.push_back(mux_eval(f.output(), inputs));
      }
    }
    run(f.graph);
    size_t i = 0;
    for (unsigned mask = 0; mask < 4; ++mask) {
      for (int a : {1, 7, 15, 31}) {
        auto inputs = f.inputs(mask, a, 5, true, true);
        EXPECT_EQ(mux_eval(f.output(), inputs), expected[i++]);
      }
    }
  }
}

// A constant shift is wiring: `mux(s, a << 1, b << 2)` must not become
// `mux(s, a, b) << mux(s, 1, 2)`, a barrel shifter. A shared variable amount
// still shares (it saves a whole shifter).
TEST_P(Aeg_parity, CpropOpSharing_ConstantShiftAmountsAreNotShared) {
  for (auto op : {Ntype_op::SHL, Ntype_op::SRA}) {
    for (const bool variable : {false, true}) {
      SCOPED_TRACE(std::string{Ntype::get_name(op)} + (variable ? " variable" : " constant"));
      Mux_graph  f(std::string{"opshare_amount_"} + std::string{Ntype::get_name(op)} + (variable ? "_v" : "_c"), 3, 8, false, true);
      const auto expr = [&](Test_pin value, int amount) {
        auto n = f.node(op);
        gu::setup_sink_pid(n, 0).connect_driver(value);
        n.create_sink_pin(1).connect_driver(variable ? f.controls[amount] : f.constant(amount));
        return n.create_driver_pin(0);
      };
      f.mux(f.controls[0], expr(f.a, 1), expr(f.b, 2)).connect_sink(f.graph->get_output_pin("out"));
      std::vector<int64_t> expected;
      for (unsigned mask = 0; mask < 8; ++mask) {
        auto values = f.inputs(mask, 23, 5);
        expected.push_back(mux_eval(f.output(), values));
      }
      run(f.graph);
      for (unsigned mask = 0; mask < 8; ++mask) {
        auto values = f.inputs(mask, 23, 5);
        EXPECT_EQ(mux_eval(f.output(), values), expected[mask]);
      }
    }
  }
}

TEST_P(Aeg_parity, CpropOpSharing_MultisetsRetainDuplicatesAndBothBanks) {
  Mux_graph  f("opshare_banks", 2);
  const auto expr = [&](bool second) {
    auto n = f.node(Ntype_op::Sum);
    for (auto p : {second ? f.b : f.a, second ? f.b : f.a, f.controls[1]}) {
      gu::setup_sink_pid(n, 0).connect_driver(p);
    }
    gu::setup_sink_pid(n, 1).connect_driver(second ? f.a : f.b);
    gu::setup_sink_pid(n, 1).connect_driver(second ? f.clock : f.en);
    return n.create_driver_pin(0);
  };
  f.mux(f.controls[0], expr(false), expr(true)).connect_sink(f.graph->get_output_pin("out"));
  run(f.graph);
  for (unsigned mask = 0; mask < 4; ++mask) {
    auto values = f.inputs(mask, 17, 5);
    EXPECT_EQ(mux_eval(f.output(), values), (mask & 1 ? -7 : 28) + int64_t((mask >> 1) & 1));
  }
}

TEST_P(Aeg_parity, CpropOpSharing_ConcatDoesNotCreateOverwideMixedSignLanes) {
  Mux_graph f("opshare_concat_signed", 1, 8, true, true);
  f.graph->get_io()->set_unsign("b", true);
  gu::set_ubits(f.b, 8);
  const auto concat = [&](Test_pin value) {
    auto n = f.node(Ntype_op::Concat);
    n.create_sink_pin(0).connect_driver(value);
    n.create_sink_pin(1).connect_driver(f.constant(8));
    n.create_sink_pin(2).connect_driver(f.controls[0]);
    n.create_sink_pin(3).connect_driver(f.constant(8));
    gu::set_ubits(n.create_driver_pin(0), 16);
    return n.create_driver_pin(0);
  };
  f.mux(f.controls[0], concat(f.a), concat(f.b)).connect_sink(f.graph->get_output_pin("out"));
  run(f.graph);
  Bitwidth{10}.do_trans(f.graph);
  for (unsigned select : {0, 1}) {
    auto values = f.inputs(select, -1, 255);
    EXPECT_EQ(mux_eval(f.output(), values), (255 << 8) + select);
  }
}

TEST_P(Aeg_parity, CpropOpSharing_IndexedSelectionPreservesAllInRangeOperandSlots) {
  Mux_graph f("opshare_index", 1, 8, false, true);
  auto      mux           = f.node(Ntype_op::Mux);
  auto      selected_bits = gu::create_get_mask(*f.graph, f.controls[0], 0, 2);
  mux.create_sink_pin(0).connect_driver(selected_bits.create_driver_pin(0));
  for (int i = 0; i < 4; ++i) {
    auto n = f.node(Ntype_op::Sum);
    gu::setup_sink_pid(n, 0).connect_driver(f.a);
    gu::setup_sink_pid(n, 0).connect_driver(f.constant(10 + i));
    mux.create_sink_pin(i + 1).connect_driver(n.create_driver_pin(0));
  }
  mux.create_driver_pin(0).connect_sink(f.graph->get_output_pin("out"));
  run(f.graph);
  for (int select : {-1, 0, 1, 2, 3, 4, 128}) {
    auto values                                   = f.inputs(0, 7, 0);
    values[f.controls[0].get_class_index().value] = select;
    EXPECT_EQ(mux_eval(f.output(), values), 17 + (select & 3));
  }
}

// Inside a guard scope (a lifted loop body) the guard moves onto the term
// (acc OP mux(c_i, id, x_i)) and the compile schedule flattens the plain
// chain into one n-ary operator: AND-OR logic, not a serial MUX chain.
TEST_P(Aeg_parity, CpropMux_GuardedAccumulationBecomesOneOperator) {
  for (const auto op : {Ntype_op::Or, Ntype_op::And, Ntype_op::Xor}) {
    for (const bool update_true : {true, false}) {
      SCOPED_TRACE(std::string{Ntype::get_name(op)} + (update_true ? " true-arm" : " false-arm"));
      Mux_graph f(std::string{"guard_"} + std::string{Ntype::get_name(op)} + (update_true ? "_t" : "_f"), 4, 8);
      build_guard_chain(f, op, update_true);
      std::vector<int64_t> expected;
      for (uint64_t mask = 0; mask < 16; ++mask) {
        for (const auto& [av, bv] : {std::pair{0x5a, 0x21}, std::pair{0xff, 0x0f}, std::pair{0x00, 0x7f}}) {
          auto values = f.inputs(mask, av, bv);
          expected.push_back(mux_eval(f.output(), values));
        }
      }
      optimize_lanes(f.graph, Cprop::whole_graph());
      size_t i = 0;
      for (uint64_t mask = 0; mask < 16; ++mask) {
        for (const auto& [av, bv] : {std::pair{0x5a, 0x21}, std::pair{0xff, 0x0f}, std::pair{0x00, 0x7f}}) {
          auto values = f.inputs(mask, av, bv);
          EXPECT_EQ(mux_eval(f.output(), values), expected[i++]) << mask;
        }
      }
    }
  }
}

// Outside a guard scope -- straight-line code, a hand-unrolled run like
// br_enc_onehot2bin -- the chain keeps every Mux on the accumulator: the
// rolled-loop rewrite must not change what loop-free code compiles to. A
// partial scope (synthesis' loop cleanup admits only the cells an expansion
// minted) rewrites exactly the links whose Mux and update cell it admits.
TEST_P(Aeg_parity, CpropMux_GuardedAccumulationOnlyInsideTheGuardScope) {
  for (const auto op : {Ntype_op::Or, Ntype_op::And, Ntype_op::Xor}) {
    SCOPED_TRACE(std::string{Ntype::get_name(op)});
    {
      Mux_graph f(std::string{"guard_off_"} + std::string{Ntype::get_name(op)}, 4, 8);
      build_guard_chain(f, op, true);
      std::vector<int64_t> expected_off;
      for (uint64_t mask = 0; mask < 16; ++mask) {
        auto values = f.inputs(mask, 0x5a, 0x21);
        expected_off.push_back(mux_eval(f.output(), values));
      }
      optimize_lanes(f.graph);
      for (uint64_t mask = 0; mask < 16; ++mask) {
        auto values = f.inputs(mask, 0x5a, 0x21);
        EXPECT_EQ(mux_eval(f.output(), values), expected_off[mask]) << mask;
      }
    }
    {
      Mux_graph f(std::string{"guard_partial_"} + std::string{Ntype::get_name(op)}, 4, 8);
      auto      chain = build_guard_chain(f, op, true);
      // Admit the last link only: its Mux and its update cell. A link whose
      // update cell is outside the scope stays a Mux even if its Mux is in.
      absl::flat_hash_set<hhds::Node_class> admitted{chain.muxes[3], chain.updates[3], chain.muxes[1]};
      std::vector<int64_t>                  expected;
      for (uint64_t mask = 0; mask < 16; ++mask) {
        auto values = f.inputs(mask, 0x5a, 0x21);
        expected.push_back(mux_eval(f.output(), values));
      }
      Cprop cprop{true};
      cprop.set_guard_scope([&](const hhds::Node_class& n) { return admitted.contains(n); });
      run(cprop, f.graph);
      Bitwidth{10}.do_trans(f.graph);
      for (uint64_t mask = 0; mask < 16; ++mask) {
        auto values = f.inputs(mask, 0x5a, 0x21);
        EXPECT_EQ(mux_eval(f.output(), values), expected[mask]) << mask;
      }
    }
  }
}

// `w = s ? a : a ^ 9` twice (sim_loop_mux_arm's two copies of the low
// nibble): the update cell has two consumers until CSE merges the muxes, so
// the guard can only appear in a later sweep. Bitwidth_rewrite runs scalar_mux
// after inference converged; a guard minted THERE kept no width and the
// emitted Verilog declared the constant-arm mux(s, 9, 0) one bit wide. Every
// value cell must end stamped, and both outputs keep their values.
TEST_P(Aeg_parity, CpropMux_GuardedAccumulationNeverLeavesAnUnstampedCell) {
  Mux_graph f("guard_const_term", 1, 4);
  auto      upd = f.node(Ntype_op::Xor);
  gu::setup_sink_pid(upd, 0).connect_driver(f.a);
  gu::setup_sink_pid(upd, 0).connect_driver(f.constant(9));
  f.mux(f.controls[0], upd.create_driver_pin(0), f.a).connect_sink(f.graph->get_output_pin("out"));
  f.mux(f.controls[0], upd.create_driver_pin(0), f.a).connect_sink(f.graph->get_output_pin("observe"));
  // One round, as the compile schedule's last: CSE merges the muxes in this
  // sweep, so the next scalar_mux to see them is Bitwidth_rewrite's.
  Cprop cprop{true};
  cprop.set_guard_scope(Cprop::whole_graph());
  run(cprop, f.graph);
  Bitwidth{10}.do_trans(f.graph);
  for (auto node : f.graph->body().nodes()) {
    if (!gu::is_builtin_node(node)) {
      EXPECT_GT(gu::bits_of(node.get_driver_pin(0)), 0) << gu::debug_name(node);
    }
  }
  for (uint64_t mask = 0; mask < 2; ++mask) {
    for (int64_t av = 0; av < 16; ++av) {
      auto values = f.inputs(mask, av, 0);
      EXPECT_EQ(mux_eval(f.output(), values), mask ? av : (av ^ 9));
      auto values2 = f.inputs(mask, av, 0);
      EXPECT_EQ(mux_eval(f.observed(), values2), mask ? av : (av ^ 9));
    }
  }
}

INSTANTIATE_TEST_SUITE_P(Engines,
                         Aeg_parity,
                         testing::Values(Engine::classic, Engine::aegraph),
                         [](const testing::TestParamInfo<Engine>& param) {
                           return param.param == Engine::classic ? "classic" : "aegraph";
                         });

}  // namespace

// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "phase_sched.hpp"

#include <string>

#include "cell.hpp"
#include "graph_library_singleton.hpp"
#include "gtest/gtest.h"
#include "hlop/dlop.hpp"
#include "node_util.hpp"
#include "query.hpp"

namespace gu  = livehd::graph_util;
namespace lec = livehd::lec;

namespace {

enum class Reader { Fall, Rise, GatedRise, GatedHighLatch, Output, OtherClock, ClockInput, DataLatch };

lec::Phase_plan preview_plan(const std::string& tag, Reader reader, bool data_enable = false, bool high_window = false) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_phase_preview_" + tag);
  auto  io  = lib.create_io("top");
  io->add_input("clk", 1);
  io->add_input("en", 2);
  io->add_input("d", 3);
  io->add_input("other_clk", 4);
  io->add_output("q", 5);
  for (const char* name : {"clk", "en", "d", "other_clk", "q"}) {
    io->set_bits(name, 1);
    io->set_unsign(name, true);
  }
  auto graph = io->create_graph();
  auto gate  = gu::create_typed_node(*graph, Ntype_op::Clock_cell);
  graph->get_input_pin("clk").connect_sink(gu::setup_sink_by_name(gate, "clk_ref"));
  graph->get_input_pin("en").connect_sink(gu::setup_sink_by_name(gate, "en"));
  gu::create_const(*graph, *Dlop::create_integer(1)).connect_sink(gu::setup_sink_by_name(gate, "div"));
  auto gated = gate.create_driver_pin(0);
  gu::set_ubits(gated, 1);
  auto inverted = gu::create_typed_node(*graph, Ntype_op::Not);
  gated.connect_sink(inverted.create_sink_pin(0));
  auto window = inverted.create_driver_pin(0);
  gu::set_ubits(window, 1);
  if (high_window) {
    window = gated;
  }
  if (data_enable) {
    auto qualified = gu::create_typed_node(*graph, Ntype_op::And);
    window.connect_sink(qualified.create_sink_pin(0));
    graph->get_input_pin("en").connect_sink(qualified.create_sink_pin(1));
    window = qualified.create_driver_pin(0);
    gu::set_ubits(window, 1);
  }
  auto latch = gu::create_typed_node(*graph, Ntype_op::Latch);
  latch.attr(hhds::attrs::name).set("preview");
  window.connect_sink(gu::setup_sink_by_name(latch, "enable"));
  graph->get_input_pin("d").connect_sink(gu::setup_sink_by_name(latch, "din"));
  auto value = latch.create_driver_pin(0);
  gu::set_ubits(value, 1);
  if (reader == Reader::Output) {
    value.connect_sink(graph->get_output_pin("q"));
  } else if (reader == Reader::DataLatch || reader == Reader::GatedHighLatch) {
    auto       sink   = gu::create_typed_node(*graph, Ntype_op::Latch);
    const auto enable = reader == Reader::GatedHighLatch ? gated : graph->get_input_pin("en");
    enable.connect_sink(gu::setup_sink_by_name(sink, "enable"));
    value.connect_sink(gu::setup_sink_by_name(sink, "din"));
    auto out = sink.create_driver_pin(0);
    gu::set_ubits(out, 1);
    out.connect_sink(graph->get_output_pin("q"));
  } else {
    auto       sink  = gu::create_typed_node(*graph, Ntype_op::Flop);
    const auto clock = reader == Reader::OtherClock   ? graph->get_input_pin("other_clk")
                       : reader == Reader::ClockInput ? value
                       : reader == Reader::GatedRise  ? gated
                                                      : graph->get_input_pin("clk");
    clock.connect_sink(gu::setup_sink_by_name(sink, "clock_pin"));
    const auto data = reader == Reader::ClockInput ? graph->get_input_pin("d") : value;
    data.connect_sink(gu::setup_sink_by_name(sink, "din"));
    gu::create_const(*graph, *Dlop::create_integer(reader == Reader::Rise || reader == Reader::GatedRise ? 1 : 0))
        .connect_sink(gu::setup_sink_by_name(sink, "posclk"));
    auto out = sink.create_driver_pin(0);
    gu::set_ubits(out, 1);
    out.connect_sink(graph->get_output_pin("q"));
  }
  return lec::plan_phases(graph.get());
}

lec::Phase_plan memory_plan(const std::string& tag, bool clocked, bool other_clock = false, bool comb = false) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_phase_memory_" + tag);
  auto  io  = lib.create_io("top");
  io->add_input("clk", 1);
  io->add_input("other_clk", 2);
  io->add_input("d", 3);
  io->add_output("q", 4);
  io->add_output("mem_q", 5);
  for (const char* name : {"clk", "other_clk", "d", "q", "mem_q"}) {
    io->set_bits(name, 1);
    io->set_unsign(name, true);
  }
  auto graph = io->create_graph();
  auto mem   = gu::create_typed_node(*graph, Ntype_op::Memory);
  gu::create_const(*graph, *Dlop::create_integer(comb ? Ntype::Memory_type_array : Ntype::Memory_type_async))
      .connect_sink(gu::setup_sink_by_name(mem, "type"));
  const auto ck_pid = Ntype::get_sink_pid(Ntype_op::Memory, "clock_pin");
  if (clocked) {
    // Port zero is an asynchronous read; the first write is port one.
    graph->get_input_pin("clk").connect_sink(mem.create_sink_pin(Ntype::Memory_port_stride + ck_pid));
  }
  if (other_clock) {
    graph->get_input_pin("other_clk").connect_sink(mem.create_sink_pin(2 * Ntype::Memory_port_stride + ck_pid));
  }
  auto value = mem.create_driver_pin(0);
  gu::set_ubits(value, 1);
  value.connect_sink(graph->get_output_pin("mem_q"));

  auto fall = gu::create_typed_node(*graph, Ntype_op::Flop);
  graph->get_input_pin("clk").connect_sink(gu::setup_sink_by_name(fall, "clock_pin"));
  graph->get_input_pin("d").connect_sink(gu::setup_sink_by_name(fall, "din"));
  gu::create_const(*graph, *Dlop::create_integer(0)).connect_sink(gu::setup_sink_by_name(fall, "posclk"));
  auto q = fall.create_driver_pin(0);
  gu::set_ubits(q, 1);
  q.connect_sink(graph->get_output_pin("q"));
  return lec::plan_phases(graph.get());
}

}  // namespace

TEST(PhaseSchedule, AsyncReadMemoryUsesItsWriteClock) {
  const auto plan = memory_plan("async_read", true);
  ASSERT_TRUE(plan.ok) << plan.error;
  EXPECT_TRUE(plan.multi);
  EXPECT_EQ(plan.n_roots, 1);
  EXPECT_EQ(plan.root_clock, "clk");
  EXPECT_EQ(plan.n_mem_rise, 1);
}

TEST(PhaseSchedule, AsyncReadMemoryKeepsIndependentWriteClocks) {
  const auto plan = memory_plan("independent_writes", true, true);
  EXPECT_FALSE(plan.ok);
  EXPECT_EQ(plan.n_roots, 2);
}

TEST(PhaseSchedule, UnclockedSequentialMemoryKeepsItsImplicitClock) {
  const auto plan = memory_plan("implicit", false);
  EXPECT_FALSE(plan.ok);
  EXPECT_EQ(plan.n_roots, 2);
}

TEST(PhaseSchedule, CombinationalMemoryDoesNotIntroduceAClock) {
  const auto plan = memory_plan("comb", false, false, true);
  ASSERT_TRUE(plan.ok) << plan.error;
  EXPECT_EQ(plan.n_roots, 1);
  EXPECT_EQ(plan.n_mem_rise, 0);
  EXPECT_EQ(plan.n_mem_fall, 0);
}

TEST(PhaseSchedule, GatedOffPreviewForwardsSettledDataAtFall) {
  const auto plan = preview_plan("fall", Reader::Fall);
  ASSERT_TRUE(plan.ok) << plan.error;
  EXPECT_TRUE(plan.multi);
  EXPECT_EQ(plan.n_latch_low, 1);
  EXPECT_EQ(plan.n_latch_off_read, 1);
  int previews = 0;
  for (const auto& [key, endpoint] : plan.ep) {
    if (endpoint.forward_off_at_fall) {
      ++previews;
      EXPECT_EQ(endpoint.phase, lec::Phase::Close_low);
      EXPECT_EQ(endpoint.guard_sample, lec::Phase::Close_low);
      EXPECT_FALSE(endpoint.live_guard);
      EXPECT_FALSE(endpoint.guard_key.empty());
    }
  }
  EXPECT_EQ(previews, 1);
}

TEST(PhaseSchedule, GatedOffPreviewDoesNotAdmitOtherObservations) {
  for (const auto& [tag, reader] : {
           std::pair{       "rise",       Reader::Rise},
           {     "output",     Reader::Output},
           {"other_clock", Reader::OtherClock},
           {"clock_input", Reader::ClockInput},
           { "data_latch",  Reader::DataLatch}
  }) {
    const auto plan = preview_plan(tag, reader);
    EXPECT_FALSE(plan.ok) << tag;
    EXPECT_EQ(plan.n_latch_off_read, 0) << tag;
  }
}

TEST(PhaseSchedule, GatedOffPreviewDoesNotDiscardADataWindowQualifier) {
  const auto plan = preview_plan("qualified", Reader::Fall, true);
  EXPECT_FALSE(plan.ok);
  EXPECT_EQ(plan.n_latch_off_read, 0);
}

TEST(PhaseSchedule, DataQualifiedGatedWindowKeepsTheClockPhase) {
  const auto plan = preview_plan("qualified_masked", Reader::GatedHighLatch, true);
  ASSERT_TRUE(plan.ok) << plan.error;
  EXPECT_EQ(plan.n_latch_low, 1);
  EXPECT_EQ(plan.n_latch_off_read, 0);
  EXPECT_EQ(plan.n_guard_low, 1);
  EXPECT_EQ(plan.n_latch_window_guards, 1);
  bool saw_qualified = false;
  for (const auto& [key, endpoint] : plan.ep) {
    if (!endpoint.window_guard_key.empty()) {
      saw_qualified = true;
      EXPECT_TRUE(endpoint.clock_role_latch);
      EXPECT_FALSE(endpoint.live_guard);
      EXPECT_FALSE(endpoint.guard_key.empty());
      EXPECT_NE(endpoint.guard_key, endpoint.window_guard_key);
      EXPECT_EQ(endpoint.phase, lec::Phase::Close_low);
    }
  }
  EXPECT_TRUE(saw_qualified);
}

TEST(PhaseSchedule, DataQualifiedWindowStillRefusesSameEdgeReaders) {
  const auto plan = preview_plan("qualified_same_edge", Reader::GatedRise, true);
  EXPECT_FALSE(plan.ok);
}

TEST(PhaseSchedule, DataQualifiedHighWindowKeepsBothGuards) {
  const auto plan = preview_plan("qualified_high", Reader::Fall, true, true);
  ASSERT_TRUE(plan.ok) << plan.error;
  EXPECT_EQ(plan.n_latch_high, 1);
  EXPECT_EQ(plan.n_latch_window_guards, 1);
  bool saw_qualified = false;
  for (const auto& [key, endpoint] : plan.ep) {
    if (endpoint.clock_role_latch) {
      saw_qualified = true;
      EXPECT_EQ(endpoint.phase, lec::Phase::Close_high);
      EXPECT_FALSE(endpoint.live_guard);
      EXPECT_FALSE(endpoint.guard_key.empty());
      EXPECT_FALSE(endpoint.window_guard_key.empty());
    }
  }
  EXPECT_TRUE(saw_qualified);
}

namespace {

std::shared_ptr<hhds::Graph> deep_guard_design(hhds::GraphLibrary& lib, const std::string& name, bool clock_gates,
                                               bool wrong_enable = false) {
  auto io   = lib.create_io(name);
  int  port = 1;
  for (const char* input : {"clk", "en0", "en1", "data"}) {
    io->add_input(input, port++);
    io->set_bits(input, 1);
    io->set_unsign(input, true);
  }
  for (const char* output : {"q0", "q1"}) {
    io->add_output(output, port++);
    io->set_bits(output, 1);
    io->set_unsign(output, true);
  }
  auto graph = io->create_graph();
  for (int index = 0; index < 2; ++index) {
    auto enable = graph->get_input_pin(index == 0 || wrong_enable ? "en0" : "en1");
    // Ten inversions preserve the input, but exceed the old structural key's
    // eight-level cutoff. Distinct enables must remain distinct clock guards.
    if (clock_gates) {
      for (int depth = 0; depth < 10; ++depth) {
        auto inv = gu::create_typed_node(*graph, Ntype_op::Not);
        enable.connect_sink(inv.create_sink_pin(0));
        enable = inv.create_driver_pin(0);
        gu::set_ubits(enable, 1);
      }
    }
    auto flop = gu::create_typed_node(*graph, Ntype_op::Flop);
    flop.set_name("state" + std::to_string(index));
    auto clock = graph->get_input_pin("clk");
    if (clock_gates) {
      auto gate = gu::create_typed_node(*graph, Ntype_op::Clock_cell);
      clock.connect_sink(gu::setup_sink_by_name(gate, "clk_ref"));
      enable.connect_sink(gu::setup_sink_by_name(gate, "en"));
      gu::create_const(*graph, *Dlop::create_integer(1)).connect_sink(gu::setup_sink_by_name(gate, "div"));
      clock = gate.create_driver_pin(0);
      gu::set_ubits(clock, 1);
    } else {
      enable.connect_sink(gu::setup_sink_by_name(flop, "enable"));
    }
    clock.connect_sink(gu::setup_sink_by_name(flop, "clock_pin"));
    graph->get_input_pin("data").connect_sink(gu::setup_sink_by_name(flop, "din"));
    gu::create_const(*graph, *Dlop::create_integer(0)).connect_sink(gu::setup_sink_by_name(flop, "initial"));
    auto q = flop.create_driver_pin(0);
    gu::set_ubits(q, 1);
    q.connect_sink(graph->get_output_pin("q" + std::to_string(index)));
  }
  return graph;
}

}  // namespace

TEST(PhaseSchedule, DeepIndependentEnablesKeepDistinctGuards) {
  hhds::GraphLibrary lib;
  auto               graph = deep_guard_design(lib, "deep_guards", true);
  const auto         plan  = lec::plan_phases(graph.get());
  ASSERT_TRUE(plan.ok) << plan.error;
  EXPECT_EQ(plan.n_guards, 2);
  EXPECT_EQ(plan.guard_cones.size(), 2);
}

TEST(PhaseSchedule, DeepClockGatesMatchIndependentRegisterEnables) {
  hhds::GraphLibrary lib;
  auto               ref  = deep_guard_design(lib, "reference", false);
  auto               same = deep_guard_design(lib, "gated", true);
  auto               bad  = deep_guard_design(lib, "wrong_gate", true, true);
  lec::Lec_options   options;
  options.engine  = "bmc";
  options.phase   = "free_toreset";
  options.bound   = 3;
  const auto good = lec::prove_equal(ref.get(), same.get(), options);
  EXPECT_EQ(good.verdict, lec::Verdict::Proven) << good.detail << " " << good.witness;
  const auto wrong = lec::prove_equal(ref.get(), bad.get(), options);
  EXPECT_EQ(wrong.verdict, lec::Verdict::Refuted) << wrong.detail;
}

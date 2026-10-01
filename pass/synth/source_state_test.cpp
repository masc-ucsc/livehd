// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "source_state.hpp"

#include <array>
#include <iostream>

#include "attrs.hpp"
#include "design_prepare.hpp"
#include "gtest/gtest.h"
#include "hhds/attrs/srcid.hpp"
#include "node_util.hpp"
#include "region_blast.hpp"
#include "region_writer.hpp"

namespace livehd::synth {
namespace {
namespace gu = graph_util;

class SourceState : public ::testing::Test {
protected:
  hhds::GraphLibrary            library;
  std::shared_ptr<hhds::Graph>  graph;
  std::vector<hhds::Node_class> nodes;
  partition::Region_body        rb;

  void SetUp() override {
    diag::sink().clear();
    diag::sink().set_jsonl_path("off");
    diag::sink().set_human_stderr(false);
    auto io = library.create_io("source");
    for (const auto& name : {"clk", "a", "en", "rst"}) {
      io->add_input(name, static_cast<hhds::Port_id>(rb.inputs.size() + 1));
      io->set_bits(name, name == std::string_view{"a"} ? 2 : 1);
      io->set_unsign(name, true);
      rb.inputs.push_back({name, {}, name == std::string_view{"a"} ? 2 : 1, false});
    }
    io->add_output("out", 5);
    io->set_bits("out", 2);
    graph = io->create_graph();
    for (auto& p : rb.inputs) {
      p.src_driver = graph->get_input_pin(p.name);
      gu::set_ubits(p.src_driver, p.bits);
    }
    rb.src         = graph.get();
    rb.module_name = "test_region";
    auto body_io   = library.create_io("region");
    for (size_t i = 0; i < rb.inputs.size(); ++i) {
      body_io->add_input(rb.inputs[i].name, i + 1);
    }
    body_io->add_output("out", 5);
    rb.body = body_io->create_graph().get();
  }
  void TearDown() override {
    if (HasFailure()) {
      for (const auto& d : diag::sink().records()) {
        std::cerr << d.code << ": " << d.message << '\n';
      }
    }
    diag::sink().clear();
  }
  hhds::Pin_class in(std::string_view name) { return graph->get_input_pin(name); }
  void            connect(hhds::Node_class n, std::string_view name, hhds::Pin_class driver) {
    driver.connect_sink(gu::setup_sink_pid(n, Ntype::get_sink_pid(gu::type_op_of(n), name)));
  }
  void constant(hhds::Node_class n, std::string_view name, int value) {
    connect(n, name, gu::create_const(*graph, *Dlop::create_integer(value)));
  }
  hhds::Node_class node(Ntype_op op, std::string_view name, int bits = 1) {
    auto n = gu::create_typed_node(*graph, op);
    auto q = n.create_driver_pin(0);
    gu::set_ubits(q, bits);
    gu::set_pin_name(q, name);
    nodes.push_back(n);
    return n;
  }
  hhds::Node_class flop(std::string_view name, int bits = 1) {
    auto n = node(Ntype_op::Flop, name, bits);
    connect(n, "din", in("a"));
    connect(n, "clock_pin", in("clk"));
    return n;
  }
  hhds::Node_class latch(std::string_view name, bool low) {
    auto n = node(Ntype_op::Latch, name);
    connect(n, "din", in("en"));
    connect(n, "enable", in("clk"));
    constant(n, "posclk", low ? 0 : 1);
    return n;
  }
  void ready(hhds::Pin_class output = {}) {
    rb.nodes = nodes;
    rb.outputs.clear();
    if (!output.is_invalid()) {
      // Region outputs originate in real edges. A detached synthetic Q is
      // absent from out_sorted_pins(), so it cannot describe a native boundary.
      auto sink      = graph->get_output_pin("out");
      bool connected = false;
      for (auto driver : sink.get_driver_pins()) {
        connected |= driver == output;
      }
      if (!connected) {
        output.connect_sink(sink);
      }
      rb.outputs.push_back({"out", output, gu::bits_of(output), false});
    }
  }
  Source_state_table inspect(State_scope scope = State_scope::logic) {
    ready();
    return inspect_source_state(rb, analyze_clock_gates(rb), scope);
  }
  Region_blast blast(Blast_options options = {}) {
    options.capture_state = true;
    return blast_region(rb, options, {});
  }
};

TEST_F(SourceState, SourceClassificationDoesNotDependOnRegisterMapping) {
  auto n = flop("r", 2);
  constant(n, "posclk", 0);
  constant(n, "initial", 2);
  ready(n.get_driver_pin(0));
  for (bool map : {false, true}) {
    Blast_options o;
    o.map_register    = map;
    const auto result = blast(o);
    ASSERT_EQ(result.status, Region_blast::Status::blasted);
    ASSERT_TRUE(result.source_state);
    ASSERT_EQ(result.source_state->sources.size(), 1U);
    const auto& s = result.source_state->sources.front();
    EXPECT_EQ(s.role, State_role::register_candidate);
    EXPECT_EQ(s.name, "r");
    EXPECT_EQ(s.bits, 2U);
    EXPECT_TRUE(s.width_known);
    EXPECT_TRUE(s.neg_clock);
    EXPECT_TRUE(s.power_on);
    EXPECT_EQ(s.translated_bits, map ? 2U : 0U);
    EXPECT_TRUE(validate_state_target(*result.source_state, State_target::cmos));
    EXPECT_FALSE(validate_state_target(*result.source_state, State_target::domino));
    diag::sink().clear();
  }
}

TEST_F(SourceState, ConstantRegionOutputsDoNotMaterializeOrdinaryDriverPins) {
  for (bool logical : {false, true}) {
    for (int value : {0, 1}) {
      ready(gu::create_const(*graph, *Dlop::create_integer(value)));
      Blast_options options;
      options.logical_state = logical;
      Region_blast result;
      ASSERT_NO_THROW(result = blast(options));
      ASSERT_EQ(result.status, Region_blast::Status::blasted);
      ASSERT_FALSE(result.lnet.outputs().empty());
      const auto out = result.lnet.outputs()[0].node;
      EXPECT_EQ(result.lnet.kind(out), Lnet::Kind::constant);
      EXPECT_EQ(result.lnet.eval(out, 0), bool(value));
    }
  }
}

TEST_F(SourceState, LogicalStateCrossesDerivedClockAndDynamicAsyncResetWithoutCells) {
  auto clk = node(Ntype_op::And, "derived_clock");
  in("clk").connect_sink(gu::setup_sink_pid(clk, 0));
  in("en").connect_sink(gu::setup_sink_pid(clk, 0));
  auto reset = node(Ntype_op::Or, "computed_reset");
  in("rst").connect_sink(gu::setup_sink_pid(reset, 0));
  in("en").connect_sink(gu::setup_sink_pid(reset, 0));
  auto n = node(Ntype_op::Flop, "r", 2);
  connect(n, "din", in("a"));
  connect(n, "clock_pin", clk.get_driver_pin(0));
  connect(n, "enable", in("en"));
  connect(n, "reset_pin", reset.get_driver_pin(0));
  connect(n, "initial", in("a"));
  constant(n, "async", 1);
  constant(n, "negreset", 1);
  constant(n, "pipe_min", 2);
  ready(n.get_driver_pin(0));
  for (bool map : {false, true}) {
    Blast_options o;
    o.logical_state  = true;
    o.state_target   = State_target::domino;
    o.map_register   = map;
    o.qn_encode      = true;  // deliberately contradictory mapping knobs
    o.icg            = map;
    o.areset_cell[0] = map ? 1 : -1;
    o.areset_cell[1] = map ? 1 : -1;
    auto result      = blast_region(rb, o, {});  // logical mode implies capture_state
    ASSERT_EQ(result.status, Region_blast::Status::blasted);
    ASSERT_TRUE(result.logical_state);
    ASSERT_TRUE(result.source_state);
    const auto& table = *result.source_state;
    EXPECT_TRUE(table.logical_boundary);
    ASSERT_EQ(table.bits.size(), 4U);
    ASSERT_EQ(table.controls.size(), 3U);
    ASSERT_EQ(result.flops.size(), 2U);
    EXPECT_TRUE(result.bboxes.empty());
    EXPECT_EQ(result.arst_pos, 0U);  // no Liberty reset-level encoding
    EXPECT_TRUE(validate_state_controls(table, result.lnet));
    for (const auto& f : result.flops) {
      EXPECT_TRUE(f.async_reset);
      EXPECT_FALSE(f.d_inverted);
    }
    for (uint32_t x = 0; x < 512; ++x) {
      std::vector<bool> values(result.lnet.size());
      for (Lid id = 0; id < result.lnet.size(); ++id) {
        if (result.lnet.kind(id) == Lnet::Kind::source) {
          if (result.lnet.is_latch_source(id)) {
            values[id] = (x >> (5 + result.lnet.source_index(id))) & 1;
          } else {
            const auto&    name = result.lnet.inputs()[result.lnet.source_index(id)].name;
            const unsigned bit  = name == "a_b0" ? 0 : name == "a_b1" ? 1 : name == "en_b0" ? 2 : name == "rst_b0" ? 3 : 4;
            ASSERT_TRUE(name == "a_b0" || name == "a_b1" || name == "en_b0" || name == "rst_b0" || name == "clk_b0");
            values[id] = (x >> bit) & 1;
          }
        } else {
          uint32_t arg = 0;
          for (uint32_t bit = 0; bit < result.lnet.fanin_count(id); ++bit) {
            arg |= uint32_t{values[result.lnet.fanin(id, bit)]} << bit;
          }
          values[id] = result.lnet.eval(id, arg);
        }
      }
      for (const auto& bit : table.bits) {
        const bool q    = (x >> (5 + bit.stage * 2 + bit.bit)) & 1;
        const bool data = (x >> (bit.stage == 0 ? bit.bit : 5 + bit.bit)) & 1;
        EXPECT_EQ(values[bit.d.node], (x & 4) ? data : q);  // async reset never enters D
      }
      for (const auto& control : table.controls) {
        for (uint32_t b = 0; b < control.outputs.size(); ++b) {
          const bool expected = control.kind == State_control_kind::clock         ? bool(x & 16) && bool(x & 4)
                                : control.kind == State_control_kind::async_reset ? bool(x & 8) || bool(x & 4)
                                                                                  : bool((x >> b) & 1);
          EXPECT_EQ(values[result.lnet.outputs()[control.outputs[b]].node], expected);
        }
      }
    }
  }
}

TEST_F(SourceState, LogicalStateKeepsRecognizedIcgNativeAndSharesClockOutputs) {
  auto l    = latch("gate_latch", true);
  auto gate = node(Ntype_op::And, "gclk");
  in("clk").connect_sink(gu::setup_sink_pid(gate, 0));
  l.get_driver_pin(0).connect_sink(gu::setup_sink_pid(gate, 0));
  for (auto name : {"r0", "r1"}) {
    auto n = node(Ntype_op::Flop, name);
    connect(n, "din", in("a"));
    connect(n, "clock_pin", gate.get_driver_pin(0));
  }
  ready(nodes.back().get_driver_pin(0));
  Blast_options o;
  o.logical_state    = true;
  o.state_target     = State_target::domino;
  o.icg              = true;
  o.latch_cell[1][0] = 1;
  auto result        = blast_region(rb, o, {});
  ASSERT_EQ(result.status, Region_blast::Status::blasted);
  ASSERT_TRUE(result.source_state);
  ASSERT_EQ(result.bboxes.size(), 1U);
  EXPECT_EQ(result.bboxes[0].op, Ntype_op::Latch);
  EXPECT_FALSE(result.bboxes[0].latch.map);
  EXPECT_TRUE(result.icgs.empty());
  EXPECT_EQ(result.source_state->clocks.size(), 1U);
  ASSERT_EQ(result.source_state->controls.size(), 2U);
  EXPECT_EQ(result.source_state->controls[0].outputs, result.source_state->controls[1].outputs);
  EXPECT_EQ(result.source_state->sources[0].role, State_role::icg);
  EXPECT_EQ(result.source_state->bits.size(), 2U);
}

TEST_F(SourceState, LogicalTargetValidationPrecedesTranslationAndKeepsCmosState) {
  auto n = flop("negative");
  constant(n, "posclk", 0);
  n.attr(hhds::attrs::srcid).set(graph->source_locator().mint_line("source.v", 7));
  latch("data_latch", false);
  ready(n.get_driver_pin(0));
  Blast_options o;
  o.logical_state = true;
  o.map_register  = false;
  auto cmos       = blast_region(rb, o, {});
  ASSERT_EQ(cmos.status, Region_blast::Status::blasted);
  ASSERT_EQ(cmos.flops.size(), 1U);
  EXPECT_TRUE(cmos.flops[0].neg_clock);
  EXPECT_EQ(cmos.bboxes.size(), 1U);
  o.state_target = State_target::domino;
  auto domino    = blast_region(rb, o, {});
  EXPECT_EQ(domino.status, Region_blast::Status::refused);
  EXPECT_TRUE(domino.lnet.latches().empty());
  ASSERT_EQ(diag::sink().records().size(), 2U);
  EXPECT_EQ(diag::sink().records()[0].span.file, "source.v");
  EXPECT_EQ(diag::sink().records()[0].span.start_line, 7U);
}

TEST_F(SourceState, LogicalStatePreservesSpecialScopesAndHonorsAdmission) {
  auto n = flop("r", 2);
  ready(n.get_driver_pin(0));
  Blast_options o;
  o.logical_state = true;
  for (auto scope : {State_scope::memory, State_scope::opaque}) {
    o.state_scope = scope;
    auto result   = blast_region(rb, o, {});
    ASSERT_EQ(result.status, Region_blast::Status::blasted);
    EXPECT_TRUE(result.flops.empty());
    ASSERT_TRUE(result.source_state);
    EXPECT_TRUE(result.source_state->bits.empty());
    EXPECT_TRUE(result.source_state->controls.empty());
    EXPECT_EQ(result.bboxes.size(), 1U);
  }
  o.state_scope       = State_scope::logic;
  o.logical_max_nodes = 16;
  constant(n, "pipe_min", 1000);
  auto large = blast_region(rb, o, {});
  EXPECT_EQ(large.status, Region_blast::Status::over_budget);
  EXPECT_TRUE(large.lnet.latches().empty());  // refused before allocating pipeline bits
}

TEST_F(SourceState, LogicalControlsAreValidatedAndCannotUseMappedWriter) {
  auto n = flop("r");
  ready(n.get_driver_pin(0));
  Blast_options o;
  o.logical_state = true;
  auto result     = blast_region(rb, o, {});
  ASSERT_EQ(result.status, Region_blast::Status::blasted);
  ASSERT_TRUE(result.source_state);
  const auto table = *result.source_state;
  ASSERT_EQ(table.controls.size(), 1U);
  for (int mutation = 0; mutation < 5; ++mutation) {
    auto bad = table;
    switch (mutation) {
      case 0: bad.controls.clear(); break;
      case 1: bad.controls.push_back(bad.controls[0]); break;
      case 2: bad.controls[0].source = 99; break;
      case 3: bad.controls[0].outputs[0] = 999; break;
      case 4: bad.controls[0].outputs.clear(); break;
    }
    EXPECT_FALSE(validate_state_controls(bad, result.lnet));
  }
  Region_writer         writer;
  Region_writer::Counts counts;
  EXPECT_FALSE(writer.write(rb, result, {}, {}, {}, counts, {}));
  ASSERT_FALSE(diag::sink().records().empty());
  EXPECT_EQ(diag::sink().records().back().code, "logical-state-writer");
}

TEST_F(SourceState, LogicalMultiBitResetAndInitializationRemainControlConsumers) {
  auto n = flop("r", 2);
  connect(n, "reset_pin", in("a"));  // multi-bit reset, not a Liberty pin
  constant(n, "initial", 2);
  constant(n, "async", 1);
  constant(n, "negreset", 1);
  auto initialized = flop("initial_function", 2);
  connect(initialized, "initial", in("a"));
  ready(n.get_driver_pin(0));
  Blast_options o;
  o.logical_state = true;
  o.map_register  = false;
  auto result     = blast_region(rb, o, {});
  ASSERT_EQ(result.status, Region_blast::Status::blasted);
  ASSERT_TRUE(result.source_state);
  const auto& table = *result.source_state;
  ASSERT_EQ(table.sources.size(), 2U);
  EXPECT_TRUE(table.sources[0].neg_reset);
  EXPECT_EQ(table.sources[0].reset.bits, 2U);
  EXPECT_TRUE(table.sources[0].initial.constant);
  EXPECT_TRUE(table.sources[0].initial.value.bit_test(1));
  EXPECT_TRUE(table.sources[1].power_on);
  ASSERT_EQ(table.controls.size(), 4U);
  EXPECT_EQ(table.controls[1].kind, State_control_kind::async_reset);
  EXPECT_EQ(table.controls[1].outputs.size(), 2U);
  EXPECT_EQ(table.controls[3].kind, State_control_kind::initial_value);
  EXPECT_EQ(table.controls[3].outputs, table.controls[1].outputs);  // shared raw bits, distinct semantics
}

TEST_F(SourceState, LogicalAdmissionChecksStateBeforeAllocation) {
  auto n = flop("r", 2);
  ready(n.get_driver_pin(0));
  Blast_options o;
  o.logical_state   = true;
  uint32_t    calls = 0;
  Blast_hooks hooks;
  hooks.over_budget = [&](uint64_t, size_t, size_t, size_t projected) {
    ++calls;
    EXPECT_GE(projected, 3U);
    return true;
  };
  auto refused = blast_region(rb, o, hooks);
  EXPECT_EQ(refused.status, Region_blast::Status::over_budget);
  EXPECT_EQ(calls, 1U);
  EXPECT_TRUE(refused.lnet.latches().empty());
  connect(n, "pipe_min", gu::create_const(*graph, *Dlop::create_integer(int64_t{1} << 32)));
  auto huge = blast_region(rb, o, {});
  EXPECT_EQ(huge.status, Region_blast::Status::over_budget);
  EXPECT_TRUE(huge.lnet.latches().empty());
}

TEST_F(SourceState, SynchronousResetEnableAndQnEncodingHaveSemanticD) {
  auto n = flop("r", 2);
  connect(n, "enable", in("en"));
  connect(n, "reset_pin", in("rst"));
  constant(n, "initial", 2);
  constant(n, "negreset", 1);
  constant(n, "pipe_min", 2);
  ready(n.get_driver_pin(0));
  for (bool qn : {false, true}) {
    Blast_options o;
    o.qn_encode       = qn;
    const auto result = blast(o);
    ASSERT_EQ(result.status, Region_blast::Status::blasted);
    ASSERT_TRUE(result.source_state);
    const auto& table = *result.source_state;
    ASSERT_EQ(table.sources.size(), 1U);
    ASSERT_EQ(table.bits.size(), 4U);
    EXPECT_EQ(table.sources[0].stages, 2U);
    EXPECT_FALSE(table.sources[0].power_on);
    EXPECT_TRUE(table.sources[0].neg_reset);
    EXPECT_TRUE(table.sources[0].enable.present);
    EXPECT_EQ(table.bits[0].name, "___pipe0_r[0]");
    EXPECT_EQ(table.bits[3].name, "r[1]");
    const auto& net = result.lnet;
    for (uint32_t x = 0; x < 256; ++x) {
      std::vector<bool> values(net.size());
      for (Lid id = 0; id < net.size(); ++id) {
        if (net.kind(id) == Lnet::Kind::source) {
          if (net.is_latch_source(id)) {
            values[id] = (x >> (4 + net.source_index(id))) & 1;
          } else {
            const auto& name = net.inputs()[net.source_index(id)].name;
            if (name == "a_b0") {
              values[id] = x & 1;
            } else if (name == "a_b1") {
              values[id] = (x >> 1) & 1;
            } else if (name == "en_b0") {
              values[id] = (x >> 2) & 1;
            } else if (name == "rst_b0") {
              values[id] = (x >> 3) & 1;
            } else {
              ADD_FAILURE() << name;
            }
          }
        } else {
          uint32_t arg = 0;
          for (uint32_t i = 0; i < net.fanin_count(id); ++i) {
            arg |= uint32_t{values[net.fanin(id, i)]} << i;
          }
          values[id] = net.eval(id, arg);
        }
      }
      for (const auto& bit : table.bits) {
        const bool q        = (x >> (4 + bit.stage * 2 + bit.bit)) & 1;
        const bool data     = (x >> (bit.stage == 0 ? bit.bit : 4 + bit.bit)) & 1;
        const bool expected = (x & 8) == 0 ? bit.bit == 1 : (x & 4) ? data : q;
        EXPECT_EQ(values[bit.q.node] != bit.q.inverted, q);
        ASSERT_EQ(values[bit.d.node] != bit.d.inverted, expected) << x << ":" << bit.stage << ":" << bit.bit;
      }
    }
  }
}

TEST_F(SourceState, IcgRecognitionSurvivesMissingCellAndNativeState) {
  auto l    = latch("enable_storage", true);
  auto gate = node(Ntype_op::And, "clock_gate");
  in("clk").connect_sink(gu::setup_sink_pid(gate, 0));
  l.get_driver_pin(0).connect_sink(gu::setup_sink_pid(gate, 0));
  auto n = node(Ntype_op::Flop, "r");
  connect(n, "din", in("en"));
  connect(n, "clock_pin", gate.get_driver_pin(0));
  ready(n.get_driver_pin(0));
  const auto clocks = analyze_clock_gates(rb);
  ASSERT_EQ(clocks.gates.size(), 1U);
  EXPECT_EQ(clocks.gates[0].latch, l);
  for (bool map : {false, true}) {
    Blast_options o;
    o.map_register    = map;
    o.icg             = false;
    const auto result = blast(o);
    ASSERT_EQ(result.status, Region_blast::Status::blasted);
    ASSERT_TRUE(result.source_state);
    EXPECT_TRUE(result.icgs.empty());
    ASSERT_EQ(result.source_state->clocks.size(), 1U);
    EXPECT_EQ(result.source_state->clocks[0].latch_node, l.get_debug_nid());
    EXPECT_EQ(result.source_state->clocks[0].output.node, gate.get_debug_nid());
    EXPECT_EQ(result.source_state->clocks[0].parent, -1);
    ASSERT_EQ(result.source_state->sources.size(), 2U);
    EXPECT_EQ(result.source_state->sources[0].role, State_role::icg);
    EXPECT_EQ(result.source_state->sources[1].role, State_role::register_candidate);
    EXPECT_EQ(result.source_state->sources[1].icg, 0);
    EXPECT_TRUE(validate_state_target(*result.source_state, State_target::domino));
  }
}

TEST_F(SourceState, IcgCannotHideADataLatchOrWrongTransparency) {
  for (bool low : {false, true}) {
    auto l    = latch(low ? "data_use" : "wrong_phase", low);
    auto gate = node(Ntype_op::And, "gate");
    in("clk").connect_sink(gu::setup_sink_pid(gate, 0));
    l.get_driver_pin(0).connect_sink(gu::setup_sink_pid(gate, 0));
    // Even the correct clocking shape is not an ICG when held Q is data.
    if (low) {
      l.get_driver_pin(0).connect_sink(graph->get_output_pin("out"));
    }
  }
  const auto table = inspect();
  ASSERT_EQ(table.sources.size(), 2U);
  EXPECT_EQ(table.sources[0].role, State_role::transparent_latch);
  EXPECT_EQ(table.sources[1].role, State_role::transparent_latch);
  EXPECT_FALSE(validate_state_target(table, State_target::domino));
  EXPECT_EQ(diag::sink().count(diag::Severity::error), 2U);
  EXPECT_TRUE(validate_state_target(table, State_target::cmos));
}

TEST_F(SourceState, ResetEventsAndMemoryBarriersDoNotDependOnNames) {
  auto n = flop("memory_looking_name");
  connect(n, "reset_pin", in("rst"));
  constant(n, "initial", 1);
  constant(n, "async", 1);
  auto table = inspect();
  ASSERT_EQ(table.sources.size(), 1U);
  EXPECT_EQ(table.sources[0].role, State_role::register_candidate);
  EXPECT_TRUE(table.sources[0].async_reset);
  EXPECT_FALSE(table.sources[0].power_on);
  ready(n.get_driver_pin(0));
  const auto native = blast();  // no asynchronous DFF cell: still classified
  ASSERT_EQ(native.status, Region_blast::Status::blasted);
  ASSERT_TRUE(native.source_state);
  EXPECT_TRUE(native.source_state->bits.empty());
  EXPECT_EQ(native.source_state->sources[0].role, State_role::register_candidate);
  graph->get_input_node().attr(attrs::memory_module).set(1);
  table = inspect();
  EXPECT_EQ(table.scope, State_scope::memory);
  EXPECT_EQ(table.sources[0].role, State_role::memory);
  EXPECT_EQ(inspect(State_scope::opaque).sources[0].role, State_role::opaque);
}

TEST_F(SourceState, DiagnosticsUseCopiedSourceSpansAndNeverInventLines) {
  auto neg = flop("negative");
  constant(neg, "posclk", 0);
  const auto sid = graph->source_locator().mint_line("rtl/design.v", 37);
  neg.attr(hhds::attrs::srcid).set(sid);
  auto l = latch("locationless", false);
  (void)l;
  const std::array sources{graph};
  auto             prepared = prepare_design(sources, false, "pass.usyn");
  ASSERT_TRUE(prepared);
  std::vector<hhds::Node_class> copied_nodes;
  for (auto n : prepared->roots.front()->body().nodes()) {
    copied_nodes.push_back(n);
  }
  partition::Region_body copied;
  copied.src   = prepared->roots.front().get();
  copied.nodes = copied_nodes;
  auto table   = inspect_source_state(copied, {});
  EXPECT_FALSE(validate_state_target(table, State_target::domino));
  ASSERT_EQ(diag::sink().records().size(), 2U);
  for (const auto& d : diag::sink().records()) {
    if (d.code == "domino-negedge-clock") {
      EXPECT_EQ(d.span.file, "rtl/design.v");
      EXPECT_EQ(d.span.start_line, 37U);
    } else {
      EXPECT_EQ(d.code, "domino-source-latch");
      EXPECT_TRUE(d.span.is_null());
    }
  }
}

TEST_F(SourceState, DuplicateOrIncompleteBitCorrespondenceIsRefused) {
  auto n = flop("r", 2);
  ready(n.get_driver_pin(0));
  auto result = blast();
  ASSERT_TRUE(result.source_state);
  ASSERT_EQ(result.source_state->bits.size(), 2U);
  auto table               = *result.source_state;
  result.flops[0].latch[1] = result.flops[0].latch[0];
  EXPECT_FALSE(bind_source_state(table, result));
  EXPECT_EQ(table.bits.size(), 2U);  // no partially overwritten metadata
}

TEST_F(SourceState, PipelineStageIdentityDoesNotDependOnSnapshotOrder) {
  auto n = flop("r");
  constant(n, "pipe_min", 2);
  ready(n.get_driver_pin(0));
  auto result = blast();
  ASSERT_TRUE(result.source_state);
  ASSERT_EQ(result.flops.size(), 2U);
  std::swap(result.flops[0], result.flops[1]);
  auto table = *result.source_state;
  ASSERT_TRUE(bind_source_state(table, result));
  EXPECT_EQ(table.bits[0].stage, 1U);
  EXPECT_EQ(table.bits[0].name, "r");
  EXPECT_EQ(table.bits[1].stage, 0U);
  EXPECT_EQ(table.bits[1].name, "___pipe0_r");
  result.flops[1].stage = result.flops[0].stage;
  EXPECT_FALSE(bind_source_state(table, result));
}

TEST_F(SourceState, HierarchicalValidationVisitsSharedDefinitionsBeforePartitioning) {
  auto n = flop("hidden_negative");
  constant(n, "posclk", 0);
  n.attr(hhds::attrs::srcid).set(graph->source_locator().mint_line("rtl/child.v", 19));
  auto parent = library.create_io("top")->create_graph();
  for (int i = 0; i < 2; ++i) {
    auto sub = gu::create_typed_node(*parent, Ntype_op::Sub);
    sub.set_subnode(graph->get_io());
  }
  const std::array roots{parent};
  EXPECT_TRUE(validate_source_design(roots, State_target::cmos));
  EXPECT_FALSE(diag::sink().has_errors());
  EXPECT_FALSE(validate_source_design(roots, State_target::domino));
  ASSERT_EQ(diag::sink().records().size(), 1U);
  EXPECT_EQ(diag::sink().records()[0].span.file, "rtl/child.v");
  EXPECT_EQ(diag::sink().records()[0].span.start_line, 19U);
}
}  // namespace
}  // namespace livehd::synth

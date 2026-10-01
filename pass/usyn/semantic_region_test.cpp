// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "semantic_region.hpp"

#include <algorithm>
#include <optional>

#include "gtest/gtest.h"
#include "node_util.hpp"
#include "region_blast.hpp"

namespace livehd::usyn {
namespace {
namespace gu = graph_util;
using synth::Lnet;
using synth::State_role;
using synth::State_target;

class SemanticRegion : public ::testing::Test {
protected:
  Lnet                      net;
  synth::Source_state_table table;

  void SetUp() override {
    diag::sink().clear();
    diag::sink().set_jsonl_path("off");
    diag::sink().set_human_stderr(false);
    table.source_graph = "source_module";
  }
  void TearDown() override { diag::sink().clear(); }

  uint32_t state(std::string name, synth::Lid d, State_role role = State_role::register_candidate, bool inverted = false) {
    const auto k = net.add_latch("encoded_" + name, 'x');
    net.set_latch_input(k, inverted ? net.add_lut({d}, Lnet::kNot) : d);
    synth::Source_state s;
    s.node            = table.sources.size() + 10;
    s.name            = name;
    s.role            = role;
    s.bits            = 1;
    s.stages          = 1;
    s.translated_bits = 1;
    const auto index  = static_cast<uint32_t>(table.sources.size());
    table.sources.push_back(std::move(s));
    table.bits.push_back({
        index,
        0,
        0,
        k,
        std::move(name),
        {net.latch(k).q,    false},
        {net.latch(k).d, inverted}
    });
    return k;
  }
};

std::vector<bool> evaluate(const Xag_region& region, uint32_t assignment) {
  std::vector<bool> values(region.graph.size());
  const auto        read = [&](Xsignal signal) { return values[signal.id] != signal.inverted; };
  for (Id id = 1; id < region.graph.size(); ++id) {
    const auto& n = region.graph.node(id);
    switch (n.kind) {
      case Xag::Kind::constant: break;
      case Xag::Kind::source  : values[id] = (assignment >> n.source_index) & 1; break;
      case Xag::Kind::and_gate: values[id] = read(n.inputs[0]) && read(n.inputs[1]); break;
      case Xag::Kind::xor_gate: values[id] = read(n.inputs[0]) != read(n.inputs[1]); break;
    }
  }
  std::vector<bool> outputs;
  for (const auto& s : region.state) {
    outputs.push_back(read(s.d));
  }
  for (const auto& po : region.outputs) {
    outputs.push_back(read(po.signal));
  }
  return outputs;
}

TEST_F(SemanticRegion, SemanticDAndNamesIgnoreBackendEncodingAndMetadataOrder) {
  const auto a = net.add_input("a"), b = net.add_input("b");
  const auto d = net.add_lut({a, b}, Lnet::kXor2);
  state("pipe.r[0]", d, State_role::register_candidate, true);
  state("pipe.r[1]", a);
  net.add_output(net.latch(0).q, "q");
  std::reverse(table.bits.begin(), table.bits.end());
  Budget work{1000000};
  auto   result = import_semantic_region(net, table, State_target::cmos, work);
  ASSERT_TRUE(result.region) << result.reason;
  EXPECT_EQ(result.region->state_bits, (std::vector<uint32_t>{1, 0}));
  EXPECT_EQ(result.region->eligible, (std::vector<uint32_t>{0, 1}));
  EXPECT_EQ(result.region->logic.state[0].name, "pipe.r[0]");
  EXPECT_EQ(result.region->logic.state[1].name, "pipe.r[1]");
  for (uint32_t x = 0; x < 16; ++x) {
    EXPECT_EQ(evaluate(result.region->logic, x), (std::vector<bool>{bool((x ^ (x >> 1)) & 1), bool(x & 1), bool(x & 4)}));
  }
  table.sources[0].name = "changed_after_import";
  EXPECT_EQ(result.region->source.sources[0].name, "pipe.r[0]");
  EXPECT_EQ(net.latch(0).name, "encoded_pipe.r[0]");
}

TEST_F(SemanticRegion, PairDomainsUseActualEventSignalsAndKeepUnknownClocksSeparate) {
  const auto a = net.add_input("a");
  for (unsigned i = 0; i < 6; ++i) {
    state("q" + std::to_string(i), a);
    auto& s         = table.sources.back();
    s.clock.present = true;
    s.clock.node    = 100;
    s.clock.bits    = 1;
    s.clock_root    = s.clock;
  }
  table.sources[2].clock.node     = 101;  // same root, different gated event
  table.sources[3].clock.present  = false;
  table.sources[4].neg_clock      = true;
  table.sources[5].clock.constant = true;
  std::reverse(table.bits.begin(), table.bits.end());
  Budget     work{1000000};
  const auto result = import_semantic_region(net, table, State_target::cmos, work);
  ASSERT_TRUE(result.region) << result.reason;
  const auto& domains = result.region->domains;
  ASSERT_EQ(domains.size(), 6U);
  EXPECT_EQ(domains[0], domains[1]);
  EXPECT_NE(domains[0], domains[2]);
  EXPECT_NE(domains[0], domains[4]);
  EXPECT_EQ(domains[3], unknown_clock_domain);
  EXPECT_EQ(domains[5], unknown_clock_domain);
}

TEST_F(SemanticRegion, CmosPreservesSpecialStateAndNegativeEdgesWithoutAbsorption) {
  const auto a = net.add_input("a");
  state("memory_is_only_a_name", a);
  state("negative", a);
  table.sources.back().neg_clock = true;
  state("latch", a, State_role::transparent_latch);
  state("storage", a, State_role::memory);
  state("clock_control", a, State_role::icg);
  state("unknown_edge", a);
  table.sources.back().clock_edge_known = false;
  Budget work{100000000};
  auto   result = synthesize_stateful_region(net, table, State_target::cmos, {}, work);
  ASSERT_TRUE(result.region) << result.reason;
  ASSERT_TRUE(result.region->frozen);
  ASSERT_EQ(result.region->frozen->state.size(), 6U);
  EXPECT_TRUE(result.region->frozen->state[0].domino_latch);
  for (size_t i = 1; i < 6; ++i) {
    EXPECT_FALSE(result.region->frozen->state[i].domino_latch);
  }
  const auto& selected = result.region->selected;
  ASSERT_EQ(selected.endpoints.size(), 1U);
  EXPECT_EQ(selected.endpoints[0].state_index, 0U);
  EXPECT_EQ(selected.logic.state.size(), 6U);
  EXPECT_EQ(result.region->source.sources.size(), 6U);
  for (uint32_t x = 0; x < 128; ++x) {
    EXPECT_EQ(evaluate(selected.logic, x), std::vector<bool>(6, x & 1));
  }
}

TEST_F(SemanticRegion, DominoRejectsSourceClockAndLatchWithOriginalLocations) {
  const auto a = net.add_input("a");
  state("negative", a);
  table.sources.back().neg_clock       = true;
  table.sources.back().span.file       = "design.prp";
  table.sources.back().span.start_line = 42;
  state("latch", a, State_role::transparent_latch);
  Budget work{1000000};
  auto   result = synthesize_stateful_region(net, table, State_target::domino, {}, work);
  EXPECT_EQ(result.status, Status::invalid);
  EXPECT_FALSE(result.region);
  ASSERT_EQ(diag::sink().records().size(), 2U);
  EXPECT_EQ(diag::sink().records()[0].span.file, "design.prp");
  EXPECT_EQ(diag::sink().records()[0].span.start_line, 42U);
  EXPECT_EQ(diag::sink().records()[1].code, "domino-source-latch");
}

TEST_F(SemanticRegion, NativeOrdinaryRegistersCannotDisappearFromEndpointSelection) {
  synth::Source_state s;
  s.node   = 20;
  s.name   = "not_crossed";
  s.bits   = 2;
  s.stages = 1;
  s.role   = State_role::register_candidate;
  table.sources.push_back(s);
  for (auto target : {State_target::cmos, State_target::domino}) {
    Budget work{1000000};
    auto   result = import_semantic_region(net, table, target, work);
    EXPECT_FALSE(result.region);
    EXPECT_EQ(result.status, Status::invalid);
    EXPECT_NE(result.reason.find("complete logical translation"), std::string::npos);
  }
  // Special native structures are retained as explicit boundary metadata.
  table.sources[0].role = State_role::memory;
  Budget work{1000000};
  auto   result = import_semantic_region(net, table, State_target::domino, work);
  ASSERT_TRUE(result.region) << result.reason;
  EXPECT_TRUE(result.region->eligible.empty());
  EXPECT_EQ(result.region->source.sources[0].name, "not_crossed");
}

TEST_F(SemanticRegion, RejectsMalformedAndDuplicateCorrespondenceAtomically) {
  const auto a = net.add_input("a");
  state("r0", a);
  state("r1", a);
  const auto original = table;
  for (int mutation = 0; mutation < 9; ++mutation) {
    table = original;
    switch (mutation) {
      case 0: table.bits.pop_back(); break;
      case 1: table.bits[1].latch = 0; break;
      case 2: table.bits[0].source = 99; break;
      case 3: table.bits[0].q.inverted = true; break;
      case 4: table.bits[0].d.node = net.latch(0).q; break;
      case 5: table.sources[0].translated_bits = 0; break;
      case 6: table.sources[1].node = table.sources[0].node; break;
      case 7: table.sources[0].stages = 2; break;
      case 8: table.bits[1].source = 0; break;
    }
    Budget work{1000000};
    auto   result = import_semantic_region(net, table, State_target::cmos, work);
    EXPECT_EQ(result.status, Status::invalid) << mutation;
    EXPECT_FALSE(result.region) << mutation;
  }
}

TEST_F(SemanticRegion, BudgetRefusalDoesNotPublishMetadataOrPartialState) {
  const auto a = net.add_input("a");
  state("r", a);
  for (uint64_t limit : {0, 1, 3, 5}) {
    Budget work{limit};
    auto   result = synthesize_stateful_region(net, table, State_target::cmos, {}, work);
    EXPECT_EQ(result.status, Status::search_exhausted) << limit;
    EXPECT_FALSE(result.region) << limit;
  }
  Budget work{1000000};
  auto   result = import_semantic_region(net, table, State_target::cmos, work, 1);
  EXPECT_EQ(result.status, Status::search_exhausted);
  EXPECT_FALSE(result.region);
}

// The stateful entry charges import, the logical admission and identity
// baseline, and freezing to the structural ledger with plain spends: without
// any search credits it still publishes a frozen, behaviorally exact identity
// selection, the structural charge is the same on every run, and one credit
// less refuses the region wherever that charge runs out.
TEST_F(SemanticRegion, MandatoryStepsDrawOnTheStructuralLedgerOnly) {
  const auto a = net.add_input("a"), b = net.add_input("b"), c = net.add_input("c");
  state("r0", net.add_lut({net.add_lut({a, b}, Lnet::kXor2), c}, Lnet::kAnd2));
  state("r1", net.add_lut({a, net.latch(0).q}, Lnet::kXor2), State_role::register_candidate, true);
  net.add_output(net.latch(1).q, "q");
  std::optional<uint64_t> charged;
  for (int run = 0; run < 2; ++run) {
    Budget structural{100000000}, search{0};
    auto   result = synthesize_stateful_region(net, table, State_target::cmos, {}, structural, search);
    ASSERT_TRUE(result.region) << result.reason;
    ASSERT_TRUE(result.region->frozen);
    EXPECT_EQ(search.consumed, 0U);
    EXPECT_EQ(result.report.identity_fallbacks, 2U);
    for (const auto& endpoint : result.region->selected.endpoints) {
      EXPECT_EQ(endpoint.origin, "identity");
    }
    EXPECT_EQ(result.region->frozen->cells.size(), 2U);  // one DominoLatch per register, nothing else
    const auto expanded = expand_endpoint_netlist(*result.region->frozen, structural);
    ASSERT_EQ(expanded.status, Status::feasible) << expanded.reason;
    Budget     reference_work{1000000};
    const auto reference = import_semantic_region(net, table, State_target::cmos, reference_work);
    ASSERT_TRUE(reference.region);
    for (uint32_t x = 0; x < 32; ++x) {
      EXPECT_EQ(evaluate(expanded, x), evaluate(reference.region->logic, x)) << x;
    }
    if (!charged) {
      Budget     counted{100000000}, none{0};
      const auto again = synthesize_stateful_region(net, table, State_target::cmos, {}, counted, none);
      ASSERT_TRUE(again.region);
      charged = counted.consumed;
    }
  }
  Budget     short_by_one{*charged - 1}, search{0};
  const auto refused = synthesize_stateful_region(net, table, State_target::cmos, {}, short_by_one, search);
  EXPECT_FALSE(refused.region);
  EXPECT_EQ(refused.status, Status::search_exhausted);
  Budget     exact{*charged}, none{0};
  const auto admitted = synthesize_stateful_region(net, table, State_target::cmos, {}, exact, none);
  ASSERT_TRUE(admitted.region) << admitted.reason;
  EXPECT_EQ(exact.remaining, 0U);
  // With search credits the same region is searched; its structural charge
  // then covers the published (searched) selection's freeze.
  Budget     structural{100000000}, plenty{100000000};
  const auto searched = synthesize_stateful_region(net, table, State_target::cmos, {}, structural, plenty);
  ASSERT_TRUE(searched.region) << searched.reason;
  EXPECT_EQ(searched.report.identity_fallbacks, 0U);
  EXPECT_GT(plenty.consumed, 0U);
  EXPECT_EQ(plenty.credit_floor().work, searched.report.work.total() - searched.report.work.admission);
}

TEST_F(SemanticRegion, CollisionsPreserveSourceNamesAndUseStateIdentity) {
  const auto a = net.add_input("r");
  state("r", a);
  state("r", a);
  state("r__state10_s0_b0", a);  // reserve a source spelling like the generated suffix
  Budget work{1000000};
  auto   first = import_semantic_region(net, table, State_target::cmos, work);
  ASSERT_TRUE(first.region) << first.reason;
  const auto& states = first.region->logic.state;
  EXPECT_EQ(states[0].name, "r__state10_s0_b0_1");
  EXPECT_EQ(states[1].name, "r__state11_s0_b0");
  EXPECT_EQ(states[2].name, "r__state10_s0_b0");
  EXPECT_EQ(first.region->source.bits[0].name, "r");
  std::reverse(table.bits.begin(), table.bits.end());
  auto second = import_semantic_region(net, table, State_target::cmos, work);
  ASSERT_TRUE(second.region) << second.reason;
  for (size_t i = 0; i < states.size(); ++i) {
    EXPECT_EQ(states[i].name, second.region->logic.state[i].name);
  }
}

TEST_F(SemanticRegion, PowerOnStateAndOutputOnlyLogicSurviveTheSemanticEntry) {
  const auto a = net.add_input("a");
  net.add_output(a, "out");
  Budget work{10000000};
  auto   combinational = synthesize_stateful_region(net, table, State_target::cmos, {}, work);
  ASSERT_TRUE(combinational.region) << combinational.reason;
  EXPECT_TRUE(combinational.region->selected.endpoints.empty());
  EXPECT_EQ(evaluate(combinational.region->selected.logic, 0), std::vector<bool>{false});
  EXPECT_EQ(evaluate(combinational.region->selected.logic, 1), std::vector<bool>{true});
  const auto k = net.add_latch("encoded", '1');
  net.set_latch_input(k, a);
  synth::Source_state source;
  source.node = 12;
  source.name = "initialized";
  source.role = State_role::register_candidate;
  source.bits = source.stages = 1;
  source.translated_bits      = 1;
  source.power_on             = true;
  source.initial.present = source.initial.constant = true;
  source.initial.value                             = *Dlop::create_integer(1);
  table.sources.push_back(source);
  table.bits.push_back({
      0,
      0,
      0,
      k,
      "initialized",
      {net.latch(k).q, false},
      {             a, false}
  });
  auto sequential = synthesize_stateful_region(net, table, State_target::cmos, {}, work);
  ASSERT_TRUE(sequential.region) << sequential.reason;
  const auto& r = *sequential.region;
  EXPECT_EQ(r.selected.logic.state[0].init, '1');
  EXPECT_TRUE(r.source.sources[0].power_on);
  EXPECT_TRUE(r.source.sources[0].initial.value.bit_test(0));
  auto emitted = export_lnet(r.selected.logic, work);
  ASSERT_TRUE(emitted.net) << emitted.reason;
  EXPECT_EQ(emitted.net->latch(0).init, '1');
  EXPECT_EQ(emitted.net->latch(0).name, "initialized");
}

TEST_F(SemanticRegion, BlastedPipelineControlsAndQnEncodingSurviveSelectionAndEmission) {
  hhds::GraphLibrary     library;
  auto                   io = library.create_io("source");
  partition::Region_body rb;
  for (const auto& name : {"clk", "a", "en", "rst"}) {
    const int bits = std::string_view{name} == "a" ? 2 : 1;
    io->add_input(name, static_cast<hhds::Port_id>(rb.inputs.size() + 1));
    io->set_bits(name, bits);
    io->set_unsign(name, true);
    rb.inputs.push_back({name, {}, bits, false});
  }
  io->add_output("out", 5);
  io->set_bits("out", 2);
  auto graph = io->create_graph();
  for (auto& p : rb.inputs) {
    p.src_driver = graph->get_input_pin(p.name);
    gu::set_ubits(p.src_driver, p.bits);
  }
  rb.src         = graph.get();
  rb.module_name = "region";
  auto body_io   = library.create_io("region");
  for (size_t i = 0; i < rb.inputs.size(); ++i) {
    body_io->add_input(rb.inputs[i].name, i + 1);
  }
  body_io->add_output("out", 5);
  rb.body = body_io->create_graph().get();
  auto n  = gu::create_typed_node(*graph, Ntype_op::Flop);
  auto q  = n.create_driver_pin(0);
  gu::set_ubits(q, 2);
  gu::set_pin_name(q, "pipe.r");
  q.connect_sink(graph->get_output_pin("out"));
  const auto connect = [&](std::string_view port, hhds::Pin_class driver) {
    driver.connect_sink(gu::setup_sink_pid(n, Ntype::get_sink_pid(Ntype_op::Flop, port)));
  };
  connect("din", graph->get_input_pin("a"));
  connect("clock_pin", graph->get_input_pin("clk"));
  connect("enable", graph->get_input_pin("en"));
  connect("reset_pin", graph->get_input_pin("rst"));
  for (auto [port, value] : {
           std::pair{ "initial", 2},
           {"negreset", 1},
           {"pipe_min", 2}
  }) {
    connect(port, gu::create_const(*graph, *Dlop::create_integer(value)));
  }
  std::vector<hhds::Node_class> nodes{n};
  rb.nodes = nodes;
  rb.outputs.push_back({"out", q, 2, false});
  for (bool async : {false, true}) {
    if (async) {
      connect("async", gu::create_const(*graph, *Dlop::create_integer(1)));
    }
    for (int mode : {0, 1, 2}) {
      const bool           qn      = mode != 0;
      const bool           logical = mode == 2;
      synth::Blast_options options;
      options.capture_state = true;
      options.qn_encode     = qn;
      options.logical_state = logical;
      options.map_register  = !logical;
      if (!logical) {
        options.areset_cell[0] = options.areset_cell[1] = qn ? 1 : 0;
      }
      auto blast = synth::blast_region(rb, options, {});
      ASSERT_EQ(blast.status, synth::Region_blast::Status::blasted);
      ASSERT_TRUE(blast.source_state);
      auto snapshot = *blast.source_state;
      std::reverse(snapshot.bits.begin(), snapshot.bits.end());
      Budget work{100000000};
      auto   result = synthesize_stateful_region(blast.lnet, snapshot, State_target::domino, {}, work);
      ASSERT_TRUE(result.region) << result.reason;
      const auto& region = *result.region;
      ASSERT_EQ(region.selected.endpoints.size(), 4U);
      ASSERT_EQ(region.selected.logic.state.size(), 4U);
      EXPECT_EQ(region.selected.logic.state[0].name, "___pipe0_pipe.r[0]");
      EXPECT_EQ(region.selected.logic.state[3].name, "pipe.r[1]");
      EXPECT_TRUE(region.source.sources[0].neg_reset);
      EXPECT_TRUE(region.source.sources[0].enable.present);
      EXPECT_FALSE(region.source.sources[0].power_on);
      EXPECT_EQ(region.source.sources[0].stages, 2U);
      EXPECT_EQ(region.source.sources[0].async_reset, async);
      EXPECT_EQ(region.source.logical_boundary, logical);
      auto exported = export_lnet(region.selected.logic, work);
      ASSERT_TRUE(exported.net) << exported.reason;
      EXPECT_TRUE(synth::validate_state_controls(region.source, *exported.net));
      auto roundtrip = import_lnet(*exported.net, work);
      ASSERT_EQ(roundtrip.status, Status::feasible);
      for (const auto& s : roundtrip.state) {
        EXPECT_EQ(s.init, 'x');  // initial is a reset value, not a power-on value
      }
      for (uint32_t x = 0; x < (logical ? 512U : 256U); ++x) {
        // Independent oracle: a[1:0], en, active-low rst, then the four
        // source-state bits. Translate the variable order from actual PI names.
        uint32_t assignment = 0;
        for (size_t i = 0; i < blast.lnet.inputs().size(); ++i) {
          const auto&    name  = blast.lnet.inputs()[i].name;
          const uint32_t index = name == "a_b0" ? 0 : name == "a_b1" ? 1 : name == "en_b0" ? 2 : name == "rst_b0" ? 3 : 8;
          ASSERT_TRUE(name == "a_b0" || name == "a_b1" || name == "en_b0" || name == "rst_b0" || name == "clk_b0") << name;
          assignment |= ((x >> index) & 1) << i;
        }
        std::vector<bool> expected;
        for (uint32_t k = 0; k < region.state_bits.size(); ++k) {
          const auto& bit          = region.source.bits[region.state_bits[k]];
          const bool  state_value  = (x >> (4 + bit.stage * 2 + bit.bit)) & 1;
          assignment              |= uint32_t{state_value} << (blast.lnet.inputs().size() + k);
          const bool data          = (x >> (bit.stage == 0 ? bit.bit : 4 + bit.bit)) & 1;
          expected.push_back(!async && (x & 8) == 0 ? bit.bit == 1 : (x & 4) ? data : state_value);
        }
        expected.push_back(x & 64);
        expected.push_back(x & 128);
        if (logical) {
          expected.push_back(x & 256);  // raw clock control output
          if (async) {
            expected.push_back(x & 8);  // raw reset; active-low remains in source metadata
          }
        }
        EXPECT_EQ(evaluate(region.selected.logic, assignment), expected) << qn << ':' << x;
        EXPECT_EQ(evaluate(roundtrip, assignment), expected) << qn << ':' << x;
      }
    }
  }
}

}  // namespace
}  // namespace livehd::usyn

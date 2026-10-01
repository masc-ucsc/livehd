// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "artifact.hpp"

#include <algorithm>
#include <functional>

#include "gtest/gtest.h"
#include "logical_writer.hpp"

namespace livehd::usyn {
namespace {
using synth::Lnet;
using synth::State_target;
class Artifact : public ::testing::Test {
protected:
  Stateful_region           region;
  Logical_report            report;
  Lnet                      raw_net;  // the RAW translation `region` was selected from
  synth::Source_state_table raw_table;
  void                      SetUp() override {
    diag::sink().clear();
    diag::sink().set_jsonl_path("off");
    diag::sink().set_human_stderr(false);
    build("a", raw_net, raw_table);
    Budget work{100000000};
    auto   result = synthesize_stateful_region(raw_net, raw_table, State_target::cmos, {}, work);
    ASSERT_TRUE(result.region) << result.reason;
    report = std::move(result.report);
    region = std::move(*result.region);
  }
  // `first` spells the first RAW PI, a region-boundary net name.
  static void build(const std::string& first, Lnet& net, synth::Source_state_table& table) {
    const auto a = net.add_input(first), b = net.add_input("b"), clk = net.add_input("clk"), rst = net.add_input("rst");
    table.source_graph     = "original";
    table.logical_boundary = true;
    synth::Source_state row;
    row.node            = 42;
    row.name            = "pipeline";
    row.bits            = 1;
    row.stages          = 2;
    row.translated_bits = 2;
    row.role            = synth::State_role::register_candidate;
    row.width_known     = true;
    row.clock           = {100, 7, true, false, 1, {}};
    row.clock_root      = {101, 9, true, false, 1, {}};
    row.reset           = {102, 3, true, false, 2, {}};
    row.initial         = {103, 4, true, true, 133, *Dlop::from_binary(std::string(130, '?') + "1?0", false)};
    row.enable          = {104, 2, true, false, 1, {}};
    row.q               = {42, 0, true, false, 1, {}};
    row.data            = {105, 0, true, false, 1, {}};
    row.async_reset     = true;
    row.neg_reset       = true;
    row.icg             = 1;
    row.span            = {1234567890123ULL, 5, "rtl/source.prp", 10, 29, 12, 3, 13, 8};
    table.sources.push_back(row);
    synth::Source_state memory;
    memory.node = 88;
    memory.name = "memory";
    memory.role = synth::State_role::memory;
    memory.bits = 128;
    table.sources.push_back(memory);
    synth::Source_clock_gate gate;
    gate.name       = "gclk0";
    gate.output     = row.clock_root;
    gate.clock      = row.clock_root;
    gate.enable     = row.enable;
    gate.latch_node = 72;
    gate.span       = row.span;
    table.clocks.push_back(gate);
    gate.name       = "gclk1";
    gate.parent     = 0;
    gate.latch_node = 73;
    gate.output     = row.clock;
    table.clocks.push_back(gate);
    for (uint32_t i = 0; i < 2; ++i) {
      const auto k = net.add_latch(i ? "pipeline" : "___pipe0_pipeline", i ? '1' : 'x');
      net.set_latch_input(k, i ? net.latch(0).q : net.add_lut({a, b}, Lnet::kXor2));
      table.bits.push_back({
          0,
          i,
          0,
          k,
          net.latch(k).name,
          {net.latch(k).q, false},
          {net.latch(k).d, false}
      });
      net.add_output(net.latch(k).q, "q" + std::to_string(i));
    }
    // Preserve metadata ordering independently of logical state order.
    std::reverse(table.bits.begin(), table.bits.end());
    table.controls.push_back({0, synth::State_control_kind::clock, {2}});
    net.add_output(clk, "clock");
    table.controls.push_back({
        0,
        synth::State_control_kind::async_reset,
        {3, 4}
    });
    net.add_output(rst, "reset0");
    net.add_output(net.add_lut({rst}, Lnet::kNot), "reset1");
  }
  void TearDown() override { diag::sink().clear(); }
};

std::vector<bool> evaluate(const Xag_region& r, uint32_t assignment) {
  std::vector<bool> values(r.graph.size());
  const auto        read = [&](Xsignal s) { return values[s.id] != s.inverted; };
  for (Id i = 1; i < r.graph.size(); ++i) {
    const auto& n = r.graph.node(i);
    switch (n.kind) {
      case Xag::Kind::constant: break;
      case Xag::Kind::source  : values[i] = (assignment >> n.source_index) & 1; break;
      case Xag::Kind::and_gate: values[i] = read(n.inputs[0]) && read(n.inputs[1]); break;
      case Xag::Kind::xor_gate: values[i] = read(n.inputs[0]) != read(n.inputs[1]); break;
    }
  }
  std::vector<bool> result;
  for (const auto& po : r.outputs) {
    result.push_back(read(po.signal));
  }
  for (const auto& s : r.state) {
    result.push_back(read(s.d));
  }
  return result;
}

void put32(std::string& bytes, size_t offset, uint32_t value) {
  for (unsigned i = 0; i < 4; ++i) {
    bytes.at(offset + i) = char(value >> (8 * i));
  }
}
void repair_checksum(std::string& bytes) {
  uint64_t h = 14695981039346656037ULL;
  for (size_t i = 0; i < bytes.size() - 8; ++i) {
    h = (h ^ uint8_t(bytes[i])) * 1099511628211ULL;
  }
  for (unsigned i = 0; i < 8; ++i) {
    bytes[bytes.size() - 8 + i] = char(h >> (8 * i));
  }
}
}  // namespace

TEST_F(Artifact, RoundTripPreservesFullSourceMetadataFunctionsAndCmosEmission) {
  Budget     work{100000000};
  const auto saved = serialize_artifact("region0", State_target::cmos, region, work);
  ASSERT_EQ(saved.status, Status::feasible) << saved.reason;
  auto loaded = deserialize_artifact(saved.bytes, State_target::cmos, work);
  ASSERT_TRUE(loaded.region) << loaded.reason;
  const auto& copy = *loaded.region;
  EXPECT_EQ(copy.module_name, "region0");
  EXPECT_EQ(copy.target, State_target::cmos);
  EXPECT_EQ(copy.source.sources.size(), 2U);
  EXPECT_EQ(copy.source.sources[0].initial.value.serialize(), region.source.sources[0].initial.value.serialize());
  EXPECT_EQ(copy.source.sources[0].span.source_id, 1234567890123ULL);
  EXPECT_EQ(copy.source.sources[0].span.start_line, 12U);
  EXPECT_EQ(copy.source.sources[0].span.end_byte, 29U);
  EXPECT_EQ(copy.state_bits, region.state_bits);
  EXPECT_EQ(copy.source.clocks[1].parent, 0);
  EXPECT_EQ(copy.source.clocks[1].output.port, 7U);
  EXPECT_EQ(copy.source.controls[1].outputs, (std::vector<uint32_t>{3, 4}));
  const auto expanded = expand_endpoint_netlist(copy.netlist, work);
  ASSERT_EQ(expanded.status, Status::feasible) << expanded.reason;
  for (uint32_t x = 0; x < 64; ++x) {
    EXPECT_EQ(evaluate(expanded, x), evaluate(region.selected.logic, x));
  }
  const auto emitted = write_logical_module(copy, work);
  ASSERT_TRUE(emitted.module) << emitted.reason;
  EXPECT_EQ(emitted.module->state.size(), 2U);
  const auto saved_again = serialize_artifact(copy, work);
  ASSERT_EQ(saved_again.status, Status::feasible) << saved_again.reason;
  EXPECT_EQ(saved_again.bytes, saved.bytes);
}

TEST_F(Artifact, CacheEnvelopeRetainsDecisionsAndRejectsMismatchedEvidence) {
  Budget work{100000000};
  report.pairs.requeues                  = 3;
  report.pairs.domain_skips              = 9;
  report.pairs.gain_skips                = 7;
  report.pairs.bounded_windows           = 11;
  report.pairs.combinations              = 13;
  report.pairs.choices                   = 17;
  report.pairs.choice_combinations       = 23;
  report.pairs.fanout_windows            = 29;
  report.pairs.fanout_ports              = 31;
  report.pairs.fanout_skips              = 37;
  report.pairs.fanout_wins               = 41;
  report.pairs.joint_windows             = 43;
  report.pairs.joint_source_pairs        = 71;
  report.pairs.joint_partitions          = 47;
  report.pairs.joint_candidates          = 53;
  report.pairs.joint_divisors            = 59;
  report.pairs.joint_combinations        = 61;
  report.pairs.joint_wins                = 67;
  report.pairs.joint_care_windows        = 73;
  report.pairs.joint_care_partitions     = 79;
  report.pairs.joint_care_phases         = 83;
  report.pairs.joint_care_attempts       = 89;
  report.pairs.joint_care_retained       = 97;
  report.pairs.joint_care_bytes          = 101;
  report.pairs.joint_care_combinations   = 103;
  report.pairs.joint_care_wins           = 107;
  report.pairs.joint_recode_windows      = 109;
  report.pairs.joint_recode_partitions   = 111;
  report.pairs.joint_recode_encodings    = 113;
  report.pairs.joint_recode_phases       = 115;
  report.pairs.joint_recode_attempts     = 117;
  report.pairs.joint_recode_retained     = 119;
  report.pairs.joint_recode_bytes        = 121;
  report.pairs.joint_recode_combinations = 123;
  report.pairs.joint_recode_wins         = 125;
  report.initial[0].limits.push_back("bounded search evidence");
  report.initial[0].image_cache_bytes         = 123;
  report.initial[0].single_divisor_attempts   = 4;
  report.initial[0].parallel_divisor_attempts = 2;
  report.initial[0].deferred_divisor_bytes    = 456;
  report.initial[0].boundary_trials           = 29;
  report.initial[0].boundary_replacements     = 7;
  report.initial[0].boundary_bytes_peak       = 4096;
  report.initial[0].boundary_wins             = 2;
  report.initial[0].boundary_two_cell_work    = 83;
  report.initial[0].boundary_multi_cell_work  = 109;
  report.residual.limits.push_back("residual evidence");
  report.work               = {17, 29, 43, 59, 71, 83};
  report.identity_fallbacks = 1;
  const Credit_floor credit{9876, 20000, false, 0};
  constexpr uint64_t structural = 4321;  // covers report.work.admission
  const auto         saved      = serialize_selection_record("key", "region", region, report, credit, structural, work);
  ASSERT_EQ(saved.status, Status::feasible) << saved.reason;
  const auto loaded = deserialize_selection_record(saved.bytes, "key", State_target::cmos, work);
  ASSERT_TRUE(loaded.record) << loaded.reason;
  EXPECT_EQ(loaded.record->report, report);
  EXPECT_EQ(loaded.record->credit, credit);
  EXPECT_EQ(loaded.record->structural, structural);
  EXPECT_EQ(loaded.record->module_name, "region");
  const auto again = serialize_selection_record("key",
                                                "region",
                                                loaded.record->selected,
                                                loaded.record->report,
                                                credit,
                                                loaded.record->structural,
                                                work);
  ASSERT_EQ(again.status, Status::feasible) << again.reason;
  EXPECT_EQ(again.bytes, saved.bytes);
  // A bound search keeps its exact credits.
  const Credit_floor bound{9876, 12000, true, 12345};
  const auto         exact = serialize_selection_record("key", "region", region, report, bound, structural, work);
  ASSERT_EQ(exact.status, Status::feasible) << exact.reason;
  const auto exact_loaded = deserialize_selection_record(exact.bytes, "key", State_target::cmos, work);
  ASSERT_TRUE(exact_loaded.record) << exact_loaded.reason;
  EXPECT_EQ(exact_loaded.record->credit, bound);
  EXPECT_TRUE(bound.reproduces(12345));
  EXPECT_FALSE(bound.reproduces(12346));
  EXPECT_FALSE(bound.reproduces(12000));
  EXPECT_TRUE(credit.reproduces(20000));
  EXPECT_TRUE(credit.reproduces(1U << 30));
  EXPECT_FALSE(credit.reproduces(19999));
  // Budget::credit_floor() never produces these; a record carrying one is invalid.
  for (const auto& inconsistent : {
           Credit_floor{9876, 9875, false,  0},
           Credit_floor{   1,    2, false,  3},
           Credit_floor{   1,   20,  true, 19}
  }) {
    EXPECT_EQ(serialize_selection_record("key", "region", region, report, inconsistent, structural, work).status, Status::invalid);
  }
  // Structural work always covers the logical admission stage, and every
  // counted identity fallback is published as an identity endpoint.
  EXPECT_EQ(serialize_selection_record("key", "region", region, report, credit, report.work.admission - 1, work).status,
            Status::invalid);
  EXPECT_EQ(serialize_selection_record("key", "region", region, report, credit, report.work.admission, work).status,
            Status::feasible);
  // Both endpoints of this fixture publish their identity; say the search chose
  // the first one's, so a count within the endpoints can still overflow.
  auto mixed                              = region;
  mixed.selected.endpoints.front().origin = "whole-one-cell";
  const auto& endpoints                   = mixed.selected.endpoints;
  const auto  identities                  = static_cast<uint64_t>(
      std::count_if(endpoints.begin(), endpoints.end(), [](const auto& e) { return e.origin == "identity"; }));
  ASSERT_EQ(identities, 1U);
  ASSERT_LT(identities, endpoints.size());
  auto fallbacks               = report;
  fallbacks.identity_fallbacks = identities;
  const auto counted           = serialize_selection_record("key", "region", mixed, fallbacks, credit, structural, work);
  ASSERT_EQ(counted.status, Status::feasible) << counted.reason;
  ASSERT_TRUE(deserialize_selection_record(counted.bytes, "key", State_target::cmos, work).record);
  for (const auto overflow : {identities + 1, uint64_t{endpoints.size()} + 1}) {
    SCOPED_TRACE(overflow);
    fallbacks.identity_fallbacks = overflow;
    const auto overflowing       = serialize_selection_record("key", "region", mixed, fallbacks, credit, structural, work);
    EXPECT_EQ(overflowing.status, Status::invalid);
    EXPECT_EQ(overflowing.reason, "incomplete cached endpoint decisions");
  }
  // Loading checks it too, under a valid checksum: respelling the only
  // identity origin leaves its counted fallback without an identity endpoint.
  const std::string origin{"identity"};
  auto              respelled = counted.bytes;
  const auto        at        = respelled.find(origin);
  ASSERT_NE(at, std::string::npos);
  ASSERT_EQ(respelled.find(origin, at + 1), std::string::npos);  // no other string spells it
  respelled[at + origin.size() - 1] = 'Y';
  repair_checksum(respelled);
  const auto respelled_loaded = deserialize_selection_record(respelled, "key", State_target::cmos, work);
  EXPECT_EQ(respelled_loaded.status, Status::invalid);
  EXPECT_FALSE(respelled_loaded.record);
  EXPECT_EQ(respelled_loaded.reason, "incomplete cached endpoint decisions");
  EXPECT_FALSE(deserialize_selection_record(saved.bytes, "another key", State_target::cmos, work).record);
  EXPECT_FALSE(deserialize_selection_record(saved.bytes, "key", static_cast<State_target>(99), work).record);
  auto bad             = saved.bytes;
  bad[bad.size() / 2] ^= 1;
  EXPECT_FALSE(deserialize_selection_record(bad, "key", State_target::cmos, work).record);
  // Version 16 predates the replayed structural work, 15 the credit floor (14
  // joint recoding evidence). Even a valid checksum must not let those layouts
  // be interpreted as the new schema.
  for (const uint32_t version : {15U, 16U}) {
    auto old_version = saved.bytes;
    put32(old_version, 4, version);
    repair_checksum(old_version);
    const auto obsolete = deserialize_selection_record(old_version, "key", State_target::cmos, work);
    EXPECT_EQ(obsolete.status, Status::invalid) << version;
    EXPECT_FALSE(obsolete.record) << version;
    EXPECT_EQ(obsolete.reason, "incompatible selection record") << version;
  }
  // A checksum cannot authorize endpoint evidence that disagrees with the
  // frozen netlist used by emission.
  region.selected.endpoints[0].cells.back().phase = 3;
  EXPECT_EQ(serialize_selection_record("key", "region", region, report, credit, structural, work).status, Status::invalid);
}

TEST_F(Artifact, LogicalIdentityIncludesSourceAndPolicyButNoCreditsOrMappingConfiguration) {
  Budget                work{100000000};
  const Logical_options options;
  const auto            identity = [&](const auto& net, const auto& source, const auto& policy, uint64_t code) {
    auto encoded = serialize_logical_identity(net, source, {}, policy, code, "resource policy", work);
    EXPECT_EQ(encoded.status, Status::feasible) << encoded.reason;
    return encoded.bytes;
  };
  // Search credits are not an argument: the record's credit floor decides reuse.
  const auto before = identity(raw_net, raw_table, options, 5678);
  EXPECT_EQ(before, identity(raw_net, raw_table, options, 5678));
  EXPECT_NE(before, identity(raw_net, raw_table, options, 5679));
  // Provenance is not identity: spans, the source graph, the local node
  // numbering and a barrier row's diagnostic spelling are rebound on a hit.
  auto source                       = raw_table;
  source.sources[0].span.start_line = 99;
  source.clocks[1].span.file        = "moved.prp";
  source.source_graph               = "renamed definition";
  source.sources[1].name            = "Memory_77 'renamed'";
  const auto renumber               = [](synth::Source_signal& s) { s.node += 1000; };
  for (auto& row : source.sources) {
    row.node += 1000;
    for (auto* signal : {&row.q, &row.data, &row.clock, &row.clock_root, &row.enable, &row.reset, &row.initial}) {
      renumber(*signal);
    }
  }
  for (auto& gate : source.clocks) {
    gate.latch_node += 1000;
    renumber(gate.output);
    renumber(gate.clock);
    renumber(gate.enable);
  }
  EXPECT_EQ(before, identity(raw_net, source, options, 5678));
  // Their equalities, and every register/latch/clock-gate spelling, are.
  source                        = raw_table;
  source.sources[0].enable.node = source.sources[0].data.node;
  EXPECT_NE(before, identity(raw_net, source, options, 5678));
  for (const auto change : {+[](synth::Source_state_table& t) { t.sources[0].name = "renamed"; },
                            +[](synth::Source_state_table& t) { t.bits[0].name = "renamed"; },
                            +[](synth::Source_state_table& t) { t.clocks[0].name = "renamed"; },
                            +[](synth::Source_state_table& t) { t.sources[0].neg_clock = true; }}) {
    source = raw_table;
    change(source);
    EXPECT_NE(before, identity(raw_net, source, options, 5678));
  }
  // RAW port spellings are boundary-net names; a top-level IO name the owner
  // binds to a position, a barrier's own name and a latch name are identity.
  Lnet                      renamed_ports;
  synth::Source_state_table renamed_table;
  build("renamed_net", renamed_ports, renamed_table);
  EXPECT_EQ(before, identity(renamed_ports, renamed_table, options, 5678));
  const auto named = [&](const Identity_names& names) {
    Budget     local{100000000};
    const auto encoded = serialize_logical_identity(raw_net, raw_table, names, options, 5678, "resource policy", local);
    EXPECT_EQ(encoded.status, Status::feasible) << encoded.reason;
    return encoded.bytes;
  };
  Identity_names io;
  io.inputs.resize(raw_net.inputs().size());
  EXPECT_EQ(before, named(io));
  io.inputs[0]       = "a[0]";
  const auto with_io = named(io);
  EXPECT_NE(before, with_io);
  io.inputs[0] = "a2[0]";
  EXPECT_NE(with_io, named(io));
  io.inputs.clear();
  io.ports = {"input a"};
  EXPECT_NE(before, named(io));
  io.ports.clear();
  io.barriers             = {"", "mem"};
  const auto with_barrier = named(io);
  EXPECT_NE(before, with_barrier);
  io.barriers = {"", "mem2"};
  EXPECT_NE(with_barrier, named(io));
  io.barriers = {"too short"};
  Budget local{100000000};
  EXPECT_EQ(serialize_logical_identity(raw_net, raw_table, io, options, 5678, "", local).status, Status::invalid);
  const std::vector<std::function<void(Logical_options&)>> changes{
      [](auto& o) { ++o.endpoint.gates.stack; },
      [](auto& o) { ++o.endpoint.cost.static_not; },
      [](auto& o) { ++o.endpoint.image_cache_bytes; },
      [](auto& o) { o.endpoint.fast_accept = false; },
      [](auto& o) { ++o.endpoint.functions.factoring_choices; },
      [](auto& o) { ++o.endpoint.local_candidates; },
      [](auto& o) { ++o.endpoint.tier_work; },
      [](auto& o) { ++o.pair_trials; },
      [](auto& o) { ++o.pair_choices; },
      [](auto& o) { o.feedback = false; },
      [](auto& o) { ++o.residual.depth_slack; },
      [](auto& o) { ++o.residual.window_work; },
      [](auto& o) { o.residual.resubstitute = false; },
  };
  for (const auto& change : changes) {
    auto changed = options;
    change(changed);
    EXPECT_NE(before, identity(raw_net, raw_table, changed, 5678));
  }
  auto changed = raw_net;
  changed.set_latch_input(0, changed.inputs()[0].node);
  EXPECT_NE(before, identity(changed, raw_table, options, 5678));
}

TEST_F(Artifact, RebindingAppliesFreshProvenanceAndPortSpellingsByPosition) {
  Budget                    work{100000000};
  Selection_record          record{"original", region, report, {}};
  // A fresh translation of the same identity: a renamed boundary net, a new
  // definition name, spans and node numbers.
  Lnet                      net;
  synth::Source_state_table table;
  build("renamed_net", net, table);
  table.source_graph                = "renamed";
  table.sources[0].span.start_line  = 77;
  table.sources[0].node            += 5;
  table.sources[0].q.node          += 5;
  ASSERT_EQ(rebind_selection(record, "renamed", net, table, work), Status::feasible);
  EXPECT_EQ(record.module_name, "renamed");
  EXPECT_EQ(record.selected.source.source_graph, "renamed");
  EXPECT_EQ(record.selected.source.sources[0].span.start_line, 77U);
  EXPECT_EQ(record.selected.source.sources[0].node, 47U);
  const auto& logic = record.selected.selected.logic;
  EXPECT_EQ(logic.graph.input_names()[logic.graph.node(logic.inputs[0].id).source_index], "renamed_net");
  // The rebound record is byte-identical to a cold selection of the fresh input.
  auto cold = synthesize_stateful_region(net, table, State_target::cmos, {}, work);
  ASSERT_TRUE(cold.region) << cold.reason;
  EXPECT_EQ(cold.report, report);
  const auto warm_bytes = serialize_artifact("renamed", State_target::cmos, record.selected, work);
  const auto cold_bytes = serialize_artifact("renamed", State_target::cmos, *cold.region, work);
  ASSERT_EQ(warm_bytes.status, Status::feasible) << warm_bytes.reason;
  EXPECT_EQ(warm_bytes.bytes, cold_bytes.bytes);
  // A semantic difference or resource refusal leaves the record unchanged.
  auto other                 = table;
  other.sources[0].neg_clock = true;
  auto copy                  = record;
  EXPECT_EQ(rebind_selection(copy, "other", net, other, work), Status::invalid);
  Budget none{0};
  EXPECT_EQ(rebind_selection(copy, "other", net, table, none), Status::search_exhausted);
  EXPECT_EQ(copy.module_name, "renamed");
  EXPECT_EQ(copy.selected.source.source_graph, "renamed");
}

TEST_F(Artifact, ParallelPhaseOneDependenciesSurviveSerialization) {
  Budget      work{100000000};
  auto        source = region.selected.logic;
  const auto& in     = source.inputs;
  source.state[0].d  = source.graph.land(source.graph.land(in[0], in[1]), source.graph.land(in[2], in[3]));
  auto raw           = export_lnet(source, work);
  ASSERT_TRUE(raw.net);
  auto snapshot = region.source;
  for (uint32_t i = 0; i < source.state.size(); ++i) {
    auto& bit = snapshot.bits[region.state_bits[i]];
    bit.q     = {raw.net->latch(i).q, false};
    bit.d     = {raw.net->latch(i).d, false};
  }
  Logical_options options;
  options.endpoint.gates           = {2, 2, 2};
  options.endpoint.cost.static_and = 20;
  auto selected                    = synthesize_stateful_region(*raw.net, snapshot, State_target::cmos, options, work);
  ASSERT_TRUE(selected.region) << selected.reason;
  const auto saved = serialize_artifact("parallel", State_target::cmos, *selected.region, work);
  ASSERT_EQ(saved.status, Status::feasible) << saved.reason;
  const auto loaded = deserialize_artifact(saved.bytes, State_target::cmos, work);
  ASSERT_TRUE(loaded.region) << loaded.reason;
  const auto& netlist = loaded.region->netlist;
  ASSERT_EQ(netlist.cells.size(), 4U);  // two phase-1 cells and the two original state owners
  const auto& latch = netlist.cells[*netlist.state[0].domino_latch];
  EXPECT_EQ(latch.phase, 2U);
  ASSERT_EQ(latch.inputs.size(), 2U);
  for (const auto& input : latch.inputs) {
    EXPECT_EQ(input.space, Endpoint_ref::Space::cell);
    EXPECT_EQ(netlist.cells[input.index].phase, 1U);
  }
  const auto expanded = expand_endpoint_netlist(netlist, work);
  ASSERT_EQ(expanded.status, Status::feasible) << expanded.reason;
  for (uint32_t x = 0; x < 64; ++x) {
    EXPECT_EQ(evaluate(expanded, x), evaluate(source, x));
  }
  auto emitted = write_logical_module(*loaded.region, work);
  ASSERT_TRUE(emitted.module) << emitted.reason;
  EXPECT_EQ(emitted.module->state.size(), 2U);
  // Cache reconstruction must preserve both parallel producers and the
  // endpoint decision bindings, not just the standalone frozen netlist.
  const auto cached = serialize_selection_record("parallel-key",
                                                 "parallel",
                                                 *selected.region,
                                                 selected.report,
                                                 {123, 123, false, 0},
                                                 selected.report.work.admission,
                                                 work);
  ASSERT_EQ(cached.status, Status::feasible) << cached.reason;
  const auto replay = deserialize_selection_record(cached.bytes, "parallel-key", State_target::cmos, work);
  ASSERT_TRUE(replay.record) << replay.reason;
  EXPECT_EQ(replay.record->report, selected.report);
  const auto restored = serialize_artifact("parallel", State_target::cmos, replay.record->selected, work);
  ASSERT_EQ(restored.status, Status::feasible) << restored.reason;
  EXPECT_EQ(restored.bytes, saved.bytes);
}

TEST_F(Artifact, SharedFirstPhaseSurvivesStandaloneAndSelectionCacheRoundTrips) {
  Budget      work{100000000};
  auto        source = region.selected.logic;
  const auto& in     = source.inputs;
  const auto  shared = source.graph.land(in[0], in[1]);
  source.state[0].d  = source.graph.lor(shared, in[2]);
  source.state[1].d  = source.graph.land(shared, in[3]);
  auto raw           = export_lnet(source, work);
  ASSERT_TRUE(raw.net);
  auto snapshot = region.source;
  for (uint32_t i = 0; i < source.state.size(); ++i) {
    auto& bit = snapshot.bits[region.state_bits[i]];
    bit.q     = {raw.net->latch(i).q, false};
    bit.d     = {raw.net->latch(i).d, false};
  }
  Logical_options options;
  options.endpoint.gates           = {2, 2, 2};
  options.endpoint.cost.static_and = 20;
  auto selected                    = synthesize_stateful_region(*raw.net, snapshot, State_target::cmos, options, work);
  ASSERT_TRUE(selected.region) << selected.reason;
  ASSERT_TRUE(selected.region->frozen);
  ASSERT_EQ(selected.region->frozen->cells.size(), 3U);
  EXPECT_EQ(selected.report.after.domino, 27U);
  const auto saved = serialize_artifact("shared", State_target::cmos, *selected.region, work);
  ASSERT_EQ(saved.status, Status::feasible) << saved.reason;
  const auto loaded = deserialize_artifact(saved.bytes, State_target::cmos, work);
  ASSERT_TRUE(loaded.region) << loaded.reason;
  const auto& n = loaded.region->netlist;
  EXPECT_EQ(n.cells.size(), 3U);
  EXPECT_NE(n.state[0].domino_latch, n.state[1].domino_latch);
  for (const auto& s : n.state) {
    const auto& latch = n.cells[*s.domino_latch];
    EXPECT_EQ(std::count_if(latch.inputs.begin(),
                            latch.inputs.end(),
                            [](const auto& ref) { return ref.space == Endpoint_ref::Space::cell && ref.index == 0; }),
              1);
  }
  auto expanded = expand_endpoint_netlist(n, work);
  ASSERT_EQ(expanded.status, Status::feasible) << expanded.reason;
  for (uint32_t x = 0; x < 64; ++x) {
    EXPECT_EQ(evaluate(expanded, x), evaluate(source, x));
  }
  const auto cached = serialize_selection_record("shared-key",
                                                 "shared",
                                                 *selected.region,
                                                 selected.report,
                                                 {456, 456, false, 0},
                                                 selected.report.work.admission,
                                                 work);
  ASSERT_EQ(cached.status, Status::feasible) << cached.reason;
  const auto replay = deserialize_selection_record(cached.bytes, "shared-key", State_target::cmos, work);
  ASSERT_TRUE(replay.record) << replay.reason;
  EXPECT_EQ(replay.record->report, selected.report);
  const auto restored = serialize_artifact("shared", State_target::cmos, replay.record->selected, work);
  ASSERT_EQ(restored.status, Status::feasible) << restored.reason;
  EXPECT_EQ(restored.bytes, saved.bytes);
}

TEST(RegionArtifact, OutputOnlyAndEmptyRegionsRoundTrip) {
  for (bool output : {false, true}) {
    Lnet net;
    if (output) {
      const auto a = net.add_input("a");
      net.add_output(net.add_lut({a}, Lnet::kNot), "inverse");
    }
    synth::Source_state_table source;
    source.logical_boundary = true;
    Budget     work{1000000};
    const auto selected = synthesize_stateful_region(net, source, State_target::cmos, {}, work);
    ASSERT_TRUE(selected.region) << selected.reason;
    const auto saved = serialize_artifact("empty_or_comb", State_target::cmos, *selected.region, work);
    ASSERT_EQ(saved.status, Status::feasible) << saved.reason;
    const auto loaded = deserialize_artifact(saved.bytes, State_target::cmos, work);
    ASSERT_TRUE(loaded.region) << loaded.reason;
    const auto emitted = write_logical_module(*loaded.region, work);
    ASSERT_TRUE(emitted.module) << emitted.reason;
    EXPECT_TRUE(emitted.module->state.empty());
    EXPECT_EQ(emitted.module->inputs.size(), output ? 1U : 0U);
    EXPECT_EQ(emitted.module->outputs.size(), output ? 1U : 0U);
  }
}

TEST_F(Artifact, EveryTruncationAndSingleByteDamageRefusesWithoutPartialState) {
  Budget     work{100000000};
  const auto saved = serialize_artifact("r", State_target::cmos, region, work);
  ASSERT_EQ(saved.status, Status::feasible) << saved.reason;
  for (size_t i = 0; i < saved.bytes.size(); ++i) {
    SCOPED_TRACE(i);
    Budget     trial{10000000};
    const auto truncated = deserialize_artifact(std::string_view(saved.bytes).substr(0, i), State_target::cmos, trial);
    EXPECT_EQ(truncated.status, Status::invalid);
    EXPECT_FALSE(truncated.region);
    auto bytes          = saved.bytes;
    bytes[i]           ^= 1;
    const auto damaged  = deserialize_artifact(bytes, State_target::cmos, trial);
    EXPECT_EQ(damaged.status, Status::invalid);
    EXPECT_FALSE(damaged.region);
  }
}

TEST_F(Artifact, ValidChecksumsCannotBypassVersionEnumLengthOrSemanticValidation) {
  Budget     work{100000000};
  const auto saved = serialize_artifact("r", State_target::cmos, region, work);
  ASSERT_EQ(saved.status, Status::feasible) << saved.reason;
  // Fixed schema prefix: magic/version, string length and "r", target enum.
  for (const auto [offset, value] : std::vector<std::pair<size_t, uint32_t>>{
           { 0,          0},
           { 4,        999},
           { 8, 0xffffffff},
           {13,          3},
           {17,        999}
  }) {
    auto bytes = saved.bytes;
    put32(bytes, offset, value);
    repair_checksum(bytes);
    const auto loaded = deserialize_artifact(bytes, State_target::cmos, work);
    EXPECT_NE(loaded.status, Status::feasible);
    EXPECT_FALSE(loaded.region);
  }
  auto trailing = saved.bytes;
  trailing.insert(trailing.size() - 8, 1, '\0');
  repair_checksum(trailing);
  EXPECT_EQ(deserialize_artifact(trailing, State_target::cmos, work).status, Status::invalid);
  const auto baseline = deserialize_artifact(saved.bytes, State_target::cmos, work);
  ASSERT_TRUE(baseline.region);
  const auto constant_offset = saved.bytes.find(region.source.sources[0].initial.value.serialize());
  ASSERT_NE(constant_offset, std::string::npos);
  for (const auto [offset, byte] : std::vector<std::pair<size_t, char>>{
           {    constant_offset, char(127)},
           {constant_offset + 1, char(128)},
           {constant_offset + 3,   char(0)}
  }) {
    auto bytes    = saved.bytes;
    bytes[offset] = byte;
    repair_checksum(bytes);
    const auto loaded = deserialize_artifact(bytes, State_target::cmos, work);
    EXPECT_EQ(loaded.status, Status::invalid) << loaded.reason;
    EXPECT_FALSE(loaded.region);
  }
  const std::vector<std::function<void(Frozen_region&)>> corruptions{
      [](auto& r) { r.state_bits[0] = r.state_bits[1]; },
      [](auto& r) { r.source.sources[0].translated_bits = 1; },
      [](auto& r) {
        r.source.sources[0].clock.node = 999;
        r.netlist.state[1].domain      = 1;
      },
      [](auto& r) { r.source.clocks[0].parent = 1; },
      [](auto& r) { r.source.controls[1].outputs = {999, 3}; },
      [](auto& r) { r.source.sources[0].neg_clock = true; },
      [](auto& r) { r.source.sources[0].initial.value = Dlop{}; },
      [](auto& r) {
        r.source.sources[0].span.file = "";
        r.netlist.cells[0].outputs[1] = "lost";
      },
  };
  for (const auto& change : corruptions) {
    auto bad = *baseline.region;
    change(bad);
    const auto refused = serialize_artifact(bad, work);
    EXPECT_EQ(refused.status, Status::invalid) << refused.reason;
    EXPECT_TRUE(refused.bytes.empty());
  }
}

TEST_F(Artifact, CmosArtifactCannotBypassCurrentDominoSourceRestrictions) {
  Budget work{100000000};
  // Construct an ordinary negative-edge CMOS region: it has no converted owners.
  region.source.sources[0].neg_clock = true;
  auto selected                      = synthesize_logical_region(region.selected.logic, {}, {}, work, std::array{0U, 0U});
  ASSERT_TRUE(selected.region);
  region.selected = std::move(*selected.region);
  auto frozen     = freeze_endpoint_netlist(region.selected, {}, work, std::array{0U, 0U});
  ASSERT_TRUE(frozen.netlist);
  region.frozen    = std::move(frozen.netlist);
  const auto saved = serialize_artifact("r", State_target::cmos, region, work);
  ASSERT_EQ(saved.status, Status::feasible) << saved.reason;
  EXPECT_TRUE(deserialize_artifact(saved.bytes, State_target::cmos, work).region);
  // A positive-edge logical endpoint may not silently lose its storage owner.
  region.source.sources[0].neg_clock = false;
  EXPECT_EQ(serialize_artifact("missing_owner", State_target::cmos, region, work).status, Status::invalid);
  region.source.sources[0].neg_clock = true;
  const auto refused                 = deserialize_artifact(saved.bytes, State_target::domino, work);
  EXPECT_EQ(refused.status, Status::invalid);
  EXPECT_FALSE(refused.region);
  ASSERT_FALSE(diag::sink().records().empty());
  EXPECT_EQ(diag::sink().records().back().span.file, "rtl/source.prp");
  EXPECT_EQ(diag::sink().records().back().span.start_line, 12U);
}

TEST_F(Artifact, ByteObjectStringNodeAndLateWorkLimitsAreAtomic) {
  Budget     work{100000000};
  const auto saved = serialize_artifact("r", State_target::cmos, region, work);
  ASSERT_EQ(saved.status, Status::feasible) << saved.reason;
  for (unsigned kind = 0; kind < 5; ++kind) {
    Artifact_limits limits;
    if (kind == 0) {
      limits.bytes = saved.bytes.size() - 1;
    }
    if (kind == 1) {
      limits.objects = 1;
    }
    if (kind == 2) {
      limits.string_bytes = 1;
    }
    if (kind == 3) {
      limits.nodes = 1;
    }
    if (kind == 4) {
      limits.decoded_bytes = 1;
    }
    const auto read = deserialize_artifact(saved.bytes, State_target::cmos, work, limits);
    EXPECT_EQ(read.status, Status::search_exhausted);
    EXPECT_FALSE(read.region);
    const auto write = serialize_artifact("r", State_target::cmos, region, work, limits);
    EXPECT_EQ(write.status, Status::search_exhausted);
    EXPECT_TRUE(write.bytes.empty());
  }
  Budget measured{10000000};
  ASSERT_TRUE(deserialize_artifact(saved.bytes, State_target::cmos, measured).region);
  Budget     late{10000000 - measured.remaining - 1};
  const auto refused = deserialize_artifact(saved.bytes, State_target::cmos, late);
  EXPECT_EQ(refused.status, Status::search_exhausted);
  EXPECT_FALSE(refused.region);
}
}  // namespace livehd::usyn

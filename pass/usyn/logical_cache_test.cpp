// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "logical_cache.hpp"

#include <cstdlib>
#include <fstream>
#include <optional>

#include "gtest/gtest.h"

namespace livehd::usyn {
namespace {
class LogicalCache : public ::testing::Test {
protected:
  std::string               directory;
  Logical_cache_options     cache;
  Logical_options           options;
  synth::Lnet               raw;
  synth::Source_state_table source;
  Stateful_result           selected;
  Credit_floor              search;
  static constexpr uint64_t credits = 10000000;

  void SetUp() override {
    directory = (std::filesystem::temp_directory_path() / "usyn-cache-XXXXXX").string();
    ASSERT_NE(mkdtemp(directory.data()), nullptr);
    cache.directory         = std::filesystem::path(directory) / "entries";
    source.logical_boundary = true;
    source.source_graph     = "source";
    const auto a = raw.add_input("a"), b = raw.add_input("b");
    raw.add_output(raw.add_lut({a, b}, synth::Lnet::kXor2), "y");
    Budget work{credits};
    selected = synthesize_stateful_region(raw, source, synth::State_target::cmos, options, work);
    ASSERT_TRUE(selected.region) << selected.reason;
    search = work.credit_floor();
    ASSERT_EQ(search.work, credits - work.remaining);
    ASSERT_FALSE(search.bound);
  }
  void                TearDown() override { std::filesystem::remove_all(directory); }
  Logical_cache_probe probe(uint64_t with = credits) {
    Budget io{cache.entry_work};
    return probe_logical_cache(cache, "region", raw, source, {}, options, with, io);
  }
  bool save(std::string_view key, const Credit_floor& floor) {
    Budget io{cache.entry_work};
    return store_logical_cache(cache, key, "region", *selected.region, selected.report, floor, selected.report.work.admission, io);
  }
  bool save(std::string_view key) { return save(key, search); }
};

TEST_F(LogicalCache, ValidatedReplayAndIdentityInvalidation) {
  const auto cold = probe();
  EXPECT_EQ(cold.outcome, Cache_lookup::miss);
  ASSERT_EQ(cold.key.size(), 64U);
  ASSERT_TRUE(save(cold.key));
  const auto warm = probe();
  ASSERT_EQ(warm.outcome, Cache_lookup::hit) << warm.reason;
  ASSERT_TRUE(warm.record);
  EXPECT_EQ(warm.record->credit, search);
  EXPECT_EQ(warm.record->report, selected.report);
  ++options.endpoint.gates.stack;
  auto changed = probe();
  EXPECT_EQ(changed.outcome, Cache_lookup::miss);
  EXPECT_NE(changed.key, cold.key);
  --options.endpoint.gates.stack;
  // The source graph is a definition name: provenance, rebound on a hit.
  source.source_graph = "different provenance";
  changed             = probe();
  ASSERT_EQ(changed.outcome, Cache_lookup::hit) << changed.reason;
  EXPECT_EQ(changed.key, cold.key);
  EXPECT_EQ(changed.record->selected.source.source_graph, "different provenance");
  source.source_graph = "source";
  cache.context       = "different admission policy";
  changed             = probe();
  EXPECT_EQ(changed.outcome, Cache_lookup::miss);
  EXPECT_NE(changed.key, cold.key);
  cache.context.clear();
  raw.add_output(raw.inputs()[0].node, "extra");
  changed = probe();
  EXPECT_EQ(changed.outcome, Cache_lookup::miss);
  EXPECT_NE(changed.key, cold.key);
}

// One register, `reg <= in0 ^ b` on clk, as a region translation delivers it.
// Node numbers, spans, the source graph and the first PI spelling are the
// provenance a recompile or rename may change.
struct Stateful_input {
  synth::Lnet               net;
  synth::Source_state_table table;
};
Stateful_input pipeline(std::string reg, std::string first_input, uint64_t base, std::string graph, uint32_t line) {
  Stateful_input s;
  auto&          net   = s.net;
  auto&          table = s.table;
  const auto     a = net.add_input(first_input), b = net.add_input("b_b0"), clk = net.add_input("clk_b0");
  table.source_graph     = std::move(graph);
  table.logical_boundary = true;
  synth::Source_state row;
  row.node            = base;
  row.name            = reg;
  row.bits            = 1;
  row.stages          = 1;
  row.translated_bits = 1;
  row.role            = synth::State_role::register_candidate;
  row.width_known     = true;
  row.q               = {base, 0, true, false, 1, {}};
  row.data            = {base + 1, 0, true, false, 1, {}};
  row.clock           = {base + 2, 3, true, false, 1, {}};
  row.clock_root      = row.clock;
  row.span            = {1, 1, "rtl/" + table.source_graph + ".v", line * 10, line * 10 + 5, line, 1, line, 6};
  table.sources.push_back(row);
  const auto k = net.add_latch(reg + "_%r0_0", 'x');
  net.set_latch_input(k, net.add_lut({a, b}, synth::Lnet::kXor2));
  table.bits.push_back({
      0,
      0,
      0,
      k,
      reg,
      {net.latch(k).q, false},
      {net.latch(k).d, false}
  });
  net.add_output(net.latch(k).q, "__po0_q_b0");
  table.controls.push_back({0, synth::State_control_kind::clock, {1}});
  net.add_output(clk, "__state0_control0_b0");
  return s;
}

TEST_F(LogicalCache, RenamedDefinitionAndBoundaryNetsRebindWhileStateAndTopIoMiss) {
  // PI 0 is an internal boundary net; b and clk are top-level design IO.
  Identity_names names;
  names.inputs   = {"", "b[0]", "clk[0]"};
  names.outputs  = {"q[0]", ""};
  names.ports    = {"input unused"};  // design IO on a port without a logical position
  const auto run = [&](const Stateful_input& in, std::string_view module, const Identity_names& io_names) {
    Budget io{cache.entry_work};
    return probe_logical_cache(cache, module, in.net, in.table, io_names, options, credits, io);
  };
  const auto fresh = [&](const Stateful_input& in, Credit_floor& used) {
    Budget work{credits};
    auto   result = synthesize_stateful_region(in.net, in.table, synth::State_target::cmos, options, work);
    used          = work.credit_floor();
    return result;
  };
  const auto   base = pipeline("state", "a_b0", 42, "top", 3);
  Credit_floor base_work;
  auto         original = fresh(base, base_work);
  ASSERT_TRUE(original.region) << original.reason;
  const auto cold = run(base, "top", names);
  ASSERT_EQ(cold.outcome, Cache_lookup::miss);
  Budget io{cache.entry_work};
  ASSERT_TRUE(store_logical_cache(cache,
                                  cold.key,
                                  "top",
                                  *original.region,
                                  original.report,
                                  base_work,
                                  original.report.work.admission,
                                  io));

  // Recompiled and renamed: other node numbers, spans, source graph, module
  // and internal net spelling. The decision is reused under current names.
  const auto moved = pipeline("state", "t_b0", 900, "top_renamed", 40);
  const auto warm  = run(moved, "top_renamed", names);
  ASSERT_EQ(warm.outcome, Cache_lookup::hit) << warm.reason;
  EXPECT_EQ(warm.key, cold.key);
  const auto& record = *warm.record;
  EXPECT_EQ(record.module_name, "top_renamed");
  EXPECT_EQ(record.selected.source.source_graph, "top_renamed");
  EXPECT_EQ(record.selected.source.sources[0].node, 900U);
  EXPECT_EQ(record.selected.source.sources[0].span.start_line, 40U);
  const auto& frozen = *record.selected.frozen;
  EXPECT_EQ(frozen.native.input_names()[frozen.native.node(frozen.inputs[0].id).source_index], "t_b0");
  // Warm equals cold: the same search, evidence and artifact bytes.
  Credit_floor moved_work;
  const auto   recomputed = fresh(moved, moved_work);
  ASSERT_TRUE(recomputed.region) << recomputed.reason;
  EXPECT_EQ(moved_work, base_work);
  EXPECT_EQ(record.credit, moved_work);
  EXPECT_EQ(record.report, recomputed.report);
  Budget     encode{100000000};
  const auto warm_bytes = serialize_artifact("top_renamed", synth::State_target::cmos, record.selected, encode);
  const auto cold_bytes = serialize_artifact("top_renamed", synth::State_target::cmos, *recomputed.region, encode);
  ASSERT_EQ(warm_bytes.status, Status::feasible) << warm_bytes.reason;
  EXPECT_EQ(warm_bytes.bytes, cold_bytes.bytes);

  // A renamed register or top-level IO is a matched name: regenerate.
  const auto renamed_state = run(pipeline("count", "a_b0", 42, "top", 3), "top", names);
  EXPECT_EQ(renamed_state.outcome, Cache_lookup::miss);
  EXPECT_NE(renamed_state.key, cold.key);
  for (const auto change : {+[](Identity_names& n) { n.inputs[1] = "b2[0]"; },
                            +[](Identity_names& n) { n.outputs[0] = "q2[0]"; },
                            +[](Identity_names& n) { n.ports[0] = "input unused2"; }}) {
    auto renamed = names;
    change(renamed);
    const auto io_rename = run(base, "top", renamed);
    EXPECT_EQ(io_rename.outcome, Cache_lookup::miss);
    EXPECT_NE(io_rename.key, cold.key);
  }
  // Node numbers keep only their equalities: merging two controls is semantic.
  auto merged                       = base;
  merged.table.sources[0].data.node = merged.table.sources[0].clock.node;
  EXPECT_NE(run(merged, "top", names).key, cold.key);

  // A PI spelling equal to the register name changes the imported state name.
  // The key cannot see it; rebinding refuses rather than emit a stale name.
  const auto colliding = run(pipeline("state", "state", 42, "top", 3), "top", names);
  EXPECT_EQ(colliding.key, cold.key);
  EXPECT_EQ(colliding.outcome, Cache_lookup::invalid);
  EXPECT_FALSE(colliding.record);
}

TEST_F(LogicalCache, CorruptionAndResourceRefusalDoNotPublishPartialEntries) {
  const auto cold = probe();
  ASSERT_TRUE(save(cold.key));
  const auto path = cache.directory / (cold.key + ".usyn-cache");
  std::ofstream(path, std::ios::binary) << "partial";
  EXPECT_EQ(probe().outcome, Cache_lookup::invalid);
  ASSERT_TRUE(save(cold.key));
  EXPECT_EQ(probe().outcome, Cache_lookup::hit);
  cache.limits.bytes = 1;
  EXPECT_EQ(probe().outcome, Cache_lookup::refused);
  EXPECT_FALSE(save(cold.key));
  cache.limits     = {};
  cache.entry_work = 0;
  EXPECT_EQ(probe().outcome, Cache_lookup::refused);
  EXPECT_FALSE(save(cold.key));
  cache.entry_work = 1000000000;
  EXPECT_EQ(probe().outcome, Cache_lookup::hit);
  size_t count = 0;
  for (const auto& entry : std::filesystem::directory_iterator(cache.directory)) {
    EXPECT_EQ(entry.path(), path);
    ++count;
  }
  EXPECT_EQ(count, 1U);
  cache.directory = std::filesystem::path(directory) / "regular-file";
  std::ofstream(cache.directory) << "not a directory";
  EXPECT_FALSE(save(cold.key));
}

TEST_F(LogicalCache, DisabledCacheDoesNoWorkAndCancellationIsObserved) {
  const auto cold = probe();
  cache.directory.clear();
  Budget     none{0};
  const auto disabled = probe_logical_cache(cache, "region", raw, source, {}, options, credits, none);
  EXPECT_EQ(disabled.outcome, Cache_lookup::miss);
  EXPECT_TRUE(disabled.key.empty());
  EXPECT_FALSE(none.exhausted);
  EXPECT_FALSE(save(cold.key));
  cache.directory = std::filesystem::path(directory) / "entries";
  EXPECT_FALSE(std::filesystem::exists(cache.directory));
  Budget io{cache.entry_work};
  io.admission          = [] { return false; };
  io.admission_interval = 1;
  EXPECT_EQ(probe_logical_cache(cache, "region", raw, source, {}, options, credits, io).outcome, Cache_lookup::refused);
  EXPECT_TRUE(io.resource_exhausted);
  EXPECT_FALSE(std::filesystem::exists(cache.directory));
}

// The key excludes credits; the stored credit floor decides reuse. An unbound
// record replays under any credits at or above its floor, a bound one only
// under its exact credits; anything else is a credit miss (regenerate and
// overwrite). A replay under the floor's credits is the identical search.
TEST_F(LogicalCache, CreditFloorDecidesReuseUnderAKeyWithoutCredits) {
  const auto cold = probe();
  ASSERT_TRUE(save(cold.key));
  EXPECT_EQ(probe(credits / 3).key, cold.key);
  ASSERT_GT(search.floor, 0U);
  ASSERT_LE(search.floor, credits);
  for (const auto with : {search.floor, credits, 7 * credits}) {
    const auto hit = probe(with);
    ASSERT_EQ(hit.outcome, Cache_lookup::hit) << with << ": " << hit.reason;
    EXPECT_EQ(hit.record->credit, search);
  }
  const auto below = probe(search.floor - 1);
  EXPECT_EQ(below.outcome, Cache_lookup::credit_miss);
  EXPECT_EQ(below.key, cold.key);
  EXPECT_FALSE(below.record);
  ASSERT_TRUE(below.stored);
  EXPECT_EQ(*below.stored, search);
  // The recorded floor really reproduces the search.
  Budget at_floor{search.floor};
  auto   again = synthesize_stateful_region(raw, source, synth::State_target::cmos, options, at_floor);
  ASSERT_TRUE(again.region) << again.reason;
  EXPECT_EQ(again.report, selected.report);
  EXPECT_EQ(at_floor.credit_floor(), search);

  // Starve the search until it depends on its exact credits, and overwrite.
  std::optional<uint64_t> starved;
  for (uint64_t with = search.work - 1; with > search.work / 4 && !starved; with = with * 9 / 10) {
    Budget work{with};
    auto   result = synthesize_stateful_region(raw, source, synth::State_target::cmos, options, work);
    if (result.region && work.bound) {
      starved = with;
      Budget io{cache.entry_work};
      ASSERT_TRUE(store_logical_cache(cache,
                                      cold.key,
                                      "region",
                                      *result.region,
                                      result.report,
                                      work.credit_floor(),
                                      result.report.work.admission,
                                      io));
      EXPECT_EQ(work.credit_floor().credits, with);
    }
  }
  ASSERT_TRUE(starved);
  EXPECT_EQ(probe(*starved).outcome, Cache_lookup::hit);
  EXPECT_EQ(probe(*starved + 1).outcome, Cache_lookup::credit_miss);
  EXPECT_EQ(probe(credits).outcome, Cache_lookup::credit_miss);
  EXPECT_EQ(probe(*starved - 1).outcome, Cache_lookup::credit_miss);
}

// A credit miss keeps the more general record: an unbound record (any credits
// from its floor up) is never replaced by a bound one (only its exact credits);
// every other fresh record replaces what the probe found.
TEST_F(LogicalCache, AnUnboundRecordIsNeverReplacedByABoundOne) {
  const Credit_floor  unbound{10, 20, false, 0}, bound{10, 15, true, 17}, other_bound{10, 30, true, 40};
  Logical_cache_probe missed;
  EXPECT_TRUE(replaces_stored_record(missed, bound));  // nothing stored
  missed.outcome = Cache_lookup::invalid;
  EXPECT_TRUE(replaces_stored_record(missed, bound));
  missed.outcome = Cache_lookup::credit_miss;
  missed.stored  = unbound;
  EXPECT_FALSE(replaces_stored_record(missed, bound));
  missed.stored = bound;
  EXPECT_TRUE(replaces_stored_record(missed, unbound));
  EXPECT_TRUE(replaces_stored_record(missed, other_bound));  // latest wins among bound records

  // End to end: starve the region below the stored unbound floor.
  const auto cold = probe();
  ASSERT_TRUE(save(cold.key));
  std::optional<Stateful_result> starved;
  Credit_floor                   fresh;
  for (uint64_t with = search.floor - 1; with > search.work / 4 && !starved; with = with * 9 / 10) {
    const auto missed_probe = probe(with);
    ASSERT_EQ(missed_probe.outcome, Cache_lookup::credit_miss) << with;
    Budget work{with};
    auto   result = synthesize_stateful_region(raw, source, synth::State_target::cmos, options, work);
    ASSERT_TRUE(result.region) << result.reason;
    fresh = work.credit_floor();
    // Below an unbound floor the re-search is always bound.
    ASSERT_TRUE(fresh.bound) << with;
    EXPECT_FALSE(replaces_stored_record(missed_probe, fresh));
    starved = std::move(result);
  }
  ASSERT_TRUE(starved);
  // The unbound record stayed: the original credits still hit.
  const auto again = probe();
  ASSERT_EQ(again.outcome, Cache_lookup::hit) << again.reason;
  EXPECT_EQ(again.record->credit, search);
}
}  // namespace
}  // namespace livehd::usyn

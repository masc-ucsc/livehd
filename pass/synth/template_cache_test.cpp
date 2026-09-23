// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "template_cache.hpp"

#include <cstdlib>
#include <fstream>

#include "gtest/gtest.h"

namespace livehd::synth {
namespace {
class Counting_backend final : public Tmap_backend {
public:
  uint64_t        calls  = 0;
  Map_status      status = Map_status::mapped;
  Mapped_fragment map(const Mapping_request& request) override {
    ++calls;
    Mapped_fragment f;
    f.status = status;
    f.inputs = request.inputs.size();
    f.output = f.inputs;
    f.area   = 1;
    f.cells  = {
        {"AND2", {"a", "b"}, "y", {0, 1}, "11 1\n", 1}
    };
    f.input_loads_ff     = {2, 3};
    f.output_arrival_ps  = 10;
    f.output_environment = {"y", "AND2", 8};
    return f;
  }
};

Mapping_request request() {
  Mapping_request r;
  r.library = "cells.lib";
  r.inputs  = {
      {"a", "BUF", 1},
      {"b", "BUF", 2}
  };
  r.terms = {
      {0, 1}
  };
  r.output_load_ff = 3;
  r.required_ps    = 100;
  return r;
}

class Templates : public ::testing::Test {
protected:
  std::string directory;
  void        SetUp() override {
    directory = (std::filesystem::temp_directory_path() / "livehd-templates-XXXXXX").string();
    ASSERT_NE(mkdtemp(directory.data()), nullptr);
  }
  void TearDown() override { std::filesystem::remove_all(directory); }
};

TEST_F(Templates, PersistentExactKeysCoverEveryEnvironmentField) {
  Counting_backend backend;
  Template_cache   cold(backend, "backend/library version");
  auto             r     = request();
  auto             first = cold.map(r);
  ASSERT_EQ(first.status, Map_status::mapped);
  first.cells[0].name = "caller mutation";
  auto hit            = cold.map(r);
  EXPECT_EQ(hit.cells[0].name, "AND2");
  EXPECT_EQ(hit.input_loads_ff, (std::vector<double>{2, 3}));
  EXPECT_DOUBLE_EQ(hit.output_environment.arrival_ps, 8);
  EXPECT_EQ(backend.calls, 1);
  const auto file = directory + "/templates";
  ASSERT_TRUE(cold.save(file));
  Template_cache warm(backend, "backend/library version");
  ASSERT_TRUE(warm.load(file));
  EXPECT_EQ(warm.statistics().loaded, 1);
  EXPECT_EQ(warm.map(r).status, Map_status::mapped);
  EXPECT_EQ(backend.calls, 1);
  const std::vector<std::function<void(Mapping_request&)>> changes{[](auto& x) { x.library += "2"; },
                                                                   [](auto& x) { std::swap(x.inputs[0], x.inputs[1]); },
                                                                   [](auto& x) { x.inputs[0].name = "other"; },
                                                                   [](auto& x) { x.inputs[0].driving_cell = "BUF2"; },
                                                                   [](auto& x) { x.inputs[0].arrival_ps += 0.001; },
                                                                   [](auto& x) { x.output_load_ff += 0.001; },
                                                                   [](auto& x) { x.required_ps += 0.001; },
                                                                   [](auto& x) { x.proof_conflicts += 1; },
                                                                   [](auto& x) { x.max_cells += 1; },
                                                                   [](auto& x) { x.terms = {{0}, {1}}; },
                                                                   [](auto& x) { x.inverter = true; }};
  for (const auto& change : changes) {
    auto changed = r;
    change(changed);
    const auto calls = backend.calls;
    EXPECT_EQ(warm.map(changed).status, Map_status::mapped);
    EXPECT_EQ(backend.calls, calls + 1);
    EXPECT_EQ(warm.map(changed).status, Map_status::mapped);
    EXPECT_EQ(backend.calls, calls + 1);
  }
  Template_cache version(backend, "new backend/library version");
  EXPECT_FALSE(version.load(file));
  EXPECT_EQ(version.entries(), 0);
  const auto calls = backend.calls;
  EXPECT_EQ(version.map(r).status, Map_status::mapped);
  EXPECT_EQ(backend.calls, calls + 1);
}

TEST_F(Templates, LibraryContentsAndVersionInvalidateEvenAtSamePath) {
  const auto file = directory + "/cells.lib";
  {
    std::ofstream out(file);
    out << "library one";
  }
  const auto one = template_context(file, "abc-v1");
  EXPECT_FALSE(one.empty());
  EXPECT_EQ(one, template_context(file, "abc-v1"));
  EXPECT_NE(one, template_context(file, "abc-v2"));
  {
    std::ofstream out(file);
    out << "library two";
  }
  EXPECT_NE(one, template_context(file, "abc-v1"));
  EXPECT_TRUE(template_context(file + "-missing", "abc-v1").empty());
}

TEST_F(Templates, CorruptTruncatedAndOversizedFilesAreColdMisses) {
  Counting_backend backend;
  Template_cache   cache(backend, "context");
  cache.map(request());
  const auto file = directory + "/templates";
  ASSERT_TRUE(cache.save(file));
  {
    std::fstream corrupt(file, std::ios::in | std::ios::out | std::ios::binary);
    corrupt.seekp(20);
    corrupt.put('!');
  }
  Template_cache cold(backend, "context");
  EXPECT_FALSE(cold.load(file));
  EXPECT_EQ(cold.entries(), 0);
  EXPECT_EQ(cold.map(request()).status, Map_status::mapped);
  EXPECT_EQ(backend.calls, 2);
  std::filesystem::resize_file(file, 7);
  EXPECT_FALSE(cold.load(file));
  // A rejected load does not discard an already verified in-memory entry.
  EXPECT_EQ(cold.entries(), 1);
  std::filesystem::resize_file(file, 64 * 1024 * 1024 + 1);
  EXPECT_FALSE(cold.load(file));
}

TEST_F(Templates, FailureCancellationAndDisabledReuseNeverBecomeHits) {
  Counting_backend backend;
  Template_cache   cache(backend, "context");
  auto             r = request();
  for (auto status : {Map_status::unsupported,
                      Map_status::exhausted,
                      Map_status::proof_inconclusive,
                      Map_status::mismatch,
                      Map_status::invalid}) {
    backend.status = status;
    EXPECT_EQ(cache.map(r).status, status);
    EXPECT_EQ(cache.map(r).status, status);
    EXPECT_EQ(cache.entries(), 0);
  }
  EXPECT_EQ(backend.calls, 10);
  backend.status = Map_status::mapped;
  EXPECT_EQ(cache.map(r).status, Map_status::mapped);
  r.admission = [] { return false; };
  EXPECT_EQ(cache.map(r).status, Map_status::exhausted);
  EXPECT_EQ(cache.statistics().hits, 0);
  EXPECT_EQ(backend.calls, 11);
  unsigned checks = 0;
  r.admission     = [&] { return ++checks < 2; };
  EXPECT_EQ(cache.map(r).status, Map_status::exhausted);
  EXPECT_EQ(cache.statistics().hits, 0);
  EXPECT_EQ(backend.calls, 11);
  r.admission = {};
  EXPECT_EQ(cache.map(r).status, Map_status::mapped);
  EXPECT_EQ(cache.statistics().hits, 1);
  Template_cache disabled(backend, "");
  EXPECT_EQ(disabled.map(r).status, Map_status::mapped);
  EXPECT_EQ(disabled.map(r).status, Map_status::mapped);
  EXPECT_EQ(backend.calls, 13);
  EXPECT_FALSE(disabled.enabled());
  EXPECT_FALSE(disabled.save(directory + "/disabled"));
  EXPECT_FALSE(std::filesystem::exists(directory + "/disabled"));
  Template_cache cancelled(backend, "context");
  checks      = 0;
  r.admission = [&] { return ++checks < 2; };
  EXPECT_EQ(cancelled.map(r).status, Map_status::exhausted);
  EXPECT_EQ(cancelled.entries(), 0);
}

TEST_F(Templates, BoundedRetentionEvictsWithoutChangingMapping) {
  Counting_backend backend;
  Template_cache   measure(backend, "context");
  auto             r = request();
  measure.map(r);
  Template_cache bounded(backend, "context", measure.bytes());
  bounded.map(r);
  r.required_ps += 1;
  bounded.map(r);
  EXPECT_EQ(bounded.entries(), 1);
  EXPECT_EQ(bounded.statistics().evictions, 1);
  EXPECT_LE(bounded.bytes(), measure.bytes());
  EXPECT_EQ(bounded.map(r).status, Map_status::mapped);
  EXPECT_EQ(bounded.statistics().hits, 1);
}

TEST_F(Templates, IdenticalFunctionsInstantiateDistinctWiresAndChargeBothAreas) {
  Unate_network network;
  for (Id i = 0; i < 4; ++i) {
    Unate_node source;
    source.kind   = Node_kind::source;
    source.origin = i;
    network.nodes.push_back(source);
  }
  for (Id i = 0; i < 2; ++i) {
    Unate_node f;
    f.kind   = Node_kind::function;
    f.origin = 4 + i;
    f.ports  = {2 * i, 2 * i + 1};
    f.terms  = {
        {0, 1}
    };
    network.nodes.push_back(f);
  }
  network.outputs = {4, 5, 4};
  Counting_backend backend;
  Template_cache   cache(backend, "context");
  const auto       mapped = map_network(network, cache, {});
  ASSERT_EQ(mapped.status, Map_status::mapped) << mapped.reason;
  EXPECT_EQ(backend.calls, 1);
  EXPECT_EQ(cache.statistics().hits, 1);
  ASSERT_EQ(mapped.cells.size(), 2);
  EXPECT_EQ(mapped.cell_origins, (std::vector<Id>{4, 5}));
  EXPECT_NE(mapped.outputs[0], mapped.outputs[1]);
  EXPECT_EQ(mapped.outputs[0], mapped.outputs[2]);
  EXPECT_DOUBLE_EQ(mapped.area, 2);
  for (unsigned x = 0; x < 16; ++x) {
    std::vector<bool> wires;
    for (auto origin : mapped.source_origins) {
      wires.push_back((x >> origin) & 1);
    }
    for (const auto& cell : mapped.cells) {
      wires.push_back(wires.at(cell.inputs[0]) && wires.at(cell.inputs[1]));
    }
    EXPECT_EQ(wires.at(mapped.outputs[0]), (x & 3) == 3);
    EXPECT_EQ(wires.at(mapped.outputs[1]), (x & 12) == 12);
  }
}
}  // namespace
}  // namespace livehd::synth

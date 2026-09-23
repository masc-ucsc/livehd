// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "abc_tmap.hpp"

#include <cstdlib>
#include <filesystem>
#include <set>

#include "gtest/gtest.h"
#include "template_cache.hpp"
#include "witness.hpp"

namespace livehd::synth {
TEST(AbcTmap, ResourceRefusalDoesNotEnterBackend) {
  Abc_tmap        backend;
  Mapping_request request;
  request.library   = "does-not-exist.lib";
  request.admission = [] { return false; };
  EXPECT_EQ(backend.map(request).status, Map_status::exhausted);
}
namespace {
constexpr auto library = "inou/prp/tests/abc/test.lib";

bool sop_value(const std::string& sop, const std::vector<bool>& inputs) {
  bool   cover    = false;
  bool   negative = false;
  size_t offset   = 0;
  while (offset < sop.size()) {
    bool term = true;
    for (size_t j = 0; j < inputs.size(); ++j) {
      if (sop.at(offset + j) == '0') {
        term &= !inputs[j];
      } else if (sop.at(offset + j) == '1') {
        term &= inputs[j];
      }
    }
    negative  = sop.at(offset + inputs.size() + 1) == '0';
    cover    |= term;
    offset   += inputs.size() + 3;
  }
  return cover != negative;
}

std::vector<bool> evaluate(const Mapped_network& network, uint32_t assignment) {
  std::vector<bool> wires;
  for (auto origin : network.source_origins) {
    wires.push_back(((assignment >> origin) & 1) != 0);
  }
  for (const auto& cell : network.cells) {
    std::vector<bool> inputs;
    for (auto in : cell.inputs) {
      inputs.push_back(wires.at(in));
    }
    wires.push_back(sop_value(cell.cell_sop, inputs));
  }
  std::vector<bool> outputs;
  for (auto out : network.outputs) {
    outputs.push_back(wires.at(out));
  }
  return outputs;
}

TEST(AbcTmap, IndependentFunctionsAndExplicitInverter) {
  Abc_tmap        backend;
  Mapping_request request;
  request.library = library;
  request.inputs  = {
      {"a", {}, 0},
      {"b", {}, 0},
      {"c", {}, 0}
  };
  request.terms = {
      {0, 1},
      {1, 2}
  };
  auto f = backend.map(request);
  ASSERT_EQ(f.status, Map_status::mapped) << f.reason;
  EXPECT_GT(f.area, 0);
  EXPECT_EQ(f.inputs, 3);
  for (uint32_t x = 0; x < 8; ++x) {
    Mapped_network net;
    net.source_origins = {0, 1, 2};
    net.cells          = f.cells;
    net.outputs        = {f.output};
    EXPECT_EQ(evaluate(net, x)[0], ((x & 3) == 3) || ((x & 6) == 6));
  }
  request.inputs.resize(1);
  request.terms.clear();
  request.inverter = true;
  auto inv         = backend.map(request);
  ASSERT_EQ(inv.status, Map_status::mapped) << inv.reason;
  EXPECT_EQ(inv.cells.size(), 1);
  EXPECT_EQ(inv.cells[0].name, "INVx1");
  EXPECT_DOUBLE_EQ(inv.area, 1);
}

TEST(AbcTmap, CancelledCommandSequenceCanReusePrivateSession) {
  Abc_tmap        backend;
  Mapping_request request;
  request.library = library;
  request.inputs  = {
      {"a", {}, 0},
      {"b", {}, 0}
  };
  request.terms = {
      {0, 1}
  };
  unsigned checks   = 0;
  request.admission = [&] { return ++checks < 4; };
  EXPECT_EQ(backend.map(request).status, Map_status::exhausted);
  EXPECT_EQ(checks, 4);
  request.admission = {};
  auto result       = backend.map(request);
  ASSERT_EQ(result.status, Map_status::mapped) << result.reason;
  Mapped_network network;
  network.source_origins = {0, 1};
  network.cells          = result.cells;
  network.outputs        = {result.output};
  for (uint32_t x = 0; x < 4; ++x) {
    EXPECT_EQ(evaluate(network, x)[0], x == 3);
  }
}

TEST(AbcTmap, ConstantFragments) {
  Abc_tmap        backend;
  Mapping_request request;
  request.library = library;
  for (bool one : {false, true}) {
    request.terms = one ? std::vector<std::vector<uint32_t>>{{}} : std::vector<std::vector<uint32_t>>{};
    auto f        = backend.map(request);
    ASSERT_EQ(f.status, Map_status::mapped) << f.reason;
    Mapped_network net;
    net.cells   = f.cells;
    net.outputs = {f.output};
    EXPECT_EQ(evaluate(net, 0)[0], one);
  }
}

TEST(AbcTmap, AlternativeClassEncodingPreservesEveryMappedOutput) {
  Logic_network source;
  for (unsigned i = 0; i < 4; ++i) {
    source.add_source();
  }
  Truth_table any(4), all(4);
  for (uint32_t x = 0; x < 16; ++x) {
    any.set(x, (x & 7) != 0 && (x & 8) != 0);
    all.set(x, (x & 7) == 7 && (x & 8) != 0);
  }
  source.outputs = {source.add_function({0, 1, 2, 3}, any), source.add_function({0, 1, 2, 3}, all)};
  source.outputs.push_back(source.outputs[0]);
  Search_options options;
  options.recipes = {
      {2, 3, 3, 3}
  };
  options.cover_limit = options.divisor_limit = options.joint_limit = options.recovery_rounds = options.reshape_limit = 0;
  options.encoding_pair_limit                                                                                         = 0;
  options.encoding_limit                                                                                              = 1;
  options.encoding_code_limit                                                                                         = 2;
  const auto search = optimize(source, options);
  ASSERT_EQ(search.status, Status::feasible);
  EXPECT_EQ(search.attempts[0].encoding_code_queries, 2);
  if (const auto* artifacts = std::getenv("TEST_UNDECLARED_OUTPUTS_DIR")) {
    Witness_archive archive(std::filesystem::path(artifacts) / "alternative_codes.jsonl", "test", 1000000, options);
    const auto&     attempt = search.attempts[0];
    EXPECT_EQ(archive.append("codes", 0, "initial", source, attempt.network, attempt.recipe).status, "archived");
    archive.close();
  }
  Abc_tmap        backend;
  Mapping_request defaults;
  defaults.library  = library;
  const auto mapped = map_network(search.attempts[0].network, backend, defaults);
  ASSERT_EQ(mapped.status, Map_status::mapped) << mapped.reason;
  ASSERT_EQ(mapped.outputs.size(), 3);
  EXPECT_EQ(mapped.outputs[0], mapped.outputs[2]);
  for (uint32_t x = 0; x < 16; ++x) {
    EXPECT_EQ(evaluate(mapped, x), (std::vector<bool>{any.get(x), all.get(x), any.get(x)}));
  }
}

TEST(AbcTmap, StitchSharedDagAndTwinWithoutDuplicatingOutputs) {
  Logic_network source;
  auto          a = source.add_source(), b = source.add_source(), c = source.add_source(), d = source.add_source();
  Truth_table   and2(2), neg_and(2);
  and2.words[0]    = 8;
  neg_and.words[0] = 4;
  auto shared      = source.add_function({a, b}, and2);
  auto y           = source.add_function({shared, c}, and2);
  auto z           = source.add_function({shared, d}, neg_and);
  source.outputs   = {y, z, y};
  Search_options options;
  options.recipes = {
      {2, 2, 8, 2}
  };
  auto search = optimize(source, options);
  ASSERT_EQ(search.status, Status::feasible);
  Abc_tmap        backend;
  Mapping_request defaults;
  defaults.library = library;
  auto net         = map_network(search.attempts[0].network, backend, defaults);
  ASSERT_EQ(net.status, Map_status::mapped) << net.reason;
  EXPECT_EQ(net.outputs[0], net.outputs[2]);
  EXPECT_EQ(net.cell_origins.size(), net.cells.size());
  double area = 0;
  for (const auto& cell : net.cells) {
    area += cell.area;
  }
  EXPECT_DOUBLE_EQ(net.area, area);
  for (uint32_t x = 0; x < 16; ++x) {
    auto out = evaluate(net, x);
    EXPECT_EQ(out[0], (x & 7) == 7);
    EXPECT_EQ(out[1], ((x & 3) != 3) && (x & 8));
    EXPECT_EQ(out[0], out[2]);
  }
}

TEST(AbcTmap, RefusesUnsupportedEnvironmentAndInvalidPorts) {
  Abc_tmap        backend;
  Mapping_request request;
  request.library = library;
  request.inputs  = {
      {"a", {}, 0}
  };
  request.terms       = {{0}};
  request.required_ps = 100;
  EXPECT_EQ(backend.map(request).status, Map_status::unsupported);
  request.required_ps = -1;
  request.terms       = {{2}};
  EXPECT_EQ(backend.map(request).status, Map_status::invalid);
  request.terms           = {{0}};
  request.proof_conflicts = 0;
  EXPECT_EQ(backend.map(request).status, Map_status::invalid);
}

class Failing_backend final : public Tmap_backend {
public:
  uint32_t        calls = 0;
  Mapped_fragment map(const Mapping_request& request) override {
    if (++calls == 2) {
      return {.status = Map_status::proof_inconclusive, .reason = "injected inconclusive proof"};
    }
    return {.status = Map_status::mapped, .inputs = static_cast<uint32_t>(request.inputs.size()), .output = 0};
  }
};

TEST(AbcTmap, InconclusiveMappingRollsBackAllFragments) {
  Unate_network network;
  Unate_node    source;
  source.kind = Node_kind::source;
  network.nodes.push_back(source);
  for (uint32_t i = 0; i < 2; ++i) {
    Unate_node function;
    function.kind  = Node_kind::function;
    function.ports = {i};
    function.terms = {{0}};
    network.nodes.push_back(function);
  }
  network.outputs = {2};
  Failing_backend backend;
  auto            result = map_network(network, backend, {});
  EXPECT_EQ(result.status, Map_status::proof_inconclusive);
  EXPECT_EQ(backend.calls, 2);
  EXPECT_TRUE(result.cells.empty());
  EXPECT_TRUE(result.outputs.empty());
  EXPECT_EQ(network.nodes.size(), 3);
}
}  // namespace
}  // namespace livehd::synth

namespace livehd::synth {
TEST(AbcTmap, NldmEnvironmentChangesArrivalAndLoadWithoutChangingFunction) {
  Abc_tmap        backend;
  Mapping_request request;
  request.library = "inou/prp/tests/abc/timing.lib";
  request.inputs  = {
      {"a", "BUF x does not exist", 0}
  };
  request.inverter       = true;
  request.required_ps    = 200;
  request.output_load_ff = 1;
  EXPECT_EQ(backend.map(request).status, Map_status::unsupported);
  request.inputs[0].driving_cell = "BUFx1";
  const auto light               = backend.map(request);
  ASSERT_EQ(light.status, Map_status::mapped) << light.reason;
  ASSERT_EQ(light.input_loads_ff.size(), 1);
  EXPECT_GT(light.input_loads_ff[0], 0);
  EXPECT_GT(light.output_arrival_ps, 0);
  request.inputs[0].arrival_ps = 100;
  const auto late              = backend.map(request);
  ASSERT_EQ(late.status, Map_status::mapped) << late.reason;
  EXPECT_NEAR(late.output_arrival_ps - light.output_arrival_ps, 100, 0.01);
  request.output_load_ff = 8;
  const auto loaded      = backend.map(request);
  ASSERT_EQ(loaded.status, Map_status::mapped) << loaded.reason;
  EXPECT_GT(loaded.output_arrival_ps, late.output_arrival_ps);
  for (const auto* fragment : {&light, &late, &loaded}) {
    Mapped_network network;
    network.source_origins = {0};
    network.cells          = fragment->cells;
    network.outputs        = {fragment->output};
    EXPECT_TRUE(evaluate(network, 0)[0]);
    EXPECT_FALSE(evaluate(network, 1)[0]);
  }
}

TEST(AbcTmap, TimedSharedNetworkPropagatesEnvironmentAndProvesEachFragment) {
  Logic_network source;
  auto          a = source.add_source(), b = source.add_source(), c = source.add_source();
  Truth_table   and2(2);
  and2.words[0]  = 8;
  auto shared    = source.add_function({a, b}, and2);
  auto y         = source.add_function({shared, c}, and2);
  source.outputs = {shared, y};
  Search_options options;
  options.recipes = {
      {2, 2, 8, 2}
  };
  auto search = optimize(source, options);
  ASSERT_EQ(search.status, Status::feasible);
  Abc_tmap        backend;
  Mapping_request defaults;
  defaults.library     = "inou/prp/tests/abc/timing.lib";
  defaults.required_ps = 200;
  Network_environment environment;
  environment.inputs = {
      {"a", "BUFx1", 10},
      {"b", "BUFx1", 20},
      {"c", "BUFx1", 30}
  };
  environment.outputs = {
      {2, 0},
      {4, 0}
  };
  auto mapped = map_network(search.attempts[0].network, backend, defaults, &environment);
  ASSERT_EQ(mapped.status, Map_status::mapped) << mapped.reason;
  for (uint32_t x = 0; x < 8; ++x) {
    const auto outputs = evaluate(mapped, x);
    EXPECT_EQ(outputs[0], (x & 3) == 3);
    EXPECT_EQ(outputs[1], x == 7);
  }
}

TEST(AbcTmap, CachedNldmTemplatePreservesTimingAndMissesWhenLoadChanges) {
  Abc_tmap        backend;
  const auto      lib = "inou/prp/tests/abc/timing.lib";
  Template_cache  cache(backend, template_context(lib, "test-abc-v1"));
  Mapping_request request;
  request.library = lib;
  request.inputs  = {
      {"a", "BUFx1", 10}
  };
  request.inverter       = true;
  request.output_load_ff = 1;
  request.required_ps    = 200;
  const auto cold        = cache.map(request);
  ASSERT_EQ(cold.status, Map_status::mapped) << cold.reason;
  const auto hit = cache.map(request);
  ASSERT_EQ(hit.status, Map_status::mapped) << hit.reason;
  EXPECT_EQ(cache.statistics().hits, 1);
  EXPECT_EQ(cache.statistics().misses, 1);
  EXPECT_EQ(hit.input_loads_ff, cold.input_loads_ff);
  EXPECT_DOUBLE_EQ(hit.output_arrival_ps, cold.output_arrival_ps);
  EXPECT_DOUBLE_EQ(hit.output_environment.arrival_ps, cold.output_environment.arrival_ps);
  request.output_load_ff = 8;
  const auto loaded      = cache.map(request);
  ASSERT_EQ(loaded.status, Map_status::mapped) << loaded.reason;
  EXPECT_EQ(cache.statistics().misses, 2);
  EXPECT_GT(loaded.output_arrival_ps, hit.output_arrival_ps);
  for (const auto* fragment : {&cold, &hit, &loaded}) {
    Mapped_network network;
    network.source_origins = {0};
    network.cells          = fragment->cells;
    network.outputs        = {fragment->output};
    EXPECT_TRUE(evaluate(network, 0)[0]);
    EXPECT_FALSE(evaluate(network, 1)[0]);
  }
}
}  // namespace livehd::synth

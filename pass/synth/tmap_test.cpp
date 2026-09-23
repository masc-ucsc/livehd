// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "tmap.hpp"

#include "gtest/gtest.h"

namespace livehd::synth {
namespace {
class Recording_backend final : public Tmap_backend {
public:
  std::vector<Mapping_request> requests;
  Mapped_fragment              map(const Mapping_request& request) override {
    requests.push_back(request);
    Mapped_fragment fragment;
    fragment.status            = Map_status::mapped;
    fragment.inputs            = request.inputs.size();
    fragment.output            = 0;
    fragment.output_arrival_ps = 42;
    fragment.input_loads_ff.resize(request.inputs.size(), 3);
    fragment.output_environment = {"", "actual_driver", 35};
    return fragment;
  }
};

TEST(Tmap, SharedProducerReceivesAllSinkLoadsAndKeepsSourcePortOrder) {
  Unate_network network;
  Unate_node    a, b, shared, left, right;
  a.kind = b.kind = Node_kind::source;
  a.origin        = 1;
  b.origin        = 0;
  shared.kind = left.kind = right.kind = Node_kind::function;
  shared.ports                         = {0, 1};
  shared.terms                         = {
      {0, 1}
  };
  shared.level = 1;
  left.ports = right.ports = {2};
  left.terms = right.terms = {{0}};
  left.level = right.level = 2;
  network.nodes            = {a, b, shared, left, right};
  network.outputs          = {2, 3, 4};
  network.depth            = 2;
  Network_environment environment;
  environment.inputs = {
      {"b", "driver_b", 20},
      {"a", "driver_a", 10}
  };
  environment.outputs = {
      { 5,  0},
      { 7,  0},
      {11, 30}
  };
  Mapping_request defaults;
  defaults.required_ps = 100;
  Recording_backend backend;
  auto              mapped = map_network(network, backend, defaults, &environment);
  ASSERT_EQ(mapped.status, Map_status::mapped) << mapped.reason;
  ASSERT_EQ(backend.requests.size(), 6);
  const auto& shared_request = backend.requests[3];
  // Two distinct consuming ports each load the producer by 3 fF, plus its
  // direct external reader at 5 fF. Neither reader may disappear via sharing.
  EXPECT_DOUBLE_EQ(shared_request.output_load_ff, 11);
  EXPECT_EQ(shared_request.inputs[0].driving_cell, "driver_a");
  EXPECT_EQ(shared_request.inputs[1].driving_cell, "driver_b");
  EXPECT_DOUBLE_EQ(shared_request.inputs[0].arrival_ps, 10);
  EXPECT_DOUBLE_EQ(shared_request.required_ps, 50);
  EXPECT_EQ(backend.requests[4].inputs[0].driving_cell, "actual_driver");
  EXPECT_DOUBLE_EQ(backend.requests[4].inputs[0].arrival_ps, 35);
  EXPECT_DOUBLE_EQ(backend.requests[4].output_load_ff, 7);
  EXPECT_DOUBLE_EQ(backend.requests[5].required_ps, 70);
}

TEST(Tmap, CancellationDiscardsPartialMappingAndDoesNotCallNextFunction) {
  Unate_network network;
  Unate_node    source, first, second;
  source.kind = Node_kind::source;
  first.kind = second.kind = Node_kind::function;
  first.ports              = {0};
  second.ports             = {1};
  first.terms = second.terms = {{0}};
  network.nodes              = {source, first, second};
  network.outputs            = {2};
  Recording_backend backend;
  Mapping_request   request;
  // The first backend call consumes the shared allowance. The partial result
  // must be discarded and the second function must never reach the backend.
  request.admission = [&] { return backend.requests.empty(); };
  const auto result = map_network(network, backend, request);
  EXPECT_EQ(result.status, Map_status::exhausted);
  EXPECT_TRUE(result.cells.empty());
  EXPECT_TRUE(result.outputs.empty());
  EXPECT_EQ(backend.requests.size(), 1);
}
}  // namespace
}  // namespace livehd::synth

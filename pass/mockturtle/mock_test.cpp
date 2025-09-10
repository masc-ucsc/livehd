#include <cassert>
#include <iostream>

#include "mockturtle/algorithms/cleanup.hpp"
#include "mockturtle/networks/mig.hpp"
#include "perf_tracing.hpp"

using namespace mockturtle;

void synthesize(mig_network& net) {
  // Simple cleanup only
  net = cleanup_dangling(net);
}

int tmap(mig_network& net) {
  // Simplified version
  int gate_count = net.num_gates();
  std::cout << "size: " << gate_count << std::endl;
  return gate_count;
}

void mock_test_or(int net_size) {
  TRACE_EVENT("pass", nullptr, [net_size](perfetto::EventContext ctx) {
    ctx.event()->set_name("MOCKTURTLE_mock_test_or" + std::to_string(net_size));
  });

  mig_network net;

  std::vector<mig_network::signal> array;
  std::vector<mig_network::signal> array2;
  array2.resize(net_size);

  for (int i = 0; i < net_size; i++) {
    array.emplace_back(net.create_pi());
  }

  array2[0] = net.create_or(array[0], array[1]);
  for (int i = 1; i < net_size; i++) {
    array2[i] = net.create_or(array[0], array2[i - 1]);
  }

  net.create_po(array2[net_size - 1]);

  synthesize(net);

  int sz = tmap(net);
  (void)sz;
}

void mock_test_xor(int net_size) {
  TRACE_EVENT("pass", nullptr, [net_size](perfetto::EventContext ctx) {
    ctx.event()->set_name("MOCKTURTLE_mock_test_xor" + std::to_string(net_size));
  });

  mig_network net;

  std::vector<mig_network::signal> array;
  std::vector<mig_network::signal> array2;
  array2.resize(net_size);

  for (int i = 0; i < net_size; i++) {
    array.emplace_back(net.create_pi());
  }

  array2[0] = net.create_xor(array[0], array[1]);
  for (int i = 1; i < net_size; i++) {
    array2[i] = net.create_xor(array[i], array2[i - 1]);
  }

  net.create_po(array2[net_size - 1]);

  synthesize(net);

  int sz = tmap(net);
  (void)sz;
}

int main() {
  // Use a smaller test size for faster testing
  int test_size = 64;

  mock_test_or(test_size);
  std::cout << "OR test complete, network size: " << test_size << std::endl;

  mock_test_xor(test_size);
  std::cout << "XOR test complete, network size: " << test_size << std::endl;

  return 0;
}

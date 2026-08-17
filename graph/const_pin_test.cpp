// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <format>
#include <string>
#include <thread>
#include <vector>

#include "graph_library_singleton.hpp"
#include "gtest/gtest.h"
#include "hhds/graph.hpp"
#include "hlop/dlop.hpp"
#include "node_util.hpp"

namespace gu = livehd::graph_util;

TEST(ConstPin, HydrationCacheSurvivesGrowthWithWideValues) {
  // Run in a short-lived thread so the test also exercises the TLS lifetime
  // boundary which a large Backend compile reaches during process teardown.
  std::thread worker([] {
    auto& lib = livehd::Hhds_graph_library::instance("lgdb_const_pin_cache_growth");
    auto  gio = lib.create_io("wide_constants");
    auto  g   = gio->create_graph();

    constexpr int                count = 32768;
    std::vector<hhds::Pin_class> pins;
    std::vector<std::string>     serialized;
    pins.reserve(count);
    serialized.reserve(count);

    // Every value needs more than one 64-bit word. This forces the hydration
    // cache to retain owning Dlops while its table repeatedly grows.
    for (int i = 0; i < count; ++i) {
      auto value = Dlop::from_pyrope(std::format("0x1{:016x}", i));
      ASSERT_TRUE(value);
      serialized.push_back(value->serialize());
      pins.push_back(gu::create_const(*g, *value));
    }

    for (size_t i = 0; i < pins.size(); ++i) {
      EXPECT_EQ(gu::hydrate_const(pins[i]).serialize(), serialized[i]);
    }
    // Hits after the final growth must still return the same owned values.
    for (size_t i = pins.size(); i-- > 0;) {
      EXPECT_EQ(gu::hydrate_const(pins[i]).serialize(), serialized[i]);
    }
  });
  worker.join();
}

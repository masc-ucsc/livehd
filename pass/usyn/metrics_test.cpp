// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "metrics.hpp"

#include "gtest/gtest.h"
#include "rapidjson/document.h"

namespace livehd::usyn {
TEST(Metrics, EndpointOccurrencesExcludeOnlyDirectSourcesAndSourceFreeConstants) {
  using livehd::synth::Lnet;
  Lnet       g;
  const auto a        = g.add_input("a");
  const auto b        = g.add_input("b");
  const auto logic    = g.add_lut({a, b}, Lnet::kAnd2);
  const auto constant = g.add_constant(true);
  const auto unused   = g.add_input("unused");
  (void)g.add_lut({unused, unused}, Lnet::kAnd2);
  g.add_output(a, "a");
  g.add_output(logic, "l0");
  g.add_output(logic, "l1");
  g.add_output(constant, "k");
  rapidjson::Document doc;
  doc.Parse(source_metrics_json(g).c_str());
  ASSERT_FALSE(doc.HasParseError());
  EXPECT_EQ(doc["outputs"].GetUint(), 4);
  EXPECT_EQ(doc["unique_outputs"].GetUint(), 3);
  EXPECT_EQ(doc["direct_source_outputs"].GetUint(), 1);
  EXPECT_EQ(doc["source_free_outputs"].GetUint(), 1);
  EXPECT_EQ(doc["logic_outputs"].GetUint(), 2);
  EXPECT_EQ(doc["unique_logic_outputs"].GetUint(), 1);
  EXPECT_EQ(doc["reachable_source_nodes"].GetUint(), 2);
  EXPECT_EQ(doc["reachable_function_nodes"].GetUint(), 2);
}
}  // namespace livehd::usyn

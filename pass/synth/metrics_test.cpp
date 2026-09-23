// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "metrics.hpp"

#include "gtest/gtest.h"
#include "rapidjson/document.h"

namespace livehd::synth {
TEST(Metrics, EndpointOccurrencesExcludeOnlyDirectSourcesAndSourceFreeConstants) {
  Logic_network g;
  const auto    a = g.add_source(), b = g.add_source();
  Truth_table   both(2);
  both.set(3, true);
  const auto logic    = g.add_function({a, b}, both);
  const auto constant = g.add_function({}, Truth_table(0, true));
  const auto unused   = g.add_source();
  g.add_function({unused, unused}, both);
  g.outputs = {a, logic, logic, constant};
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
TEST(Metrics, CountsSharedReadsTwinsAndUnreachableProducersSeparately) {
  Unate_network net;
  Unate_node    source;
  source.kind = Node_kind::source;
  Unate_node positive;
  positive.kind     = Node_kind::function;
  positive.origin   = 1;
  positive.ports    = {0};
  positive.terms    = {{0}};
  auto negative     = positive;
  negative.negative = true;
  Unate_node root;
  root.kind   = Node_kind::function;
  root.origin = 2;
  root.ports  = {1, 2};
  root.terms  = {
      {0, 1}
  };
  auto unused   = positive;
  unused.origin = 3;
  net.nodes     = {source, positive, negative, root, unused};
  net.outputs   = {1, 3, 3};
  rapidjson::Document doc;
  doc.Parse(network_metrics_json(net).c_str());
  ASSERT_FALSE(doc.HasParseError());
  EXPECT_EQ(doc["function_producers"].GetUint(), 4);
  EXPECT_EQ(doc["twin_pairs"].GetUint(), 1);
  EXPECT_EQ(doc["shared_function_producers"].GetUint(), 2);
  EXPECT_EQ(doc["function_reads"].GetUint(), 5);
  EXPECT_EQ(doc["endpoint_reads"].GetUint(), 3);
  EXPECT_EQ(doc["literal_occurrences"].GetUint(), 5);
  EXPECT_EQ(doc["function_ports"].GetUint(), 5);
  EXPECT_EQ(doc["reachable_function_producers"].GetUint(), 3);
  EXPECT_EQ(doc["unused_function_producers"].GetUint(), 1);
}
}  // namespace livehd::synth

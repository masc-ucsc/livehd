// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "cmos_cleanup.hpp"

#include "gtest/gtest.h"

namespace livehd::usyn {
namespace {
uint16_t truth_word(const Xag& graph, Xsignal root) {
  std::vector<uint16_t>             words(graph.size());
  constexpr std::array<uint16_t, 4> variables{0xAAAA, 0xCCCC, 0xF0F0, 0xFF00};
  const auto value = [&](Xsignal s) { return static_cast<uint16_t>(words[s.id] ^ (s.inverted ? 65535 : 0)); };
  for (Id id = 1; id < graph.size(); ++id) {
    const auto& node = graph.node(id);
    if (node.kind == Xag::Kind::source) {
      words[id] = variables[node.source_index];
    } else if (node.kind == Xag::Kind::and_gate) {
      words[id] = value(node.inputs[0]) & value(node.inputs[1]);
    } else if (node.kind == Xag::Kind::xor_gate) {
      words[id] = value(node.inputs[0]) ^ value(node.inputs[1]);
    }
  }
  return value(root);
}
Xag_region expansion() {
  Xag_region region;
  const auto a = region.graph.input("a"), b = region.graph.input("b"), clk = region.graph.input("clk");
  const auto q    = region.graph.input("top.register[2]");
  const auto root = region.graph.land(region.graph.lor(a, b), region.graph.lor(a, q));
  region.inputs   = {a, b, clk};
  region.state.push_back({"top.register[2]", '1', q, root});
  region.outputs = {
      {          "out",  root},
      {        "out_n", ~root},
      {"clock_control",   clk}
  };
  region.status = Status::feasible;
  return region;
}
}  // namespace

TEST(CmosCleanup, PreservesStateControlsPortsAndIncumbentWhileOptimizingExpandedLogic) {
  const auto       source = expansion();
  const auto       size   = source.graph.size();
  Residual_options options;
  options.npn4 = options.sweep = options.balance = true;
  Budget     work{10000000};
  const auto result = clean_cmos_expansion(source, options, work);
  ASSERT_EQ(result.status, Status::feasible) << result.reason;
  ASSERT_TRUE(result.region);
  const auto& region = *result.region;
  EXPECT_EQ(source.graph.size(), size);
  EXPECT_EQ(source.graph.input_names(), region.graph.input_names());
  ASSERT_EQ(source.state.size(), region.state.size());
  ASSERT_EQ(source.outputs.size(), region.outputs.size());
  EXPECT_EQ(source.inputs, region.inputs);
  EXPECT_EQ(source.state[0].name, region.state[0].name);
  EXPECT_EQ(source.state[0].init, region.state[0].init);
  EXPECT_EQ(source.state[0].q, region.state[0].q);
  EXPECT_EQ(truth_word(source.graph, source.state[0].d), truth_word(region.graph, region.state[0].d));
  for (size_t i = 0; i < source.outputs.size(); ++i) {
    EXPECT_EQ(source.outputs[i].name, region.outputs[i].name);
    EXPECT_EQ(truth_word(source.graph, source.outputs[i].signal), truth_word(region.graph, region.outputs[i].signal));
  }
  EXPECT_LT(result.report.cost_after, result.report.cost_before);
  EXPECT_EQ(region.outputs[0].signal, ~region.outputs[1].signal);
  Budget     emission{100000};
  const auto emitted = export_lnet(region, emission);
  ASSERT_TRUE(emitted.net);
  EXPECT_EQ(emitted.net->latches().size(), 1U);
  EXPECT_EQ(emitted.net->latch(0).name, "top.register[2]");
}

TEST(CmosCleanup, RefusedSearchAndCancellationRetainTheOwnersCompleteExpansion) {
  const auto       source = expansion();
  Residual_options options;
  options.max_nodes = 1;
  Budget     work{100000};
  const auto refused = clean_cmos_expansion(source, options, work);
  EXPECT_EQ(refused.status, Status::search_exhausted);
  EXPECT_FALSE(refused.region);
  EXPECT_EQ(source.state[0].name, "top.register[2]");
  options.max_nodes = 100;
  Budget cancelled{100000};
  cancelled.admission = [] { return false; };
  const auto stopped  = clean_cmos_expansion(source, options, cancelled);
  EXPECT_EQ(stopped.status, Status::search_exhausted);
  EXPECT_TRUE(cancelled.resource_exhausted);
  EXPECT_FALSE(stopped.region);
  EXPECT_EQ(source.outputs.size(), 3U);
}
}  // namespace livehd::usyn

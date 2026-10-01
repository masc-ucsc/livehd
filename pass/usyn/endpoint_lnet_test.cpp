// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "endpoint_lnet.hpp"

#include "gtest/gtest.h"

namespace livehd::usyn {
namespace {
std::vector<bool> evaluate(const synth::Lnet& net, uint32_t assignment) {
  std::vector<bool> values(net.size(), false);
  for (synth::Lid id = 0; id < net.size(); ++id) {
    if (net.kind(id) == synth::Lnet::Kind::source) {
      const auto index = net.source_index(id) + (net.is_latch_source(id) ? net.inputs().size() : 0);
      values[id]       = (assignment >> index) & 1;
    } else {
      uint32_t input = 0;
      for (uint32_t j = 0; j < net.fanin_count(id); ++j) {
        input |= uint32_t{values[net.fanin(id, j)]} << j;
      }
      values[id] = net.eval(id, input);
    }
  }
  std::vector<bool> outputs;
  for (const auto& po : net.outputs()) {
    outputs.push_back(values[po.node]);
  }
  for (const auto& state : net.latches()) {
    outputs.push_back(values[state.d]);
  }
  return outputs;
}

void check_state_and_functions(const synth::Lnet& before, const synth::Lnet& after) {
  ASSERT_EQ(before.inputs().size(), after.inputs().size());
  ASSERT_EQ(before.outputs().size(), after.outputs().size());
  ASSERT_EQ(before.latches().size(), after.latches().size());
  for (size_t i = 0; i < before.inputs().size(); ++i) {
    EXPECT_EQ(before.inputs()[i].name, after.inputs()[i].name);
  }
  for (size_t i = 0; i < before.outputs().size(); ++i) {
    EXPECT_EQ(before.outputs()[i].name, after.outputs()[i].name);
  }
  for (size_t i = 0; i < before.latches().size(); ++i) {
    EXPECT_EQ(before.latches()[i].name, after.latches()[i].name);
    EXPECT_EQ(before.latches()[i].init, after.latches()[i].init);
  }
  const auto sources = before.inputs().size() + before.latches().size();
  ASSERT_LE(sources, 10U);
  for (uint32_t x = 0; x < (1U << sources); ++x) {
    ASSERT_EQ(evaluate(before, x), evaluate(after, x)) << x;
  }
}
}  // namespace

TEST(EndpointLnet, OneCellExpansionPreservesOriginalAndUnselectedState) {
  using synth::Lnet;
  Lnet       net;
  const auto a = net.add_input("a"), b = net.add_input("b"), c = net.add_input("c");
  const auto r = net.add_latch("pipe.r[0]", '0'), s = net.add_latch("special", '1');
  const auto ab = net.add_lut({a, b}, Lnet::kOr2), ac = net.add_lut({a, c}, Lnet::kOr2);
  net.set_latch_input(r, net.add_lut({ab, ac}, Lnet::kAnd2));
  net.set_latch_input(s, net.add_lut({a, net.latch(r).q}, Lnet::kXor2));
  const auto nq = net.add_lut({net.latch(r).q}, Lnet::kNot);
  net.add_output(net.latch(r).q, "q");
  net.add_output(nq, "nq");
  net.add_output(nq, "another_nq");
  net.add_output(net.latch(s).q, "special_q");
  Budget     work{10000000};
  const auto region = import_lnet(net, work);
  ASSERT_EQ(region.status, Status::feasible);
  const std::array protected_roots{region.state[s].d};
  const auto       selected = select_endpoint(region.graph, region.state[r].d, region.state[r].name, protected_roots, {}, {}, work);
  ASSERT_TRUE(selected.selected);
  const std::array endpoints{
      Cmos_endpoint{r, *selected.selected}
  };
  const auto expanded = expand_cmos(region, endpoints, {}, work);
  ASSERT_EQ(expanded.status, Status::feasible) << expanded.reason;
  ASSERT_TRUE(expanded.net);
  check_state_and_functions(net, *expanded.net);
  EXPECT_EQ(expanded.net->outputs()[1].node, expanded.net->outputs()[2].node);
}

TEST(EndpointLnet, ParallelFirstPhaseCellsDoNotAddStateOrLatencyInCmos) {
  using synth::Lnet;
  Lnet       net;
  const auto a = net.add_input("a"), b = net.add_input("b"), c = net.add_input("c"), d = net.add_input("d");
  const auto r = net.add_latch("r", 'x');
  net.set_latch_input(r, net.add_lut({net.add_lut({a, b}, Lnet::kAnd2), net.add_lut({c, d}, Lnet::kAnd2)}, Lnet::kAnd2));
  net.add_output(net.latch(r).q, "out");
  Endpoint_options o;
  o.gates           = {2, 2, 2};
  o.cost.static_and = 20;
  Budget     work{10000000};
  const auto region = import_lnet(net, work);
  ASSERT_EQ(region.status, Status::feasible);
  const auto selected = select_endpoint(region.graph, region.state[r].d, "r", {}, {}, o, work);
  ASSERT_TRUE(selected.selected);
  ASSERT_EQ(selected.selected->cells.size(), 3U);
  const std::array endpoints{
      Cmos_endpoint{r, *selected.selected}
  };
  const auto expanded = expand_cmos(region, endpoints, o, work);
  ASSERT_TRUE(expanded.net) << expanded.reason;
  check_state_and_functions(net, *expanded.net);
}

TEST(EndpointLnet, WideFunctionsLowerToSmallFaninWithoutWideningLnet) {
  using synth::Lnet;
  Lnet net;
  auto root = net.add_input("0");
  for (uint32_t i = 1; i < 9; ++i) {
    root = net.add_lut({root, net.add_input(std::to_string(i))}, Lnet::kAnd2);
  }
  const auto r = net.add_latch("wide", '0');
  net.set_latch_input(r, root);
  net.add_output(net.latch(r).q, "out");
  Budget     work{10000000};
  const auto region = import_lnet(net, work);
  ASSERT_EQ(region.status, Status::feasible);
  Endpoint_options o;
  o.gates             = {9, 9, 10};
  const auto selected = select_endpoint(region.graph, region.state[r].d, "wide", {}, {}, o, work);
  ASSERT_TRUE(selected.selected);
  ASSERT_TRUE(selected.selected->whole_cone);
  ASSERT_EQ(selected.selected->cells.size(), 1U);
  ASSERT_EQ(selected.selected->cells.front().function->inputs.size(), 9U);
  const std::array endpoints{
      Cmos_endpoint{r, *selected.selected}
  };
  const auto expanded = expand_cmos(region, endpoints, o, work);
  ASSERT_TRUE(expanded.net) << expanded.reason;
  check_state_and_functions(net, *expanded.net);
  for (synth::Lid id = 0; id < expanded.net->size(); ++id) {
    EXPECT_LE(expanded.net->fanin_count(id), 2U);
  }
}

TEST(EndpointLnet, NewDivisorExpansionKeepsOriginalStateAndBothOutputPolarities) {
  using synth::Lnet;
  Lnet       net;
  const auto a = net.add_input("a"), b = net.add_input("b"), c = net.add_input("c");
  const auto r = net.add_latch("pipe.r[2]", '1');
  net.set_latch_input(r, net.add_lut({net.add_lut({a, b}, Lnet::kAnd2), net.add_lut({a, c}, Lnet::kAnd2)}, Lnet::kOr2));
  net.add_output(net.latch(r).q, "q");
  net.add_output(net.add_lut({net.latch(r).q}, Lnet::kNot), "nq");
  Endpoint_options o;
  o.gates           = {2, 2, 2};
  o.cost.static_and = 20;
  Budget     work{10000000};
  const auto region = import_lnet(net, work);
  ASSERT_EQ(region.status, Status::feasible);
  const auto selected = select_endpoint(region.graph, region.state[r].d, region.state[r].name, {}, {}, o, work);
  ASSERT_TRUE(selected.selected);
  ASSERT_EQ(selected.selected->origin, "functional-two-phase");
  ASSERT_EQ(selected.selected->cells.size(), 2U);
  ASSERT_FALSE(selected.selected->cells.front().function->root);
  const std::array endpoints{
      Cmos_endpoint{r, *selected.selected}
  };
  const auto expanded = expand_cmos(region, endpoints, o, work);
  ASSERT_TRUE(expanded.net) << expanded.reason;
  check_state_and_functions(net, *expanded.net);
}

TEST(EndpointLnet, InvalidCorrespondenceAndBudgetRefusalNeverPublishPartialNetworks) {
  synth::Lnet net;
  const auto  a = net.add_input("a"), r = net.add_latch("r", '0');
  net.set_latch_input(r, a);
  Budget     work{100000};
  const auto region   = import_lnet(net, work);
  const auto selected = select_endpoint(region.graph, region.state[r].d, "r", {}, {}, {}, work);
  ASSERT_TRUE(selected.selected);
  std::array endpoints{
      Cmos_endpoint{r, *selected.selected}
  };
  endpoints[0].solution.name = "wrong";
  const auto mismatch        = expand_cmos(region, endpoints, {}, work);
  EXPECT_EQ(mismatch.status, Status::invalid);
  EXPECT_FALSE(mismatch.net);
  endpoints[0].solution.name                  = "r";
  auto function                               = std::make_shared<Endpoint_function>(*endpoints[0].solution.cells.back().function);
  function->formula.output_inverted           = !function->formula.output_inverted;
  endpoints[0].solution.cells.back().function = function;
  const auto bad_function                     = expand_cmos(region, endpoints, {}, work);
  EXPECT_EQ(bad_function.status, Status::invalid);
  EXPECT_FALSE(bad_function.net);
  endpoints[0].solution = *selected.selected;
  Budget     none{0};
  const auto exhausted = expand_cmos(region, endpoints, {}, none);
  EXPECT_EQ(exhausted.status, Status::search_exhausted);
  EXPECT_FALSE(exhausted.net);
  const auto too_large = expand_cmos(region, endpoints, {}, work, 1);
  EXPECT_EQ(too_large.status, Status::search_exhausted);
  EXPECT_FALSE(too_large.net);
}

TEST(EndpointLnet, ExportPrunesDeadLogicAndSupportsOutputOnlyNetworks) {
  synth::Lnet net;
  const auto  a = net.add_input("a"), b = net.add_input("b");
  net.add_lut({a, b}, synth::Lnet::kAnd2);  // dead
  net.add_output(net.add_lut({a, b}, synth::Lnet::kXor2), "parity");
  net.add_output(net.add_constant(true), "one");
  Budget     work{10000};
  const auto region   = import_lnet(net, work);
  const auto expanded = expand_cmos(region, {}, {}, work);
  ASSERT_TRUE(expanded.net) << expanded.reason;
  check_state_and_functions(net, *expanded.net);
  EXPECT_LT(expanded.net->size(), net.size());
  auto invalid      = region;
  invalid.inputs[1] = invalid.inputs[0];
  const auto bad    = export_lnet(invalid, work);
  EXPECT_EQ(bad.status, Status::invalid);
  EXPECT_FALSE(bad.net);
}
}  // namespace livehd::usyn

// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "endpoint_netlist.hpp"

#include <functional>

#include "gtest/gtest.h"

namespace livehd::usyn {
namespace {
Xsignal input(Xag_region& r, std::string name) {
  const auto s = r.graph.input(std::move(name));
  r.inputs.push_back(s);
  return s;
}

uint32_t state(Xag_region& r, std::string name, Xsignal d, char init) {
  const auto q = r.graph.input(name);
  r.state.push_back({std::move(name), init, q, d});
  return r.state.size() - 1;
}

std::vector<bool> evaluate(const Xag_region& r, uint32_t assignment) {
  std::vector<bool> values(r.graph.size());
  const auto        read = [&](Xsignal s) { return values[s.id] != s.inverted; };
  for (Id i = 1; i < r.graph.size(); ++i) {
    const auto& n = r.graph.node(i);
    switch (n.kind) {
      case Xag::Kind::constant: break;
      case Xag::Kind::source  : values[i] = (assignment >> n.source_index) & 1; break;
      case Xag::Kind::and_gate: values[i] = read(n.inputs[0]) && read(n.inputs[1]); break;
      case Xag::Kind::xor_gate: values[i] = read(n.inputs[0]) != read(n.inputs[1]); break;
    }
  }
  std::vector<bool> out;
  for (const auto& po : r.outputs) {
    out.push_back(read(po.signal));
  }
  for (const auto& s : r.state) {
    out.push_back(read(s.d));
  }
  return out;
}

void check_expansion(const Xag_region& source, const Endpoint_netlist& n) {
  Budget     work{100000000};
  const auto expanded = expand_endpoint_netlist(n, work);
  ASSERT_EQ(expanded.status, Status::feasible) << expanded.reason;
  ASSERT_EQ(expanded.state.size(), source.state.size());
  ASSERT_EQ(expanded.outputs.size(), source.outputs.size());
  EXPECT_EQ(expanded.graph.input_names(), source.graph.input_names());
  for (size_t i = 0; i < source.state.size(); ++i) {
    EXPECT_EQ(expanded.state[i].name, source.state[i].name);
    EXPECT_EQ(expanded.state[i].init, source.state[i].init);
    EXPECT_EQ(expanded.state[i].q, n.state[i].q);
    EXPECT_EQ(expanded.graph.node(expanded.state[i].q.id).source_index, source.graph.node(source.state[i].q.id).source_index);
  }
  for (size_t i = 0; i < source.outputs.size(); ++i) {
    EXPECT_EQ(expanded.outputs[i].name, source.outputs[i].name);
  }
  ASSERT_LE(source.graph.input_names().size(), 10U);
  for (uint32_t x = 0; x < (1U << source.graph.input_names().size()); ++x) {
    ASSERT_EQ(evaluate(expanded, x), evaluate(source, x)) << x;
  }
  for (const auto& cell : n.cells) {
    EXPECT_EQ(cell.outputs, (std::array<std::string, 2>{"Q", "!Q"}));
    for (uint32_t x = 0; x < (1U << cell.inputs.size()); ++x) {
      EXPECT_EQ(cell.function.get(x), cell.formula.evaluate(x));
      EXPECT_EQ(cell.function.complement().get(x), !cell.formula.evaluate(x));
    }
  }
}

// A shared phase-1 cell drives two endpoints through opposite rails. No
// original endpoint has an ordinary register after its DominoLatch owner.
Endpoint_netlist shared_netlist() {
  Endpoint_netlist n;
  const auto       a = n.native.input("a"), b = n.native.input("b"), c = n.native.input("c"), d = n.native.input("d");
  n.inputs      = {a, b, c, d};
  const auto ab = n.native.land(a, b);
  const auto r0 = n.native.input("pipe.r0"), r1 = n.native.input("pipe.r1");
  n.state = {
      {"pipe.r0", '0', r0,   n.native.lor(ab, c), 7, 1},
      {"pipe.r1", '1', r1, n.native.land(~ab, d), 7, 2}
  };
  n.outputs = {
      { "q0",  r0},
      {"nq0", ~r0},
      { "q1",  r1},
      {"nq1", ~r1}
  };
  const auto append = [&](Endpoint_kind kind, uint32_t state_index, std::vector<Endpoint_ref> inputs, bool parallel) {
    Frozen_cell cell;
    cell.kind          = kind;
    cell.state         = state_index;
    cell.phase         = kind == Endpoint_kind::Domino ? 1 : 2;
    cell.name          = kind == Endpoint_kind::Domino ? "" : n.state[state_index].name;
    cell.inputs        = std::move(inputs);
    cell.formula.nodes = {
        {Gate_formula::Kind::literal, 0, 0, 0, false},
        {Gate_formula::Kind::literal, 0, 0, 1, false},
        {parallel ? Gate_formula::Kind::parallel : Gate_formula::Kind::series, 0, 1}
    };
    cell.metrics  = *cell.formula.metrics();
    cell.function = Truth_table(2);
    for (uint32_t x = 0; x < 4; ++x) {
      cell.function.set(x, parallel ? bool(x & 1) || bool(x & 2) : (x & 3) == 3);
    }
    cell.rails = {
        {0, cell.inputs[0]},
        {1, cell.inputs[1]}
    };
    n.estimated_cost.domino += 2 + (kind == Endpoint_kind::Domino ? n.costs.domino : n.costs.domino_latch);
    n.cells.push_back(std::move(cell));
  };
  using Space = Endpoint_ref::Space;
  append(Endpoint_kind::Domino,
         0,
         {
             {Space::native, a.id},
             {Space::native, b.id}
  },
         false);
  append(Endpoint_kind::DominoLatch,
         0,
         {
             {  Space::cell,    0},
             {Space::native, c.id}
  },
         true);
  append(Endpoint_kind::DominoLatch,
         1,
         {
             {Space::cell, 0, true},
             {Space::native, d.id}
  },
         false);
  return n;
}
}  // namespace

TEST(EndpointNetlist, FreezeSelectedOneAndTwoPhaseNetworksPreservesBehaviorAndState) {
  for (uint32_t phases : {1U, 2U}) {
    Xag_region source;
    const auto a = input(source, "a"), b = input(source, "b"), c = input(source, "c"), d = input(source, "d");
    const auto root     = source.graph.land(source.graph.land(a, b), source.graph.land(c, d));
    const auto chosen   = state(source, "pipe.r[0]", root, '1');
    const auto ordinary = state(source, "negative_edge", ~a, '0');
    source.outputs      = {
        {       "q",   source.state[chosen].q},
        {      "nq",  ~source.state[chosen].q},
        {"ordinary", source.state[ordinary].q}
    };
    source.status = Status::feasible;
    Logical_options options;
    options.endpoint.clock_phases    = phases;
    options.endpoint.gates           = {2, 2, 2};
    options.endpoint.cost.static_and = 20;
    Budget     work{100000000};
    const auto selected = synthesize_logical_region(source, std::array{chosen}, options, work);
    ASSERT_TRUE(selected.region) << selected.reason;
    const auto frozen = freeze_endpoint_netlist(*selected.region, options, work, std::array{0U, 1U});
    ASSERT_TRUE(frozen.netlist) << frozen.reason;
    const auto& n = *frozen.netlist;
    ASSERT_EQ(n.state.size(), 2U);
    EXPECT_TRUE(n.state[chosen].domino_latch);
    EXPECT_FALSE(n.state[ordinary].domino_latch);
    EXPECT_EQ(n.cells.size(), phases == 1 ? 1U : 3U);
    EXPECT_EQ(n.cells.back().phase, phases);
    check_expansion(source, n);
  }
}

TEST(EndpointNetlist, SharedPhaseOneSupportsMixedRailsWithoutExtraStateOrInverters) {
  const auto n = shared_netlist();
  Xag_region source;
  source.graph   = n.native;
  source.inputs  = n.inputs;
  source.outputs = n.outputs;
  for (const auto& s : n.state) {
    source.state.push_back({s.name, s.init, s.q, s.reference_d});
  }
  check_expansion(source, n);
  EXPECT_EQ(n.estimated_cost.inverters, 0U);
  Budget exact_capacity{1000000};
  EXPECT_EQ(expand_endpoint_netlist(n, exact_capacity, n.native.size()).status, Status::feasible);
}

TEST(EndpointNetlist, PairSelectionAndFreezingChargeSharedFirstPhaseOnce) {
  Xag_region source;
  const auto a = input(source, "a"), b = input(source, "b"), c = input(source, "c"), d = input(source, "d");
  const auto shared = source.graph.land(a, b);
  const auto s0     = state(source, "left", source.graph.lor(shared, c), '0');
  const auto s1     = state(source, "right", source.graph.land(shared, d), '1');
  source.outputs    = {
      { "left", source.state[s0].q},
      {"right", source.state[s1].q}
  };
  source.status = Status::feasible;
  Logical_options options;
  options.endpoint.gates           = {2, 2, 2};
  options.endpoint.cost.static_and = 20;
  for (bool outside : {false, true}) {
    if (outside) {
      source.outputs.push_back({"static_reader", ~shared});
    }
    Budget           work{100000000};
    const std::array domains{7U, 7U};
    const auto       selected = synthesize_logical_region(source, std::array{s0, s1}, options, work, domains);
    ASSERT_TRUE(selected.region) << selected.reason;
    ASSERT_EQ(selected.region->endpoints.size(), 2U);
    for (const auto& endpoint : selected.region->endpoints) {
      ASSERT_EQ(endpoint.cells.size(), outside ? 1U : 2U);
    }
    EXPECT_EQ(selected.region->cost.domino, outside ? 20U : 27U);
    EXPECT_EQ(selected.region->cost.static_logic, outside ? 20U : 0U);
    EXPECT_EQ(selected.region->cost.inverters, outside ? 1U : 0U);
    const auto frozen = freeze_endpoint_netlist(*selected.region, options, work, domains);
    ASSERT_TRUE(frozen.netlist) << frozen.reason;
    EXPECT_EQ(frozen.netlist->cells.size(), outside ? 2U : 3U);
    EXPECT_NE(frozen.netlist->state[s0].domino_latch, frozen.netlist->state[s1].domino_latch);
    EXPECT_EQ(frozen.netlist->estimated_cost, selected.region->cost);
    check_expansion(source, *frozen.netlist);
  }
}

TEST(EndpointNetlist, FreezingMergesOppositeOutputPolaritiesThroughFreeRails) {
  const auto     original = shared_netlist();
  Logical_region region;
  region.logic.graph   = original.native;
  region.logic.inputs  = original.inputs;
  region.logic.outputs = original.outputs;
  region.logic.status  = Status::feasible;
  region.cost          = original.estimated_cost;
  for (uint32_t i = 0; i < original.state.size(); ++i) {
    const auto& s = original.state[i];
    region.logic.state.push_back({s.name, s.init, s.q, s.reference_d});
    Logical_endpoint endpoint;
    endpoint.state_index = i;
    endpoint.name        = s.name;
    for (size_t j : {size_t{0}, size_t{i + 1}}) {
      const auto&  source = original.cells[j];
      Logical_cell cell;
      cell.formula = source.formula;
      cell.metrics = source.metrics;
      cell.phase   = source.phase;
      cell.latch   = j != 0;
      for (const auto& input : source.inputs) {
        cell.inputs.push_back(input.space == Endpoint_ref::Space::native ? Xsignal{input.index, input.inverted} : Xsignal{});
        cell.producers.push_back(input.space == Endpoint_ref::Space::native ? -1 : 0);
      }
      // The second endpoint's local producer implements !AB, so its positive
      // local binding needs the first representative's free inverse output.
      if (!cell.latch && i == 1) {
        cell.formula.output_inverted = true;
      }
      endpoint.cells.push_back(std::move(cell));
    }
    region.endpoints.push_back(std::move(endpoint));
  }
  Budget     work{1000000};
  const auto frozen = freeze_endpoint_netlist(region, {}, work, std::array{7U, 7U});
  ASSERT_TRUE(frozen.netlist) << frozen.reason;
  ASSERT_EQ(frozen.netlist->cells.size(), 3U);
  const auto& second = frozen.netlist->cells[*frozen.netlist->state[1].domino_latch];
  EXPECT_EQ(second.inputs[0], (Endpoint_ref{Endpoint_ref::Space::cell, 0, true}));
  EXPECT_EQ(frozen.netlist->estimated_cost.inverters, 0U);
  check_expansion(region.logic, *frozen.netlist);
  auto reversed = region;
  std::reverse(reversed.endpoints.begin(), reversed.endpoints.end());
  const auto inverse_first = freeze_endpoint_netlist(reversed, {}, work, std::array{7U, 7U});
  ASSERT_TRUE(inverse_first.netlist) << inverse_first.reason;
  EXPECT_EQ(inverse_first.netlist->cells.size(), 3U);
  EXPECT_TRUE(inverse_first.netlist->cells[0].formula.output_inverted);
  const auto& positive_consumer = inverse_first.netlist->cells[*inverse_first.netlist->state[0].domino_latch];
  EXPECT_EQ(positive_consumer.inputs[0], (Endpoint_ref{Endpoint_ref::Space::cell, 0, true}));
  check_expansion(region.logic, *inverse_first.netlist);
  // Identical implementations must stay separate when the domains differ or
  // are unknown; the two latches remain separate in every configuration.
  region.cost.domino += original.costs.domino + original.cells[0].metrics.transistors;
  for (auto domains : {
           std::array{                  7U,                   8U},
           std::array{unknown_clock_domain, unknown_clock_domain}
  }) {
    const auto separate = freeze_endpoint_netlist(region, {}, work, domains);
    ASSERT_TRUE(separate.netlist) << separate.reason;
    EXPECT_EQ(separate.netlist->cells.size(), 4U);
    check_expansion(region.logic, *separate.netlist);
  }
  region.cost = original.estimated_cost;
  for (uint64_t limit : {0U, 20U, 50U, 100U}) {
    Budget     limited{limit};
    const auto refused = freeze_endpoint_netlist(region, {}, limited, std::array{7U, 7U});
    EXPECT_EQ(refused.status, Status::search_exhausted);
    EXPECT_FALSE(refused.netlist);
  }
}

TEST(EndpointNetlist, MultiwordExactFunctionsRetainBothInputRailsAndOutputPolarity) {
  Xag_region           source;
  std::vector<Xsignal> inputs;
  for (uint32_t i = 0; i < 8; ++i) {
    inputs.push_back(input(source, "x" + std::to_string(i)));
  }
  auto d = source.graph.lxor(inputs[0], inputs[1]);
  for (uint32_t i = 2; i < 8; ++i) {
    d = source.graph.land(d, i % 2 ? ~inputs[i] : inputs[i]);
  }
  const auto si = state(source, "r", ~d, 'x');
  source.status = Status::feasible;
  Logical_options options;
  options.endpoint.clock_phases    = 1;
  options.endpoint.gates           = {8, 8, 10};
  options.endpoint.cost.static_and = 20;
  options.endpoint.cost.static_xor = 20;
  Budget     work{100000000};
  const auto selected = synthesize_logical_region(source, std::array{si}, options, work);
  ASSERT_TRUE(selected.region) << selected.reason;
  const auto frozen = freeze_endpoint_netlist(*selected.region, options, work);
  ASSERT_TRUE(frozen.netlist) << frozen.reason;
  ASSERT_EQ(frozen.netlist->cells.size(), 1U);
  const auto& cell = frozen.netlist->cells[0];
  EXPECT_EQ(cell.inputs.size(), 8U);
  EXPECT_EQ(cell.function.words.size(), 4U);
  EXPECT_NE(cell.metrics.positive & cell.metrics.negative, 0U);
  check_expansion(source, *frozen.netlist);
  // The output port polarity is part of both the truth table and canonical D.
  auto opposite                             = *frozen.netlist;
  opposite.cells[0].formula.output_inverted = !opposite.cells[0].formula.output_inverted;
  opposite.cells[0].function                = opposite.cells[0].function.complement();
  opposite.state[0].reference_d             = ~opposite.state[0].reference_d;
  source.state[0].d                         = ~source.state[0].d;
  check_expansion(source, opposite);
}

TEST(EndpointNetlist, RejectsCorruptedFunctionsRailsPhasesDomainsAndOwnershipAtomically) {
  const std::vector<std::function<void(Endpoint_netlist&)>> corruptions{
      [](auto& n) { ++n.policy_version; },
      [](auto& n) { n.residual_kind = Endpoint_kind::Domino; },
      [](auto& n) { n.inputs.pop_back(); },
      [](auto& n) { n.inputs[0] = n.inputs[1]; },
      [](auto& n) { n.state[0].name = n.state[1].name; },
      [](auto& n) { n.state[0].init = 'z'; },
      [](auto& n) { n.state[0].domino_latch = 0; },
      [](auto& n) { n.state[0].reference_d = n.inputs[0]; },
      [](auto& n) { n.state[1].domain = 8; },
      [](auto& n) { n.state[1].domain = unknown_clock_domain; },
      [](auto& n) { n.cells[0].function.set(0, true); },
      [](auto& n) { n.cells[0].formula.nodes.back().left = 2; },
      [](auto& n) { ++n.cells[0].metrics.transistors; },
      [](auto& n) { n.cells[0].rails[0].producer.inverted = true; },
      [](auto& n) { n.cells[0].outputs[1] = "unused"; },
      [](auto& n) { n.cells[0].phase = 2; },
      [](auto& n) { n.cells[1].phase = 3; },
      [](auto& n) { n.cells[1].name = "wrong"; },
      [](auto& n) { n.cells[0].kind = Endpoint_kind::Static; },
      [](auto& n) { n.cells[0].state = 42; },
      [](auto& n) { n.gates.logical_inputs = 1; },
      [](auto& n) { n.gates.stack = 1; },
      [](auto& n) { n.clock_phases = 1; },
      [](auto& n) { ++n.estimated_cost.domino; },
      [](auto& n) {
        n.cells[1].inputs[0].index   = 2;  // forward reference
        n.cells[1].rails[0].producer = n.cells[1].inputs[0];
      },
      [](auto& n) {
        n.cells[2].inputs[0].index   = 1;  // latch/same-phase dependency
        n.cells[2].rails[0].producer = n.cells[2].inputs[0];
      },
      [](auto& n) {
        n.cells[0].inputs[0].index   = n.native.size();
        n.cells[0].rails[0].producer = n.cells[0].inputs[0];
      },
      [](auto& n) { n.cells.push_back(n.cells[0]); },
  };
  for (size_t i = 0; i < corruptions.size(); ++i) {
    SCOPED_TRACE(i);
    auto n = shared_netlist();
    corruptions[i](n);
    Budget     work{1000000};
    const auto result = expand_endpoint_netlist(n, work);
    EXPECT_EQ(result.status, Status::invalid) << result.reason;
    EXPECT_TRUE(result.state.empty());
    EXPECT_TRUE(result.inputs.empty());
    EXPECT_TRUE(result.outputs.empty());
    EXPECT_EQ(result.graph.size(), 1U);
  }
}

TEST(EndpointNetlist, ConstantsIdentitySignedInputsAndOutputOnlyResidual) {
  Xag_region       source;
  const auto       a = input(source, "a"), b = input(source, "b");
  const std::array eligible{state(source, "zero", source.graph.constant(false), '0'),
                            state(source, "one", source.graph.constant(true), '1'),
                            state(source, "inverted", ~a, 'x')};
  source.outputs = {
      {"residual", source.graph.lxor(a, b)}
  };
  source.status = Status::feasible;
  Budget     work{100000000};
  const auto selected = synthesize_logical_region(source, eligible, {}, work);
  ASSERT_TRUE(selected.region) << selected.reason;
  const auto frozen = freeze_endpoint_netlist(*selected.region, {}, work);
  ASSERT_TRUE(frozen.netlist) << frozen.reason;
  check_expansion(source, *frozen.netlist);
  // Rebuild a source inventory without the discarded state sources.
  source       = {};
  const auto x = input(source, "x"), y = input(source, "y");
  source.outputs = {
      {"xor", source.graph.lxor(x, y)}
  };
  source.status          = Status::feasible;
  const auto output_only = synthesize_logical_region(source, {}, {}, work);
  ASSERT_TRUE(output_only.region);
  const auto output_frozen = freeze_endpoint_netlist(*output_only.region, {}, work);
  ASSERT_TRUE(output_frozen.netlist) << output_frozen.reason;
  EXPECT_TRUE(output_frozen.netlist->cells.empty());
  check_expansion(source, *output_frozen.netlist);
}

TEST(EndpointNetlist, RefusesWorkAndStorageLimitsWithoutPartialPublication) {
  const auto n = shared_netlist();
  Budget     none{0};
  EXPECT_EQ(expand_endpoint_netlist(n, none).status, Status::search_exhausted);
  Budget work{1000000};
  EXPECT_EQ(expand_endpoint_netlist(n, work, 2).status, Status::search_exhausted);
  EXPECT_EQ(expand_endpoint_netlist(n, work, 2000000, 2).status, Status::search_exhausted);
  Xag_region source;
  const auto a        = input(source, "a");
  const auto si       = state(source, "r", a, 'x');
  source.status       = Status::feasible;
  const auto selected = synthesize_logical_region(source, std::array{si}, {}, work);
  ASSERT_TRUE(selected.region);
  const auto frozen = freeze_endpoint_netlist(*selected.region, {}, none);
  EXPECT_EQ(frozen.status, Status::search_exhausted);
  EXPECT_FALSE(frozen.netlist);
}

}  // namespace livehd::usyn

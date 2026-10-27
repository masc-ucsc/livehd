// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "domino_timing_model.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <stdexcept>

#include "domino_timing_fixture.hpp"
#include "gtest/gtest.h"

namespace livehd::domplace::timing {
namespace {
namespace model = liberty::timing;
const model::Library& base() {
  static const auto library = model::parse(test::kLiberty);
  return library;
}
Domino_timing_view view(const test::Sidecar& sidecar, const std::string& name) {
  std::string why;
  auto        read = read_domino_timing(sidecar.write(name), &why);
  EXPECT_TRUE(read) << why;
  return read.value_or(Domino_timing_view{});
}
bool contains(const std::vector<std::string>& issues, std::string_view text) {
  return std::any_of(issues.begin(), issues.end(), [&](const auto& issue) { return issue.find(text) != issue.npos; });
}
}  // namespace

TEST(DominoTimingModel, ReadsTheTimingViewOfASchemaThreeBundle) {
  test::Sidecar sidecar;
  sidecar.instances = {
      {"q1", test::kDomino}
  };
  const auto v = view(sidecar, "model_view");
  EXPECT_EQ(v.bundle_schema, 6U);
  EXPECT_EQ(v.stage_version, 1U);
  EXPECT_EQ(v.model_version, 1U);
  EXPECT_EQ(v.clock, "clk");
  EXPECT_EQ(v.corner, "test");
  EXPECT_EQ(v.fo4_ps, 10);
  EXPECT_EQ(v.dbu, 1000);
  // Evaluate and precharge are per variant; the stage-wide slot is only an
  // override.
  EXPECT_FALSE(v.stage.eval_delay_ps);
  EXPECT_FALSE(v.stage.precharge_ps);
  EXPECT_EQ(v.stage.precharge_margin_ps, 5);
  EXPECT_EQ(v.sources.at("eval_delay_ps"), "logical-effort-estimate");
  ASSERT_TRUE(v.find(test::kDomino));
  EXPECT_EQ(v.find(test::kDomino)->eval_delay_ps, 20);
  EXPECT_EQ(v.find(test::kDomino)->precharge_ps, 20);
  EXPECT_EQ(variant_delays(v.stage, v.find(test::kDomino)).eval_delay_ps, 20);
  EXPECT_FALSE(variant_delays(v.stage, nullptr).eval_delay_ps);
  ASSERT_EQ(v.variants.size(), 2U);
  const auto* qn = v.find(test::kQn);
  ASSERT_TRUE(qn);
  EXPECT_EQ(qn->family, test::kBase + "_t0");
  EXPECT_EQ(qn->register_cell, "FFQN");
  EXPECT_EQ(qn->clk_pin, "CLK");
  EXPECT_EQ(qn->q_pin, "QN");
  EXPECT_EQ(qn->setup_capture_ps, 7);
  EXPECT_EQ(qn->rails, (std::vector<std::string>{"R0", "R1"}));
  EXPECT_EQ(qn->rails_ff, (std::vector<std::optional<double>>{2, 2}));  // one per rail (U26)
  EXPECT_EQ(qn->reset_ff, 2);
  ASSERT_EQ(v.instances.size(), 1U);
  EXPECT_EQ(v.instances[0].state, "q1");
  EXPECT_EQ(v.digest.size(), 16U);
  std::string why;
  // A sidecar of the previous schema is refused by name, not misread.
  const auto  old_dir = sidecar.write("model_schema3");
  {
    std::ifstream     in(old_dir / kBundleSidecar);
    std::string       text{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    const std::string key = "\"schema\":6";
    ASSERT_NE(text.find(key), std::string::npos);
    text.replace(text.find(key), key.size(), "\"schema\":5");
    std::ofstream(old_dir / kBundleSidecar) << text;
  }
  EXPECT_FALSE(read_domino_timing(old_dir, &why));
  EXPECT_NE(why.find("is schema 5; this build reads schema 6 only"), std::string::npos) << why;
  EXPECT_FALSE(read_domino_timing(std::filesystem::path(std::getenv("TEST_TMPDIR")) / "missing", &why));
  EXPECT_NE(why.find("missing"), std::string::npos) << why;
}

TEST(DominoTimingModel, OneSequentialCellPerVariantWithTheRegisterArcsUnswapped) {
  const auto               v          = view(test::Sidecar{}, "model_shape");
  uint64_t                 generation = 0;
  std::vector<std::string> notes;
  const auto               cells = domino_timing_cells(v, base(), &generation, &notes);
  ASSERT_EQ(cells->library.cells.size(), 2U);
  const auto& cell = cells->library.cells.at(test::kDomino);
  EXPECT_TRUE(cell.sequential);
  EXPECT_TRUE(cell.unsupported.empty());
  EXPECT_EQ(cell.thresholds.input, base().cells.at("FF").thresholds.input);
  ASSERT_EQ(cell.pins.size(), 4U);
  for (const auto* rail : {"R0", "R1"}) {
    const auto& pin = cell.pins.at(rail);
    EXPECT_EQ(pin.direction, "input");
    EXPECT_EQ(pin.capacitance_ff[0], 2);
    EXPECT_EQ(pin.capacitance_ff[1], 2);
    ASSERT_EQ(pin.arcs.size(), 1U);
    EXPECT_EQ(pin.arcs[0].type, "setup_rising");
    EXPECT_EQ(pin.arcs[0].related_pin, "CLK");
    EXPECT_EQ(pin.arcs[0].constraint[0]->lookup({}), 3);
    EXPECT_EQ(pin.arcs[0].constraint[1]->lookup({}), 3);
  }
  EXPECT_TRUE(cell.pins.at("CLK").clock);
  EXPECT_EQ(cell.pins.at("CLK").capacitance_ff[0], 4);
  const auto& q = cell.pins.at("Q");
  EXPECT_EQ(q.max_capacitance_ff, 50);
  EXPECT_EQ(q.max_transition_ps, 40);
  ASSERT_EQ(q.arcs.size(), 1U);
  EXPECT_EQ(q.arcs[0].related_pin, "CLK");
  EXPECT_EQ(q.arcs[0].type, "rising_edge");
  EXPECT_EQ(cells->capture.at(test::kDomino).fraction, .5);
  EXPECT_EQ(cells->capture.at(test::kDomino).uncertainty_ps, 1);
  // A QN-only register: its QN pin's own rise/fall tables, never swapped.
  const auto& qn = cells->library.cells.at(test::kQn).pins.at("Q");
  ASSERT_EQ(qn.arcs.size(), 1U);
  const auto& register_arc = base().cells.at("FFQN").pins.at("QN").arcs.at(0);
  EXPECT_EQ(qn.arcs[0].delay[0]->values, register_arc.delay[0]->values);
  EXPECT_EQ(qn.arcs[0].delay[0]->lookup({}), 12);
  EXPECT_EQ(qn.arcs[0].delay[1]->lookup({}), 22);
  EXPECT_EQ(qn.arcs[0].slew[1]->values, register_arc.slew[1]->values);
  EXPECT_FALSE(qn.max_capacitance_ff);
  EXPECT_TRUE(contains(notes, "labeled estimate"));
  // Bound to a graph, the cell launches like its register and is a T/2 endpoint.
  livehd::placement::test_fixture::Timing_circuit circuit(test::kLiberty, "shape", {"clk"}, {});
  auto                                            domino = circuit.cell("d", test::kDomino, &cells->library);
  auto                                            ff     = circuit.cell("ff", "FF");
  domino.create_sink_pin("CLK").connect_driver(circuit.graph->get_input_pin("clk"));
  ff.create_sink_pin("CLK").connect_driver(circuit.graph->get_input_pin("clk"));
  domino.create_sink_pin("R0").connect_driver(ff.create_driver_pin("Q"));
  domino.create_sink_pin("R1").connect_driver(domino.create_driver_pin("Q"));
  ff.create_sink_pin("D").connect_driver(domino.create_driver_pin("Q"));
  late::Constraints constraints;
  constraints.clocks["clk"] = {.period_ps = 100, .roots = {"clk"}};
  constraints.complete      = true;
  const auto result         = late::Proxy(*circuit.graph, circuit.library, constraints, 0, generation, cells).evaluate();
  ASSERT_TRUE(result.certified) << (result.unsupported.empty() ? "" : result.unsupported.front());
  EXPECT_EQ(result.model_generation, generation);
  EXPECT_EQ(result.fractional_endpoints, 2U);
  // R0: 50 - 1 - 3 - 20 = 26; R1 (own Q): the same; D: 92 - 20 = 72.
  EXPECT_EQ(result.wns_ps, 26);
  EXPECT_EQ(result.achievable_period_ps, 48);
}

TEST(DominoTimingModel, NullCoefficientsAreUnsupportedAndNullCapacitancesStayAbsent) {
  test::Sidecar sidecar;
  sidecar.stage["setup_eval_ps"].reset();
  sidecar.clock_ff.reset();
  const auto               v = view(sidecar, "model_nulls");
  std::vector<std::string> notes;
  const auto               cells = domino_timing_cells(v, base(), nullptr, &notes);
  const auto&              cell  = cells->library.cells.at(test::kDomino);
  EXPECT_TRUE(contains(cell.unsupported, "setup_eval_ps is null"));
  EXPECT_FALSE(cell.pins.at("R0").arcs.at(0).constraint[0]);
  EXPECT_FALSE(cell.pins.at("CLK").capacitance_ff[0]);
  EXPECT_TRUE(contains(notes, "rail capture is unmeasured: setup_eval_ps null"));
  livehd::placement::test_fixture::Timing_circuit circuit(test::kLiberty, "nulls", {"clk"}, {});
  auto                                            domino = circuit.cell("d", test::kDomino, &cells->library);
  domino.create_sink_pin("CLK").connect_driver(circuit.graph->get_input_pin("clk"));
  domino.create_sink_pin("R0").connect_driver(domino.create_driver_pin("Q"));
  late::Constraints constraints;
  constraints.clocks["clk"] = {.period_ps = 100, .roots = {"clk"}};
  constraints.complete      = true;
  const auto result         = late::Proxy(*circuit.graph, circuit.library, constraints, 0, 0, cells).evaluate();
  EXPECT_FALSE(result.certified);
  EXPECT_TRUE(contains(result.unsupported, "d: domino setup_eval_ps is null (unmeasured)"));
  EXPECT_TRUE(contains(result.unsupported, "missing Liberty input capacitance: d:CLK"));
  EXPECT_TRUE(contains(result.unsupported, "missing setup table or data slew: d:R0"));
  // A register missing from the Liberty is unsupported too, never a zero arc.
  auto other = base();
  other.cells.erase("FFQN");
  const auto missing = domino_timing_cells(v, other, nullptr, nullptr);
  EXPECT_TRUE(contains(missing->library.cells.at(test::kQn).unsupported, "FFQN is not in the Liberty timing model"));
  EXPECT_TRUE(missing->library.cells.at(test::kQn).pins.at("Q").arcs.empty());
}

TEST(DominoTimingModel, LibertyUnitsNormalizeAndCoefficientsStayInPicoseconds) {
  // A 1 ns Liberty: the register's 0.01/0.02 ns clock-to-Q is 10/20 ps, while
  // the sidecar coefficients are already ps.
  const auto  nanoseconds = model::parse(R"(
library(ns) {time_unit:"1ns";capacitive_load_unit(1,pf);
  cell(FF) {area:10; ff(IQ,IQN){clocked_on:CLK;next_state:D;}
    pin(CLK){direction:input;clock:true;capacitance:0.003;}
    pin(D){direction:input;capacitance:0.001;}
    pin(Q){direction:output;function:IQ;max_capacitance:0.05;
      timing(){related_pin:CLK;timing_type:rising_edge;timing_sense:non_unate;
      cell_rise(scalar){values("0.01");}cell_fall(scalar){values("0.02");}
      rise_transition(scalar){values("0.002");}fall_transition(scalar){values("0.003");}}}}
})");
  const auto  v           = view(test::Sidecar{}, "model_units");
  const auto  cells       = domino_timing_cells(v, nanoseconds, nullptr, nullptr);
  const auto& cell        = cells->library.cells.at(test::kDomino);
  EXPECT_NEAR(cell.pins.at("Q").arcs.at(0).delay[0]->lookup({}), 10, 1e-9);
  EXPECT_NEAR(cell.pins.at("Q").arcs.at(0).delay[1]->lookup({}), 20, 1e-9);
  EXPECT_NEAR(*cell.pins.at("Q").max_capacitance_ff, 50, 1e-9);
  EXPECT_EQ(cell.pins.at("R0").arcs.at(0).constraint[0]->lookup({}), 3);
  EXPECT_EQ(cell.pins.at("R0").capacitance_ff[0], 2);
}

TEST(DominoTimingModel, EachRailPinTakesItsOwnCapacitance) {
  // U26: a rail gating three PDN devices loads 3x one gating a single device.
  test::Sidecar sidecar;
  sidecar.r1_ff     = 6;
  const auto  v     = view(sidecar, "model_rail_caps");
  const auto  cells = domino_timing_cells(v, base(), nullptr, nullptr);
  const auto& cell  = cells->library.cells.at(test::kDomino);
  EXPECT_EQ(cell.pins.at("R0").capacitance_ff[0], 2);
  EXPECT_EQ(cell.pins.at("R1").capacitance_ff[0], 6);
  EXPECT_EQ(cell.pins.at("R1").capacitance_ff[1], 6);
}

TEST(DominoTimingModel, OverridesAreRecordedAndMoveTheGeneration) {
  auto     v       = view(test::Sidecar{}, "model_overrides");
  uint64_t initial = 0, again = 0, moved = 0;
  (void)domino_timing_cells(v, base(), &initial, nullptr);
  (void)domino_timing_cells(v, base(), &again, nullptr);
  EXPECT_EQ(initial, again);
  apply_overrides(v, {.stage_ps = {{"eval_delay_ps", 33}}, .setup_eval_fo4 = .5});
  EXPECT_EQ(v.stage.eval_delay_ps, 33);
  EXPECT_EQ(v.stage.setup_eval_ps, 5);  // 0.5 * FO4 10
  EXPECT_EQ(v.sources.at("eval_delay_ps"), "override");
  EXPECT_EQ(v.sources.at("setup_eval_ps"), "override");
  EXPECT_EQ(v.sources.at("precharge_ps"), "logical-effort-estimate");
  // The override wins over every variant's own estimate.
  EXPECT_EQ(variant_delays(v.stage, v.find(test::kDomino)).eval_delay_ps, 33);
  EXPECT_EQ(variant_delays(v.stage, v.find(test::kDomino)).precharge_ps, 20);
  const auto cells = domino_timing_cells(v, base(), &moved, nullptr);
  EXPECT_NE(moved, initial);
  EXPECT_EQ(cells->library.cells.at(test::kDomino).pins.at("R1").arcs.at(0).constraint[0]->lookup({}), 5);
  EXPECT_THROW(apply_overrides(v, {.stage_ps = {{"nope_ps", 1}}}), std::invalid_argument);
  EXPECT_THROW(apply_overrides(v, {.stage_ps = {{"eval_delay_ps", -1}}}), std::invalid_argument);
  EXPECT_THROW(apply_overrides(v, {.stage_ps = {{"precharge_ps", 1}}, .precharge_fo4 = 2}), std::invalid_argument);
  test::Sidecar no_fo4;
  no_fo4.fo4.reset();
  auto unscaled = view(no_fo4, "model_no_fo4");
  EXPECT_THROW(apply_overrides(unscaled, {.eval_fo4 = 2}), std::invalid_argument);
  EXPECT_TRUE((Model_overrides{}).empty());
}
}  // namespace livehd::domplace::timing

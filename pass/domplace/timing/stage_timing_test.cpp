// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "stage_timing.hpp"

#include <algorithm>
#include <map>
#include <stdexcept>
#include <string>
#include <tuple>

#include "domino_timing_fixture.hpp"
#include "gtest/gtest.h"
#include "rapidjson/document.h"

namespace livehd::domplace::timing {
namespace {
using Circuit = livehd::placement::test_fixture::Timing_circuit;
// A bundle view, its provisional cells and a circuit to place them in. All
// tables are scalar and there is no RC, so every number is hand-computable:
// clock 100 ps, setup_eval 3, u1 1, u2 2, eval 20, precharge 20, margin 5,
// setup_capture 5 (v0 on FF) and 7 (v1 on FFQN).
struct Bench {
  test::Sidecar                                  sidecar;
  Domino_timing_view                             view;
  std::shared_ptr<const late::Provisional_cells> cells;
  uint64_t                                       generation = 0;
  Circuit                                        circuit;
  Bench(const std::string& name, std::initializer_list<std::string> inputs, std::initializer_list<std::string> outputs,
        test::Sidecar s = {})
      : sidecar(std::move(s)), circuit(test::kLiberty, name, inputs, outputs) {
    std::string why;
    auto        read = read_domino_timing(sidecar.write(name), &why);
    EXPECT_TRUE(read) << why;
    view = read.value_or(Domino_timing_view{});
    model();
  }
  void             model() { cells = domino_timing_cells(view, *circuit.library, &generation, nullptr); }
  hhds::Node_class domino(const std::string& name, const std::string& master) {
    auto node = circuit.cell(name, master, &cells->library);
    node.create_sink_pin("CLK").connect_driver(circuit.graph->get_input_pin("clk"));
    return node;
  }
  hhds::Node_class flop(const std::string& name) {
    auto node = circuit.cell(name, "FF");
    node.create_sink_pin("CLK").connect_driver(circuit.graph->get_input_pin("clk"));
    return node;
  }
  Stage_timing evaluate(Stage_timing_options options = {}, const physical::Parasitics* rc = nullptr) {
    model();
    options.model_generation = generation;
    return evaluate_stage_timing(
        *circuit.graph,
        rc,
        circuit.library,
        cells,
        &view,
        stage_constraints(*circuit.graph, std::nullopt, circuit.graph->get_name(), {}, Clock_spec{"clk", "clk", 100, {}}, 0),
        0,
        options);
  }
};
// Every class in one design (inputs clk, a; output z):
//   flop->flop      ff1.Q -> INV -> ff1.D             30/30 into 95/92: slack 62, period 38
//   flop->domino    ff1.Q -> BUF -> d1.R0             15/27 into 46:    slack 19, period 62
//   domino->domino  d1.Q -> d1.R1 (its own Q)         10/20 into 46:    slack 26, period 48
//                   d1.Q -> INV -> d2.R0 (negative)   30/30 into 46:    slack 16, period 68
//   domino->flop    d2.Q (FFQN: 12/22) -> ff2.D       12/22 into 95/92: slack 70, period 30
// d2.R1 is driven only by the cut input a; z is a cut output.
struct Classes : Bench {
  explicit Classes(const std::string& name, test::Sidecar s = {}) : Bench(name, {"clk", "a"}, {"z"}, with_instances(std::move(s))) {
    auto ff1 = flop("ff1"), ff2 = flop("ff2");
    auto i1 = circuit.cell("i1", "INV"), b1 = circuit.cell("b1", "BUF"), i2 = circuit.cell("i2", "INV");
    auto d1 = domino("d1", test::kDomino), d2 = domino("d2", test::kQn);
    i1.create_sink_pin("A").connect_driver(ff1.create_driver_pin("Q"));
    ff1.create_sink_pin("D").connect_driver(i1.create_driver_pin("Y"));
    b1.create_sink_pin("A").connect_driver(ff1.create_driver_pin("Q"));
    d1.create_sink_pin("R0").connect_driver(b1.create_driver_pin("Y"));
    d1.create_sink_pin("R1").connect_driver(d1.create_driver_pin("Q"));
    i2.create_sink_pin("A").connect_driver(d1.create_driver_pin("Q"));
    d2.create_sink_pin("R0").connect_driver(i2.create_driver_pin("Y"));
    d2.create_sink_pin("R1").connect_driver(circuit.graph->get_input_pin("a"));
    ff2.create_sink_pin("D").connect_driver(d2.create_driver_pin("Q"));
    circuit.graph->get_output_pin("z").connect_driver(ff2.create_driver_pin("Q"));
  }
  static test::Sidecar with_instances(test::Sidecar s) {
    s.instances = {
        {"d1", test::kDomino},
        {"d2",     test::kQn}
    };
    return s;
  }
};
rapidjson::Document parse(const Stage_timing& timing) {
  rapidjson::Document doc;
  doc.Parse(timing.json.c_str());
  EXPECT_FALSE(doc.HasParseError());
  return doc;
}
const rapidjson::Value* find(const rapidjson::Value& array, const char* key, std::string_view value) {
  for (const auto& item : array.GetArray()) {
    if (item.HasMember(key) && item[key].IsString() && item[key].GetString() == value) {
      return &item;
    }
  }
  return nullptr;
}
const rapidjson::Value* class_of(const rapidjson::Document& doc, std::string_view launch, std::string_view capture) {
  for (const auto& item : doc["classes"].GetArray()) {
    if (item["launch"].GetString() == launch && item["capture"].GetString() == capture) {
      return &item;
    }
  }
  return nullptr;
}
}  // namespace

TEST(StageTiming, EveryLaunchCaptureClassIsHandComputable) {
  Classes    bench("classes");
  const auto timing = bench.evaluate();
  EXPECT_EQ(timing.status, "partial");
  EXPECT_EQ(timing.period_ps, 68);
  EXPECT_EQ(timing.limiting, "domino->domino d2:R0");
  EXPECT_EQ(timing.excluded, 3U);
  EXPECT_TRUE(timing.unmeasured.empty());
  EXPECT_EQ(timing.feedback.delay, 68);
  // Feedback slack is in period terms (slack / capture fraction): WNS is
  // 100 - 68; ff1.Q meets ff1.D at 62 but d1.R0 at 19 of a half cycle (38);
  // d1.Q meets d1.R1 at 26 and d2.R0 at 16 (32), d2.Q only ff2.D (70).
  EXPECT_EQ(timing.feedback.wns, 32);
  EXPECT_EQ(timing.feedback.pins.at("ff1:Q").slack, 38);
  EXPECT_EQ(timing.feedback.pins.at("d1:Q").slack, 32);
  EXPECT_EQ(timing.feedback.pins.at("d2:Q").slack, 70);
  const auto doc = parse(timing);
  for (const auto& [launch, capture, endpoints, wns, period] : {
           std::tuple{  "flop",   "flop", 1U, 62., 38.},
           std::tuple{  "flop", "domino", 1U, 19., 62.},
           std::tuple{"domino", "domino", 2U, 16., 68.},
           std::tuple{"domino",   "flop", 1U, 70., 30.}
  }) {
    const auto* summary = class_of(doc, launch, capture);
    ASSERT_TRUE(summary) << launch << "->" << capture;
    EXPECT_EQ((*summary)["endpoints"].GetUint(), endpoints) << launch << "->" << capture;
    EXPECT_EQ((*summary)["wns_ps"].GetDouble(), wns) << launch << "->" << capture;
    EXPECT_EQ((*summary)["tns_ps"].GetDouble(), 0) << launch << "->" << capture;
    EXPECT_EQ((*summary)["period_ps"].GetDouble(), period) << launch << "->" << capture;
  }
  EXPECT_STREQ((*class_of(doc, "domino", "domino"))["worst"].GetString(), "d2:R0");
  EXPECT_EQ(doc["classes"].Size(), 4U);
  const auto& period = doc["period"];
  EXPECT_EQ(period["values"]["graph_ps"].GetDouble(), 68);
  EXPECT_EQ(period["values"]["internal_ps"].GetDouble(), 58);   // 2 * (20 + 7 + 2), the FFQN variant
  EXPECT_EQ(period["values"]["precharge_ps"].GetDouble(), 50);  // 2 * (20 + 5)
  EXPECT_EQ(period["values"]["domino->flop_ps"].GetDouble(), 30);
  std::vector<std::string> absent;
  for (const auto& name : period["absent"].GetArray()) {
    absent.emplace_back(name.GetString());
  }
  EXPECT_EQ(absent, (std::vector<std::string>{"flop->output", "domino->output", "input->flop", "input->domino", "input->output"}));

  // The stages: IDs, owners, rails with the path split, constant checks.
  ASSERT_EQ(doc["stages"].Size(), 2U);
  const auto* d1 = find(doc["stages"], "stage_id", "d1");
  ASSERT_TRUE(d1);
  EXPECT_STREQ((*d1)["source_state"]["state"].GetString(), "d1");
  EXPECT_STREQ((*d1)["source_state"]["region"].GetString(), "top");
  EXPECT_STREQ((*d1)["hold"].GetString(), "unknown");
  EXPECT_STREQ((*d1)["clock"].GetString(), "clk");
  const auto* r0 = find((*d1)["rails"], "pin", "d1:R0");
  ASSERT_TRUE(r0);
  EXPECT_EQ((*r0)["arrival_ps"].GetDouble(), 27);
  EXPECT_EQ((*r0)["required_ps"].GetDouble(), 46);
  EXPECT_EQ((*r0)["slack_ps"].GetDouble(), 19);
  EXPECT_EQ((*r0)["fraction"].GetDouble(), .5);
  EXPECT_STREQ((*r0)["capture_clock"].GetString(), "clk");
  EXPECT_STREQ((*r0)["launch"]["pin"].GetString(), "ff1:Q");
  EXPECT_STREQ((*r0)["launch"]["class"].GetString(), "flop");
  EXPECT_STREQ((*r0)["launch"]["clock"].GetString(), "clk");
  EXPECT_EQ((*r0)["path"]["clock_to_q_ps"].GetDouble(), 20);
  EXPECT_EQ((*r0)["path"]["cell_ps"].GetDouble(), 7);
  EXPECT_EQ((*r0)["path"]["wire_ps"].GetDouble(), 0);
  EXPECT_EQ((*d1)["internal"]["period_ps"].GetDouble(), 54);
  EXPECT_EQ((*d1)["internal"]["slack_ps"].GetDouble(), 50 - 27);
  EXPECT_EQ((*d1)["precharge"]["slack_ps"].GetDouble(), 50 - 25);
  EXPECT_EQ((*d1)["output"]["t_cq_ps"].GetDouble(), 20);
  EXPECT_EQ((*d1)["output"]["max_capacitance_ff"].GetDouble(), 50);
  const auto* d2 = find(doc["stages"], "stage_id", "d2");
  ASSERT_TRUE(d2);
  const auto* negative = find((*d2)["rails"], "pin", "d2:R0");
  ASSERT_TRUE(negative);
  EXPECT_EQ((*negative)["slack_ps"].GetDouble(), 16);
  EXPECT_STREQ((*negative)["launch"]["class"].GetString(), "domino");
  EXPECT_EQ((*negative)["path"]["cell_ps"].GetDouble(), 10);  // the rail inverter
  const auto* cut = find((*d2)["rails"], "pin", "d2:R1");
  ASSERT_TRUE(cut);
  EXPECT_NE(std::string((*cut)["timing"].GetString()).find("unreached"), std::string::npos);
  // Polarity: the QN register's own arcs, 12 rise and 22 fall, reach ff2.D.
  const auto* d = find(doc["endpoints"], "pin", "ff2:D");
  ASSERT_TRUE(d);
  EXPECT_EQ((*d)["arrival_ps"].GetDouble(), 22);
  EXPECT_STREQ((*d)["transition"].GetString(), "fall");
  EXPECT_EQ((*d)["slack_ps"].GetDouble(), 70);
  EXPECT_STREQ((*d)["launch"]["pin"].GetString(), "d2:Q");
  EXPECT_EQ((*d)["path"]["clock_to_q_ps"].GetDouble(), 22);
  EXPECT_STREQ(doc["coverage"]["early"].GetString(), "unknown");
  EXPECT_TRUE(doc["coverage"]["certified"].GetBool());
  EXPECT_STREQ(doc["constraints"]["kind"].GetString(), "clock-scan");
  std::vector<std::string> excluded;
  for (const auto& item : doc["constraints"]["excluded"].GetArray()) {
    excluded.emplace_back(item["pin"].GetString());
  }
  EXPECT_EQ(excluded, (std::vector<std::string>{"a", "z", "d2:R1"}));
  EXPECT_STREQ(doc["model"]["provisional"]["coefficients"]["eval_delay_ps"]["source"].GetString(), "bundle");
  EXPECT_EQ(doc["model"]["generation"].GetString(), std::format("{:016x}", bench.generation));
}

TEST(StageTiming, InternalAndPrechargeChecksCanLimitThePeriod) {
  Classes bench("internal");
  apply_overrides(bench.view, {.stage_ps = {{"eval_delay_ps", 30}}});
  auto timing = bench.evaluate();
  EXPECT_EQ(timing.period_ps, 78);  // 2 * (30 + 7 + 2) on the FFQN variant
  EXPECT_EQ(timing.limiting, "internal d2");
  EXPECT_STREQ(parse(timing)["model"]["provisional"]["coefficients"]["eval_delay_ps"]["source"].GetString(), "override");
  apply_overrides(bench.view, {.stage_ps = {{"precharge_ps", 40}}});
  timing = bench.evaluate();
  EXPECT_EQ(timing.period_ps, 90);  // 2 * (40 + 5)
  EXPECT_EQ(timing.limiting, "precharge d1");
}

TEST(StageTiming, SharedFanoutTakesTheLargerOfTheFullAndTwiceTheHalfCycle) {
  // ffa.Q -> BUF -> {ffb.D, d.R0}: full 27 + 8 = 35, half 2 * (27 + 3 + 1) = 62.
  Bench bench("shared", {"clk"}, {});
  auto  ffa = bench.flop("ffa"), ffb = bench.flop("ffb");
  auto  buffer = bench.circuit.cell("b", "BUF");
  auto  d      = bench.domino("d", test::kDomino);
  buffer.create_sink_pin("A").connect_driver(ffa.create_driver_pin("Q"));
  ffb.create_sink_pin("D").connect_driver(buffer.create_driver_pin("Y"));
  d.create_sink_pin("R0").connect_driver(buffer.create_driver_pin("Y"));
  d.create_sink_pin("R1").connect_driver(d.create_driver_pin("Q"));
  ffa.create_sink_pin("D").connect_driver(ffb.create_driver_pin("Q"));
  const auto timing = bench.evaluate();
  EXPECT_EQ(timing.status, "complete");
  EXPECT_EQ(timing.period_ps, 62);
  const auto doc = parse(timing);
  EXPECT_EQ((*class_of(doc, "flop", "flop"))["period_ps"].GetDouble(), 35);
  EXPECT_EQ((*class_of(doc, "flop", "domino"))["period_ps"].GetDouble(), 62);
  // No sidecar instance owns d: the stage ID stands, the source state is null.
  EXPECT_TRUE(doc["stages"][0]["source_state"].IsNull());
  EXPECT_NE(std::string(doc["stages"][0]["source_reason"].GetString()).find("no sidecar instance"), std::string::npos);
}

TEST(StageTiming, FlopToFlopEqualsThePlainProxyPeriod) {
  // Without a bundle the stage timer is the native proxy: T - WNS.
  Circuit circuit(test::kLiberty, "plain", {"clk"}, {});
  auto    ff = circuit.cell("ff", "FF"), inv = circuit.cell("inv", "INV");
  ff.create_sink_pin("CLK").connect_driver(circuit.graph->get_input_pin("clk"));
  inv.create_sink_pin("A").connect_driver(ff.create_driver_pin("Q"));
  ff.create_sink_pin("D").connect_driver(inv.create_driver_pin("Y"));
  const auto source = stage_constraints(*circuit.graph, std::nullopt, "plain", {}, Clock_spec{"clk", "clk", 100, {}}, 0);
  const auto plain  = late::Proxy(*circuit.graph, circuit.library, source.constraints).evaluate();
  ASSERT_TRUE(plain.certified);
  const auto timing = evaluate_stage_timing(*circuit.graph, nullptr, circuit.library, nullptr, nullptr, source, 0, {});
  EXPECT_EQ(timing.status, "complete");
  EXPECT_EQ(timing.period_ps, 100 - *plain.wns_ps);
  EXPECT_EQ(timing.period_ps, 38);
  const auto doc = parse(timing);
  EXPECT_TRUE(doc["model"]["provisional"].IsNull());
  EXPECT_TRUE(doc["stages"].Empty());
  EXPECT_EQ((*class_of(doc, "flop", "flop"))["wns_ps"].GetDouble(), *plain.wns_ps);
}

TEST(StageTiming, NullCoefficientsUnknownMastersAndRequireCompleteAreExplicit) {
  // An unmeasured coefficient: no period, the class is listed.
  test::Sidecar sidecar;
  sidecar.stage["eval_delay_ps"].reset();
  sidecar.stage["uncertainty_first_ps"].reset();
  Classes    nulls("nulls", sidecar);
  const auto incomplete = nulls.evaluate();
  EXPECT_EQ(incomplete.status, "incomplete");
  EXPECT_FALSE(incomplete.period_ps);
  EXPECT_EQ(incomplete.unmeasured, (std::vector<std::string>{"eval_delay_ps", "uncertainty_first_ps"}));
  EXPECT_EQ(incomplete.feedback.delay, 0);
  const auto doc = parse(incomplete);
  EXPECT_STREQ(doc["stages"][0]["internal"]["status"].GetString(), "unmeasured");
  EXPECT_TRUE(doc["stages"][0]["precharge"]["period_ps"].IsNumber());
  EXPECT_FALSE(doc["coverage"]["certified"].GetBool());

  // A domino master the bundle does not model is uncharacterized.
  Bench unknown("unknown", {"clk"}, {});
  auto  other   = std::make_shared<late::Provisional_cells>(*unknown.cells);
  auto  foreign = other->library.cells.at(test::kDomino);
  foreign.name  = "__lhd_domino_ffffffffffffffff_v0_t0";
  other->library.cells.emplace(foreign.name, foreign);
  auto stray = unknown.circuit.cell("x", foreign.name, &other->library);
  stray.create_sink_pin("CLK").connect_driver(unknown.circuit.graph->get_input_pin("clk"));
  stray.create_sink_pin("R0").connect_driver(stray.create_driver_pin("Q"));
  const auto uncharacterized = unknown.evaluate();
  EXPECT_EQ(uncharacterized.status, "incomplete");
  ASSERT_FALSE(uncharacterized.reasons.empty());
  EXPECT_NE(uncharacterized.reasons.front().find("uncharacterized cell: x (__lhd_domino_ffffffffffffffff_v0_t0)"),
            std::string::npos)
      << uncharacterized.reasons.front();

  // require_complete names the status and its reasons.
  Classes partial("require");
  try {
    (void)partial.evaluate({.require_complete = true});
    ADD_FAILURE() << "a partial result passed require_complete";
  } catch (const std::runtime_error& error) {
    EXPECT_NE(std::string(error.what()).find("coverage is partial"), std::string::npos) << error.what();
    EXPECT_NE(std::string(error.what()).find("excluded: 3"), std::string::npos) << error.what();
  }
}

TEST(StageTiming, ClockAccountingSumsSinksLoadsAndPlacementWire) {
  // clk drives ff.CLK (3 fF) and d.CLK (4 fF estimate); ff.Q -> d.R0 and
  // d.Q -> ff.D. Data nets carry no resistance, so timing is unchanged.
  Bench bench("clocking", {"clk"}, {});
  auto  ff = bench.flop("ff");
  auto  d  = bench.domino("d", test::kDomino);
  d.create_sink_pin("R0").connect_driver(ff.create_driver_pin("Q"));
  d.create_sink_pin("R1").connect_driver(ff.create_driver_pin("Q"));
  ff.create_sink_pin("D").connect_driver(d.create_driver_pin("Q"));
  physical::Parasitics rc;
  rc.nets = {
      { "clk", {{"clk", .5}, {"ff:CLK", 1}, {"d:CLK", 1.5}}, {{0, 1, 100}, {0, 2, 100}}},
      {"ff:Q",      {{"ff:Q", 0}, {"d:R0", 0}, {"d:R1", 0}},     {{0, 1, 0}, {0, 2, 0}}},
      { "d:Q",                    {{"d:Q", 0}, {"ff:D", 0}},                {{0, 1, 0}}}
  };
  rc.cells = {
      {"ff", 0, 0, 0,          "FF", 2000, 1000},
      { "d", 0, 0, 0, test::kDomino, 3000, 2000}
  };
  const auto timing = bench.evaluate({}, &rc);
  EXPECT_EQ(timing.status, "complete");
  EXPECT_EQ(timing.c_clk_ff, 7);
  const auto  doc   = parse(timing);
  const auto& clock = doc["clock_accounting"]["clocks"]["clk"];
  EXPECT_EQ(clock["domino_sinks"].GetUint(), 1U);
  EXPECT_EQ(clock["cmos_sinks"].GetUint(), 1U);
  EXPECT_EQ(clock["sink_cap_ff"].GetDouble(), 7);
  EXPECT_EQ(clock["domino_cap_ff"].GetDouble(), 4);
  EXPECT_EQ(clock["cmos_cap_ff"].GetDouble(), 3);
  EXPECT_EQ(clock["net_load_ff"].GetDouble(), 7);
  EXPECT_EQ(clock["wire_cap_ff"].GetDouble(), 3);
  EXPECT_EQ(clock["masters"]["FF"]["sinks"].GetUint(), 1U);
  EXPECT_EQ(clock["masters"][test::kDomino.c_str()]["cap_ff"].GetDouble(), 4);
  // Per instance: each clock pin with its master (variant) and capacitance.
  ASSERT_EQ(clock["sinks"].Size(), 2U);
  std::map<std::string, std::tuple<std::string, std::string, double>> sinks;
  for (const auto& sink : clock["sinks"].GetArray()) {
    sinks[sink["pin"].GetString()] = {sink["master"].GetString(), sink["kind"].GetString(), sink["cap_ff"].GetDouble()};
  }
  EXPECT_EQ(sinks.at("ff:CLK"), std::make_tuple(std::string("FF"), std::string("cmos"), 3.));
  EXPECT_EQ(sinks.at("d:CLK"), std::make_tuple(test::kDomino, std::string("domino"), 4.));
  ASSERT_EQ(doc["stages"].Size(), 1U);
  EXPECT_EQ(doc["stages"][0]["clock_cap_ff"].GetDouble(), 4);
  // Area: 2 + 6 um^2 of footprint, the domino's 4 um^2 from the bundle, and
  // the Liberty basis FF 10 + the domino estimate 3.5.
  EXPECT_EQ(doc["area"]["lef_footprint_um2"].GetDouble(), 8);
  EXPECT_EQ(doc["area"]["domino_footprint_um2"].GetDouble(), 4);
  EXPECT_EQ(doc["area"]["liberty_um2"].GetDouble(), 13.5);
  EXPECT_EQ(timing.feedback.area, 8);
  EXPECT_EQ(doc["electrical"]["max_capacitance_violations"].GetUint(), 0U);
  // A missing clock capacitance leaves the total unknown, never zero.
  test::Sidecar no_clock;
  no_clock.clock_ff.reset();
  Bench missing("clock_missing", {"clk"}, {}, no_clock);
  auto  ff2 = missing.flop("ff");
  auto  d2  = missing.domino("d", test::kDomino);
  d2.create_sink_pin("R0").connect_driver(ff2.create_driver_pin("Q"));
  ff2.create_sink_pin("D").connect_driver(d2.create_driver_pin("Q"));
  const auto unknown = missing.evaluate();
  EXPECT_FALSE(unknown.c_clk_ff);
  EXPECT_EQ(unknown.status, "incomplete");
}

TEST(StageTiming, ConstraintSourcesCutUnconstrainedPortsAndRefuseOtherDutyCycles) {
  Classes     bench("sources");
  const auto& graph = *bench.circuit.graph;
  EXPECT_THROW(stage_constraints(graph,
                                 std::nullopt,
                                 "sources",
                                 {
  },
                                 Clock_spec{"clk", "clk", 100, std::array{0., 30.}},
                                 0),
               std::invalid_argument);
  EXPECT_THROW(stage_constraints(graph, std::nullopt, "sources", {}, Clock_spec{"clk", "nope", 100, {}}, 0), std::invalid_argument);
  EXPECT_THROW(stage_constraints(graph, std::nullopt, "sources", {}, std::nullopt, 0), std::invalid_argument);
  const auto shifted = stage_constraints(graph,
                                         std::nullopt,
                                         "sources",
                                         {
  },
                                         Clock_spec{"", "clk", 100, std::array{10., 60.}},
                                         2);
  EXPECT_EQ(shifted.constraints.clocks.at("clk").phase_ps, 10);
  EXPECT_EQ(shifted.constraints.clocks.at("clk").uncertainty_ps, 2);
  // A saved SDC: a has an input delay, z is still cut.
  const std::string sdc
      = "current_design sources\n"
        "create_clock -name core -period 100 -waveform {0 50} [get_ports {clk}]\n"
        "set_input_delay 5 -clock [get_clocks {core}] [get_ports {a}]\n";
  const auto saved = stage_constraints(graph, sdc, "sources", {}, std::nullopt, 0);
  EXPECT_EQ(saved.kind, "saved-sdc");
  EXPECT_EQ(saved.excluded,
            (std::vector<std::pair<std::string, std::string>>{
                {"z", "io-unconstrained: no output delay"}
  }));
  auto       timing = evaluate_stage_timing(graph, nullptr, bench.circuit.library, bench.cells, &bench.view, saved, 0, {});
  // input->domino: d2.R1 at 5 against 46, period 100 - 2 * 41 = 18.
  const auto doc    = parse(timing);
  ASSERT_TRUE(class_of(doc, "input", "domino"));
  EXPECT_EQ((*class_of(doc, "input", "domino"))["period_ps"].GetDouble(), 18);
  EXPECT_EQ(timing.status, "partial");
  EXPECT_EQ(timing.period_ps, 68);
  EXPECT_THROW(stage_constraints(graph,
                                 "current_design sources\ncreate_clock -name c -period 100 -waveform {0 30} [get_ports {clk}]\n",
                                 "sources",
                                 {},
                                 std::nullopt,
                                 0),
               std::invalid_argument);
  // Outside the SDC subset: the named clock, with the reason noted.
  const auto fallback = stage_constraints(graph, "set x 1\n", "sources", {}, Clock_spec{"clk", "clk", 100, {}}, 0);
  EXPECT_EQ(fallback.kind, "clock-scan");
  ASSERT_EQ(fallback.notes.size(), 1U);
  EXPECT_THROW(stage_constraints(graph, "set x 1\n", "sources", {}, std::nullopt, 0), std::invalid_argument);
  // A spec with no known period for its port: the saved SDC still times, the
  // fallback is refused with the reason.
  Clock_spec unresolved{"", "clk", 0, {}, "the SDC has 2 clocks and none resolves on port clk"};
  EXPECT_EQ(stage_constraints(graph, sdc, "sources", {}, unresolved, 0).kind, "saved-sdc");
  try {
    (void)stage_constraints(graph, "set x 1\n", "sources", {}, unresolved, 0);
    ADD_FAILURE() << "an unresolved clock spec was timed";
  } catch (const std::invalid_argument& error) {
    EXPECT_NE(std::string(error.what()).find("none resolves on port clk; the saved-SDC subset refused it"), std::string::npos)
        << error.what();
  }
  EXPECT_THROW(check_clock_spec(unresolved), std::invalid_argument);
}

TEST(StageTiming, TheStageTimerIsTheDirectEvaluation) {
  // One timer (Liberty text, bundle directory, clock spec) reproduces the
  // direct evaluation byte for byte, and applies overrides at construction.
  Classes            bench("timer");
  Stage_timer_inputs inputs;
  inputs.liberty = test::kLiberty;
  inputs.bundle  = bench.sidecar.write("timer_bundle");
  inputs.design  = bench.circuit.graph->get_name();
  inputs.clock   = Clock_spec{"clk", "clk", 100, {}};
  const Stage_timer timer(inputs);
  const auto        direct = bench.evaluate();
  const auto        timed  = timer.evaluate(*bench.circuit.graph, nullptr, nullptr, {});
  EXPECT_EQ(timed.json, direct.json);
  EXPECT_EQ(timer.generation(), bench.generation);
  ASSERT_TRUE(timed.period_ps);
  EXPECT_EQ(timed.feedback.delay, *timed.period_ps);
  // eval at 4 FO4 (40 ps): the internal check 2 (40 + 7 + 2) = 98 limits.
  inputs.overrides.eval_fo4 = 4;
  const Stage_timer slow(inputs);
  EXPECT_NE(slow.generation(), timer.generation());
  EXPECT_EQ(slow.evaluate(*bench.circuit.graph, nullptr, nullptr, {}).period_ps, 98);
  inputs.bundle.reset();
  EXPECT_THROW(Stage_timer{inputs}, std::invalid_argument);  // overrides need a bundle
  // A word-valued port needs the placed floorplan for its bit-level import.
  Circuit words(test::kLiberty, "words", {"clk"}, {});
  words.graph->get_io()->add_input("bus", 9);
  words.graph->get_io()->set_bits("bus", 4);
  inputs.overrides = {};
  EXPECT_THROW((void)Stage_timer(inputs).evaluate(*words.graph, nullptr, nullptr, {}), std::invalid_argument);
}
}  // namespace livehd::domplace::timing

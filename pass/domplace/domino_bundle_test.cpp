// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "domino_bundle.hpp"

#include <filesystem>
#include <fstream>
#include <iterator>

#include "graph_library_singleton.hpp"
#include "gtest/gtest.h"
#include "rapidjson/document.h"

namespace livehd::domplace {
namespace {
namespace fs                = std::filesystem;
namespace ph                = physical;
using Kind                  = usyn::Gate_formula::Kind;
using Node                  = usyn::Gate_formula::Node;
constexpr const char* kTlef = "inou/lefdef/tests/data/sky130hd.tlef";
constexpr const char* kLef  = "inou/lefdef/tests/data/sky130hd.lef";
constexpr const char* kInv  = "sky130_fd_sc_hd__inv_1";
constexpr const char* kLib  = "inou/lefdef/tests/data/sky130hd.lib";

Node          lit(uint32_t variable, bool negative = false) { return {Kind::literal, 0, 0, variable, negative}; }
Node          series(uint32_t left, uint32_t right) { return {Kind::series, left, right}; }
Node          parallel(uint32_t left, uint32_t right) { return {Kind::parallel, left, right}; }
Domino_master master(std::vector<Node> nodes, uint32_t inputs) {
  usyn::Gate_formula formula;
  formula.nodes = std::move(nodes);
  usyn::Budget work{100000};
  auto         result = make_domino_master(formula, inputs, work);
  EXPECT_TRUE(result);
  return *result;
}
std::string read(const fs::path& path) {
  std::ifstream file(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

struct Fixture {
  ph::Technology   tech = ph::read_lef({kTlef, kLef});
  Domino_reference reference;
  Bundle_inputs    inputs;
  Bundle_design    design;
  Fixture() {
    std::string why;
    auto        resolved = resolve_domino_reference(kLib, "", "", 4, 3, &why);
    EXPECT_TRUE(resolved) << why;
    reference          = std::move(*resolved);
    inputs.reference   = &reference;
    const auto route   = domino_route_policy(tech, &inputs.technology);
    inputs.geometry    = geometry_inputs(tech, reference.inverter.name, route);
    inputs.stage.clock = "clk";
    inputs.provenance  = {
        {"lef", "sky130hd fixture"}
    };
    // (a & b) | (c & d), and the binate enable mux en ? x : q (both rails of en).
    design.masters = {
        master({lit(0), lit(1), series(0, 1), lit(2), lit(3), series(3, 4), parallel(2, 5)}, 4),
        master({lit(0), lit(1), series(0, 1), lit(0, true), lit(2), series(3, 4), parallel(2, 5)}, 3),
    };
    std::sort(design.masters.begin(), design.masters.end(), [](const auto& a, const auto& b) { return a.base < b.base; });
    design.instances = {
        {"top", "r1", domino_interface(design.masters[0], 0).name, 0},
        {"top", "r2", domino_interface(design.masters[1], 0).name, 1},
    };
  }
};
}  // namespace

TEST(DominoBundle, WritesARoundTrippingDeterministicBundle) {
  Fixture    f;
  const auto first  = fs::path(testing::TempDir()) / "bundle_a";
  const auto second = fs::path(testing::TempDir()) / "bundle_b";
  const auto a      = generate_domino_bundle(f.design, f.inputs, first);
  ASSERT_TRUE(a.ok) << a.reason;
  EXPECT_EQ(a.macros, f.design.masters.size() * f.reference.registers.size() * 2);  // t0 and t1 of every variant
  const auto b = generate_domino_bundle(f.design, f.inputs, second);
  ASSERT_TRUE(b.ok) << b.reason;
  for (const auto file : {kBundleLef, kBundleSidecar, kBundleVerilog, kBundleAccess}) {
    EXPECT_FALSE(read(first / file).empty()) << file;
    EXPECT_EQ(read(first / file), read(second / file)) << file;
  }
  EXPECT_EQ(a.diagnostics, b.diagnostics);

  // The abstracts read back after the technology and cell LEFs.
  const auto back = ph::read_lef({kTlef, kLef, (first / kBundleLef).string()});
  for (const auto& m : f.design.masters) {
    for (uint32_t rail_template = 0; rail_template < 2; ++rail_template) {
      const auto& macro = back.macros.at(domino_interface(m, 0, rail_template).name);
      EXPECT_EQ(macro.klass, "CORE");
      EXPECT_DOUBLE_EQ(macro.height, 5.44);
      // The outer rails: the inverter's bottom one (VGND) for t0, its top one for t1.
      const auto& outer = macro.pins.at(rail_template ? "VPWR" : "VGND");
      EXPECT_EQ(outer.shapes.size(), 2 * back.macros.at(kInv).pins.at(rail_template ? "VPWR" : "VGND").shapes.size());
    }
    EXPECT_EQ(back.macros.at(domino_interface(m, 0, 1).name).width, back.macros.at(domino_interface(m, 0).name).width);
  }

  rapidjson::Document doc;
  const auto          text = read(first / kBundleSidecar);
  doc.Parse(text.c_str());
  ASSERT_FALSE(doc.HasParseError());
  EXPECT_EQ(doc["schema"].GetUint(), kBundleSchema);
  EXPECT_STREQ(doc["units"]["time"].GetString(), "ps");
  EXPECT_STREQ(doc["units"]["capacitance"].GetString(), "fF");
  EXPECT_EQ(doc["units"]["dbu"].GetUint(), 1000U);
  EXPECT_STREQ(doc["technology"]["route_policy"].GetString(), "lef-stack");
  EXPECT_STREQ(doc["technology"]["site"].GetString(), "unithd");
  EXPECT_STREQ(doc["row_access"]["name"].GetString(), "stand-in");
  EXPECT_STREQ(doc["stage_model"]["clock"].GetString(), "clk");
  // Missing data is an explicit null, never zero.
  EXPECT_TRUE(doc["stage_model"]["hold_capture_ps"].IsNull());
  EXPECT_TRUE(doc["coefficients"]["eval_fo4"].IsNull());
  // No option: every stage coefficient is the labeled logical-effort
  // estimate (domino_delay_test has the hand computation), recorded with its
  // FO4 derivation and parameters. Evaluate and precharge are per variant.
  const auto& stage   = doc["stage_model"];
  const auto& sources = stage["sources"];
  EXPECT_FALSE(stage.HasMember("eval_delay_ps"));
  EXPECT_FALSE(stage.HasMember("precharge_ps"));
  for (const auto* name : {"eval_delay_ps", "precharge_ps", "setup_eval_ps", "precharge_margin_ps"}) {
    EXPECT_STREQ(sources[name].GetString(), "logical-effort-estimate") << name;
  }
  EXPECT_STREQ(sources["uncertainty_first_ps"].GetString(), "estimate: ideal clock (0 ps)");
  EXPECT_EQ(stage["uncertainty_first_ps"].GetDouble(), 0);
  EXPECT_EQ(stage["uncertainty_second_ps"].GetDouble(), 0);
  const double fo4 = doc["fo4_ps"]["value"].GetDouble();
  const double tau = fo4 / 5;
  const double eval_ps = (2 + 13.0 / 3 + 1.681 / 2.39 + 1) * tau;
  EXPECT_NEAR(fo4, 82.91234659320048, 1e-6);
  EXPECT_DOUBLE_EQ(stage["setup_eval_ps"].GetDouble(), .5 * fo4);
  EXPECT_DOUBLE_EQ(stage["precharge_margin_ps"].GetDouble(), .5 * fo4);
  const auto& model = doc["delay_model"];
  EXPECT_STREQ(model["source"].GetString(), "logical-effort-estimate");
  EXPECT_NE(std::string(model["citation"].GetString()).find("Sutherland, R. Sproull, D. Harris, Logical Effort"),
            std::string::npos);
  EXPECT_NE(std::string(model["formula"].GetString()).find("(s+1)/3"), std::string::npos);
  EXPECT_EQ(model["parameters"]["p_inv"].GetDouble(), 1);
  EXPECT_EQ(model["parameters"]["internal_node_weight"].GetDouble(), .5);
  EXPECT_DOUBLE_EQ(model["tau_ps"].GetDouble(), tau);
  EXPECT_STREQ(model["fo4"]["inverter"].GetString(), "sky130_fd_sc_hd__inv_1");
  EXPECT_DOUBLE_EQ(model["fo4"]["load_ff"].GetDouble(), 4 * 2.39);
  EXPECT_FALSE(model.HasMember("issue"));
  EXPECT_TRUE(doc["reference"]["registers"][0]["hold_ps"].IsNull());
  EXPECT_EQ(doc["coefficients"]["aux_nonregister"].GetUint(), 4U);
  EXPECT_STREQ(doc["reference"]["inverter"]["name"].GetString(), "sky130_fd_sc_hd__inv_1");
  EXPECT_STREQ(doc["reference"]["registers"][0]["name"].GetString(), "sky130_fd_sc_hd__dfxtp_1");

  // The 4-literal master is the exact 14-site example; sky130 pin access is
  // reported as unverified, never as passed.
  const auto& masters = doc["masters"];
  ASSERT_EQ(masters.Size(), 2U);
  bool found = false;
  for (const auto& m : masters.GetArray()) {
    const auto& v = m["variants"][0];
    EXPECT_STREQ(v["halves"][0].GetString(), "horizontal");
    EXPECT_FALSE(v["access_check"]["verified"].GetBool());
    EXPECT_FALSE(v["access_check"]["ok"].GetBool());
    // The CLK load estimate: the register's CK input plus two clocked devices
    // at half an inverter input each.
    EXPECT_DOUBLE_EQ(v["pin_caps"]["clock_ff"].GetDouble(),
                     doc["reference"]["registers"][0]["clock_capacitance_ff"].GetDouble()
                         + doc["reference"]["inverter"]["input_capacitance_ff"].GetDouble());
    EXPECT_EQ(v["setup_capture_ps"], doc["reference"]["registers"][0]["setup_ps"]);
    // Both masters are two branches of two in series (s=2, b=2, n=4) into
    // dfxtp_1's 1.681 fF D input, half-inverter devices (U26):
    // (2 + 13/3 + 1.681/2.39 + 1) tau and 8/3 tau.
    EXPECT_NEAR(v["timing"]["eval_delay_ps"].GetDouble(), eval_ps, 1e-6);
    EXPECT_NEAR(v["timing"]["precharge_ps"].GetDouble(), 8.0 / 3 * tau, 1e-6);
    EXPECT_DOUBLE_EQ(v["timing"]["register_d_ff"].GetDouble(), 1.681);
    EXPECT_DOUBLE_EQ(v["timing"]["estimate"]["g_dynamic"].GetDouble(), 1);
    if (m["n_pdn"].GetUint() == 4 && m["inputs"].GetUint() == 4) {
      found = true;
      EXPECT_EQ(v["width_dbu"].GetUint(), 14U * 460U);
      EXPECT_NEAR(v["a_estimate_um2"].GetDouble(), 35.0336, 1e-9);
      EXPECT_STREQ(m["next_state"].GetString(), "((R0 & R1) | (R2 & R3))");
    }
  }
  EXPECT_TRUE(found);
  bool unverified = false;
  for (const auto& note : doc["diagnostics"].GetArray()) {
    unverified |= std::string_view(note.GetString()).starts_with("pin access unverified");
  }
  EXPECT_TRUE(unverified);
  ASSERT_EQ(doc["instances"].Size(), 2U);
  EXPECT_STREQ(doc["instances"][1]["state"].GetString(), "r2");

  // Placement's view and the lg models.
  std::string why;
  const auto  sidecar = read_domino_sidecar(first, &why);
  ASSERT_TRUE(sidecar) << why;
  EXPECT_EQ(sidecar->lef, first / kBundleLef);
  EXPECT_EQ(sidecar->row_access, ph::Row_access_convention{});
  EXPECT_EQ(sidecar->templates.size(), a.macros);
  EXPECT_EQ(sidecar->instances, 2U);
  EXPECT_EQ(sidecar->templates.at(f.design.instances[0].cell), (std::array{ph::Row_access::horizontal, ph::Row_access::vertical}));
  for (const auto& entry : sidecar->catalog) {
    EXPECT_NEAR(entry.eval_delay_ps.value_or(0), eval_ps, 1e-6) << entry.name;
    EXPECT_NEAR(entry.precharge_ps.value_or(0), 8.0 / 3 * tau, 1e-6) << entry.name;
  }
  EXPECT_TRUE(a.delay_issue.empty()) << a.delay_issue;
  {
    const auto                          models = (first / kBundleModels).string();
    Hhds_graph_library::Scoped_instance scope(models);
    for (const auto& instance : f.design.instances) {
      const auto io = Hhds_graph_library::instance(models).find_io(instance.cell);
      ASSERT_TRUE(io) << instance.cell;
      EXPECT_TRUE(io->has_graph());
    }
  }
  EXPECT_NE(read(first / kBundleVerilog).find("module " + f.design.instances[0].cell), std::string::npos);
}

TEST(DominoBundle, SchemaFourRecordsRegisterPinsClockLoadsCoefficientsTemplatesAndTheCatalog) {
  Fixture    f;
  // A second rung: the fixture's register widened in LEF, with its own setup
  // and no CK capacitance or drive sample.
  const auto base_register            = f.reference.registers.front().name;
  f.tech.macros["test_dfxtp_2"]       = f.tech.macros.at(base_register);
  f.tech.macros["test_dfxtp_2"].width = 8.28;
  auto second                         = f.reference.registers.front();
  second.name                         = "test_dfxtp_2";
  second.setup_ps                     = 42;
  second.clock_ff.reset();
  second.drive.reset();
  f.reference.registers.push_back(second);
  f.reference.clock_devices            = 4;
  f.inputs.eval_fo4                    = 2;
  f.inputs.precharge_fo4               = 2;
  f.inputs.setup_eval_fo4              = .5;
  f.inputs.precharge_margin_fo4        = .25;
  f.inputs.area_margin_pct             = 25;
  f.inputs.stage.uncertainty_first_ps  = 0;
  f.inputs.stage.uncertainty_second_ps = 3;
  const auto dir                       = fs::path(testing::TempDir()) / "bundle_schema4";
  const auto result                    = generate_domino_bundle(f.design, f.inputs, dir);
  ASSERT_TRUE(result.ok) << result.reason;
  rapidjson::Document doc;
  const auto          text = read(dir / kBundleSidecar);
  doc.Parse(text.c_str());
  ASSERT_FALSE(doc.HasParseError());
  EXPECT_EQ(doc["schema"].GetUint(), 6U);
  EXPECT_STREQ(doc["technology"]["row_rails"]["bottom"].GetString(), "VGND");
  EXPECT_STREQ(doc["technology"]["row_rails"]["top"].GetString(), "VPWR");
  const double fo4   = doc["fo4_ps"]["value"].GetDouble();
  const auto&  stage = doc["stage_model"];
  // Explicit FO4 options win over the estimate, coefficient by coefficient.
  for (const auto& m : doc["masters"].GetArray()) {
    for (const auto& v : m["variants"].GetArray()) {
      EXPECT_DOUBLE_EQ(v["timing"]["eval_delay_ps"].GetDouble(), 2 * fo4);
      EXPECT_DOUBLE_EQ(v["timing"]["precharge_ps"].GetDouble(), 2 * fo4);
      EXPECT_TRUE(v["timing"]["estimate"].IsObject());  // still recorded beside the option
    }
  }
  for (const auto* name : {"eval_delay_ps", "precharge_ps", "setup_eval_ps", "precharge_margin_ps", "uncertainty_first_ps"}) {
    EXPECT_STREQ(stage["sources"][name].GetString(), "option") << name;
  }
  EXPECT_DOUBLE_EQ(stage["setup_eval_ps"].GetDouble(), .5 * fo4);
  EXPECT_DOUBLE_EQ(stage["precharge_margin_ps"].GetDouble(), .25 * fo4);
  EXPECT_EQ(stage["uncertainty_first_ps"].GetDouble(), 0);  // an explicit zero, not null
  EXPECT_EQ(stage["uncertainty_second_ps"].GetDouble(), 3);
  EXPECT_TRUE(stage["hold_capture_ps"].IsNull());
  const auto& coefficients = doc["coefficients"];
  EXPECT_EQ(coefficients["pair_area_pct"].GetUint(), 100U);
  EXPECT_EQ(coefficients["area_margin_pct"].GetUint(), 25U);
  EXPECT_EQ(coefficients["clock_devices"].GetUint(), 4U);
  EXPECT_EQ(coefficients["setup_eval_fo4"].GetDouble(), .5);
  EXPECT_EQ(coefficients["precharge_margin_fo4"].GetDouble(), .25);
  const auto& registers = doc["reference"]["registers"];
  EXPECT_STREQ(registers[0]["pins"]["d"].GetString(), "D");
  EXPECT_STREQ(registers[0]["pins"]["clk"].GetString(), "CLK");
  EXPECT_STREQ(registers[0]["pins"]["q"].GetString(), "Q");
  const double ck   = registers[0]["clock_capacitance_ff"].GetDouble();
  const double c_in = doc["reference"]["inverter"]["input_capacitance_ff"].GetDouble();
  EXPECT_NEAR(ck, 1.877, 1e-9);  // dfxtp_1 CLK, the larger of rise and fall
  EXPECT_TRUE(registers[1]["clock_capacitance_ff"].IsNull());
  for (const auto& m : doc["masters"].GetArray()) {
    const auto& variants = m["variants"];
    ASSERT_EQ(variants.Size(), 4U);  // two rungs, each in PG rail templates t0 and t1
    for (rapidjson::SizeType i = 0; i < variants.Size(); ++i) {
      const auto& v    = variants[i];
      const bool  t1   = i % 2;
      const auto& twin = variants[i ^ 1];
      EXPECT_EQ(v["template"].GetUint(), unsigned(t1));
      EXPECT_STREQ(v["rail_twin"].GetString(), twin["name"].GetString());
      EXPECT_STREQ(v["register"].GetString(), twin["register"].GetString());
      EXPECT_EQ(v["width_dbu"], twin["width_dbu"]);
      EXPECT_STREQ(v["pg_rails"][0].GetString(), t1 ? "VPWR" : "VGND");
      EXPECT_STREQ(v["pg_rails"][1].GetString(), t1 ? "VGND" : "VPWR");
      EXPECT_STREQ(v["pg_rails"][2].GetString(), t1 ? "VPWR" : "VGND");
      EXPECT_STREQ(v["start_orientations"][0].GetString(), t1 ? "FS" : "N");
      // On alternating N/FS rows each template fits one start parity.
      EXPECT_EQ(v["access_check"]["even_starts"].GetBool(), !t1);
      EXPECT_EQ(v["access_check"]["odd_starts"].GetBool(), t1);
    }
    EXPECT_DOUBLE_EQ(variants[0]["pin_caps"]["clock_ff"].GetDouble(), ck + 2 * c_in);
    EXPECT_NE(std::string(variants[0]["pin_caps"]["source"].GetString()).find("4/2 reference-inverter inputs"), std::string::npos);
    EXPECT_TRUE(variants[2]["pin_caps"]["clock_ff"].IsNull());
    EXPECT_EQ(variants[0]["setup_capture_ps"], registers[0]["setup_ps"]);
    EXPECT_EQ(variants[2]["setup_capture_ps"].GetDouble(), 42);  // each variant's own register
  }
  bool null_clock = false;
  for (const auto& note : doc["diagnostics"].GetArray()) {
    null_clock |= std::string_view(note.GetString()).starts_with("pin_caps.clock_ff of the test_dfxtp_2 variants is null");
  }
  EXPECT_TRUE(null_clock);

  std::string why;
  const auto  sidecar = read_domino_sidecar(dir, &why);
  ASSERT_TRUE(sidecar) << why;
  ASSERT_EQ(sidecar->catalog.size(), 4 * f.design.masters.size());
  for (size_t i = 0; i < sidecar->catalog.size(); ++i) {
    const auto& entry         = sidecar->catalog[i];
    const auto& master        = f.design.masters[i / 4];
    const auto  rung          = (i % 4) / 2;
    const auto  rail_template = uint32_t(i % 2);
    EXPECT_EQ(entry.name, domino_interface(master, rung, rail_template).name);
    EXPECT_EQ(entry.family, master.base + "_t" + std::to_string(rail_template));
    EXPECT_EQ(entry.rank, rung);  // the ladder order within each template's family
    EXPECT_EQ(sidecar->rail_twins.at(entry.name), domino_interface(master, rung, 1 - rail_template).name);
    EXPECT_EQ(sidecar->templates.at(entry.name), (std::array{ph::Row_access::horizontal, ph::Row_access::vertical}));
    EXPECT_EQ(entry.register_cell, f.reference.registers[rung].name);
    EXPECT_EQ(entry.rails, domino_interface(master, 0).rails);
    EXPECT_EQ(entry.width_dbu, static_cast<int64_t>(doc["masters"][i / 4]["variants"][i % 4]["width_dbu"].GetUint()));
    EXPECT_GT(entry.a_footprint_um2, entry.a_estimate_um2 - 1e-9);
    // U26: each rail is its PDN devices x 1/2 reference-inverter input.
    const auto devices = rail_devices(master.formula, master.rails);
    ASSERT_EQ(entry.rails_ff.size(), devices.size());
    for (size_t k = 0; k < devices.size(); ++k) {
      ASSERT_TRUE(entry.rails_ff[k]);
      EXPECT_DOUBLE_EQ(*entry.rails_ff[k], devices[k] * 0.5 * c_in) << k;
    }
    EXPECT_EQ(entry.clock_ff.has_value(), rung == 0);
    EXPECT_EQ(entry.drive.has_value(), rung == 0);
    EXPECT_EQ(entry.clk_pin, "CLK");  // the register's own pins
    EXPECT_EQ(entry.q_pin, "Q");
    EXPECT_EQ(entry.setup_capture_ps,
              rung ? std::optional<double>{42} : std::optional<double>{registers[0]["setup_ps"].GetDouble()});
  }
  EXPECT_GT(sidecar->catalog[2].width_dbu, sidecar->catalog[0].width_dbu);
  ASSERT_TRUE(sidecar->catalog[0].drive);
  EXPECT_TRUE(sidecar->catalog[0].drive->sequential);
  EXPECT_EQ(sidecar->catalog[0].drive->name, sidecar->catalog[0].name);
  EXPECT_EQ(sidecar->catalog[0].drive->output, "Q");

  // A variant whose register is not a listed reference register is refused.
  auto unlisted = text;
  unlisted.replace(unlisted.find("\"name\":\"test_dfxtp_2\""), 21, "\"name\":\"test_dfxtp_9\"");
  std::ofstream(dir / kBundleSidecar, std::ios::trunc) << unlisted;
  EXPECT_FALSE(read_domino_sidecar(dir, &why));
  EXPECT_NE(why.find("names an unlisted register test_dfxtp_2"), std::string::npos) << why;

  // Only schema 6 is read; an older sidecar (schema 5 had one capacitance
  // for every rail) asks for regeneration.
  auto older = text;
  older.replace(older.find("\"schema\":6"), 10, "\"schema\":5");
  std::ofstream(dir / kBundleSidecar, std::ios::trunc) << older;
  EXPECT_FALSE(read_domino_sidecar(dir, &why));
  EXPECT_NE(why.find("is schema 5; this build reads schema 6 only (regenerate the bundle"), std::string::npos) << why;
}

TEST(DominoBundle, MissingDelayDataIsNamedAndNeverZero) {
  // No D capacitance on the register: FO4 and the stage-wide fractions stand,
  // the per-variant delays are null with a diagnostic.
  Fixture f;
  f.reference.registers.front().d_ff.reset();
  const auto no_load = generate_domino_bundle(f.design, f.inputs, fs::path(testing::TempDir()) / "bundle_no_load");
  ASSERT_TRUE(no_load.ok) << no_load.reason;
  EXPECT_NE(no_load.delay_issue.find("estimate is null for the sky130_fd_sc_hd__dfxtp_1: the register has no D input"),
            std::string::npos)
      << no_load.delay_issue;
  rapidjson::Document doc;
  auto                text = read(fs::path(testing::TempDir()) / "bundle_no_load" / kBundleSidecar);
  doc.Parse(text.c_str());
  ASSERT_FALSE(doc.HasParseError());
  EXPECT_TRUE(doc["masters"][0]["variants"][0]["timing"]["eval_delay_ps"].IsNull());
  EXPECT_TRUE(doc["masters"][0]["variants"][0]["timing"]["estimate"].IsNull());
  EXPECT_TRUE(doc["stage_model"]["setup_eval_ps"].IsNumber());

  // No NLDM model for the inverter: no FO4, so nothing it scales.
  Fixture g;
  g.reference.inverter_timing.reset();
  const auto no_fo4 = generate_domino_bundle(g.design, g.inputs, fs::path(testing::TempDir()) / "bundle_no_fo4");
  ASSERT_TRUE(no_fo4.ok) << no_fo4.reason;
  EXPECT_NE(no_fo4.delay_issue.find("fo4_ps is null: sky130_fd_sc_hd__inv_1 has no Liberty NLDM timing model"), std::string::npos)
      << no_fo4.delay_issue;
  text = read(fs::path(testing::TempDir()) / "bundle_no_fo4" / kBundleSidecar);
  doc.Parse(text.c_str());
  ASSERT_FALSE(doc.HasParseError());
  EXPECT_TRUE(doc["fo4_ps"]["value"].IsNull());
  EXPECT_TRUE(doc["delay_model"]["fo4"].IsNull());
  EXPECT_NE(std::string(doc["delay_model"]["issue"].GetString()).find("NLDM"), std::string::npos);
  EXPECT_TRUE(doc["stage_model"]["setup_eval_ps"].IsNull());
  EXPECT_TRUE(doc["masters"][0]["variants"][0]["timing"]["precharge_ps"].IsNull());
  std::string why;
  const auto  sidecar = read_domino_sidecar(fs::path(testing::TempDir()) / "bundle_no_fo4", &why);
  ASSERT_TRUE(sidecar) << why;
  EXPECT_FALSE(sidecar->catalog.front().eval_delay_ps);
}

TEST(DominoBundle, EqualMastersAreCollectedOnceWithEveryOwner) {
  // Two converted states share a & b: one master, two instances naming their
  // own original states.
  usyn::Logical_design design;
  auto&                region = design.regions.emplace_back();
  region.module_name          = "top";
  region.selected.frozen.emplace();
  for (const auto* name : {"q0", "q1"}) {
    region.selected.frozen->state.push_back({name, 'x', {}, {}, usyn::unknown_clock_domain, {}});
  }
  usyn::Gate_formula formula;
  formula.nodes = {lit(0), lit(1), series(0, 1)};
  usyn::Physical_emission emission;
  emission.cells.push_back(domino_interface(master(formula.nodes, 2), 0));
  for (uint32_t i = 0; i < 2; ++i) {
    auto& state    = emission.states.emplace_back();
    state.state    = i;
    state.decision = usyn::Physical_decision::domino;
    state.formula  = formula;
    state.metrics  = *formula.metrics();
  }
  region.physical = emission;
  usyn::Budget work{100000};
  const auto   out = collect_domino_design(design, work);
  ASSERT_TRUE(out);
  ASSERT_EQ(out->masters.size(), 1U);
  ASSERT_EQ(out->instances.size(), 2U);
  EXPECT_EQ(out->instances[0].cell, emission.cells[0].name);
  EXPECT_EQ(out->instances[1].cell, emission.cells[0].name);
  EXPECT_EQ(out->instances[0].state, "q0");
  EXPECT_EQ(out->instances[1].state, "q1");
  EXPECT_EQ(out->instances[1].state_index, 1U);
  // A selection that names another master than the formula rebuilds is a bug.
  region.physical->cells[0].name = "__lhd_domino_0000000000000000_v0_t0";
  EXPECT_THROW((void)collect_domino_design(design, work), std::logic_error);
}

TEST(DominoBundle, RefusalsLeaveNoBundle) {
  Fixture    f;
  const auto dir = fs::path(testing::TempDir()) / "bundle_refused";
  ASSERT_TRUE(generate_domino_bundle(f.design, f.inputs, dir).ok);
  // Abstracts drawn for another cell than the reference inverter.
  auto other              = f.inputs;
  other.geometry          = geometry_inputs(f.tech, "sky130_fd_sc_hd__buf_1", domino_route_policy(f.tech, nullptr));
  const auto refused      = generate_domino_bundle(f.design, other, dir);
  EXPECT_FALSE(refused.ok);
  EXPECT_NE(refused.reason.find("reference inverter"), std::string::npos) << refused.reason;
  EXPECT_FALSE(fs::exists(dir));
  std::string why;
  EXPECT_FALSE(read_domino_sidecar(dir, &why));
  EXPECT_NE(why.find(std::string(kBundleSidecar)), std::string::npos) << why;
}

TEST(DominoBundle, AFailedAccessCheckRefusesTheBundle) {
  // Bottom rails drawn off the row edge: no placement finds its PG rail, so
  // the access check fails and nothing is written.
  Fixture f;
  for (auto& shape : f.inputs.geometry.footprint.rails[0].shapes) {
    shape.rect = shape.rect.translated(0, 50);
  }
  const auto dir     = fs::path(testing::TempDir()) / "bundle_access";
  const auto refused = generate_domino_bundle(f.design, f.inputs, dir);
  EXPECT_FALSE(refused.ok);
  EXPECT_NE(refused.reason.find("pin access check failed"), std::string::npos) << refused.reason;
  EXPECT_NE(refused.reason.find("PG:"), std::string::npos) << refused.reason;
  EXPECT_FALSE(fs::exists(dir));
}

}  // namespace livehd::domplace

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
  EXPECT_EQ(a.macros, f.design.masters.size() * f.reference.registers.size());
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
    const auto& macro = back.macros.at(domino_interface(m, 0).name);
    EXPECT_EQ(macro.klass, "CORE");
    EXPECT_DOUBLE_EQ(macro.height, 5.44);
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
  EXPECT_TRUE(doc["stage_model"]["eval_delay_ps"].IsNull());
  EXPECT_TRUE(doc["coefficients"]["eval_fo4"].IsNull());
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
    EXPECT_TRUE(v["pin_caps"]["clock_ff"].IsNull());
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

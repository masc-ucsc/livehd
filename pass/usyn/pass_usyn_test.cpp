// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "pass_usyn.hpp"

#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <map>
#include <tuple>

#include "artifact.hpp"
#include "graph_library_singleton.hpp"
#include "gtest/gtest.h"
#include "logical_writer.hpp"
#include "node_util.hpp"
#include "rapidjson/document.h"
#include "tmap.hpp"

namespace {
namespace fs   = std::filesystem;
namespace gu   = livehd::graph_util;
using Registry = livehd::Hhds_graph_library;
class PassSynth : public ::testing::Test {
protected:
  std::string root;
  void        SetUp() override {
    livehd::diag::sink().clear();
    livehd::diag::sink().set_human_stderr(false);
    livehd::diag::sink().set_jsonl_path("off");
    root = (fs::temp_directory_path() / "livehd-usyn-lifetime-XXXXXX").string();
    ASSERT_NE(mkdtemp(root.data()), nullptr);
  }
  void TearDown() override {
    livehd::diag::sink().clear();
    fs::remove_all(root);
  }
};
std::shared_ptr<hhds::Graph> design(hhds::GraphLibrary& library) {
  auto io = library.create_io("top");
  io->add_input("a", 1);
  io->add_input("b", 2);
  io->add_input("clk", 3);
  io->add_output("y", 4);
  for (auto name : {"a", "b", "clk", "y"}) {
    io->set_bits(name, 1);
    io->set_unsign(name, true);
  }
  auto graph = io->create_graph();
  auto node  = gu::create_typed_node(*graph, Ntype_op::And);
  for (auto name : {"a", "b"}) {
    graph->get_input_pin(name).connect_sink(gu::setup_sink_pid(node, 0));
  }
  auto pin = node.create_driver_pin(0);
  gu::set_ubits(pin, 1);
  auto reg = gu::create_typed_node(*graph, Ntype_op::Flop);
  pin.connect_sink(gu::setup_sink_by_name(reg, "din"));
  graph->get_input_pin("clk").connect_sink(gu::setup_sink_by_name(reg, "clock_pin"));
  auto q = reg.create_driver_pin(0);
  gu::set_ubits(q, 1);
  gu::set_pin_name(q, "state");
  q.connect_sink(graph->get_output_pin("y"));
  return graph;
}
rapidjson::Document read(const std::string& path) {
  std::ifstream       input(path);
  std::string         text{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
  rapidjson::Document result;
  result.Parse(text.c_str());
  return result;
}
void run(Eprp_var& var) {
  var.set_stage_labels(var.dict);
  Pass_usyn::work(var);
}

TEST_F(PassSynth, PublicLogicalEntryEmitsStateWithoutAbcOrLibertyAndReleasesScratch) {
  EXPECT_FALSE(livehd::synth::has_tmap_provider("abc"));
  hhds::GraphLibrary        input;
  const auto                source = design(input);
  const auto                output = root + "/published";
  Registry::Scoped_instance scope(output);
  Eprp_var                  var;
  var.graphs.push_back(source);
  var.dict["top"]       = "top";
  var.dict["tmap"]      = "none";
  var.dict["out"]       = output;
  var.dict["qor"]       = root + "/qor.json";
  var.dict["cache_dir"] = root + "/cache";
  // Insertion can rehash the option dictionary and invalidate the reference
  // returned by a preceding assignment. Keep each insertion independent.
  for (const auto flag : {"npn4", "sweep", "balance", "cmos_cleanup", "sop_tree", "p1", "multi_rep"}) {
    var.dict[flag] = "true";
  }
  size_t                instances = 0;
  std::string           first_artifact;
  std::vector<uint64_t> first_work;
  std::vector<uint64_t> first_residual;
  for (unsigned i = 0; i < 3; ++i) {
    run(var);
    const auto& diagnostics = livehd::diag::sink().records();
    ASSERT_FALSE(livehd::diag::sink().has_halting_errors()) << (diagnostics.empty() ? "" : diagnostics.back().message);
    if (i == 0) {
      instances = Registry::registered_instances();
    }
    EXPECT_EQ(Registry::registered_instances(), instances);
    const auto io = Registry::instance(output).find_io("top");
    ASSERT_TRUE(io);
    ASSERT_TRUE(io->get_graph());
    EXPECT_NE(io->get_graph(), source);
    auto report = read(root + "/qor.json.usyn.json");
    ASSERT_FALSE(report.HasParseError());
    EXPECT_EQ(report["schema_version"].GetInt(), 5);
    EXPECT_STREQ(report["output"].GetString(), "logical-cmos");
    EXPECT_EQ(report["totals"]["register_bits"].GetUint64(), 1U);
    EXPECT_EQ(report["totals"]["eligible_endpoints"].GetUint64(), 1U);
    EXPECT_TRUE(report["cache"]["available"].GetBool());
    EXPECT_TRUE(report["cache"]["enabled"].GetBool());
    EXPECT_EQ(report["cache"]["reused"].GetUint64(), i == 0 ? 0U : 1U);
    EXPECT_EQ(report["cache"]["stored"].GetUint64(), i == 0 ? 1U : 0U);
    EXPECT_EQ(report["cache"]["invalid"].GetUint64(), 0U);
    EXPECT_EQ(report["cache"]["credit_misses"].GetUint64(), 0U);
    EXPECT_EQ(report["cache"]["kept"].GetUint64(), 0U);
    {
      // The search's credit floor and the structural work, replayed on a hit.
      const auto& region = report["regions"][0];
      EXPECT_EQ(region["search_credits"].GetUint64(), 4000000000U);
      const auto& floor = region["credit_floor"];
      EXPECT_EQ(floor["work"].GetUint64(),
                region["work"]["total"].GetUint64() - region["work"]["admission"].GetUint64()
                    - region["work"]["cmos_cleanup"].GetUint64());
      EXPECT_GE(floor["floor"].GetUint64(), floor["work"].GetUint64());
      EXPECT_FALSE(floor["bound"].GetBool());
      EXPECT_EQ(floor["credits"].GetUint64(), 0U);
      EXPECT_GT(region["structural_work"].GetUint64(), region["work"]["admission"].GetUint64());
      EXPECT_EQ(region["identity_fallbacks"].GetUint64(), 0U);
      EXPECT_EQ(report["cache"]["replayed_structural_work"].GetUint64(), i == 0 ? 0U : region["structural_work"].GetUint64());
      EXPECT_EQ(report["cache"]["replayed_search_work"].GetUint64(), i == 0 ? 0U : floor["work"].GetUint64());
    }
    EXPECT_TRUE(report["native_optimization"]["p1"].GetBool());
    EXPECT_TRUE(report["native_optimization"]["multi_rep"].GetBool());
    EXPECT_TRUE(report["regions"][0].HasMember("multi_rep"));
    EXPECT_TRUE(report["native_optimization"]["sop_tree"].GetBool());
    EXPECT_TRUE(report["native_optimization"]["cmos_cleanup"].GetBool());
    EXPECT_TRUE(report["native_optimization"]["npn4"].GetBool());
    EXPECT_TRUE(report["native_optimization"]["sweep"].GetBool());
    EXPECT_TRUE(report["native_optimization"]["balance"].GetBool());
    EXPECT_EQ(report["regions"][0]["cmos_cleanup"]["work"].GetUint64(), report["regions"][0]["work"]["cmos_cleanup"].GetUint64());
    EXPECT_EQ(report["endpoint_search"]["pair_choices"].GetUint(), 4U);
    EXPECT_TRUE(report["regions"][0]["pairs"].HasMember("choice_combinations"));
    const auto&           charges = report["regions"][0]["work"];
    std::vector<uint64_t> stages;
    uint64_t              total = 0;
    for (auto key : {"admission", "p1", "selection", "pairs", "residual", "feedback", "cleanup", "cmos_cleanup"}) {
      stages.push_back(charges[key].GetUint64());
      total += stages.back();
    }
    EXPECT_EQ(charges["total"].GetUint64(), total);
    EXPECT_GT(total, 0U);
    if (i == 0) {
      first_work = stages;
    }
    EXPECT_EQ(stages, first_work);  // Warm evidence describes the original search.
    const auto&           residual = report["regions"][0]["residual"];
    std::vector<uint64_t> residual_counts;
    for (auto key : {"cost_before",
                     "cost_after",
                     "rewrite_windows",
                     "rewrite_wins",
                     "resub_windows",
                     "resub_wins",
                     "candidates",
                     "depth_rejections",
                     "cost_rejections",
                     "reference_visits"}) {
      ASSERT_TRUE(residual.HasMember(key)) << key;
      ASSERT_TRUE(residual[key].IsUint64()) << key;
      residual_counts.push_back(residual[key].GetUint64());
    }
    EXPECT_GE(residual["cost_before"].GetUint64(), residual["cost_after"].GetUint64());
    ASSERT_TRUE(residual["exhausted"].IsBool());
    ASSERT_TRUE(residual["limits"].IsArray());
    if (i == 0) {
      first_residual = residual_counts;
    }
    EXPECT_EQ(residual_counts, first_residual);  // A hit preserves rejection evidence too.
    const auto& endpoints = report["regions"][0]["endpoints"];
    ASSERT_EQ(endpoints.Size(), 1U);
    EXPECT_STREQ(endpoints[0]["name"].GetString(), "state");
    EXPECT_TRUE(fs::exists(root + "/qor.json.provenance/manifest.json"));
    const auto& artifact = report["regions"][0]["artifact"];
    EXPECT_EQ(artifact["version"].GetUint(), 4U);
    if (i == 0) {
      first_artifact = artifact["path"].GetString();
    }
    EXPECT_EQ(first_artifact, artifact["path"].GetString());
    std::ifstream file(fs::path(root) / artifact["path"].GetString(), std::ios::binary);
    ASSERT_TRUE(file);
    const std::string    bytes{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    livehd::usyn::Budget work{10000000};
    const auto           loaded = livehd::usyn::deserialize_artifact(bytes, livehd::synth::State_target::cmos, work);
    ASSERT_TRUE(loaded.region) << loaded.reason;
    ASSERT_EQ(loaded.region->netlist.state.size(), 1U);
    EXPECT_EQ(loaded.region->netlist.state[0].name, "state");
    const auto replayed = livehd::usyn::write_logical_module(*loaded.region, work);
    ASSERT_TRUE(replayed.module) << replayed.reason;
    EXPECT_EQ(replayed.module->state.size(), 1U);
  }
}

TEST_F(PassSynth, NativeCellCostModeUsesLibertyContentsAndInvalidatesWarmSelection) {
  hhds::GraphLibrary input;
  Eprp_var           var;
  var.graphs.push_back(design(input));
  var.dict["tmap"]         = "none";
  var.dict["out"]          = root + "/cost-net";
  var.dict["qor"]          = root + "/cost.json";
  var.dict["cache_dir"]    = root + "/cost-cache";
  var.dict["cost_mode"]    = "cells";
  var.dict["library"]      = root + "/cost.lib";
  const auto write_library = [&](double area) {
    std::ofstream out(var.dict["library"]);
    out << R"LIB(library(t) {
      cell(INV) {area:0.2; pin(A) {direction:input;} pin(Y) {direction:output; function:"!A";} }
      cell(NAND) {area:)LIB"
        << area << R"LIB(; pin(A) {direction:input;} pin(B) {direction:input;}
        pin(Y) {direction:output; function:"!(A B)";} }
    })LIB";
  };
  write_library(0.8);
  for (unsigned iteration = 0; iteration < 3; ++iteration) {
    if (iteration == 2) {
      write_library(1.2);
    }
    run(var);
    ASSERT_FALSE(livehd::diag::sink().has_halting_errors());
    const auto report = read(root + "/cost.json.usyn.json");
    ASSERT_FALSE(report.HasParseError());
    EXPECT_STREQ(report["native_optimization"]["cost_mode"].GetString(), "cells");
    EXPECT_TRUE(report["native_optimization"]["multi_rep"].GetBool());
    EXPECT_GT(report["native_optimization"]["cost_model_work"].GetUint64(), 0U);
    EXPECT_EQ(report["cache"]["reused"].GetUint64(), iteration == 1 ? 1U : 0U);
    EXPECT_EQ(report["cache"]["stored"].GetUint64(), iteration == 1 ? 0U : 1U);
    ASSERT_EQ(report["regions"].Size(), 1U);
    EXPECT_STREQ(report["regions"][0]["multi_rep"]["estimate_kind"].GetString(), "legal-cell-covering-proxy");
  }
}

TEST_F(PassSynth, PublicSearchControlsReachNativeSelectionAndRejectInvalidSettings) {
  hhds::GraphLibrary input;
  Eprp_var           var;
  var.graphs.push_back(design(input));
  var.dict["tmap"] = "none";
  var.dict["out"]  = root + "/net";
  var.dict["qor"]  = root + "/qor.json";
  for (const auto value : {"true", "false"}) {
    var.dict["fast_accept"] = value;
    run(var);
    ASSERT_FALSE(livehd::diag::sink().has_halting_errors());
    const auto report = read(root + "/qor.json.usyn.json");
    ASSERT_FALSE(report.HasParseError());
    EXPECT_EQ(report["endpoint_search"]["fast_accept"].GetBool(), std::string_view{value} == "true");
    ASSERT_EQ(report["regions"].Size(), 1U);
    const auto& search = report["regions"][0]["search"];
    ASSERT_EQ(search.Size(), 1U);
    if (std::string_view{value} == "true") {
      EXPECT_EQ(search[0]["boundary_work"].GetUint64(), 0U);
      EXPECT_EQ(search[0]["local_attempts"].GetUint64(), 0U);
    } else {
      // The single AND has no interior node to collapse, but the initial
      // boundary is retained and the later local tier must still run.
      EXPECT_EQ(search[0]["boundaries"].GetUint64(), 1U);
      EXPECT_GT(search[0]["local_attempts"].GetUint64(), 0U);
    }
  }
  for (const auto& [key, value] : {
           std::pair{        "fast_accept", "sometimes"},
           std::pair{              "adder",       "bad"},
           std::pair{         "multiplier",       "bad"},
           std::pair{       "mux_lowering",       "bad"},
           std::pair{         "eq_balance",       "bad"},
           std::pair{       "cmos_cleanup",       "bad"},
           std::pair{           "sop_tree",       "bad"},
           std::pair{          "multi_rep",       "bad"},
           std::pair{          "cost_mode",       "bad"},
           std::pair{"tmap_sharing_fanout",         "1"},
           std::pair{"tmap_sharing_fanout",      "4097"},
           std::pair{               "npn4",       "bad"},
           std::pair{              "sweep",       "bad"},
           std::pair{            "balance",       "bad"},
           std::pair{  "balance_dup_limit",      "1025"},
           std::pair{         "max_fanout",        "-1"},
           std::pair{         "area_relax",       "bad"},
           std::pair{    "boundary_rounds",         "0"},
           std::pair{    "boundary_rounds",        "65"},
           std::pair{            "io_load",       "nan"},
           std::pair{         "reg_margin",        "-3"},
           std::pair{           "boundary",     "maybe"},
           std::pair{    "boundary_buffer",     "maybe"},
           std::pair{        "tmap_trials",         "0"},
           std::pair{        "tmap_trials",         "3"},
           std::pair{       "rewrite_cuts",         "0"},
           std::pair{       "rewrite_cuts",        "33"},
           std::pair{        "adder_block",        "-1"},
           std::pair{     "local_divisors",         "0"},
           std::pair{   "local_candidates",      "4097"},
           std::pair{    "pair_candidates",      "4097"},
           std::pair{        "pair_inputs",        "17"},
           std::pair{       "pair_choices",         "9"},
           std::pair{          "pair_work",         "0"},
           std::pair{        "pair_trials",         "0"}
  }) {
    var.dict.erase("fast_accept");
    var.dict[key]   = value;
    var.dict["out"] = root + "/invalid";
    run(var);
    EXPECT_TRUE(livehd::diag::sink().has_halting_errors()) << key;
    EXPECT_FALSE(fs::exists(root + "/invalid"));
    var.dict.erase(key);
    livehd::diag::sink().clear();
  }
}

TEST_F(PassSynth, UnavailableMappingAndObsoleteOptionsFailWithoutPublishing) {
  hhds::GraphLibrary input;
  Eprp_var           var;
  var.graphs.push_back(design(input));
  var.dict["out"] = root + "/net";
  run(var);
  ASSERT_TRUE(livehd::diag::sink().has_halting_errors());
  EXPECT_EQ(livehd::diag::sink().records().back().code, "tmap-unavailable");
  EXPECT_FALSE(fs::exists(root + "/net"));
  livehd::diag::sink().clear();
  var.dict["tmap"]     = "none";
  var.dict["literals"] = "16";
  run(var);
  EXPECT_TRUE(livehd::diag::sink().has_halting_errors());
  EXPECT_EQ(livehd::diag::sink().records().back().code, "obsolete-option");
  EXPECT_FALSE(fs::exists(root + "/net"));
}

TEST_F(PassSynth, ExhaustionRetainsSourceAndCannotFallBackToAbc) {
  hhds::GraphLibrary input;
  const auto         source     = design(input);
  const auto         old_output = source->get_output_pin("y").get_driver_pin();
  Eprp_var           var;
  var.graphs.push_back(source);
  var.dict["tmap"] = "none";
  var.dict["work"] = "1";
  var.dict["out"]  = root + "/net";
  var.dict["qor"]  = root + "/qor.json";
  run(var);
  EXPECT_TRUE(livehd::diag::sink().has_halting_errors());
  EXPECT_EQ(source->get_output_pin("y").get_driver_pin(), old_output);
  EXPECT_FALSE(fs::exists(root + "/qor.json"));
  EXPECT_FALSE(fs::exists(root + "/net"));
}

// The opt-in domino profile with its explicit requirements: one phase, the
// clock port, a Liberty and the LEFs (the abstracts consume them later).
void domino_profile(Eprp_var& var, const std::string& root) {
  const auto library = root + "/domino.lib";
  std::ofstream(library) << R"LIB(library(t) {
    cell(INV) {area:0.2; pin(A) {direction:input;} pin(Y) {direction:output; function:"!A";} }
    cell(NAND) {area:0.8; pin(A) {direction:input;} pin(B) {direction:input;} pin(Y) {direction:output; function:"!(A B)";} }
    cell(DFF) {area:4; ff(IQ, IQN) {clocked_on:"CLK"; next_state:"D";}
      pin(CLK) {direction:input; clock:true;} pin(D) {direction:input;} pin(Q) {direction:output; function:"IQ";} }
  })LIB";
  // A one-file technology and cell LEF for the provisional abstracts: one
  // site, vertical M1 pins, a horizontal M2 escape, vertical M3, and the
  // Liberty cells with matching footprints and edge PG rails.
  std::string lef = R"LEF(VERSION 5.8 ;
BUSBITCHARS "[]" ;
DIVIDERCHAR "/" ;
UNITS DATABASE MICRONS 1000 ; END UNITS
MANUFACTURINGGRID 0.005 ;
SITE core CLASS CORE ; SYMMETRY Y ; SIZE 0.2 BY 1 ; END core
LAYER M1 TYPE ROUTING ; DIRECTION VERTICAL ; PITCH 0.2 ; OFFSET 0.1 ; WIDTH 0.06 ; SPACING 0.06 ; END M1
LAYER V1 TYPE CUT ; WIDTH 0.06 ; SPACING 0.06 ; END V1
LAYER M2 TYPE ROUTING ; DIRECTION HORIZONTAL ; PITCH 0.2 ; OFFSET 0.1 ; WIDTH 0.06 ; SPACING 0.06 ; END M2
LAYER V2 TYPE CUT ; WIDTH 0.06 ; SPACING 0.06 ; END V2
LAYER M3 TYPE ROUTING ; DIRECTION VERTICAL ; PITCH 0.2 ; OFFSET 0.1 ; WIDTH 0.06 ; SPACING 0.06 ; END M3
VIA VIA12 DEFAULT
  LAYER V1 ; RECT -0.03 -0.03 0.03 0.03 ;
  LAYER M1 ; RECT -0.03 -0.05 0.03 0.05 ;
  LAYER M2 ; RECT -0.05 -0.03 0.05 0.03 ;
END VIA12
)LEF";
  for (const auto& [cell, width, pins] : {
           std::tuple{ "INV", "0.2", std::vector<std::string>{"A", "Y"}},
           {"NAND", "0.8",                    {"A", "B", "Y"}},
           { "DFF",   "4",                  {"CLK", "D", "Q"}}
  }) {
    lef += std::format("MACRO {0}\n  CLASS CORE ;\n  ORIGIN 0 0 ;\n  SIZE {1} BY 1 ;\n  SYMMETRY X Y ;\n  SITE core ;\n",
                       cell,
                       width);
    for (size_t i = 0; i < pins.size(); ++i) {
      lef += std::format("  PIN {0} DIRECTION {1} ; USE SIGNAL ; PORT LAYER M1 ; RECT {2:.2f} 0.2 {3:.2f} 0.8 ; END END {0}\n",
                         pins[i],
                         i + 1 == pins.size() ? "OUTPUT" : "INPUT",
                         0.07 + 0.2 * double(i % 2),
                         0.13 + 0.2 * double(i % 2));
    }
    lef += std::format(
        "  PIN VDD DIRECTION INOUT ; USE POWER ; PORT LAYER M1 ; RECT 0 0.97 {1} 1.03 ; END END VDD\n"
        "  PIN VSS DIRECTION INOUT ; USE GROUND ; PORT LAYER M1 ; RECT 0 -0.03 {1} 0.03 ; END END VSS\nEND {0}\n",
        cell,
        width);
  }
  std::ofstream(root + "/cells.lef") << lef << "END LIBRARY\n";
  var.dict["tmap"]         = "none";
  var.dict["library"]      = library;
  var.dict["lef"]          = root + "/cells.lef";
  var.dict["physical"]     = "domino_provisional";
  var.dict["clock_phases"] = "1";
  var.dict["domino_clock"] = "clk";
}

TEST_F(PassSynth, DominoProfileEmitsCellInstancesAndPublishesTheBundle) {
  hhds::GraphLibrary input;
  Eprp_var           var;
  var.graphs.push_back(design(input));
  var.dict["tmap"] = "none";
  var.dict["qor"]  = root + "/qor.json";
  run(var);
  ASSERT_FALSE(livehd::diag::sink().has_halting_errors());
  auto plain = read(root + "/qor.json.usyn.json");
  EXPECT_STREQ(plain["physical_profile"].GetString(), "none");
  EXPECT_FALSE(plain.HasMember("physical"));

  const auto                output = root + "/domino-net";
  Registry::Scoped_instance scope(output);
  domino_profile(var, root);
  var.dict["out"]       = output;
  var.dict["cache_dir"] = root + "/domino-cache";
  // The domino options are not logical identity: the second run is a logical
  // cache hit, and the physical choice is recomputed on it.
  for (const auto select : {"all", "area"}) {
    var.dict["domino_select"] = select;
    run(var);
    const auto& diagnostics = livehd::diag::sink().records();
    ASSERT_FALSE(livehd::diag::sink().has_halting_errors()) << (diagnostics.empty() ? "" : diagnostics.back().message);
    const auto report = read(root + "/qor.json.usyn.json");
    ASSERT_FALSE(report.HasParseError());
    EXPECT_STREQ(report["target"].GetString(), "cmos");
    EXPECT_STREQ(report["output"].GetString(), "logical-mixed");
    EXPECT_STREQ(report["physical_profile"].GetString(), "domino_provisional");
    EXPECT_EQ(report["cache"]["reused"].GetUint64(), std::string_view{select} == "all" ? 0U : 1U);
    const auto& physical = report["physical"];
    EXPECT_STREQ(physical["emission"].GetString(), "mixed");
    // The model bundle is published beside the report: every master in every
    // register variant (DFF is the only rung here).
    ASSERT_TRUE(physical["bundle"].IsObject());
    EXPECT_STREQ(physical["bundle"]["directory"].GetString(), "qor.json.domino");
    EXPECT_EQ(physical["bundle"]["macros"].GetUint64(), 1U);
    for (const auto file : {"domino_cells.lef", "domino_models.json", "domino_models.v", "row_access.json"}) {
      EXPECT_TRUE(fs::exists(fs::path(root) / "qor.json.domino" / file)) << file;
    }
    EXPECT_FALSE(fs::exists(root + "/qor.json.domino.tmp"));
    const auto sidecar = read(root + "/qor.json.domino/domino_models.json");
    ASSERT_FALSE(sidecar.HasParseError());
    EXPECT_TRUE(sidecar["masters"][0]["variants"][0]["access_check"]["ok"].GetBool());
    EXPECT_STREQ(sidecar["instances"][0]["state"].GetString(), "state");
    EXPECT_STREQ(physical["select"].GetString(), select);
    EXPECT_STREQ(physical["clock"].GetString(), "clk");
    EXPECT_STREQ(physical["reference"]["inverter"]["name"].GetString(), "INV");
    EXPECT_STREQ(physical["reference"]["inverter"]["source"].GetString(), "liberty-function");
    EXPECT_STREQ(physical["reference"]["registers"][0]["name"].GetString(), "DFF");
    EXPECT_TRUE(physical["reference"]["registers"][0]["hold_ps"].IsNull());
    EXPECT_EQ(physical["totals"]["states"].GetUint64(), 1U);
    EXPECT_EQ(physical["totals"]["eligible"].GetUint64(), 1U);
    EXPECT_EQ(physical["totals"]["converted"].GetUint64(), 1U);
    EXPECT_EQ(physical["totals"]["masters"].GetUint64(), 1U);
    const auto& endpoint = physical["regions"][0]["endpoints"][0];
    EXPECT_STREQ(endpoint["name"].GetString(), "state");
    EXPECT_STREQ(endpoint["decision"].GetString(), "domino");
    EXPECT_TRUE(std::string_view{endpoint["master"].GetString()}.starts_with("__lhd_domino_"));
    EXPECT_EQ(endpoint["n_pdn"].GetUint(), 2U);
    EXPECT_EQ(endpoint["rails"].Size(), 2U);
    // a & b: x = (2 + 4) / 2 inverters of 0.2 plus the 4.0 register, against
    // the register plus a NAND+INV for the exclusive AND.
    EXPECT_DOUBLE_EQ(endpoint["a_domino"].GetDouble(), 4.6);
    if (std::string_view{select} == "all") {
      EXPECT_STREQ(endpoint["reason"].GetString(), "domino-all");
      EXPECT_TRUE(endpoint["a_cmos"].IsNull());
    } else {
      EXPECT_STREQ(endpoint["reason"].GetString(), "domino-area");
      EXPECT_DOUBLE_EQ(endpoint["a_cmos"].GetDouble(), 5.0);
    }
    // The converted register is a body-less domino cell instance, not a Flop,
    // and both area bases are reported.
    EXPECT_DOUBLE_EQ(physical["totals"]["a_domino_estimate"].GetDouble(), 4.6);
    EXPECT_GT(physical["totals"]["a_footprint_um2"].GetDouble(), 4.6);
    EXPECT_EQ(endpoint["a_footprint_um2"].GetDouble(), physical["totals"]["a_footprint_um2"].GetDouble());
    const auto io = Registry::instance(output).find_io("top");
    ASSERT_TRUE(io && io->get_graph());
    uint32_t                      flops = 0;
    std::vector<hhds::Node_class> cells;
    for (auto node : io->get_graph()->body().nodes()) {
      flops += gu::type_op_of(node) == Ntype_op::Flop;
      if (gu::type_op_of(node) == Ntype_op::Sub) {
        cells.push_back(node);
      }
    }
    EXPECT_EQ(flops, 0U);
    ASSERT_EQ(cells.size(), 1U);
    EXPECT_EQ(gu::node_name_of(cells[0]), "state");
    const auto decl = cells[0].get_subnode_io();
    ASSERT_TRUE(decl);
    EXPECT_EQ(decl->get_name(), endpoint["master"].GetString());
    EXPECT_FALSE(decl->has_graph());
  }
  // A run without the profile drops the previous run's bundle.
  for (const auto key : {"physical", "domino_select", "domino_clock", "clock_phases", "lef"}) {
    var.dict.erase(key);
  }
  run(var);
  ASSERT_FALSE(livehd::diag::sink().has_halting_errors());
  EXPECT_FALSE(fs::exists(root + "/qor.json.domino"));
}

// leaf(clk, d, e) -> q holds one register, held <= d & e. top keeps its own
// register r <= a & b on clk and instantiates leaf twice, clocking one
// instance from an inverter of clk and the other from a clk/2 divider.
std::shared_ptr<hhds::Graph> derived_clock_children(hhds::GraphLibrary& library) {
  const auto bit = [](hhds::Node_class node) {
    auto pin = node.create_driver_pin(0);
    gu::set_ubits(pin, 1);
    return pin;
  };
  auto leaf_io = library.create_io("leaf");
  leaf_io->add_input("clk", 1);
  leaf_io->add_input("d", 2);
  leaf_io->add_input("e", 3);
  leaf_io->add_output("q", 4);
  for (auto name : {"clk", "d", "e", "q"}) {
    leaf_io->set_bits(name, 1);
    leaf_io->set_unsign(name, true);
  }
  auto leaf = leaf_io->create_graph();
  auto both = gu::create_typed_node(*leaf, Ntype_op::And);
  auto held = gu::create_typed_node(*leaf, Ntype_op::Flop);
  for (auto name : {"d", "e"}) {
    leaf->get_input_pin(name).connect_sink(gu::setup_sink_pid(both, 0));
  }
  bit(both).connect_sink(gu::setup_sink_by_name(held, "din"));
  leaf->get_input_pin("clk").connect_sink(gu::setup_sink_by_name(held, "clock_pin"));
  auto held_q = bit(held);
  gu::set_pin_name(held_q, "held");
  held_q.connect_sink(leaf->get_output_pin("q"));

  auto io = library.create_io("top");
  io->add_input("a", 1);
  io->add_input("b", 2);
  io->add_input("clk", 3);
  io->add_output("y", 4);
  io->add_output("y0", 5);
  io->add_output("y1", 6);
  for (auto name : {"a", "b", "clk", "y", "y0", "y1"}) {
    io->set_bits(name, 1);
    io->set_unsign(name, true);
  }
  auto graph = io->create_graph();
  auto conj  = gu::create_typed_node(*graph, Ntype_op::And);
  for (auto name : {"a", "b"}) {
    graph->get_input_pin(name).connect_sink(gu::setup_sink_pid(conj, 0));
  }
  auto r = gu::create_typed_node(*graph, Ntype_op::Flop);
  bit(conj).connect_sink(gu::setup_sink_by_name(r, "din"));
  graph->get_input_pin("clk").connect_sink(gu::setup_sink_by_name(r, "clock_pin"));
  auto r_q = bit(r);
  gu::set_pin_name(r_q, "r");
  r_q.connect_sink(graph->get_output_pin("y"));
  auto inverter = gu::create_typed_node(*graph, Ntype_op::Not);
  graph->get_input_pin("clk").connect_sink(gu::setup_sink_by_name(inverter, "a"));
  auto divider = gu::create_typed_node(*graph, Ntype_op::Flop);
  graph->get_input_pin("clk").connect_sink(gu::setup_sink_by_name(divider, "clock_pin"));
  auto div_q = bit(divider);
  gu::set_pin_name(div_q, "div");
  auto toggle = gu::create_typed_node(*graph, Ntype_op::Not);
  div_q.connect_sink(gu::setup_sink_by_name(toggle, "a"));
  bit(toggle).connect_sink(gu::setup_sink_by_name(divider, "din"));
  for (const auto& [clock, output] : {
           std::pair{bit(inverter), "y0"},
           std::pair{        div_q, "y1"}
  }) {
    auto sub = gu::create_typed_node(*graph, Ntype_op::Sub);
    sub.set_subnode(leaf_io);
    clock.connect_sink(sub.create_sink_pin(1));
    graph->get_input_pin("a").connect_sink(sub.create_sink_pin(2));
    graph->get_input_pin("b").connect_sink(sub.create_sink_pin(3));
    auto q = sub.create_driver_pin(4);
    gu::set_ubits(q, 1);
    q.connect_sink(graph->get_output_pin(output));
  }
  return graph;
}

TEST_F(PassSynth, DominoProfileRetainsRegistersClockedThroughADefinitionPort) {
  // leaf's port is named like the selected clock, but its instances are
  // clocked by an inverter and a divider: the shared definition is never
  // converted, while top's own register on the design clock is.
  hhds::GraphLibrary input;
  Eprp_var           var;
  var.graphs.push_back(derived_clock_children(input));
  var.dict["qor"] = root + "/qor.json";
  domino_profile(var, root);
  const auto                output = root + "/hier-net";
  Registry::Scoped_instance scope(output);
  var.dict["out"]           = output;
  var.dict["flatten"]       = "false";
  var.dict["domino_select"] = "all";
  run(var);
  const auto& diagnostics = livehd::diag::sink().records();
  ASSERT_FALSE(livehd::diag::sink().has_halting_errors()) << (diagnostics.empty() ? "" : diagnostics.back().message);
  const auto report = read(root + "/qor.json.usyn.json");
  ASSERT_FALSE(report.HasParseError());
  // state -> (definition region?, decision, reason); top's regions are named after it.
  std::map<std::string, std::tuple<bool, std::string, std::string>> decisions;
  for (const auto& region : report["physical"]["regions"].GetArray()) {
    for (const auto& endpoint : region["endpoints"].GetArray()) {
      decisions[endpoint["name"].GetString()] = {std::string_view{region["module"].GetString()} == "leaf",
                                                 endpoint["decision"].GetString(),
                                                 endpoint["reason"].GetString()};
    }
  }
  ASSERT_EQ(decisions.size(), 3U);
  EXPECT_EQ(decisions["held"], std::make_tuple(true, std::string("retained_cmos"), std::string("definition-clock")));
  EXPECT_EQ(decisions["r"], std::make_tuple(false, std::string("domino"), std::string("domino-all")));
  EXPECT_EQ(std::get<1>(decisions["div"]), "retained_cmos");
  const auto leaf = Registry::instance(output).find_io("leaf");
  ASSERT_TRUE(leaf && leaf->get_graph());
  uint32_t flops = 0, cells = 0;
  for (auto node : leaf->get_graph()->body().nodes()) {
    flops += gu::type_op_of(node) == Ntype_op::Flop;
    cells += gu::type_op_of(node) == Ntype_op::Sub;
  }
  EXPECT_EQ(flops, 1U);
  EXPECT_EQ(cells, 0U);
}

TEST_F(PassSynth, DominoProfileRequirementsAreExplicitUsageErrors) {
  hhds::GraphLibrary input;
  Eprp_var           var;
  var.graphs.push_back(design(input));
  var.dict["qor"] = root + "/qor.json";
  domino_profile(var, root);
  run(var);
  ASSERT_FALSE(livehd::diag::sink().has_halting_errors());
  const auto base = var.dict;
  // A refused run publishes nothing: the previous report and bundle stay.
  const auto report_text = [&] {
    std::ifstream file(root + "/qor.json.usyn.json");
    return std::string{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
  };
  const auto published = report_text();
  ASSERT_FALSE(published.empty());
  const auto stub = root + "/stub.lef";  // a readable LEF without the reference cells
  std::ofstream(stub) << "VERSION 5.8 ;\nEND LIBRARY\n";
  for (const auto& [key, value, code] : {
           std::tuple{    "clock_phases",                               "2",   "domino-profile"},
           std::tuple{           "stack",                               "5",   "domino-profile"},
           std::tuple{ "literal_extract",                               "1",   "domino-profile"},
           std::tuple{             "lef",                                "",   "domino-profile"},
           std::tuple{             "lef", "/nonexistent/domino_missing.lef",    "domino-bundle"},
           std::tuple{             "lef",                      stub.c_str(),    "domino-bundle"},
           std::tuple{    "domino_clock",                                "",   "domino-profile"},
           std::tuple{    "domino_clock",                           "clock",   "domino-profile"},
           std::tuple{         "library",                                "",   "domino-profile"},
           std::tuple{        "physical",                             "bad",  "invalid-options"},
           std::tuple{   "domino_select",                             "bad",  "invalid-options"},
           std::tuple{ "domino_identity",                           "maybe",  "invalid-options"},
           std::tuple{      "domino_aux",                              "65",  "invalid-options"},
           std::tuple{ "domino_variants",                               "0",  "invalid-options"},
           std::tuple{ "domino_variants",                               "4",  "invalid-options"},
           std::tuple{ "domino_inverter",                            "NOPE", "domino-reference"},
           std::tuple{ "domino_register",                             "INV", "domino-reference"},
           std::tuple{"domino_selection",                "the wrong option",   "domino-profile"}
  }) {
    var.dict = base;
    if (std::string_view{key} == "domino_selection") {
      var.dict["physical"]      = "none";  // a domino option without the profile
      var.dict["domino_select"] = "all";
    } else {
      var.dict[key] = value;
    }
    var.dict["out"] = root + "/invalid";
    run(var);
    EXPECT_TRUE(livehd::diag::sink().has_halting_errors()) << key;
    ASSERT_FALSE(livehd::diag::sink().records().empty()) << key;
    EXPECT_EQ(livehd::diag::sink().records().back().code, code) << key;
    EXPECT_FALSE(fs::exists(root + "/invalid")) << key;
    EXPECT_FALSE(fs::exists(root + "/qor.json.domino.tmp")) << key;
    EXPECT_FALSE(fs::exists(root + "/qor.json.provenance.tmp")) << key;
    EXPECT_TRUE(fs::exists(root + "/qor.json.domino/domino_models.json")) << key;
    EXPECT_EQ(report_text(), published) << key;
    livehd::diag::sink().clear();
  }
  // Without the profile, a domino option spelled at its listed default is
  // the implicit default, not an error.
  var.dict             = base;
  var.dict["physical"] = "none";
  for (const auto& [key, value] : {
           std::pair{  "domino_select",  "area"},
           std::pair{"domino_identity", "false"},
           std::pair{     "domino_aux",     "4"},
           std::pair{"domino_variants",     "3"}
  }) {
    var.dict[key] = value;
  }
  var.dict.erase("domino_clock");
  run(var);
  EXPECT_FALSE(livehd::diag::sink().has_halting_errors())
      << (livehd::diag::sink().records().empty() ? "" : livehd::diag::sink().records().back().message);
}

TEST_F(PassSynth, FailedRunKeepsPreviousReportProvenance) {
  hhds::GraphLibrary input;
  Eprp_var           var;
  var.graphs.push_back(design(input));
  var.dict["tmap"]               = "none";
  var.dict["qor"]                = root + "/qor.json";
  var.dict["invocation_context"] = R"({"inputs":[],"argv":["first-run"]})";
  run(var);
  ASSERT_FALSE(livehd::diag::sink().has_halting_errors());
  const auto manifest = [&] {
    std::ifstream file(root + "/qor.json.provenance/manifest.json");
    return std::string{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
  };
  const auto published = manifest();
  ASSERT_NE(published.find("first-run"), std::string::npos);
  var.dict["work"]               = "1";
  var.dict["invocation_context"] = R"({"inputs":[],"argv":["failed-run"]})";
  run(var);
  EXPECT_TRUE(livehd::diag::sink().has_halting_errors());
  EXPECT_TRUE(fs::exists(root + "/qor.json"));
  EXPECT_EQ(manifest(), published);
  EXPECT_FALSE(fs::exists(root + "/qor.json.provenance.tmp"));
}
}  // namespace

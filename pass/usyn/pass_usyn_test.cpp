// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "pass_usyn.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>

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
  var.dict["top"]                 = "top";
  var.dict["tmap"]                = "none";
  var.dict["out"]                 = output;
  var.dict["qor"]                 = root + "/qor.json";
  var.dict["cache_dir"]           = root + "/cache";
  size_t                instances = 0;
  std::string           first_artifact;
  std::vector<uint64_t> first_work;
  for (unsigned i = 0; i < 3; ++i) {
    run(var);
    ASSERT_FALSE(livehd::diag::sink().has_halting_errors());
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
      EXPECT_EQ(floor["work"].GetUint64(), region["work"]["total"].GetUint64() - region["work"]["admission"].GetUint64());
      EXPECT_GE(floor["floor"].GetUint64(), floor["work"].GetUint64());
      EXPECT_FALSE(floor["bound"].GetBool());
      EXPECT_EQ(floor["credits"].GetUint64(), 0U);
      EXPECT_GT(region["structural_work"].GetUint64(), region["work"]["admission"].GetUint64());
      EXPECT_EQ(region["identity_fallbacks"].GetUint64(), 0U);
      EXPECT_EQ(report["cache"]["replayed_structural_work"].GetUint64(), i == 0 ? 0U : region["structural_work"].GetUint64());
      EXPECT_EQ(report["cache"]["replayed_search_work"].GetUint64(), i == 0 ? 0U : floor["work"].GetUint64());
    }
    EXPECT_EQ(report["endpoint_search"]["pair_choices"].GetUint(), 4U);
    EXPECT_TRUE(report["regions"][0]["pairs"].HasMember("choice_combinations"));
    const auto&           charges = report["regions"][0]["work"];
    std::vector<uint64_t> stages;
    uint64_t              total = 0;
    for (auto key : {"admission", "selection", "pairs", "residual", "feedback", "cleanup"}) {
      stages.push_back(charges[key].GetUint64());
      total += stages.back();
    }
    EXPECT_EQ(charges["total"].GetUint64(), total);
    EXPECT_GT(total, 0U);
    if (i == 0) {
      first_work = stages;
    }
    EXPECT_EQ(stages, first_work);  // Warm evidence describes the original search.
    const auto& endpoints = report["regions"][0]["endpoints"];
    ASSERT_EQ(endpoints.Size(), 1U);
    EXPECT_STREQ(endpoints[0]["name"].GetString(), "state");
    EXPECT_TRUE(fs::exists(root + "/qor.json.provenance/manifest.json"));
    const auto& artifact = report["regions"][0]["artifact"];
    EXPECT_EQ(artifact["version"].GetUint(), 1U);
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
           std::pair{     "fast_accept", "sometimes"},
           std::pair{           "adder",       "bad"},
           std::pair{     "adder_block",        "-1"},
           std::pair{  "local_divisors",         "0"},
           std::pair{"local_candidates",      "4097"},
           std::pair{ "pair_candidates",      "4097"},
           std::pair{     "pair_inputs",        "17"},
           std::pair{    "pair_choices",         "9"},
           std::pair{       "pair_work",         "0"},
           std::pair{     "pair_trials",         "0"}
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

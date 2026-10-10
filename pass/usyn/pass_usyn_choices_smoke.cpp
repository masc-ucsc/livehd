// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>

#include "graph_library_singleton.hpp"
#include "gtest/gtest.h"
#include "node_util.hpp"
#include "pass_usyn.hpp"
#include "rapidjson/document.h"
#include "tmap.hpp"

namespace {
TEST(UsynMappedChoices, WinnerOwnsItsNativeArtifactsAndWarmTrialsRemainDeterministic) {
  namespace fs = std::filesystem;
  namespace gu = livehd::graph_util;
  using namespace livehd::synth;
  livehd::diag::sink().clear();
  livehd::diag::sink().set_human_stderr(true);
  livehd::diag::sink().set_jsonl_path("off");
  auto root = (fs::temp_directory_path() / "usyn-map-choices-XXXXXX").string();
  ASSERT_NE(mkdtemp(root.data()), nullptr);
  struct Cleanup {
    std::string path;
    ~Cleanup() {
      fs::remove_all(path);
      livehd::diag::sink().clear();
    }
  } cleanup{root};
  hhds::GraphLibrary library;
  auto               io = library.create_io("top");
  io->add_input("a", 1);
  io->add_input("b", 2);
  io->add_output("y", 3);
  for (auto name : {"a", "b", "y"}) {
    io->set_bits(name, 1);
    io->set_unsign(name, true);
  }
  auto top  = io->create_graph();
  auto gate = gu::create_typed_node(*top, Ntype_op::And);
  for (auto name : {"a", "b"}) {
    top->get_input_pin(name).connect_sink(gu::setup_sink_pid(gate, 0));
  }
  auto output = gate.create_driver_pin(0);
  gu::set_ubits(output, 1);
  output.connect_sink(top->get_output_pin("y"));
  unsigned calls         = 0;
  bool     refuse_second = false, tie_cost = false;
  ASSERT_TRUE(register_tmap_provider("abc", [&](const auto& source, const auto& options) {
    EXPECT_TRUE(!options.admission || options.admission("mock"));
    const bool second = calls++ % 2;
    if (second && refuse_second) {
      return Tmap_result{Tmap_status::refused, {}, "optional map refused"};
    }
    auto mapped = std::make_unique<Mapped_design>();
    EXPECT_TRUE(livehd::copy_with_callees(mapped->library, *source->get_io()->get_library(), source->get_name()));
    mapped->top = mapped->library.find_io(source->get_name())->get_graph();
    Region_qor row;
    row.module = std::string(source->get_name());
    row.gates  = second && !tie_cost ? 3 : 7;
    row.area   = row.gates - (second && tie_cost ? 1e-8 : 0);
    mapped->regions.push_back(std::move(row));
    return Tmap_result{Tmap_status::mapped, std::move(mapped), {}};
  }));
  Eprp_var var;
  var.graphs.push_back(top);
  var.dict["tmap_trials"] = "2";
  // The provider is mocked, but tuning still fingerprints its library input.
  std::ofstream(root + "/mock.lib") << "mock provider library\n";
  var.dict["library"]   = root + "/mock.lib";
  var.dict["qor"]       = root + "/qor.json";
  var.dict["cache_dir"] = root + "/cache";
  for (unsigned run = 0; run < 4; ++run) {
    refuse_second               = run == 2;
    tie_cost                    = run == 3;
    const bool alternative_wins = !refuse_second && !tie_cost;
    var.set_stage_labels(var.dict);
    Pass_usyn::work(var);
    ASSERT_FALSE(livehd::diag::sink().has_halting_errors());
    std::ifstream       file(root + "/qor.json.usyn.json");
    std::string         text{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    rapidjson::Document report;
    report.Parse(text.c_str());
    ASSERT_FALSE(report.HasParseError());
    const auto& trials = report["mapping_trials"];
    ASSERT_EQ(trials.Size(), 2U);
    EXPECT_EQ(trials[0]["physical_logic_gates"].GetUint64(), 7U);
    EXPECT_EQ(trials[0]["work_limit"].GetUint64(), trials[1]["work_limit"].GetUint64());
    EXPECT_EQ(trials[1]["selected"].GetBool(), alternative_wins);
    EXPECT_EQ(trials[0]["selected"].GetBool(), !alternative_wins);
    EXPECT_EQ(report["native_optimization"]["p1"].GetBool(), alternative_wins);
    EXPECT_EQ(report["native_optimization"]["multi_rep"].GetBool(), alternative_wins);
    if (!refuse_second) {
      EXPECT_EQ(trials[1]["physical_logic_gates"].GetUint64(), tie_cost ? 7U : 3U);
    } else {
      EXPECT_STREQ(trials[1]["reason"].GetString(), "optional map refused");
    }
    EXPECT_EQ(top->get_output_pin("y").get_driver_pin(), output);
  }
  EXPECT_EQ(calls, 8U);
}
}  // namespace

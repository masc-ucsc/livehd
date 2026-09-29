// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "pass_usyn.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>

#include "graph_library_singleton.hpp"
#include "gtest/gtest.h"
#include "node_util.hpp"

namespace {
namespace fs   = std::filesystem;
namespace gu   = livehd::graph_util;
using Registry = livehd::Hhds_graph_library;

class PassSynth : public ::testing::Test {
protected:
  std::string root;
  void        SetUp() override {
    root = (fs::temp_directory_path() / "livehd-usyn-lifetime-XXXXXX").string();
    ASSERT_NE(mkdtemp(root.data()), nullptr);
  }
  void TearDown() override { fs::remove_all(root); }
};

std::shared_ptr<hhds::Graph> design(hhds::GraphLibrary& library) {
  auto io = library.create_io("top");
  io->add_input("a", 1);
  io->add_input("b", 2);
  io->add_output("y", 3);
  for (const auto* name : {"a", "b", "y"}) {
    io->set_bits(name, 1);
    io->set_unsign(name, true);
  }
  auto graph = io->create_graph();
  auto node  = gu::create_typed_node(*graph, Ntype_op::And);
  for (const auto* name : {"a", "b"}) {
    auto pin = graph->get_input_pin(name);
    gu::set_ubits(pin, 1);
    pin.connect_sink(gu::setup_sink_pid(node, 0));
  }
  auto pin = node.create_driver_pin(0);
  gu::set_ubits(pin, 1);
  pin.connect_sink(graph->get_output_pin("y"));
  return graph;
}

std::string read(const std::string& path) {
  std::ifstream input(path);
  return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

TEST_F(PassSynth, RepeatedRunsMapUnateRegionsAndReuseTheCache) {
  hhds::GraphLibrary        input;
  const auto                source = design(input);
  const auto                output = root + "/published";
  const auto                report = root + "/qor.json";
  Registry::Scoped_instance published_scope(output);
  size_t                    count = 0;  // after the first run: the output plus pass.abc's cache libraries
  Eprp_var                  var;
  var.graphs.push_back(source);
  var.dict["library"]   = "inou/prp/tests/abc/test.lib";
  var.dict["top"]       = "top";
  var.dict["out"]       = output;
  var.dict["qor"]       = report;
  var.dict["cache_dir"] = root + "/cache";

  const auto run = [&] {
    var.set_stage_labels(var.dict);
    Pass_usyn::work(var);
  };

  // Cold then warm in the SAME process: every run publishes a mapped design and
  // repeated runs register no additional library.
  for (unsigned i = 0; i < 3; ++i) {
    run();
    if (i == 0) {
      count = Registry::registered_instances();
    }
    EXPECT_EQ(Registry::registered_instances(), count);
    auto io = Registry::instance(output).find_io("top");
    ASSERT_TRUE(io);
    ASSERT_TRUE(io->get_graph());
    const auto text = read(report + ".usyn.json");
    EXPECT_NE(text.find("\"kind\":\"usyn\""), std::string::npos) << text;
    if (i == 0) {
      EXPECT_NE(text.find("\"status\":\"abc_tmap\""), std::string::npos) << text;
      EXPECT_NE(text.find("\"reused\":0"), std::string::npos) << text;
    } else {
      EXPECT_EQ(text.find("\"reused\":0"), std::string::npos) << text;  // warm: regions come from the cache
    }
    EXPECT_TRUE(fs::exists(report + ".provenance"));
  }
  // Without a cache the region is searched again, with identical decisions.
  var.dict.erase("cache_dir");
  run();
  EXPECT_NE(read(report + ".usyn.json").find("\"reused\":0"), std::string::npos);
  EXPECT_LE(Registry::registered_instances(), count);
}
}  // namespace

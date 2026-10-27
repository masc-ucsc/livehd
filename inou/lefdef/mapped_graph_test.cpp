// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "mapped_graph.hpp"

#include <cstdlib>
#include <filesystem>
#include <set>
#include <stdexcept>

#include "attr_carry.hpp"
#include "attrs.hpp"
#include "gtest/gtest.h"
#include "node_util.hpp"

namespace livehd::physical {
TEST(MappedGraph, ImportPreservesSignalConnectivityAndUnusedCells) {
  Technology tech;
  tech.macros["INV"].pins["A"].direction = "INPUT";
  tech.macros["INV"].pins["Y"].direction = "OUTPUT";
  Floorplan fp;
  fp.design = "exact";
  fp.components.push_back({"inv", "INV", {}});
  fp.components.push_back({"unused", "INV", {}});
  Terminal input, output;
  input.name       = "a";
  input.direction  = "INPUT";
  output.name      = "y";
  output.direction = "OUTPUT";
  fp.terminals     = {input, output};
  fp.nets          = {
      { "in", {{"PIN", "a"}, {"inv", "A"}}},
      {"out", {{"inv", "Y"}, {"PIN", "y"}}}
  };
  hhds::GraphLibrary    library;
  const auto            graph = mapped_graph(library, fp, tech);
  std::set<std::string> cells;
  for (auto node : graph->body().nodes()) {
    if (node.get_debug_nid() != hhds::Graph::INPUT_NODE && node.get_debug_nid() != hhds::Graph::OUTPUT_NODE) {
      cells.emplace(node.get_name());
    }
  }
  EXPECT_EQ(cells, (std::set<std::string>{"inv", "unused"}));
  const auto drivers = graph->get_output_pin("y").get_driver_pins();
  ASSERT_EQ(drivers.size(), 1);
  EXPECT_EQ(drivers.front().get_master_node().get_name(), "inv");
  const auto source = drivers.front().get_master_node().get_sink_pin("A").get_driver_pins();
  ASSERT_EQ(source.size(), 1);
  EXPECT_EQ(source.front(), graph->get_input_pin("a"));
  const auto copied = library.create_io("copy")->create_graph();
  for (const auto node : graph->body().nodes()) {
    const auto open = node.attr(livehd::attrs::physical_open_inputs);
    if (node.get_name() == "unused") {
      ASSERT_TRUE(open.has());
      EXPECT_EQ(open.get(), (std::vector<std::string>{"A"}));
      // The annotation must follow physical graph reconstruction, too.
      auto clone = livehd::graph_util::create_typed_node(*copied, Ntype_op::Sub);
      clone.set_subnode(node.get_subnode_io());
      livehd::graph_util::carry_node_attrs(node, clone);
      EXPECT_EQ(clone.attr(livehd::attrs::physical_open_inputs).get(), open.get());
    } else {
      EXPECT_FALSE(open.has());
    }
  }
  const auto saved = std::filesystem::path(std::getenv("TEST_TMPDIR")) / "open_inputs";
  library.save(saved.string());
  hhds::GraphLibrary restored;
  restored.load(saved.string());
  bool found = false;
  for (const auto node : restored.find_io("exact")->get_graph()->body().nodes()) {
    if (node.get_name() == "unused") {
      ASSERT_TRUE(node.attr(livehd::attrs::physical_open_inputs).has());
      EXPECT_EQ(node.attr(livehd::attrs::physical_open_inputs).get(), (std::vector<std::string>{"A"}));
      found = true;
    }
  }
  EXPECT_TRUE(found);
  std::filesystem::remove_all(saved);
}
TEST(MappedGraph, UndrivenSignalNetIsRejectedInsteadOfDropped) {
  Floorplan fp;
  fp.design = "undriven";
  Terminal output;
  output.name      = "y";
  output.direction = "OUTPUT";
  fp.terminals     = {output};
  fp.nets          = {
      {"missing_driver", {{"PIN", "y"}}}
  };
  hhds::GraphLibrary library;
  EXPECT_THROW(mapped_graph(library, fp, {}), std::runtime_error);
}
TEST(MappedGraph, MultiplePhysicalDriversAreRejected) {
  Floorplan fp;
  fp.design = "multiple";
  for (const auto* name : {"a", "b"}) {
    Terminal input;
    input.name      = name;
    input.direction = "INPUT";
    fp.terminals.push_back(input);
  }
  fp.nets = {
      {"shorted_inputs", {{"PIN", "a"}, {"PIN", "b"}}}
  };
  hhds::GraphLibrary library;
  EXPECT_THROW(mapped_graph(library, fp, {}), std::runtime_error);
}
}  // namespace livehd::physical

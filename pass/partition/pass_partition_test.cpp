// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "pass_partition.hpp"

#include <cstdlib>
#include <filesystem>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include "flatten.hpp"
#include "graph_library_singleton.hpp"
#include "gtest/gtest.h"
#include "node_util.hpp"
#include "pass_color.hpp"

TEST(PartitionAdmission, RefusalStopsConstructionAndFreshRetryPreservesConnectivity) {
  namespace gu = livehd::graph_util;
  hhds::GraphLibrary input;
  auto               io = input.create_io("top");
  io->add_input("a", 1);
  io->add_output("y", 2);
  io->set_bits("a", 1);
  io->set_bits("y", 1);
  auto graph = io->create_graph();
  auto first = gu::create_typed_node(*graph, Ntype_op::Not, 1);
  auto last  = gu::create_typed_node(*graph, Ntype_op::Not, 1);
  gu::set_color(first, 1);
  gu::set_color(last, 2);
  graph->get_input_pin("a").connect_sink(first.create_sink_pin(0));
  first.create_driver_pin(0).connect_sink(last.create_sink_pin(0));
  last.create_driver_pin(0).connect_sink(graph->get_output_pin("y"));
  const auto source_gids = input.all_gids();
  using namespace livehd::partition;
  for (const auto* stop : {"begin",
                           "resolve-step",
                           "collect-step",
                           "name_ports-step",
                           "forward-signature-step",
                           "forward-signatures-done",
                           "producer-signature-step",
                           "producer-signatures-done",
                           "declare_region_io-step",
                           "emit_region_body-step",
                           "commit",
                           "committed",
                           "build_top-step",
                           "complete"}) {
    SCOPED_TRACE(stop);
    hhds::GraphLibrary output;
    bool               refused   = false;
    const Admission    admission = [&](std::string_view stage, uint64_t) {
      EXPECT_FALSE(refused);  // a single false must stop, even if a later call would allow work
      if (stage == stop) {
        refused = true;
        return false;
      }
      return true;
    };
    EXPECT_FALSE(Pass_partition::
                     build_decomposition({graph}, &output, "top", false, {}, Flatten_mode::off, false, {}, 1, {}, {}, admission));
    EXPECT_TRUE(refused);
    EXPECT_EQ(input.all_gids(), source_gids);
    EXPECT_EQ(graph->get_output_pin("y").get_driver_pin(), last.get_driver_pin(0));
    EXPECT_EQ(last.get_sink_pin(0).get_driver_pin(), first.get_driver_pin(0));
  }
  hhds::GraphLibrary retry;
  ASSERT_TRUE(Pass_partition::build_decomposition({graph}, &retry, "top", false));
  auto result = retry.find_io("top")->get_graph();
  ASSERT_TRUE(result);
  EXPECT_FALSE(result->get_output_pin("y").get_driver_pin().is_invalid());
  unsigned nots = 0;
  for (auto gid : retry.all_gids()) {
    auto body = retry.find_io(gid)->get_graph();
    for (auto node : body->body().nodes()) {
      if (gu::type_op_of(node) == Ntype_op::Not) {
        ++nots;
        EXPECT_FALSE(node.get_sink_pin(0).get_driver_pin().is_invalid());
      }
    }
  }
  EXPECT_EQ(nots, 2U);
  hhds::GraphLibrary exceptional;
  EXPECT_THROW(
      Pass_partition::build_decomposition({graph},
                                          &exceptional,
                                          "top",
                                          false,
                                          {},
                                          Flatten_mode::off,
                                          false,
                                          {},
                                          1,
                                          {},
                                          {},
                                          [](std::string_view, uint64_t) -> bool { throw std::runtime_error("caller failure"); }),
      std::runtime_error);
}

TEST(PartitionAdmission, BuilderRefusalStopsBeforeNextRegionForSingleAndBatchHooks) {
  namespace gu = livehd::graph_util;
  using namespace livehd::partition;
  for (bool single_region : {false, true}) {
    for (bool batch : {false, true}) {
      hhds::GraphLibrary input, output;
      auto               io = input.create_io("top");
      io->add_input("a", 1);
      io->add_output("y", 2);
      auto graph  = io->create_graph();
      auto driver = graph->get_input_pin("a");
      for (int i = 1; i <= 3; ++i) {
        auto node = gu::create_typed_node(*graph, Ntype_op::Not, 1);
        gu::set_color(node, single_region ? 1 : i);
        driver.connect_sink(node.create_sink_pin(0));
        driver = node.create_driver_pin(0);
      }
      driver.connect_sink(graph->get_output_pin("y"));
      unsigned                 built      = 0;
      bool                     refused    = false;
      const Body_builder       hook       = [&](const Region_body&) { ++built; };
      const Body_batch_builder batch_hook = [&](std::span<const Region_body> regions) { built += regions.size(); };
      const Admission          admission  = [&](std::string_view stage, uint64_t work) {
        EXPECT_FALSE(refused);
        if (built) {
          EXPECT_EQ(stage, "built");
          EXPECT_EQ(work, 0U);
          refused = true;
          return false;
        }
        return true;
      };
      EXPECT_FALSE(Pass_partition::build_decomposition({graph},
                                                       &output,
                                                       "top",
                                                       false,
                                                       batch ? Body_builder{} : hook,
                                                       Flatten_mode::off,
                                                       true,
                                                       batch ? batch_hook : Body_batch_builder{},
                                                       1,
                                                       {},
                                                       {},
                                                       admission));
      EXPECT_TRUE(refused);
      EXPECT_EQ(built, 1U);
      EXPECT_EQ(graph->get_output_pin("y").get_driver_pin(), driver);
    }
  }
}

TEST(PartitionReuse, SingleVirtualFlatColorRetainsPreBodyButExplicitFlatDoesNot) {
  namespace gu = livehd::graph_util;
  for (bool virtual_flat : {false, true}) {
    hhds::GraphLibrary input, output;
    auto               io = input.create_io("top");
    io->add_input("a", 1);
    io->add_output("y", 2);
    for (const auto* name : {"a", "y"}) {
      io->set_bits(name, 1);
      io->set_unsign(name, true);
    }
    auto graph = io->create_graph();
    auto node  = gu::create_typed_node(*graph, Ntype_op::Not, 1);
    gu::set_color(node, 1);
    auto pin = node.create_driver_pin(0);
    gu::set_ubits(pin, 1);
    gu::set_ubits(graph->get_input_pin("a"), 1);
    graph->get_input_pin("a").connect_sink(node.create_sink_pin(0));
    pin.connect_sink(graph->get_output_pin("y"));
    graph->get_input_node()
        .attr(livehd::attrs::coloring_info)
        .set(virtual_flat ? R"({"algorithm":"synth","hier_flat":true})" : R"({"algorithm":"flat"})");
    unsigned calls = 0;
    ASSERT_TRUE(Pass_partition::build_decomposition(
        {graph},
        &output,
        "top",
        false,
        [&](const livehd::partition::Region_body& region) {
          ++calls;
          EXPECT_EQ(region.module_name, "top");
          EXPECT_EQ(region.pre_body != nullptr, virtual_flat);
          EXPECT_EQ(region.pre_lib != nullptr, virtual_flat);
          if (region.pre_body) {
            EXPECT_FALSE(region.pre_body->get_input_pin("a").is_invalid());
            EXPECT_FALSE(region.pre_body->get_output_pin("y").is_invalid());
            size_t nots = 0;
            for (auto candidate : region.pre_body->body().nodes()) {
              nots += gu::type_op_of(candidate) == Ntype_op::Not;
            }
            EXPECT_EQ(nots, 1);
          }
        },
        livehd::partition::Flatten_mode::automatic,
        true));
    EXPECT_EQ(calls, 1);
  }
}

// A depth cutoff must not turn shared fan-in into an exponential tree walk.
// Two allocations of the same anonymous graph must also name boundaries alike.
TEST(PartitionNames, DeepReconvergentProducerCones) {
  namespace gu = livehd::graph_util;
  std::vector<std::vector<std::string>> runs;
  for (int run = 0; run < 2; ++run) {
    auto& lib = livehd::Hhds_graph_library::instance("partition_deep_src_" + std::to_string(run));
    auto  io  = lib.create_io("deep");
    io->add_input("a", 1);
    io->set_bits("a", 4);
    io->add_input("b", 2);
    io->set_bits("b", 4);
    io->add_output("y", 3);
    io->set_bits("y", 4);
    auto g = io->create_graph();
    // Perturb node IDs without changing the live graph.
    if (run != 0) {
      for (int i = 0; i < 7; ++i) {
        auto unused = gu::create_typed_node(*g, Ntype_op::Not, 4);
        unused.del_node();
      }
    }
    auto prev  = g->get_input_pin("a");
    auto older = g->get_input_pin("b");
    for (int i = 0; i < 650; ++i) {
      auto node = gu::create_typed_node(*g, Ntype_op::Xor, 4);
      gu::set_color(node, i / 20 + 1);
      auto result = node.create_driver_pin(0);
      gu::set_ubits(result, 4);
      // Xor is a SINGLE bank, so its two operands take consecutive pids. Piling
      // both onto pid 0 left a two-driver sink that any singular get_driver_pin()
      // reader silently halves.
      node.create_sink_pin(0).connect_driver(prev);
      node.create_sink_pin(1).connect_driver(older);
      older = prev;
      prev  = result;
    }
    prev.connect_sink(g->get_output_pin("y"));
    auto&                    out = livehd::Hhds_graph_library::instance("partition_deep_dst_" + std::to_string(run));
    std::vector<std::string> names;
    uint64_t                           signature_work = 0;
    const livehd::partition::Admission admission      = [&](std::string_view stage, uint64_t work) {
      if (stage == "forward-signature-step" || stage == "producer-signature-step") {
        signature_work += work;
      }
      return true;
    };
    ASSERT_TRUE(Pass_partition::build_decomposition(
        {g},
        &out,
        "deep",
        false,
        [&](const livehd::partition::Region_body& body) {
          for (const auto& port : body.inputs) {
            names.push_back(port.name);
          }
          for (const auto& port : body.outputs) {
            names.push_back(port.name);
          }
        },
        livehd::partition::Flatten_mode::off,
        false,
        {},
        64,
        {},
        {},
        run ? admission : livehd::partition::Admission{}));
    if (run) {
      EXPECT_GT(signature_work, 650U);
    }
    EXPECT_GT(names.size(), 60U);
    runs.push_back(std::move(names));
  }
  EXPECT_EQ(runs[0], runs[1]);
}

// Nested repeated definitions must retain the flat algorithm's four distinct
// regions, including through save/load and a subsequent recoloring/clear.
TEST(PartitionColors, SharedNestedOccurrencesSurvivePersistenceAndRecoloring) {
  namespace gu = livehd::graph_util;
  hhds::GraphLibrary library;
  auto               make_io = [&](const char* name, const std::vector<std::string>& outputs) {
    auto io = library.create_io(name);
    io->add_input("a", 1);
    io->add_input("b", 2);
    unsigned pid = 3;
    for (const auto& output : outputs) {
      io->add_output(output, pid++);
      io->set_bits(output, 1);
      io->set_unsign(output, true);
    }
    for (const auto* input : {"a", "b"}) {
      io->set_bits(input, 1);
      io->set_unsign(input, true);
    }
    return io;
  };
  auto leaf_io = make_io("leaf", {"y"});
  auto leaf    = leaf_io->create_graph();
  auto gate    = gu::create_typed_node(*leaf, Ntype_op::And, 1);
  gate.attr(hhds::attrs::name).set("gate");
  leaf->get_input_pin("a").connect_sink(gate.create_sink_pin(0));
  leaf->get_input_pin("b").connect_sink(gate.create_sink_pin(1));
  gu::set_ubits(gate.create_driver_pin(0), 1);
  gate.create_driver_pin(0).connect_sink(leaf->get_output_pin("y"));
  auto middle_io   = make_io("middle", {"y", "z"});
  auto middle      = middle_io->create_graph();
  auto instantiate = [&](hhds::Graph& parent, const std::shared_ptr<hhds::GraphIO>& child, const char* name) {
    auto node = gu::create_typed_node(parent, Ntype_op::Sub);
    node.set_subnode(child);
    node.attr(hhds::attrs::name).set(name);
    parent.get_input_pin("a").connect_sink(node.create_sink_pin(1));
    parent.get_input_pin("b").connect_sink(node.create_sink_pin(2));
    return node;
  };
  instantiate(*middle, leaf_io, "left").create_driver_pin(3).connect_sink(middle->get_output_pin("y"));
  instantiate(*middle, leaf_io, "right").create_driver_pin(3).connect_sink(middle->get_output_pin("z"));
  auto top_io = make_io("top", {"w", "x", "y", "z"});
  auto top    = top_io->create_graph();
  auto left   = instantiate(*top, middle_io, "left");
  auto right  = instantiate(*top, middle_io, "right");
  left.create_driver_pin(3).connect_sink(top->get_output_pin("w"));
  left.create_driver_pin(4).connect_sink(top->get_output_pin("x"));
  right.create_driver_pin(3).connect_sink(top->get_output_pin("y"));
  right.create_driver_pin(4).connect_sink(top->get_output_pin("z"));
  if (!Pass::eprp.get_method("pass.color")) {
    Pass_color::setup();
  }
  Eprp_var var;
  var.add(top);
  var.add(middle);
  var.add(leaf);
  auto recolor = [&] {
    Pass::eprp.run_method_now("pass.color",
                              var,
                              {
                                  {     "alg", "synth"},
                                  {     "top",   "top"},
                                  {    "hier",  "true"},
                                  {"max_gate",     "1"},
                                  {  "min_ge",     "0"}
    });
  };
  auto flattened_colors = [&](hhds::GraphLibrary& lib, hhds::Graph* root) {
    std::map<std::string, int32_t> colors;
    auto                           flat = livehd::partition::flatten_hierarchy(root, &lib, "flat_check");
    EXPECT_NE(flat, nullptr);
    if (flat) {
      for (auto node : flat->body().nodes()) {
        if (gu::type_op_of(node) == Ntype_op::And) {
          colors.emplace(gu::node_name_of(node), gu::node_color_of(node));
        }
      }
      lib.delete_graph(flat);
      lib.delete_graphio("flat_check");
    }
    return colors;
  };
  recolor();
  const auto expected = flattened_colors(library, top.get());
  ASSERT_EQ(expected.size(), 4);
  std::set<int32_t> distinct;
  for (const auto& [name, color] : expected) {
    EXPECT_NE(color, 0) << name;
    distinct.insert(color);
  }
  EXPECT_EQ(distinct.size(), 1) << "a shared implementation is colored once";
  size_t occurrences = 0;
  for (auto node : top->occurrences().nodes()) {
    if (gu::type_op_of(node.base_node()) == Ntype_op::And) {
      ++occurrences;
      EXPECT_FALSE(gu::has_hier_color(node));
      EXPECT_TRUE(distinct.contains(gu::node_color_of(node)));
    }
  }
  EXPECT_EQ(occurrences, 4);
  EXPECT_TRUE(distinct.contains(gu::node_color_of(gate))) << "the definition owns the shared implementation color";
  // A preserved boundary has the Sub's call path, not just its parent's path.
  livehd::partition::Flat_origin_map opaque_origins;
  auto                               opaque
      = livehd::partition::flatten_hierarchy(top.get(), &library, "opaque_check", &opaque_origins, false, {leaf->get_gid()});
  ASSERT_NE(opaque, nullptr);
  int32_t opaque_color = 100;
  for (const auto& [node, origin] : opaque_origins) {
    if (gu::type_op_of(node) == Ntype_op::Sub) {
      origin.set_color(++opaque_color);
    }
  }
  EXPECT_EQ(opaque_color, 104);
  for (auto node : top->occurrences().nodes()) {
    if (gu::type_op_of(node.base_node()) == Ntype_op::Sub && node.get_subnode_gid() == leaf->get_gid()) {
      EXPECT_GT(gu::node_color_of(node), 100);
    }
  }
  library.delete_graph(opaque);
  library.delete_graphio("opaque_check");
  // The next virtual-flat coloring removes overrides on now-dissolved Subs.
  recolor();
  for (auto node : top->occurrences().nodes()) {
    if (gu::type_op_of(node.base_node()) == Ntype_op::Sub) {
      EXPECT_FALSE(gu::has_hier_color(node));
    }
  }
  const char* tmp  = std::getenv("TEST_TMPDIR");
  auto        path = std::filesystem::path(tmp ? tmp : "/tmp") / "partition_occurrence_colors";
  library.save(path.string());
  hhds::GraphLibrary loaded;
  loaded.load(path.string());
  auto loaded_top = loaded.find_io("top")->get_graph();
  EXPECT_EQ(flattened_colors(loaded, loaded_top.get()), expected);
  // Recoloring must replace, rather than accumulate, occurrence overrides.
  recolor();
  EXPECT_EQ(flattened_colors(library, top.get()), expected);
  Pass::eprp.run_method_now("pass.color",
                            var,
                            {
                                {"alg", "clear"}
  });
  for (auto node : top->occurrences().nodes()) {
    EXPECT_FALSE(gu::has_hier_color(node));
    EXPECT_EQ(gu::node_color_of(node), 0);
  }
  for (const auto& [name, color] : flattened_colors(library, top.get())) {
    EXPECT_EQ(color, 0) << name;
  }
  std::filesystem::remove_all(path);
}


TEST(PartitionWiringContext, CopiesConstantSliceWithoutMergingOwnersAndIncludesItInPreBody) {
  namespace gu = livehd::graph_util;
  using namespace livehd::partition;
  for (const bool enabled : {false, true}) {
    hhds::GraphLibrary input, output;
    auto io = input.create_io("context");
    io->add_input("a", 1);
    io->add_output("y", 2);
    io->set_bits("a", 4);
    io->set_bits("y", 8);
    auto graph = io->create_graph();
    gu::set_ubits(graph->get_input_pin("a"), 4);
    const auto slice = gu::create_get_mask(*graph, graph->get_input_pin("a"), 0, 8);
    gu::set_bits(slice.create_driver_pin(0), 8);
    const auto consumer = gu::create_typed_node(*graph, Ntype_op::Not, 8);
    slice.create_driver_pin(0).connect_sink(consumer.create_sink_pin(0));
    consumer.create_driver_pin(0).connect_sink(graph->get_output_pin("y"));
    gu::set_color(slice, 1);
    gu::set_color(consumer, 2);
    graph->get_input_node().attr(livehd::attrs::coloring_info).set(
        enabled ? "{\"boundary_wiring\":true}" : "{\"boundary_wiring\":false}");
    unsigned visited = 0;
    const Body_builder hook = [&](const Region_body& rb) {
      ++visited;
      ASSERT_NE(rb.pre_body, nullptr);
      if (rb.color != 2) {
        return;
      }
      EXPECT_EQ(rb.nodes.size(), enabled ? 2U : 1U);
      ASSERT_EQ(rb.inputs.size(), 1U);
      EXPECT_EQ(rb.inputs.front().src_driver, enabled ? graph->get_input_pin("a") : slice.create_driver_pin(0));
      EXPECT_EQ(rb.inputs.front().bits, enabled ? 4 : 8);
      unsigned copies = 0;
      for (const auto node : rb.pre_body->body().nodes()) {
        if (gu::type_op_of(node) == Ntype_op::Get_mask) {
          ++copies;
          EXPECT_TRUE(gu::bit_range(node).has_value());
        }
      }
      EXPECT_EQ(copies, enabled ? 1U : 0U);
    };
    ASSERT_TRUE(Pass_partition::build_decomposition({graph}, &output, "context", false,
                                                    hook, Flatten_mode::off, true));
    EXPECT_EQ(visited, 2U);
    EXPECT_EQ(gu::node_color_of(slice), 1);
    EXPECT_EQ(gu::node_color_of(consumer), 2);
    EXPECT_EQ(consumer.get_sink_pin(0).get_driver_pin(), slice.create_driver_pin(0));
  }
}

TEST(PartitionWiringContext, OrdinaryNarrowLogicSliceRemainsTheExportedInterface) {
  namespace gu = livehd::graph_util;
  using namespace livehd::partition;
  hhds::GraphLibrary input, output;
  auto io = input.create_io("narrow_context");
  io->add_input("a", 1);
  io->add_output("y", 2);
  io->set_bits("a", 8);
  io->set_bits("y", 4);
  auto graph = io->create_graph();
  gu::set_ubits(graph->get_input_pin("a"), 8);
  const auto logic = gu::create_typed_node(*graph, Ntype_op::Not, 8);
  graph->get_input_pin("a").connect_sink(logic.create_sink_pin(0));
  const auto slice = gu::create_get_mask(*graph, logic.create_driver_pin(0), 0, 4);
  gu::set_bits(slice.create_driver_pin(0), 4);
  const auto consumer = gu::create_typed_node(*graph, Ntype_op::Not, 4);
  slice.create_driver_pin(0).connect_sink(consumer.create_sink_pin(0));
  consumer.create_driver_pin(0).connect_sink(graph->get_output_pin("y"));
  gu::set_color(logic, 1);
  gu::set_color(slice, 1);
  gu::set_color(consumer, 2);
  graph->get_input_node().attr(livehd::attrs::coloring_info).set("{\"boundary_wiring\":true}");
  unsigned visited = 0;
  const Body_builder hook = [&](const Region_body& rb) {
    ++visited;
    if (rb.color == 2) {
      ASSERT_EQ(rb.nodes.size(), 1U);
      ASSERT_EQ(rb.inputs.size(), 1U);
      EXPECT_EQ(rb.inputs.front().src_driver, slice.create_driver_pin(0));
      EXPECT_EQ(rb.inputs.front().bits, 4);
    }
  };
  ASSERT_TRUE(Pass_partition::build_decomposition({graph}, &output, "narrow_context", false,
                                                  hook, Flatten_mode::off, true));
  EXPECT_EQ(visited, 2U);
}

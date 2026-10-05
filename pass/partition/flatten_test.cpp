// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "flatten.hpp"

#include <stdexcept>
#include <vector>

#include "gtest/gtest.h"
#include "node_util.hpp"

namespace livehd::partition {
namespace {
namespace gu = graph_util;

class FlattenAdmission : public ::testing::Test {
protected:
  hhds::GraphLibrary             source;
  std::shared_ptr<hhds::Graph>   top, child;
  std::shared_ptr<hhds::GraphIO> opaque;
  hhds::Node_class               invert;

  static std::shared_ptr<hhds::Graph> make_graph(hhds::GraphLibrary& lib, const std::string& name) {
    auto io = lib.create_io(name);
    io->add_input("a", 1);
    io->add_output("y", 2);
    for (auto port : {"a", "y"}) {
      io->set_bits(port, 1);
      io->set_unsign(port, true);
    }
    auto graph = io->create_graph();
    gu::set_ubits(graph->get_input_pin("a"), 1);
    return graph;
  }
  static hhds::Pin_class instance(hhds::Graph& parent, const std::shared_ptr<hhds::GraphIO>& io) {
    auto node = gu::create_typed_node(parent, Ntype_op::Sub);
    node.set_subnode(io);
    parent.get_input_pin("a").connect_sink(node.create_sink_pin(1));
    auto result = node.create_driver_pin(2);
    gu::set_ubits(result, 1);
    return result;
  }
  void SetUp() override {
    child  = make_graph(source, "child");
    invert = gu::create_typed_node(*child, Ntype_op::Not, 1);
    child->get_input_pin("a").connect_sink(invert.create_sink_pin(0));
    auto result = invert.create_driver_pin(0);
    gu::set_ubits(result, 1);
    result.connect_sink(child->get_output_pin("y"));
    opaque = source.create_io("opaque");
    opaque->add_input("a", 1);
    opaque->add_output("y", 2);
    opaque->set_bits("a", 1);
    opaque->set_bits("y", 1);
    instance(*child, opaque);
    top = make_graph(source, "top");
    instance(*top, child->get_io()).connect_sink(top->get_output_pin("y"));
    instance(*top, child->get_io());
  }
};

TEST_F(FlattenAdmission, WideInputsKeepTheirPortsAndInstanceIdentity) {
  constexpr unsigned            count = 4096;
  auto                          wide  = make_graph(source, "wide_top");
  auto                          join  = gu::create_typed_node(*wide, Ntype_op::Or, 1);
  std::vector<hhds::Pin_class>  inputs;
  std::vector<hhds::Node_class> producers;
  for (unsigned i = 0; i < count; ++i) {
    inputs.push_back(join.create_sink_pin(i + 1));
  }
  for (unsigned i = 0; i < count; ++i) {
    auto output = instance(*wide, opaque);
    producers.push_back(output.get_master_node());
    output.connect_sink(inputs[i]);
  }
  join.create_driver_pin(0).connect_sink(wide->get_output_pin("y"));

  hhds::GraphLibrary destination;
  Flat_origin_map    origins;
  auto               flat = flatten_hierarchy(wide.get(), &destination, "flat_wide", &origins);
  ASSERT_TRUE(flat);
  auto result = flat->get_output_pin("y").get_driver_pin().get_master_node();
  ASSERT_EQ(gu::type_op_of(result), Ntype_op::Or);
  for (unsigned i = 0; i < count; ++i) {
    auto output = result.get_sink_pin(i + 1).get_driver_pin();
    ASSERT_FALSE(output.is_invalid());
    EXPECT_EQ(output.get_port_id(), 2U);
    auto producer = output.get_master_node();
    EXPECT_EQ(origins.at(producer).src_node, producers[i]);
    EXPECT_EQ(producer.get_sink_pin(1).get_driver_pin(), flat->get_input_pin("a"));
  }
}

TEST_F(FlattenAdmission, RefusalDuringExpansionWiringAndCompletionPreservesSources) {
  const auto gids   = source.all_gids();
  const auto driver = top->get_output_pin("y").get_driver_pin();
  for (auto stop : {"flatten-run",
                    "flatten-run-step",
                    "flatten-make_ctx-step",
                    "flatten-create_nodes-step",
                    "flatten-opaque-port",
                    "flatten-opaque-copy",
                    "flatten-opaque-copied",
                    "flatten-resolve_driver",
                    "flatten-resolve_output_of",
                    "flatten-apply_port_shape",
                    "flatten-wire_edges-step",
                    "flatten-wire_top_outputs-step",
                    "flatten-complete_bbox_outputs-step",
                    "flatten-complete"}) {
    SCOPED_TRACE(stop);
    hhds::GraphLibrary output;
    Flat_origin_map    origins;
    bool               refused   = false;
    const Admission    admission = [&](std::string_view stage, uint64_t) {
      EXPECT_FALSE(refused);
      if (stage == stop) {
        refused = true;
        return false;
      }
      return true;
    };
    EXPECT_FALSE(flatten_hierarchy(top.get(), &output, "flat", &origins, false, {}, admission));
    EXPECT_TRUE(refused);
    EXPECT_EQ(source.all_gids(), gids);
    EXPECT_EQ(top->get_output_pin("y").get_driver_pin(), driver);
    EXPECT_EQ(child->get_output_pin("y").get_driver_pin(), invert.get_driver_pin(0));
    EXPECT_FALSE(opaque->get_graph());
    // output/origins deliberately die together: neither partial object is a result.
  }
  hhds::GraphLibrary retry;
  Flat_origin_map    origins;
  uint64_t           opaque_ports = 0;
  const Admission    admission    = [&](std::string_view stage, uint64_t work) {
    if (stage == "flatten-opaque-port") {
      opaque_ports += work;
    }
    return true;
  };
  auto flat = flatten_hierarchy(top.get(), &retry, "flat", &origins, false, {}, admission);
  ASSERT_TRUE(flat);
  EXPECT_EQ(opaque_ports, 2U);  // shared declaration admitted once, despite two instances
  auto result = flat->get_output_pin("y").get_driver_pin().get_master_node();
  ASSERT_EQ(gu::type_op_of(result), Ntype_op::Not);
  EXPECT_EQ(result.get_sink_pin(0).get_driver_pin(), flat->get_input_pin("a"));
  EXPECT_EQ(origins.size(), 4U);  // two independent NOTs and two opaque instances
  unsigned nots = 0, boxes = 0;
  for (auto node : flat->body().nodes()) {
    if (gu::type_op_of(node) == Ntype_op::Not) {
      ++nots;
      EXPECT_EQ(node.get_sink_pin(0).get_driver_pin(), flat->get_input_pin("a"));
      EXPECT_EQ(origins.at(node).src_node, invert);
    } else if (gu::type_op_of(node) == Ntype_op::Sub) {
      ++boxes;
      EXPECT_EQ(node.get_subnode_io(), retry.find_io("opaque"));
      EXPECT_FALSE(node.try_get_driver_pin(2).is_invalid());
    }
  }
  EXPECT_EQ(nots, 2U);
  EXPECT_EQ(boxes, 2U);
  hhds::GraphLibrary exceptional;
  EXPECT_THROW((void)flatten_hierarchy(top.get(),
                                       &exceptional,
                                       "flat",
                                       nullptr,
                                       false,
                                       {},
                                       [](std::string_view, uint64_t) -> bool { throw std::runtime_error("caller error"); }),
               std::runtime_error);
}

TEST_F(FlattenAdmission, RepeatedHierarchyChargesEveryInstanceAndStopsBeforeFullExpansion) {
  // A small definition DAG expands into 32 copies of the child. Work must be
  // charged per occurrence, not only once per distinct definition.
  auto root = child;
  for (unsigned depth = 0; depth < 5; ++depth) {
    auto parent = make_graph(source, "level" + std::to_string(depth));
    auto left   = instance(*parent, root->get_io());
    auto right  = instance(*parent, root->get_io());
    auto both   = gu::create_typed_node(*parent, Ntype_op::And, 1);
    left.connect_sink(both.create_sink_pin(0));
    right.connect_sink(both.create_sink_pin(1));
    auto result = both.create_driver_pin(0);
    gu::set_ubits(result, 1);
    result.connect_sink(parent->get_output_pin("y"));
    root = parent;
  }
  hhds::GraphLibrary full;
  Flat_origin_map    origins;
  uint64_t           full_steps = 0, construction_steps = 0;
  ASSERT_TRUE(flatten_hierarchy(root.get(), &full, "flat", &origins, false, {}, [&](std::string_view stage, uint64_t work) {
    if (stage == "flatten-wire_edges" && !construction_steps) {
      construction_steps = full_steps;
    }
    full_steps += work;
    return true;
  }));
  ASSERT_EQ(origins.size(), 32U * 2U + 31U);
  ASSERT_GT(full_steps, 100U);
  hhds::GraphLibrary limited;
  Flat_origin_map    partial;
  ASSERT_GT(construction_steps, 50U);
  uint64_t remaining = construction_steps / 2;
  bool     refused   = false;
  EXPECT_FALSE(flatten_hierarchy(root.get(), &limited, "flat", &partial, false, {}, [&](std::string_view, uint64_t work) {
    EXPECT_FALSE(refused);
    if (work > remaining) {
      refused = true;
      return false;
    }
    remaining -= work;
    return true;
  }));
  EXPECT_TRUE(refused);
  EXPECT_LT(partial.size(), origins.size());
}
}  // namespace
}  // namespace livehd::partition

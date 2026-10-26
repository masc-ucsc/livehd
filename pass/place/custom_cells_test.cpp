// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "custom_cells.hpp"

#include <stdexcept>
#include <string>
#include <vector>

#include "gtest/gtest.h"
#include "logical_writer.hpp"
#include "node_util.hpp"

namespace livehd::placement {
namespace {
namespace gu = graph_util;

// Family "a" has three drive variants, family "b" one; widths grow with rank.
std::vector<Custom_variant> variants() {
  std::vector<Custom_variant> out;
  for (unsigned rank = 0; rank < 3; ++rank) {
    Custom_variant v;
    v.name          = "__lhd_domino_a_v" + std::to_string(rank) + "_t0";
    v.family        = "__lhd_domino_a_t0";
    v.register_cell = "dff_" + std::to_string(rank + 1);
    v.rank          = rank;
    v.rails         = {"R0", "R1"};
    v.area_um2      = 10 + rank;
    v.width_dbu     = 1000 + 100 * rank;
    v.clock_ff      = 2 + rank;
    out.push_back(v);
  }
  Custom_variant b = out.front();
  b.name           = "__lhd_domino_b_v0_t0";
  b.family         = "__lhd_domino_b_t0";
  b.rails          = {"R0"};
  out.push_back(b);
  return out;
}
usyn::Physical_cell_interface interface_of(const Custom_variant& v) { return {v.name, v.rails, v.clock, v.output}; }
void                          expect_same_pins(const hhds::GraphIO& a, const hhds::GraphIO& b) {
  const auto same = [](const auto& x, const auto& y) {
    ASSERT_EQ(x.size(), y.size());
    for (size_t i = 0; i < x.size(); ++i) {
      EXPECT_EQ(x[i].name, y[i].name);
      EXPECT_EQ(x[i].port_id, y[i].port_id) << x[i].name;
      EXPECT_EQ(x[i].loop_break, y[i].loop_break) << x[i].name;
      EXPECT_EQ(x[i].bits, y[i].bits) << x[i].name;
      EXPECT_EQ(x[i].unsign, y[i].unsign) << x[i].name;
    }
  };
  same(a.get_input_pin_decls(), b.get_input_pin_decls());
  same(a.get_output_pin_decls(), b.get_output_pin_decls());
}

TEST(CustomCells, TheCatalogStepsWithinOneFamilyByRank) {
  auto       list    = variants();
  const auto catalog = custom_catalog({list.rbegin(), list.rend()});  // any input order
  ASSERT_EQ(catalog->families.at("__lhd_domino_a_t0").size(), 3U);
  EXPECT_EQ(catalog->step("__lhd_domino_a_v0_t0", 1)->name, "__lhd_domino_a_v1_t0");
  EXPECT_EQ(catalog->step("__lhd_domino_a_v0_t0", 2)->name, "__lhd_domino_a_v2_t0");
  EXPECT_EQ(catalog->step("__lhd_domino_a_v2_t0", -1)->name, "__lhd_domino_a_v1_t0");
  EXPECT_EQ(catalog->step("__lhd_domino_a_v2_t0", 1), nullptr);
  EXPECT_EQ(catalog->step("__lhd_domino_a_v0_t0", -1), nullptr);
  EXPECT_EQ(catalog->step("__lhd_domino_b_v0_t0", 1), nullptr);  // never into another family
  EXPECT_EQ(catalog->find("nope"), nullptr);
  // The identity follows every field, not the input order.
  EXPECT_EQ(custom_catalog(variants())->identity, catalog->identity);
  list.front().clock_ff = 9;
  EXPECT_NE(custom_catalog(list)->identity, catalog->identity);
  // Malformed catalogs.
  list = variants();
  list.push_back(list.front());
  EXPECT_THROW(custom_catalog(list), std::invalid_argument);  // a name twice
  list          = variants();
  list[1].rails = {"R0"};
  EXPECT_THROW(custom_catalog(list), std::invalid_argument);  // one family, other pins
  list         = variants();
  list[1].rank = 0;
  EXPECT_THROW(custom_catalog(list), std::invalid_argument);  // a repeated rank
}

TEST(CustomCells, TheDeclarationIsTheGeneratorsAndASwapKeepsEveryPin) {
  const auto         catalog = custom_catalog(variants());
  const auto&        v0      = *catalog->find("__lhd_domino_a_v0_t0");
  const auto&        v2      = *catalog->find("__lhd_domino_a_v2_t0");
  hhds::GraphLibrary library, reference;
  // The generator's declaration path names the instance's master.
  const auto         decl = usyn::declare_physical_cell(library, interface_of(v0));
  auto               io   = library.create_io("top");
  io->add_input("a", 1);
  io->add_input("b", 2);
  io->add_input("clk", 3);
  io->add_output("y", 4);
  for (const auto* name : {"a", "b", "clk", "y"}) {
    io->set_bits(name, 1);
  }
  auto graph = io->create_graph();
  auto node  = gu::create_typed_node(*graph, Ntype_op::Sub);
  node.set_subnode(decl);
  node.set_name("q1");
  node.create_sink_pin(decl->get_input_port_id("R0")).connect_driver(graph->get_input_pin("a"));
  node.create_sink_pin(decl->get_input_port_id("R1")).connect_driver(graph->get_input_pin("b"));
  node.create_sink_pin(decl->get_input_port_id("CLK")).connect_driver(graph->get_input_pin("clk"));
  auto q = node.create_driver_pin(decl->get_output_port_id("Q"));
  gu::set_bits(q, 1);
  gu::set_pin_name(q, "q1");
  gu::add_signal_roles(q, 4);
  graph->get_output_pin("y").connect_driver(q);

  change_custom_master(node, v0, v2);
  ASSERT_EQ(node.get_subnode_io()->get_name(), v2.name);
  // The new declaration equals the generator's for v2.
  expect_same_pins(*node.get_subnode_io(), *usyn::declare_physical_cell(reference, interface_of(v2)));
  EXPECT_TRUE(node.get_subnode_io()->is_loop_break("Q"));
  // Rails, clock and output keep their connections, name and roles.
  EXPECT_EQ(node.get_sink_pin("R0").get_driver_pin(), graph->get_input_pin("a"));
  EXPECT_EQ(node.get_sink_pin("R1").get_driver_pin(), graph->get_input_pin("b"));
  EXPECT_EQ(node.get_sink_pin("CLK").get_driver_pin(), graph->get_input_pin("clk"));
  const auto out = node.get_driver_pin("Q");
  EXPECT_EQ(graph->get_output_pin("y").get_driver_pin(), out);
  EXPECT_EQ(gu::pin_name_of(out), "q1");
  EXPECT_EQ(gu::signal_roles_of(out), 4);
  EXPECT_EQ(gu::bits_of(out), 1);
  // Back down: the existing declaration is reused.
  change_custom_master(node, v2, v0);
  EXPECT_EQ(node.get_subnode_io(), decl);

  // Refusals leave the instance unchanged.
  const auto& b = *catalog->find("__lhd_domino_b_v0_t0");
  EXPECT_THROW(change_custom_master(node, v0, b), std::invalid_argument);   // another family
  EXPECT_THROW(change_custom_master(node, v2, v0), std::invalid_argument);  // not an instance of v2
  auto other  = v2;
  other.rails = {"R0", "R9"};
  EXPECT_THROW(change_custom_master(node, v0, other), std::invalid_argument);  // another interface
  EXPECT_EQ(node.get_subnode_io(), decl);
  // A clashing declaration under the variant's name is refused.
  auto clash = library.create_io(catalog->find("__lhd_domino_a_v1_t0")->name);
  clash->add_input("R0", 1);
  EXPECT_THROW(change_custom_master(node, v0, *catalog->find("__lhd_domino_a_v1_t0")), std::invalid_argument);
  EXPECT_EQ(node.get_subnode_io(), decl);
}
}  // namespace
}  // namespace livehd::placement

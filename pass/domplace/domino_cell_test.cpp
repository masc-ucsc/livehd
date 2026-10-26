// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "domino_cell.hpp"

#include <functional>
#include <stdexcept>

#include "gtest/gtest.h"
#include "logical_writer.hpp"
#include "node_util.hpp"
#include "provisional_cell.hpp"

namespace livehd::domplace {
namespace {
namespace gu = graph_util;
using Kind   = usyn::Gate_formula::Kind;
using Node   = usyn::Gate_formula::Node;

usyn::Gate_formula formula(std::vector<Node> nodes) {
  usyn::Gate_formula f;
  f.nodes = std::move(nodes);
  return f;
}
Node lit(uint32_t variable, bool negative = false) { return {Kind::literal, 0, 0, variable, negative}; }
Node series(uint32_t left, uint32_t right) { return {Kind::series, left, right}; }
Node parallel(uint32_t left, uint32_t right) { return {Kind::parallel, left, right}; }

Domino_master master(const usyn::Gate_formula& f, uint32_t inputs) {
  usyn::Budget work{100000};
  auto         result = make_domino_master(f, inputs, work);
  EXPECT_TRUE(result);
  return *result;
}

// Evaluate a model's driver pin with each rail pin set from its input and
// polarity: an independent check of the graph the model emitter wrote.
bool evaluate(const hhds::Pin_class& pin, const Domino_master& m, const usyn::Physical_cell_interface& cell, uint32_t x) {
  if (gu::is_graph_input_pin(pin)) {
    const auto name = gu::pin_name_of(pin);
    for (size_t k = 0; k < cell.rails.size(); ++k) {
      if (cell.rails[k] == name) {
        return (((x >> m.rails[k].input) & 1) != 0) != m.rails[k].negative;
      }
    }
    ADD_FAILURE() << "unexpected model input " << name;
    return false;
  }
  const auto node = pin.get_master_node();
  const auto op   = gu::type_op_of(node);
  EXPECT_TRUE(op == Ntype_op::And || op == Ntype_op::Or);
  bool value = op == Ntype_op::And;
  for (const auto& operand : gu::inp_sink_drivers(node)) {
    const auto v = evaluate(operand.driver, m, cell, x);
    value        = op == Ntype_op::And ? value && v : value || v;
  }
  return value;
}
}  // namespace

TEST(DominoCell, IdentityIsExactBooleanAndStable) {
  // (a & b) | c and (a | b) & c: three devices, stack 2, two branches each.
  const auto first  = master(formula({lit(0), lit(1), series(0, 1), lit(2), parallel(2, 3)}), 3);
  const auto second = master(formula({lit(0), lit(1), parallel(0, 1), lit(2), series(2, 3)}), 3);
  const auto m1 = first.formula.metrics(), m2 = second.formula.metrics();
  ASSERT_TRUE(m1 && m2);
  EXPECT_EQ(m1->transistors, m2->transistors);
  EXPECT_EQ(m1->stack, m2->stack);
  EXPECT_EQ(m1->branches, m2->branches);
  EXPECT_NE(first.identity, second.identity);
  EXPECT_NE(first.base, second.base);

  // The same function, formula and rails always give the same master, and the
  // canonical encoding is pinned: a change here renames every generated cell.
  const auto again = master(formula({lit(0), lit(1), series(0, 1), lit(2), parallel(2, 3)}), 3);
  EXPECT_EQ(again.identity, first.identity);
  EXPECT_EQ(first.base, "__lhd_domino_aab0e3b7026abf30");
  EXPECT_TRUE(is_provisional_domino_cell(first.base));
  // A different transistor order is a different physical network.
  const auto swapped = master(formula({lit(1), lit(0), series(0, 1), lit(2), parallel(2, 3)}), 3);
  EXPECT_EQ(swapped.function, first.function);
  EXPECT_NE(swapped.identity, first.identity);

  // Both polarities of one input are two rails, positive first.
  const auto mux = master(formula({lit(0), lit(1), series(0, 1), lit(0, true), lit(2), series(3, 4), parallel(2, 5)}), 3);
  EXPECT_EQ(mux.rails,
            (std::vector<usyn::Gate_rail>{
                {0, false},
                {0,  true},
                {1, false},
                {2, false}
  }));

  usyn::Budget none{0};
  EXPECT_FALSE(make_domino_master(first.formula, 3, none));
  auto inverted            = first.formula;
  inverted.output_inverted = true;
  usyn::Budget work{1000};
  EXPECT_THROW((void)make_domino_master(inverted, 3, work), std::invalid_argument);
  EXPECT_THROW((void)make_domino_master(first.formula, 4, work), std::invalid_argument);  // unused input
  EXPECT_THROW((void)make_domino_master(formula({}), 1, work), std::invalid_argument);
}

TEST(DominoCell, DeclarationPortsLoopBreakAndCollision) {
  const auto m    = master(formula({lit(0), lit(1, true), series(0, 1), lit(2), parallel(2, 3)}), 3);
  const auto cell = domino_interface(m, 0);
  EXPECT_EQ(cell.name, m.base + "_v0_t0");
  EXPECT_EQ(domino_interface(m, 2, 1).name, m.base + "_v2_t1");
  EXPECT_EQ(cell.rails, (std::vector<std::string>{"R0", "R1", "R2"}));

  hhds::GraphLibrary library;
  const auto         decl = usyn::declare_physical_cell(library, cell);
  ASSERT_TRUE(decl);
  EXPECT_FALSE(decl->has_graph());
  for (const auto& [name, id] : {
           std::pair{ "R0", 1},
           { "R1", 2},
           { "R2", 3},
           {"CLK", 4}
  }) {
    ASSERT_TRUE(decl->has_input(name)) << name;
    EXPECT_EQ(decl->get_input_port_id(name), id) << name;
    EXPECT_EQ(decl->get_bits(name), 1U) << name;
  }
  ASSERT_TRUE(decl->has_output("Q"));
  EXPECT_EQ(decl->get_output_port_id("Q"), 5);
  EXPECT_TRUE(decl->is_loop_break("Q"));
  EXPECT_EQ(usyn::declare_physical_cell(library, cell)->get_gid(), decl->get_gid());  // idempotent

  hhds::GraphLibrary bodied;
  (void)bodied.create_io(cell.name)->create_graph();
  EXPECT_THROW((void)usyn::declare_physical_cell(bodied, cell), std::runtime_error);
}

TEST(DominoCell, ModelsImplementTheMasterFunction) {
  // (a & !b) | (!a & c): a binate, so both of its rails are pins.
  const auto m = master(formula({lit(0), lit(1, true), series(0, 1), lit(0, true), lit(2), series(3, 4), parallel(2, 5)}), 3);
  ASSERT_EQ(m.rails.size(), 4U);
  const auto         cell = domino_interface(m, 0);
  hhds::GraphLibrary models;
  emit_domino_model(models, m, cell);
  emit_domino_model(models, m, cell);  // no-op
  const auto io = models.find_io(cell.name);
  ASSERT_TRUE(io && io->has_graph());
  hhds::GraphLibrary netlist;
  const auto         decl = usyn::declare_physical_cell(netlist, cell);
  ASSERT_EQ(io->get_input_pin_decls().size(), decl->get_input_pin_decls().size());
  for (size_t i = 0; i < decl->get_input_pin_decls().size(); ++i) {
    EXPECT_EQ(io->get_input_pin_decls()[i].name, decl->get_input_pin_decls()[i].name);
    EXPECT_EQ(io->get_input_pin_decls()[i].port_id, decl->get_input_pin_decls()[i].port_id);
  }
  EXPECT_TRUE(io->is_loop_break("Q"));

  const auto graph = io->get_graph();
  const auto q     = graph->get_output_pin("Q").get_driver_pin();
  const auto flop  = q.get_master_node();
  ASSERT_EQ(gu::type_op_of(flop), Ntype_op::Flop);
  EXPECT_EQ(gu::pin_name_of(gu::get_driver_of_sink_name(flop, "clock_pin")), "CLK");
  EXPECT_TRUE(gu::get_driver_of_sink_name(flop, "initial").is_invalid());
  const auto din = gu::get_driver_of_sink_name(flop, "din");
  for (uint32_t x = 0; x < 8; ++x) {
    EXPECT_EQ(evaluate(din, m, cell, x), m.function.get(x)) << x;
    EXPECT_EQ(m.function.get(x), ((x & 1) && !(x & 2)) || (!(x & 1) && (x & 4))) << x;
  }

  const auto verilog = domino_verilog_model(m, cell);
  EXPECT_NE(verilog.find("module " + cell.name + "(R0, R1, R2, R3, CLK, Q);"), std::string::npos) << verilog;
  EXPECT_NE(verilog.find("always @(posedge CLK) Q <= ((R0 & R2) | (R1 & R3));"), std::string::npos) << verilog;
  EXPECT_THROW((void)domino_verilog_model(m, domino_interface(master(formula({lit(0)}), 1), 0)), std::invalid_argument);
}

}  // namespace livehd::domplace

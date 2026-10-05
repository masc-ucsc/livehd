// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "native_cost.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>

#include "gtest/gtest.h"

namespace livehd::usyn {
namespace {
std::string library(std::string_view extra = "") {
  const auto    path = std::filesystem::path(std::getenv("TEST_TMPDIR")) / "native.lib";
  std::ofstream out(path);
  out << R"LIB(library(t) {
    cell(INV) {area:0.2; pin(A) {direction:input;} pin(Y) {direction:output; function:"A'";} }
    cell(NAND) {area:0.8; pin(A) {direction:input;} pin(B) {direction:input;}
      pin(Y) {direction:output; function:"!(A B)";} }
    cell(XOR) {area:1.2; pin(A) {direction:input;} pin(B) {direction:input;}
      pin(Y) {direction:output; function:"A^B";} }
  )LIB"
      << extra << "}";
  return path.string();
}
TEST(NativeCost, InvertersAreDemandedOnceAndNandOutputPolarityIsExplicit) {
  Budget work{10000000};
  auto   model = read_native_cost_model(library(), work);
  ASSERT_TRUE(model);
  EXPECT_EQ(model->admitted_cells, 3U);
  Xag              graph;
  auto             a = graph.input("a"), b = graph.input("b"), and_gate = graph.land(a, b);
  const std::array positive{and_gate, and_gate};
  auto             cost = estimate_native_cost(graph, positive, *model, work);
  ASSERT_EQ(cost.status, Status::feasible);
  EXPECT_EQ(cost.gates, 2U);
  EXPECT_DOUBLE_EQ(cost.area, 1.0);
  const std::array both{and_gate, ~and_gate};
  auto             rails = estimate_native_cost(graph, both, *model, work);
  ASSERT_EQ(rails.status, Status::feasible);
  EXPECT_EQ(rails.gates, 2U);
  const std::array inverted{~and_gate};
  auto             negative = estimate_native_cost(graph, inverted, *model, work);
  ASSERT_EQ(negative.status, Status::feasible);
  EXPECT_EQ(negative.gates, 1U);
  EXPECT_DOUBLE_EQ(negative.area, 0.8);
  const std::array input_rails{a, ~a, ~a};
  auto             input = estimate_native_cost(graph, input_rails, *model, work);
  ASSERT_EQ(input.status, Status::feasible);
  EXPECT_EQ(input.gates, 1U);
  EXPECT_DOUBLE_EQ(input.area, 0.2);
}
TEST(NativeCost, PinPermutationsLegalCellFilteringAndBooleanSyntax) {
  const auto path = library(R"LIB(
    cell(AOI) {area:0.5; pin(C) {direction:input;} pin(A) {direction:input;} pin(B) {direction:input;}
      pin(Y) {direction:output; function:"~((A & B) + C)";
}
}
cell(DISABLED) {
area:
  0.01;
dont_use:
  true;
  pin(A) {
  direction:
    input;
  }
  pin(Y) {
  direction:
    output;
  function:
    "!A";
  }
}
cell(UNKNOWN) {
area:
  0.01;
  pin(A) {
  direction:
    input;
  }
  pin(Y) {
  direction:
    output;
  function:
    "MISSING";
  }
}
  )LIB");
  Budget     work{10000000};
  auto       model = read_native_cost_model(path, work);
  ASSERT_TRUE(model);
  EXPECT_EQ(model->admitted_cells, 4U);
  EXPECT_EQ(model->skipped_cells, 1U);
  Xag              graph;
  auto             a = graph.input("a"), b = graph.input("b"), c = graph.input("c");
  const std::array outputs{~graph.lor(graph.land(a, b), c)};
  auto             cost = estimate_native_cost(graph, outputs, *model, work);
  ASSERT_EQ(cost.status, Status::feasible);
  EXPECT_EQ(cost.gates, 1U);
  EXPECT_DOUBLE_EQ(cost.area, 0.5);
  const std::array xor_outputs{graph.lxor(a, b)};
  auto             xor_cost = estimate_native_cost(graph, xor_outputs, *model, work);
  ASSERT_EQ(xor_cost.status, Status::feasible);
  EXPECT_EQ(xor_cost.gates, 1U);
  EXPECT_DOUBLE_EQ(xor_cost.area, 1.2);
}
TEST(NativeCost, BudgetAndProcessRefusalNeverProduceUsableEstimates) {
  Budget setup{1000000};
  auto   model = read_native_cost_model(library(), setup);
  ASSERT_TRUE(model);
  Xag              graph;
  const std::array outputs{graph.land(graph.input("a"), graph.input("b"))};
  Budget           empty{0};
  EXPECT_EQ(estimate_native_cost(graph, outputs, *model, empty).status, Status::search_exhausted);
  EXPECT_TRUE(empty.credit_floor().bound);
  Budget cancelled{1000000};
  cancelled.admission          = [] { return false; };
  cancelled.admission_interval = 1;
  EXPECT_FALSE(read_native_cost_model(library(), cancelled));
  EXPECT_TRUE(cancelled.resource_exhausted);
}
TEST(NativeCost, UnsupportedWideCutsRetainThePrimitiveLegalCover) {
  Budget work{10000000};
  auto   model = read_native_cost_model(library(), work);
  ASSERT_TRUE(model);
  Xag              graph;
  auto             a = graph.input("a"), b = graph.input("b"), c = graph.input("c"), d = graph.input("d");
  auto             root = graph.land(graph.lxor(a, b), graph.lxor(c, d));
  const std::array outputs{root, ~root};
  auto             cost = estimate_native_cost(graph, outputs, *model, work);
  ASSERT_EQ(cost.status, Status::feasible);
  EXPECT_EQ(cost.gates, 4U);
  EXPECT_DOUBLE_EQ(cost.area, 3.4);
}
// Liberty binds XOR tighter than AND (OpenSTA LibExprParse.yy: `%left '*' '&'`
// before `%left '^'`), so `A^B*C` is `(A^B)*C`, never `A^(B*C)`.
TEST(NativeCost, XorBindsTighterThanAndInCellFunctions) {
  const auto path = library(R"LIB(
    cell(XA) {area:0.7; pin(A) {direction:input;} pin(B) {direction:input;} pin(C) {direction:input;}
      pin(Y) {direction:output; function:"A^B*C";} }
  )LIB");
  Budget     work{10000000};
  auto       model = read_native_cost_model(path, work);
  ASSERT_TRUE(model);
  Xag              graph;
  auto             a = graph.input("a"), b = graph.input("b"), c = graph.input("c");
  const std::array xor_then_and{graph.land(graph.lxor(a, b), c)};
  auto             hit = estimate_native_cost(graph, xor_then_and, *model, work);
  ASSERT_EQ(hit.status, Status::feasible);
  EXPECT_EQ(hit.gates, 1U) << "(A^B)*C is the one XA cell";
  EXPECT_DOUBLE_EQ(hit.area, 0.7);
  const std::array and_inside_xor{graph.lxor(a, graph.land(b, c))};
  auto             miss = estimate_native_cost(graph, and_inside_xor, *model, work);
  ASSERT_EQ(miss.status, Status::feasible);
  EXPECT_NE(miss.area, 0.7) << "A^(B*C) is not what XA computes";
}

}  // namespace
}  // namespace livehd::usyn

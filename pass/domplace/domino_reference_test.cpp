// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "domino_reference.hpp"

#include <filesystem>
#include <format>
#include <fstream>

#include "gtest/gtest.h"

namespace livehd::domplace {
namespace {
std::string inv(std::string_view name, std::string_view area) {
  return std::format(
      "  cell({}) {{ {} pin(A) {{direction : input; capacitance : 0.002;}}\n"
      "    pin(Y) {{direction : output; function : \"!A\";}} }}\n",
      name,
      area);
}
std::string dff(std::string_view name, std::string_view area) {
  return std::format(
      "  cell({}) {{ {} ff(IQ, IQN) {{clocked_on : \"CLK\"; next_state : \"D\";}}\n"
      "    pin(CLK) {{direction : input; clock : true;}} pin(D) {{direction : input;}}\n"
      "    pin(Q) {{direction : output; function : \"IQ\";}} }}\n",
      name,
      area);
}
std::string library(std::string_view name, const std::string& cells) { return std::format("library({}) {{\n{}}}\n", name, cells); }
std::string write(std::string_view file, const std::string& text) {
  const auto path = std::filesystem::path(testing::TempDir()) / file;
  std::ofstream(path) << text;
  return path.string();
}
// A cheaper buffer that must never be taken for the inverter, two inverters
// and a three-rung register family. Areas are binary-exact.
std::string reference_library() {
  return write("reference.lib",
               library("ref",
                       inv("INV_X2", "area : 2.0;") + inv("INV_X1", "area : 1.25;")
                           + "  cell(BUF_X1) { area : 1.0; pin(A) {direction : input;}\n"
                             "    pin(Y) {direction : output; function : \"A\";} }\n"
                           + dff("DFF_X4", "area : 23.5;") + dff("DFF_X1", "area : 20;") + dff("DFF_X2", "area : 21.25;")));
}
usyn::Gate_formula::Metrics devices(uint32_t transistors) {
  usyn::Gate_formula::Metrics metrics;
  metrics.transistors = transistors;
  return metrics;
}
}  // namespace

TEST(DominoReference, FunctionPickRecordsNamesAndChargesTheRegisterOnce) {
  std::string why;
  const auto  reference = resolve_domino_reference(reference_library(), "", "", 4, 3, &why);
  ASSERT_TRUE(reference) << why;
  EXPECT_EQ(reference->inverter.name, "INV_X1");
  EXPECT_EQ(reference->inverter_source, "liberty-function");
  EXPECT_EQ(reference->register_source, "liberty-function");
  EXPECT_EQ(reference->corner, "ref");
  ASSERT_EQ(reference->registers.size(), 3U);
  EXPECT_EQ(reference->registers[0].name, "DFF_X1");
  EXPECT_EQ(reference->registers[1].name, "DFF_X2");
  EXPECT_EQ(reference->registers[2].name, "DFF_X4");
  EXPECT_FALSE(reference->registers[0].clk_to_q_ps);  // no table: absent, not zero
  EXPECT_FALSE(reference->registers[0].hold_ps);

  // x = (N_PDN + aux) / 2: four literal devices and four auxiliary devices are
  // four inverter equivalents; the full register is added exactly once.
  const auto four = domino_area(devices(4), *reference, 0);
  EXPECT_EQ(four.x, 4.0);
  EXPECT_EQ(four.a_not, 1.25);
  EXPECT_EQ(four.a_register, 20.0);
  EXPECT_EQ(four.a_domino, 25.0);
  EXPECT_EQ(domino_area(devices(5), *reference, 0).a_domino, 4.5 * 1.25 + 20);

  // Drive variants share the PDN term and take the ladder's own areas: X4 is
  // the family's area, never four times the base register.
  for (size_t variant = 0; variant < 3; ++variant) {
    const auto area = domino_area(devices(4), *reference, variant);
    EXPECT_EQ(area.a_domino - area.a_register, four.a_domino - four.a_register) << variant;
    EXPECT_EQ(area.a_register, reference->registers[variant].area) << variant;
  }
  EXPECT_EQ(domino_area(devices(4), *reference, 2).a_domino - four.a_domino, 3.5);
  EXPECT_THROW((void)domino_area(devices(4), *reference, 3), std::out_of_range);
}

TEST(DominoReference, ExplicitNamesStartTheLadderAtTheNamedDrive) {
  std::string why;
  const auto  reference = resolve_domino_reference(reference_library(), "INV_X2", "DFF_X2", 0, 3, &why);
  ASSERT_TRUE(reference) << why;
  EXPECT_EQ(reference->inverter.name, "INV_X2");
  EXPECT_EQ(reference->inverter_source, "explicit");
  EXPECT_EQ(reference->register_source, "explicit");
  ASSERT_EQ(reference->registers.size(), 2U);
  EXPECT_EQ(reference->registers[0].name, "DFF_X2");
  EXPECT_EQ(reference->registers[1].name, "DFF_X4");
  EXPECT_EQ(domino_area(devices(0), *reference, 0).a_domino, 21.25);
  const auto one = resolve_domino_reference(reference_library(), "", "", 4, 1, &why);
  ASSERT_TRUE(one) << why;
  ASSERT_EQ(one->registers.size(), 1U);
  EXPECT_EQ(one->registers[0].name, "DFF_X1");
}

TEST(DominoReference, MissingCellsAreasAndCornersAreDiagnosedNeverZero) {
  const auto lib = reference_library();
  const auto refused
      = [](const std::string& files, std::string_view inverter, std::string_view reg, uint32_t aux, uint32_t variants) {
          std::string why;
          EXPECT_FALSE(resolve_domino_reference(files, inverter, reg, aux, variants, &why));
          EXPECT_FALSE(why.empty());
          return why;
        };
  EXPECT_NE(refused(lib, "NOPE", "", 4, 3).find("NOPE"), std::string::npos);
  EXPECT_NE(refused(lib, "BUF_X1", "", 4, 3).find("BUF_X1"), std::string::npos);
  EXPECT_NE(refused(lib, "", "INV_X1", 4, 3).find("INV_X1"), std::string::npos);
  EXPECT_NE(refused(lib, "", "", 65, 3).find("65"), std::string::npos);
  EXPECT_FALSE(refused(lib, "", "", 4, 0).empty());
  EXPECT_FALSE(refused(lib, "", "", 4, 4).empty());

  const auto no_inverter_area = write("no_inverter_area.lib", library("t", inv("INV", "") + dff("DFF", "area : 20;")));
  EXPECT_NE(refused(no_inverter_area, "", "", 4, 3).find("Liberty area"), std::string::npos);
  EXPECT_NE(refused(no_inverter_area, "INV", "", 4, 3).find("Liberty area"), std::string::npos);
  const auto no_register_area = write("no_register_area.lib", library("t", inv("INV", "area : 1;") + dff("DFF", "")));
  EXPECT_NE(refused(no_register_area, "", "", 4, 3).find("no Liberty area"), std::string::npos);
  const auto no_register = write("no_register.lib", library("t", inv("INV", "area : 1;")));
  EXPECT_NE(refused(no_register, "", "", 4, 3).find("flip-flop"), std::string::npos);
  const auto split = write("split.lib", library("a", inv("INV", "area : 1;")) + library("b", dff("DFF", "area : 20;")));
  EXPECT_NE(refused(split, "", "", 4, 3).find("library group"), std::string::npos);
}

}  // namespace livehd::domplace

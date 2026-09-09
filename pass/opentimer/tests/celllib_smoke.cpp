// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "gtest/gtest.h"
#include "ot/liberty/celllib.hpp"

namespace {
class CelllibSmoke : public testing::Test {
protected:
  std::filesystem::path path;

  void SetUp() override {
    char name[] = "/tmp/livehd-celllib-XXXXXX";
    int  fd     = mkstemp(name);
    ASSERT_GE(fd, 0);
    close(fd);
    path = name;
  }
  void TearDown() override { std::filesystem::remove(path); }
  void write(const std::string& body) { std::ofstream(path) << body; }
};

TEST_F(CelllibSmoke, TemplateAxesAndBusAttributes) {
  write(R"lib(library(test) {
    lu_table_template(delay) {
      variable_1 : input_net_transition;
      variable_2 : total_output_net_capacitance;
      index_1("1, 2"); index_2("3, 4, 5");
    }
    type(data) { base_type : array; data_type : bit; bit_width : 4; bit_from : 0; bit_to : 3; }
    cell(RAM) {
      pin(clk) { direction : input; clock : true; }
      bus(A) {
        bus_type : data; direction : input; capacitance : 0.25;
        pin(A[3:0]) {
          timing() { related_pin : clk; timing_type : setup_rising;
            rise_constraint(delay) { values("1, 2, 3", "4, 5, 6"); }
          }
        }
      }
      bus(Q) {
        bus_type : data; direction : output;
        timing() { related_pin : clk; timing_type : rising_edge;
          cell_rise(delay) { index_1("7"); values("8, 9, 10"); }
        }
      }
    }
  })lib");
  ot::Celllib lib;
  lib.read(path);
  const auto* cell = lib.cell("RAM");
  ASSERT_NE(cell, nullptr);
  EXPECT_EQ(cell->cellpins.size(), 9);
  for (int bit = 0; bit < 4; ++bit) {
    const auto* input = cell->cellpin("A[" + std::to_string(bit) + "]");
    ASSERT_NE(input, nullptr);
    EXPECT_EQ(input->direction, ot::CellpinDirection::INPUT);
    EXPECT_EQ(input->capacitance, 0.25f);
    ASSERT_EQ(input->timings.size(), 1);
    const auto& lut = input->timings.front().rise_constraint;
    ASSERT_TRUE(lut);
    EXPECT_EQ(lut->indices1, (std::vector<float>{1, 2}));
    EXPECT_EQ(lut->indices2, (std::vector<float>{3, 4, 5}));
    EXPECT_EQ(lut->table, (std::vector<float>{1, 2, 3, 4, 5, 6}));
    const auto* output = cell->cellpin("Q[" + std::to_string(bit) + "]");
    ASSERT_NE(output, nullptr);
    EXPECT_EQ(output->direction, ot::CellpinDirection::OUTPUT);
    ASSERT_EQ(output->timings.size(), 1);
    const auto& out_lut = output->timings.front().cell_rise;
    ASSERT_TRUE(out_lut);
    EXPECT_EQ(out_lut->indices1, (std::vector<float>{7}));
    EXPECT_EQ(out_lut->indices2, (std::vector<float>{3, 4, 5}));
    EXPECT_EQ(out_lut->table, (std::vector<float>{8, 9, 10}));
  }
}
}  // namespace

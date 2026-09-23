// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "pass_opentimer.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>

#include "gtest/gtest.h"

namespace {
class Sdc_probe : public Pass_opentimer {
public:
  explicit Sdc_probe(const Eprp_var& var) : Pass_opentimer(var) {
    timer.set_num_threads(1);
    timer.insert_primary_input("a").insert_primary_output("y").insert_net("wire");
    timer.disconnect_pin("a").disconnect_pin("y");
    timer.connect_pin("a", "wire").connect_pin("y", "wire");
  }
  using Pass_opentimer::clock_qor_json;
  using Pass_opentimer::read_sdc;
  using Pass_opentimer::timer;
  bool complete() const { return constraints_complete_; }
};
class SdcClock : public ::testing::Test {
protected:
  std::string directory;
  Eprp_var    var;
  void        SetUp() override {
    directory = (std::filesystem::temp_directory_path() / "livehd-sdc-clock-XXXXXX").string();
    ASSERT_NE(mkdtemp(directory.data()), nullptr);
    var.dict["files"] = directory + "/unused.v";
    std::ofstream(directory + "/unused.v") << "";
    var.set_stage_labels(var.dict);
  }
  void        TearDown() override { std::filesystem::remove_all(directory); }
  std::string write(const std::string& text) {
    const auto path = directory + "/timing.sdc";
    std::ofstream(path) << text;
    return path;
  }
};
TEST_F(SdcClock, ResolvesClockRelativeSetupAndHoldWithSeparateCornersAndTransitions) {
  Sdc_probe pass(var);
  pass.read_sdc(write(R"sdc(
create_clock -name virtual -period 100
set_input_delay -clock virtual -min 3 [all_inputs]
set_input_delay -clock virtual -max 10 [all_inputs]
set_input_delay -clock virtual -max -rise 12 [get_ports a]
set_output_delay -clock virtual -max 20 [all_outputs]
set_output_delay -clock virtual -min -5 [all_outputs]
)sdc"));
  ASSERT_EQ(pass.timer.report_slack("y", ot::MAX, ot::RISE), 68);
  EXPECT_EQ(pass.timer.report_slack("y", ot::MAX, ot::FALL), 70);
  EXPECT_EQ(pass.timer.report_slack("y", ot::MIN, ot::RISE), -2);
  EXPECT_EQ(pass.timer.report_slack("y", ot::MIN, ot::FALL), -2);
  const auto report = pass.clock_qor_json();
  EXPECT_TRUE(pass.complete());
  EXPECT_NE(report.find("\"setup_slack\":68"), std::string::npos);
  EXPECT_NE(report.find("\"hold_slack\":-2"), std::string::npos);
}
TEST_F(SdcClock, VirtualClockNameDoesNotTurnADataInputIntoAPhysicalClock) {
  Sdc_probe pass(var);
  pass.read_sdc(
      write("create_clock -name a -period 100\n"
            "set_input_delay -clock a 10 [all_inputs -no_clocks]\n"
            "set_output_delay -clock a 20 [all_outputs]\n"));
  pass.clock_qor_json();
  EXPECT_TRUE(pass.complete());
}
TEST_F(SdcClock, MissingAndUnsupportedClockSemanticsCannotCertifyCoverage) {
  const std::string clock  = "create_clock -name virtual -period 100\n";
  const std::string input  = "set_input_delay -clock virtual 10 [all_inputs]\n";
  const std::string output = "set_output_delay -clock virtual 20 [all_outputs]\n";
  for (const auto& text : {clock + "set_input_delay -clock virtual -max 10 [all_inputs]\n" + output,
                           clock + input + "set_output_delay -clock virtual -max 20 [all_outputs]\n",
                           clock + input + "set_output_delay -clock missing 20 [all_outputs]\n",
                           clock + input + output + "set_input_delay 5 [all_inputs]\n",
                           clock + input + output + "create_clock -name second -period 80\n",
                           clock + input + "set_output_delay -clock virtual -clock_fall 20 [all_outputs]\n",
                           clock + input + "set_output_delay -clock virtual 20 [all_outputs unknown]\n",
                           clock + input + "set_output_delay -clock virtual 20 [get_ports y\n",
                           "create_clock -name virtual -period 100 [get_ports a]\n" + input + output,
                           "create_clock -name virtual -period 100 -waveform {0 40}\n" + input + output}) {
    Sdc_probe pass(var);
    pass.read_sdc(write(text));
    pass.clock_qor_json();
    EXPECT_FALSE(pass.complete()) << text;
  }
}
}  // namespace

// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "pass_opentimer.hpp"

#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <map>

#include "attrs.hpp"
#include "gtest/gtest.h"
#include "node_util.hpp"
#include "rapidjson/document.h"

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

// A placed three-port memory (write port 0, read ports 1 and 2) timed with
// hand-built placement RC. Every addr/din/enable sink is a capture endpoint;
// the clock pins only carry clock-net RC.
class PhysicalMemory : public ::testing::Test {
protected:
  std::string        directory;
  hhds::GraphLibrary library;
  void               SetUp() override {
    directory = (std::filesystem::temp_directory_path() / "livehd-physical-memory-XXXXXX").string();
    ASSERT_NE(mkdtemp(directory.data()), nullptr);
    std::ofstream(directory + "/units.lib") << R"lib(library(units) {
      delay_model : table_lookup; time_unit : "1ns"; capacitive_load_unit (1,ff);
    })lib";
  }
  void                TearDown() override { std::filesystem::remove_all(directory); }
  // `shared_clock`: only port 0 has a clock pin; ports 1 and 2 have none.
  rapidjson::Document time(bool shared_clock, const std::string& name, bool critical = false) {
    namespace gu                        = livehd::graph_util;
    auto                             io = library.create_io(name);
    const std::map<std::string, int> widths{
        {"clk", 1},
        { "wa", 3},
        { "ra", 3},
        { "rb", 3},
        {"din", 4},
        { "en", 1}
    };
    unsigned port = 0;
    for (const auto* input : {"clk", "wa", "ra", "rb", "din", "en"}) {
      io->add_input(input, port++);
      io->set_bits(input, widths.at(input));
    }
    io->add_output("y", 0);
    io->set_bits("y", 4);
    io->add_output("z", 1);
    io->set_bits("z", 4);
    auto graph = io->create_graph();
    for (const auto& d : io->get_input_pin_decls()) {
      gu::set_bits(graph->get_input_pin(d.name), d.bits);
    }
    auto mem = gu::create_typed_node(*graph, Ntype_op::Memory);
    mem.set_name("ram");
    const auto constant = [&](unsigned pid, int value) {
      mem.create_sink_pin(pid).connect_driver(gu::create_const(*graph, *Dlop::create_integer(value)));
    };
    constant(1, 4);
    constant(9, 8);
    constant(6, 1);
    std::map<std::string, std::vector<std::string>> sinks;  // input bit -> memory sink pins
    const auto                                      connect = [&](unsigned pid, const std::string& input) {
      mem.create_sink_pin(pid).connect_driver(graph->get_input_pin(input));
      for (int bit = 0; bit < widths.at(input); ++bit) {
        sinks[widths.at(input) > 1 ? std::format("{}[{}]", input, bit) : input].push_back(std::format("ram:in_{}[{}]", pid, bit));
      }
    };
    for (unsigned p = 0; p < 3; ++p) {
      const auto base = p * Ntype::Memory_port_stride;
      connect(base, p == 0 ? "wa" : p == 1 ? "ra" : "rb");
      if (p == 0 || !shared_clock) {
        connect(base + 2, "clk");
      }
      if (p == 1) {
        constant(base + 4, 1);
      } else {
        connect(base + 4, "en");
      }
      constant(base + 7, 1);
      constant(base + 10, p != 0);
    }
    connect(3, "din");
    for (unsigned pid = 1; pid <= 2; ++pid) {
      auto q = mem.create_driver_pin(pid);
      gu::set_bits(q, 4);
      graph->get_output_pin(pid == 1 ? "y" : "z").connect_driver(q);
    }
    mem.attr(livehd::attrs::place).set(Ann_place(100, 100, 50, 50));
    auto rc      = std::make_shared<livehd::physical::Parasitics>();
    rc->identity = "hand-built";
    rc->cells.push_back({"ram", 100, 100, 0, "", 50, 50});
    for (const auto& [driver, pins] : sinks) {
      livehd::physical::Rc_net net{driver, {{driver, 1}}, {}};
      for (const auto& pin : pins) {
        net.edges.push_back({0, static_cast<unsigned>(net.nodes.size()), driver == "clk" ? 5000.0 : 10.0});
        net.nodes.push_back({pin, 1});
      }
      rc->nets.push_back(std::move(net));
    }
    Eprp_var var;
    var.graphs           = {graph};
    var.physical_rc      = rc;
    var.dict["files"]    = directory + "/units.lib";
    var.dict["targets"]  = "clk:10ns";
    var.dict["top"]      = name;
    var.dict["hier"]     = "false";
    var.dict["critical"] = critical ? "true" : "false";
    var.dict["qor"]      = directory + "/" + name + ".json";
    var.set_stage_labels(var.dict);
    Pass_opentimer::time_work(var);
    std::ifstream       file(directory + "/" + name + ".json");
    std::string         contents{std::istreambuf_iterator<char>(file), {}};
    rapidjson::Document json;
    json.Parse(contents.c_str());
    return json;
  }
  // Every addr/din/enable capture has a required time; clock pins have none.
  static void check(const rapidjson::Document& json) {
    ASSERT_FALSE(json.HasParseError());
    const auto& design = json["designs"][0];
    EXPECT_TRUE(design["constraints_complete"].GetBool());
    EXPECT_EQ(std::string_view(design["critical_pin"].GetString()).find("clock_pin"), std::string_view::npos)
        << design["critical_pin"].GetString();
    unsigned captures = 0;
    for (const auto& p : design["physical_pins"].GetArray()) {
      const std::string_view pin = p["pin"].GetString();
      if (pin.starts_with("ram:in_2[") || pin.starts_with("ram:in_18[") || pin.starts_with("ram:in_34[")) {
        EXPECT_FALSE(p.HasMember("required")) << pin;
      } else if (pin.starts_with("ram:in_")) {
        ++captures;
        EXPECT_TRUE(p.HasMember("required")) << pin;
      }
    }
    EXPECT_EQ(captures, 3 + 3 + 3 + 4 + 2);  // wa, ra, rb, din, port 0/2 enables
  }
};
TEST_F(PhysicalMemory, TargetOnlyClockConstrainsEveryCaptureButNotClockPins) { check(time(false, "per_port_clock")); }
TEST_F(PhysicalMemory, PortsWithoutAClockPinCaptureInTheMemoryClock) { check(time(true, "shared_clock")); }
TEST_F(PhysicalMemory, CriticalModeDoesNotTimeClockPinsAsCaptures) { check(time(false, "critical", true)); }
}  // namespace

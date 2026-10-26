// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "domino_bundle.hpp"
#include "inou/lefdef/row_access.hpp"
#include "pass/place/timing_circuit_fixture.hpp"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"

// Hand-computable fixtures for the provisional domino timing model: an inline
// Liberty (scalar tables, 1 ps / 1 fF units) and a schema-2 bundle written to
// TEST_TMPDIR with chosen coefficients.
namespace livehd::domplace::timing::test {
// FF: clk-to-Q 10 rise / 20 fall, setup 5 rise / 8 fall, CLK 3 fF.
// FFQN: a QN-only register (next state !D), QN clk-to-Q 12 rise / 22 fall.
// INV: 10 rise / 20 fall, A 2 fF. BUF: 5 rise / 7 fall, A 1 fF.
inline const char*       kLiberty = R"(
library(test) {time_unit:"1ps";capacitive_load_unit(1,ff);default_input_pin_cap:1;
  cell(INV) {area:1; pin(A){direction:input;capacitance:2;}
    pin(Y){direction:output;function:"!A";timing(){related_pin:A;timing_sense:negative_unate;
      cell_rise(scalar){values("10");}cell_fall(scalar){values("20");}
      rise_transition(scalar){values("3");}fall_transition(scalar){values("4");}}}}
  cell(BUF) {area:2; pin(A){direction:input;capacitance:1;}
    pin(Y){direction:output;function:A;timing(){related_pin:A;timing_sense:positive_unate;
      cell_rise(scalar){values("5");}cell_fall(scalar){values("7");}
      rise_transition(scalar){values("1");}fall_transition(scalar){values("2");}}}}
  cell(FF) {area:10; ff(IQ,IQN){clocked_on:CLK;next_state:D;}
    pin(CLK){direction:input;clock:true;capacitance:3;}
    pin(D){direction:input;capacitance:1;timing(){related_pin:CLK;timing_type:setup_rising;
      rise_constraint(scalar){values("5");}fall_constraint(scalar){values("8");}}}
    pin(Q){direction:output;function:IQ;max_capacitance:50;max_transition:40;
      timing(){related_pin:CLK;timing_type:rising_edge;timing_sense:non_unate;
      cell_rise(scalar){values("10");}cell_fall(scalar){values("20");}
      rise_transition(scalar){values("2");}fall_transition(scalar){values("3");}}}}
  cell(FFQN) {area:9; ff(IQN,IQNN){clocked_on:CLK;next_state:"!D";}
    pin(CLK){direction:input;clock:true;capacitance:3;}
    pin(D){direction:input;capacitance:1;timing(){related_pin:CLK;timing_type:setup_rising;
      rise_constraint(scalar){values("6");}fall_constraint(scalar){values("9");}}}
    pin(QN){direction:output;function:IQN;timing(){related_pin:CLK;timing_type:rising_edge;timing_sense:non_unate;
      cell_rise(scalar){values("12");}cell_fall(scalar){values("22");}
      rise_transition(scalar){values("2");}fall_transition(scalar){values("3");}}}}
})";
// One master with rails R0 and R1 in two variants: v0 on FF, v1 on FFQN.
inline const std::string kBase    = "__lhd_domino_00000000000000aa";
inline const std::string kDomino  = kBase + "_v0_t0";
inline const std::string kQn      = kBase + "_v1_t0";

struct Sidecar {
  std::map<std::string, std::optional<double>> stage{
      {        "setup_eval_ps",  3},
      { "uncertainty_first_ps",  1},
      {"uncertainty_second_ps",  2},
      {        "eval_delay_ps", 20},
      {         "precharge_ps", 20},
      {  "precharge_margin_ps",  5}
  };
  std::optional<double>                            rail_ff = 2, clock_ff = 4, fo4 = 10;
  std::array<std::optional<double>, 2>             setup_capture{5, 7};
  std::vector<std::pair<std::string, std::string>> instances;  // (state, master)

  // Write a bundle directory: the sidecar, an empty LEF and the stand-in
  // row-access convention.
  std::filesystem::path write(const std::string& name) const {
    const auto dir = std::filesystem::path(std::getenv("TEST_TMPDIR") ? std::getenv("TEST_TMPDIR") : "/tmp") / name;
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    std::ofstream(dir / kBundleLef) << "";
    std::ofstream(dir / kBundleAccess) << physical::Row_access_convention{}.identity();
    rapidjson::StringBuffer                    buffer;
    rapidjson::Writer<rapidjson::StringBuffer> w(buffer);
    const auto                                 real = [&](const char* key, std::optional<double> value) {
      w.Key(key);
      value ? w.Double(*value) : w.Null();
    };
    w.StartObject();
    w.Key("schema");
    w.Uint(kBundleSchema);
    w.Key("model_version");
    w.Uint(1);
    w.Key("producer_salt");
    w.String("0123456789abcdef");
    w.Key("stage_model");
    w.StartObject();
    w.Key("version");
    w.Uint(1);
    w.Key("clock");
    w.String("clk");
    for (const auto& [key, value] : stage) {
      real(key.c_str(), value);
    }
    w.EndObject();
    w.Key("units");
    w.StartObject();
    w.Key("time");
    w.String("ps");
    w.Key("dbu");
    w.Int64(1000);
    w.EndObject();
    w.Key("corner");
    w.StartObject();
    w.Key("library");
    w.String("test");
    w.EndObject();
    w.Key("fo4_ps");
    w.StartObject();
    real("value", fo4);
    w.EndObject();
    w.Key("reference");
    w.StartObject();
    w.Key("registers");
    w.StartArray();
    for (const auto& [reg, q] : {
             std::pair{  "FF",  "Q"},
             {"FFQN", "QN"}
    }) {
      w.StartObject();
      w.Key("name");
      w.String(reg);
      w.Key("pins");
      w.StartObject();
      w.Key("d");
      w.String("D");
      w.Key("clk");
      w.String("CLK");
      w.Key("q");
      w.String(q);
      w.EndObject();
      w.EndObject();
    }
    w.EndArray();
    w.EndObject();
    w.Key("masters");
    w.StartArray();
    w.StartObject();
    w.Key("base");
    w.String(kBase.c_str());
    w.Key("rails");
    w.StartArray();
    for (const auto* rail : {"R0", "R1"}) {
      w.StartObject();
      w.Key("pin");
      w.String(rail);
      w.EndObject();
    }
    w.EndArray();
    w.Key("variants");
    w.StartArray();
    for (size_t v = 0; v < 2; ++v) {
      w.StartObject();
      w.Key("name");
      w.String((v ? kQn : kDomino).c_str());
      w.Key("register");
      w.String(v ? "FFQN" : "FF");
      w.Key("width_dbu");
      w.Uint64(2000 + 500 * v);
      w.Key("halves");
      w.StartArray();
      w.String("horizontal");
      w.String("vertical");
      w.EndArray();
      real("a_footprint_um2", 4. + v);
      real("a_estimate_um2", 3.5 + v);
      w.Key("pin_caps");
      w.StartObject();
      real("rail_ff", rail_ff);
      real("clock_ff", clock_ff);
      w.EndObject();
      real("setup_capture_ps", setup_capture[v]);
      w.Key("drive");
      w.Null();
      w.EndObject();
    }
    w.EndArray();
    w.EndObject();
    w.EndArray();
    w.Key("instances");
    w.StartArray();
    for (uint32_t i = 0; i < instances.size(); ++i) {
      w.StartObject();
      w.Key("region");
      w.String("top");
      w.Key("state_index");
      w.Uint(i);
      w.Key("state");
      w.String(instances[i].first.c_str());
      w.Key("master");
      w.String(instances[i].second.c_str());
      w.EndObject();
    }
    w.EndArray();
    w.EndObject();
    std::ofstream(dir / kBundleSidecar) << buffer.GetString() << "\n";
    return dir;
  }
};
}  // namespace livehd::domplace::timing::test

// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
// Small timing-engine/SDC reproducer. No synthesis or placement is performed.
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>

#include "pass/opentimer/pass_opentimer.hpp"
#include "rapidjson/document.h"
#include "rapidjson/ostreamwrapper.h"
#include "rapidjson/writer.h"

class Timing_probe : public Pass_opentimer {
public:
  explicit Timing_probe(const Eprp_var& var) : Pass_opentimer(var) {}
  void run(const std::string& verilog, std::ostream& output, const std::set<std::string>& selected) {
    ensure_libs();
    timer.set_num_threads(1);
    timer.read_verilog(verilog).update_timing();
    read_sdc_spef();
    apply_case_analysis();
    timer.update_timing();
    rapidjson::OStreamWrapper                    stream(output);
    rapidjson::Writer<rapidjson::OStreamWrapper> w(stream);
    w.StartObject();
    w.Key("wns");
    const auto wns = timer.report_wns(ot::MAX);
    if (wns) {
      w.Double(*wns);
    } else {
      w.Null();
    }
    w.Key("tns");
    w.Double(endpoint_tns(ot::MAX));
    w.Key("pins");
    w.StartObject();
    for (const auto& [name, pin] : timer.pins()) {
      if (!selected.empty() && !selected.contains(name)) {
        continue;
      }
      w.Key(name.c_str());
      w.StartArray();
      for (auto split : {ot::MIN, ot::MAX}) {
        for (auto tran : {ot::RISE, ot::FALL}) {
          w.StartObject();
          const auto field = [&](const char* key, std::optional<float> value) {
            w.Key(key);
            if (value && std::isfinite(*value)) {
              w.Double(*value);
            } else {
              w.Null();
            }
          };
          field("arrival", pin.at(split, tran));
          field("required", pin.rat(split, tran));
          field("slew", pin.slew(split, tran));
          field("slack", pin.slack(split, tran));
          if (pin.net()) {
            field("load", timer.report_load(pin.net()->name(), split, tran));
            field("effective_load", pin.net()->effective_load(split, tran));
            w.Key("wire_elmore_fallback");
            w.Bool(pin.net()->elmore_fallback(split, tran));
          }
          w.EndObject();
        }
      }
      w.EndArray();
    }
    w.EndObject();
    w.EndObject();
  }
};

int main(int argc, char** argv) {
  try {
    if (argc != 2) {
      throw std::invalid_argument("usage: timing_probe config.json");
    }
    std::ifstream       input(argv[1]);
    std::string         contents{std::istreambuf_iterator<char>(input), {}};
    rapidjson::Document config;
    config.Parse(contents.c_str());
    if (config.HasParseError() || !config.IsObject()) {
      throw std::invalid_argument("invalid probe config");
    }
    std::string files  = config["liberty"].GetString();
    files             += std::string(",") + config["sdc"].GetString();
    if (config.HasMember("spef")) {
      files += std::string(",") + config["spef"].GetString();
    }
    Eprp_var var;
    var.dict = {
        {"files", files}
    };
    var.set_stage_labels(var.dict);
    Timing_probe          probe(var);
    std::ofstream         output(config["output"].GetString());
    std::set<std::string> selected;
    if (config.HasMember("pins")) {
      for (const auto& pin : config["pins"].GetArray()) {
        selected.insert(pin.GetString());
      }
    }
    probe.run(config["verilog"].GetString(), output, selected);
    return 0;
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}

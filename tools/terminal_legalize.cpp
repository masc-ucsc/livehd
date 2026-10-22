// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
// Replay the terminal legalizer on a saved final-GP DEF without rerunning GP.
#include "pass/place/terminal_legalize.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

#include "rapidjson/document.h"

int main(int argc, char** argv) {
  try {
    if (argc != 2) {
      throw std::invalid_argument("usage: terminal_legalize config.json");
    }
    std::ifstream       input(argv[1]);
    const std::string   text{std::istreambuf_iterator<char>(input), {}};
    rapidjson::Document config;
    config.Parse(text.c_str());
    if (config.HasParseError() || !config.IsObject()) {
      throw std::invalid_argument("invalid configuration JSON");
    }
    const auto string = [&](const char* key) -> std::string {
      if (!config.HasMember(key) || !config[key].IsString()) {
        throw std::invalid_argument(std::string("missing string: ") + key);
      }
      return config[key].GetString();
    };
    if (!config.HasMember("lefs") || !config["lefs"].IsArray()) {
      throw std::invalid_argument("missing lefs array");
    }
    std::vector<std::string> lefs;
    for (const auto& lef : config["lefs"].GetArray()) {
      if (!lef.IsString()) {
        throw std::invalid_argument("LEF paths must be strings");
      }
      lefs.emplace_back(lef.GetString());
    }
    auto tech = livehd::physical::read_lef(lefs);
    auto fp   = livehd::physical::read_def(string("def"));
    tech.dbu  = fp.dbu;
    for (auto& c : fp.components) {
      const auto& master = tech.macros.at(c.macro);
      c.place.width      = std::llround(master.width * fp.dbu);
      c.place.height     = std::llround(master.height * fp.dbu);
      if (c.place.orientation == Ann_place::Orientation::E || c.place.orientation == Ann_place::Orientation::W
          || c.place.orientation == Ann_place::Orientation::FE || c.place.orientation == Ann_place::Orientation::FW) {
        std::swap(c.place.width, c.place.height);
      }
    }
    livehd::placement::Terminal_legalize_options options;
    const auto                                   number = [&](const char* key, double& value) {
      if (config.HasMember(key)) {
        if (!config[key].IsNumber()) {
          throw std::invalid_argument(std::string("expected number: ") + key);
        }
        value = config[key].GetDouble();
      }
    };
    const auto integer = [&](const char* key, unsigned& value) {
      if (config.HasMember(key)) {
        if (!config[key].IsUint()) {
          throw std::invalid_argument(std::string("expected unsigned integer: ") + key);
        }
        value = config[key].GetUint();
      }
    };
    number("remainder_wire_weight", options.remainder_wire_weight);
    number("max_density", options.max_density);
    number("target_density", options.target_density);
    number("critical_fraction", options.critical_fraction);
    number("local_pair_fraction", options.local_pair_fraction);
    integer("bins_x", options.bins_x);
    integer("bins_y", options.bins_y);
    integer("padding_sites", options.padding_sites);
    integer("padding_max_extra_rows", options.padding_max_extra_rows);
    integer("critical_padding_sites", options.critical_padding_sites);
    integer("critical_fanin_depth", options.critical_fanin_depth);
    if (config.HasMember("joint_minimize_total")) {
      if (!config["joint_minimize_total"].IsBool()) {
        throw std::invalid_argument("expected boolean: joint_minimize_total");
      }
      options.joint_minimize_total = config["joint_minimize_total"].GetBool();
    }
    integer("candidates", options.candidates);
    integer("pair_move_sites", options.pair_move_sites);
    integer("pair_move_rows", options.pair_move_rows);
    integer("critical_pair_limit", options.critical_pair_limit);
    integer("local_pair_limit", options.local_pair_limit);
    integer("sat_conflicts", options.sat_conflicts);
    integer("sat_milliseconds", options.sat_milliseconds);
    if (config.HasMember("backend")) {
      options.backend = string("backend");
    }
    double halo_x = 5, halo_y = 5;
    number("macro_halo_x_um", halo_x);
    number("macro_halo_y_um", halo_y);
    if (!std::isfinite(halo_x) || !std::isfinite(halo_y) || halo_x < 0 || halo_y < 0) {
      throw std::invalid_argument("invalid macro halo");
    }
    options.macro_halo_x_dbu = std::llround(halo_x * fp.dbu);
    options.macro_halo_y_dbu = std::llround(halo_y * fp.dbu);
    if (!config.HasMember("bins_x") && !config.HasMember("bins_y")) {
      livehd::placement::fit_terminal_legalize_grid(tech, fp, options);
    }
    livehd::timing::Feedback timing;
    timing.time_unit_ps = 1;
    // Each line: exact instance:pin (or instance/pin), slack in ps, clock 0/1.
    if (config.HasMember("timing")) {
      std::ifstream feedback(string("timing"));
      if (!feedback) {
        throw std::invalid_argument("cannot read timing feedback");
      }
      std::string line;
      while (std::getline(feedback, line)) {
        if (line.empty() || line.front() == '#') {
          continue;
        }
        std::istringstream row(line);
        std::string        pin;
        double             slack;
        int                clock;
        if (!(row >> pin >> slack >> clock) || !std::isfinite(slack) || (clock != 0 && clock != 1)) {
          throw std::invalid_argument("invalid timing feedback line");
        }
        auto& p = timing.pins[pin];
        p.slack = slack;
        p.clock = clock;
      }
    }
    options.prects = livehd::placement::prects_library(config.HasMember("liberty") ? string("liberty") : "",
                                                       config.HasMember("control_ports") ? string("control_ports") : "");
    const auto output = string("output");
    std::filesystem::create_directories(output);
    const auto    excluded = livehd::placement::prects_networks(tech, fp, options.prects, timing);
    std::ofstream mask(output + "/prects_ignored_nets.txt");
    for (size_t n = 0; n < fp.nets.size(); ++n) {
      if (excluded.ignored[n]) {
        mask << fp.nets[n].name << '\n';
      }
    }
    mask.close();
    if (!mask) {
      throw std::runtime_error("cannot write pre-CTS net exclusion report");
    }
    const auto report = livehd::placement::terminal_legalize(tech, fp, timing, options);
    livehd::physical::write_def(fp, output + "/out.def");
    livehd::placement::write_terminal_legalize_report(report, fp, options, output + "/terminal_legalize");
    std::cout << "Legalized " << report.cells.size() << " cells; " << report.critical_cells << " critical; "
              << report.critical_pairs << " critical pairs; " << report.local_pairs << " local pairs; " << report.crossed_cells
              << " crossed bins; " << report.seconds << " seconds\n";
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}

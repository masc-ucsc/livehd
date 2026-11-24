// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <stdexcept>

#include "inou/lefdef/routing_geometry.hpp"
#include "pass/place/legroute.hpp"

int main(int argc, char** argv) {
  namespace ph = livehd::physical;
  namespace pl = livehd::placement;
  if (argc < 8) {
    std::cerr << "usage: frame_trees frame.def native-gp.def timing.tsv trees.tsv report.json tech.lef cells.lef ...\n";
    return 2;
  }
  try {
    const auto                        tech  = ph::read_lef({argv + 6, argv + argc});
    auto                              frame = ph::read_def_snapshot(argv[1]);
    const auto                        gp    = ph::read_def_snapshot(argv[2]);
    std::map<std::string, ph::Status> original;
    for (const auto& cell : gp.components) {
      original.emplace(cell.name, cell.place.status);
    }
    // Use the original classification for legroute's tile sizing (twice the widest
    // flop). This diagnostic is read-only and measures the input XY of every cell.
    for (auto& cell : frame.components) {
      cell.place.status = original.at(cell.name);
    }
    livehd::timing::Feedback timing;
    timing.time_unit_ps = 1;
    std::ifstream ranks(argv[3]);
    std::string   line;
    std::getline(ranks, line);
    std::string label;
    double      slack;
    bool        clock;
    while (ranks >> std::quoted(label) >> slack >> clock) {
      auto& pin = timing.pins[label];
      pin.slack = slack;
      pin.clock = clock;
    }
    if (!ranks.eof() || timing.pins.empty()) {
      throw std::runtime_error("cannot read saved pin timing");
    }
    pl::Legroute_power infrastructure;
    // The DEF special nets already account for PG once. Add occupied regular
    // wires and via landings once, and remove those completed nets from demand.
    std::erase_if(frame.nets, [&](const auto& net) {
      if (net.wires.empty()) {
        return false;
      }
      for (const auto& wire : net.wires) {
        if (wire.type != "FIXED") {
          throw std::runtime_error("expected complete FIXED infrastructure");
        }
      }
      auto shapes = ph::route_geometry(tech, frame, net);
      infrastructure.obstacles.insert(infrastructure.obstacles.end(), shapes.begin(), shapes.end());
      return true;
    });
    const auto&          profile = ph::tech_profile("asap7");
    pl::Legroute_options options;
    options.capacity_share = .75;
    options.route_escape   = true;
    options.global_pattern = true;
    options.clock_charge   = false;
    std::ofstream trees(argv[4]);
    const auto    result = pl::legroute_plan(tech, frame, timing, options, infrastructure, &profile, 2000, nullptr, &trees);
    if (!trees || result.pg_missing) {
      throw std::runtime_error("incomplete frame or missing tree output");
    }
    std::ofstream report(argv[5]);
    if (!(report << pl::legroute_plan_json(result) << '\n')) {
      throw std::runtime_error("cannot write capacity report");
    }
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}

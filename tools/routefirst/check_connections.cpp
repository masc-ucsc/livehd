// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include <algorithm>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>

#include "inou/lefdef/routing_connectivity.hpp"

// Emit one independent pin-to-wire connectivity result per multi-terminal signal.
int main(int argc, char** argv) {
  namespace ph = livehd::physical;
  if (argc < 5) {
    std::cerr << "usage: check_connections routed.def results.tsv tech.lef cells.lef ...\n";
    return 2;
  }
  try {
    auto                                                   tech = ph::read_lef({argv + 3, argv + argc});
    // A cell pin is one electrical terminal. An enclosed cut internal to that pin
    // adds no external contact beyond its two already-equivalent metal landings.
    std::map<std::string, std::set<std::set<std::string>>> cut_stacks;
    for (const auto& [name, via] : tech.vias) {
      std::set<std::string> metals, cuts;
      for (const auto& shape : via.shapes) {
        (tech.layers.contains(shape.layer) ? metals : cuts).insert(shape.layer);
      }
      if (metals.size() == 2 && cuts.size() == 1 && tech.cut_layers.contains(*cuts.begin())) {
        cut_stacks[*cuts.begin()].insert(metals);
      }
    }
    for (auto& [name, macro] : tech.macros) {
      for (auto& [pin_name, pin] : macro.pins) {
        const auto original = pin.shapes;
        std::erase_if(pin.shapes, [&](const auto& shape) {
          const auto stacks = cut_stacks.find(shape.layer);
          if (stacks == cut_stacks.end() || shape.rect.empty()) {
            return false;
          }
          std::set<std::string> landings;
          for (const auto& rect : original) {
            if (tech.layers.contains(rect.layer) && rect.rect.contains(shape.rect)) {
              landings.insert(rect.layer);
            }
          }
          return stacks->second.contains(landings);
        });
      }
    }
    const auto                 frame = ph::read_def(argv[1]);
    const ph::Route_components cells(frame);
    std::ofstream              out(argv[2]);
    out << "net\tterminals\tconnected\tcomponents\treason\n";
    for (const auto& net : frame.nets) {
      if (net.connections.size() < 2) {
        continue;
      }
      const auto result = ph::check_route_connectivity(tech, frame, net, &cells);
      out << net.name << '\t' << net.connections.size() << '\t' << result.connected << '\t' << result.components << '\t'
          << result.reason << '\n';
    }
    if (!out) {
      throw std::runtime_error("cannot write geometry check results");
    }
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}

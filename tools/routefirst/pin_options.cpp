// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <unordered_map>

#include "inou/lefdef/pin_access.hpp"
#include "inou/lefdef/routing_occupancy.hpp"

namespace ph = livehd::physical;

int main(int argc, char** argv) {
  const bool filter = argc > 2 && std::string_view(argv[1]) == "--net";
  const int  first  = filter ? 3 : 1;
  if (argc < first + 4) {
    std::cerr << "usage: pin_options [--net name] input.def pin-options.tsv tech.lef cells.lef ...\n";
    return 2;
  }
  try {
    const auto                                            tech     = ph::read_lef({argv + first + 2, argv + argc});
    const auto                                            fp       = ph::read_def_snapshot(argv[first]);
    const auto&                                           policy   = ph::tech_profile("asap7").route;
    const auto                                            occupied = ph::routing_occupancy(tech, fp, fp.dbu);
    const ph::Signal_metal_rules                          rules(tech, fp);
    std::unordered_map<std::string, const ph::Component*> cells;
    for (const auto& cell : fp.components) {
      cells.emplace(cell.name.str(), &cell);
    }
    std::ofstream out(argv[first + 1]);
    bool          found = false;
    for (const auto& net : fp.nets) {
      if (filter && net.name != argv[2]) {
        continue;
      }
      found = true;
      for (const auto& terminal : net.connections) {
        if (terminal.instance == "PIN") {
          continue;
        }
        const auto& cell = *cells.at(terminal.instance.str());
        const auto  access
            = ph::enumerate_pin_access(tech, fp, cell, terminal.pin.str(), occupied, rules, 4096, net.name.str(), policy.access);
        if (filter) {
          for (const auto& [reason, count] : access.rejected) {
            std::cerr << cell.name << '\t' << terminal.pin << '\t' << reason << '\t' << count << '\n';
          }
        }
        out << cell.name.str() << '\t' << terminal.pin.str() << '\t' << net.name.str() << '\t' << access.exhausted;
        for (const auto& p : access.points) {
          const auto& b = p.escape.rect;
          out << '\t' << p.x << ',' << p.y << ',' << b.xl << ',' << b.xh;
        }
        out << '\n';
      }
    }
    if (filter && !found) {
      throw std::runtime_error("requested net is absent from the frame");
    }
    if (!out) {
      throw std::runtime_error("failed to write pin access options");
    }
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}

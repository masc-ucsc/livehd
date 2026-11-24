// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>

#include "inou/lefdef/routing_geometry.hpp"

// Read the topology after removing the candidate signal nets' M1 and V1.
// Each row carries real conductor geometry, including M2 landings of V2.
int main(int argc, char** argv) {
  namespace ph = livehd::physical;
  try {
    const bool all_layers = argc > 1 && std::string_view(argv[1]) == "--all-layers";
    const int  first      = all_layers ? 2 : 1;
    if (argc < first + 4) {
      throw std::invalid_argument("usage: retained_metal [--all-layers] layout.def metal.tsv tech.lef cells.lef ...");
    }
    const auto    tech  = ph::read_lef({argv + first + 2, argv + argc});
    const auto    frame = ph::read_def_snapshot(argv[first]);
    std::ofstream out(argv[first + 1]);
    out << std::setprecision(std::numeric_limits<double>::max_digits10);
    for (const auto& net : frame.nets) {
      ph::visit_route_geometry(tech, frame, net, [&](const ph::Shape& s) {
        if (all_layers || s.layer == "M2") {
          out << net.name;
          if (all_layers) {
            out << '\t' << s.layer;
          }
          out << '\t' << s.rect.xl << '\t' << s.rect.yl << '\t' << s.rect.xh << '\t' << s.rect.yh << '\n';
        }
      });
    }
    if (!out) {
      throw std::runtime_error("cannot write retained route geometry");
    }
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}

// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
// Routability oracle of place.area=auto on a placed DEF: measurement for comparing
// placements (legroute global route with measure_only, RUDY fallback). Prints one JSON line.
// usage: area_fit_route [--oracle legroute|rudy] [--net-pops N] [--pattern] placed.def lef...
#include <cstdio>
#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "inou/lefdef/physical.hpp"
#include "pass/place/area_fit.hpp"

int main(int argc, char** argv) {
  try {
    livehd::placement::Area_fit_options options;
    std::vector<std::string>            files;
    for (int i = 1; i < argc; ++i) {
      const std::string arg = argv[i];
      if (arg == "--oracle" && i + 1 < argc) {
        options.oracle = argv[++i];
      } else if (arg == "--pattern") {
        options.pattern = true;
      } else if (arg == "--net-pops" && i + 1 < argc) {
        options.net_pops = static_cast<unsigned>(std::stoul(argv[++i]));
      } else {
        files.push_back(arg);
      }
    }
    if (files.size() < 2) {
      std::cerr << "usage: area_fit_route [--oracle legroute|rudy] [--net-pops N] [--pattern] placed.def lef...\n";
      return 2;
    }
    livehd::placement::validate_area_fit_options(options);
    const auto   tech  = livehd::physical::read_lef({files.begin() + 1, files.end()});
    auto         fp    = livehd::physical::read_def(files.front());
    const auto   core  = livehd::placement::area_fit_core(tech, fp);
    const auto   areas = livehd::placement::area_fit_areas(tech, fp);
    const auto   route = livehd::placement::area_fit_route(tech, std::move(fp), options);
    const double dbu   = double(livehd::physical::read_def(files.front()).dbu);
    std::printf("{\"core_area_um2\":%.3f,\"cell_area_um2\":%.3f,\"macro_area_um2\":%.3f,\"route\":%s}\n",
                (core.rect.xh - core.rect.xl) * (core.rect.yh - core.rect.yl) / dbu / dbu,
                areas.cells / dbu / dbu,
                areas.macros / dbu / dbu,
                livehd::placement::area_fit_route_json(route).c_str());
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "area_fit_route: " << e.what() << '\n';
    return 1;
  }
}

// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
// Geometry-only diagnostic: output is for independent checking, not flow handoff.
#include <algorithm>
#include <charconv>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <numeric>
#include <stdexcept>

#include "inou/lefdef/routing_connectivity.hpp"
#include "pass/place/routed_rc.hpp"
#include "pass/place/satlegalize.hpp"
#include "pass/place/signal_router.hpp"

int main(int argc, char** argv) {
  namespace ph = livehd::physical;
  try {
    std::vector<std::string> lefs;
    std::string              def, net_name, output, rc_file, liberty, driver, rc_output;
    bool                     check_only = false, straight = false, probe = false, clear_routes = false;
    size_t                   patterns  = 10000;
    int                      budget_ms = 1000;
    for (int i = 1; i < argc; ++i) {
      const std::string key = argv[i];
      if (++i == argc) {
        throw std::invalid_argument("missing argument value");
      }
      if (key == "--lef") {
        lefs.emplace_back(argv[i]);
      } else if (key == "--def") {
        def = argv[i];
      } else if (key == "--net") {
        net_name = argv[i];
      } else if (key == "--check-only" && std::string(argv[i]) == "true") {
        check_only = true;
      } else if (key == "--probe" && std::string(argv[i]) == "true") {
        probe = true;
      } else if (key == "--clear-routes" && std::string(argv[i]) == "true") {
        clear_routes = true;
      } else if (key == "--straight" && std::string(argv[i]) == "true") {
        straight = true;
      } else if (key == "--patterns" || key == "--budget-ms") {
        const std::string value  = argv[i];
        int               parsed = 0;
        const auto        result = std::from_chars(value.data(), value.data() + value.size(), parsed);
        if (result.ec != std::errc{} || result.ptr != value.data() + value.size() || parsed <= 0
            || (key == "--budget-ms" && parsed > 3600000)) {
          throw std::invalid_argument("invalid positive diagnostic search budget");
        }
        if (key == "--patterns") {
          patterns = parsed;
        } else {
          budget_ms = parsed;
        }
      } else if (key == "--rc-file") {
        rc_file = argv[i];
      } else if (key == "--liberty") {
        liberty = argv[i];
      } else if (key == "--driver") {
        driver = argv[i];
      } else if (key == "--diagnostic-rc") {
        rc_output = argv[i];
      } else if (key == "--diagnostic-def") {
        output = argv[i];
      } else {
        throw std::invalid_argument("unknown argument: " + key);
      }
    }
    if (lefs.empty() || def.empty() || net_name.empty() || (!check_only && !probe && output.empty()) || output == def) {
      throw std::invalid_argument(
          "requires --lef, --def, --net, and --probe true, --check-only true, or a separate --diagnostic-def output");
    }
    if ((probe && (check_only || !output.empty())) || (clear_routes && !probe)) {
      throw std::invalid_argument("probe is non-emitting; clear-routes requires probe mode");
    }
    const bool rc_requested = !rc_file.empty() || !liberty.empty() || !driver.empty() || !rc_output.empty();
    if (rc_requested
        && (check_only || probe || rc_file.empty() || liberty.empty() || driver.empty() || rc_output.empty() || rc_output == def
            || rc_output == output || rc_output == rc_file || rc_output == liberty)) {
      throw std::invalid_argument("RC diagnostic requires --rc-file, --liberty, --driver and a separate --diagnostic-rc output");
    }
    std::vector<std::string> inputs = lefs;
    inputs.push_back(def);
    if (!rc_file.empty()) {
      inputs.push_back(rc_file);
    }
    if (!liberty.empty()) {
      inputs.push_back(liberty);
    }
    for (const auto& destination : {output, rc_output}) {
      if (destination.empty()) {
        continue;
      }
      const auto path = std::filesystem::weakly_canonical(destination);
      for (const auto& input : inputs) {
        if (path == std::filesystem::weakly_canonical(input)) {
          throw std::invalid_argument("diagnostic output aliases an input");
        }
      }
    }
    if (rc_requested && std::filesystem::weakly_canonical(output) == std::filesystem::weakly_canonical(rc_output)) {
      throw std::invalid_argument("diagnostic DEF and RC outputs must be distinct");
    }
    auto tech = ph::read_lef(lefs);
    auto fp   = ph::read_def(def);
    if (clear_routes) {
      for (auto& n : fp.nets) {
        n.wires.clear();
      }
      for (auto& n : fp.special_nets) {
        n.wires.clear();
      }
    }
    auto net = std::find_if(fp.nets.begin(), fp.nets.end(), [&](const auto& n) { return n.name == net_name; });
    if (net == fp.nets.end()) {
      throw std::invalid_argument("unknown net");
    }
    if (check_only) {
      const auto checked = ph::check_route_connectivity(tech, fp, *net);
      std::cout << (checked.connected ? "connected" : checked.reason) << "; components=" << checked.components << '\n';
      return checked.connected ? 0 : 2;
    }
    const auto                       occupancy = ph::routing_occupancy(tech, fp, fp.dbu);
    livehd::placement::Signal_router router(tech, fp, occupancy);
    const auto                       deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(budget_ms);
    if (probe) {
      const auto result = router.probe_route(*net,
                                             {
                                                 {10000, patterns, deadline},
                                                 {},
                                                 false,
                                                 straight
      });
      std::cout << "routable=" << result.routable << " timed_out=" << result.timed_out << " access=" << result.access_examined
                << " patterns=" << result.patterns_examined << " reason=" << result.reason << '\n';
      return result.routable ? 0 : 2;
    }
    livehd::placement::Signal_route_proposals result;
    if (straight) {
      if (net->connections.size() != 2) {
        throw std::invalid_argument("straight diagnostic requires two pins");
      }
      std::map<std::string, size_t> cells;
      std::vector<size_t>           obstacles(fp.components.size());
      std::iota(obstacles.begin(), obstacles.end(), 0);
      for (size_t i = 0; i < fp.components.size(); ++i) {
        cells.emplace(fp.components[i].name, i);
      }
      const auto& a = net->connections[0];
      const auto& b = net->connections[1];
      if (a.instance == "PIN" || b.instance == "PIN") {
        throw std::invalid_argument("straight diagnostic requires cell pins");
      }
      const livehd::placement::Satlegalize_alignment pair{cells.at(a.instance), cells.at(b.instance), a.pin, b.pin};
      ph::Shape                                      corridor;
      if (!livehd::placement::satlegalize_aligned(tech,
                                                  fp,
                                                  pair,
                                                  fp.components[pair.first].place,
                                                  fp.components[pair.second].place,
                                                  obstacles,
                                                  &corridor)) {
        throw std::invalid_argument("no SAT straight-track witness");
      }
      const auto print_shape = [](const ph::Shape& s) {
        std::cerr << s.layer << " (" << s.rect.xl << ',' << s.rect.yl << ")-(" << s.rect.xh << ',' << s.rect.yh << ')';
      };
      std::cerr << "SAT corridor ";
      print_shape(corridor);
      std::cerr << '\n';
      result = router.try_route(*net,
                                {
                                    {10000, patterns, deadline},
                                    corridor,
                                    false
      });
      if (result.candidates.empty()) {
        ph::Signal_metal_rules rules(tech, fp);
        if (const auto clearance = rules.clearance(corridor.layer)) {
          for (auto id : occupancy.query(corridor.layer, corridor.rect, *clearance, net->name)) {
            const auto& obstacle = occupancy.at(id);
            const auto  check    = rules.spacing(corridor, obstacle.shape);
            if (!check.legal) {
              std::cerr << "conflict owner=" << (obstacle.net.empty() ? "<unowned>" : obstacle.net) << " ";
              print_shape(obstacle.shape);
              std::cerr << ": " << check.reason << '\n';
            }
          }
        }
      }
    } else {
      result = router.try_route(*net,
                                {
                                    {10000, patterns, deadline},
                                    {},
                                    false
      });
    }
    if (result.candidates.empty()) {
      std::cerr << "no geometry: " << result.reason << '\n';
      return 2;
    }
    const auto& geometry = result.candidates.front();
    livehd::placement::apply_signal_route_rules(fp, geometry);
    net->wires.push_back({"FIXED", geometry.paths});
    const auto connectivity = ph::check_route_connectivity(tech, fp, *net);
    if (!connectivity.connected) {
      throw std::runtime_error(connectivity.reason);
    }
    if (rc_requested) {
      const auto                                   model = livehd::placement::technology_route_rc(tech, liberty, rc_file);
      std::vector<livehd::placement::Route_rc_pin> pins;
      for (size_t i = 0; i < net->connections.size(); ++i) {
        const auto& connection = net->connections[i];
        const auto& point      = geometry.endpoints.at(i);
        pins.push_back({connection.instance + ":" + connection.pin, point.pin_layer, point.x, point.y});
      }
      ph::Parasitics parasitics;
      parasitics.identity    = "diagnostic:" + net_name;
      parasitics.approximate = true;
      parasitics.nets.push_back(livehd::placement::routed_rc(tech, fp, *net, driver, pins, model));
      std::ofstream rc_out(rc_output);
      if (!(rc_out << ph::serialize_parasitics(parasitics) << '\n')) {
        throw std::runtime_error("cannot write diagnostic RC");
      }
    }
    ph::write_def(fp, output);
    ph::verify_def_roundtrip(fp, ph::read_def(output));
    std::cout << "Diagnostic geometry only; timing/capacity not accepted. " << geometry.wire_length_um << " um, " << geometry.vias
              << " vias; " << result.patterns_examined << " patterns.\n";
  } catch (const std::exception& e) {
    std::cerr << "signal_route: " << e.what() << '\n';
    return 1;
  }
}

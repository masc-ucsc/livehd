// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
// Builds the native clock tree on an already placed DEF, without running the flow: the planner, its
// handoff report (pcrs_clock.json) and the buffered DEF (checkpoint.def) for tools/clock_timing_report.
//   clock_probe --def placed.def --liberty cells.lib --sdc constraint.sdc --out DIR --wire-rc setRC.tcl
//               --lef tech.lef --lef cells.lef ... [--layers auto|M6-M7|none] [--max-slew PS] [--fanout N]
//               [--balance | --balance-verified] [--timer] [--trunk-steps N] [--leaf-steps N]
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>

#include "core/file_utils.hpp"
#include "core/metadata.hpp"
#include "pass/place/legroute.hpp"
#include "inou/lefdef/mapped_graph.hpp"
#include "pass/place/placement_technology.hpp"
#include "pass/place/repair.hpp"
#include "pass/place/technology_rc.hpp"
#include "pass/timing/physical_rank.hpp"

int main(int argc, char** argv) {
  using namespace livehd;
  try {
    placement::Options options;
    std::string        def, sdc, root = "clk";
    bool               use_timer = false, run_legroute = false, legalize_clock = false;
    auto route_pops = options.legroute_plan.route_net_pops;
    std::vector<std::string> macro_libraries;
    options.clock_tree           = true;
    for (int i = 1; i < argc; ++i) {
      const std::string_view option = argv[i];
      if (option == "--balance" || option == "--balance-verified") {
        options.clock_balance            = true;
        options.clock_balance_unverified = option == "--balance";
        continue;
      }
      if (option == "--legalize-clock") {
        legalize_clock = true;
        continue;
      }
      if (option == "--legroute") {
        run_legroute = true;
        continue;
      }
      if (option == "--timer") {
        use_timer = true;
        continue;
      }
      if (++i >= argc) {
        throw std::invalid_argument("missing argument for " + std::string(option));
      }
      const std::string value = argv[i];
      if (option == "--def") {
        def = value;
      } else if (option == "--liberty") {
        options.liberty = value;
      } else if (option == "--metadata") {
        options.metadata = std::make_shared<const metadata::Bundle>(metadata::Bundle::read(value));
      } else if (option == "--macro-liberty") {
        macro_libraries.push_back(value);
      } else if (option == "--sdc") {
        sdc = value;
      } else if (option == "--out") {
        options.output_dir = value;
      } else if (option == "--wire-rc") {
        options.tech_rc_file = value;
      } else if (option == "--lef") {
        options.lefs.push_back(value);
      } else if (option == "--layers") {
        options.clock_layers = value == "none" ? "" : value;
      } else if (option == "--max-slew") {
        options.clock_max_slew_ps = std::stod(value);
      } else if (option == "--legroute-pops") {
        route_pops = std::stoul(value);
      } else if (option == "--fanout") {
        options.clock_leaf_fanout = std::stoul(value);
      } else if (option == "--trunk-steps") {
        options.clock_trunk_drive_steps = std::stoul(value);
      } else if (option == "--leaf-steps") {
        options.clock_leaf_drive_steps = std::stoul(value);
      } else if (option == "--clock-port") {
        root = value;
      } else {
        throw std::invalid_argument("unknown option " + std::string(option));
      }
    }
    if (def.empty() || sdc.empty() || options.liberty.empty() || options.output_dir.empty() || options.lefs.empty()) {
      throw std::invalid_argument(
          "usage: clock_probe --def F --liberty F --sdc F --out DIR --wire-rc F --lef F... [--layers L] [--max-slew PS] [--fanout "
          "N]");
    }
    std::filesystem::create_directories(options.output_dir);
    const auto&       tech = options.technology();
    placement::Result result;
    auto&             fp = result.floorplan;
    fp                   = physical::read_def_snapshot(def);
    if (run_legroute) {
      placement::Legroute_options plan;
      plan.global_rounds = 0;
      plan.global_pattern = true;
      const auto* profile = physical::detect_tech_profile(tech.lef, physical::Tech_policy::power_grid);
      const auto route = placement::legroute_plan(tech.lef, fp, {}, plan, {}, profile, route_pops);
      std::ofstream out(options.output_dir + "/legroute.json");
      out << placement::legroute_plan_json(route) << '\n';
      if (!out) {
        throw std::runtime_error("cannot write legroute report");
      }
    }
    hhds::GraphLibrary library;
    result.graph  = physical::mapped_graph(library, fp, tech.lef);
    result.prects = placement::prects_library(tech.cells);
    result.prects.clock_ports.insert(root);
    for (const auto& terminal : fp.terminals) {
      result.pin_locations[terminal.name] = {terminal.x, terminal.y};
    }
    for (const auto& cell : fp.components) {
      const auto& master = tech.lef.macros.at(cell.macro);
      for (const auto& [name, pin] : master.pins) {
        if (pin.use == "POWER" || pin.use == "GROUND") {
          continue;
        }
        const auto offset = placement::placement_pin_offset(master, name, cell.place.orientation, fp.dbu);
        result.pin_locations[physical::connection_label({cell.name, name})]
            = {cell.place.x + offset.first, cell.place.y + offset.second};
      }
    }
    const auto text = file_utils::read_file(sdc);
    if (!text) {
      throw std::runtime_error("cannot read " + sdc);
    }
    // With the timer, balancing is accepted or rejected by timing as in the flow.
    std::unique_ptr<timing::fast::Physical_ranker> timer;
    if (use_timer || (options.clock_balance && !options.clock_balance_unverified)) {
      timing::fast::Physical_ranker::Options ranker;
      ranker.liberty_texts       = {tech.cells.contents};
      for (const auto& path : macro_libraries) {
        auto content = file_utils::read_file(path);
        if (!content) {
          throw std::runtime_error("cannot read " + path);
        }
        ranker.liberty_texts.front() += "\n" + *content;
      }
      ranker.sdc_text            = *text;
      ranker.default_constraints = true;
      timer                      = std::make_unique<timing::fast::Physical_ranker>(std::move(ranker));
    }
    const auto buffers = placement::build_clock_tree(result, options, macro_libraries, *text, std::move(timer));
    if (legalize_clock && !buffers.empty()) {
      // Reserve the accepted placement, including any gate moves from the clock builder.
      // Newly seeded clock buffers occupy free row sites in deterministic netlist order.
      auto components = std::move(fp.components);
      for (const auto& cell : components) {
        if (!buffers.contains(cell.name.str())) {
          fp.components.push_back(cell);
        }
      }
      auto rows = placement::unoccupied_rows(tech.lef, fp);
      fp.components = std::move(components);
      for (auto& cell : fp.components) {
        if (!buffers.contains(cell.name.str())) {
          continue;
        }
        const auto& master = tech.lef.macros.at(cell.macro);
        auto pose = placement::closest_free_site(fp, rows, master, cell.place.x, cell.place.y);
        if (!pose) {
          throw std::runtime_error("no free row site for clock buffer " + cell.name.str());
        }
        cell.place = *pose;
        const auto rect = physical::placed_rect(master, cell.place, fp.dbu,
            {-master.origin_x, -master.origin_y, master.width-master.origin_x, master.height-master.origin_y});
        placement::reserve_gaps(rows, rect);
        for (const auto& [name, pin] : master.pins) {
          const auto offset = placement::placement_pin_offset(master, name, cell.place.orientation, fp.dbu);
          result.pin_locations[physical::connection_label({cell.name, name})] =
              {cell.place.x + offset.first, cell.place.y + offset.second};
        }
      }
      placement::record_final_clock_leaf_geometry(result, options.output_dir);
    }
    physical::write_def(fp, options.output_dir + "/checkpoint.def");
    std::cout << "physical clock buffer instances inserted: " << buffers.size() << "\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "clock_probe: " << error.what() << '\n';
    return 1;
  }
}

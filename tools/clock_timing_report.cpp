// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
// A native timing report of a saved post-CTS DEF. When no parasitics are
// supplied, the wires have zero RC and the Liberty pin loads still apply.
#include <iostream>
#include <stdexcept>

#include "clock_spef.hpp"
#include "core/file_utils.hpp"
#include "inou/lefdef/mapped_graph.hpp"
#include "pass/place/technology_rc.hpp"
#include "pass/timing/report.hpp"

namespace {
std::string read(const std::string& path) {
  auto data = livehd::file_utils::read_file(path);
  if (!data) {
    throw std::runtime_error("cannot read " + path);
  }
  return std::move(*data);
}
}  // namespace

int main(int argc, char** argv) {
  try {
    std::string                         def, sdc, rc, spef, wire_rc, main_liberty, clock_layers;
    std::vector<std::string>            lefs;
    bool                                use_def_clock_nets = false;
    livehd::timing::fast::Report_inputs inputs;
    inputs.corner_name         = "typ";
    inputs.time_unit_ps        = 1;
    inputs.physical_clock_tree = true;
    for (int i = 1; i < argc; ++i) {
      const std::string_view option = argv[i];
      if (option == "--use-def-clock-nets") {
        use_def_clock_nets = true;
        continue;
      }
      if (++i >= argc) {
        throw std::invalid_argument("missing argument for " + std::string(option));
      }
      if (option == "--def") {
        def = argv[i];
      } else if (option == "--lef") {
        lefs.emplace_back(argv[i]);
      } else if (option == "--liberty") {
        if (main_liberty.empty()) {
          main_liberty = argv[i];
        }
        inputs.liberty_text += read(argv[i]) + "\n";
      } else if (option == "--corner") {
        inputs.corner_name = argv[i];
      } else if (option == "--sdc") {
        sdc = argv[i];
      } else if (option == "--wire-rc") {
        wire_rc = argv[i];
      } else if (option == "--clock-layers") {
        clock_layers = argv[i];
      } else if (option == "--spef") {
        spef = argv[i];
      } else if (option == "--parasitics") {
        rc = argv[i];
      } else {
        throw std::invalid_argument("unknown option " + std::string(option));
      }
    }
    if (def.empty() || lefs.empty() || inputs.liberty_text.empty() || sdc.empty()) {
      throw std::invalid_argument(
          "usage: clock_timing_report --def FILE --lef FILE... --liberty FILE... --sdc FILE "
          "[--corner NAME] [--use-def-clock-nets] [--parasitics LIVEHD_RC | --spef FILE | --wire-rc PLATFORM_RC [--clock-layers "
          "auto|M6-M7]]");
    }
    if (!clock_layers.empty() && wire_rc.empty()) {
      throw std::invalid_argument("--clock-layers requires --wire-rc");
    }
    inputs.sdc_text = read(sdc);
    auto       fp   = livehd::physical::read_def_snapshot(def);
    const auto tech = livehd::physical::read_lef(lefs);
    // Physical-only spacers, well taps and endcaps carry only supplies. Their absence from Liberty
    // is expected in a routed DEF; keep every component with a signal pin.
    std::erase_if(fp.components, [&](const auto& cell) {
      const auto& m = tech.macros.at(cell.macro);
      return (m.klass == "CORE SPACER" || m.klass == "CORE WELLTAP" || m.klass.starts_with("ENDCAP"))
             && std::ranges::all_of(m.pins, [](const auto& p) { return p.second.use == "POWER" || p.second.use == "GROUND"; });
    });
    hhds::GraphLibrary library;
    auto               graph = livehd::physical::mapped_graph(library, fp, tech, nullptr, use_def_clock_nets);
    std::shared_ptr<const livehd::physical::Parasitics> parasitics;
    if (int(!rc.empty()) + int(!spef.empty()) + int(!wire_rc.empty()) > 1) {
      throw std::invalid_argument("select one parasitic input");
    }
    if (!wire_rc.empty()) {
      const auto wire
          = livehd::placement::technology_net_wire_rc(tech, main_liberty, wire_rc, -1, -1, 1, inputs.liberty_text, clock_layers);
      inputs.parasitic_model = "placement_steiner";
      parasitics = std::make_shared<livehd::physical::Parasitics>(livehd::placement::estimate_floorplan_rc(fp, tech, wire));
    } else if (!spef.empty()) {
      inputs.parasitic_model = "extracted_spef";
      parasitics             = std::make_shared<livehd::physical::Parasitics>(livehd::physical::clock_report_spef(spef, fp, tech));
    } else if (!rc.empty()) {
      parasitics = std::make_shared<livehd::physical::Parasitics>(livehd::physical::deserialize_parasitics_exact(read(rc)));
    }
    std::cout << livehd::timing::fast::report_json(library, fp.design, inputs, parasitics) << '\n';
    return std::cout.good() ? 0 : 1;
  } catch (const std::exception& error) {
    std::cerr << "clock_timing_report: " << error.what() << '\n';
    return 1;
  }
}

// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
// Time a technology-mapped design with the fast engine and report its cost.
//
//   fast_timing_probe --lg DIR --top NAME --corner NAME=FILE[,FILE...] [--corner ...]
//                     --clock-port PORT --period-ps P [--uncertainty-ps U] [--no-input-launch]
//                     [--input-slew-ps S] [--output-delay-ps D --output-load-ff L] [--opentimer-json FILE]
//   fast_timing_probe --lg DIR --top NAME --corner NAME=FILE[,FILE...] --sdc FILE
//
// With --sdc the constraints come from the SDC file (run against the bound netlist, in the units of the first
// corner) instead of the clock and delay options, and what the reader made of it is printed first.
//
// DIR is the lg: library of a mapped netlist (`lhd synth --emit-dir lg:DIR`). The hierarchy under NAME is
// flattened in memory. A corner is one or more Liberty files, concatenated (a `.gz` goes through zcat).
// With --opentimer-json (the timing.json of `lhd pass opentimer`) the worst slack, the total negative slack
// and the longest arrival of OpenTimer are printed next to the engine's, as a diagnostic.
#include <sys/resource.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "bind_graph.hpp"
#include "engine.hpp"
#include "graph_library_singleton.hpp"
#include "node_util.hpp"
#include "pass/partition/flatten.hpp"
#include "rapidjson/document.h"

namespace fast = livehd::timing::fast;
namespace lt   = livehd::liberty::timing;

namespace {
std::string read_file(const std::string& path) {
  const bool  gz   = path.size() > 3 && path.substr(path.size() - 3) == ".gz";
  FILE*       file = gz ? popen(("zcat '" + path + "'").c_str(), "r") : fopen(path.c_str(), "rb");
  std::string text;
  if (file == nullptr) {
    std::fprintf(stderr, "cannot read %s\n", path.c_str());
    std::exit(2);
  }
  char   buffer[1 << 16];
  size_t got;
  while ((got = fread(buffer, 1, sizeof(buffer), file)) > 0) {
    text.append(buffer, got);
  }
  gz ? pclose(file) : fclose(file);
  return text;
}
double now() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
double rss_mb() {
  std::ifstream status("/proc/self/status");
  std::string   line;
  while (std::getline(status, line)) {
    if (line.rfind("VmRSS:", 0) == 0) {
      return std::stod(line.substr(6)) / 1024.0;
    }
  }
  return 0;
}
}  // namespace

int main(int argc, char** argv) {
  std::string              lg, top, clock_port, opentimer_json, sdc_file;
  double                   period = 0, uncertainty = 0, input_slew = 0, output_delay = 0, output_load = 0;
  bool                     input_launch = true, outputs = false;
  std::vector<std::string> corner_specs;
  for (int i = 1; i < argc; ++i) {
    const std::string arg  = argv[i];
    const auto        next = [&]() -> std::string {
      if (i + 1 >= argc) {
        std::fprintf(stderr, "missing value for %s\n", arg.c_str());
        std::exit(2);
      }
      return argv[++i];
    };
    if (arg == "--lg") {
      lg = next();
    } else if (arg == "--top") {
      top = next();
    } else if (arg == "--corner") {
      corner_specs.push_back(next());
    } else if (arg == "--clock-port") {
      clock_port = next();
    } else if (arg == "--period-ps") {
      period = std::stod(next());
    } else if (arg == "--uncertainty-ps") {
      uncertainty = std::stod(next());
    } else if (arg == "--input-slew-ps") {
      input_slew = std::stod(next());
    } else if (arg == "--output-delay-ps") {
      output_delay = std::stod(next());
      outputs      = true;
    } else if (arg == "--output-load-ff") {
      output_load = std::stod(next());
      outputs     = true;
    } else if (arg == "--no-input-launch") {
      input_launch = false;
    } else if (arg == "--opentimer-json") {
      opentimer_json = next();
    } else if (arg == "--sdc") {
      sdc_file = next();
    } else {
      std::fprintf(stderr, "unknown argument %s\n", arg.c_str());
      return 2;
    }
  }
  if (lg.empty() || top.empty() || corner_specs.empty() || (sdc_file.empty() && (clock_port.empty() || period <= 0))) {
    std::fprintf(stderr, "usage: see the head of fast_timing_probe.cpp\n");
    return 2;
  }
  const double t_start = now();

  // Liberty corners.
  std::vector<lt::Library> libraries;
  std::vector<std::string> names;
  for (const auto& spec : corner_specs) {
    const auto  eq = spec.find('=');
    std::string text;
    for (size_t at = eq + 1; at <= spec.size();) {
      auto comma = spec.find(',', at);
      if (comma == std::string::npos) {
        comma = spec.size();
      }
      text += read_file(spec.substr(at, comma - at)) + "\n";
      at    = comma + 1;
    }
    names.push_back(spec.substr(0, eq));
    libraries.push_back(lt::parse(text));
  }
  std::vector<const lt::Library*> pointers;
  for (const auto& library : libraries) {
    pointers.push_back(&library);
  }
  const auto   types = fast::Cell_types::build(pointers, names);
  const double t_lib = now();

  // The design.
  auto&      library = livehd::Hhds_graph_library::instance(lg);
  const auto io      = library.find_io(top);
  if (!io || !io->get_graph()) {
    std::fprintf(stderr, "no module %s in %s\n", top.c_str(), lg.c_str());
    return 2;
  }
  std::shared_ptr<hhds::Graph> graph        = io->get_graph(), flat;
  bool                         hierarchical = false;
  for (auto node : graph->body().nodes(hhds::Node_order::forward)) {
    if (livehd::graph_util::is_type_sub(node) && node.get_subnode_graph() != nullptr) {
      hierarchical = true;
      break;
    }
  }
  if (hierarchical) {
    flat = livehd::partition::flatten_hierarchy(graph.get(), &library, top + "__fast_flat");
    if (!flat) {
      std::fprintf(stderr, "flattening failed\n");
      return 2;
    }
    graph = flat;
  }
  const double t_flat    = now();
  auto         bound     = fast::bind_graph(*graph, types);
  const double t_bind    = now();
  const double rss_bound = rss_mb();

  fast::Constraints constraints;
  if (!sdc_file.empty()) {
    // The constraint file, run against this netlist, in the units of the first corner.
    const fast::Bound_names design_names(bound);
    constraints = fast::read_constraints(read_file(sdc_file), {libraries.front().time_unit_ps, libraries.front().capacitance_unit_ff},
                                         *bound.netlist, &design_names);
    period = constraints.clocks.empty() ? 0 : constraints.clocks.front().period_ps;
    std::printf("sdc: %zu clocks, %zu input and %zu output constraints, %zu cut pins, %zu excluded endpoints, %u launch groups, "
                "%zu endpoint relations (false paths to pins, set_max_delay), max fanout %.0f\n",
                constraints.clocks.size(),
                constraints.inputs.size(),
                constraints.outputs.size(),
                constraints.cut_pins.size(),
                constraints.excluded_endpoints.size(),
                constraints.groups,
                constraints.endpoint_relations.size(),
                constraints.max_fanout);
    for (size_t c = 0; c < constraints.clocks.size(); ++c) {
      const auto& clock = constraints.clocks[c];
      std::printf("  clock %s: period %.1f ps, %zu source pins, launch group %u; setup relation from each group:",
                  clock.name.c_str(),
                  clock.period_ps,
                  clock.source_pins.size(),
                  clock.group);
      for (uint32_t g = 0; g < constraints.groups; ++g) {
        std::printf(" %.1f", constraints.setup_relation_ps.empty() ? clock.period_ps : constraints.setup_relation_ps[g * constraints.clocks.size() + c]);
      }
      std::printf("\n");
    }
    for (const auto& line : constraints.unsupported) {
      std::printf("  sdc unsupported: %s\n", line.c_str());
    }
  } else {
    constraints.clocks.push_back({"clk", clock_port, static_cast<float>(period), static_cast<float>(uncertainty), 0.f});
    constraints.default_input_slew_ps = static_cast<float>(input_slew);
    for (const auto& port : bound.netlist->ports()) {
      if (port.input && port.name != clock_port && !input_launch) {
        constraints.cut_pins.push_back(bound.netlist->pin(port.cell, 0u));
      }
      if (!port.input && outputs) {
        constraints.outputs.push_back({port.name, "", static_cast<float>(output_delay), static_cast<float>(output_load)});
      }
    }
  }
  for (const auto& line : bound.unsupported) {
    constraints.unsupported.push_back(line);
  }
  fast::Engine engine(*bound.netlist, std::move(constraints));
  const double t_engine = now();
  engine.rank();
  const double t_rank = now();
  engine.evaluate();
  const double t_eval = now();
  double       again  = now();
  for (int i = 0; i < 3; ++i) {
    engine.evaluate();
  }
  again                = (now() - again) / 3;
  const double rss_end = rss_mb();

  const auto&  report = engine.report();
  const size_t cells = bound.netlist->cells(), pins = bound.netlist->pins();
  std::printf("design: %zu cells (%zu mapped, %zu opaque), %zu pins, %zu nets; endpoints %zu\n",
              cells,
              bound.mapped_cells,
              bound.opaque_cells,
              pins,
              bound.netlist->nets(),
              report.endpoints);
  std::printf("unbound: constants %zu, undriven %zu, multi-driver %zu, unresolved %zu, unmapped nodes %zu\n",
              bound.constant_sinks,
              bound.undriven_sinks,
              bound.multiple_driver_sinks,
              bound.unresolved_sinks,
              bound.unmapped_nodes);
  std::printf(
      "time: liberty %.2f s, flatten %.2f s, bind %.2f s, engine build %.2f s, rank %.3f s, evaluate %.3f s (repeat %.3f s)\n",
      t_lib - t_start,
      t_flat - t_lib,
      t_bind - t_flat,
      t_engine - t_bind,
      t_rank - t_engine,
      t_eval - t_rank,
      again);
  std::printf(
      "memory: cell tables %.1f MB, netlist %.1f MB (%.0f B/pin), engine %.1f MB (%.0f B/pin, %.0f B/cell); rss %.0f MB after "
      "bind, %.0f MB end\n",
      types.bytes() / 1e6,
      bound.netlist->bytes() / 1e6,
      double(bound.netlist->bytes()) / pins,
      engine.bytes() / 1e6,
      double(engine.bytes()) / pins,
      double(engine.bytes()) / cells,
      rss_bound,
      rss_end);
  std::printf("setup: wns %.2f ps, tns %.2f ps; achievable period %.2f ps (period %.0f)\n",
              report.wns_ps,
              report.tns_ps,
              report.clocks.empty() ? 0.0 : report.clocks[0].achievable_period_ps,
              period);
  for (const auto& corner : report.corners) {
    std::printf("  corner %s: wns %.2f ps, tns %.2f ps\n", corner.name.c_str(), corner.wns_ps, corner.tns_ps);
  }
  for (const auto& clock : report.clocks) {
    std::printf("  clock %s: %zu endpoints, wns %.2f ps, tns %.2f ps, achievable period %.2f ps; hold wns %.2f ps, tns %.2f ps\n",
                clock.name.c_str(),
                clock.endpoints,
                clock.wns_ps,
                clock.tns_ps,
                clock.achievable_period_ps,
                clock.hold_wns_ps,
                clock.hold_tns_ps);
  }
  std::printf("electrical: %zu slew, %zu capacitance, %zu fanout violations; unclocked flip-flops %zu\n",
              report.slew_violations,
              report.capacitance_violations,
              report.fanout_violations,
              report.unclocked_flops);
  std::printf("unsupported (%zu):\n", report.unsupported.size());
  for (size_t i = 0; i < report.unsupported.size() && i < 8; ++i) {
    std::printf("  %s\n", report.unsupported[i].c_str());
  }
  // The longest arrival at an endpoint, and the worst path.
  const auto path = engine.worst_path();
  if (!path.empty()) {
    std::printf("worst path: %zu points, arrival %.2f ps, ends at %s\n",
                path.size(),
                path.back().arrival_ps,
                [&] {
                  const auto pin  = path.back().pin;
                  const auto cell = bound.netlist->cell_of(pin);
                  if (bound.cell_node[cell].is_invalid()) {
                    return bound.netlist->ports().empty() ? std::string("?") : std::string("a port");
                  }
                  return std::string(bound.cell_node[cell].get_name()) + ":" + bound.netlist->pin_type(pin).name;
                }()
                    .c_str());
  }
  // The longest arrival over the endpoints, in the worst corner and transition.
  const auto arrival_at = [&](uint32_t pin) {
    float worst = -std::numeric_limits<float>::infinity();
    for (size_t k = 0; k < engine.corners(); ++k) {
      for (unsigned t = 0; t < 2; ++t) {
        worst = std::max(worst, engine.arrival(pin, k, t));
      }
    }
    return worst;
  };
  float longest = -std::numeric_limits<float>::infinity();
  for (const auto pin : engine.endpoint_pins()) {
    longest = std::max(longest, arrival_at(pin));
  }
  std::printf("longest arrival at an endpoint: %.2f ps\n", longest);
  if (!opentimer_json.empty()) {
    rapidjson::Document doc;
    const auto          text = read_file(opentimer_json);
    doc.Parse(text.c_str());
    if (!doc.HasParseError() && doc.HasMember("designs") && doc["designs"].IsArray() && !doc["designs"].Empty()) {
      const auto&  d    = doc["designs"][0];
      const double unit = d.HasMember("time_unit_ps") ? d["time_unit_ps"].GetDouble() : 1000.0;
      std::printf("opentimer: max_delay %.2f ps", d.HasMember("max_delay") ? d["max_delay"].GetDouble() * unit : 0.0);
      if (d.HasMember("wns")) {
        std::printf(", wns %.2f ps, tns %.2f ps", d["wns"].GetDouble() * unit, d["tns"].GetDouble() * unit);
      }
      std::printf("\n");
      // OpenTimer's longest endpoints, with the engine's arrival at the same pins.
      std::map<std::string, uint32_t> cell_by_name;
      for (uint32_t c = 0; c < bound.netlist->cells(); ++c) {
        if (!bound.cell_node[c].is_invalid()) {
          cell_by_name.emplace(std::string(bound.cell_node[c].get_name()), c);
        }
      }
      if (d.HasMember("endpoints") && d["endpoints"].IsArray()) {
        std::printf("  %-52s %10s %10s %8s\n", "endpoint", "opentimer", "fast", "diff");
        double sum_diff = 0;
        size_t counted  = 0;
        for (const auto& e : d["endpoints"].GetArray()) {
          const std::string label = e["pin"].GetString();
          const auto        colon = label.rfind(':');
          // OpenTimer appends a unique node id (`__n23652`) to the instance name.
          auto              owner = label.substr(0, colon);
          if (const auto mark = owner.rfind("__n"); mark != std::string::npos && mark + 3 < owner.size()
                                                    && owner.find_first_not_of("0123456789", mark + 3) == std::string::npos) {
            owner.erase(mark);
          }
          const auto cell = cell_by_name.find(owner);
          if (colon == std::string::npos || cell == cell_by_name.end()) {
            continue;
          }
          const auto pin = bound.netlist->pin(cell->second, label.substr(colon + 1));
          if (pin == fast::no_index) {
            continue;
          }
          const double ot = e["delay"].GetDouble() * unit, mine = arrival_at(pin);
          const auto   shown = label.substr(label.size() > 52 ? label.size() - 52 : 0);
          if (!std::isfinite(mine)) {
            const bool loop = std::binary_search(bound.netlist->loop_pins().begin(), bound.netlist->loop_pins().end(), pin);
            std::printf("  %-52s %10.2f %10s %8s\n", shown.c_str(), ot, "-", loop ? "(loop)" : "(none)");
            continue;
          }
          std::printf("  %-52s %10.2f %10.2f %7.2f%%\n", shown.c_str(), ot, mine, 100.0 * (mine - ot) / ot);
          sum_diff += std::abs(mine - ot) / ot;
          ++counted;
        }
        if (counted) {
          std::printf("  mean absolute difference %.2f%% over %zu endpoints\n", 100.0 * sum_diff / counted, counted);
        }
      }
    }
  }
  return 0;
}

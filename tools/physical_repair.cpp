// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
// Native repair-only diagnostic: retain a supplied placement and compare fresh
// placement-RC timing before/after repair, without synthesis or global placement.
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>

#include "attrs.hpp"
#include "graph_library_singleton.hpp"
#include "node_util.hpp"
#include "pass/opentimer/pass_opentimer.hpp"
#include "pass/place/repair.hpp"
#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"

namespace pl = livehd::placement;
namespace ph = livehd::physical;
std::string read(const std::string& path) {
  std::ifstream file(path);
  if (!file) {
    throw std::runtime_error("cannot read " + path);
  }
  return {std::istreambuf_iterator<char>(file), {}};
}

// Keep callback cost separate from placement and repair wall time. The engine
// interval includes report generation; feedback measures reading/parsing it.
pl::Timing_feedback evaluate_timing(const pl::Result& placed, const std::string& root, unsigned& serial, const std::string& liberty,
                                    const std::string& sdc, const std::string& top) {
  const auto start  = std::chrono::steady_clock::now();
  const auto call   = serial++;
  const auto report = root + "/timing_" + std::to_string(call) + ".json";
  Eprp_var   analysis;
  analysis.graphs      = {placed.graph};
  analysis.physical_rc = placed.parasitics;
  analysis.dict        = {
      {"files", liberty + "," + sdc},
      {  "top",                 top},
      { "hier",             "false"},
      {  "qor",              report}
  };
  analysis.set_stage_labels(analysis.dict);
  Pass_opentimer::time_work(analysis);
  const auto timed                                = std::chrono::steady_clock::now();
  auto       feedback                             = pl::timing_feedback(read(report));
  feedback.source_report                          = report;
  const auto                                 done = std::chrono::steady_clock::now();
  rapidjson::StringBuffer                    json;
  rapidjson::Writer<rapidjson::StringBuffer> w(json);
  w.StartObject();
  w.Key("call");
  w.Uint(call);
  w.Key("engine_and_report_s");
  w.Double(std::chrono::duration<double>(timed - start).count());
  w.Key("feedback_parse_s");
  w.Double(std::chrono::duration<double>(done - timed).count());
  w.Key("total_s");
  w.Double(std::chrono::duration<double>(done - start).count());
  w.Key("report_bytes");
  w.Uint64(std::filesystem::file_size(report));
  w.EndObject();
  std::ofstream(root + "/timing_calls.jsonl", call ? std::ios::app : std::ios::trunc) << json.GetString() << '\n';
  return feedback;
}

// Import the mapped DEF connectivity directly: no HDL optimization may delete
// dangling or redundant cells in a timing-engine comparison.
std::shared_ptr<hhds::Graph> mapped_def(hhds::GraphLibrary& library, const ph::Floorplan& fp, const ph::Technology& tech) {
  auto                        io = library.create_io(fp.design);
  std::map<std::string, bool> input_ports;
  for (const auto& port : fp.terminals) {
    if (port.direction == "INPUT") {
      io->add_input(port.name, io->get_input_pin_decls().size());
      io->set_bits(port.name, 1);
      input_ports.emplace(port.name, true);
    } else if (port.direction == "OUTPUT") {
      io->add_output(port.name, io->get_output_pin_decls().size());
      io->set_bits(port.name, 1);
      input_ports.emplace(port.name, false);
    }
  }
  auto                                    graph = io->create_graph();
  std::map<std::string, hhds::Node_class> nodes;
  for (const auto& cell : fp.components) {
    auto master = library.find_io(cell.macro);
    if (!master) {
      master = library.create_io(cell.macro);
      for (const auto& [name, pin] : tech.macros.at(cell.macro).pins) {
        if (pin.direction == "INPUT") {
          master->add_input(name, master->get_input_pin_decls().size());
          master->set_bits(name, 1);
        } else if (pin.direction == "OUTPUT") {
          master->add_output(name, master->get_output_pin_decls().size());
          master->set_bits(name, 1);
        }
      }
    }
    auto node = livehd::graph_util::create_typed_node(*graph, Ntype_op::Sub);
    node.set_name(cell.name);
    node.set_subnode(master);
    nodes.emplace(cell.name, node);
  }
  for (const auto& net : fp.nets) {
    hhds::Pin_class              driver;
    std::vector<hhds::Pin_class> sinks;
    for (const auto& pin : net.connections) {
      bool            output = false;
      hhds::Pin_class physical;
      if (pin.instance == "PIN") {
        if (!input_ports.contains(pin.pin)) {
          throw std::runtime_error("unsupported DEF signal port: " + pin.pin);
        }
        if (input_ports.at(pin.pin)) {
          physical = graph->get_input_pin(pin.pin);
          output   = true;
        } else {
          physical = graph->get_output_pin(pin.pin);
        }
      } else {
        auto        node        = nodes.at(pin.instance);
        const auto& declaration = tech.macros.at(std::string(node.get_subnode_io()->get_name())).pins.at(pin.pin);
        output                  = declaration.direction == "OUTPUT";
        if (!output && declaration.direction != "INPUT") {
          throw std::runtime_error("unsupported DEF signal direction: " + pin.instance + ":" + pin.pin);
        }
        physical = output ? node.create_driver_pin(pin.pin) : node.create_sink_pin(pin.pin);
      }
      if (output) {
        livehd::graph_util::set_bits(physical, 1);
        if (!driver.is_invalid()) {
          throw std::runtime_error("multiple DEF signal drivers: " + net.name);
        }
        driver = physical;
      } else {
        sinks.push_back(physical);
      }
    }
    if (!driver.is_invalid()) {
      for (auto sink : sinks) {
        sink.connect_driver(driver);
      }
    }
  }
  return graph;
}

// Geometry-only export for the native RUDY evaluator and placement PNGs. It
// uses the same LEF pin centers, ideal-clock masks and layer capacities as GP.
void physical_snapshot(const ph::Technology& tech, const ph::Floorplan& fp, const std::string& root, double halo) {
  if (fp.rows.empty() || fp.dbu != tech.dbu) {
    throw std::invalid_argument("physical snapshot requires rows and matching LEF/DEF units");
  }
  const auto&  row = fp.rows.front();
  const double sw = row.step, rh = std::llround(tech.sites.at(row.site).height * tech.dbu);
  double       capacity_h = 0, capacity_v = 0;
  for (const auto& name : tech.routing_layers) {
    const auto& l = tech.layers.at(name);
    if (l.direction == "HORIZONTAL" && l.pitch_y > 0) {
      capacity_h += 1 / (l.pitch_y * tech.dbu);
    }
    if (l.direction == "VERTICAL" && l.pitch_x > 0) {
      capacity_v += 1 / (l.pitch_x * tech.dbu);
    }
  }
  std::ofstream out(root + "/physical_snapshot.txt"), boxes(root + "/boxes.tsv");
  out << std::setprecision(17) << row.x << ' ' << row.y << ' ' << row.x + row.count * row.step << ' ' << fp.rows.back().y + rh
      << ' ' << sw << ' ' << rh << ' ' << capacity_h << ' ' << capacity_v << " 64\n"
      << fp.components.size() + fp.terminals.size() << '\n';
  std::map<std::string, int> cells, terminals;
  for (size_t i = 0; i < fp.components.size(); ++i) {
    const auto& c     = fp.components[i];
    const auto& m     = tech.macros.at(c.macro);
    const bool  macro = !m.klass.starts_with("CORE");
    double      bh = 0, bv = 0;
    if (macro) {
      for (const auto& name : m.blocked_layers) {
        const auto l = tech.layers.find(name);
        if (l == tech.layers.end()) {
          continue;
        }
        if (l->second.direction == "HORIZONTAL" && l->second.pitch_y > 0) {
          bh += 1 / (l->second.pitch_y * tech.dbu);
        }
        if (l->second.direction == "VERTICAL" && l->second.pitch_x > 0) {
          bv += 1 / (l->second.pitch_x * tech.dbu);
        }
      }
    }
    const double hx = macro ? std::ceil(halo * tech.dbu / sw) * sw : 0;
    const double hy = macro ? std::ceil(halo * tech.dbu / rh) * rh : 0;
    cells.emplace(c.name, i);
    out << c.place.x - hx << ' ' << c.place.y - hy << ' ' << c.place.width + 2 * hx << ' ' << c.place.height + 2 * hy << ' '
        << macro << ' ' << bh << ' ' << bv << '\n';
    boxes << c.name << '\t' << c.macro << '\t' << c.place.x << '\t' << c.place.y << '\t' << c.place.width << '\t' << c.place.height
          << '\t' << macro << '\n';
  }
  for (size_t i = 0; i < fp.terminals.size(); ++i) {
    const auto& t = fp.terminals[i];
    terminals.emplace(t.name, i);
    out << t.x << ' ' << t.y << " 0 0 0 0 0\n";
  }
  const auto measured = [&](const ph::Net& net) {
    return net.connections.size() >= 2 && std::none_of(net.connections.begin(), net.connections.end(), [&](const auto& c) {
             return c.instance != "PIN"
                    && tech.macros.at(fp.components.at(cells.at(c.instance)).macro).pins.at(c.pin).use == "CLOCK";
           });
  };
  out << std::count_if(fp.nets.begin(), fp.nets.end(), measured) << '\n';
  for (const auto& net : fp.nets) {
    if (!measured(net)) {
      continue;
    }
    out << net.connections.size();
    for (const auto& endpoint : net.connections) {
      if (endpoint.instance == "PIN") {
        const auto  i = terminals.at(endpoint.pin);
        const auto& t = fp.terminals[i];
        out << ' ' << fp.components.size() + i << ' ' << t.x << ' ' << t.y;
      } else {
        const auto  i   = cells.at(endpoint.instance);
        const auto& c   = fp.components[i];
        const auto& m   = tech.macros.at(c.macro);
        const auto& pin = m.pins.at(endpoint.pin);
        if (pin.shapes.empty()) {
          throw std::runtime_error("physical snapshot pin has no LEF geometry");
        }
        const auto& rect  = pin.shapes.front().rect;
        const auto [x, y] = ph::orient_point((rect.xl + rect.xh) / 2 + m.origin_x,
                                             (rect.yl + rect.yh) / 2 + m.origin_y,
                                             m.width,
                                             m.height,
                                             c.place.orientation);
        out << ' ' << i << ' ' << c.place.x + x * tech.dbu << ' ' << c.place.y + y * tech.dbu;
      }
    }
    out << '\n';
  }
  if (!out || !boxes) {
    throw std::runtime_error("cannot write physical snapshot");
  }
}

int main(int argc, char** argv) {
  try {
    if (argc != 2) {
      throw std::invalid_argument("usage: physical_repair config.json");
    }
    rapidjson::Document config;
    config.Parse(read(argv[1]).c_str());
    if (config.HasParseError() || !config.IsObject()) {
      throw std::invalid_argument("invalid repair config");
    }
    const auto text = [&](const char* key) -> std::string {
      if (!config.HasMember(key) || !config[key].IsString()) {
        throw std::invalid_argument(std::string("missing string: ") + key);
      }
      return config[key].GetString();
    };
    const auto root = text("output"), top = text("top"), sdc = text("sdc");
    std::filesystem::create_directories(root);
    const bool  floorplan_mode = config.HasMember("floorplan_place") && config["floorplan_place"].GetBool();
    pl::Options options;
    options.fp_place = options.final_cells = options.macro_snap = false;
    options.legalize_cells                                      = true;
    options.retain_placement    = config.HasMember("timing_only") && config["timing_only"].GetBool();
    options.density_upper_bound = config.HasMember("density_upper_bound") && config["density_upper_bound"].GetBool();
    options.exact_cell_density  = config.HasMember("exact_cell_density") && config["exact_cell_density"].GetBool();
    options.min_iterations      = config.HasMember("min_iterations") ? config["min_iterations"].GetInt() : 40;
    if (config.HasMember("post_density_factor")) {
      options.post_density_factor = config["post_density_factor"].GetInt();
    }
    if (config.HasMember("convergence_tolerance")) {
      options.convergence_tolerance = config["convergence_tolerance"].GetDouble();
    }
    if (config.HasMember("timing_max_weight")) {
      options.timing_max_weight = config["timing_max_weight"].GetDouble();
    }
    if (config.HasMember("timing_smoothing")) {
      options.timing_smoothing = config["timing_smoothing"].GetDouble();
    }
    if (config.HasMember("stop_overflow")) {
      options.stop_overflow = config["stop_overflow"].GetDouble();
    }
    if (config.HasMember("initial_density_weight")) {
      options.initial_density_weight = config["initial_density_weight"].GetDouble();
    }
    if (config.HasMember("timing_fraction")) {
      options.timing_fraction = config["timing_fraction"].GetDouble();
    }
    options.liberty      = text("liberty");
    options.tech_rc_file = text("tech_rc");
    options.output_dir   = root + "/initial";
    options.floorplan    = text("def");
    options.previous     = std::make_shared<ph::Floorplan>(ph::read_def(options.floorplan));
    for (const auto& path : config["lefs"].GetArray()) {
      options.lefs.emplace_back(path.GetString());
    }
    const auto tech  = ph::read_lef(options.lefs);
    auto       frame = std::make_shared<ph::Floorplan>(*options.previous);
    for (auto& cell : frame->components) {
      const auto& macro = tech.macros.at(cell.macro);
      cell.place.width  = std::llround(macro.width * tech.dbu);
      cell.place.height = std::llround(macro.height * tech.dbu);
    }
    options.previous = std::move(frame);
    if (config.HasMember("io_iterations")) {
      options.io_iterations = config["io_iterations"].GetInt();
    }
    if (config.HasMember("fixed_macro_overlap_weight")) {
      options.fixed_macro_overlap_weight = config["fixed_macro_overlap_weight"].GetDouble();
    }
    if (config.HasMember("wirelength_gamma_end")) {
      options.wirelength_gamma_end = config["wirelength_gamma_end"].GetDouble();
    }
    if (config.HasMember("detailed_iterations")) {
      options.detailed_iterations = config["detailed_iterations"].GetInt();
    }
    if (config.HasMember("halo")) {
      options.halo_x = options.halo_y = config["halo"].GetDouble();
    }
    if (config.HasMember("snapshot_only") && config["snapshot_only"].GetBool()) {
      physical_snapshot(tech, *options.previous, root, config.HasMember("halo") ? config["halo"].GetDouble() : 0);
      return 0;
    }
    std::cout << "loading mapped graph" << std::endl;
    hhds::GraphLibrary           source_library;
    std::shared_ptr<hhds::Graph> source;
    if (config.HasMember("source_def") && config["source_def"].GetBool()) {
      source = mapped_def(source_library, *options.previous, tech);
    } else {
      auto&      library = livehd::Hhds_graph_library::instance(text("lg"));
      const auto io      = library.find_io(top);
      if (!io || !io->get_graph()) {
        throw std::runtime_error("missing top graph");
      }
      source = io->get_graph();
    }
    if (config.HasMember("seed_def_positions") && config["seed_def_positions"].GetBool()) {
      std::map<std::string, Ann_place> seeds;
      for (const auto& cell : options.previous->components) {
        seeds.emplace(cell.name, cell.place);
      }
      for (auto node : source->body().nodes()) {
        if (const auto it = seeds.find(std::string(node.get_name())); it != seeds.end()) {
          node.attr(livehd::attrs::place).set(it->second);
        }
      }
    }
    hhds::GraphLibrary placed_library;
    if (floorplan_mode || (config.HasMember("cell_place") && config["cell_place"].GetBool())) {
      // Controlled full cell-GP import, not the ECO/retained-placement path.
      const auto input = options.previous;
      options.previous.reset();
      options.snapshots = true;
      options.bins      = 64;
      options.density   = .5;
      if (config.HasMember("iterations")) {
        options.iterations = config["iterations"].GetInt();
      }
      if (floorplan_mode) {
        options.fp_place = options.final_cells = options.macro_snap = true;
        options.fp_colors                                           = true;
        // Diagnostic comparisons use the same mapped inputs/timing policy
        // while selecting the production FP/color switches independently.
        if (config.HasMember("macro_snap_mode")) {
          options.macro_snap_mode = text("macro_snap_mode");
        }
        if (config.HasMember("fp_place")) {
          options.fp_place = config["fp_place"].GetBool();
        }
        if (config.HasMember("fp_colors")) {
          options.fp_colors = config["fp_colors"].GetBool();
        }
        if (config.HasMember("fp_distance_attraction")) {
          options.fp_distance_attraction = config["fp_distance_attraction"].GetBool();
        }
        if (config.HasMember("fp_attraction_outside")) {
          options.fp_attraction_outside = config["fp_attraction_outside"].GetDouble();
        }
        if (config.HasMember("fp_color_source")) {
          options.fp_color_source = text("fp_color_source");
        }
        if (config.HasMember("fp_color_maximum")) {
          options.fp_color_maximum = config["fp_color_maximum"].GetDouble();
        }
        if (config.HasMember("fp_color_minimum")) {
          options.fp_color_minimum = config["fp_color_minimum"].GetDouble();
        }
        if (config.HasMember("fp_pin_adjust")) {
          options.fp_pin_adjust = config["fp_pin_adjust"].GetBool();
        }
        if (config.HasMember("fp_pin_interval")) {
          options.fp_pin_interval = config["fp_pin_interval"].GetInt();
        }
        if (config.HasMember("fp_iterations")) {
          options.fp_iterations = config["fp_iterations"].GetInt();
        }
        if (config.HasMember("fp_timing_interval")) {
          options.fp_timing_interval = config["fp_timing_interval"].GetInt();
        }
        if (config.HasMember("fp_timing_after_density")) {
          options.fp_timing_after_density = config["fp_timing_after_density"].GetBool();
        }
        if (config.HasMember("fp_timing_movement")) {
          options.fp_timing_movement = config["fp_timing_movement"].GetDouble();
        }
        if (config.HasMember("fp_resize")) {
          options.fp_resize = config["fp_resize"].GetBool();
        }
      }
      if (config.HasMember("color_analysis_only") && config["color_analysis_only"].GetBool()) {
        if (!floorplan_mode) {
          throw std::runtime_error("color_analysis_only requires floorplan_place");
        }
        options.analyze_colors_only = true;
        options.final_cells         = false;
        options.output_dir          = root + "/analysis";
        auto analysis               = pl::place(*source, placed_library, options);
        std::ofstream(root + "/qor.json") << analysis.qor;
        return 0;
      }
      unsigned   serial = 0;
      const auto raw_evaluate
          = [&](const pl::Result& placed) { return evaluate_timing(placed, root, serial, options.liberty, sdc, top); };
      pl::Evaluate evaluate     = raw_evaluate;
      const auto   start        = std::chrono::steady_clock::now();
      auto         seed_options = options;
      seed_options.timing_seed  = true;
      seed_options.output_dir   = root + "/seed";
      hhds::GraphLibrary seed_library;
      auto               seed          = pl::place(*source, seed_library, seed_options);
      const auto         seed_timing   = evaluate(seed);
      const bool         timing_driven = !config.HasMember("timing_driven") || config["timing_driven"].GetBool();
      if (timing_driven) {
        options.net_weights = pl::timing_weights(*seed.parasitics, seed_timing, options.timing_fraction, options.timing_max_weight);
      }
      if (floorplan_mode && timing_driven) {
        options.fp_evaluate = raw_evaluate;
      }
      const auto seeded = std::chrono::steady_clock::now();
      auto       state  = pl::place(*source, placed_library, options);
      ph::write_def(state.floorplan, root + "/initial.def");
      const auto placed         = std::chrono::steady_clock::now();
      unsigned   virtual_serial = 0;
      if (config.HasMember("virtual_timing") && config["virtual_timing"].GetBool()) {
        evaluate = [&](const pl::Result& candidate) {
          auto virtual_options       = options;
          virtual_options.output_dir = root + "/virtual_" + std::to_string(virtual_serial++);
          auto sized                 = pl::repair_buffers(candidate, virtual_options, raw_evaluate, pl::Repair_scope::sizing);
          std::ofstream(virtual_options.output_dir + "/virtual_sizing.json") << sized.report;
          auto virtual_result
              = pl::repair_buffers(std::move(sized.placed), virtual_options, raw_evaluate, pl::Repair_scope::forest);
          std::ofstream(virtual_options.output_dir + "/virtual_repair.json") << virtual_result.report;
          // The temporary graph dies here. GP receives timing keyed by original
          // surviving pins, while masters, buffers and connectivity stay frozen.
          return raw_evaluate(virtual_result.placed);
        };
      }
      const auto        timing_rounds = config.HasMember("timing_rounds") ? config["timing_rounds"].GetUint() : 0;
      pl::Repair_result refined;
      if (timing_rounds) {
        refined = pl::timing_place(std::move(state), options, evaluate, timing_rounds);
      } else {
        refined.placed = std::move(state);
        refined.report = "{\"model\":\"placement_only\",\"rounds\":[],\"accepted\":0}";
      }
      std::ofstream(root + "/timing_place.json") << refined.report;
      auto& result = refined.placed;
      raw_evaluate(result);
      std::map<std::string, const ph::Component*> originals;
      for (const auto& cell : (floorplan_mode ? seed.floorplan.components : input->components)) {
        originals.emplace(cell.name, &cell);
      }
      if (options.fp_resize) {
        originals.clear();
      }
      if (!options.fp_resize && result.floorplan.components.size() != originals.size()) {
        throw std::runtime_error("cell placement changed component count");
      }
      unsigned fixed = 0;
      for (const auto& cell : result.floorplan.components) {
        if (options.fp_resize) {
          continue;
        }
        const auto& old = *originals.at(cell.name);
        if (cell.macro != old.macro) {
          throw std::runtime_error("cell placement changed master: " + cell.name);
        }
        if (old.place.status == ph::Status::FIXED || old.place.status == ph::Status::LOCKED) {
          ++fixed;
          if (cell.place.x != old.place.x || cell.place.y != old.place.y || cell.place.orientation != old.place.orientation) {
            throw std::runtime_error("cell placement moved fixed component: " + cell.name);
          }
        }
      }
      std::ofstream(root + "/qor.json") << result.qor << '\n';
      ph::write_def(result.floorplan, root + "/out.def");
      const auto done = std::chrono::steady_clock::now();
      std::ofstream(root + "/runtime.json") << "{\"seed_s\":" << std::chrono::duration<double>(seeded - start).count()
                                            << ",\"place_s\":" << std::chrono::duration<double>(placed - seeded).count()
                                            << ",\"timing_refine_s\":" << std::chrono::duration<double>(done - placed).count()
                                            << ",\"fixed_components\":" << fixed << ",\"timing_evaluations\":" << serial << "}\n";
      std::cout << (floorplan_mode ? "Floorplan/macro/cell placement completed"
                                   : "Cell placement completed; component masters and fixed positions unchanged")
                << std::endl;
      return 0;
    }
    std::cout << "reconstructing retained placement and RC" << std::endl;
    auto                                        state = pl::place(*source, placed_library, options);
    std::map<std::string, const ph::Component*> initial_geometry;
    for (const auto& cell : options.previous->components) {
      initial_geometry.emplace(cell.name, &cell);
    }
    if (state.floorplan.components.size() != initial_geometry.size()) {
      throw std::runtime_error("retained placement cell count changed");
    }
    for (const auto& cell : state.floorplan.components) {
      const auto& old = *initial_geometry.at(cell.name);
      if (old.macro != cell.macro || old.place.x != cell.place.x || old.place.y != cell.place.y
          || old.place.orientation != cell.place.orientation) {
        throw std::runtime_error("retained placement moved " + cell.name);
      }
    }
    std::cout << "retained placement ready: all masters, positions and orientations unchanged" << std::endl;
    if (config.HasMember("rc_file")) {
      auto rc          = std::make_shared<ph::Parasitics>(ph::deserialize_parasitics(read(text("rc_file"))));
      rc->cells        = state.parasitics->cells;
      rc->terminals    = state.parasitics->terminals;
      state.parasitics = std::move(rc);
    }
    if (config.HasMember("zero_wire") && config["zero_wire"].GetBool()) {
      auto rc       = std::make_shared<ph::Parasitics>(*state.parasitics);
      rc->identity += ":zero-wire";
      for (auto& net : rc->nets) {
        for (auto& node : net.nodes) {
          node.capacitance_ff = 0;
        }
        for (auto& edge : net.edges) {
          edge.resistance_ohm = 0;
        }
      }
      state.parasitics = std::move(rc);
    }
    unsigned   serial = 0;
    const auto evaluate
        = [&](const pl::Result& placed) { return evaluate_timing(placed, root, serial, options.liberty, sdc, top); };
    if (options.retain_placement) {
      std::ofstream(root + "/parasitics.json") << ph::serialize_parasitics(*state.parasitics);
    }
    auto initial = evaluate(state);
    std::cout << "initial delay " << initial.delay << std::endl;
    if (config.HasMember("timing_only") && config["timing_only"].GetBool()) {
      std::ofstream(root + "/parasitics.json") << ph::serialize_parasitics(*state.parasitics);
      ph::write_def(state.floorplan, root + "/out.def");
      return 0;
    }
    const bool repair_first = config.HasMember("repair_first") && config["repair_first"].GetBool();
    const auto start        = std::chrono::steady_clock::now();
    auto       sizing       = pl::repair_buffers(std::move(state),
                                                 options,
                                                 evaluate,
                                                 repair_first ? pl::Repair_scope::design : pl::Repair_scope::sizing,
                                                 std::move(initial));
    std::ofstream(root + (repair_first ? "/repair.json" : "/resize.json")) << sizing.report;
    const auto sized  = std::chrono::steady_clock::now();
    auto       repair = pl::repair_buffers(std::move(sizing.placed),
                                           options,
                                           evaluate,
                                           repair_first ? pl::Repair_scope::sizing : pl::Repair_scope::design,
                                           std::move(sizing.timing));
    std::ofstream(root + (repair_first ? "/resize.json" : "/repair.json")) << repair.report;
    const auto repaired = std::chrono::steady_clock::now();
    std::filesystem::copy_file(repair.timing->source_report,
                               root + "/final_timing.json",
                               std::filesystem::copy_options::overwrite_existing);
    ph::write_def(repair.placed.floorplan, root + "/out.def");
    std::ofstream colors(root + "/colors.tsv");
    for (auto node : repair.placed.graph->body().nodes()) {
      if (!node.get_subnode_io() && livehd::graph_util::type_op_of(node) != Ntype_op::Memory) {
        continue;
      }
      const auto group = node.attr(livehd::attrs::synth_color_group);
      colors << node.get_name() << '\t' << livehd::graph_util::node_color_of(node) << '\t'
             << (group.has() ? group.get() : std::string{}) << '\n';
    }

    hhds::GraphLibrary output;
    if (!livehd::copy_with_callees(output, *repair.placed.graph->get_io()->get_library(), top)) {
      throw std::runtime_error("cannot save repaired graph");
    }
    output.save(root + "/lg");
    std::ofstream(root + "/runtime.json") << "{\"sizing_s\":"
                                          << std::chrono::duration<double>(repair_first ? repaired - sized : sized - start).count()
                                          << ",\"repair_s\":"
                                          << std::chrono::duration<double>(repair_first ? sized - start : repaired - sized).count()
                                          << ",\"timing_evaluations\":" << serial << "}\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}

// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
// Native repair-only diagnostic: retain a supplied placement and compare fresh
// placement-RC timing before/after repair, without synthesis or global placement.
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
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
    if (config.HasMember("cell_place") && config["cell_place"].GetBool()) {
      // Controlled full cell-GP import, not the ECO/retained-placement path.
      const auto input = options.previous;
      options.previous.reset();
      options.snapshots = true;
      options.bins      = 64;
      options.density   = .5;
      if (config.HasMember("iterations")) {
        options.iterations = config["iterations"].GetInt();
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
      for (const auto& cell : input->components) {
        originals.emplace(cell.name, &cell);
      }
      if (result.floorplan.components.size() != originals.size()) {
        throw std::runtime_error("cell placement changed component count");
      }
      unsigned fixed = 0;
      for (const auto& cell : result.floorplan.components) {
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
      ph::write_def(result.floorplan, root + "/out.def");
      const auto done = std::chrono::steady_clock::now();
      std::ofstream(root + "/runtime.json") << "{\"seed_s\":" << std::chrono::duration<double>(seeded - start).count()
                                            << ",\"place_s\":" << std::chrono::duration<double>(placed - seeded).count()
                                            << ",\"timing_refine_s\":" << std::chrono::duration<double>(done - placed).count()
                                            << ",\"fixed_components\":" << fixed << ",\"timing_evaluations\":" << serial << "}\n";
      std::cout << "Cell placement completed; component masters and fixed positions unchanged" << std::endl;
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

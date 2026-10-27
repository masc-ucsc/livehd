// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
// Native timing observation and opt-in fixed-footprint pin-force GP experiment.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>

#include "inou/lefdef/mapped_graph.hpp"
#include "pass/place/forces/gp_timing.hpp"
#include "pass/place/late_timing.hpp"
#include "pass/place/place.hpp"
#include "pass/place/sdc_subset.hpp"
#include "pass/place/technology_rc.hpp"
#include "pass/place/timing_comparison.hpp"
#include "rapidjson/document.h"
#include "rapidjson/ostreamwrapper.h"
#include "rapidjson/writer.h"
#include "zlib.h"

namespace pl   = livehd::placement;
namespace ph   = livehd::physical;
namespace late = pl::late;
namespace fs   = std::filesystem;
namespace {
struct Capture_complete {};
std::string read(const std::string& path) {
  if (path.ends_with(".gz")) {
    gzFile input = gzopen(path.c_str(), "rb");
    if (!input) {
      throw std::runtime_error("cannot read " + path);
    }
    std::string result;
    char        bytes[65536];
    int         count = 0;
    while ((count = gzread(input, bytes, sizeof(bytes))) > 0) {
      result.append(bytes, count);
    }
    const int closed = gzclose(input);
    if (count < 0 || closed != Z_OK) {
      throw std::runtime_error("cannot decompress " + path);
    }
    return result;
  }
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    throw std::runtime_error("cannot read " + path);
  }
  return {std::istreambuf_iterator<char>(input), {}};
}
void write(const fs::path& path, const std::string& text) {
  std::ofstream output(path);
  output << text;
  output.close();
  if (!output) {
    throw std::runtime_error("cannot write " + path.string());
  }
}
std::string string(const rapidjson::Value& object, const char* key) {
  if (!object.IsObject() || !object.HasMember(key) || !object[key].IsString()) {
    throw std::invalid_argument(std::string("missing string: ") + key);
  }
  return object[key].GetString();
}
double number(const rapidjson::Value& object, const char* key) {
  if (!object.IsObject() || !object.HasMember(key) || !object[key].IsNumber() || !std::isfinite(object[key].GetDouble())) {
    throw std::invalid_argument(std::string("missing finite number: ") + key);
  }
  return object[key].GetDouble();
}
int integer(const rapidjson::Value& object, const char* key, int fallback) {
  if (!object.HasMember(key)) {
    return fallback;
  }
  if (!object[key].IsInt() || object[key].GetInt() <= 0 || object[key].GetInt() > 1000000) {
    throw std::invalid_argument(std::string(key) + " must be an integer in [1,1000000]");
  }
  return object[key].GetInt();
}
bool flag(const rapidjson::Value& object, const char* key) {
  if (!object.HasMember(key)) {
    return false;
  }
  if (!object[key].IsBool()) {
    throw std::invalid_argument(std::string(key) + " must be Boolean");
  }
  return object[key].GetBool();
}
std::map<std::string, std::pair<double, double>> read_locations(const std::string& path) {
  rapidjson::Document json;
  json.Parse<rapidjson::kParseFullPrecisionFlag>(read(path).c_str());
  if (json.HasParseError() || !json.IsObject()) {
    throw std::invalid_argument("invalid checkpoint pin locations");
  }
  std::map<std::string, std::pair<double, double>> result;
  for (const auto& member : json.GetObject()) {
    const auto& xy = member.value;
    if (!xy.IsArray() || xy.Size() != 2 || !xy[0].IsNumber() || !xy[1].IsNumber() || !std::isfinite(xy[0].GetDouble())
        || !std::isfinite(xy[1].GetDouble())
        || !result.emplace(member.name.GetString(), std::pair{xy[0].GetDouble(), xy[1].GetDouble()}).second) {
      throw std::invalid_argument("invalid or duplicate checkpoint pin location");
    }
  }
  return result;
}
template <typename Writer>
void compare(Writer& writer, const late::Result& star, const late::Result& reference) {
  const auto comparison = late::compare_timing(star, reference);
  const auto optional   = [&](std::optional<double> value) {
    if (value) {
      writer.Double(*value);
    } else {
      writer.Null();
    }
  };
  writer.StartObject();
  writer.Key("reference_wns_ps");
  if (reference.wns_ps) {
    writer.Double(*reference.wns_ps);
  } else {
    writer.Null();
  }
  writer.Key("reference_certified");
  writer.Bool(reference.certified);
  writer.Key("reference_driver_fit_failures");
  writer.Uint64(reference.driver_fit_failures);
  writer.Key("reference_full_load_boundary_driver_fits");
  writer.Uint64(reference.full_load_boundary_driver_fits);
  writer.Key("reference_crossing_boundary_driver_fits");
  writer.Uint64(reference.crossing_boundary_driver_fits);
  writer.Key("reference_table_boundary_driver_fits");
  writer.Uint64(reference.table_boundary_driver_fits);
  writer.Key("numerical_gates_pass");
  writer.Bool(comparison.numerical_gates_pass);
  writer.Key("ranking_gates_pass");
  writer.Bool(comparison.ranking_gates_pass);
  writer.Key("comparison_gates_pass");
  writer.Bool(comparison.comparison_gates_pass);
  writer.Key("supported_coverage_pass");
  writer.Bool(comparison.supported_coverage_pass);
  for (unsigned field = 0; field < 4; ++field) {
    const auto& error = comparison.fields[field];
    writer.Key(field == 0 ? "arrival_ps" : field == 1 ? "required_ps" : field == 2 ? "slew_ps" : "load_ff");
    writer.StartObject();
    writer.Key("compared_transitions");
    writer.Uint64(error.compared);
    writer.Key("coverage_disagreements");
    writer.Uint64(error.coverage_disagreements);
    writer.Key("maximum_absolute_error");
    writer.Double(error.maximum);
    writer.Key("mean_absolute_error");
    optional(error.mean);
    writer.Key("numerical_tolerance_failures");
    if (error.tolerance_failures) {
      writer.Uint64(*error.tolerance_failures);
    } else {
      writer.Null();
    }
    writer.Key("worst_pin");
    writer.String(error.worst ? error.worst->pin.c_str() : "");
    writer.Key("worst_transition");
    writer.Uint(error.worst ? error.worst->transition : 0);
    writer.Key("worst_reference");
    optional(error.worst ? std::optional<double>(error.worst->reference) : std::nullopt);
    writer.Key("worst_prediction");
    optional(error.worst ? std::optional<double>(error.worst->prediction) : std::nullopt);
    writer.Key("failure_examples");
    writer.StartArray();
    for (const auto& failure : error.failure_examples) {
      writer.StartObject();
      writer.Key("pin");
      writer.String(failure.pin.c_str());
      writer.Key("transition");
      writer.Uint(failure.transition);
      writer.Key("reference");
      writer.Double(failure.reference);
      writer.Key("prediction");
      writer.Double(failure.prediction);
      writer.Key("error");
      writer.Double(failure.error);
      writer.Key("tolerance");
      writer.Double(failure.tolerance);
      writer.EndObject();
    }
    writer.EndArray();
    writer.EndObject();
  }
  const auto& rank = comparison.ranking;
  writer.Key("near_critical");
  writer.StartObject();
  writer.Key("compared_endpoint_transitions");
  writer.Uint64(rank.compared_endpoints);
  writer.Key("coverage_disagreements");
  writer.Uint64(rank.coverage_disagreements);
  writer.Key("active_period_ps");
  optional(rank.period_ps);
  writer.Key("reference_count");
  writer.Uint64(rank.reference_critical);
  writer.Key("prediction_count");
  writer.Uint64(rank.prediction_critical);
  writer.Key("union_count");
  writer.Uint64(rank.union_size);
  writer.Key("jaccard");
  optional(rank.jaccard);
  writer.Key("tau_b");
  optional(rank.tau_b);
  writer.Key("unavailable");
  writer.String(rank.unavailable.c_str());
  writer.EndObject();
  writer.EndObject();
}
using Connectivity = std::set<std::vector<std::pair<std::string, std::string>>>;
Connectivity connectivity(const ph::Floorplan& fp) {
  Connectivity result;
  for (const auto& net : fp.nets) {
    if (net.connections.size() < 2) {
      continue;  // isolated physical pins have no signal edge
    }
    std::vector<std::pair<std::string, std::string>> pins;
    for (const auto& pin : net.connections) {
      pins.emplace_back(pin.instance, pin.pin);
    }
    std::sort(pins.begin(), pins.end());
    if (!result.insert(std::move(pins)).second) {
      throw std::runtime_error("duplicate connected signal net");
    }
  }
  return result;
}
void verify_signal_frame(const ph::Floorplan& original, const ph::Floorplan& placed, const Connectivity& nets) {
  std::map<std::string, const ph::Component*> cells;
  for (const auto& c : original.components) {
    if (!cells.emplace(c.name, &c).second) {
      throw std::runtime_error("duplicate original instance: " + c.name);
    }
  }
  if (placed.components.size() != cells.size() || placed.blockages != original.blockages || connectivity(placed) != nets) {
    throw std::runtime_error("cell GP changed original signal connectivity or placement exclusions");
  }
  for (const auto& c : placed.components) {
    const auto it = cells.find(c.name);
    if (it == cells.end() || c.macro != it->second->macro) {
      throw std::runtime_error("cell GP changed instance/master: " + c.name);
    }
    const auto& p = it->second->place;
    if ((p.status == ph::Status::FIXED || p.status == ph::Status::LOCKED)
        && (c.place.x != p.x || c.place.y != p.y || c.place.orientation != p.orientation)) {
      throw std::runtime_error("cell GP moved a fixed instance: " + c.name);
    }
    cells.erase(it);
  }
  if (placed.terminals != original.terminals) {
    // Snapshot net names are local aliases. Compare every other IO field.
    auto actual = placed.terminals;
    for (auto& t : actual) {
      const auto it
          = std::find_if(original.terminals.begin(), original.terminals.end(), [&](const auto& p) { return p.name == t.name; });
      if (it == original.terminals.end()) {
        throw std::runtime_error("cell GP added an IO: " + t.name);
      }
      t.net = it->net;
      if (t.status == ph::Status::FIXED && it->status == ph::Status::PLACED) {
        t.status = it->status;  // temporary GP freeze, original status restored on export
      }
    }
    auto       expected = original.terminals;
    const auto by_name  = [](const auto& a, const auto& b) { return a.name < b.name; };
    std::sort(actual.begin(), actual.end(), by_name);
    std::sort(expected.begin(), expected.end(), by_name);
    if (actual != expected) {
      std::string detail;
      for (size_t i = 0; i < std::min(actual.size(), expected.size()); ++i) {
        if (actual[i] != expected[i]) {
          const auto& a = actual[i];
          const auto& b = expected[i];
          detail        = ": " + a.name + " / " + b.name + " xy=" + std::to_string(a.x) + "," + std::to_string(a.y) + " / "
                   + std::to_string(b.x) + "," + std::to_string(b.y) + " status=" + std::to_string(int(a.status)) + " / "
                   + std::to_string(int(b.status));
          break;
        }
      }
      throw std::runtime_error("cell GP changed fixed IO geometry" + detail);
    }
  }
}
}  // namespace

int main(int argc, char** argv) {
  try {
    if (argc != 2) {
      throw std::invalid_argument("usage: timeclosure_gp_probe config.json");
    }
    const auto          started = std::chrono::steady_clock::now();
    const auto          elapsed = [&] { return std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count(); };
    rapidjson::Document config;
    config.Parse(read(argv[1]).c_str());
    if (config.HasParseError() || !config.IsObject()) {
      throw std::invalid_argument("invalid GP observation config");
    }
    const bool                 continuous_star         = flag(config, "continuous_star_wire");
    const bool                 compare_star            = flag(config, "compare_continuous_star");
    const bool                 save_checkpoints        = flag(config, "save_timing_checkpoints");
    const bool                 capture_only            = flag(config, "capture_only");
    const bool                 replay                  = config.HasMember("replay_checkpoints");
    const bool                 native_forces           = flag(config, "native_pin_timing");
    const bool                 native_rollback         = flag(config, "native_timing_rollback");
    const bool                 legal_output_guard      = flag(config, "native_timing_final_guard");
    const bool                 recalibrate_on_mismatch = flag(config, "native_timing_recalibrate_on_mismatch");
    const late::Driver_options driver{.characterized_boundary = flag(config, "characterized_driver_boundary"),
                                      .bounded_boundary       = flag(config, "bounded_driver_boundary")};
    if (!native_forces
        && (config.HasMember("native_legalize_timing") || config.HasMember("native_legalize_displacement_strength")
            || config.HasMember("native_legalize_net_strength"))) {
      throw std::invalid_argument("native legalization priorities require native_pin_timing");
    }
    if (!native_forces
        && (legal_output_guard || config.HasMember("native_timing_max_overflow")
            || config.HasMember("native_timing_startup_coefficient") || config.HasMember("native_timing_startup_path_fraction")
            || config.HasMember("native_timing_startup_average_wire_um"))) {
      throw std::invalid_argument("native timing density/final guards require native_pin_timing");
    }
    if (native_rollback && !native_forces) {
      throw std::invalid_argument("native_timing_rollback requires native_pin_timing");
    }
    if (config.HasMember("native_timing_recalibrate_on_mismatch") && !native_forces) {
      throw std::invalid_argument("native timing recalibration requires native_pin_timing");
    }
    if (native_forces && (!continuous_star || !compare_star || capture_only || replay)) {
      throw std::invalid_argument(
          "native_pin_timing requires continuous_star_wire and comparison; capture-only/replay cannot optimize");
    }
    if ((capture_only && (!save_checkpoints || replay)) || (replay && save_checkpoints)) {
      throw std::invalid_argument("capture_only requires saved checkpoints; replay cannot capture or stop GP");
    }
    if (compare_star && !continuous_star) {
      throw std::invalid_argument("compare_continuous_star requires continuous_star_wire");
    }
    const fs::path root = fs::absolute(string(config, "output"));
    if (fs::exists(root) || root.string().find_first_of(" \t\r\n") != std::string::npos) {
      throw std::invalid_argument("GP observation requires a fresh output directory without whitespace");
    }
    pl::Options options;
    options.iterations           = integer(config, "iterations", 20);
    options.min_iterations       = integer(config, "min_iterations", options.iterations);
    options.gp_observer_interval = integer(config, "observe_interval", 20);
    if (options.min_iterations > options.iterations) {
      throw std::invalid_argument("min_iterations must not exceed iterations");
    }
    if (capture_only && options.min_iterations != options.iterations) {
      throw std::invalid_argument("capture_only requires min_iterations equal to iterations to guarantee the final observation");
    }
    if (capture_only && options.iterations % options.gp_observer_interval) {
      throw std::invalid_argument("capture_only requires an observation at the final GP iteration");
    }
    if (config.HasMember("reuse_steiner_rc")) {
      if (!config["reuse_steiner_rc"].IsBool()) {
        throw std::invalid_argument("reuse_steiner_rc must be Boolean");
      }
      options.gp_observer_rc_reuse = config["reuse_steiner_rc"].GetBool();
    }
    if (config.HasMember("verify_reused_steiner_rc")) {
      if (!config["verify_reused_steiner_rc"].IsBool()) {
        throw std::invalid_argument("verify_reused_steiner_rc must be Boolean");
      }
      options.gp_observer_rc_verify = config["verify_reused_steiner_rc"].GetBool();
    }
    if (options.gp_observer_rc_verify && !options.gp_observer_rc_reuse) {
      throw std::invalid_argument("verify_reused_steiner_rc requires reuse_steiner_rc");
    }
    options.bins = integer(config, "bins", 32);
    if (options.bins < 2 || options.bins > 512 || options.gp_observer_interval > options.iterations) {
      throw std::invalid_argument("bins must be in [2,512] and observation interval must fit the GP budget");
    }
    if (config.HasMember("virtual_filler_target_count")) {
      throw std::invalid_argument("virtual_filler_target_count was removed; use the area-relative virtual_filler_fraction");
    }
    if (config.HasMember("virtual_filler_fraction")) {
      options.virtual_filler_fraction = number(config, "virtual_filler_fraction");
      if (options.virtual_filler_fraction <= 0 || options.virtual_filler_fraction > 2) {
        throw std::invalid_argument("virtual_filler_fraction must be in (0,2]");
      }
      std::cerr << "native virtual filler budget fraction=" << options.virtual_filler_fraction << '\n';
    }
    options.density = number(config, "density");
    if (options.density <= 0 || options.density > 1) {
      throw std::invalid_argument("density must be in (0,1]");
    }
    if (!config.HasMember("wire_rc") || !config["wire_rc"].IsObject() || !config.HasMember("sdc_units")
        || !config["sdc_units"].IsObject() || !config.HasMember("liberty") || !config["liberty"].IsArray()
        || config["liberty"].Empty() || !config.HasMember("lefs") || !config["lefs"].IsArray()) {
      throw std::invalid_argument("GP observation requires explicit wire_rc, sdc_units, liberty and lefs");
    }
    options.resistance_ohm_um = number(config["wire_rc"], "resistance_ohm_um");
    options.capacitance_ff_um = number(config["wire_rc"], "capacitance_ff_um");
    if (options.resistance_ohm_um < 0 || options.capacitance_ff_um < 0) {
      throw std::invalid_argument("wire RC must be nonnegative");
    }
    for (const auto& path : config["lefs"].GetArray()) {
      if (!path.IsString()) {
        throw std::invalid_argument("LEF paths must be strings");
      }
      options.lefs.emplace_back(path.GetString());
    }
    const auto technology = ph::read_lef(options.lefs);
    options.floorplan     = string(config, "def");
    const auto original   = ph::read_def(options.floorplan);
    for (const auto& c : original.components) {
      if (!technology.macros.at(c.macro).klass.starts_with("CORE") && c.place.status != ph::Status::FIXED
          && c.place.status != ph::Status::LOCKED) {
        throw std::invalid_argument("cell GP observation requires fixed macros: " + c.name);
      }
    }
    for (const auto& p : original.terminals) {
      if (p.status == ph::Status::UNPLACED) {
        throw std::invalid_argument("cell GP observation requires positioned IO: " + p.name);
      }
    }
    const auto constraints
        = late::read_saved_sdc(read(string(config, "sdc")),
                               original.design,
                               {number(config["sdc_units"], "time_ps"), number(config["sdc_units"], "capacitance_ff")});
    fs::create_directories(root / "libraries");
    write(root / "config.json", read(argv[1]));
    auto   solver_frame               = ph::rectangular_placement_frame(original, technology);
    auto   export_blockages           = original.blockages;
    size_t fixed_occupancy_rectangles = 0;
    if (config.HasMember("fixed_occupancy_def")) {
      const auto path      = string(config, "fixed_occupancy_def");
      const auto occupancy = ph::read_def(path);
      if (occupancy.design != original.design || occupancy.dbu != original.dbu || occupancy.die != original.die
          || !occupancy.components.empty() || !occupancy.terminals.empty() || !occupancy.nets.empty() || !occupancy.rows.empty()
          || !occupancy.tracks.empty() || occupancy.blockages.empty()) {
        throw std::invalid_argument("fixed occupancy requires a matching DEF frame containing only hard placement blockages");
      }
      for (const auto& blockage : occupancy.blockages) {
        if (blockage.soft || !blockage.component.empty()) {
          throw std::invalid_argument("fixed occupancy requires unassociated hard placement blockages");
        }
        fixed_occupancy_rectangles += blockage.rectangles.size();
        solver_frame.blockages.push_back(blockage);
        export_blockages.push_back(blockage);
      }
      // Persist the exact physical-only input alongside the unmodified signal
      // inventory. It adds density/legalization masks, never graph instances.
      ph::write_def(occupancy, (root / "fixed_occupancy.def").string());
      ph::verify_def_roundtrip(occupancy, ph::read_def((root / "fixed_occupancy.def").string()));
    }
    auto fixed_io = solver_frame;
    for (auto& port : fixed_io.terminals) {
      port.status = ph::Status::FIXED;
    }
    options.floorplan = (root / "fixed_io.def").string();
    ph::write_def(fixed_io, options.floorplan);
    ph::verify_def_roundtrip(fixed_io, ph::read_def(options.floorplan));
    std::string contents;
    for (const auto& path : config["liberty"].GetArray()) {
      if (!path.IsString()) {
        throw std::invalid_argument("Liberty paths must be strings");
      }
      auto       text = read(path.GetString());
      const auto copy = root / "libraries" / (std::to_string(contents.size()) + ".lib");
      write(copy, text);
      if (!options.liberty.empty()) {
        options.liberty += ' ';
      }
      options.liberty += copy.string();
      contents        += text + '\n';
    }
    auto models = std::make_shared<livehd::liberty::timing::Library>(livehd::liberty::timing::parse(contents));
    contents.clear();
    contents.shrink_to_fit();
    hhds::GraphLibrary source_library, placed_library;
    const auto         source = ph::mapped_graph(source_library, original, technology);
    const auto         nets   = connectivity(original);
    options.output_dir        = (root / "placement").string();
    options.fp_place = options.final_cells = options.macro_snap = false;
    options.fp_pin_adjust = options.macro_pin_adjust = false;
    options.halo_x = options.halo_y = 0;
    options.optimization_pending    = true;
    options.preserve_imported_ties  = true;
    options.threads                 = 1;
    options.post_density_factor     = 0;
    options.routability             = false;
    options.restart_seed            = std::make_shared<ph::Floorplan>(original);
    options.snapshots               = true;
    std::ofstream observations(root / "observations.jsonl");
    if (!observations) {
      throw std::runtime_error("cannot create native GP observations");
    }
    std::unique_ptr<late::Proxy> proxy;
    const hhds::Graph*           bound          = nullptr;
    unsigned                     samples        = 0;
    double                       timing_seconds = 0;
    late::Star_context           star;
    star.options.calibrate_admittance         = flag(config, "star_admittance_correction");
    star.options.branch_relative_displacement = flag(config, "star_branch_relative_displacement");
    if (config.HasMember("star_max_pin_displacement_fraction")) {
      star.options.max_pin_displacement_fraction = number(config, "star_max_pin_displacement_fraction");
    }
    if (!continuous_star
        && (star.options.calibrate_admittance || star.options.max_pin_displacement_fraction
            || star.options.branch_relative_displacement)) {
      throw std::invalid_argument("star correction options require continuous_star_wire");
    }
    std::optional<double> calibrated_overflow;
    if (continuous_star || save_checkpoints || replay) {
      const auto wire = pl::technology_wire_rc(technology,
                                               options.liberty,
                                               options.tech_rc_file,
                                               options.resistance_ohm_um,
                                               options.capacitance_ff_um,
                                               options.rc_pessimism);
      star.policy     = {livehd::placement::per_um(wire), double(technology.dbu), wire.source};
    }
    if (native_forces) {
      auto settings                     = std::make_shared<pl::timeclosure::Gp_timing_settings>();
      settings->continuing_rollback     = native_rollback;
      settings->legal_output_guard      = legal_output_guard;
      settings->driver                  = driver;
      settings->legalize_observe        = flag(config, "native_legalize_timing");
      settings->recalibrate_on_mismatch = recalibrate_on_mismatch;
      if (config.HasMember("native_legalize_displacement_strength")) {
        settings->legalization.displacement = number(config, "native_legalize_displacement_strength");
      }
      if (config.HasMember("native_legalize_net_strength")) {
        settings->legalization.net = number(config, "native_legalize_net_strength");
      }
      if (config.HasMember("native_timing_max_overflow")) {
        settings->maximum_density_overflow = number(config, "native_timing_max_overflow");
      }
      settings->models      = models;
      settings->constraints = constraints;
      settings->wire        = star.options;
      settings->interval    = integer(config, "native_timing_interval", 10);
      if (config.HasMember("native_timing_startup_average_wire_um")) {
        settings->startup_average_wire_um = number(config, "native_timing_startup_average_wire_um");
      }
      if (config.HasMember("native_timing_startup_coefficient")) {
        settings->startup_coefficient = number(config, "native_timing_startup_coefficient");
      }
      if (config.HasMember("native_timing_startup_path_fraction")) {
        settings->startup_path_fraction = number(config, "native_timing_startup_path_fraction");
      }
      settings->start = 0;
      if (config.HasMember("native_timing_start")) {
        if (!config["native_timing_start"].IsInt() || config["native_timing_start"].GetInt() < 0) {
          throw std::invalid_argument("native_timing_start must be a nonnegative integer");
        }
        settings->start = config["native_timing_start"].GetInt();
      }
      if (config.HasMember("native_timing_coefficient")) {
        settings->coefficient = number(config, "native_timing_coefficient");
      }
      settings->validate();
      options.native_gp_timing = std::move(settings);
    }
    rapidjson::Document checkpoint_index(rapidjson::kArrayType);
    options.gp_observer = [&](const pl::Result& snapshot, const pl::Gp_observation& sample) {
      verify_signal_frame(solver_frame, snapshot.floorplan, nets);
      if (save_checkpoints) {
        const auto prefix  = root / ("checkpoint_" + std::to_string(sample.iteration));
        const auto rc_path = prefix.string() + ".rc.json", pin_path = prefix.string() + ".pins.json";
        const auto frame_path = prefix.string() + ".def";
        write(rc_path, ph::serialize_parasitics(*snapshot.parasitics));
        ph::write_def(snapshot.floorplan, frame_path);
        std::ofstream                                locations(pin_path);
        rapidjson::OStreamWrapper                    stream(locations);
        rapidjson::Writer<rapidjson::OStreamWrapper> writer(stream);
        writer.StartObject();
        for (const auto& [name, xy] : snapshot.pin_locations) {
          writer.Key(name.c_str());
          writer.StartArray();
          writer.Double(xy.first);
          writer.Double(xy.second);
          writer.EndArray();
        }
        writer.EndObject();
        locations.close();
        if (!locations) {
          throw std::runtime_error("cannot save checkpoint pin locations");
        }
        auto&            allocator = checkpoint_index.GetAllocator();
        rapidjson::Value item(rapidjson::kObjectType);
        item.AddMember("iteration", sample.iteration, allocator);
        item.AddMember("parasitics", rapidjson::Value(rc_path.c_str(), allocator), allocator);
        item.AddMember("pin_locations", rapidjson::Value(pin_path.c_str(), allocator), allocator);
        item.AddMember("frame", rapidjson::Value(frame_path.c_str(), allocator), allocator);
        item.AddMember("density_overflow", sample.density_overflow, allocator);
        item.AddMember("hpwl_um", sample.hpwl_um, allocator);
        item.AddMember("opaque_route_overflow", sample.opaque_route_overflow, allocator);
        rapidjson::Value policy(rapidjson::kArrayType);
        for (double value : star.policy.per_um) {
          policy.PushBack(value, allocator);
        }
        policy.PushBack(star.policy.dbu, allocator);
        item.AddMember("wire_policy", policy, allocator);
        checkpoint_index.PushBack(item, allocator);
        std::ofstream                                index(root / "checkpoints.json");
        rapidjson::OStreamWrapper                    index_stream(index);
        rapidjson::Writer<rapidjson::OStreamWrapper> index_writer(index_stream);
        checkpoint_index.Accept(index_writer);
        index.close();
        if (!index) {
          throw std::runtime_error("cannot save checkpoint index");
        }
      }
      const auto begin = elapsed();
      if (bound != snapshot.graph.get()) {
        bound     = snapshot.graph.get();
        proxy     = std::make_unique<late::Proxy>(*bound, models, constraints);
        star.bank = {};
        calibrated_overflow.reset();
      }
      if (continuous_star) {
        star.locations = snapshot.pin_locations;
        star.refresh   = !calibrated_overflow || *calibrated_overflow - sample.density_overflow >= .1;
        if (star.refresh) {
          calibrated_overflow = sample.density_overflow;
        }
      }
      const auto   result   = proxy->evaluate(snapshot.parasitics.get(), continuous_star ? &star : nullptr, {.driver = driver});
      const double seconds  = elapsed() - begin;
      timing_seconds       += seconds;
      const auto                  reference_begin = elapsed();
      std::optional<late::Result> reference;
      if (compare_star) {
        reference = proxy->evaluate(snapshot.parasitics.get(), nullptr, {.driver = driver});
      }
      const double comparison_seconds = elapsed() - reference_begin;
      ++samples;
      rapidjson::OStreamWrapper                    stream(observations);
      rapidjson::Writer<rapidjson::OStreamWrapper> writer(stream);
      const auto                                   optional = [&](std::optional<double> value) {
        if (value) {
          writer.Double(*value);
        } else {
          writer.Null();
        }
      };
      writer.StartObject();
      writer.Key("iteration");
      writer.Int(sample.iteration);
      writer.Key("rc_identity");
      writer.String(snapshot.parasitics->identity.c_str());
      writer.Key("rc_reuse_verified");
      writer.Bool(sample.rc_reuse_verified);
      writer.Key("certified");
      writer.Bool(result.certified);
      writer.Key("wns_ps");
      optional(result.wns_ps);
      writer.Key("tns_ps");
      optional(result.tns_ps);
      writer.Key("observed_achievable_period_ps");
      optional(result.achievable_period_ps);
      writer.Key("optimizer_feedback_enabled");
      writer.Bool(native_forces);
      writer.Key("hpwl_um");
      writer.Double(sample.hpwl_um);
      writer.Key("physical_density_overflow");
      writer.Double(sample.density_overflow);
      writer.Key("wire_model");
      writer.String(continuous_star
                        ? (star.options.calibrate_admittance ? "wire_scaled_star_admittance_v2" : "wire_scaled_star_elmore_v1")
                        : "driver_aware_Steiner");
      writer.Key("star_calibration");
      if (continuous_star) {
        writer.StartObject();
        writer.Key("explicit_refresh");
        writer.Bool(star.refresh);
        writer.Key("branch_relative_displacement");
        writer.Bool(star.options.branch_relative_displacement);
        for (const auto& [name, value] : {
                 std::pair{       "calibrated",        star.stats.calibrated},
                 {           "reused",            star.stats.reused},
                 {     "load_rebound",      star.stats.load_rebound},
                 {          "removed",           star.stats.removed},
                 {   "pins_evaluated",    star.stats.pins_evaluated},
                 {"zero_wire_anchors", star.stats.zero_wire_anchors}
        }) {
          writer.Key(name);
          writer.Uint64(value);
        }
        writer.Key("displacement_refreshed");
        writer.Uint64(star.stats.displacement_refreshed);
        writer.Key("max_displacement_fraction");
        writer.Double(star.stats.max_displacement_fraction);
        writer.EndObject();
      } else {
        writer.Null();
      }
      writer.Key("star_steiner_comparison");
      if (reference) {
        compare(writer, result, *reference);
      } else {
        writer.Null();
      }
      writer.Key("comparison_seconds");
      writer.Double(comparison_seconds);
      writer.Key("opaque_route_overflow");
      writer.Double(sample.opaque_route_overflow);
      writer.Key("driver_fit_failures");
      writer.Uint64(result.driver_fit_failures);
      writer.Key("characterized_driver_boundary");
      writer.Bool(result.characterized_boundary_policy);
      writer.Key("bounded_driver_boundary");
      writer.Bool(result.bounded_boundary_policy);
      writer.Key("full_load_boundary_driver_fits");
      writer.Uint64(result.full_load_boundary_driver_fits);
      writer.Key("crossing_boundary_driver_fits");
      writer.Uint64(result.crossing_boundary_driver_fits);
      writer.Key("table_boundary_driver_fits");
      writer.Uint64(result.table_boundary_driver_fits);
      writer.Key("rc_reuse");
      if (sample.rc_reuse) {
        writer.StartObject();
        for (const auto& [name, value] : {
                 std::pair{"nets_inspected", sample.rc_reuse->nets_inspected},
                 {"pins_inspected", sample.rc_reuse->pins_inspected},
                 {       "rebuilt",        sample.rc_reuse->rebuilt},
                 {        "reused",         sample.rc_reuse->reused},
                 {  "timing_dirty",   sample.rc_reuse->timing_dirty},
                 {       "removed",        sample.rc_reuse->removed}
        }) {
          writer.Key(name);
          writer.Uint64(value);
        }
        writer.EndObject();
      } else {
        writer.Null();
      }
      writer.Key("unsupported");
      writer.StartArray();
      for (const auto& issue : result.unsupported) {
        writer.String(issue.c_str());
      }
      writer.EndArray();
      writer.Key("evaluation_seconds");
      writer.Double(seconds);
      writer.EndObject();
      observations << '\n';
      observations.flush();
      if (!observations) {
        throw std::runtime_error("cannot write native GP observation");
      }
      if (capture_only && sample.iteration == options.iterations) {
        throw Capture_complete{};
      }
    };
    const double imported           = elapsed();
    const auto   diagnostic_summary = [&](const char* mode) {
      rapidjson::Document summary(rapidjson::kObjectType);
      auto&               allocator = summary.GetAllocator();
      summary.AddMember("mode", rapidjson::Value(mode, allocator), allocator);
      summary.AddMember("legalized", false, allocator);
      summary.AddMember("synthesis_resynthesis", false, allocator);
      summary.AddMember("optimizer_feedback_enabled", false, allocator);
      summary.AddMember("samples", samples, allocator);
      summary.AddMember("signal_instances", uint64_t(original.components.size()), allocator);
      summary.AddMember("total_seconds", elapsed(), allocator);
      std::ofstream                                output(root / "summary.json");
      rapidjson::OStreamWrapper                    stream(output);
      rapidjson::Writer<rapidjson::OStreamWrapper> writer(stream);
      summary.Accept(writer);
      output.close();
      if (!output) {
        throw std::runtime_error("cannot write diagnostic summary");
      }
    };
    if (replay) {
      rapidjson::Document index;
      index.Parse<rapidjson::kParseFullPrecisionFlag>(read(string(config, "replay_checkpoints")).c_str());
      if (index.HasParseError() || !index.IsArray() || index.Empty()) {
        throw std::invalid_argument("invalid checkpoint index");
      }
      int last = 0;
      for (const auto& item : index.GetArray()) {
        if (!item.IsObject() || !item.HasMember("wire_policy") || !item["wire_policy"].IsArray()
            || item["wire_policy"].Size() != 5) {
          throw std::invalid_argument("checkpoint lacks normalized wire policy");
        }
        for (unsigned i = 0; i < 5; ++i) {
          const auto& value = item["wire_policy"][i];
          if (!value.IsNumber() || value.GetDouble() != (i == 4 ? star.policy.dbu : star.policy.per_um[i])) {
            throw std::invalid_argument("checkpoint wire policy differs from replay");
          }
        }
        pl::Result snapshot;
        snapshot.graph      = source;
        snapshot.floorplan  = ph::read_def(string(item, "frame"));
        snapshot.parasitics = std::make_shared<ph::Parasitics>(ph::deserialize_parasitics_exact(read(string(item, "parasitics"))));
        snapshot.pin_locations = read_locations(string(item, "pin_locations"));
        pl::Gp_observation sample;
        sample.iteration = integer(item, "iteration", 0);
        if (sample.iteration <= last) {
          throw std::invalid_argument("checkpoint iterations must increase");
        }
        last                         = sample.iteration;
        sample.density_overflow      = number(item, "density_overflow");
        sample.hpwl_um               = number(item, "hpwl_um");
        sample.opaque_route_overflow = number(item, "opaque_route_overflow");
        options.gp_observer(snapshot, sample);
      }
      diagnostic_summary("checkpoint_replay");
      return 0;
    }
    pl::Result placed;
    try {
      placed = pl::place(*source, placed_library, options);
    } catch (const Capture_complete&) {
      diagnostic_summary("checkpoint_capture_only");
      return 0;
    }
    verify_signal_frame(solver_frame, placed.floorplan, nets);
    pl::check_legality(technology, placed.floorplan);
    // Restore original names and isolated-net records after checking the
    // complete connected partition. No signal edge or cell is changed.
    placed.floorplan.nets      = original.nets;
    placed.floorplan.terminals = original.terminals;
    placed.floorplan.rows      = original.rows;
    placed.floorplan.blockages = export_blockages;
    ph::write_def(placed.floorplan, (root / "out.def").string());
    ph::verify_def_roundtrip(placed.floorplan, ph::read_def((root / "out.def").string()));
    write(root / "native_rc.json", ph::serialize_parasitics(*placed.parasitics));
    write(root / "qor.json", placed.qor + '\n');
    std::ofstream                                report(root / "summary.json");
    rapidjson::OStreamWrapper                    stream(report);
    rapidjson::Writer<rapidjson::OStreamWrapper> writer(stream);
    writer.StartObject();
    writer.Key("mode");
    writer.String(native_forces ? "exact_netlist_native_pin_force_gp_experiment"
                                : "exact_netlist_unweighted_cell_gp_native_observations");
    writer.Key("optimizer_feedback_enabled");
    writer.Bool(native_forces);
    if (native_forces) {
      writer.Key("phase_controller_integrated");
      writer.Bool(false);
      writer.Key("continuing_rollback_integrated");
      writer.Bool(native_rollback);
      writer.Key("native_legal_nonregression_checked");
      writer.Bool(legal_output_guard);
      writer.Key("accepted_for_optimization");
      writer.Bool(false);
    }
    writer.Key("synthesis_resynthesis");
    writer.Bool(false);
    writer.Key("io_locations_frozen_for_gp");
    writer.Bool(true);
    writer.Key("signal_instances");
    writer.Uint64(original.components.size());
    writer.Key("placement_blockages");
    writer.Uint64(original.blockages.size());
    writer.Key("solver_placement_blockages");
    writer.Uint64(solver_frame.blockages.size());
    writer.Key("fixed_occupancy_rectangles");
    writer.Uint64(fixed_occupancy_rectangles);
    writer.Key("original_row_segments");
    writer.Uint64(original.rows.size());
    writer.Key("solver_rows");
    writer.Uint64(solver_frame.rows.size());
    writer.Key("samples");
    writer.Uint(samples);
    writer.Key("virtual_filler_fraction");
    writer.Double(options.virtual_filler_fraction);
    writer.Key("minimum_gp_iterations");
    writer.Int(options.min_iterations);
    writer.Key("maximum_gp_iterations");
    writer.Int(options.iterations);
    writer.Key("setup_seconds");
    writer.Double(imported);
    writer.Key("timing_seconds");
    writer.Double(timing_seconds);
    writer.Key("total_seconds");
    writer.Double(elapsed());
    writer.Key("legalized");
    writer.Bool(true);
    writer.EndObject();
    report << '\n';
    report.close();
    if (!report) {
      throw std::runtime_error("cannot write GP observation summary");
    }
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "native GP observation failed: " << error.what() << '\n';
    return 1;
  }
}

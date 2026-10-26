// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "domino_timing_model.hpp"

#include <array>
#include <cmath>
#include <format>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <utility>

#include "domino_bundle.hpp"
#include "hash_util.hpp"
#include "json_reader.hpp"
#include "rapidjson/document.h"

namespace livehd::domplace::timing {
namespace {
namespace model   = liberty::timing;
namespace fs      = std::filesystem;
using Coefficient = std::optional<double>                         Stage_values::*;
// Stage_values in report order, named as the sidecar's stage_model keys.
constexpr std::array<std::pair<std::string_view, Coefficient>, 6> kCoefficients{
    {
     {"setup_eval_ps", &Stage_values::setup_eval_ps},
     {"uncertainty_first_ps", &Stage_values::uncertainty_first_ps},
     {"uncertainty_second_ps", &Stage_values::uncertainty_second_ps},
     {"eval_delay_ps", &Stage_values::eval_delay_ps},
     {"precharge_ps", &Stage_values::precharge_ps},
     {"precharge_margin_ps", &Stage_values::precharge_margin_ps},
     }
};
Coefficient coefficient(std::string_view name) {
  for (const auto& [key, member] : kCoefficients) {
    if (key == name) {
      return member;
    }
  }
  return nullptr;
}
using json::read_estimate;
using json::read_text;
model::Table scalar(double value) { return {model::Variable::none, model::Variable::none, {}, {}, {value}}; }
// A canonical text image of the generated cells for the model generation.
void         image(std::string& out, const model::Table& table) {
  out += std::format("t{} {}", static_cast<int>(table.x_variable), static_cast<int>(table.y_variable));
  for (const auto* values : {&table.x, &table.y, &table.values}) {
    out += "|";
    for (const auto value : *values) {
      out += std::format("{:a},", value);
    }
  }
  out += ";";
}
void image(std::string& out, const model::Cell& cell) {
  out += "cell " + cell.name + (cell.sequential ? " seq" : "") + "\n";
  for (const auto& threshold : {cell.thresholds.input, cell.thresholds.output, cell.thresholds.lower, cell.thresholds.upper}) {
    out += std::format("{:a} {:a} ", threshold[0], threshold[1]);
  }
  out += std::format("{:a}\n", cell.thresholds.slew_derate);
  for (const auto& issue : cell.unsupported) {
    out += "unsupported " + issue + "\n";
  }
  for (const auto& [name, pin] : cell.pins) {
    out += std::format("pin {} {} {}", name, pin.direction, pin.clock);
    for (const auto& value : {pin.capacitance_ff[0], pin.capacitance_ff[1], pin.max_capacitance_ff, pin.max_transition_ps}) {
      out += value ? std::format(" {:a}", *value) : std::string(" -");
    }
    out += "\n";
    for (const auto& arc : pin.arcs) {
      out += std::format("arc {} {} {} {}:", arc.related_pin, arc.type, arc.when, static_cast<int>(arc.sense));
      for (const auto* tables : {&arc.delay, &arc.slew, &arc.constraint}) {
        for (const auto& table : *tables) {
          if (table) {
            image(out, *table);
          } else {
            out += "-;";
          }
        }
      }
      out += "\n";
    }
  }
}
}  // namespace

const Domino_timing_view::Variant* Domino_timing_view::find(std::string_view name) const {
  for (const auto& variant : variants) {
    if (variant.name == name) {
      return &variant;
    }
  }
  return nullptr;
}

std::optional<Domino_timing_view> read_domino_timing(const fs::path& bundle, std::string* why) {
  const auto refuse = [&](std::string reason) -> std::optional<Domino_timing_view> {
    if (why) {
      *why = "domino bundle " + bundle.string() + ": " + std::move(reason);
    }
    return std::nullopt;
  };
  // The placement view validates the schema, files, masters and variants.
  const auto sidecar = read_domino_sidecar(bundle, why);
  if (!sidecar) {
    return std::nullopt;
  }
  std::ifstream       file(bundle / kBundleSidecar, std::ios::binary);
  const std::string   contents{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
  rapidjson::Document doc;
  doc.Parse(contents.c_str(), contents.size());
  if (doc.HasParseError() || !doc.IsObject()) {
    return refuse("unreadable sidecar");
  }
  bool               malformed = false;
  Domino_timing_view view;
  view.bundle_schema = kBundleSchema;
  view.digest        = std::format("{:016x}", hash_util::fnv1a64(contents));
  view.producer_salt = read_text(doc, "producer_salt", malformed);
  if (!doc.HasMember("model_version") || !doc["model_version"].IsUint() || !doc.HasMember("stage_model")
      || !doc["stage_model"].IsObject() || !doc["stage_model"].HasMember("version") || !doc["stage_model"]["version"].IsUint()) {
    return refuse("sidecar has no model or stage-model version");
  }
  view.model_version = doc["model_version"].GetUint();
  const auto& stage  = doc["stage_model"];
  view.stage_version = stage["version"].GetUint();
  view.clock         = read_text(stage, "clock", malformed);
  for (const auto& [name, member] : kCoefficients) {
    view.stage.*member = read_estimate(stage, std::string(name).c_str(), malformed);
    view.sources.emplace(name, "bundle");
  }
  if (doc.HasMember("corner")) {
    view.corner = read_text(doc["corner"], "library", malformed);
  }
  if (doc.HasMember("fo4_ps")) {
    view.fo4_ps = read_estimate(doc["fo4_ps"], "value", malformed);
  }
  if (doc.HasMember("units") && doc["units"].IsObject() && doc["units"].HasMember("dbu") && doc["units"]["dbu"].IsInt64()) {
    view.dbu = doc["units"]["dbu"].GetInt64();
  }
  std::map<std::string, std::pair<std::string, std::string>> register_pins;  // name -> (clk, q)
  if (!doc.HasMember("reference") || !doc["reference"].IsObject() || !doc["reference"].HasMember("registers")
      || !doc["reference"]["registers"].IsArray()) {
    return refuse("sidecar has no reference registers");
  }
  for (const auto& r : doc["reference"]["registers"].GetArray()) {
    if (!r.IsObject() || !r.HasMember("pins")) {
      return refuse("reference register without pin names");
    }
    register_pins[read_text(r, "name", malformed)] = {read_text(r["pins"], "clk", malformed), read_text(r["pins"], "q", malformed)};
  }
  size_t entry = 0;
  for (const auto& master : doc["masters"].GetArray()) {
    for (const auto& item : master["variants"].GetArray()) {
      const auto& catalog      = sidecar->catalog.at(entry++);
      auto&       variant      = view.variants.emplace_back();
      variant.name             = catalog.name;
      variant.family           = catalog.family;
      variant.rails            = catalog.rails;
      variant.rail_ff          = catalog.rail_ff;
      variant.clock_ff         = catalog.clock_ff;
      variant.a_footprint_um2  = catalog.a_footprint_um2;
      variant.a_estimate_um2   = catalog.a_estimate_um2;
      variant.register_cell    = catalog.register_cell;
      variant.setup_capture_ps = read_estimate(item, "setup_capture_ps", malformed);
      const auto pins          = register_pins.find(catalog.register_cell);
      if (pins == register_pins.end()) {
        return refuse("variant " + catalog.name + " names an unlisted register " + catalog.register_cell);
      }
      std::tie(variant.clk_pin, variant.q_pin) = pins->second;
    }
  }
  for (const auto& instance : doc["instances"].GetArray()) {
    auto& owner  = view.instances.emplace_back();
    owner.region = read_text(instance, "region", malformed);
    owner.state  = read_text(instance, "state", malformed);
    owner.master = read_text(instance, "master", malformed);
    if (!instance.IsObject() || !instance.HasMember("state_index") || !instance["state_index"].IsUint()) {
      malformed = true;
    } else {
      owner.state_index = instance["state_index"].GetUint();
    }
  }
  if (malformed) {
    return refuse("malformed timing fields in the sidecar");
  }
  return view;
}

bool Model_overrides::empty() const {
  return stage_ps.empty() && !eval_fo4 && !precharge_fo4 && !setup_eval_fo4 && !precharge_margin_fo4;
}

void apply_overrides(Domino_timing_view& view, const Model_overrides& overrides) {
  std::map<std::string, double> values;
  for (const auto& [name, value] : overrides.stage_ps) {
    if (!coefficient(name)) {
      throw std::invalid_argument("unknown stage coefficient override: " + name);
    }
    values[name] = value;
  }
  for (const auto& [multiple, name] : {
           std::pair{            overrides.eval_fo4,       "eval_delay_ps"},
           std::pair{       overrides.precharge_fo4,        "precharge_ps"},
           std::pair{      overrides.setup_eval_fo4,       "setup_eval_ps"},
           std::pair{overrides.precharge_margin_fo4, "precharge_margin_ps"}
  }) {
    if (!multiple) {
      continue;
    }
    if (!view.fo4_ps) {
      throw std::invalid_argument(std::string("an FO4 override of ") + name + " needs the bundle's FO4 estimate");
    }
    if (!std::isfinite(*multiple) || !(*multiple > 0)) {
      throw std::invalid_argument(std::string("FO4 override of ") + name + " must be a positive multiple");
    }
    if (!values.emplace(name, *multiple * *view.fo4_ps).second) {
      throw std::invalid_argument(std::string("conflicting overrides of ") + name);
    }
  }
  for (const auto& [name, value] : values) {
    if (!std::isfinite(value) || value < 0) {
      throw std::invalid_argument("stage coefficient override " + name + " must be a finite non-negative ps value");
    }
    view.stage.*coefficient(name) = value;
    view.sources[name]            = "override";
  }
}

std::vector<std::pair<std::string, std::optional<double>>> stage_coefficients(const Stage_values& stage) {
  std::vector<std::pair<std::string, std::optional<double>>> out;
  for (const auto& [name, member] : kCoefficients) {
    out.emplace_back(name, stage.*member);
  }
  return out;
}

std::shared_ptr<const late::Provisional_cells> domino_timing_cells(const Domino_timing_view& view, const model::Library& base,
                                                                   uint64_t* generation, std::vector<std::string>* diagnostics) {
  const auto& interface = kDominoPins;
  auto        cells     = std::make_shared<late::Provisional_cells>();
  cells->label          = std::format("provisional domino timing (stage model v{}, model v{}, bundle {})",
                             view.stage_version,
                             view.model_version,
                             view.digest);
  const auto note       = [&](std::string text) {
    if (diagnostics) {
      diagnostics->push_back(std::move(text));
    }
  };
  std::string missing;
  for (const auto& [name, value] : {
           std::pair{       "setup_eval_ps",        view.stage.setup_eval_ps},
           std::pair{"uncertainty_first_ps", view.stage.uncertainty_first_ps}
  }) {
    if (!value) {
      missing += std::string(missing.empty() ? "" : ", ") + name;
    }
  }
  if (!missing.empty()) {
    note("rail capture is unmeasured: " + missing + " null");
  }
  std::string generation_image = view.digest + "\n";
  for (const auto& [name, value] : stage_coefficients(view.stage)) {
    generation_image += name + (value ? std::format(" {:a}\n", *value) : std::string(" -\n"));
  }
  for (const auto& variant : view.variants) {
    model::Cell cell;
    cell.name       = variant.name;
    cell.library    = "provisional";
    cell.area       = variant.a_estimate_um2;
    cell.sequential = true;
    if (!view.stage.setup_eval_ps) {
      cell.unsupported.push_back("domino setup_eval_ps is null (unmeasured)");
    }
    if (!view.stage.uncertainty_first_ps) {
      cell.unsupported.push_back("domino uncertainty_first_ps is null (unmeasured)");
    }
    for (const auto& rail : variant.rails) {
      model::Pin pin;
      pin.name      = rail;
      pin.direction = "input";
      if (variant.rail_ff) {
        pin.capacitance_ff = {variant.rail_ff, variant.rail_ff};
      }
      model::Arc setup;
      setup.related_pin = interface.clock;
      setup.type        = "setup_rising";
      if (view.stage.setup_eval_ps) {
        setup.constraint = {scalar(*view.stage.setup_eval_ps), scalar(*view.stage.setup_eval_ps)};
      }
      pin.arcs.push_back(std::move(setup));
      cell.pins.emplace(rail, std::move(pin));
    }
    model::Pin clock;
    clock.name      = interface.clock;
    clock.direction = "input";
    clock.clock     = true;
    if (variant.clock_ff) {
      clock.capacitance_ff = {variant.clock_ff, variant.clock_ff};
    }
    cell.pins.emplace(interface.clock, std::move(clock));
    model::Pin output;
    output.name      = interface.output;
    output.direction = "output";
    const auto reg   = base.cells.find(variant.register_cell);
    if (reg == base.cells.end()) {
      cell.unsupported.push_back("register " + variant.register_cell + " is not in the Liberty timing model");
    } else if (const auto q = reg->second.pins.find(variant.q_pin); q == reg->second.pins.end()) {
      cell.unsupported.push_back("register " + variant.register_cell + " has no output pin " + variant.q_pin);
    } else {
      cell.thresholds           = reg->second.thresholds;
      output.max_capacitance_ff = q->second.max_capacitance_ff;
      output.max_transition_ps  = q->second.max_transition_ps;
      for (auto arc : q->second.arcs) {
        if (arc.type == "rising_edge" && arc.related_pin == variant.clk_pin) {
          arc.related_pin = interface.clock;
          output.arcs.push_back(std::move(arc));
        }
      }
      if (output.arcs.empty()) {
        cell.unsupported.push_back("register " + variant.register_cell + " has no rising_edge arc from " + variant.clk_pin);
      }
    }
    cell.pins.emplace(interface.output, std::move(output));
    image(generation_image, cell);
    cells->capture[variant.name] = {.fraction = .5, .uncertainty_ps = view.stage.uncertainty_first_ps};
    cells->library.cells.emplace(variant.name, std::move(cell));
  }
  if (!view.variants.empty()) {
    note(
        "domino Q timing copies each variant register's clock-to-Q arcs (labeled estimate); rail and CLK capacitances are "
        "the bundle's labeled estimates");
  }
  if (generation) {
    *generation = hash_util::fnv1a64(generation_image);
  }
  return cells;
}
}  // namespace livehd::domplace::timing

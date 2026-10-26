// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "domino_bundle.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <format>
#include <fstream>
#include <iterator>
#include <set>
#include <stdexcept>

#include "domplace_salt.hpp"
#include "graph_library_singleton.hpp"
#include "inou/lefdef/lef_writer.hpp"
#include "json_writer.hpp"
#include "rapidjson/document.h"

namespace livehd::domplace {
namespace {
namespace ph = physical;
namespace fs = std::filesystem;
using json::number;
using json::real;
using json::text;
using json::Writer;

std::optional<double> positive(double value) { return value > 0 ? std::optional<double>{value} : std::nullopt; }

// LEF footprint of a named cell in um^2, absent when LEF lacks it.
std::optional<double> lef_area(const ph::Technology& tech, const std::string& name) {
  const auto it = tech.macros.find(name);
  return it == tech.macros.end() ? std::nullopt : std::optional<double>{it->second.width * it->second.height};
}

// The provisional model is expressed in the LEF footprint (placement) basis and
// the Liberty basis (selection); a technology whose two areas disagree distorts
// one of them, so a difference above 1% is reported.
void compare_areas(const std::string& cell, double liberty, std::optional<double> lef, std::vector<std::string>& diagnostics) {
  if (!lef) {
    diagnostics.push_back(std::format("{} has no LEF footprint", cell));
  } else if (std::abs(*lef - liberty) > 0.01 * std::max(*lef, liberty)) {
    diagnostics.push_back(
        std::format("LEF ({:.6g} um^2) and Liberty ({:.6g}) areas of {} differ by more than 1%", *lef, liberty, cell));
  }
}

void write_file(const fs::path& path, std::string_view contents) {
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  file.write(contents.data(), static_cast<std::streamsize>(contents.size()));
  file.close();
  if (!file) {
    throw std::runtime_error("cannot write " + path.string());
  }
}

void drive(Writer& w, const std::optional<liberty::Output_drive>& d) {
  w.Key("drive");
  if (!d) {
    w.Null();
    return;
  }
  w.StartObject();
  text(w, "source", "register family output model (Liberty sample)");
  real(w, "intrinsic_ps", d->intrinsic_ps);
  real(w, "resistance_ohm", d->resistance_ohm);
  real(w, "max_capacitance_ff", d->max_capacitance_ff);
  real(w, "max_transition_ps", d->max_transition_ps);
  w.EndObject();
}

struct Variant {
  usyn::Physical_cell_interface cell;
  Domino_variant_geometry       geometry;
  Access_check                  access;
};
}  // namespace

std::optional<Bundle_design> collect_domino_design(const usyn::Logical_design& design, usyn::Budget& work) {
  Bundle_design                 out;
  std::map<std::string, size_t> masters;
  for (const auto& region : design.regions) {
    if (!region.physical) {
      continue;
    }
    for (const auto& s : region.physical->states) {
      if (s.decision != usyn::Physical_decision::domino) {
        continue;
      }
      const auto& cell = region.physical->cells.at(s.cell);
      if (!s.formula || !region.selected.frozen) {
        throw std::invalid_argument("converted domino state without a formula: " + cell.name);
      }
      auto master = make_domino_master(*s.formula, static_cast<uint32_t>(std::bit_width(s.metrics.support)), work);
      if (!master) {
        return std::nullopt;
      }
      if (!cell.name.starts_with(master->base + "_")) {
        throw std::logic_error("domino master changed between selection and bundle: " + cell.name);
      }
      if (masters.emplace(master->base, out.masters.size()).second) {
        out.masters.push_back(std::move(*master));
      }
      out.instances.push_back({region.module_name, region.selected.frozen->state.at(s.state).name, cell.name, s.state});
    }
  }
  std::sort(out.masters.begin(), out.masters.end(), [](const auto& a, const auto& b) { return a.base < b.base; });
  return out;
}

Bundle_result generate_domino_bundle(const Bundle_design& design, const Bundle_inputs& inputs, const fs::path& out_dir) {
  Bundle_result   result;
  std::error_code ec;
  fs::remove_all(out_dir, ec);
  try {
    if (!inputs.reference || inputs.reference->registers.empty() || !inputs.geometry.tech) {
      throw std::invalid_argument("a domino bundle needs the reference cells and the technology");
    }
    const auto& reference = *inputs.reference;
    const auto& tech      = *inputs.geometry.tech;
    auto&       notes     = result.diagnostics;
    const auto  inv_macro = tech.macros.find(inputs.geometry.inverter);
    if (inputs.geometry.inverter != reference.inverter.name || inv_macro == tech.macros.end()
        || !tech.sites.contains(inv_macro->second.site)) {
      throw std::invalid_argument("the reference inverter " + reference.inverter.name
                                  + " must be the geometry inverter on a LEF site");
    }
    const auto& site = inv_macro->second.site;
    fs::create_directories(out_dir);

    // FO4 of the reference inverter, a labeled estimate: its first-slew
    // intrinsic delay plus its drive resistance into four of its own inputs.
    const auto&           inverter = reference.inverter;
    std::optional<double> c_in, fo4;
    if (!inverter.input_capacitance_ff.empty()) {
      c_in = positive(inverter.input_capacitance_ff.front());
    }
    if (c_in && inverter.resistance_ohm > 0) {
      fo4 = inverter.intrinsic_ps + inverter.resistance_ohm * 4 * *c_in * 1e-3;  // ohm * fF = 1e-3 ps
    } else {
      notes.push_back("fo4_ps: " + inverter.name + " has no Liberty input capacitance or drive resistance");
    }
    auto stage = inputs.stage;
    if (!stage.setup_capture_ps) {
      stage.setup_capture_ps = reference.registers.front().setup_ps;
    }
    if (inputs.eval_fo4 && fo4) {
      stage.eval_delay_ps = *inputs.eval_fo4 * *fo4;
    }
    if (inputs.precharge_fo4 && fo4) {
      stage.precharge_ps = *inputs.precharge_fo4 * *fo4;
    }
    std::string missing;
    for (const auto& [name, value] : {
             std::pair{        "setup_eval_ps",         stage.setup_eval_ps},
             {     "setup_capture_ps",      stage.setup_capture_ps},
             {      "hold_capture_ps",       stage.hold_capture_ps},
             {         "precharge_ps",          stage.precharge_ps},
             {  "precharge_margin_ps",   stage.precharge_margin_ps},
             { "uncertainty_first_ps",  stage.uncertainty_first_ps},
             {"uncertainty_second_ps", stage.uncertainty_second_ps},
             {        "eval_delay_ps",         stage.eval_delay_ps}
    }) {
      if (!value) {
        missing += std::string(missing.empty() ? "" : ", ") + name;
      }
    }
    if (!missing.empty()) {
      notes.push_back("stage_model coefficients are null (not characterized): " + missing);
    }
    compare_areas(inverter.name, inverter.area, lef_area(tech, inverter.name), notes);
    for (const auto& r : reference.registers) {
      compare_areas(r.name, r.area, lef_area(tech, r.name), notes);
      if (!r.hold_ps) {
        notes.push_back("hold_ps of " + r.name + " is null: the register family has no hold sample");
      }
    }

    // Abstracts, access checks and models of every master and variant.
    std::vector<std::vector<Variant>> variants(design.masters.size());
    std::map<std::string, ph::Macro>  macros;
    std::set<std::string>             unsupported;
    std::string                       verilog
        = std::format("// Provisional domino models (model v{}): cycle-level next state only.\n", kDominoModelVersion);
    const auto models = (out_dir / kBundleModels).string();
    {
      Hhds_graph_library::Scoped_instance scope(models);
      auto&                               library = Hhds_graph_library::instance(models);
      for (size_t m = 0; m < design.masters.size(); ++m) {
        const auto& master  = design.masters[m];
        const auto  metrics = master.formula.metrics();
        if (!metrics) {
          throw std::invalid_argument("invalid domino master formula: " + master.base);
        }
        for (size_t v = 0; v < reference.registers.size(); ++v) {
          auto& variant    = variants[m].emplace_back();
          variant.cell     = domino_interface(master, v);
          variant.geometry = generate_domino_macro(variant.cell, *metrics, reference, v, inputs.geometry);
          variant.access   = check_domino_access(inputs.geometry, variant.geometry.macro, variant.geometry.halves);
          if (!variant.access.failures.empty()) {
            result.reason = std::format("{}: pin access check failed: {}", variant.cell.name, variant.access.failures.front());
            fs::remove_all(out_dir, ec);
            return result;
          }
          unsupported.insert(variant.access.unsupported.begin(), variant.access.unsupported.end());
          macros.emplace(variant.cell.name, variant.geometry.macro);
          emit_domino_model(library, master, variant.cell);
          verilog += domino_verilog_model(master, variant.cell);
        }
      }
      Hhds_graph_library::save(models);
    }
    for (const auto& rule : unsupported) {
      notes.push_back("pin access unverified (the access engine lacks a technology rule): " + rule);
    }
    write_file(out_dir / kBundleLef, ph::write_lef_macros(macros, tech.dbu));
    write_file(out_dir / kBundleVerilog, verilog);
    const auto row_access = inputs.geometry.row_access.identity();
    write_file(out_dir / kBundleAccess, row_access);

    rapidjson::StringBuffer buffer;
    Writer                  w(buffer);
    w.StartObject();
    number(w, "schema", kBundleSchema);
    number(w, "model_version", kDominoModelVersion);
    text(w, "profile", "domino_provisional");
    text(w, "estimate", "provisional model; labeled estimates, not characterized PPA");
    text(w, "producer_salt", std::format("{:016x}", kDomplaceSrcSalt));
    w.Key("files");
    w.StartObject();
    text(w, "lef", kBundleLef);
    text(w, "verilog", kBundleVerilog);
    text(w, "models", kBundleModels);
    text(w, "row_access", kBundleAccess);
    w.EndObject();

    w.Key("stage_model");
    w.StartObject();
    number(w, "version", stage.version);
    text(w, "clock", stage.clock);
    real(w, "setup_eval_ps", stage.setup_eval_ps);
    real(w, "setup_capture_ps", stage.setup_capture_ps);
    real(w, "hold_capture_ps", stage.hold_capture_ps);
    real(w, "precharge_ps", stage.precharge_ps);
    real(w, "precharge_margin_ps", stage.precharge_margin_ps);
    real(w, "uncertainty_first_ps", stage.uncertainty_first_ps);
    real(w, "uncertainty_second_ps", stage.uncertainty_second_ps);
    real(w, "eval_delay_ps", stage.eval_delay_ps);
    text(w, "storage_init", std::string(1, stage.storage_init));
    w.EndObject();

    w.Key("units");
    w.StartObject();
    text(w, "time", "ps");
    text(w, "capacitance", "fF");
    text(w, "resistance", "ohm");
    text(w, "length", "um");
    text(w, "area", "um2 (LEF footprints); Liberty area units for liberty_area");
    number(w, "dbu", static_cast<uint64_t>(tech.dbu));
    w.EndObject();
    w.Key("corner");
    w.StartObject();
    text(w, "library", reference.corner);
    w.EndObject();

    w.Key("technology");
    w.StartObject();
    text(w, "route_policy", inputs.technology);
    text(w, "pin_layer", inputs.geometry.access.pin);
    text(w, "escape_layer", inputs.geometry.access.escape);
    text(w, "cut_layer", inputs.geometry.access.cut);
    text(w, "vertical_layer", inputs.geometry.vertical_layer);
    text(w, "site", site);
    real(w, "row_height_um", tech.sites.at(site).height);
    real(w, "site_width_um", tech.sites.at(site).width);
    w.EndObject();
    w.Key("row_access");
    w.RawValue(row_access.data(), row_access.size(), rapidjson::kObjectType);
    number(w, "access_rule_version", kAccessRuleVersion);

    w.Key("reference");
    w.StartObject();
    w.Key("inverter");
    w.StartObject();
    text(w, "name", inverter.name);
    text(w, "source", reference.inverter_source);
    real(w, "liberty_area", inverter.area);
    real(w, "lef_area_um2", lef_area(tech, inverter.name));
    real(w, "input_capacitance_ff", c_in);
    real(w, "intrinsic_ps", positive(inverter.intrinsic_ps));
    real(w, "resistance_ohm", positive(inverter.resistance_ohm));
    w.EndObject();
    w.Key("registers");
    w.StartArray();
    for (const auto& r : reference.registers) {
      w.StartObject();
      text(w, "name", r.name);
      text(w, "source", reference.register_source);
      real(w, "liberty_area", r.area);
      real(w, "lef_area_um2", lef_area(tech, r.name));
      real(w, "clk_to_q_ps", r.clk_to_q_ps);
      real(w, "setup_ps", r.setup_ps);
      real(w, "hold_ps", r.hold_ps);
      json::flag(w, "q_inverted", r.q_inverted);
      drive(w, r.drive);
      w.EndObject();
    }
    w.EndArray();
    w.EndObject();
    w.Key("fo4_ps");
    w.StartObject();
    real(w, "value", fo4);
    text(w, "source", "labeled-estimate: intrinsic + R * 4 * C_in of " + inverter.name);
    w.EndObject();
    w.Key("coefficients");
    w.StartObject();
    number(w, "aux_nonregister", reference.aux_nonregister);
    real(w, "eval_fo4", inputs.eval_fo4);
    real(w, "precharge_fo4", inputs.precharge_fo4);
    w.EndObject();
    if (!inputs.eval_fo4 || !inputs.precharge_fo4) {
      notes.push_back("eval_fo4 and precharge_fo4 are hand-set coefficients; unset ones stay null");
    }

    w.Key("masters");
    w.StartArray();
    for (size_t m = 0; m < design.masters.size(); ++m) {
      const auto& master  = design.masters[m];
      const auto  metrics = *master.formula.metrics();
      w.StartObject();
      text(w, "base", master.base);
      text(w, "identity", std::format("{:016x}", master.identity));
      number(w, "inputs", master.function.inputs);
      w.Key("truth");
      w.StartArray();
      for (const auto word : master.function.words) {
        json::string(w, std::format("{:016x}", word));
      }
      w.EndArray();
      text(w, "next_state", domino_expression(master, variants[m].front().cell));
      w.Key("rails");
      w.StartArray();
      for (size_t k = 0; k < master.rails.size(); ++k) {
        w.StartObject();
        text(w, "pin", variants[m].front().cell.rails[k]);
        number(w, "input", master.rails[k].input);
        text(w, "polarity", master.rails[k].negative ? "negative" : "positive");
        w.EndObject();
      }
      w.EndArray();
      number(w, "n_pdn", metrics.transistors);
      number(w, "stack", metrics.stack);
      number(w, "branches", metrics.branches);
      w.Key("variants");
      w.StartArray();
      for (size_t v = 0; v < variants[m].size(); ++v) {
        const auto& [cell, g, access] = variants[m][v];
        w.StartObject();
        text(w, "name", cell.name);
        text(w, "register", reference.registers[v].name);
        real(w, "x", g.x);
        real(w, "a_not_um2", g.a_not_um2);
        real(w, "a_register_um2", g.a_register_um2);
        real(w, "a_estimate_um2", g.a_estimate_um2);
        real(w, "a_footprint_um2", g.a_footprint_um2);
        number(w, "w_area_dbu", static_cast<uint64_t>(g.w_area));
        number(w, "w_pin_dbu", static_cast<uint64_t>(g.w_pin));
        number(w, "width_dbu", static_cast<uint64_t>(g.width));
        number(w, "height_dbu", static_cast<uint64_t>(g.height));
        w.Key("halves");
        w.StartArray();
        for (const auto half : g.halves) {
          json::string(w, ph::row_access_name(half));
        }
        w.EndArray();
        w.Key("pin_caps");
        w.StartObject();
        real(w, "rail_ff", c_in);
        real(w, "clock_ff", std::nullopt);
        text(w, "source", "labeled-estimate: one reference-inverter input per rail; the clock load is not characterized");
        w.EndObject();
        drive(w, reference.registers[v].drive);
        w.Key("access_check");
        w.StartObject();
        json::flag(w, "ok", access.ok);
        json::flag(w, "verified", access.unsupported.empty());
        w.Key("unsupported");
        w.StartArray();
        for (const auto& rule : access.unsupported) {
          json::string(w, rule);
        }
        w.EndArray();
        number(w,
               "compatible_cases",
               static_cast<uint64_t>(
                   std::count_if(access.cases.begin(), access.cases.end(), [](const auto& c) { return c.compatible; })));
        number(w, "x_phases", access.x_phases);
        w.EndObject();
        w.EndObject();
      }
      w.EndArray();
      w.EndObject();
    }
    w.EndArray();

    w.Key("instances");
    w.StartArray();
    for (const auto& instance : design.instances) {
      w.StartObject();
      text(w, "region", instance.region);
      number(w, "state_index", instance.state_index);
      text(w, "state", instance.state);
      text(w, "master", instance.cell);
      w.EndObject();
    }
    w.EndArray();
    w.Key("provenance");
    w.StartArray();
    for (const auto& [input, digest] : inputs.provenance) {
      w.StartObject();
      text(w, "input", input);
      text(w, "digest", digest);
      w.EndObject();
    }
    w.EndArray();
    w.Key("diagnostics");
    w.StartArray();
    for (const auto& note : notes) {
      json::string(w, note);
    }
    w.EndArray();
    w.EndObject();
    write_file(out_dir / kBundleSidecar, std::string(buffer.GetString()) + "\n");
    result.macros = macros.size();
    result.ok     = true;
  } catch (const std::exception& error) {
    result.reason = error.what();
    fs::remove_all(out_dir, ec);
  }
  return result;
}

std::optional<Domino_sidecar> read_domino_sidecar(const fs::path& dir, std::string* why) {
  const auto refuse = [&](std::string reason) -> std::optional<Domino_sidecar> {
    if (why) {
      *why = "domino bundle " + dir.string() + ": " + reason;
    }
    return std::nullopt;
  };
  std::ifstream file(dir / kBundleSidecar);
  if (!file) {
    return refuse(std::format("cannot open {}", kBundleSidecar));
  }
  const std::string   contents{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
  rapidjson::Document doc;
  doc.Parse(contents.c_str(), contents.size());
  if (doc.HasParseError() || !doc.IsObject() || !doc.HasMember("schema") || !doc["schema"].IsUint()
      || doc["schema"].GetUint() != kBundleSchema) {
    return refuse(std::format("{} is not a schema-{} sidecar", kBundleSidecar, kBundleSchema));
  }
  Domino_sidecar sidecar;
  sidecar.lef = dir / kBundleLef;
  if (!fs::is_regular_file(sidecar.lef)) {
    return refuse(std::format("missing {}", kBundleLef));
  }
  try {
    sidecar.row_access = ph::read_row_access((dir / kBundleAccess).string());
  } catch (const std::exception& error) {
    return refuse(error.what());
  }
  if (!doc.HasMember("masters") || !doc["masters"].IsArray() || !doc.HasMember("instances") || !doc["instances"].IsArray()) {
    return refuse("sidecar has no masters or instances");
  }
  for (const auto& master : doc["masters"].GetArray()) {
    if (!master.IsObject() || !master.HasMember("variants") || !master["variants"].IsArray()) {
      return refuse("malformed master");
    }
    for (const auto& variant : master["variants"].GetArray()) {
      if (!variant.IsObject() || !variant.HasMember("name") || !variant["name"].IsString() || !variant.HasMember("halves")
          || !variant["halves"].IsArray() || variant["halves"].Size() != 2) {
        return refuse("malformed master variant");
      }
      std::array<ph::Row_access, 2> halves{};
      for (rapidjson::SizeType h = 0; h < 2; ++h) {
        const auto& half = variant["halves"][h];
        if (!half.IsString()) {
          return refuse("malformed row access template");
        }
        const std::string_view name{half.GetString(), half.GetStringLength()};
        if (name == ph::row_access_name(ph::Row_access::horizontal)) {
          halves[h] = ph::Row_access::horizontal;
        } else if (name == ph::row_access_name(ph::Row_access::vertical)) {
          halves[h] = ph::Row_access::vertical;
        } else {
          return refuse("unknown row access " + std::string(name));
        }
      }
      sidecar.templates.emplace(variant["name"].GetString(), halves);
    }
  }
  sidecar.instances = doc["instances"].Size();
  return sidecar;
}

}  // namespace livehd::domplace

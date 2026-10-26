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
#include <tuple>

#include "domplace_salt.hpp"
#include "graph_library_singleton.hpp"
#include "inou/lefdef/lef_writer.hpp"
#include "json_reader.hpp"
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
  size_t                        rung = 0;  // index into the reference register ladder
  std::string                   twin;      // the other rail template of this rung
};
// Row orientations a rail template's bottom rail matches (template_rails).
std::array<std::string_view, 2> start_orientations(uint32_t rail_template) {
  return rail_template ? std::array<std::string_view, 2>{"FS", "S"} : std::array<std::string_view, 2>{"N", "FN"};
}
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
    const auto& site      = inputs.geometry.footprint.site;  // checked when the footprint was resolved
    fs::create_directories(out_dir);

    // FO4 and tau of the reference inverter on its NLDM tables, and the
    // logical-effort estimate of every coefficient no option sets
    // (domino_delay.hpp). Without FO4 they stay null with a diagnostic.
    const auto&           inverter = reference.inverter;
    const auto            delay    = domino_delay_model(reference, inputs.delay);
    const auto            c_in     = delay.c_in_ff;
    std::optional<double> fo4;
    if (delay.fo4) {
      fo4 = delay.fo4->fo4_ps;
    } else {
      result.delay_issue = "fo4_ps is null: " + delay.issue + "; the stage delays it scales are null (unmeasured)";
      notes.push_back(result.delay_issue);
    }
    auto                               stage = inputs.stage;
    std::map<std::string, std::string> sources;
    // The base variant's; each variant records its own register's setup.
    if (!stage.setup_capture_ps) {
      stage.setup_capture_ps = reference.registers.front().setup_ps;
    }
    sources["setup_capture_ps"] = "register-liberty: the base variant's register setup; each variant records its own";
    sources["hold_capture_ps"]  = "unknown: setup-only model";
    const auto fraction
        = [&](const std::optional<double>& option, double fallback, const char* name, std::optional<double>& target) {
            target        = fo4 ? std::optional<double>{option.value_or(fallback) * *fo4} : std::nullopt;
            sources[name] = option ? "option" : std::string(kDelaySource);
          };
    fraction(inputs.setup_eval_fo4, inputs.delay.setup_eval_fo4, "setup_eval_ps", stage.setup_eval_ps);
    fraction(inputs.precharge_margin_fo4, inputs.delay.precharge_margin_fo4, "precharge_margin_ps", stage.precharge_margin_ps);
    // An unset uncertainty is the ideal clock, as the full-cycle
    // synth.place.stage_timing.clock_uncertainty_ps defaults to 0.
    for (const auto& [name, target] : {
             std::pair{ "uncertainty_first_ps",  &stage.uncertainty_first_ps},
             std::pair{"uncertainty_second_ps", &stage.uncertainty_second_ps}
    }) {
      sources[name] = *target ? "option" : "estimate: ideal clock (0 ps)";
      *target       = target->value_or(0);
    }
    sources["eval_delay_ps"] = inputs.eval_fo4 ? "option" : std::string(kDelaySource);
    sources["precharge_ps"]  = inputs.precharge_fo4 ? "option" : std::string(kDelaySource);
    // Every variant's CLK load, a labeled estimate: its register's CK input
    // plus the clocked domino devices (precharge, foot) at half an inverter
    // input each. Null when either input is unknown.
    std::vector<std::optional<double>> clock_ff;
    for (const auto& r : reference.registers) {
      clock_ff.push_back(r.clock_ff && c_in ? std::optional<double>{*r.clock_ff + reference.clock_devices / 2.0 * *c_in}
                                            : std::nullopt);
      if (!clock_ff.back()) {
        notes.push_back("pin_caps.clock_ff of the " + r.name + " variants is null: no register CK or inverter input capacitance");
      }
    }
    std::string missing;
    for (const auto& [name, value] : {
             std::pair{        "setup_eval_ps",         stage.setup_eval_ps},
             {     "setup_capture_ps",      stage.setup_capture_ps},
             {      "hold_capture_ps",       stage.hold_capture_ps},
             {  "precharge_margin_ps",   stage.precharge_margin_ps},
             { "uncertainty_first_ps",  stage.uncertainty_first_ps},
             {"uncertainty_second_ps", stage.uncertainty_second_ps}
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
    auto                              scratch = tech;  // every access check adds its master here
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
          // Both PG rail templates of the rung: one per start-row parity, the
          // same function, pins and drive.
          std::array<bool, 2> parity{};
          for (uint32_t rail_template = 0; rail_template < 2; ++rail_template) {
            auto& variant    = variants[m].emplace_back();
            variant.rung     = v;
            variant.cell     = domino_interface(master, v, rail_template);
            variant.twin     = domino_interface(master, v, 1 - rail_template).name;
            variant.geometry = generate_domino_macro(variant.cell, *metrics, reference, v, inputs.geometry, {}, rail_template);
            variant.access   = check_domino_access(inputs.geometry, variant.geometry.macro, variant.geometry.halves, scratch);
            if (!variant.access.failures.empty()) {
              result.reason = std::format("{}: pin access check failed: {}", variant.cell.name, variant.access.failures.front());
              fs::remove_all(out_dir, ec);
              return result;
            }
            for (int p = 0; p < 2; ++p) {
              parity[p] = parity[p] || variant.access.parity[p];
            }
            unsupported.insert(variant.access.unsupported.begin(), variant.access.unsupported.end());
            macros.emplace(variant.cell.name, variant.geometry.macro);
            result.footprints.emplace(variant.cell.name, variant.geometry.a_footprint_um2);
            emit_domino_model(library, master, variant.cell);
            verilog += domino_verilog_model(master, variant.cell);
          }
          for (int p = 0; p < 2; ++p) {
            if (!parity[p]) {
              result.reason
                  = std::format("{}: no PG rail template fits {} start rows", variants[m].back().cell.name, p ? "odd" : "even");
              fs::remove_all(out_dir, ec);
              return result;
            }
          }
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
    real(w, "precharge_margin_ps", stage.precharge_margin_ps);
    real(w, "uncertainty_first_ps", stage.uncertainty_first_ps);
    real(w, "uncertainty_second_ps", stage.uncertainty_second_ps);
    text(w, "storage_init", std::string(1, stage.storage_init));
    text(w, "per_variant", "eval_delay_ps and precharge_ps: masters[].variants[].timing");
    w.Key("sources");
    w.StartObject();
    for (const auto& [name, source] : sources) {
      text(w, name.c_str(), source);
    }
    w.EndObject();
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
    // The rail pattern every template derives from: the reference inverter's
    // PG pins on the bottom and top edges of an N row.
    w.Key("row_rails");
    w.StartObject();
    text(w, "source", "reference inverter " + inverter.name + " in an N row; flipped (FS, S) rows swap them");
    text(w, "bottom", inputs.geometry.footprint.rails[0].pin);
    text(w, "top", inputs.geometry.footprint.rails[1].pin);
    w.EndObject();
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
      w.Key("pins");
      w.StartObject();
      text(w, "d", r.d_pin);
      text(w, "clk", r.clk_pin);
      text(w, "q", r.q_pin);
      w.EndObject();
      real(w, "clock_capacitance_ff", r.clock_ff);
      drive(w, r.drive);
      w.EndObject();
    }
    w.EndArray();
    w.EndObject();
    w.Key("fo4_ps");
    w.StartObject();
    real(w, "value", fo4);
    text(w,
         "source",
         "liberty-nldm: " + inverter.name + " driving 4 x its input capacitance at a self-consistent input slew (delay_model.fo4)");
    w.EndObject();
    w.Key("delay_model");
    w.StartObject();
    text(w, "source", kDelaySource);
    text(w, "model", kDelayModel);
    text(w,
         "citation",
         "I. Sutherland, R. Sproull, D. Harris, Logical Effort: Designing Fast CMOS Circuits (1999), circuit families: "
         "domino gates");
    text(w, "formula", kDelayFormula);
    w.Key("parameters");
    w.StartObject();
    real(w, "p_inv", inputs.delay.p_inv);
    real(w, "precharge_width", inputs.delay.precharge_width);
    real(w, "internal_load", inputs.delay.internal_load);
    real(w, "internal_node_weight", inputs.delay.internal_node_weight);
    real(w, "setup_eval_fo4", inputs.delay.setup_eval_fo4);
    real(w, "precharge_margin_fo4", inputs.delay.precharge_margin_fo4);
    w.EndObject();
    real(w, "tau_ps", delay.tau_ps);
    real(w, "c_in_ff", c_in);
    w.Key("fo4");
    if (delay.fo4) {
      const auto& f = *delay.fo4;
      w.StartObject();
      text(w, "inverter", f.inverter);
      text(w, "arc", f.input_pin + "->" + f.output_pin);
      real(w, "load_ff", f.load_ff);
      real(w, "slew_rise_ps", f.slew_rise_ps);
      real(w, "slew_fall_ps", f.slew_fall_ps);
      real(w, "delay_rise_ps", f.delay_rise_ps);
      real(w, "delay_fall_ps", f.delay_fall_ps);
      real(w, "fo4_ps", f.fo4_ps);
      number(w, "iterations", f.iterations);
      text(w,
           "method",
           "each edge's input slew is the opposite output transition at 4 x C_in (fixed point); FO4 is the mean of rise and fall");
      w.EndObject();
    } else {
      w.Null();
    }
    if (!delay.fo4) {
      text(w, "issue", delay.issue);
    }
    w.EndObject();
    w.Key("coefficients");
    w.StartObject();
    number(w, "aux_nonregister", reference.aux_nonregister);
    number(w, "pair_area_pct", reference.pair_area_pct);
    number(w, "area_margin_pct", inputs.area_margin_pct);
    number(w, "clock_devices", reference.clock_devices);
    real(w, "eval_fo4", inputs.eval_fo4);
    real(w, "precharge_fo4", inputs.precharge_fo4);
    real(w, "setup_eval_fo4", inputs.setup_eval_fo4);
    real(w, "precharge_margin_fo4", inputs.precharge_margin_fo4);
    w.EndObject();
    if (!inputs.eval_fo4 || !inputs.precharge_fo4 || !inputs.setup_eval_fo4 || !inputs.precharge_margin_fo4) {
      notes.push_back(
          "stage delays not set by an FO4 option are the logical-effort estimate (delay_model), a labeled estimate, not "
          "characterized");
    }
    std::set<std::string> delay_issues;

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
      for (const auto& [cell, g, access, v, twin] : variants[m]) {
        w.StartObject();
        text(w, "name", cell.name);
        text(w, "register", reference.registers[v].name);
        number(w, "template", g.rail_template);
        w.Key("pg_rails");
        w.StartArray();
        for (const auto& pin : g.pg_rails) {
          json::string(w, pin);
        }
        w.EndArray();
        w.Key("start_orientations");
        w.StartArray();
        for (const auto o : start_orientations(g.rail_template)) {
          json::string(w, o);
        }
        w.EndArray();
        text(w, "rail_twin", twin);
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
        real(w, "clock_ff", clock_ff[v]);
        text(w,
             "source",
             std::format("labeled-estimate: one reference-inverter input per rail; CLK = the register's CK input + {}/2 "
                         "reference-inverter inputs (clocked precharge and foot devices)",
                         reference.clock_devices));
        w.EndObject();
        real(w, "setup_capture_ps", reference.registers[v].setup_ps);
        {
          // eval_delay_ps and precharge_ps: an FO4 option, else the estimate.
          std::string why;
          const auto  estimate = estimate_stage_delay(delay, metrics, reference.registers[v].d_ff, &why);
          if (!estimate && (!inputs.eval_fo4 || !inputs.precharge_fo4)) {
            delay_issues.insert(reference.registers[v].name + ": " + why);
          }
          const auto pick = [&](const std::optional<double>& option, double Stage_delay::* value) -> std::optional<double> {
            if (option) {
              return fo4 ? std::optional<double>{*option * *fo4} : std::nullopt;
            }
            return estimate ? std::optional<double>{*estimate.*value} : std::nullopt;
          };
          w.Key("timing");
          w.StartObject();
          real(w, "eval_delay_ps", pick(inputs.eval_fo4, &Stage_delay::eval_ps));
          real(w, "precharge_ps", pick(inputs.precharge_fo4, &Stage_delay::precharge_ps));
          real(w, "register_d_ff", reference.registers[v].d_ff);
          w.Key("estimate");
          if (estimate) {
            w.StartObject();
            real(w, "g_dynamic", estimate->g_dynamic);
            real(w, "h_dynamic", estimate->h_dynamic);
            real(w, "p_dynamic", estimate->p_dynamic);
            real(w, "h_output", estimate->h_output);
            real(w, "p_output", estimate->p_output);
            real(w, "c_dynamic", estimate->c_dynamic);
            real(w, "eval_tau", estimate->eval_tau);
            real(w, "precharge_tau", estimate->precharge_tau);
            real(w, "eval_ps", estimate->eval_ps);
            real(w, "precharge_ps", estimate->precharge_ps);
            w.EndObject();
          } else {
            w.Null();
          }
          w.EndObject();
        }
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
        json::flag(w, "even_starts", access.parity[0]);
        json::flag(w, "odd_starts", access.parity[1]);
        w.EndObject();
        w.EndObject();
      }
      w.EndArray();
      w.EndObject();
    }
    w.EndArray();

    for (const auto& issue : delay_issues) {
      const auto text = "eval_delay_ps/precharge_ps estimate is null for the " + issue;
      notes.push_back(text);
      if (result.delay_issue.empty()) {
        result.delay_issue = text;
      }
    }

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
  std::ifstream file(dir / kBundleSidecar);
  if (!file) {
    if (why) {
      *why = std::format("domino bundle {}: cannot open {}", dir.string(), kBundleSidecar);
    }
    return std::nullopt;
  }
  const std::string   contents{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
  rapidjson::Document doc;
  doc.Parse(contents.c_str(), contents.size());
  return read_domino_sidecar(doc, dir, why);
}

std::optional<Domino_sidecar> read_domino_sidecar(const rapidjson::Document& doc, const fs::path& dir, std::string* why) {
  const auto refuse = [&](std::string reason) -> std::optional<Domino_sidecar> {
    if (why) {
      *why = "domino bundle " + dir.string() + ": " + reason;
    }
    return std::nullopt;
  };
  if (doc.HasParseError() || !doc.IsObject() || !doc.HasMember("schema") || !doc["schema"].IsUint()) {
    return refuse(std::format("{} is not a schema-{} sidecar", kBundleSidecar, kBundleSchema));
  }
  if (const auto schema = doc["schema"].GetUint(); schema != kBundleSchema) {
    return refuse(
        std::format("{} is schema {}; this build reads schema {} only (regenerate the bundle with pass.usyn "
                    "physical=domino_provisional)",
                    kBundleSidecar,
                    schema,
                    kBundleSchema));
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
  bool       malformed = false;
  const auto estimate
      = [&](const rapidjson::Value& object, const char* key) { return json::read_estimate(object, key, malformed); };
  const auto string = [&](const rapidjson::Value& object, const char* key) { return json::read_text(object, key, malformed); };
  // Each variant's register resolves to its clock and output pin names.
  std::map<std::string, std::pair<std::string, std::string>> register_pins;  // name -> (clk, q)
  if (!doc.HasMember("reference") || !doc["reference"].IsObject() || !doc["reference"].HasMember("registers")
      || !doc["reference"]["registers"].IsArray()) {
    return refuse("sidecar has no reference registers");
  }
  for (const auto& r : doc["reference"]["registers"].GetArray()) {
    if (!r.IsObject() || !r.HasMember("pins")) {
      return refuse("reference register without pin names");
    }
    register_pins[string(r, "name")] = {string(r["pins"], "clk"), string(r["pins"], "q")};
  }
  for (const auto& master : doc["masters"].GetArray()) {
    if (!master.IsObject() || !master.HasMember("variants") || !master["variants"].IsArray() || !master.HasMember("rails")
        || !master["rails"].IsArray()) {
      return refuse("malformed master");
    }
    const auto               base = string(master, "base");
    std::vector<std::string> rails;
    for (const auto& rail : master["rails"].GetArray()) {
      rails.push_back(string(rail, "pin"));
    }
    std::map<std::string, uint32_t> ranks;  // by family: the register ladder order
    for (const auto& variant : master["variants"].GetArray()) {
      if (!variant.IsObject() || !variant.HasMember("name") || !variant["name"].IsString() || !variant.HasMember("halves")
          || !variant["halves"].IsArray() || variant["halves"].Size() != 2 || !variant.HasMember("width_dbu")
          || !variant["width_dbu"].IsUint64() || !variant.HasMember("pin_caps") || !variant.HasMember("drive")
          || !variant.HasMember("timing") || !variant["timing"].IsObject()) {
        return refuse("malformed master variant");
      }
      Catalog_entry entry;
      entry.name             = string(variant, "name");
      entry.register_cell    = string(variant, "register");
      entry.rails            = rails;
      entry.width_dbu        = static_cast<int64_t>(variant["width_dbu"].GetUint64());
      entry.a_footprint_um2  = estimate(variant, "a_footprint_um2").value_or(0);
      entry.a_estimate_um2   = estimate(variant, "a_estimate_um2").value_or(0);
      entry.rail_ff          = estimate(variant["pin_caps"], "rail_ff");
      entry.clock_ff         = estimate(variant["pin_caps"], "clock_ff");
      entry.setup_capture_ps = estimate(variant, "setup_capture_ps");
      entry.eval_delay_ps    = estimate(variant["timing"], "eval_delay_ps");
      entry.precharge_ps     = estimate(variant["timing"], "precharge_ps");
      if (const auto pins = register_pins.find(entry.register_cell); pins != register_pins.end()) {
        std::tie(entry.clk_pin, entry.q_pin) = pins->second;
      } else {
        return refuse("variant " + entry.name + " names an unlisted register " + entry.register_cell);
      }
      // <base>_v<k>_t<t>: the family keeps the base and the template.
      const auto tag = entry.name.rfind("_t");
      if (base.empty() || !entry.name.starts_with(base + "_v") || tag == std::string::npos) {
        return refuse("variant " + entry.name + " is not named after its master " + base);
      }
      entry.family = base + entry.name.substr(tag);
      entry.base   = base;
      entry.rank   = ranks[entry.family]++;
      // The other PG rail template; without one, starts of the other rail
      // parity are refused at placement.
      if (variant.HasMember("rail_twin")) {
        if (!variant["rail_twin"].IsString()) {
          return refuse("variant " + entry.name + " has a malformed rail twin");
        }
        sidecar.rail_twins.emplace(entry.name, variant["rail_twin"].GetString());
      }
      if (const auto& d = variant["drive"]; !d.IsNull()) {
        liberty::Output_drive model;
        model.sequential         = true;
        model.name               = entry.name;
        model.output             = "Q";
        model.intrinsic_ps       = estimate(d, "intrinsic_ps").value_or(0);
        model.resistance_ohm     = estimate(d, "resistance_ohm").value_or(0);
        model.max_capacitance_ff = estimate(d, "max_capacitance_ff");
        model.max_transition_ps  = estimate(d, "max_transition_ps");
        entry.drive              = model;
      }
      if (malformed) {
        return refuse("malformed variant " + entry.name);
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
      sidecar.catalog.push_back(std::move(entry));
    }
  }
  sidecar.instances = doc["instances"].Size();
  return sidecar;
}

}  // namespace livehd::domplace

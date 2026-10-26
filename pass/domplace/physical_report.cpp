// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "physical_report.hpp"

#include <format>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>

#include "json_writer.hpp"
#include "stage_model.hpp"

namespace livehd::domplace {
namespace {
using json::number;
using json::real;
using json::text;
using json::Writer;
}  // namespace

std::string physical_report(const usyn::Logical_design& design, const Physical_report_inputs& inputs) {
  if (!inputs.reference || !inputs.options) {
    throw std::invalid_argument("physical report needs the reference cells and the selection options");
  }
  const auto&             reference = *inputs.reference;
  rapidjson::StringBuffer buffer;
  Writer                  w(buffer);
  w.StartObject();
  number(w, "schema", 1);
  text(w, "profile", "domino_provisional");
  text(w, "emission", inputs.emitted ? "mixed" : "dry-run");
  text(w, "estimate", "liberty-area-proxy; not characterized PPA");
  number(w, "stage_model_version", kStageModelVersion);
  text(w, "select", inputs.options->policy == Select_options::Policy::all ? "all" : "area");
  json::flag(w, "identity", inputs.options->identity);
  text(w, "clock", inputs.options->clock);
  w.Key("reference");
  w.StartObject();
  text(w, "corner", reference.corner);
  w.Key("inverter");
  w.StartObject();
  text(w, "name", reference.inverter.name);
  text(w, "source", reference.inverter_source);
  real(w, "area", reference.inverter.area);
  w.EndObject();
  text(w, "register_source", reference.register_source);
  w.Key("registers");
  w.StartArray();
  for (const auto& r : reference.registers) {
    w.StartObject();
    text(w, "name", r.name);
    real(w, "area", r.area);
    real(w, "clk_to_q_ps", r.clk_to_q_ps);
    real(w, "setup_ps", r.setup_ps);
    real(w, "hold_ps", r.hold_ps);
    w.EndObject();
  }
  w.EndArray();
  number(w, "aux_nonregister", reference.aux_nonregister);
  w.EndObject();
  w.Key("cmos_register");
  w.StartObject();
  text(w, "name", inputs.cmos_register);
  real(w, "area", inputs.cmos_register_area);
  w.EndObject();
  number(w, "model_work", inputs.model_work);
  w.Key("bundle");
  if (inputs.bundle.empty()) {
    w.Null();
  } else {
    w.StartObject();
    text(w, "directory", inputs.bundle);
    number(w, "macros", inputs.bundle_macros);
    w.Key("diagnostics");
    w.StartArray();
    for (const auto& note : inputs.bundle_diagnostics) {
      json::string(w, note);
    }
    w.EndArray();
    w.EndObject();
  }

  uint64_t                        states = 0, eligible = 0, converted = 0;
  std::map<std::string, uint64_t> reasons;
  std::set<std::string>           masters;
  double                          estimate = 0, rails = 0, footprint = 0;
  bool                            rounded      = true;  // every converted cell has a bundle footprint
  const auto                      footprint_of = [&](const std::string& cell) -> std::optional<double> {
    const auto it = inputs.footprints.find(cell);
    return it == inputs.footprints.end() ? std::nullopt : std::optional<double>{it->second};
  };
  for (const auto& region : design.regions) {
    if (!region.physical) {
      continue;
    }
    for (const auto& s : region.physical->states) {
      ++states;
      eligible += s.eligible;
      ++reasons[s.reason];
      if (s.decision == usyn::Physical_decision::domino) {
        ++converted;
        const auto& cell = region.physical->cells.at(s.cell).name;
        masters.insert(cell);
        estimate        += s.a_domino;
        rails           += s.a_rail_not;
        const auto area  = footprint_of(cell);
        rounded         &= area.has_value();
        footprint       += area.value_or(0);
      }
    }
  }
  w.Key("totals");
  w.StartObject();
  number(w, "states", states);
  number(w, "eligible", eligible);
  number(w, "converted", converted);
  number(w, "retained_cmos", states - converted);
  number(w, "masters", masters.size());
  // Converted states over the definition regions: the Liberty-basis estimate,
  // the materialized rail inverters, and the rounded LEF footprint (null
  // without a bundle footprint for every cell).
  real(w, "a_domino_estimate", estimate);
  real(w, "a_rail_not", rails);
  real(w, "a_footprint_um2", rounded ? std::optional<double>{footprint} : std::nullopt);
  w.Key("reasons");
  w.StartObject();
  for (const auto& [reason, count] : reasons) {
    number(w, reason.c_str(), count);
  }
  w.EndObject();
  w.EndObject();

  w.Key("regions");
  w.StartArray();
  for (const auto& region : design.regions) {
    if (!region.physical) {
      continue;
    }
    w.StartObject();
    text(w, "module", region.module_name);
    w.Key("endpoints");
    w.StartArray();
    for (const auto& s : region.physical->states) {
      const bool domino = s.decision == usyn::Physical_decision::domino;
      w.StartObject();
      number(w, "state", s.state);
      text(w, "name", region.selected.frozen->state.at(s.state).name);
      text(w, "decision", domino ? "domino" : "retained_cmos");
      text(w, "reason", s.reason);
      w.Key("master");
      if (domino) {
        json::string(w, region.physical->cells.at(s.cell).name);
      } else {
        w.Null();
      }
      if (s.formula) {
        number(w, "n_pdn", s.metrics.transistors);
        number(w, "stack", s.metrics.stack);
        number(w, "branches", s.metrics.branches);
        real(w, "x", (s.metrics.transistors + reference.aux_nonregister) / 2.0);
        real(w, "a_domino", s.a_domino);
        real(w, "a_rail", s.a_rail_not);
        real(w, "a_cmos", s.a_cmos);
        if (domino) {
          real(w, "a_footprint_um2", footprint_of(region.physical->cells.at(s.cell).name));
        }
        w.Key("rails");
        w.StartArray();
        for (size_t k = 0; k < s.rails.size(); ++k) {
          w.StartObject();
          text(w, "pin", std::format("R{}", k));
          number(w, "input", s.rails[k].input);
          text(w, "polarity", s.rails[k].negative ? "negative" : "positive");
          number(w, "signal", s.rails[k].producer.id);
          json::flag(w, "inverted", s.rails[k].producer.inverted);
          w.EndObject();
        }
        w.EndArray();
      }
      w.EndObject();
    }
    w.EndArray();
    w.EndObject();
  }
  w.EndArray();
  w.EndObject();
  return buffer.GetString();
}

}  // namespace livehd::domplace

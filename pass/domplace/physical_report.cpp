// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "physical_report.hpp"

#include <format>
#include <map>
#include <set>
#include <stdexcept>

#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"
#include "stage_model.hpp"

namespace livehd::domplace {
namespace {
using Writer = rapidjson::Writer<rapidjson::StringBuffer>;
void text(Writer& w, const char* key, std::string_view value) {
  w.Key(key);
  w.String(value.data(), static_cast<rapidjson::SizeType>(value.size()));
}
void number(Writer& w, const char* key, uint64_t value) {
  w.Key(key);
  w.Uint64(value);
}
void real(Writer& w, const char* key, std::optional<double> value) {
  w.Key(key);
  if (value) {
    w.Double(*value);
  } else {
    w.Null();  // missing data is reported as such, never as zero
  }
}
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
  w.Key("identity");
  w.Bool(inputs.options->identity);
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

  uint64_t                        states = 0, eligible = 0, converted = 0;
  std::map<std::string, uint64_t> reasons;
  std::set<std::string>           masters;
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
        masters.insert(region.physical->cells.at(s.cell).name);
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
        const auto& name = region.physical->cells.at(s.cell).name;
        w.String(name.data(), static_cast<rapidjson::SizeType>(name.size()));
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
        w.Key("rails");
        w.StartArray();
        for (size_t k = 0; k < s.rails.size(); ++k) {
          w.StartObject();
          text(w, "pin", std::format("R{}", k));
          number(w, "input", s.rails[k].input);
          text(w, "polarity", s.rails[k].negative ? "negative" : "positive");
          number(w, "signal", s.rails[k].producer.id);
          w.Key("inverted");
          w.Bool(s.rails[k].producer.inverted);
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

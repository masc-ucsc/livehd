// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "domino_liberty.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <map>
#include <set>
#include <stdexcept>

namespace livehd::domplace::timing {
namespace {
namespace model = liberty::timing;

const char* variable_name(model::Variable variable) {
  switch (variable) {
    case model::Variable::input_slew: return "input_net_transition";
    case model::Variable::output_load: return "total_output_net_capacitance";
    case model::Variable::related_slew: return "related_pin_transition";
    case model::Variable::constrained_slew: return "constrained_pin_transition";
    case model::Variable::none: break;
  }
  return nullptr;
}

std::string number(double value) { return std::format("{:.9g}", value); }

std::string list(const std::vector<double>& values) {
  std::string out;
  for (const auto value : values) {
    out += (out.empty() ? "" : ", ") + number(value);
  }
  return out;
}

// The template a table needs: scalar tables use the predefined `scalar`.
std::string template_name(const model::Table& table, std::set<std::string>& templates, std::string& definitions) {
  const auto* v1 = variable_name(table.x_variable);
  const auto* v2 = variable_name(table.y_variable);
  if (!v1) {
    return "scalar";
  }
  const std::string name = std::string("t_") + v1 + (v2 ? std::string("_") + v2 : std::string());
  if (templates.insert(name).second) {
    definitions += std::format("  lu_table_template ({}) {{\n    variable_1 : {};\n", name, v1);
    if (v2) {
      definitions += std::format("    variable_2 : {};\n", v2);
    }
    // OpenTimer requires indices on a template; every table repeats its own.
    definitions += std::format("    index_1 (\"{}\");\n", list(table.x));
    if (v2) {
      definitions += std::format("    index_2 (\"{}\");\n", list(table.y));
    }
    definitions += "  }\n";
  }
  return name;
}

// `offset_ps` is added to every value (the rail capture shift of a setup check).
std::string table_group(const char* kind, const model::Table& table, double offset_ps, std::set<std::string>& templates,
                        std::string& definitions) {
  table.validate();
  std::string out = std::format("        {} ({}) {{\n", kind, template_name(table, templates, definitions));
  const bool  one = table.x_variable != model::Variable::none;
  if (one) {
    out += std::format("          index_1 (\"{}\");\n", list(table.x));
    if (table.y_variable != model::Variable::none) {
      out += std::format("          index_2 (\"{}\");\n", list(table.y));
    }
  }
  const size_t columns = table.y_variable != model::Variable::none ? table.y.size() : table.values.size();
  out += "          values (";
  for (size_t at = 0; at < table.values.size(); at += columns) {
    std::string row;
    for (size_t i = at; i < at + columns; ++i) {
      row += (row.empty() ? "" : ", ") + number(table.values[i] + offset_ps);
    }
    out += std::format("{}\"{}\"", at ? ", \\\n                   " : "", row);
  }
  out += ");\n        }\n";
  return out;
}

const char* sense_name(model::Sense sense) {
  switch (sense) {
    case model::Sense::positive: return "positive_unate";
    case model::Sense::negative: return "negative_unate";
    case model::Sense::non_unate: return "non_unate";
    case model::Sense::unknown: break;
  }
  return nullptr;
}

std::string arc_group(const model::Arc& arc, double constraint_offset_ps, std::set<std::string>& templates, std::string& definitions) {
  std::string out = "      timing () {\n";
  out += std::format("        related_pin : \"{}\";\n        timing_type : {};\n", arc.related_pin, arc.type);
  if (const auto* sense = sense_name(arc.sense)) {
    out += std::format("        timing_sense : {};\n", sense);
  }
  if (!arc.when.empty()) {
    out += std::format("        when : \"{}\";\n", arc.when);
  }
  constexpr const char* delay_kind[2]      = {"cell_rise", "cell_fall"};
  constexpr const char* slew_kind[2]       = {"rise_transition", "fall_transition"};
  constexpr const char* constraint_kind[2] = {"rise_constraint", "fall_constraint"};
  for (size_t edge = 0; edge < 2; ++edge) {
    if (arc.delay[edge]) {
      out += table_group(delay_kind[edge], *arc.delay[edge], 0, templates, definitions);
    }
    if (arc.slew[edge]) {
      out += table_group(slew_kind[edge], *arc.slew[edge], 0, templates, definitions);
    }
    if (arc.constraint[edge]) {
      out += table_group(constraint_kind[edge], *arc.constraint[edge], constraint_offset_ps, templates, definitions);
    }
  }
  out += "      }\n";
  return out;
}

std::string pin_group(const model::Pin& pin, double constraint_offset_ps, const std::string& ff_function,
                      std::set<std::string>& templates, std::string& definitions) {
  std::string out = std::format("    pin ({}) {{\n      direction : {};\n", pin.name, pin.direction);
  if (pin.clock) {
    out += "      clock : true;\n";
  }
  if (pin.direction == "output" && !ff_function.empty()) {
    out += std::format("      function : \"{}\";\n", ff_function);
  }
  const auto& cap = pin.capacitance_ff;
  if (cap[0] || cap[1]) {
    const double rise = cap[0].value_or(*cap[1]);
    const double fall = cap[1].value_or(*cap[0]);
    out += std::format("      capacitance : {};\n      rise_capacitance : {};\n      fall_capacitance : {};\n",
                       number(std::max(rise, fall)),
                       number(rise),
                       number(fall));
  }
  if (pin.max_capacitance_ff) {
    out += std::format("      max_capacitance : {};\n", number(*pin.max_capacitance_ff));
  }
  if (pin.max_transition_ps) {
    out += std::format("      max_transition : {};\n", number(*pin.max_transition_ps));
  }
  for (const auto& arc : pin.arcs) {
    out += arc_group(arc, constraint_offset_ps, templates, definitions);
  }
  out += "    }\n";
  return out;
}
}  // namespace

std::string domino_liberty(const Domino_timing_view& view, const model::Library& base, double period_ps) {
  if (!(period_ps > 0) || !std::isfinite(period_ps)) {
    throw std::runtime_error("the domino Liberty needs a positive clock period");
  }
  const auto cells = domino_timing_cells(view, base, nullptr, nullptr);
  if (cells->library.cells.empty()) {
    throw std::runtime_error("the domino bundle has no variants");
  }
  std::set<std::string> templates;
  std::string           definitions, body;
  const model::Thresholds* thresholds = nullptr;
  for (const auto& [name, cell] : cells->library.cells) {
    if (!cell.unsupported.empty()) {
      throw std::runtime_error(std::format("domino cell {} cannot be timed: {}", name, cell.unsupported.front()));
    }
    if (!thresholds) {
      thresholds = &cell.thresholds;
    } else if (thresholds->input != cell.thresholds.input || thresholds->output != cell.thresholds.output
               || thresholds->lower != cell.thresholds.lower || thresholds->upper != cell.thresholds.upper
               || thresholds->slew_derate != cell.thresholds.slew_derate) {
      throw std::runtime_error(std::format("domino cell {} has thresholds different from the other variants", name));
    }
    const auto  budget = cells->capture.find(name);
    const auto  capture = budget == cells->capture.end() ? late::Capture_budget{} : budget->second;
    const double shift  = capture.uncertainty_ps.value_or(0) + (1. - capture.fraction) * period_ps;
    std::string  first_rail;
    body += std::format("  cell ({}) {{\n    area : {};\n", name, number(cell.area));
    // The storage element: tools that look for a sequential cell find a flop on
    // CLK whose state is the first rail.
    for (const auto& [pin_name, pin] : cell.pins) {
      if (pin.direction == "input" && !pin.clock && !pin.arcs.empty()) {
        first_rail = pin_name;
        break;
      }
    }
    if (!first_rail.empty()) {
      body += std::format("    ff (IQ, IQN) {{\n      clocked_on : \"CLK\";\n      next_state : \"{}\";\n    }}\n", first_rail);
    }
    for (const auto& [pin_name, pin] : cell.pins) {
      body += pin_group(pin, shift, pin.direction == "output" ? "IQ" : "", templates, definitions);
    }
    body += "  }\n";
  }
  std::string out = "/* Provisional domino cells (generated by LiveHD; stage model v" + std::to_string(view.stage_version) + ", bundle "
                    + view.digest + ", period " + number(period_ps) + " ps) */\nlibrary (lhd_domino_provisional) {\n";
  out += "  delay_model : table_lookup;\n  time_unit : \"1ps\";\n  voltage_unit : \"1V\";\n  current_unit : \"1uA\";\n";
  out += "  capacitive_load_unit (1, ff);\n  leakage_power_unit : \"1nW\";\n  pulling_resistance_unit : \"1kohm\";\n";
  out += std::format("  input_threshold_pct_rise : {};\n  input_threshold_pct_fall : {};\n",
                     number(thresholds->input[0] * 100),
                     number(thresholds->input[1] * 100));
  out += std::format("  output_threshold_pct_rise : {};\n  output_threshold_pct_fall : {};\n",
                     number(thresholds->output[0] * 100),
                     number(thresholds->output[1] * 100));
  out += std::format("  slew_lower_threshold_pct_rise : {};\n  slew_lower_threshold_pct_fall : {};\n",
                     number(thresholds->lower[0] * 100),
                     number(thresholds->lower[1] * 100));
  out += std::format("  slew_upper_threshold_pct_rise : {};\n  slew_upper_threshold_pct_fall : {};\n",
                     number(thresholds->upper[0] * 100),
                     number(thresholds->upper[1] * 100));
  out += std::format("  slew_derate_from_library : {};\n", number(thresholds->slew_derate));
  out += definitions + body + "}\n";
  return out;
}
}  // namespace livehd::domplace::timing

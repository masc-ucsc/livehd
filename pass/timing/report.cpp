// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "report.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <functional>
#include <optional>
#include <limits>
#include <map>
#include <stdexcept>

#include "absl/container/flat_hash_map.h"
#include "attrs.hpp"
#include "hhds/attrs/srcid.hpp"
#include "node_util.hpp"
#include "pass/partition/flatten.hpp"
#include "pass/liberty/timing_model.hpp"
#include "physical_rank.hpp"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"

namespace livehd::timing::fast {
namespace {
struct Region_row {
  std::string module;
  int         color   = 0;
  size_t      cells   = 0;
  bool        resynth = false;
  float       arrival = -std::numeric_limits<float>::infinity();
  std::string critical_pin, critical_src;
};
}  // namespace

namespace {
// The report of one timed design: everything below reads only the engine, the binding and these.
struct Timed {
  const Engine*                   engine = nullptr;
  const Bound_graph*              bound  = nullptr;
  std::optional<float>            wns, tns;
  std::vector<std::string>        unsupported;    // the constraint reader's and the binding's
  const livehd::timing::Feedback* feedback = nullptr;  // the placed evaluation (physical_pins), else null
  // The module a cell belongs to (its region), with the color: false for a port cell.
  std::function<bool(uint32_t, const hhds::GraphIO*&, int&)> region;
};

std::string write_report(hhds::GraphLibrary& library, std::string_view top, const Report_inputs& inputs,
                         const std::shared_ptr<const physical::Parasitics>& placed, const Timed& timed);
std::string report_json_placed(hhds::GraphLibrary& library, std::string_view top, const Report_inputs& inputs,
                               const std::shared_ptr<const physical::Parasitics>& placed);

// The synthesis wire estimate (synth.wire_rc): every net a star of the predicted length for its fanout, driver -> one
// junction (the wire resistance and capacitance) -> every sink. Its Elmore solution has a closed form: every sink sees
// delay R * C_total (the wire plus the sink pins), and the impulse 2*beta - delta^2 equals delta^2, so the slew it adds
// in quadrature is the delay itself.
Wire_model star_wire(const Netlist& netlist, const Report_inputs& inputs) {
  const auto&  types = netlist.types();
  const size_t C     = types.corners();
  Wire_model   wire;
  wire.net_cap_ff.assign(netlist.nets() * C, 0.f);
  wire.sink_delay_ps.assign(netlist.pins() * C, 0.f);
  wire.sink_slew_ps.assign(netlist.pins() * C, 0.f);
  const auto length = [&](unsigned fanout) {
    const auto& table = inputs.wire_length_um;
    if (const auto it = table.find(fanout); it != table.end()) {
      return it->second;
    }
    const auto above = table.lower_bound(fanout);
    if (above == table.end()) {
      const auto last = std::prev(table.end());
      return last->second + inputs.wire_slope_um * (fanout - last->first);
    }
    if (above == table.begin()) {
      return above->second * fanout / above->first;
    }
    const auto below = std::prev(above);
    return below->second + (above->second - below->second) * (fanout - below->first) / (above->first - below->first);
  };
  constexpr float to_kohm = 1e-3f;  // Ω * fF -> ps (wire.cpp)
  for (uint32_t net = 0; net < netlist.nets(); ++net) {
    const auto sinks = netlist.sinks_of(net);
    if (sinks.empty() || netlist.driver_of(net) == no_index) {
      continue;
    }
    const double um       = std::max(0.0, length(static_cast<unsigned>(sinks.size())));
    const float  wire_cap = static_cast<float>(um * inputs.wire_cap_ff_um);
    const float  res      = static_cast<float>(um * inputs.wire_res_ohm_um);
    for (size_t k = 0; k < C; ++k) {
      float total = wire_cap;
      for (const auto sink : sinks) {
        const auto type  = netlist.type_of(netlist.cell_of(sink));
        const auto local = netlist.local_of(sink);
        total += std::max(types.pin_cap(type, local, k, 0), types.pin_cap(type, local, k, 1));
      }
      const float delay              = res * to_kohm * total;
      wire.net_cap_ff[net * C + k] = wire_cap;
      for (const auto sink : sinks) {
        wire.sink_delay_ps[static_cast<size_t>(sink) * C + k] = delay;
        wire.sink_slew_ps[static_cast<size_t>(sink) * C + k]  = delay;
      }
    }
  }
  return wire;
}
}  // namespace

std::string report_json(hhds::GraphLibrary& library, std::string_view top, const Report_inputs& inputs,
                        const std::shared_ptr<const physical::Parasitics>& placed) {
  const auto io = library.find_io(top);
  if (!io || !io->get_graph()) {
    throw std::runtime_error(std::format("timing report: no module {}", top));
  }
  if (placed) {
    return report_json_placed(library, top, inputs, placed);
  }
  // Synthesis: the mapped hierarchy is timed in place. The hierarchical traversal binds every cell occurrence to the
  // numeric netlist (no flattened copy), the wire estimate goes straight into the engine's per-net arrays, and the
  // design is evaluated once.
  auto                  graph   = io->get_graph();
  const auto            liberty = liberty::timing::parse(inputs.liberty_text);
  const auto*           corner  = &liberty;
  const std::string     name    = inputs.corner_name.empty() ? std::string{"corner0"} : inputs.corner_name;
  const auto            types   = Cell_types::build(std::span<const liberty::timing::Library* const>(&corner, 1),
                                                    std::span<const std::string>(&name, 1));
  const Sdc_units       units{liberty.time_unit_ps, liberty.capacitance_unit_ff};
  const auto            bound   = bind_hier(*graph, types);
  const Bound_names     names(bound);
  auto                  constraints = read_constraints(inputs.sdc_text, units, *bound.netlist, &names);
  complete_constraints(constraints, *bound.netlist, inputs.sdc_text.empty(), true, Physical_ranker::Options{}.default_period_ps,
                       inputs.io_load_ff);
  if (inputs.physical_clock_tree) {
    for (auto& clock : constraints.clocks) {
      clock.propagated = true;
    }
  }
  Timed timed;
  timed.unsupported = constraints.unsupported;
  timed.unsupported.insert(timed.unsupported.end(), bound.unsupported.begin(), bound.unsupported.end());
  Engine engine(*bound.netlist, std::move(constraints));
  if (!inputs.wire_length_um.empty() && (inputs.wire_cap_ff_um > 0 || inputs.wire_res_ohm_um > 0)) {
    engine.set_wire(star_wire(*bound.netlist, inputs));
  }
  engine.evaluate();
  const auto& report = engine.report();
  if (report.endpoints != 0 && std::isfinite(report.wns_ps)) {
    timed.wns = report.wns_ps;
    timed.tns = report.tns_ps;
  }
  timed.engine = &engine;
  timed.bound  = &bound;
  timed.region = [&](uint32_t cell, const hhds::GraphIO*& def, int& color) {
    const auto& node = bound.cell_node[cell];
    if (node.is_invalid()) {
      return false;
    }
    def   = node.get_graph()->get_io().get();
    color = bound.cell_color[cell];
    return def != nullptr;
  };
  return write_report(library, top, inputs, nullptr, timed);
}

namespace {
// The placed design: the placer's RC is named by pin labels, which the Physical_ranker maps to the bound netlist.
std::string report_json_placed(hhds::GraphLibrary& library, std::string_view top, const Report_inputs& inputs,
                               const std::shared_ptr<const physical::Parasitics>& placed) {
  std::shared_ptr<hhds::Graph> graph        = library.find_io(top)->get_graph();
  bool                         hierarchical = false;
  for (auto node : graph->body().nodes()) {
    if (livehd::graph_util::is_type_sub(node) && node.get_subnode_graph() != nullptr) {
      hierarchical = true;
      break;
    }
  }
  livehd::partition::Flat_origin_map origin;
  std::shared_ptr<hhds::Graph>       flat;
  if (hierarchical) {
    flat = livehd::partition::flatten_hierarchy(graph.get(), &library, std::string(top) + "__timing_report", &origin);
    if (!flat) {
      throw std::runtime_error(std::format("timing report: cannot flatten {}", top));
    }
    graph = flat;
  }
  struct Scratch {
    std::shared_ptr<hhds::Graph> g;
    ~Scratch() {
      if (g) {
        g->get_io()->clear();  // the flat copy is a scratch def, never saved
      }
    }
  } scratch{flat};

  Physical_ranker::Options options;
  options.liberty_texts.push_back(inputs.liberty_text);
  options.corner_names.push_back(inputs.corner_name);
  options.sdc_text            = inputs.sdc_text;
  options.default_constraints = true;
  options.propagate_clocks    = inputs.physical_clock_tree;
  options.output_load_ff      = inputs.io_load_ff;
  Physical_ranker ranker(std::move(options));
  // The placement's timing cuts (pins the placed flow does not time through) apply to its report too.
  const auto feedback = ranker.evaluate(graph, placed, placed->timing_cuts);
  Timed      timed;
  timed.engine      = ranker.engine();
  timed.bound       = ranker.bound();
  timed.wns         = feedback.wns ? std::optional<float>(static_cast<float>(*feedback.wns)) : std::nullopt;
  timed.tns         = feedback.tns ? std::optional<float>(static_cast<float>(*feedback.tns)) : std::nullopt;
  timed.unsupported = ranker.unsupported();
  timed.feedback    = &feedback;
  const auto* bound = ranker.bound();
  timed.region      = [&](uint32_t cell, const hhds::GraphIO*& def, int& color) {
    const auto& node = bound->cell_node[cell];
    if (node.is_invalid()) {
      return false;
    }
    const auto it = origin.find(node);
    def           = it == origin.end() ? graph->get_io().get() : library.find_io(it->second.def_gid).get();
    color         = it == origin.end() ? 0 : it->second.color();
    return def != nullptr;
  };
  return write_report(library, top, inputs, placed, timed);
}

std::string write_report(hhds::GraphLibrary& library, std::string_view top, const Report_inputs& inputs,
                         const std::shared_ptr<const physical::Parasitics>& placed, const Timed& timed) {
  (void)library;
  const auto&     engine   = *timed.engine;
  const auto&     bound    = *timed.bound;
  const auto&     netlist  = *bound.netlist;
  const auto&     types    = netlist.types();
  // All time fields in the report use the same unit. Unlabeled Liberty scales
  // are normalized to ps so readers can interpret them without the library.
  const double    unit_div  = inputs.time_unit_ps == 1e6 || inputs.time_unit_ps == 1e3 ? inputs.time_unit_ps : 1.0;
  const auto      unit_name = unit_div == 1e6 ? "us" : unit_div == 1e3 ? "ns" : "ps";

  const auto label = [&](uint32_t pin) -> std::string {
    const auto cell = netlist.cell_of(pin);
    if (bound.cell_node[cell].is_invalid()) {
      for (const auto& port : netlist.ports()) {
        if (port.cell == cell) {
          return port.name;
        }
      }
      return "?";
    }
    return std::format("{}:{}", bound.name(cell), netlist.pin_type(pin).name);
  };
  const auto src_of = [&](uint32_t pin) -> std::string {
    const auto cell = netlist.cell_of(pin);
    if (bound.cell_node[cell].is_invalid()) {
      return {};
    }
    const auto a = bound.cell_node[cell].attr(hhds::attrs::srcid);
    if (!a.has() || a.get() == 0) {
      return {};
    }
    const auto span
        = bound.cell_node[cell].get_graph()->source_locator().resolve_spans(static_cast<hhds::SourceId>(a.get())).primary;
    return span.is_null() ? std::string{} : std::format("{}:{}", span.file, span.start_line.value_or(0));
  };
  const auto worst_arrival = [&](uint32_t pin) {
    float worst = -std::numeric_limits<float>::infinity();
    for (size_t k = 0; k < engine.corners(); ++k) {
      for (unsigned t = 0; t < 2; ++t) {
        worst = std::max(worst, engine.arrival(pin, k, t));
      }
    }
    return worst;
  };

  // Endpoints by arrival.
  std::vector<std::pair<float, uint32_t>> ends;
  for (const auto pin : engine.endpoint_pins()) {
    if (const auto a = worst_arrival(pin); std::isfinite(a)) {
      ends.emplace_back(a, pin);
    }
  }
  std::sort(ends.begin(), ends.end(), [](const auto& a, const auto& b) { return a.first != b.first ? a.first > b.first : a.second < b.second; });

  // Region rows: the module each cell belongs to.
  std::map<std::string, Region_row>                             rows;
  absl::flat_hash_map<const hhds::GraphIO*, Region_row*>        row_of_def;
  const auto                                                    region_of = [&](uint32_t cell) -> Region_row* {
    const hhds::GraphIO* def   = nullptr;
    int                  color = 0;
    if (!timed.region(cell, def, color)) {
      return nullptr;
    }
    if (const auto it = row_of_def.find(def); it != row_of_def.end()) {
      return it->second;
    }
    auto& row = rows[std::string(def->get_name())];
    if (row.module.empty()) {
      row.module  = def->get_name();
      row.color   = color;
      row.resynth = def->get_graph() && def->get_graph()->get_input_node().attr(livehd::attrs::resynth).has();
    }
    row_of_def.emplace(def, &row);
    return &row;
  };
  double area = 0;
  for (uint32_t cell = 0; cell < netlist.cells(); ++cell) {
    if (bound.cell_node[cell].is_invalid()) {
      continue;
    }
    area += types.type(netlist.type_of(cell)).area;
    if (auto* row = region_of(cell)) {
      ++row->cells;
    }
  }
  for (const auto& [arrival, pin] : ends) {
    if (auto* row = region_of(netlist.cell_of(pin)); row && arrival > row->arrival) {
      row->arrival      = arrival;
      row->critical_pin = label(pin);
      row->critical_src = src_of(pin);
    }
  }

  rapidjson::StringBuffer                    buffer;
  rapidjson::Writer<rapidjson::StringBuffer> w(buffer);
  w.StartObject();
  w.Key("schema_version");
  w.Int(1);
  w.Key("kind");
  w.String("sta");
  w.Key("engine");
  w.String("fast");
  w.Key("time_unit");
  w.String(unit_name);
  w.Key("designs");
  w.StartArray();
  w.StartObject();
  w.Key("module");
  w.String(std::string(top).c_str());
  w.Key("area");
  w.Double(area);
  w.Key("cells");
  w.Uint64(bound.mapped_cells);
  w.Key("opaque_logic_nodes");
  w.Uint64(bound.opaque_cells);
  w.Key("native_state_nodes");
  w.Uint64(bound.unmapped_nodes + bound.memory_nodes);
  w.Key("constraints_complete");
  w.Bool(engine.constraints().unsupported.empty());
  const auto& unsupported = timed.unsupported;
  w.Key("timing_cells_complete");
  w.Bool(unsupported.empty() && engine.report().unsupported.empty());
  const auto delay_of = [&](float ps) { return static_cast<double>(ps) / unit_div; };
  if (std::ranges::any_of(engine.constraints().clocks, [](const Clock& clock) { return clock.propagated; })) {
    const auto& derates = engine.constraints().clock_derates;
    w.Key("clock_delay_derates");
    w.StartObject();
    w.Key("early_cell");
    w.Double(derates.cell[0]);
    w.Key("late_cell");
    w.Double(derates.cell[1]);
    w.Key("early_net");
    w.Double(derates.net[0]);
    w.Key("late_net");
    w.Double(derates.net[1]);
    w.EndObject();
    w.Key("propagated_clock_sinks");
    w.StartArray();
    for (auto pin : netlist.order()) {
      if (!engine.propagated_clock_sink(pin)) {
        continue;
      }
      for (size_t k = 0; k < engine.corners(); ++k) {
        w.StartObject();
        w.Key("pin");
        w.String(label(pin).c_str());
        w.Key("clock");
        w.String(engine.clock_at(pin).name.c_str());
        w.Key("corner");
        w.String(types.corner_name(k).c_str());
        for (const auto& [key, early] : {std::pair{"early", true}, std::pair{"late", false}}) {
          w.Key(key);
          w.StartObject();
          for (unsigned t = 0; t < 2; ++t) {
            const auto edge = engine.clock_edge(pin, k, t, early);
            w.Key(t == 0 ? "rise" : "fall");
            w.StartObject();
            w.Key("source_edge");
            w.String((t == 0) != edge.inverted ? "rise" : "fall");
            w.Key("arrival");
            w.Double(delay_of(edge.arrival));
            w.Key("slew");
            w.Double(delay_of(edge.slew));
            w.EndObject();
          }
          w.EndObject();
        }
        w.EndObject();
      }
    }
    w.EndArray();
    w.Key("endpoint_slacks");
    w.StartArray();
    for (const auto pin : engine.endpoint_pins()) {
      const auto setup = engine.endpoint_setup_slack(pin), hold = engine.hold_slack(pin);
      if (!std::isfinite(setup) && !std::isfinite(hold)) {
        continue;
      }
      w.StartObject();
      w.Key("pin");
      w.String(label(pin).c_str());
      if (std::isfinite(setup)) {
        w.Key("setup");
        w.Double(delay_of(setup));
      }
      if (std::isfinite(hold)) {
        w.Key("hold");
        w.Double(delay_of(hold));
      }
      w.EndObject();
    }
    w.EndArray();
  }
  w.Key("max_delay");
  w.Double(ends.empty() ? 0.0 : delay_of(ends.front().first));
  if (timed.wns) {
    w.Key("wns");
    w.Double(delay_of(*timed.wns));
  }
  if (timed.tns) {
    w.Key("tns");
    w.Double(delay_of(*timed.tns));
  }
  if (!ends.empty()) {
    w.Key("critical_pin");
    w.String(label(ends.front().second).c_str());
    w.Key("critical_src");
    w.String(src_of(ends.front().second).c_str());
    w.Key("path");
    w.StartArray();
    float previous = 0;
    for (const auto& point : engine.worst_path(ends.front().second)) {
      w.StartObject();
      w.Key("pin");
      w.String(label(point.pin).c_str());
      w.Key("at");
      w.Double(delay_of(point.arrival_ps));
      w.Key("delay");
      w.Double(delay_of(point.arrival_ps - previous));
      w.Key("dir");
      w.String(point.transition == 0 ? "rise" : "fall");
      if (const auto cell = netlist.cell_of(point.pin); !bound.cell_node[cell].is_invalid()) {
        w.Key("cell");
        w.String(types.type(netlist.type_of(cell)).name.c_str());
      }
      w.EndObject();
      previous = point.arrival_ps;
    }
    w.EndArray();
  }
  w.Key("endpoints");
  w.StartArray();
  for (size_t i = 0; i < ends.size() && i < 10; ++i) {
    w.StartObject();
    w.Key("pin");
    w.String(label(ends[i].second).c_str());
    w.Key("delay");
    w.Double(delay_of(ends[i].first));
    w.Key("src");
    w.String(src_of(ends[i].second).c_str());
    w.EndObject();
  }
  w.EndArray();
  w.Key("colors");
  w.StartArray();
  for (const auto& [name, row] : rows) {
    w.StartObject();
    w.Key("module");
    w.String(row.module.c_str());
    w.Key("color");
    w.Int(row.color);
    w.Key("cells");
    w.Uint64(row.cells);
    w.Key("resynth");
    w.Int(row.resynth ? 1 : 0);
    if (std::isfinite(row.arrival)) {
      w.Key("max_arrival");
      w.Double(delay_of(row.arrival));
      w.Key("critical_pin");
      w.String(row.critical_pin.c_str());
      w.Key("critical_src");
      w.String(row.critical_src.c_str());
    }
    w.EndObject();
  }
  w.EndArray();
  if (placed && timed.feedback) {
    const auto& feedback = *timed.feedback;
    // The placed design: the RC it was timed with and every physical pin's timing, in time_unit_ps units.
    w.Key("parasitics");
    w.StartObject();
    w.Key("model");
    w.String(inputs.parasitic_model.c_str());
    w.Key("identity");
    w.String(placed->identity.c_str());
    w.Key("approximate");
    w.Bool(placed->approximate);
    w.Key("nets");
    w.Uint64(placed->nets.size());
    w.EndObject();
    if (bound.memory_nodes != 0) {
      w.Key("memory_timing_model");
      w.String("clock_boundary_zero_internal_delay");
    }
    w.Key("time_unit_ps");
    w.Double(unit_div);
    const double scale    = feedback.time_unit_ps / unit_div;
    const auto   pin_json = [&](std::string_view name, const livehd::timing::Feedback::Pin& pin) {
      w.StartObject();
      w.Key("pin");
      w.String(name.data(), static_cast<rapidjson::SizeType>(name.size()));
      for (const auto& [key, value] : {
               std::pair{   "arrival",    pin.arrival},
               std::pair{  "required",   pin.required},
               std::pair{     "slack",      pin.slack},
               std::pair{"hold_slack", pin.hold_slack},
               std::pair{      "slew",       pin.slew}
      }) {
        if (value && std::isfinite(*value)) {
          w.Key(key);
          w.Double(*value * scale);
        }
      }
      for (const auto& [key, value] : {
               std::pair{       "load_ff",        pin.load_ff},
               std::pair{"capacitance_ff", pin.capacitance_ff}
      }) {
        if (value && std::isfinite(*value)) {
          w.Key(key);
          w.Double(*value);
        }
      }
      if (pin.violations) {
        w.Key("violations");
        w.Uint(pin.violations);
        if (std::isfinite(pin.electrical_excess)) {
          w.Key("electrical_excess");
          w.Double(pin.electrical_excess);
        }
      }
      w.Key("clock");
      w.Bool(pin.clock);
      w.EndObject();
    };
    w.Key("physical_pins");
    w.StartArray();
    if (feedback.dense && feedback.dense->index) {
      for (size_t i = 0; i < feedback.dense->values.size(); ++i) {
        if (feedback.dense->contains(i)) {
          pin_json(feedback.dense->index->labels[i], feedback.dense->values[i]);
        }
      }
    } else {
      for (const auto& [name, pin] : feedback.pins) {
        pin_json(name, pin);
      }
    }
    w.EndArray();
  }
  w.Key("unsupported");
  w.StartArray();
  for (const auto& u : unsupported) {
    w.String(u.c_str());
  }
  for (const auto& u : engine.report().unsupported) {
    w.String(u.c_str());
  }
  w.EndArray();
  w.EndObject();
  w.EndArray();
  w.EndObject();
  return buffer.GetString();
}
}  // namespace

}  // namespace livehd::timing::fast

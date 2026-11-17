// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "report.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <limits>
#include <map>
#include <stdexcept>

#include "attrs.hpp"
#include "hhds/attrs/srcid.hpp"
#include "node_util.hpp"
#include "pass/partition/flatten.hpp"
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

std::string report_json(hhds::GraphLibrary& library, std::string_view top, const Report_inputs& inputs,
                        const std::shared_ptr<const physical::Parasitics>& placed) {
  const auto io = library.find_io(top);
  if (!io || !io->get_graph()) {
    throw std::runtime_error(std::format("timing report: no module {}", top));
  }
  std::shared_ptr<hhds::Graph> graph = io->get_graph();
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
  options.output_load_ff      = inputs.io_load_ff;
  Physical_ranker ranker(std::move(options));
  // The placement's timing cuts (pins the placed flow does not time through) apply to its report too.
  const std::set<std::string> cuts = placed ? placed->timing_cuts : std::set<std::string>{};
  auto feedback = ranker.evaluate(graph, placed ? placed : std::make_shared<const physical::Parasitics>(), cuts);
  if (!placed && !inputs.wire_length_um.empty() && (inputs.wire_cap_ff_um > 0 || inputs.wire_res_ohm_um > 0)) {
    // The synthesis wire estimate: every net of the bound netlist gets its predicted wire as a star, and the design
    // is timed again with it.
    const auto& nl     = *ranker.bound()->netlist;
    const auto& bnd    = *ranker.bound();
    const auto  length = [&](unsigned fanout) {
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
    const auto pin_label = [&](uint32_t pin) -> std::string {
      const auto cell = nl.cell_of(pin);
      if (bnd.cell_node[cell].is_invalid()) {
        for (const auto& port : nl.ports()) {
          if (port.cell == cell) {
            return port.name;
          }
        }
        return {};
      }
      return std::format("{}:{}", bnd.cell_node[cell].get_name(), nl.pin_type(pin).name);
    };
    auto wire = std::make_shared<physical::Parasitics>();
    for (uint32_t net = 0; net < nl.nets(); ++net) {
      const auto sinks = nl.sinks_of(net);
      const auto drv   = nl.driver_of(net);
      if (sinks.empty() || drv == no_index) {
        continue;
      }
      const auto driver = pin_label(drv);
      if (driver.empty()) {
        continue;
      }
      const double     um = std::max(0.0, length(static_cast<unsigned>(sinks.size())));
      physical::Rc_net rc_net;
      rc_net.driver         = livehd::Pin_label(driver);
      rc_net.wire_length_um = um;
      rc_net.nodes.push_back({livehd::Pin_label(driver), 0});
      rc_net.nodes.push_back({livehd::Pin_label(), um * inputs.wire_cap_ff_um});  // the junction
      rc_net.edges.push_back({0, 1, um * inputs.wire_res_ohm_um});
      for (const auto sink : sinks) {
        const auto label = pin_label(sink);
        if (label.empty()) {
          continue;
        }
        rc_net.nodes.push_back({livehd::Pin_label(label), 0});
        rc_net.edges.push_back({1, static_cast<unsigned>(rc_net.nodes.size() - 1), 0});
      }
      wire->nets.push_back(std::move(rc_net));
    }
    feedback = ranker.evaluate(graph, wire, {});
  }
  const auto&     engine   = *ranker.engine();
  const auto&     bound    = *ranker.bound();
  const auto&     netlist  = *bound.netlist;
  const auto&     types    = netlist.types();

  // Units: the report speaks the Liberty's time unit, as pass.opentimer does (ns or ps; anything else in ps).
  const double unit_div  = inputs.time_unit_ps == 1000 ? 1000.0 : 1.0;
  const auto   unit_name = inputs.time_unit_ps == 1000 ? "ns" : "ps";

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
    return std::format("{}:{}", bound.cell_node[cell].get_name(), netlist.pin_type(pin).name);
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
    const auto span = graph->source_locator().resolve_spans(static_cast<hhds::SourceId>(a.get())).primary;
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

  // Region rows: the module each flat cell was cloned from.
  std::map<std::string, Region_row> rows;
  const auto                        region_of = [&](uint32_t cell) -> Region_row* {
    if (bound.cell_node[cell].is_invalid()) {
      return nullptr;
    }
    const auto it = origin.find(bound.cell_node[cell]);
    const auto def = it == origin.end() ? graph->get_io() : library.find_io(it->second.def_gid);
    if (!def) {
      return nullptr;
    }
    auto& row = rows[std::string(def->get_name())];
    if (row.module.empty()) {
      row.module  = def->get_name();
      row.color   = it == origin.end() ? 0 : it->second.color();
      row.resynth = def->get_graph() && def->get_graph()->get_input_node().attr(livehd::attrs::resynth).has();
    }
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
  w.Uint64(bound.unmapped_nodes);
  w.Key("constraints_complete");
  w.Bool(engine.constraints().unsupported.empty());
  const auto unsupported = ranker.unsupported();
  w.Key("timing_cells_complete");
  w.Bool(unsupported.empty() && engine.report().unsupported.empty());
  const auto delay_of = [&](float ps) { return static_cast<double>(ps) / unit_div; };
  w.Key("max_delay");
  w.Double(ends.empty() ? 0.0 : delay_of(ends.front().first));
  if (feedback.wns) {
    w.Key("wns");
    w.Double(delay_of(*feedback.wns));
  }
  if (feedback.tns) {
    w.Key("tns");
    w.Double(delay_of(*feedback.tns));
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
  if (placed) {
    // The placed design: the RC it was timed with and every physical pin's timing, in time_unit_ps units.
    w.Key("parasitics");
    w.StartObject();
    w.Key("model");
    w.String("placement_flute");
    w.Key("identity");
    w.String(placed->identity.c_str());
    w.Key("approximate");
    w.Bool(placed->approximate);
    w.Key("nets");
    w.Uint64(placed->nets.size());
    w.EndObject();
    if (bound.unmapped_nodes != 0) {
      w.Key("memory_timing_model");
      w.String("clock_boundary_zero_internal_delay");
    }
    w.Key("time_unit_ps");
    w.Double(inputs.time_unit_ps);
    const double scale = feedback.time_unit_ps / inputs.time_unit_ps;
    const auto   pin_json = [&](std::string_view name, const livehd::timing::Feedback::Pin& pin) {
      w.StartObject();
      w.Key("pin");
      w.String(name.data(), static_cast<rapidjson::SizeType>(name.size()));
      for (const auto& [key, value] : {std::pair{"arrival", pin.arrival}, std::pair{"required", pin.required},
                                       std::pair{"slack", pin.slack}, std::pair{"hold_slack", pin.hold_slack}}) {
        if (value && std::isfinite(*value)) {
          w.Key(key);
          w.Double(*value * scale);
        }
      }
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

}  // namespace livehd::timing::fast

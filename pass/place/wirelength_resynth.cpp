// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include <bit>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <numeric>

#include "attrs.hpp"
#include "memory_proxy.hpp"
#include "node_util.hpp"
#include "pass/partition/flatten.hpp"
#include "placement_technology.hpp"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"
#include "repair_internal.hpp"
#include "row_occupancy.hpp"
#include "wirelength_factor.hpp"
#include "wirelength_register.hpp"

namespace livehd::placement {
namespace {
using namespace detail;
namespace fs = std::filesystem;
namespace gu = graph_util;
struct Box {
  Point lo{INFINITY, INFINITY}, hi{-INFINITY, -INFINITY};
  void  add(Point p) {
    lo.first  = std::min(lo.first, p.first);
    lo.second = std::min(lo.second, p.second);
    hi.first  = std::max(hi.first, p.first);
    hi.second = std::max(hi.second, p.second);
  }
  double length() const { return hi.first < lo.first ? 0 : hi.first - lo.first + hi.second - lo.second; }
};
struct Geometry {
  Box                                        bounds;
  bool                                       complete = true;
  std::vector<std::pair<std::string, Point>> pins;
};
using Nets = std::map<std::string, Geometry>;
Nets geometry(const Result& state) {
  Nets nets;
  for (const auto& net : state.parasitics->nets) {
    auto& g = nets[std::string(net.driver)];
    for (const auto& node : net.nodes) {
      if (node.pin.empty()) {
        continue;
      }
      const auto it = state.pin_locations.find(node.pin);
      if (it == state.pin_locations.end()) {
        g.complete = false;
        g.bounds   = {};
        g.pins.clear();
        break;
      }
      g.pins.emplace_back(std::string(node.pin), it->second);
      g.bounds.add(it->second);
    }
  }
  return nets;
}
bool complete_geometry(const Nets& nets) {
  return std::ranges::all_of(nets, [](const auto& entry) { return entry.second.complete; });
}
double hpwl(const Nets& nets) {
  double length = 0;
  for (const auto& [driver, g] : nets) {
    length += g.bounds.length();
  }
  return length;
}
Node_index named_nodes(hhds::Graph& graph) {
  Node_index result;
  for (auto n : graph.body().nodes()) {
    if (!n.get_name().empty()) {
      result.emplace(std::string(n.get_name()), n);
    }
  }
  return result;
}
struct Local_cluster {
  std::vector<unsigned> members;
  Box                   box;
  Point                 center{};
};
std::vector<Local_cluster> local_clusters(const std::vector<Point>& loads, unsigned fanout) {
  std::vector<unsigned> all(loads.size());
  std::iota(all.begin(), all.end(), 0);
  std::vector<std::vector<unsigned>> work{std::move(all)};
  std::vector<Local_cluster>         clusters;
  while (!work.empty()) {
    auto group = std::move(work.back());
    work.pop_back();
    Local_cluster c;
    for (auto i : group) {
      c.box.add(loads[i]);
      c.center.first  += loads[i].first;
      c.center.second += loads[i].second;
    }
    if (group.size() > std::max(2U, fanout)) {
      partition_spatial_loads(loads, c.box.hi.first - c.box.lo.first >= c.box.hi.second - c.box.lo.second, group);
      auto mid = group.begin() + group.size() / 2;
      work.emplace_back(mid, group.end());
      work.emplace_back(group.begin(), mid);
      continue;
    }
    c.center.first  /= group.size();
    c.center.second /= group.size();
    c.members        = std::move(group);
    clusters.push_back(std::move(c));
  }
  return clusters;
}
struct Copy_branch {
  Point                    center;
  std::vector<std::string> sinks;
};
struct Candidate {
  std::vector<Copy_branch>   branches;
  std::string                driver;
  bool                       duplicate = false;
  std::optional<Wire_factor> factor;
  std::optional<Wire_push>   push;
  std::vector<std::string>   successor_names, copied_inputs;
  Point                      divisor_center{}, intermediate_center{};
  Point                      center{};
  std::vector<std::string>   selected, boundary;
  std::set<std::string>      touched;
  double                     gain = 0, area = 0;
};
// An ordinary combinational gate or the single observable output of a
// recognized mapped register. Sequential functions never enter USYN's
// combinational inventory. A copy must bind every control and data pin.
Inventory root_types(const Inventory& combinational_cells, const liberty::Cell_library& tech) {
  auto       roots = combinational_cells;
  const auto add   = [&](const auto& dff) {
    if (dff.n_out != 1) {
      return;
    }
    Buffer type;
    type.name   = dff.name;
    type.inputs = {dff.d_pin, dff.clk_pin};
    type.output = dff.q_pin;
    type.area   = dff.area;
    for (const auto& p : {dff.reset0_pin, dff.reset1_pin}) {
      if (!p.empty()) {
        type.inputs.push_back(p);
      }
    }
    roots.emplace(type.name, std::move(type));
  };
  for (const auto& f : tech.dffs) {
    add(f);
  }
  for (const auto& f : tech.async_dffs) {
    add(f);
  }
  return roots;
}
using Writer = rapidjson::Writer<rapidjson::StringBuffer>;
void measure(const Result& initial, const Options& options, const Timing_feedback& fb, const Nets& nets, const Nets& buffered,
             const Result& baseline, std::vector<Candidate>& candidates, Writer& report) {
  auto&       graph           = *initial.graph;
  const auto& tech            = options.technology();
  const auto  cells           = inventory(options, tech.lef);
  const auto  roots           = root_types(cells, tech.cells);
  const auto  controls        = scan_special_pins(tech.cells, true);
  auto        protected_names = protected_buffers(graph, cells, controls, true);
  if (!options.control_ports.empty()) {
    protected_names.merge(protected_buffers(graph, cells, controls, true, options.control_ports));
  }
  const auto                         nodes = named_nodes(graph);
  const auto                         dbu   = initial.floorplan.dbu;
  std::map<std::string, std::string> drivers;
  for (const auto& [name, g] : nets) {
    for (const auto& [pin, p] : g.pins) {
      drivers.emplace(pin, name);
    }
  }
  const auto rc = technology_wire_rc(tech.lef,
                                     options.liberty,
                                     options.tech_rc_file,
                                     options.resistance_ohm_um,
                                     options.capacitance_ff_um,
                                     options.rc_pessimism,
                                     tech.cells.contents);
  Layer_rc   layer{std::max(rc.horizontal_r, rc.vertical_r), std::max(rc.horizontal_c, rc.vertical_c)};
  const auto long_wire
      = (layer.r > 0 && layer.c > 0) ? Repeater_proposer(options, tech.lef).length_limit_um(layer) * dbu : INFINITY;
  std::map<std::string, std::vector<Wire_factor>>                                    factors;
  std::map<std::tuple<std::string, std::string, unsigned>, std::optional<Wire_push>> push_cache;
  auto                                                                               mappable = cells;
  std::erase_if(mappable, [&](const auto& entry) { return matches_dont_use(entry.first, options); });
  struct Target {
    std::string           driver;
    bool                  virt;
    std::vector<unsigned> order;
    double                baseline_length, upper;
  };
  std::vector<Target> targets;
  const auto          expanded_nodes = named_nodes(*baseline.graph);
  for (const auto& [driver, g] : nets) {
    if (g.pins.size() < 2) {
      continue;
    }
    const bool virt = options.virtual_buffer_trees && options.virtual_buffer_trees->contains(driver);
    if (!virt && (g.pins.size() - 1 > options.virtual_buffer_fanout || g.bounds.length() < long_wire)) {
      continue;
    }
    Target                   t{driver, virt, {}, 0, 0};
    // Measure exactly the branches expansion actually built from this root.
    std::vector<std::string> pending{driver};
    std::set<std::string>    visited;
    while (!pending.empty()) {
      auto current = std::move(pending.back());
      pending.pop_back();
      if (!visited.insert(current).second || !buffered.contains(current)) {
        continue;
      }
      const auto& net    = buffered.at(current);
      t.baseline_length += net.bounds.length();
      for (const auto& [pin, xy] : net.pins) {
        const auto at = pin.rfind(':');
        if (pin == current || at == std::string::npos) {
          continue;
        }
        const auto name = pin.substr(0, at);
        const auto n    = expanded_nodes.find(name);
        if (nodes.contains(name) || n == expanded_nodes.end()) {
          continue;
        }
        if (auto b = buffer(n->second, cells)) {
          pending.push_back(name + ":" + b->output);
        }
      }
    }
    std::vector<Point> loads;
    Box                spread;
    for (const auto& [pin, xy] : g.pins) {
      if (pin != driver) {
        loads.push_back(xy);
        spread.add(xy);
      }
    }
    if (loads.empty()) {
      continue;
    }
    t.order.resize(loads.size());
    std::iota(t.order.begin(), t.order.end(), 0);
    partition_spatial_loads(loads, spread.hi.first - spread.lo.first >= spread.hi.second - spread.lo.second, t.order);
    Box left, right;
    for (size_t i = 0; i < loads.size(); ++i) {
      (i < loads.size() / 2 ? left : right).add(loads[t.order[i]]);
    }
    t.upper = t.baseline_length - left.length() - right.length();
    targets.push_back(std::move(t));
  }
  std::stable_sort(targets.begin(), targets.end(), [](const auto& a, const auto& b) { return a.upper > b.upper; });
  for (const auto& target : targets) {
    const auto& driver = target.driver;
    const auto& g      = nets.at(driver);
    const bool  virt   = target.virt;
    const auto  colon  = driver.rfind(':');
    if (colon == std::string::npos) {
      continue;
    }
    const auto owner = driver.substr(0, colon);
    const auto it    = nodes.find(owner);
    if (it == nodes.end() || protected_names.contains(owner)) {
      continue;
    }
    auto root = it->second;
    if (fixed(root) || root.attr(attrs::user_cell).has()) {
      continue;
    }
    auto cell = combinational(root, roots);
    if (!cell || cell->inputs.size() != root.get_subnode_io()->get_input_pin_decls().size()
        || root.get_subnode_io()->get_output_pin_decls().size() != 1 || driver.substr(colon + 1) != cell->output) {
      continue;
    }
    // Every pin and wire used by the edit must have an unambiguous scalar binding.
    std::map<std::string, std::set<std::string>> input_pins;
    bool                                         complete = true;
    for (auto& input : cell->inputs) {
      auto sink = root.get_sink_pin(input);
      auto pin  = owner + ":" + input;
      if (sink.is_invalid() || sink.get_driver_pins().size() != 1 || !drivers.contains(pin)
          || !initial.pin_locations.contains(drivers.at(pin)) || gu::bits_of(sink.get_driver_pin()) > 1) {
        complete = false;
        break;
      }
      input_pins[drivers.at(pin)].insert(pin);
    }
    if (!complete || input_pins.empty()) {
      continue;
    }
    Point                    source;
    std::vector<Point>       loads;
    std::vector<std::string> labels;
    bool                     found_driver = false;
    for (const auto& [pin, p] : g.pins) {
      if (pin == driver) {
        source       = p;
        found_driver = true;
        continue;
      }
      labels.push_back(pin);
      loads.push_back(p);
    }
    if (!found_driver || loads.empty()) {
      continue;
    }
    const auto root_pin = output(root, *cell);
    if (root_pin.is_invalid() || gu::bits_of(root_pin) > 1) {
      continue;
    }
    std::map<std::string, Pin> direct;
    for (const auto& sink : sinks(root_pin)) {
      auto dest = sink.get_master_node();
      if (dest.attr(attrs::user_cell).has() || protected_names.contains(std::string(dest.get_name()))) {
        complete = false;
        break;
      }
      direct.emplace(physical_label(sink), sink);
    }
    if (!complete || !std::ranges::all_of(labels, [&](const auto& s) { return direct.contains(s); })
        || direct.size() != labels.size()) {
      continue;
    }
    // Record the complete combinational fan-in up to state and primary inputs.
    std::set<uint64_t> seen;
    std::vector<Node>  pending{root};
    while (!pending.empty()) {
      auto n = pending.back();
      pending.pop_back();
      if (!seen.insert(n.get_debug_nid()).second || !combinational(n, cells)) {
        continue;
      }
      for (auto pin : n.inp_sorted_pins()) {
        for (auto upstream : pin.get_driver_pins()) {
          if (!upstream.is_const()) {
            pending.push_back(upstream.get_master_node());
          }
        }
      }
    }
    // Stop forward extraction at state or where the path leaves the area
    // spanned by this net. A boundary gate's input is a timed load.
    std::set<std::string> cone_outputs;
    std::set<uint64_t>    successors_seen;
    std::vector<Pin>      frontier;
    for (const auto& [label, pin] : direct) {
      frontier.push_back(pin);
    }
    while (!frontier.empty()) {
      auto pin = frontier.back();
      frontier.pop_back();
      auto       n     = pin.get_master_node();
      const auto label = physical_label(pin);
      const auto at    = initial.pin_locations.find(label);
      if (at == initial.pin_locations.end()) {
        continue;
      }
      const auto  p    = at->second;
      const auto* gate = combinational(n, cells);
      if (!gate || n.attr(attrs::user_cell).has() || protected_names.contains(std::string(n.get_name()))
          || p.first < g.bounds.lo.first || p.first > g.bounds.hi.first || p.second < g.bounds.lo.second
          || p.second > g.bounds.hi.second) {
        cone_outputs.insert(label);
        continue;
      }
      if (!successors_seen.insert(n.get_debug_nid()).second) {
        continue;
      }
      for (auto endpoint : sinks(output(n, *gate))) {
        frontier.push_back(endpoint);
      }
    }
    const auto& indexes = target.order;
    const auto  middle  = indexes.size() / 2;
    Box         spread;
    for (auto p : loads) {
      spread.add(p);
    }
    report.StartObject();
    report.Key("driver");
    report.String(driver.c_str());
    report.Key("fanin_nodes");
    report.Uint64(seen.size());
    report.Key("downstream_gates");
    report.Uint64(successors_seen.size());
    report.Key("cone_endpoints");
    report.Uint64(cone_outputs.size());
    report.Key("virtual");
    report.Bool(virt);
    report.Key("loads");
    report.Uint64(loads.size());
    report.Key("spread_um");
    report.Double(spread.length() / dbu);
    report.Key("clusters");
    report.StartArray();
    if (middle) {
      report.Uint64(middle);
    }
    report.Uint64(loads.size() - middle);
    report.EndArray();
    report.Key("ideal_output_gain_um");
    report.Double(target.upper / dbu);
    report.Key("buffer_baseline_hpwl_um");
    report.Double(target.baseline_length / dbu);
    const auto boundary = [&](const std::string& label, bool input) {
      const auto& pos = initial.pin_locations.at(label);
      report.StartObject();
      report.Key("pin");
      report.String(label.c_str());
      report.Key("xy_dbu");
      report.StartArray();
      report.Double(pos.first);
      report.Double(pos.second);
      report.EndArray();
      report.Key(input ? "arrival_ps" : "required_ps");
      const auto* timing = fb.find_label(label);
      const auto  time   = timing ? (input ? timing->arrival : timing->required) : std::nullopt;
      if (time && std::isfinite(*time)) {
        report.Double(*time * fb.time_unit_ps);
      } else {
        report.Null();
      }
      report.EndObject();
    };
    report.Key("inputs");
    report.StartArray();
    for (const auto& [name, pins] : input_pins) {
      for (const auto& pin : pins) {
        boundary(pin, true);
      }
    }
    report.EndArray();
    report.Key("sinks");
    report.StartArray();
    for (const auto& pin : labels) {
      boundary(pin, false);
    }
    report.EndArray();
    report.EndObject();
    const auto propose = [&](size_t begin, size_t end, bool duplicate) {
      Candidate c;
      c.driver    = driver;
      c.duplicate = duplicate;
      c.area      = duplicate ? cell->area : 0;
      c.boundary  = labels;
      c.boundary.insert(c.boundary.end(), cone_outputs.begin(), cone_outputs.end());
      c.touched.insert(driver);
      for (size_t i = begin; i < end; ++i) {
        auto k           = indexes[i];
        c.center.first  += loads[k].first;
        c.center.second += loads[k].second;
        c.selected.push_back(labels[k]);
      }
      c.center.first  /= end - begin;
      c.center.second /= end - begin;
      Box local, retained;
      local.add(c.center);
      retained.add(source);
      for (size_t i = 0; i < indexes.size(); ++i) {
        (i >= begin && i < end ? local : retained).add(loads[indexes[i]]);
      }
      // Moving the root keeps the distribution tree. Credit the old
      // tree's removal only if both new branches fit the fanout target.
      const bool resolves_tree
          = duplicate && std::max(end - begin, indexes.size() - (end - begin)) <= options.virtual_buffer_fanout;
      const double old_output = resolves_tree ? target.baseline_length : g.bounds.length();
      c.gain                  = old_output - local.length() - (duplicate ? retained.length() : 0);
      // Include each input net once, including other fanouts and tied pins.
      for (const auto& [name, pins] : input_pins) {
        const auto& old = nets.at(name);
        Box         next;
        for (const auto& [pin, p] : old.pins) {
          if (!pins.contains(pin) || duplicate) {
            next.add(p);
          }
          if (pins.contains(pin)) {
            next.add({p.first + c.center.first - source.first, p.second + c.center.second - source.second});
          }
        }
        c.gain += old.bounds.length() - next.length();
        c.touched.insert(name);
        for (const auto& [pin, xy] : old.pins) {
          if (!pins.contains(pin)) {
            c.boundary.push_back(pin);
          }
        }
      }
      if (c.gain > 0) {
        candidates.push_back(c);
      }
      if (duplicate && cells.contains(cell->name)) {
        // Extend the copy backward while another local gate saves more of
        // its old output wire than its own inputs would add. Reconvergent
        // branches stay shared. A color boundary is immaterial.
        Candidate                cone = c;
        std::vector<std::string> upstream_nets;
        for (const auto& [net, pins] : input_pins) {
          upstream_nets.push_back(net);
        }
        for (size_t i = 0; i < upstream_nets.size(); ++i) {
          const auto& net = upstream_nets[i];
          const auto  at  = net.rfind(':');
          if (at == std::string::npos) {
            continue;
          }
          const auto parent_name = net.substr(0, at);
          auto       node        = nodes.find(parent_name);
          if (node == nodes.end() || protected_names.contains(parent_name) || node->second.attr(attrs::user_cell).has()
              || fixed(node->second)) {
            continue;
          }
          const auto* type = combinational(node->second, cells);
          if (!type || node->second.get_subnode_io()->get_output_pin_decls().size() != 1 || type->output != net.substr(at + 1)) {
            continue;
          }
          auto old  = nets.at(net).bounds;
          auto next = old;
          next.add(c.center);
          const auto saving = next.length() - old.length();
          if (saving <= 0) {
            continue;
          }
          std::set<std::string> extra;
          bool                  independent = true;
          for (const auto& arg : type->inputs) {
            const auto port = parent_name + ":" + arg;
            const auto sink = node->second.get_sink_pin(arg);
            if (!drivers.contains(port) || sink.is_invalid() || sink.get_driver_pins().size() != 1
                || gu::bits_of(sink.get_driver_pin()) != 1) {
              independent = false;
              break;
            }
            const auto& name = drivers.at(port);
            if (cone.touched.contains(name)) {
              independent = false;
              break;
            }
            extra.insert(name);
          }
          if (!independent) {
            continue;
          }
          double cost = 0;
          for (const auto& name : extra) {
            auto       box    = nets.at(name).bounds;
            const auto before = box.length();
            box.add(c.center);
            cost += box.length() - before;
          }
          if (saving <= cost) {
            continue;
          }
          cone.gain += saving - cost;
          cone.area += type->area;
          cone.copied_inputs.push_back(net);
          for (const auto& name : extra) {
            cone.touched.insert(name);
            upstream_nets.push_back(name);
            for (const auto& [pin, xy] : nets.at(name).pins) {
              cone.boundary.push_back(pin);
            }
          }
        }
        if (!cone.copied_inputs.empty() && cone.gain > 0) {
          candidates.push_back(std::move(cone));
        }
      }
      if (!duplicate && cells.contains(cell->name)) {
        Candidate                                       pushed = c;
        std::optional<std::pair<std::string, unsigned>> role;
        double                                          saved = 0;
        Box                                             combined;
        combined.add(c.center);
        bool                  uniform = true;
        std::set<std::string> successors;
        for (const auto& label : labels) {
          auto        sink      = direct.at(label);
          auto        successor = sink.get_master_node();
          const auto  name      = std::string(successor.get_name());
          const auto* type      = combinational(successor, cells);
          if (!type || !successors.insert(name).second || fixed(successor)
              || std::ranges::any_of(sinks(output(successor, *type)),
                                     [&](const auto& downstream) {
                                       auto dest = downstream.get_master_node();
                                       return dest.attr(attrs::user_cell).has()
                                              || protected_names.contains(std::string(dest.get_name()));
                                     })
              || successor.get_subnode_io()->get_output_pin_decls().size() != 1) {
            uniform = false;
            break;
          }
          const auto arg = label.substr(label.rfind(':') + 1);
          const auto at  = std::ranges::find(type->inputs, arg);
          if (at == type->inputs.end()) {
            uniform = false;
            break;
          }
          auto this_role = std::pair{type->name, static_cast<unsigned>(at - type->inputs.begin())};
          if (role && *role != this_role) {
            uniform = false;
            break;
          }
          role           = this_role;
          const auto key = std::tuple{cell->name, type->name, role->second};
          if (!push_cache.contains(key)) {
            push_cache.emplace(key, wire_push(*cell, *type, role->second, mappable));
          }
          pushed.push = push_cache.at(key);
          if (!pushed.push) {
            uniform = false;
            break;
          }
          const auto out = name + ":" + type->output;
          if (!nets.contains(out) || nets.at(out).pins.empty()) {
            uniform = false;
            break;
          }
          for (const auto& input : type->inputs) {
            auto pin = successor.get_sink_pin(input);
            if (pin.is_invalid() || pin.get_driver_pins().size() != 1 || !drivers.contains(name + ":" + input)) {
              uniform = false;
              break;
            }
            pushed.touched.insert(drivers.at(name + ":" + input));
          }
          if (!uniform) {
            break;
          }
          pushed.successor_names.push_back(name);
          pushed.touched.insert(out);
          for (const auto& [pin, p] : nets.at(out).pins) {
            if (pin == out) {
              continue;
            }
            pushed.boundary.push_back(pin);
            combined.add(p);
          }
          if (pushed.push->bypass) {
            saved += nets.at(out).bounds.length();
          }
        }
        if (uniform && pushed.push) {
          const auto& next = cells.at(role->first);
          if (pushed.push->bypass) {
            // The original root-to-inverter branches and their output nets
            // become a single distribution net to the old inverter sinks.
            pushed.gain += local.length() + saved - combined.length();
            std::erase_if(pushed.boundary, [&](const auto& pin) { return direct.contains(pin); });
          }
          const auto area = mappable.at(pushed.push->root).area - cell->area
                            + labels.size() * ((pushed.push->bypass ? 0 : mappable.at(pushed.push->load).area) - next.area);
          pushed.area = std::max(0., area);
          if (pushed.gain > 0 || (pushed.gain == 0 && pushed.push->bypass)) {
            candidates.push_back(std::move(pushed));
          }
        }
      }
      if (cell->inputs.size() < 3 || !cells.contains(cell->name)) {
        return;
      }
      if (!factors.contains(cell->name)) {
        factors.emplace(cell->name, wire_factors(*cell, mappable));
      }
      for (const auto& f : factors.at(cell->name)) {
        auto split   = c;
        split.factor = f;
        split.area   = std::max(0., mappable.at(f.divisor).area + mappable.at(f.top).area - (duplicate ? 0 : cell->area));
        Point part{};
        for (auto i : f.bound) {
          const auto& input = drivers.at(owner + ":" + cell->inputs[i]);
          const auto  loc   = initial.pin_locations.find(input);
          if (loc == initial.pin_locations.end()) {
            complete = false;
            break;
          }
          part.first  += loc->second.first;
          part.second += loc->second.second;
        }
        if (!complete) {
          break;
        }
        part.first           /= f.bound.size();
        part.second          /= f.bound.size();
        split.divisor_center  = part;
        Point mid{};
        if (!f.intermediate.empty()) {
          for (auto i : f.intermediate_args) {
            const auto src  = i == cell->inputs.size() ? part : initial.pin_locations.at(drivers.at(owner + ":" + cell->inputs[i]));
            mid.first      += src.first;
            mid.second     += src.second;
          }
          mid.first                 /= f.intermediate_args.size();
          mid.second                /= f.intermediate_args.size();
          split.intermediate_center  = mid;
          split.area                 = std::max(0.,
                                mappable.at(f.divisor).area + mappable.at(f.intermediate).area + mappable.at(f.top).area
                                    - (duplicate ? 0 : cell->area));
        }
        split.gain = old_output - local.length() - (duplicate ? retained.length() : 0) - std::abs(part.first - c.center.first)
                     - std::abs(part.second - c.center.second);
        if (!f.intermediate.empty()) {
          split.gain += std::abs(part.first - c.center.first) + std::abs(part.second - c.center.second)
                        - std::abs(part.first - mid.first) - std::abs(part.second - mid.second)
                        - std::abs(mid.first - c.center.first) - std::abs(mid.second - c.center.second);
        }
        for (const auto& [name, pins] : input_pins) {
          const auto& old = nets.at(name);
          Box         next;
          for (const auto& [pin, p] : old.pins) {
            if (duplicate || !pins.contains(pin)) {
              next.add(p);
            }
          }
          for (auto i : f.bound) {
            if (pins.contains(owner + ":" + cell->inputs[i])) {
              next.add(part);
            }
          }
          for (auto i : f.intermediate_args) {
            if (i < cell->inputs.size() && pins.contains(owner + ":" + cell->inputs[i])) {
              next.add(mid);
            }
          }
          for (auto i : f.free) {
            if (i < cell->inputs.size() && pins.contains(owner + ":" + cell->inputs[i])) {
              next.add(c.center);
            }
          }
          split.gain += old.bounds.length() - next.length();
        }
        if (split.gain > 0) {
          candidates.push_back(std::move(split));
        }
      }
    };
    propose(0, indexes.size(), false);
    // In an arbitrary power-up state distinct uninitialized physical flops can
    // disagree before the first clock. Only duplicate state with a defined
    // externally driven reset. Moving the original remains safe.
    const bool initialized = cells.contains(cell->name) || std::ranges::any_of(tech.cells.async_dffs, [&](const auto& f) {
                               return f.name == cell->name && wire_register_has_reset(root, f);
                             });
    if (middle && initialized && !matches_dont_use(cell->name, options)) {
      propose(0, middle, true);
      propose(middle, indexes.size(), true);
      auto clusters = local_clusters(loads, options.virtual_buffer_fanout);
      if (clusters.size() > 2) {
        Candidate group;
        group.driver    = driver;
        group.duplicate = true;
        group.gain      = target.baseline_length;
        group.area      = (clusters.size() - 1) * cell->area;
        group.boundary  = labels;
        group.boundary.insert(group.boundary.end(), cone_outputs.begin(), cone_outputs.end());
        group.touched.insert(driver);
        const auto distance = [&](const auto& cluster) {
          auto box = cluster.box;
          box.add(source);
          return box.length() - cluster.box.length();
        };
        const auto keep = std::min_element(clusters.begin(), clusters.end(), [&](const auto& x, const auto& y) {
          return distance(x) < distance(y);
        });
        for (auto cluster = clusters.begin(); cluster != clusters.end(); ++cluster) {
          group.gain -= cluster->box.length();
          if (cluster == keep) {
            group.gain -= distance(*cluster);
            continue;
          }
          Copy_branch branch{cluster->center, {}};
          for (auto i : cluster->members) {
            branch.sinks.push_back(labels[i]);
          }
          group.branches.push_back(std::move(branch));
        }
        for (const auto& [name, pins] : input_pins) {
          auto       box    = nets.at(name).bounds;
          const auto before = box.length();
          for (const auto& b : group.branches) {
            for (const auto& pin : pins) {
              const auto p = initial.pin_locations.at(pin);
              box.add({p.first + b.center.first - source.first, p.second + b.center.second - source.second});
            }
          }
          group.gain -= box.length() - before;
          group.touched.insert(name);
          for (const auto& [pin, pos] : nets.at(name).pins) {
            if (!pins.contains(pin)) {
              group.boundary.push_back(pin);
            }
          }
        }
        if (group.gain > 0) {
          candidates.push_back(std::move(group));
        }
      }
    }
  }
}
double free_area(const Result& state, const Options& options) {
  auto tech = options.technology().lef;
  memory_proxies(*state.graph, tech, options.liberty);
  const auto  occupied
      = row_occupancy(tech, state.floorplan, occupancy_controls(options.terminal_legalize_options), "wirelength resynthesis", true);
  double       area = 0;
  const double dbu  = state.floorplan.dbu;
  for (const auto& row : occupied.rows) {
    for (const auto& [first, last] : row.free) {
      area += (last - first) * double(row.step) * row.height / (dbu * dbu);
    }
  }
  for (size_t i = 0; i < occupied.roles.size(); ++i) {
    if (occupied.roles[i] != Frame_role::movable) {
      continue;
    }
    const auto& master  = tech.macros.at(std::string(state.floorplan.components[i].macro));
    area               -= master.width * master.height;
  }
  return std::max(0., area);
}
bool apply_candidate(hhds::Graph& graph, Node_index& nodes, const Candidate& c, const Inventory& cells, const Inventory& roots,
                     physical::Floorplan& previous, std::vector<std::string>& changed,
                     std::map<std::string, Options::Virtual_buffer>& trees) {
  if (!c.branches.empty()) {
    for (const auto& branch : c.branches) {
      Candidate piece;
      piece.driver    = c.driver;
      piece.duplicate = true;
      piece.center    = branch.center;
      piece.selected  = branch.sinks;
      if (!apply_candidate(graph, nodes, piece, cells, roots, previous, changed, trees)) {
        throw std::runtime_error("incomplete spatial cluster rewrite");
      }
    }
    return true;
  }
  const auto root_binding = nodes.find(c.driver.substr(0, c.driver.rfind(':')));
  if (root_binding == nodes.end()) {
    return false;
  }
  auto root = root_binding->second;
  auto cell = combinational(root, roots);
  if (!cell || !root.attr(attrs::place).has()) {
    return false;
  }
  // Recheck any wide branch with the normal virtual-tree electrical model.
  // Retire a renamed root; never leave a new overloaded copy unexamined.
  const auto branch = [&](const std::string& old, Pin pin, bool retain) {
    auto at = trees.find(old);
    if (at == trees.end()) {
      return;
    }
    const auto label = physical_label(pin);
    if (label == old) {
      return;
    }
    trees.emplace(label, at->second);
    if (!retain) {
      trees.erase(at);
    }
  };
  auto seed   = root.attr(attrs::place).get();
  seed.status = Ann_place::Status::PLACED;
  seed.x      = std::llround(c.center.first);
  seed.y      = std::llround(c.center.second);
  if (!c.duplicate && !c.factor && !c.push) {
    root.attr(attrs::place).set(seed);
    for (auto& comp : previous.components) {
      if (comp.name == root.get_name()) {
        comp.place = seed;
        break;
      }
    }
    changed.emplace_back(root.get_name());
    return true;
  }
  auto                       old = output(root, *cell);
  std::map<std::string, Pin> loads;
  for (auto p : sinks(old)) {
    loads.emplace(physical_label(p), p);
  }
  for (const auto& label : c.selected) {
    if (!loads.contains(label)) {
      return false;
    }
  }
  const auto original_name = std::string(root.get_name());
  const auto new_gate = [&](Node source_node, std::string name, const std::shared_ptr<hhds::GraphIO>& io, const Ann_place& pose) {
    while (nodes.contains(name)) {
      name += "_";
    }
    auto copy = gu::create_typed_node(graph, Ntype_op::Sub);
    copy.set_subnode(io);
    copy.set_name(name);
    copy.attr(attrs::place).set(pose);
    copy.attr(attrs::color).set(gu::node_color_of(source_node));
    gu::Color_group::copy(source_node, copy);
    gu::Placement_block::copy(source_node, copy);
    if (auto a = source_node.attr(hhds::attrs::srcid); a.has()) {
      copy.attr(hhds::attrs::srcid).set(a.get());
    }
    if (auto a = source_node.attr(attrs::synth_region_id); a.has()) {
      copy.attr(attrs::synth_region_id).set(a.get());
    }
    nodes.emplace(name, copy);
    changed.push_back(name);
    return copy;
  };
  if (c.push) {
    const auto& push    = *c.push;
    const auto  replace = [&](Node node, const Buffer& from, const Buffer& to, const std::vector<unsigned>& permutation) {
      std::vector<Pin> args;
      for (const auto& i : from.inputs) {
        args.push_back(node.get_sink_pin(i).get_driver_pin());
      }
      auto       prev    = output(node, from);
      const auto outputs = sinks(prev);
      const auto roles   = gu::signal_roles_of(prev);
      for (auto p : outputs) {
        p.del_sink(prev);
      }
      for (auto p : node.inp_pins_snapshot()) {
        gu::drop_drivers(p);
      }
      node.set_subnode(buffer_io(graph, to));
      for (size_t i = 0; i < to.inputs.size(); ++i) {
        node.create_sink_pin(to.inputs[i]).connect_driver(args.at(permutation.at(i)));
      }
      auto out = node.create_driver_pin(to.output);
      gu::set_bits(out, 1);
      gu::add_signal_roles(out, roles);
      for (auto p : outputs) {
        p.connect_driver(out);
      }
      return out;
    };
    const auto new_root = replace(root, *cell, cells.at(push.root), push.root_arguments);
    branch(c.driver, new_root, false);
    root.attr(attrs::place).set(seed);
    for (auto& comp : previous.components) {
      if (comp.name == original_name) {
        comp.place = seed;
        break;
      }
    }
    for (const auto& name : c.successor_names) {
      auto        child = nodes.at(name);
      const auto& type  = *combinational(child, cells);
      if (push.bypass) {
        auto prev = output(child, type);
        for (auto p : sinks(prev)) {
          p.del_sink(prev);
          p.connect_driver(new_root);
        }
        child.del_node();
      } else {
        replace(child, type, cells.at(push.load), push.load_arguments);
      }
      changed.push_back(name);
    }
    changed.push_back(original_name);
    return true;
  }
  if (c.factor) {
    const auto& f = *c.factor;
    if (!equivalent_factor(*cell, f, cells)) {
      return false;
    }
    std::vector<Pin> inputs;
    for (const auto& p : cell->inputs) {
      inputs.push_back(root.get_sink_pin(p).get_driver_pin());
    }
    auto part_seed = seed;
    part_seed.x    = std::llround(c.divisor_center.first);
    part_seed.y    = std::llround(c.divisor_center.second);
    const auto make
        = [&](const std::string& master, const std::vector<unsigned>& args, const Ann_place& pose, const std::string& name) {
            const auto& type = cells.at(master);
            auto        gate = new_gate(root, name, buffer_io(graph, type), pose);
            for (size_t i = 0; i < args.size(); ++i) {
              gate.create_sink_pin(type.inputs[i]).connect_driver(inputs.at(args[i]));
            }
            auto pin = gate.create_driver_pin(type.output);
            gu::set_bits(pin, 1);
            return pin;
          };
    inputs.push_back(make(f.divisor, f.bound, part_seed, "__wl_part_" + original_name));
    if (!f.intermediate.empty()) {
      auto mid = seed;
      mid.x    = std::llround(c.intermediate_center.first);
      mid.y    = std::llround(c.intermediate_center.second);
      inputs.push_back(make(f.intermediate, f.intermediate_args, mid, "__wl_mid_" + original_name));
    }
    const auto out = make(f.top, f.free, seed, "__wl_top_" + original_name);
    branch(c.driver, out, c.duplicate);
    for (const auto& label : c.selected) {
      auto sink = loads.at(label);
      sink.del_sink(old);
      sink.connect_driver(out);
    }
    if (!c.duplicate) {
      root.del_node();
    }
    changed.push_back(original_name);
    return true;
  }
  std::vector<std::pair<Node, const Buffer*>> cone_members;
  cone_members.reserve(c.copied_inputs.size());
  for (auto label = c.copied_inputs.rbegin(); label != c.copied_inputs.rend(); ++label) {
    const auto binding = nodes.find(label->substr(0, label->rfind(':')));
    if (binding == nodes.end()) {
      return false;
    }
    const auto* type = combinational(binding->second, cells);
    if (!type || physical_label(output(binding->second, *type)) != *label) {
      return false;
    }
    cone_members.emplace_back(binding->second, type);
  }
  std::map<std::string, Pin> local_inputs;
  const auto                 local_driver = [&](Pin old_input) {
    if (old_input.is_const()) {
      return old_input;
    }
    auto i = local_inputs.find(physical_label(old_input));
    return i == local_inputs.end() ? old_input : i->second;
  };
  for (const auto& [orig, type] : cone_members) {
    auto member = new_gate(orig, "__wl_copy_" + std::string(orig.get_name()), orig.get_subnode_io(), seed);
    for (const auto& arg : type->inputs) {
      auto expected = local_driver(orig.get_sink_pin(arg).get_driver_pin());
      member.create_sink_pin(arg).connect_driver(expected);
      if (member.get_sink_pin(arg).get_driver_pin() != expected) {
        throw std::runtime_error("wirelength cone binding mismatch");
      }
    }
    auto out = member.create_driver_pin(type->output);
    gu::set_bits(out, 1);
    local_inputs.emplace(physical_label(output(orig, *type)), out);
  }
  auto copy = new_gate(root, "__wl_copy_" + original_name, root.get_subnode_io(), seed);
  for (const auto& input : cell->inputs) {
    copy.create_sink_pin(input).connect_driver(local_driver(root.get_sink_pin(input).get_driver_pin()));
  }
  auto out = copy.create_driver_pin(cell->output);
  branch(c.driver, out, true);
  gu::set_bits(out, 1);
  if (!cells.contains(cell->name)) {
    // One logical state bit. Equal initial/reset value is the base case;
    // identical master, inputs and clock below establish the induction step.
    const auto        origin       = root.attr(attrs::aggregate_origin);
    const std::string logical_name = origin.has() ? origin.get() : original_name;
    root.attr(attrs::aggregate_origin).set(logical_name);
    copy.attr(attrs::aggregate_origin).set(logical_name);
    const auto pin_name = std::string(gu::pin_name_of(old));
    gu::set_pin_name(out, pin_name.empty() ? logical_name : pin_name);
  }
  // Structural identity of master and every named data/control argument.
  if (copy.get_subnode_io() != root.get_subnode_io()) {
    throw std::runtime_error("wirelength copy changed master");
  }
  for (const auto& input : cell->inputs) {
    if (copy.get_sink_pin(input).get_driver_pin() != local_driver(root.get_sink_pin(input).get_driver_pin())) {
      throw std::runtime_error("wirelength copy changed input");
    }
  }
  for (const auto& label : c.selected) {
    const auto sink = loads.at(label);
    sink.del_sink(old);
    sink.connect_driver(out);
  }
  changed.push_back(original_name);
  return true;
}
std::optional<double> worst(const Timing_feedback& fb, const std::vector<std::string>& pins) {
  std::optional<double> value;
  for (const auto& pin : pins) {
    if (const auto* p = fb.find_label(pin); p && p->slack) {
      value = std::min(value.value_or(INFINITY), *p->slack);
    }
  }
  return value;
}
}  // namespace
Repair_result wirelength_resynthesize(Result initial, const Options& options, const Evaluate& evaluate,
                                      const Evaluate& trial_evaluate, Virtual_expansion& stats) {
  namespace fs         = std::filesystem;
  const auto directory = fs::path(options.output_dir) / "wl_resynth";
  fs::create_directories(directory);
  if (!initial.graph || !initial.parasitics || initial.floorplan.rows.empty()
      || (options.mutation_policy().no_buffer_edits || options.mutation_policy().no_resynthesis)) {
    return expand_virtual_buffers(std::move(initial), options, evaluate, &stats);
  }
  auto                    baseline = expand_virtual_buffers(initial, options, evaluate, &stats);
  const auto              nets     = geometry(initial);
  auto                    before   = trial_evaluate(initial);
  const auto              buffered = geometry(baseline.placed);
  rapidjson::StringBuffer doc;
  Writer                  report(doc);
  report.StartObject();
  report.Key("targets");
  report.StartArray();
  std::vector<Candidate> choices;
  measure(initial, options, before, nets, buffered, baseline.placed, choices, report);
  report.EndArray();
  const auto added_cells = [](const Candidate& c) -> int {
    if (c.factor) {
      return 1 + c.duplicate + !c.factor->intermediate.empty();
    }
    if (c.push && c.push->bypass) {
      return -static_cast<int>(c.successor_names.size());
    }
    return static_cast<int>(c.copied_inputs.size() + c.branches.size()) + (c.duplicate && c.branches.empty())
           + c.factor.has_value();
  };
  const auto& site = options.technology().lef.sites.at(initial.floorplan.rows.front().site);
  const auto  rank = [&](const Candidate& c) { return c.gain / std::max(c.area, site.width * site.height); };
  std::stable_sort(choices.begin(), choices.end(), [&](const auto& a, const auto& b) {
    if (rank(a) != rank(b)) {
      return rank(a) > rank(b);
    }
    if (a.gain != b.gain) {
      return a.gain > b.gain;
    }
    if (added_cells(a) != added_cells(b)) {
      return added_cells(a) < added_cells(b);
    }
    return a.driver < b.driver;
  });
  const auto&            tech = options.technology().lef;
  auto                   room = choices.empty() ? 0. : free_area(initial, options);
  std::set<std::string>  touched;
  std::vector<Candidate> batch;
  for (auto& c : choices) {
    if (c.area > room || std::ranges::any_of(c.touched, [&](const auto& n) { return touched.contains(n); })) {
      continue;
    }
    touched.insert(c.touched.begin(), c.touched.end());
    room -= c.area;
    batch.push_back(std::move(c));
  }
  auto       reference     = baseline.timing ? *baseline.timing : (stats.expanded ? evaluate(baseline.placed) : std::move(before));
  const auto baseline_hpwl = hpwl(buffered);
  const bool baseline_complete = complete_geometry(buffered);
  bool       accepted          = false;
  double     trial_hpwl        = baseline_hpwl;
  std::optional<double>               trial_wns;
  std::optional<Repair_result>        winner;
  std::set<std::string>               local_failures;
  unsigned                            attempts = 0;
  std::string                         refusal;
  std::string                         rejected_by;
  std::vector<std::vector<Candidate>> pending;
  const unsigned                      max_trials  = 2U * std::bit_width(batch.size()) + 1U;
  const auto                          trial_batch = [&]() {
    local_failures.clear();
    refusal.clear();
    rejected_by.clear();
    trial_wns.reset();
    ++attempts;
    hhds::GraphLibrary draft;
    auto graph = partition::flatten_hierarchy(initial.graph.get(), &draft, initial.graph->get_name(), nullptr, false, {}, {}, true);
    if (!graph) {
      throw std::runtime_error("cannot clone placed graph for wirelength resynthesis");
    }
    auto trial_options = options;
    // Trial legalization must not update the live optimizer, GUI or
    // retained layout that a routability-driven area retry will resume.
    trial_options.placement_session.reset();
    trial_options.gui.reset();
    trial_options.after_stage   = {};
    trial_options.gp_observer   = {};
    trial_options.gp_rank       = {};
    trial_options.gp_rank_trees = {};
    trial_options.whatif        = nullptr;
    auto       previous         = std::make_shared<physical::Floorplan>(initial.floorplan);
    auto       nodes            = named_nodes(*graph);
    auto       cells            = inventory(options, tech);
    const auto roots            = root_types(cells, options.technology().cells);
    auto       trees            = std::make_shared<std::map<std::string, Options::Virtual_buffer>>();
    if (options.virtual_buffer_trees) {
      *trees = *options.virtual_buffer_trees;
    }
    trial_options.virtual_buffer_trees = trees;
    trial_options.changed_cells.clear();
    std::erase_if(batch, [&](const auto& c) {
      return !apply_candidate(*graph, nodes, c, cells, roots, *previous, trial_options.changed_cells, *trees);
    });
    if (batch.empty()) {
      return;
    }
    if (std::ranges::any_of(batch, [&](const Candidate& c) {
          if (!c.duplicate) {
            return false;
          }
          auto root = nodes.at(c.driver.substr(0, c.driver.rfind(':')));
          return !combinational(root, cells);
        })) {
      hhds::GraphLibrary proof_library;
      auto               normalized = wire_register_proof_view(*graph, proof_library, options.technology().cells, true);
      if (!normalized || has_wire_register_replicas(*normalized)) {
        throw std::runtime_error("unable to undo duplicated state for proof");
      }
    }
    trial_options.output_dir = directory.string();
    trial_options.previous   = previous;
    trial_options.cache_dir.clear();
    trial_options.net_weights    = initial.net_weights;
    trial_options.legalize_cells = true;
    if (options.optimization_multirow_eco) {
      trial_options.multirow_eco = options.optimization_multirow_eco;
    }
    trial_options.own_frame = options.own_frame || options.floorplan.empty();
    trial_options.floorplan = (directory / "floorplan.def").string();
    detail::write_eco_floorplan(initial.floorplan, trial_options.floorplan);
    if (!batch.empty()) {
      auto owner              = std::make_unique<hhds::GraphLibrary>();
      auto trial              = place(*graph, *owner, trial_options);
      trial.gp_density_weight = initial.gp_density_weight;
      Virtual_expansion trial_stats;
      // Keep electrical expansion available on any remaining overloaded branches.
      auto              alternative = expand_virtual_buffers(std::move(trial), trial_options, trial_evaluate, &trial_stats);
      auto              measured = alternative.timing ? *alternative.timing : trial_evaluate(alternative.placed);
      const auto        trial_geometry = geometry(alternative.placed);
      trial_hpwl                       = hpwl(trial_geometry);
      trial_wns                        = measured.wns;
      accepted = baseline_complete && complete_geometry(trial_geometry) && !measured.ranking_only && measured.wns
                 && *measured.wns >= *reference.wns
                 && (trial_hpwl < baseline_hpwl
                     || (trial_hpwl == baseline_hpwl
                         && alternative.placed.floorplan.components.size() < baseline.placed.floorplan.components.size()))
                 && measured.violations <= reference.violations && measured.electrical_excess <= reference.electrical_excess
                 && (!reference.tns || (measured.tns && *measured.tns >= *reference.tns));
      if (!baseline_complete || !complete_geometry(trial_geometry)) {
        rejected_by += " missing_coordinates";
      }
      if (!measured.wns || !reference.wns || *measured.wns < *reference.wns) {
        rejected_by += " setup";
      }
      if (reference.tns && (!measured.tns || *measured.tns < *reference.tns)) {
        rejected_by += " tns";
      }
      if (measured.violations > reference.violations || measured.electrical_excess > reference.electrical_excess) {
        rejected_by += " electrical";
      }
      if (trial_hpwl >= baseline_hpwl) {
        rejected_by += " wirelength";
      }
      for (const auto& c : batch) {
        const auto old = worst(reference, c.boundary), now = worst(measured, c.boundary);
        if (!old || !now || *now < *old) {
          accepted = false;
          local_failures.insert(c.driver);
        }
        // A worst-pin reduction alone can hide damage to another load on the
        // same net or to an existing sink of a newly loaded input net.
        for (const auto& pin : c.boundary) {
          const auto* b = reference.find_label(pin);
          if (!b || b->clock) {
            continue;
          }
          const auto* t = measured.find_label(pin);
          const auto  margin = options.timing_margin * (b->period > 0 ? b->period : reference.clock_period);
          if (!t || (b->slack && (!t->slack || *t->slack < std::min(*b->slack, margin))) || t->violations > b->violations
              || t->electrical_excess > b->electrical_excess) {
            accepted = false;
            local_failures.insert(c.driver);
            break;
          }
        }
      }
      if (accepted) {
        check_placed_legality(alternative.placed, trial_options);
        alternative.libraries.insert(alternative.libraries.begin(), std::move(owner));
        alternative.timing = std::move(measured);
        refresh_repair_weights(alternative.placed, options, *alternative.timing);
        winner.emplace(std::move(alternative));
        stats = trial_stats;
      }
    }
  };
  report.Key("trials");
  report.StartArray();
  while (!batch.empty() && reference.wns && !reference.ranking_only && attempts < max_trials) {
    try {
      trial_batch();
    } catch (const std::runtime_error& error) {
      refusal  = error.what();
      accepted = false;
    }
    report.StartObject();
    report.Key("roots");
    report.Uint64(batch.size());
    report.Key("local_failures");
    report.Uint64(local_failures.size());
    report.Key("hpwl_delta_um");
    report.Double((trial_hpwl - baseline_hpwl) / initial.floorplan.dbu);
    report.Key("global_rejections");
    report.String(rejected_by.c_str());
    report.Key("accepted");
    report.Bool(accepted);
    if (!refusal.empty()) {
      report.Key("error");
      report.String(refusal.c_str());
    }
    report.EndObject();
    if (accepted || attempts >= max_trials) {
      break;
    }
    if (batch.size() > 1) {
      std::stable_partition(batch.begin(), batch.end(), [&](const auto& c) { return !local_failures.contains(c.driver); });
      auto mid = batch.begin() + (batch.size() + 1) / 2;
      pending.emplace_back(std::make_move_iterator(mid), std::make_move_iterator(batch.end()));
      batch.erase(mid, batch.end());
    } else if (!pending.empty()) {
      batch = std::move(pending.back());
      pending.pop_back();
    } else {
      break;
    }
  }
  report.EndArray();
  report.Key("attempts");
  report.Uint(attempts);
  if (!refusal.empty()) {
    report.Key("trial_error");
    report.String(refusal.c_str());
  }
  report.Key("proposed");
  report.Uint64(choices.size());
  report.Key("trial_roots");
  report.Uint64(batch.size());
  report.Key("baseline_hpwl_um");
  report.Double(baseline_hpwl / initial.floorplan.dbu);
  report.Key("trial_hpwl_um");
  report.Double(trial_hpwl / initial.floorplan.dbu);
  report.Key("baseline_wns_ps");
  if (reference.wns) {
    report.Double(*reference.wns * reference.time_unit_ps);
  } else {
    report.Null();
  }
  report.Key("trial_wns_ps");
  if (trial_wns) {
    report.Double(*trial_wns * reference.time_unit_ps);
  } else {
    report.Null();
  }
  report.Key("edits");
  report.StartArray();
  for (const auto& c : batch) {
    report.StartObject();
    report.Key("driver");
    report.String(c.driver.c_str());
    report.Key("kind");
    report.String(c.push ? "push" : c.factor ? "decompose" : (c.duplicate ? "duplicate" : "move"));
    report.Key("cluster_copies");
    report.Uint64(c.branches.size());
    report.Key("copied_fanin_gates");
    report.Uint64(c.copied_inputs.size());
    report.Key("projected_gain_um");
    report.Double(c.gain / initial.floorplan.dbu);
    report.EndObject();
  }
  report.EndArray();
  report.Key("accepted");
  report.Bool(accepted);
  report.EndObject();
  std::ofstream(directory / "report.json") << doc.GetString() << '\n';
  // The driver may restart at a larger routability point in this workdir.
  // Preserve each decision; report.json describes the current placement.
  std::ofstream(directory / "history.jsonl", std::ios::app)
      << std::format("{{\"accepted\":{},\"roots\":{},\"attempts\":{},\"baseline_hpwl_um\":{},\"trial_hpwl_um\":{}}}\n",
                     accepted,
                     accepted ? batch.size() : 0,
                     attempts,
                     baseline_hpwl / initial.floorplan.dbu,
                     trial_hpwl / initial.floorplan.dbu);
  if (winner) {
    const auto expansion = directory / "virtual_buffers/report.json";
    if (stats.expanded && fs::is_regular_file(expansion)) {
      fs::create_directories(fs::path(options.output_dir) / "virtual_buffers");
      fs::copy_file(expansion, fs::path(options.output_dir) / "virtual_buffers/report.json", fs::copy_options::overwrite_existing);
    }
    winner->report = std::format("accepted={}.attempts={}", batch.size(), attempts);
    return std::move(*winner);
  }
  baseline.report = std::format("accepted=0.attempts={}", attempts);
  return baseline;
}
}  // namespace livehd::placement

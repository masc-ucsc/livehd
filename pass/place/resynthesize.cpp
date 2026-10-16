// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <format>
#include <fstream>
#include <map>
#include <set>
#include <stdexcept>

#include "attrs.hpp"
#include "node_util.hpp"
#include "pass/liberty/liberty_dff.hpp"
#include "pass/partition/flatten.hpp"
#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"
#include "repair.hpp"

namespace livehd::placement {
namespace {
namespace gu = graph_util;
using Node   = hhds::Node_class;
using Pin    = hhds::Pin_class;
using Cells  = std::map<std::string, liberty::Comb_cell>;
std::string               label(Node n, const std::string& pin) { return std::string(n.get_name()) + ":" + pin; }
const liberty::Comb_cell* cell(Node n, const Cells& cells) {
  const auto io = n.get_subnode_io();
  if (!io) {
    return nullptr;
  }
  const auto it = cells.find(std::string(io->get_name()));
  if (it == cells.end() || io->get_input_pin_decls().size() != it->second.inputs.size() || io->get_output_pin_decls().size() != 1) {
    return nullptr;
  }
  return &it->second;
}
bool fixed(Node n) {
  const auto p = n.attr(attrs::place);
  return p.has() && (p.get().status == Ann_place::Status::FIXED || p.get().status == Ann_place::Status::LOCKED);
}
std::set<std::string> protected_cones(hhds::Graph& graph, const Timing_feedback& timing, const Cells& cells) {
  std::set<std::string> result;
  std::set<uint64_t>    visited;
  const auto            protect = [&](auto&& self, Pin driver) -> void {
    if (driver.is_invalid() || driver.is_const()) {
      return;
    }
    auto n = driver.get_master_node();
    if (!visited.insert(n.get_debug_nid()).second) {
      return;
    }
    result.emplace(n.get_name());
    const auto op     = gu::type_op_of(n);
    const bool wiring = !n.get_subnode_io() && op != Ntype_op::Flop && op != Ntype_op::Latch && op != Ntype_op::Memory;
    if (!cell(n, cells) && !wiring) {
      return;
    }
    for (auto p : n.inp_sorted_pins()) {
      for (auto d : p.get_driver_pins()) {
        self(self, d);
      }
    }
  };
  for (auto n : graph.body().nodes()) {
    const auto io = n.get_subnode_io();
    for (auto p : n.inp_sorted_pins()) {
      std::string name(p.get_pin_name());
      if (io) {
        for (const auto& decl : io->get_input_pin_decls()) {
          if (decl.port_id == p.get_port_id()) {
            name = decl.name;
          }
        }
      }
      auto lower = name;
      std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return std::tolower(c); });
      const auto it  = timing.pins.find(label(n, name));
      const auto op  = gu::type_op_of(n);
      const auto off = p.get_port_id() % Ntype::Memory_port_stride;
      if ((it != timing.pins.end() && it->second.clock) || lower.find("clk") != std::string::npos
          || lower.find("clock") != std::string::npos || lower.find("reset") != std::string::npos
          || lower.find("rst") != std::string::npos || (op == Ntype_op::Memory && (off == 2 || off == 14))) {
        for (auto d : p.get_driver_pins()) {
          protect(protect, d);
        }
      }
    }
  }
  return result;
}
struct Cut_limit : std::runtime_error {
  using std::runtime_error::runtime_error;
};
struct Cone {
  Node                 owner;
  synth::Lnet          logic;
  synth::Cone_boundary boundary;
  std::vector<Node>    nodes;
  std::vector<Pin>     inputs, outputs;
};
Cone extract(hhds::Graph& graph, const std::string& root, const Cells& cells, const Timing_feedback& timing, unsigned max_cells) {
  const auto                  protected_names = protected_cones(graph, timing, cells);
  std::map<std::string, Node> selected;
  const auto                  visit = [&](auto&& self, Node n, unsigned depth) -> void {
    const auto* c    = cell(n, cells);
    const auto  name = std::string(n.get_name());
    if (!c || fixed(n) || protected_names.contains(name) || selected.contains(name) || depth > 16 || selected.size() == max_cells) {
      return;
    }
    auto out = n.get_driver_pin(c->output);
    if (out.is_invalid() || gu::bits_of(out) != 1) {
      return;
    }
    selected.emplace(name, n);
    for (const auto& pin : c->inputs) {
      const auto drivers = n.get_sink_pin(pin).get_driver_pins();
      if (drivers.size() != 1) {
        throw std::runtime_error("cone input is not singly driven");
      }
      if (!drivers.front().is_const()) {
        self(self, drivers.front().get_master_node(), depth + 1);
      }
    }
  };
  for (auto n : graph.body().nodes()) {
    if (n.get_name() == root) {
      visit(visit, n, 0);
    }
  }
  if (selected.empty()) {
    throw std::runtime_error("critical root is fixed, protected or not scalar combinational logic");
  }
  Cone cone;
  cone.owner = selected.at(root);
  std::map<uint64_t, synth::Lid> values;
  std::map<uint64_t, size_t>     input_indices;
  std::set<uint64_t>             busy;
  const auto                     build = [&](auto&& self, Pin driver, const std::string& sink) -> synth::Lid {
    if (driver.is_const()) {
      const auto& c = gu::const_of(driver);
      if (c.unknown_bit_test(0)) {
        throw std::runtime_error("unknown cone constant");
      }
      return cone.logic.add_constant(c.bit_test(0));
    }
    if (gu::bits_of(driver) != 1) {
      throw std::runtime_error("cone boundary is not scalar");
    }
    const auto id = driver.get_debug_pid();
    auto       n  = driver.get_master_node();
    if (!selected.contains(std::string(n.get_name()))) {
      const auto it = timing.pins.find(sink);
      if (it == timing.pins.end() || !it->second.arrival || !it->second.slew || it->second.clock) {
        throw std::runtime_error("missing data-pin arrival/slew at cut input: " + sink);
      }
      const auto& p = it->second;
      size_t      index;
      if (const auto found = input_indices.find(id); found != input_indices.end()) {
        index = found->second;
      } else {
        index = cone.inputs.size();
        if (index == 12) {
          throw Cut_limit("cone exceeds 12 independent inputs");
        }
        input_indices.emplace(id, index);
        cone.inputs.push_back(driver);
        cone.boundary.inputs.push_back({});
        values.emplace(id, cone.logic.add_input("cut_" + std::to_string(index)));
      }
      auto& b = cone.boundary.inputs[index];
      if (*p.arrival < 0 || *p.slew < 0) {
        throw std::runtime_error("negative cut arrival/slew is unsupported");
      }
      b.arrival_ps = std::max(b.arrival_ps, *p.arrival * timing.time_unit_ps);
      b.slew_ps    = std::max(b.slew_ps, *p.slew * timing.time_unit_ps);
      return values.at(id);
    }
    if (const auto it = values.find(id); it != values.end()) {
      return it->second;
    }
    if (!busy.insert(id).second) {
      throw std::runtime_error("combinational cycle in cut");
    }
    const auto& c = *cell(n, cells);
    if (driver != n.get_driver_pin(c.output)) {
      throw std::runtime_error("unexpected cone output");
    }
    std::vector<synth::Lid> fanins;
    for (const auto& pin : c.inputs) {
      fanins.push_back(self(self, n.get_sink_pin(pin).get_driver_pin(), label(n, pin)));
    }
    const auto value = cone.logic.add_lut(fanins, *liberty::cell_truth(c));
    values.emplace(id, value);
    busy.erase(id);
    return value;
  };
  for (const auto& [name, n] : selected) {
    cone.nodes.push_back(n);
    const auto& c        = *cell(n, cells);
    auto        out      = n.get_driver_pin(c.output);
    bool        crossing = false;
    for (const auto& e : out.out_edges()) {
      crossing |= !selected.contains(std::string(e.sink.get_master_node().get_name()));
    }
    if (!crossing) {
      continue;
    }
    if (cone.outputs.size() == 8) {
      throw Cut_limit("cone exceeds eight outputs");
    }
    const auto it = timing.pins.find(label(n, c.output));
    if (it == timing.pins.end() || !it->second.required || !it->second.load_ff) {
      throw std::runtime_error("missing output required time/load");
    }
    double load = *it->second.load_ff;
    // Keep wire capacitance and external pins; remove the selected sink cells'
    // old pin capacitance because ABC accounts for its replacement cells.
    for (const auto& pin : timing.pins) {
      const auto colon = pin.first.rfind(':');
      if (colon == std::string::npos || !selected.contains(pin.first.substr(0, colon)) || !pin.second.capacitance_ff) {
        continue;
      }
      auto        sink_node = selected.at(pin.first.substr(0, colon));
      const auto* sc        = cell(sink_node, cells);
      const auto  port      = pin.first.substr(colon + 1);
      if (std::find(sc->inputs.begin(), sc->inputs.end(), port) == sc->inputs.end()) {
        continue;
      }
      if (sink_node.get_sink_pin(port).get_driver_pin() == out) {
        load -= *pin.second.capacitance_ff;
      }
    }
    cone.outputs.push_back(out);
    cone.boundary.outputs.push_back({*it->second.required * timing.time_unit_ps, std::max(0.0, load)});
    cone.logic.add_output(build(build, out, {}), "out_" + std::to_string(cone.outputs.size() - 1));
  }
  if (cone.outputs.empty()) {
    throw std::runtime_error("cone has no external output");
  }
  cone.boundary.budget_ps = std::max(1.0, timing.delay * timing.time_unit_ps * 0.99);
  return cone;
}
// Complete combinational proof over independent cut inputs, including every
// reconvergent side output. Validate the provider against the actual Liberty;
// a self-reported truth table is never accepted as equivalence evidence.
unsigned prove(const Cone& cone, const synth::Cone_mapping& mapping, const Cells& cells) {
  const auto& net = mapping.netlist;
  if (!net.latches.empty() || net.cells.size() > 256 || net.sources.size() != cone.inputs.size()
      || net.outputs.size() != cone.outputs.size()) {
    throw std::runtime_error("invalid mapped cone boundary");
  }
  std::vector<uint64_t> functions;
  std::vector<bool>     defined(net.signals.size());
  for (size_t i = 0; i < net.sources.size(); ++i) {
    const auto s = net.sources[i];
    if (s >= defined.size() || defined[s]) {
      throw std::runtime_error("invalid cone source");
    }
    defined[s] = true;
  }
  for (const auto& c : net.cells) {
    if (c.type >= mapping.cells.size() || c.output >= defined.size() || defined[c.output]) {
      throw std::runtime_error("invalid mapped cell");
    }
    const auto& type = mapping.cells.type(c.type);
    const auto  it   = cells.find(type.name);
    if (it == cells.end() || it->second.output != type.output || type.inputs.size() != it->second.inputs.size()
        || c.fanins.size() != type.inputs.size()) {
      throw std::runtime_error("unsupported replacement cell: " + type.name);
    }
    auto declared = it->second;
    auto a = declared.inputs, b = type.inputs;
    std::sort(a.begin(), a.end());
    std::sort(b.begin(), b.end());
    if (a != b) {
      throw std::runtime_error("replacement pin interface mismatch");
    }
    declared.inputs  = type.inputs;
    const auto truth = liberty::cell_truth(declared);
    if (!truth || *truth != type.truth) {
      throw std::runtime_error("replacement function differs from Liberty");
    }
    for (const auto f : c.fanins) {
      if (f >= defined.size() || !defined[f]) {
        throw std::runtime_error("mapped cone is not topological");
      }
    }
    functions.push_back(*truth);
    defined[c.output] = true;
  }
  for (const auto out : net.outputs) {
    if (out >= defined.size() || !defined[out]) {
      throw std::runtime_error("undriven replacement output");
    }
  }
  const auto assignments = 1U << cone.inputs.size();
  for (unsigned pattern = 0; pattern < assignments; ++pattern) {
    std::vector<bool> old(cone.logic.size()), next(net.signals.size());
    for (synth::Lid id = 0; id < cone.logic.size(); ++id) {
      if (cone.logic.kind(id) == synth::Lnet::Kind::source) {
        old[id] = (pattern >> cone.logic.source_index(id)) & 1;
      } else if (cone.logic.kind(id) == synth::Lnet::Kind::constant) {
        old[id] = cone.logic.eval(id, 0);
      } else {
        unsigned p = 0;
        for (unsigned i = 0; i < cone.logic.fanin_count(id); ++i) {
          p |= unsigned(old[cone.logic.fanin(id, i)]) << i;
        }
        old[id] = cone.logic.eval(id, p);
      }
    }
    for (size_t i = 0; i < net.sources.size(); ++i) {
      next[net.sources[i]] = (pattern >> i) & 1;
    }
    for (size_t i = 0; i < net.cells.size(); ++i) {
      const auto& c = net.cells[i];
      unsigned    p = 0;
      for (unsigned j = 0; j < c.fanins.size(); ++j) {
        p |= unsigned(next[c.fanins[j]]) << j;
      }
      next[c.output] = (functions[i] >> p) & 1;
    }
    for (size_t i = 0; i < net.outputs.size(); ++i) {
      if (old[cone.logic.outputs()[i].node] != next[net.outputs[i]]) {
        throw std::runtime_error("cone equivalence REFUTED");
      }
    }
  }
  return assignments;
}
void splice(hhds::Graph& graph, const Cone& cone, const synth::Cone_mapping& mapping, unsigned attempt) {
  const auto root       = cone.owner;
  const auto place_attr = root.attr(attrs::place);
  Ann_place  seed       = place_attr.has() ? place_attr.get() : Ann_place{};
  seed.status           = Ann_place::Status::PLACED;
  if (!cone.nodes.empty()) {
    seed.x = seed.y = 0;
    for (auto n : cone.nodes) {
      const auto p = n.attr(attrs::place);
      if (p.has()) {
        seed.x += p.get().x;
        seed.y += p.get().y;
      }
    }
    seed.x /= cone.nodes.size();
    seed.y /= cone.nodes.size();
  }
  const auto&      net = mapping.netlist;
  std::vector<Pin> signals(net.signals.size());
  for (size_t i = 0; i < cone.inputs.size(); ++i) {
    signals[net.sources[i]] = cone.inputs[i];
  }
  std::set<std::string> names;
  for (auto n : graph.body().nodes()) {
    names.emplace(n.get_name());
  }
  unsigned serial = 0;
  for (const auto& c : net.cells) {
    const auto& type = mapping.cells.type(c.type);
    auto        n    = gu::create_typed_node(graph, Ntype_op::Sub);
    n.set_subnode(synth::declare_cell(*graph.get_io()->get_library(), type).io);
    std::string name;
    do {
      name = std::format("__place_resynth_{}_{}", attempt, serial++);
    } while (!names.insert(name).second);
    n.set_name(name);
    n.attr(attrs::place).set(seed);
    n.attr(attrs::color).set(gu::node_color_of(root));
    if (const auto a = root.attr(hhds::attrs::srcid); a.has()) {
      n.attr(hhds::attrs::srcid).set(a.get());
    }
    if (const auto a = root.attr(attrs::placement_block); a.has()) {
      n.attr(attrs::placement_block).set(a.get());
    }
    if (const auto a = root.attr(attrs::synth_region_id); a.has()) {
      n.attr(attrs::synth_region_id).set(a.get());
    }
    for (size_t i = 0; i < c.fanins.size(); ++i) {
      n.create_sink_pin(type.inputs[i]).connect_driver(signals[c.fanins[i]]);
    }
    auto out = n.create_driver_pin(type.output);
    gu::set_bits(out, 1);
    signals[c.output] = out;
  }
  std::set<uint64_t> selected;
  for (auto n : cone.nodes) {
    selected.insert(n.get_debug_nid());
  }
  for (size_t i = 0; i < cone.outputs.size(); ++i) {
    const auto old   = cone.outputs[i];
    const auto edges = gu::Edge_vec(old.out_edges().begin(), old.out_edges().end());
    for (const auto& e : edges) {
      if (selected.contains(e.sink.get_master_node().get_debug_nid())) {
        continue;
      }
      e.sink.del_sink(old);
      e.sink.connect_driver(signals[net.outputs[i]]);
    }
  }
  for (auto n : cone.nodes) {
    n.del_node();
  }
}
}  // namespace
Repair_result resynthesize_cones(Result initial, const Options& options, const Evaluate& evaluate,
                                 const synth::Cone_provider& provider) {
  if (!options.full || !provider) {
    throw std::invalid_argument("cone re-synthesis requires full placement and a provider");
  }
  const auto    prior_qor = initial.qor;
  Repair_result result;
  result.placed   = std::move(initial);
  const auto tech = physical::read_lef(options.lefs);
  Cells      cells;
  for (auto c : liberty::scan_comb_cells(options.liberty)) {
    if (!c.physical_barrier && liberty::cell_truth(c) && tech.macros.contains(c.name)) {
      cells.emplace(c.name, std::move(c));
    }
  }
  auto                                       score = evaluate(result.placed);
  rapidjson::StringBuffer                    trace;
  rapidjson::Writer<rapidjson::StringBuffer> w(trace);
  w.StartObject();
  w.Key("model");
  w.String("proved_critical_cone_resynthesis");
  w.Key("before_delay");
  w.Double(score.delay);
  w.Key("trials");
  w.StartArray();
  std::set<std::string> tried;
  unsigned              attempts = 0, accepted = 0;
  while (attempts < 4) {
    std::string root;
    for (auto it = score.path.rbegin(); it != score.path.rend(); ++it) {
      const auto colon = it->rfind(':');
      if (colon == std::string::npos) {
        continue;
      }
      const auto name = it->substr(0, colon);
      if (tried.contains(name)) {
        continue;
      }
      for (auto n : result.placed.graph->body().nodes()) {
        if (n.get_name() == name && cell(n, cells)) {
          root = name;
          break;
        }
      }
      if (!root.empty()) {
        break;
      }
    }
    if (root.empty()) {
      break;
    }
    tried.insert(root);
    ++attempts;
    w.StartObject();
    w.Key("root");
    w.String(root.c_str());
    bool kept = false;
    try {
      hhds::GraphLibrary draft;
      auto               graph = partition::flatten_hierarchy(result.placed.graph.get(),
                                                              &draft,
                                                              result.placed.graph->get_name(),
                                                              nullptr,
                                                              false,
                                                              {},
                                                              {},
                                                              true);
      if (!graph) {
        throw std::runtime_error("cannot clone cone trial");
      }
      Cone cone;
      for (const unsigned limit : {32, 16, 8, 4, 2, 1}) {
        try {
          cone = extract(*graph, root, cells, score, limit);
          break;
        } catch (const Cut_limit&) {
          if (limit == 1) {
            throw;
          }
        }
      }
      w.Key("cells");
      w.Uint64(cone.nodes.size());
      w.Key("inputs");
      w.Uint64(cone.inputs.size());
      w.Key("outputs");
      w.Uint64(cone.outputs.size());
      w.Key("boundary");
      w.StartObject();
      w.Key("budget_ps");
      w.Double(cone.boundary.budget_ps);
      w.Key("inputs");
      w.StartArray();
      for (const auto& in : cone.boundary.inputs) {
        w.StartObject();
        w.Key("arrival_ps");
        w.Double(in.arrival_ps);
        w.Key("slew_ps");
        w.Double(in.slew_ps);
        w.EndObject();
      }
      w.EndArray();
      w.Key("outputs");
      w.StartArray();
      for (const auto& out : cone.boundary.outputs) {
        w.StartObject();
        w.Key("required_ps");
        w.Double(out.required_ps);
        w.Key("load_ff");
        w.Double(out.load_ff);
        w.EndObject();
      }
      w.EndArray();
      w.EndObject();
      const auto mapping = provider(cone.logic, cone.boundary, options.liberty);
      if (!mapping) {
        throw std::runtime_error("cone mapper unavailable, refused or timed out");
      }
      const auto assignments = prove(cone, *mapping, cells);
      w.Key("proof");
      w.String("exhaustive_equivalent");
      w.Key("assignments");
      w.Uint(assignments);
      splice(*graph, cone, *mapping, attempts);
      auto trial = options;
      trial.cache_dir.clear();
      trial.net_weights = result.placed.net_weights;
      trial.previous    = std::make_shared<physical::Floorplan>(result.placed.floorplan);
      trial.changed_cells.clear();
      for (const auto& c : result.placed.floorplan.components) {
        if (c.name == root) {
          trial.changed_cells.push_back(c.name);
        }
      }
      trial.output_dir  = options.output_dir + "/resynth/trial_" + std::to_string(attempts);
      auto       owner  = std::make_unique<hhds::GraphLibrary>();
      auto       placed = place(*graph, *owner, trial);
      const auto next   = evaluate(placed);
      w.Key("delay");
      w.Double(next.delay);
      w.Key("area");
      w.Double(next.area);
      kept = timing_improves(score, next);
      if (kept) {
        result.placed = std::move(placed);
        result.libraries.push_back(std::move(owner));
        score = next;
        ++accepted;
      }
    } catch (const std::exception& e) {
      w.Key("reason");
      w.String(e.what());
    }
    w.Key("accepted");
    w.Bool(kept);
    w.EndObject();
  }
  w.EndArray();
  w.Key("attempts");
  w.Uint(attempts);
  w.Key("accepted");
  w.Uint(accepted);
  w.Key("after_delay");
  w.Double(score.delay);
  w.EndObject();
  result.report = trace.GetString();
  physical::write_def(result.placed.floorplan, options.output_dir + "/out.def");
  rapidjson::Document qor, prior, report;
  qor.Parse(result.placed.qor.c_str());
  prior.Parse(prior_qor.c_str());
  report.Parse(result.report.c_str());
  for (const auto* key : {"timing_driven", "resize", "repair"}) {
    if (prior.HasMember(key) && !qor.HasMember(key)) {
      qor.AddMember(rapidjson::Value(key, qor.GetAllocator()),
                    rapidjson::Value(prior[key], qor.GetAllocator()),
                    qor.GetAllocator());
    }
  }
  qor.AddMember("resynth", rapidjson::Value(report, qor.GetAllocator()), qor.GetAllocator());
  rapidjson::StringBuffer                    out;
  rapidjson::Writer<rapidjson::StringBuffer> qw(out);
  qor.Accept(qw);
  result.placed.qor = out.GetString();
  std::ofstream(options.output_dir + "/qor.json") << result.placed.qor << '\n';
  std::ofstream(options.output_dir + "/resynth.json") << result.report << '\n';
  return result;
}
}  // namespace livehd::placement

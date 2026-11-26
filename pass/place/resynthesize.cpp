// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <format>
#include <fstream>
#include <map>
#include <set>
#include <stdexcept>

#include "attrs.hpp"
#include "growth_budget.hpp"
#include "node_util.hpp"
#include "pass/liberty/liberty_dff.hpp"
#include "pass/partition/flatten.hpp"
#include "placement_technology.hpp"
#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"
#include "repair.hpp"
#include "repair_internal.hpp"
#include "technology_rc.hpp"

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
Cone extract(hhds::Graph& graph, const std::string& root, const Cells& cells, const Timing_feedback& timing,
             const std::set<std::string>& protected_names, unsigned max_cells) {
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
      const auto it = timing.find_label(sink);
      if (it == nullptr || !it->arrival || !it->slew || it->clock) {
        throw std::runtime_error("missing data-pin arrival/slew at cut input: " + sink);
      }
      const auto& p = *it;
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
    const auto it = timing.find_label(label(n, c.output));
    if (it == nullptr || !it->required || !it->load_ff) {
      throw std::runtime_error("missing output required time/load");
    }
    double load = *it->load_ff;
    // Keep wire capacitance and external pins; remove the selected sink cells'
    // old pin capacitance because ABC accounts for its replacement cells.
    timing.for_each_pin([&](const auto& pin_name, const auto& pin) {
      const auto colon = pin_name.rfind(':');
      if (colon == std::string::npos || !selected.contains(pin_name.substr(0, colon)) || !pin.capacitance_ff) {
        return;
      }
      auto        sink_node = selected.at(pin_name.substr(0, colon));
      const auto* sc        = cell(sink_node, cells);
      const auto  port      = pin_name.substr(colon + 1);
      if (std::find(sc->inputs.begin(), sc->inputs.end(), port) == sc->inputs.end()) {
        return;
      }
      if (sink_node.get_sink_pin(port).get_driver_pin() == out) {
        load -= *pin.capacitance_ff;
      }
    });
    cone.outputs.push_back(out);
    cone.boundary.outputs.push_back({*it->required * timing.time_unit_ps, std::max(0.0, load)});
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
// `seeds`, when given, holds one position per mapped cell; otherwise every new cell starts at the centre of the
// cells it replaces.
void splice(hhds::Graph& graph, const Cone& cone, const synth::Cone_mapping& mapping, unsigned attempt,
            std::string_view prefix = "__place_resynth", const std::vector<std::pair<double, double>>* seeds = nullptr) {
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
      name = std::format("{}_{}_{}", prefix, attempt, serial++);
    } while (!names.insert(name).second);
    n.set_name(name);
    if (seeds != nullptr) {
      const auto at = static_cast<size_t>(&c - net.cells.data());
      seed.x        = static_cast<decltype(seed.x)>(std::llround((*seeds)[at].first));
      seed.y        = static_cast<decltype(seed.y)>(std::llround((*seeds)[at].second));
    }
    n.attr(attrs::place).set(seed);
    n.attr(attrs::color).set(gu::node_color_of(root));
    if (const auto a = root.attr(hhds::attrs::srcid); a.has()) {
      n.attr(hhds::attrs::srcid).set(a.get());
    }
    gu::Placement_block::copy(root, n);
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
Repair_result resynthesize_cones(Result initial, const Options& requested, const Evaluate& evaluate,
                                 const synth::Cone_provider& provider, std::optional<Timing_feedback> prior_timing,
                                 const Trial_admission& admit_trial) {
  std::optional<Options> resolved_options;
  if (requested.cell_halo) {
    resolved_options = final_cell_options(requested);
  }
  const auto& options = resolved_options ? *resolved_options : requested;
  if (options.cell_halo) {
    check_placed_legality(initial, options);
  }
  if (!options.full || !provider) {
    throw std::invalid_argument("cone re-synthesis requires full placement and a provider");
  }
  const auto    prior_qor = initial.qor;
  Repair_result result;
  result.placed    = std::move(initial);
  const auto&             technology = options.technology();
  const auto              cells      = detail::inventory(options, technology.lef);
  const auto              controls   = detail::scan_special_pins(technology.cells, /*protect_reset=*/true);
  auto                    score = prior_timing && !prior_timing->ranking_only ? std::move(*prior_timing) : evaluate(result.placed);
  rapidjson::StringBuffer trace;
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
  while (attempts < 4 && options.trial_available() && !options.mutation_policy().no_resynthesis) {
    if (admit_trial && !admit_trial()) {
      break;
    }
    std::string root;
    for (auto it = score.path.rbegin(); it != score.path.rend(); ++it) {
      const auto colon = it->rfind(':');
      if (colon == std::string::npos) {
        continue;
      }
      const auto name = it->substr(0, colon);
      // One call per cone: a root is tried once, and what an accepted cone left behind is not cut again.
      if (tried.contains(name) || name.starts_with("__place_resynth_")) {
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
    if (root.empty() || !options.take_repair_trial()) {
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
      auto protected_names = detail::protected_buffers(*graph, cells, controls, /*protect_reset=*/true, {}, &score);
      if (!options.control_ports.empty()) {
        protected_names.merge(detail::protected_buffers(*graph, cells, controls, true, options.control_ports));
      }
      Cone cone;
      for (const unsigned limit : {32, 16, 8, 4, 2, 1}) {
        try {
          cone = extract(*graph, root, cells, score, protected_names, limit);
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
      trial.legalize_cells = true;
      trial.net_weights = result.placed.net_weights;
      trial.previous    = std::make_shared<physical::Floorplan>(result.placed.floorplan);
      if (options.optimization_multirow_eco) {
        trial.multirow_eco   = options.optimization_multirow_eco;
      }
      trial.changed_cells.clear();
      for (const auto& c : result.placed.floorplan.components) {
        if (c.name == root) {
          trial.changed_cells.push_back(c.name);
        }
      }
      trial.output_dir = options.output_dir + "/resynth/trial_" + std::to_string(attempts);
      std::filesystem::create_directories(trial.output_dir);
      trial.own_frame = options.own_frame || options.floorplan.empty();
      trial.floorplan = trial.output_dir + "/floorplan.def";
      detail::write_eco_floorplan(result.placed.floorplan, trial.floorplan);
      auto owner = std::make_unique<hhds::GraphLibrary>();
      require_growth_budget(*graph, trial);
      auto       placed = place(*graph, *owner, trial);
      placed.gp_density_weight = result.placed.gp_density_weight;
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
  w.Key("timing_budget_exhausted");
  w.Bool(admit_trial && !admit_trial());
  w.Key("after_delay");
  w.Double(score.delay);
  w.EndObject();
  result.report = trace.GetString();
  physical::write_def(result.placed.floorplan, options.output_dir + "/out.def");
  detail::publish_engine_qor(result.placed, prior_qor, {}, {}, "resynth", result.report, options.output_dir);
  std::ofstream(options.output_dir + "/resynth.json") << result.report << '\n';
  result.timing = std::move(score);
  return result;
}
// ---------------------------------------------------------------------------------------------------------------
// Whole-color resynthesis on the placed, flat netlist.
//
// A cone is 32 cells on the worst path. A color is every combinational cell of one placement block (one instance
// prefix and color): its body as it stands after sizing, buffering and cones is cut out, mapped again against the
// arrival and required times the placement measured at its boundary, proved equivalent, and spliced back. Only that
// block is placed again; every other cell keeps its site.
namespace {
struct Block_view {
  uint32_t    id = 0;
  std::string name;
  unsigned    cells     = 0;
  double      area      = 0;  // of the combinational cells that can be replaced
  double      cone_area = 0;  // of those, the cells accepted cones left behind
  double      slack_ps  = std::numeric_limits<double>::infinity();
  double      period_ps = 0;
  uint32_t    region    = 0;
};
bool replaceable(Node n, const Cells& cells, const std::set<std::string>& protected_names) {
  const auto* c = cell(n, cells);
  if (!c || fixed(n) || protected_names.contains(std::string(n.get_name()))) {
    return false;
  }
  const auto out = n.get_driver_pin(c->output);
  if (out.is_invalid() || gu::bits_of(out) != 1) {
    return false;
  }
  for (const auto& pin : c->inputs) {
    if (n.get_sink_pin(pin).get_driver_pins().size() != 1) {
      return false;
    }
  }
  return true;
}
std::vector<Block_view> block_views(hhds::Graph& graph, const Cells& cells, const Timing_feedback& timing,
                                    const std::set<std::string>& protected_names) {
  std::map<uint32_t, Block_view> found;
  for (auto n : graph.body().nodes()) {
    const auto id = gu::Placement_block::id_of(n);
    if (id == 0 || !replaceable(n, cells, protected_names)) {
      continue;
    }
    const auto& c = *cell(n, cells);
    auto&       b = found[id];
    if (b.id == 0) {
      b.id   = id;
      b.name = std::string(gu::Placement_block::text(n));
      if (const auto region = n.attr(attrs::synth_region_id); region.has()) {
        b.region = region.get();
      }
    }
    ++b.cells;
    b.area += c.area;
    if (n.get_name().starts_with("__place_resynth_")) {
      b.cone_area += c.area;
    }
    if (const auto* pin = timing.find_label(label(n, c.output)); pin != nullptr && !pin->clock) {
      if (pin->slack) {
        b.slack_ps = std::min(b.slack_ps, *pin->slack * timing.time_unit_ps);
      } else if (pin->arrival && pin->required) {
        b.slack_ps = std::min(b.slack_ps, (*pin->required - *pin->arrival) * timing.time_unit_ps);
      }
      b.period_ps = std::max(b.period_ps, double(pin->period) * timing.time_unit_ps);
    }
  }
  std::vector<Block_view> result;
  for (auto& [id, b] : found) {
    result.push_back(std::move(b));
  }
  return result;
}
// The cut of one block: every replaceable cell of it. Unlike a cone it has no size limit, and a boundary pin the
// timer did not time (a constant-like or unconstrained input) is an early input or an unconstrained output
// instead of a refusal.
Cone extract_block(hhds::Graph& graph, uint32_t block, const Cells& cells, const Timing_feedback& timing,
                   const std::set<std::string>& protected_names, double budget_ps, double headroom_ps) {
  std::map<std::string, Node> selected;
  for (auto n : graph.body().nodes()) {
    if (gu::Placement_block::id_of(n) == block && replaceable(n, cells, protected_names)) {
      selected.emplace(std::string(n.get_name()), n);
    }
  }
  if (selected.empty()) {
    throw std::runtime_error("the color has no replaceable combinational cell");
  }
  Cone cone;
  cone.owner              = selected.begin()->second;
  cone.boundary.budget_ps = budget_ps;
  std::map<uint64_t, synth::Lid> values;
  std::map<uint64_t, size_t>     input_indices;
  std::map<uint64_t, double>     replaced_load;  // per driver pin: capacitance of the sink pins that go away
  for (const auto& [name, n] : selected) {
    const auto& c = *cell(n, cells);
    for (const auto& pin : c.inputs) {
      const auto driver = n.get_sink_pin(pin).get_driver_pin();
      if (const auto* p = timing.find_label(label(n, pin)); p != nullptr && p->capacitance_ff && !driver.is_const()) {
        replaced_load[driver.get_debug_pid()] += *p->capacitance_ff;
      }
    }
  }
  // Iterative, in dependency order: a color's logic can be far deeper than a cone's.
  const auto external = [&](Pin driver, const std::string& sink) -> synth::Lid {
    const auto id = driver.get_debug_pid();
    size_t     index;
    if (const auto it = input_indices.find(id); it != input_indices.end()) {
      index = it->second;
    } else {
      index = cone.inputs.size();
      input_indices.emplace(id, index);
      cone.inputs.push_back(driver);
      cone.boundary.inputs.push_back({});
      values.emplace(id, cone.logic.add_input("cut_" + std::to_string(index)));
    }
    if (const auto* p = timing.find_label(sink); p != nullptr && !p->clock && p->arrival && p->slew) {
      auto& b      = cone.boundary.inputs[index];
      b.arrival_ps = std::max(b.arrival_ps, std::max(0.0, *p->arrival * timing.time_unit_ps));
      b.slew_ps    = std::max(b.slew_ps, std::max(0.0, *p->slew * timing.time_unit_ps));
    }
    return values.at(id);
  };
  std::set<uint64_t> busy;
  struct Frame {
    Node   node;
    size_t next = 0;
  };
  const auto build = [&](Node root) {
    std::vector<Frame> stack{{root}};
    busy.insert(root.get_debug_nid());
    while (!stack.empty()) {
      auto&       frame     = stack.back();
      const auto  n         = frame.node;
      const auto& c         = *cell(n, cells);
      bool        descended = false;
      while (frame.next < c.inputs.size()) {
        const auto driver = n.get_sink_pin(c.inputs[frame.next]).get_driver_pin();
        ++frame.next;
        if (driver.is_const() || values.contains(driver.get_debug_pid())) {
          continue;
        }
        auto from = driver.get_master_node();
        if (!selected.contains(std::string(from.get_name()))) {
          continue;  // becomes a cut input below
        }
        if (driver != from.get_driver_pin(cell(from, cells)->output)) {
          throw std::runtime_error("unexpected output pin inside the color");
        }
        if (!busy.insert(from.get_debug_nid()).second) {
          throw std::runtime_error("combinational cycle inside the color");
        }
        stack.push_back({from});
        descended = true;
        break;
      }
      if (descended) {
        continue;
      }
      std::vector<synth::Lid> fanins;
      for (const auto& pin : c.inputs) {
        const auto driver = n.get_sink_pin(pin).get_driver_pin();
        if (driver.is_const()) {
          const auto& value = gu::const_of(driver);
          if (gu::bits_of(driver) != 1 || value.unknown_bit_test(0)) {
            throw std::runtime_error("unknown or wide constant inside the color");
          }
          fanins.push_back(cone.logic.add_constant(value.bit_test(0)));
        } else if (const auto it = values.find(driver.get_debug_pid());
                   it != values.end() && selected.contains(std::string(driver.get_master_node().get_name()))) {
          fanins.push_back(it->second);
        } else {
          if (gu::bits_of(driver) != 1) {
            throw std::runtime_error("color boundary is not scalar");
          }
          fanins.push_back(external(driver, label(n, pin)));
        }
      }
      values.emplace(n.get_driver_pin(c.output).get_debug_pid(), cone.logic.add_lut(fanins, *liberty::cell_truth(c)));
      busy.erase(n.get_debug_nid());
      stack.pop_back();
    }
  };
  for (const auto& [name, n] : selected) {
    cone.nodes.push_back(n);
    if (!values.contains(n.get_driver_pin(cell(n, cells)->output).get_debug_pid())) {
      build(n);
    }
  }
  for (const auto& [name, n] : selected) {
    const auto& c        = *cell(n, cells);
    auto        out      = n.get_driver_pin(c.output);
    bool        crossing = false;
    for (const auto& e : out.out_edges()) {
      crossing |= !selected.contains(std::string(e.sink.get_master_node().get_name()));
    }
    if (!crossing) {
      continue;
    }
    synth::Cone_boundary::Output boundary{budget_ps, 0};
    if (const auto* p = timing.find_label(label(n, c.output)); p != nullptr) {
      if (p->required && !p->clock) {
        boundary.required_ps = *p->required * timing.time_unit_ps - headroom_ps;
      }
      if (p->load_ff) {
        const auto gone  = replaced_load.find(out.get_debug_pid());
        boundary.load_ff = std::max(0.0, *p->load_ff - (gone == replaced_load.end() ? 0.0 : gone->second));
      }
    }
    cone.outputs.push_back(out);
    cone.boundary.outputs.push_back(boundary);
    cone.logic.add_output(values.at(out.get_debug_pid()), "out_" + std::to_string(cone.outputs.size() - 1));
  }
  if (cone.outputs.empty()) {
    throw std::runtime_error("the color has no external output");
  }
  return cone;
}
// The mapped replacement as a logic network over the cut inputs, built from the Liberty functions and not from what
// the mapper says its cells compute. Returns the replacement's cell area with it.
std::pair<synth::Lnet, double> mapped_logic(const Cone& cone, const synth::Cone_mapping& mapping, const Cells& cells) {
  const auto& net = mapping.netlist;
  if (!net.latches.empty() || net.sources.size() != cone.inputs.size() || net.outputs.size() != cone.outputs.size()) {
    throw std::runtime_error("invalid mapped color boundary");
  }
  synth::Lnet             logic;
  std::vector<synth::Lid> value(net.signals.size(), synth::Lnet::kNone);
  for (size_t i = 0; i < net.sources.size(); ++i) {
    if (net.sources[i] >= value.size() || value[net.sources[i]] != synth::Lnet::kNone) {
      throw std::runtime_error("invalid mapped color source");
    }
    value[net.sources[i]] = logic.add_input("cut_" + std::to_string(i));
  }
  double area = 0;
  for (const auto& c : net.cells) {
    if (c.type >= mapping.cells.size() || c.output >= value.size() || value[c.output] != synth::Lnet::kNone) {
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
    if (!truth) {
      throw std::runtime_error("replacement cell without a Liberty function: " + type.name);
    }
    std::vector<synth::Lid> fanins;
    for (const auto f : c.fanins) {
      if (f >= value.size() || value[f] == synth::Lnet::kNone) {
        throw std::runtime_error("mapped color is not topological");
      }
      fanins.push_back(value[f]);
    }
    value[c.output]  = logic.add_lut(fanins, *truth);
    area            += it->second.area;
  }
  for (size_t i = 0; i < net.outputs.size(); ++i) {
    if (net.outputs[i] >= value.size() || value[net.outputs[i]] == synth::Lnet::kNone) {
      throw std::runtime_error("undriven replacement output");
    }
    logic.add_output(value[net.outputs[i]], "out_" + std::to_string(i));
  }
  return {std::move(logic), area};
}
// A start position per new cell: the mean of what it connects to, relaxed a few times with the boundary held. The
// local placement that follows only has to spread the cells; the wire length is already close.
std::vector<std::pair<double, double>> seed_positions(const Cone& cone, const synth::Cone_mapping& mapping) {
  const auto& net = mapping.netlist;
  double      cx = 0, cy = 0;
  size_t      placed = 0;
  for (auto n : cone.nodes) {
    if (const auto p = n.attr(attrs::place); p.has()) {
      cx += double(p.get().x);
      cy += double(p.get().y);
      ++placed;
    }
  }
  if (placed != 0) {
    cx /= double(placed);
    cy /= double(placed);
  }
  const auto pose = [](Node n) -> std::optional<std::pair<double, double>> {
    if (const auto p = n.attr(attrs::place); p.has() && p.get().status != Ann_place::Status::UNPLACED) {
      return std::pair{double(p.get().x), double(p.get().y)};
    }
    return {};
  };
  std::set<uint64_t> gone;
  for (auto n : cone.nodes) {
    gone.insert(n.get_debug_nid());
  }
  // Held neighbours of each signal: the driver of a cut input, the outside readers of a cut output.
  std::vector<std::vector<std::pair<double, double>>> held(net.signals.size());
  for (size_t i = 0; i < cone.inputs.size(); ++i) {
    if (const auto at = pose(cone.inputs[i].get_master_node())) {
      held[net.sources[i]].push_back(*at);
    }
  }
  for (size_t i = 0; i < cone.outputs.size(); ++i) {
    for (const auto& e : cone.outputs[i].out_edges()) {
      const auto sink = e.sink.get_master_node();
      if (gone.contains(sink.get_debug_nid())) {
        continue;
      }
      if (const auto at = pose(sink)) {
        held[net.outputs[i]].push_back(*at);
      }
    }
  }
  std::vector<int64_t> producer(net.signals.size(), -1);
  for (size_t k = 0; k < net.cells.size(); ++k) {
    producer[net.cells[k].output] = int64_t(k);
  }
  std::vector<std::vector<uint32_t>> readers(net.cells.size());  // cells that read each cell's output
  for (size_t k = 0; k < net.cells.size(); ++k) {
    for (const auto f : net.cells[k].fanins) {
      if (producer[f] >= 0) {
        readers[size_t(producer[f])].push_back(uint32_t(k));
      }
    }
  }
  std::vector<std::pair<double, double>> at(net.cells.size(), {cx, cy});
  for (int round = 0; round < 24; ++round) {
    auto next = at;
    for (size_t k = 0; k < net.cells.size(); ++k) {
      double     sx = 0, sy = 0;
      size_t     count = 0;
      const auto add   = [&](const std::pair<double, double>& p) {
        sx += p.first;
        sy += p.second;
        ++count;
      };
      for (const auto f : net.cells[k].fanins) {
        if (producer[f] >= 0) {
          add(at[size_t(producer[f])]);
        }
        for (const auto& p : held[f]) {
          add(p);
        }
      }
      for (const auto r : readers[k]) {
        add(at[r]);
      }
      for (const auto& p : held[net.cells[k].output]) {
        add(p);
      }
      if (count != 0) {
        next[k] = {sx / double(count), sy / double(count)};
      }
    }
    at = std::move(next);
  }
  return at;
}
// A cheap refutation before the proof: random input vectors through both networks. Returns the first output that
// differs, or nothing.
std::optional<size_t> simulation_difference(const synth::Lnet& a, const synth::Lnet& b, unsigned vectors) {
  uint64_t   state = 0x9e3779b97f4a7c15ULL;
  const auto next  = [&] {
    state ^= state << 13;
    state ^= state >> 7;
    state ^= state << 17;
    return state;
  };
  const auto run = [](const synth::Lnet& net, const std::vector<bool>& in) {
    std::vector<bool> value(net.size());
    for (synth::Lid id = 0; id < net.size(); ++id) {
      if (net.kind(id) == synth::Lnet::Kind::source) {
        value[id] = in[net.source_index(id)];
      } else if (net.kind(id) == synth::Lnet::Kind::constant) {
        value[id] = net.eval(id, 0);
      } else {
        unsigned p = 0;
        for (unsigned i = 0; i < net.fanin_count(id); ++i) {
          p |= unsigned(value[net.fanin(id, i)]) << i;
        }
        value[id] = net.eval(id, p);
      }
    }
    return value;
  };
  for (unsigned v = 0; v < vectors; ++v) {
    std::vector<bool> in(a.inputs().size());
    for (size_t i = 0; i < in.size(); ++i) {
      in[i] = (next() >> 17) & 1;
    }
    const auto va = run(a, in), vb = run(b, in);
    for (size_t o = 0; o < a.outputs().size(); ++o) {
      if (va[a.outputs()[o].node] != vb[b.outputs()[o].node]) {
        return o;
      }
    }
  }
  return {};
}
bool no_timing_loss(const Timing_feedback& before, const Timing_feedback& after) {
  const double eps = 1e-6 * std::max(1.0, std::abs(before.delay));
  if (before.wns && after.wns) {
    if (*after.wns < *before.wns - eps) {
      return false;
    }
    return !before.tns || !after.tns || *after.tns >= *before.tns - eps * 16;
  }
  return after.delay <= before.delay + eps;
}
}  // namespace
Repair_result resynthesize_colors(Result initial, const Options& requested, const Evaluate& evaluate,
                                  const std::vector<std::pair<std::string, synth::Cone_provider>>& providers,
                                  const Color_request& request, std::optional<Timing_feedback> prior_timing,
                                  const Trial_admission& admit_trial) {
  std::optional<Options> resolved_options;
  if (requested.cell_halo) {
    resolved_options = final_cell_options(requested);
  }
  const auto& options = resolved_options ? *resolved_options : requested;
  if (!options.full || providers.empty()) {
    throw std::invalid_argument("color re-synthesis requires full placement and a provider");
  }
  const auto    prior_qor = initial.qor;
  Repair_result result;
  result.placed                      = std::move(initial);
  const auto&             technology = options.technology();
  const auto              cells      = detail::inventory(options, technology.lef);
  const auto              controls   = detail::scan_special_pins(technology.cells, /*protect_reset=*/true);
  auto                    score = prior_timing && !prior_timing->ranking_only ? std::move(*prior_timing) : evaluate(result.placed);
  const bool              timing_goal = request.goal == Color_request::Goal::timing;
  rapidjson::StringBuffer trace;
  rapidjson::Writer<rapidjson::StringBuffer> w(trace);
  w.StartObject();
  w.Key("model");
  w.String("proved_color_resynthesis");
  w.Key("goal");
  w.String(timing_goal ? "timing" : request.goal == Color_request::Goal::area ? "area" : "area_any");
  w.Key("before_delay");
  w.Double(score.delay);
  const auto protect = [&](hhds::Graph& graph) {
    auto names = detail::protected_buffers(graph, cells, controls, /*protect_reset=*/true, {}, &score);
    if (!options.control_ports.empty()) {
      names.merge(detail::protected_buffers(graph, cells, controls, true, options.control_ports));
    }
    return names;
  };
  // Which occurrences, in which order.
  struct Candidate {
    Block_view  view;
    const char* why = "";
  };
  std::vector<Candidate> candidates;
  unsigned               skipped_size = 0, skipped_history = 0, skipped_done = 0;
  for (auto& b : block_views(*result.placed.graph, cells, score, protect(*result.placed.graph))) {
    if (request.done.contains(b.name)) {
      ++skipped_done;
      continue;
    }
    if (b.cells < 8 || (request.max_cells != 0 && b.cells > request.max_cells)) {
      ++skipped_size;
      continue;
    }
    if (!timing_goal) {
      candidates.push_back({std::move(b), "does not fit"});
      continue;
    }
    // Smallest-area bodies get timing once they fail. A body that was mapped with timing and missed its target has
    // nothing left to give; one that met it is tried again only when the placement moved it well past the target.
    const int history = request.history ? request.history(b.region) : 0;
    if (!std::isfinite(b.slack_ps) || b.slack_ps >= 0 || history == 2) {
      skipped_history += history == 2;
      continue;
    }
    if (history == 1 && b.slack_ps > -request.trigger * (b.period_ps > 0 ? b.period_ps : score.delay * score.time_unit_ps)) {
      ++skipped_history;
      continue;
    }
    candidates.push_back({std::move(b), history == 1 ? "met its target, now fails" : "untimed body on a failing path"});
  }
  std::stable_sort(candidates.begin(), candidates.end(), [&](const Candidate& a, const Candidate& b) {
    if (timing_goal) {
      return a.view.slack_ps < b.view.slack_ps;
    }
    return a.view.cone_area != b.view.cone_area ? a.view.cone_area > b.view.cone_area : a.view.area > b.view.area;
  });
  w.Key("candidates");
  w.Uint64(candidates.size());
  w.Key("skipped_size");
  w.Uint(skipped_size);
  w.Key("skipped_history");
  w.Uint(skipped_history);
  w.Key("skipped_done");
  w.Uint(skipped_done);
  w.Key("trials");
  w.StartArray();
  unsigned attempts = 0, accepted = 0;
  for (const auto& candidate : candidates) {
    if (attempts >= request.max_colors || !options.trial_available() || options.mutation_policy().no_resynthesis) {
      break;
    }
    if (admit_trial && !admit_trial()) {
      break;
    }
    result.colors_tried.push_back(candidate.view.name);
    // Every flow maps the same cut; each result is placed and timed, and the best one that passes stays.
    struct Passed {
      Result                              placed;
      std::unique_ptr<hhds::GraphLibrary> owner;
      Timing_feedback                     timing;
      double                              area = 0;
    };
    std::optional<Passed> best;
    for (const auto& [flow, provider] : providers) {
      if (!options.take_repair_trial()) {
        break;
      }
      ++attempts;
      w.StartObject();
      w.Key("color");
      w.String(candidate.view.name.c_str());
      w.Key("why");
      w.String(candidate.why);
      w.Key("flow");
      w.String(flow.c_str());
      w.Key("slack_ps");
      w.Double(std::isfinite(candidate.view.slack_ps) ? candidate.view.slack_ps : 0);
      w.Key("cone_area");
      w.Double(candidate.view.cone_area);
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
          throw std::runtime_error("cannot clone color trial");
        }
        // The id of a block is the same in the clone: the table is copied with the graph.
        uint32_t block = 0;
        for (auto n : graph->body().nodes()) {
          if (gu::Placement_block::has(n) && gu::Placement_block::text(n) == candidate.view.name) {
            block = gu::Placement_block::id_of(n);
            break;
          }
        }
        // Faster for a timing call; for an area call the same delay with less area.
        const double budget = std::max(1.0, score.delay * score.time_unit_ps * (timing_goal ? 0.99 : 1.0));
        // A timing call asks for a tenth of the period more than the placement needs today, as the stage-boundary
        // resynthesis does: the new body still has to be placed, buffered and sized.
        const double period = candidate.view.period_ps > 0 ? candidate.view.period_ps : score.delay * score.time_unit_ps;
        auto         cone   = extract_block(*graph, block, cells, score, protect(*graph), budget, timing_goal ? 0.1 * period : 0.0);
        double       before_area = 0;
        std::set<std::string> removed;
        for (auto n : cone.nodes) {
          before_area += cell(n, cells)->area;
          removed.emplace(n.get_name());
        }
        w.Key("cells");
        w.Uint64(cone.nodes.size());
        w.Key("inputs");
        w.Uint64(cone.inputs.size());
        w.Key("outputs");
        w.Uint64(cone.outputs.size());
        w.Key("area_before");
        w.Double(before_area);
        // The wires this block has today, by fanout: what its nets will look like again after placement.
        if (result.placed.parasitics) {
          std::map<unsigned, std::pair<double, unsigned>> lengths;
          for (const auto& net : result.placed.parasitics->nets) {
            if (net.driver.bare() || net.wire_length_um <= 0 || !removed.contains(net.driver.instance().str())) {
              continue;
            }
            unsigned fanout = 0;
            for (const auto& node : net.nodes) {
              fanout += !node.pin.empty() && node.pin != net.driver;
            }
            if (fanout != 0) {
              lengths[fanout].first += net.wire_length_um;
              ++lengths[fanout].second;
            }
          }
          if (!lengths.empty()) {
            const auto rc                        = technology_wire_rc(technology.lef,
                                               options.liberty,
                                               options.tech_rc_file,
                                               options.resistance_ohm_um,
                                               options.capacitance_ff_um,
                                               options.rc_pessimism,
                                               technology.cells.contents);
            cone.boundary.wire.capacitance_ff_um = (rc.horizontal_c + rc.vertical_c) / 2;
            cone.boundary.wire.resistance_ohm_um = (rc.horizontal_r + rc.vertical_r) / 2;
            for (const auto& [fanout, sum] : lengths) {
              cone.boundary.wire.length_um[fanout] = sum.first / sum.second;
            }
          }
        }
        const auto mapping = provider(cone.logic, cone.boundary, options.liberty);
        if (!mapping) {
          throw std::runtime_error("color mapper unavailable, refused or timed out");
        }
        const auto [after_logic, after_area] = mapped_logic(cone, *mapping, cells);
        w.Key("cells_after");
        w.Uint64(mapping->netlist.cells.size());
        w.Key("area_after");
        w.Double(after_area);
        if (!timing_goal && after_area >= before_area) {
          throw std::runtime_error("no area gain");
        }
        if (const auto differs = simulation_difference(cone.logic, after_logic, 256)) {
          throw std::runtime_error("color equivalence REFUTED by simulation at output " + std::to_string(*differs));
        }
        const auto proved = synth::prove_equivalent(request.prover, cone.logic, after_logic);
        if (!proved || !*proved) {
          throw std::runtime_error(proved ? "color equivalence unproven; retained original" : "no equivalence prover");
        }
        w.Key("proof");
        w.String("sat_equivalent");
        const auto seeds = seed_positions(cone, *mapping);
        splice(*graph, cone, *mapping, request.serial + attempts, "__place_color", &seeds);
        auto trial = options;
        trial.cache_dir.clear();
        trial.legalize_cells = true;
        trial.net_weights    = result.placed.net_weights;
        trial.previous       = std::make_shared<physical::Floorplan>(result.placed.floorplan);
        if (options.optimization_multirow_eco) {
          trial.multirow_eco = options.optimization_multirow_eco;
        }
        trial.changed_cells.clear();
        for (const auto& c : result.placed.floorplan.components) {
          if (removed.contains(std::string(c.name.str()))) {
            trial.changed_cells.push_back(c.name);
          }
        }
        trial.output_dir = options.output_dir + "/color_resynth/trial_" + std::to_string(request.serial + attempts);
        std::filesystem::create_directories(trial.output_dir);
        trial.own_frame = options.own_frame || options.floorplan.empty();
        trial.floorplan = trial.output_dir + "/floorplan.def";
        detail::write_eco_floorplan(result.placed.floorplan, trial.floorplan);
        auto owner = std::make_unique<hhds::GraphLibrary>();
        require_growth_budget(*graph, trial);
        auto placed              = place(*graph, *owner, trial);
        placed.gp_density_weight = result.placed.gp_density_weight;
        const auto next          = evaluate(placed);
        w.Key("delay");
        w.Double(next.delay);
        kept = request.goal == Color_request::Goal::timing ? timing_improves(score, next)
               : request.goal == Color_request::Goal::area ? no_timing_loss(score, next)
                                                           : true;
        if (kept && (!best || (timing_goal ? timing_improves(best->timing, next) : after_area < best->area))) {
          best = Passed{std::move(placed), std::move(owner), next, after_area};
        }
      } catch (const std::exception& e) {
        w.Key("reason");
        w.String(e.what());
      }
      w.Key("accepted");
      w.Bool(kept);
      w.EndObject();
    }
    if (best) {
      result.placed = std::move(best->placed);
      result.libraries.push_back(std::move(best->owner));
      score = std::move(best->timing);
      ++accepted;
      result.colors.push_back(candidate.view.name);
    }
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
  if (accepted != 0) {
    physical::write_def(result.placed.floorplan, options.output_dir + "/out.def");
  }
  const char* key = timing_goal ? "color_resynth" : "color_resynth_area";
  detail::publish_engine_qor(result.placed, prior_qor, {}, {}, key, result.report, options.output_dir);
  std::ofstream(options.output_dir + "/" + key + ".json") << result.report << '\n';
  // The area call can run several times in one final legalization; every call keeps its line.
  std::ofstream(options.output_dir + "/color_resynth.jsonl", std::ios::app) << result.report << '\n';
  result.timing = std::move(score);
  return result;
}
}  // namespace livehd::placement

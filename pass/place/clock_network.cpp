// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <filesystem>
#include <format>
#include <fstream>
#include <map>
#include <numeric>
#include <optional>
#include <set>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

#include "clock_model.hpp"
#include "clock_schedule.hpp"
#include "core/file_utils.hpp"
#include "core/hash_util.hpp"
#include "node_util.hpp"
#include "pass/liberty/timing_model.hpp"
#include "pass/partition/flatten.hpp"
#include "pass/place/pcrs/clock_gate.hpp"
#include "pass/place/pcrs/clock_tree.hpp"
#include "pass/timing/physical_rank.hpp"
#include "pass/timing/sdc.hpp"
#include "placement_technology.hpp"
#include "rapidjson/ostreamwrapper.h"
#include "rapidjson/writer.h"
#include "repair.hpp"
#include "repair_internal.hpp"
#include "row_occupancy.hpp"
#include "steiner_net.hpp"
#include "technology_rc.hpp"

namespace livehd::placement {
namespace {
namespace gu          = graph_util;
using Node            = hhds::Node_class;
using Pin             = hhds::Pin_class;
using Clock_instances = std::map<std::string, const physical::Component*, std::less<>>;

struct Pending_clock_report {
  std::filesystem::path path;
  explicit Pending_clock_report(const std::filesystem::path& target) {
    static std::atomic<uint64_t> sequence{};
    path = std::format("{}.{}.{}.tmp", target.string(), getpid(), sequence.fetch_add(1));
  }
  ~Pending_clock_report() {
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
  }
};

struct Clock_distribution {
  Name                                source_master;
  bool                                gate_leaf = false;
  std::optional<size_t>               merged_into;
  std::string                         net;
  size_t                              net_index   = 0;
  bool                                serves_gate = false;
  bool                                discrete_gate_feeder = false;
  std::vector<physical::Connection>   sinks, direct;
  std::vector<std::string>            names;
  Pin                                 source;
  std::optional<physical::Connection> source_connection;
  std::vector<pcrs::Clock_sink>       loads;
  std::vector<Pin>                    pins;
  pcrs::Clock_tree                    tree;
  pcrs::Clock_source                  source_model;
  std::string                         source_chunk;

  std::vector<size_t> branch_parents() const {
    std::vector<size_t> parents(names.size(), names.size());
    for (size_t i = 0; i < names.size(); ++i) {
      if (!tree.branches[i].leaf) {
        for (auto child : tree.branches[i].children) {
          parents.at(child) = i;
        }
      }
    }
    return parents;
  }
  bool minimize_source_latency(bool balance) const { return serves_gate || (balance && discrete_gate_feeder); }
};

// Use the same color, placement-block and macro identities as fp_plan. Pins
// without a planned object (ports and newly synthesized cells) use the nearest
// accepted region. A GUI reassignment therefore takes priority over position.

struct Previous_clock_branch {
  std::string            instance;
  bool                   leaf;
  std::vector<Pin_label> children;
};
using Clock_source_pin        = std::pair<Name, Name>;
using Previous_clock_branches = std::map<Clock_source_pin, std::vector<Previous_clock_branch>>;

// A previous invocation supplies identities only. Electrical sizing, positions
// and graph connectivity are computed from the current design.
Previous_clock_branches read_clock_history(const std::string& path) {
  Previous_clock_branches history;
  if (path.empty()) {
    return history;
  }
  const auto input = file_utils::read_file(path);
  rapidjson::Document report;
  if (!input || report.Parse(input->c_str()).HasParseError() || !report.IsObject() || !report.HasMember("networks")
      || !report["networks"].IsArray()) {
    return {};
  }
  for (const auto& net : report["networks"].GetArray()) {
    if (!net.IsObject() || !net.HasMember("source_instance") || !net["source_instance"].IsString() || !net.HasMember("source_pin")
        || !net["source_pin"].IsString() || !net.HasMember("branches") || !net["branches"].IsArray()) {
      return {};
    }
    auto& branches = history[{net["source_instance"].GetString(), net["source_pin"].GetString()}];
    for (const auto& branch : net["branches"].GetArray()) {
      if (!branch.IsObject() || !branch.HasMember("instance") || !branch["instance"].IsString()
          || !std::string_view(branch["instance"].GetString()).starts_with("__pcrs_clk_") || !branch.HasMember("leaf")
          || !branch["leaf"].IsBool() || !branch.HasMember("children") || !branch["children"].IsArray()) {
        return {};
      }
      auto& old = branches.emplace_back(Previous_clock_branch{branch["instance"].GetString(), branch["leaf"].GetBool(), {}});
      for (const auto& child : branch["children"].GetArray()) {
        if (!child.IsString()) {
          return {};
        }
        old.children.emplace_back(child.GetString());
      }
    }
  }
  return history;
}

Previous_clock_branches strip_clock_buffers(Result& work, const physical::Technology& tech) {
  std::map<std::string, Node, std::less<>> marked;
  for (auto node : work.graph->body().nodes()) {
    if (node.attr(attrs::pcrs_clock_buffer).has()) {
      marked.emplace(std::string(node.get_name()), node);
    }
  }
  auto&                fp = work.floorplan;
  std::map<Name, Name> masters;
  for (const auto& c : fp.components) {
    masters.emplace(c.name, c.macro);
  }
  std::map<std::string, size_t, std::less<>> incoming, outgoing;
  std::vector<std::string>                   driver(fp.nets.size());
  for (size_t i = 0; i < fp.nets.size(); ++i) {
    for (const auto& pin : fp.nets[i].connections) {
      if (!marked.contains(pin.instance.view())) {
        continue;
      }
      const auto& row     = tech.macros.at(masters.at(pin.instance)).pins.at(pin.pin);
      auto&       indexed = row.direction == "OUTPUT" ? outgoing : incoming;
      if (!indexed.emplace(pin.instance.str(), i).second) {
        throw std::runtime_error("multiply connected PCRS clock buffer " + pin.instance.str());
      }
      if (row.direction == "OUTPUT") {
        driver[i] = pin.instance.str();
      }
      work.pin_locations.erase(physical::connection_label(pin));
    }
  }
  if (outgoing.size() != marked.size() || incoming.size() != marked.size()) {
    throw std::runtime_error("incomplete native clock buffer connections during rebuild");
  }
  Previous_clock_branches            previous;
  std::set<size_t>                   removed;
  std::map<size_t, Clock_source_pin> sources;
  for (auto& [name, node] : marked) {
    size_t           root = incoming.at(name);
    const Node*      root_buffer = &node;
    std::set<size_t> path;
    while (!driver[root].empty()) {
      if (!path.insert(root).second) {
        throw std::runtime_error("cycle in native clock tree during rebuild");
      }
      root_buffer = &marked.at(driver[root]);
      root        = incoming.at(driver[root]);
    }
    auto [source, fresh] = sources.try_emplace(root);
    if (fresh) {
      const auto inputs = root_buffer->inp_sorted_pins();
      if (std::distance(inputs.begin(), inputs.end()) != 1) {
        throw std::runtime_error("non-unary native clock buffer " + std::string(root_buffer->get_name()));
      }
      const auto input = (*inputs.begin()).get_driver_pin();
      const auto owner = input.get_master_node();
      source->second   = {owner.is_input_node() ? Name("PIN") : Name(owner.get_name()), Name(input.get_pin_name())};
    }
    const size_t net = outgoing.at(name);
    removed.insert(net);
    fp.snapshot_ndr.net_rules.erase(fp.nets[net].name.str());
    auto& branch = previous[source->second].emplace_back(Previous_clock_branch{name, true, {}});
    for (const auto& pin : fp.nets[net].connections) {
      if (pin.instance == name) {
        continue;
      }
      if (marked.contains(pin.instance.view())) {
        branch.children.push_back(pin.instance.str());
        branch.leaf = false;
      } else {
        branch.children.push_back(physical::connection_label(pin));
        fp.nets[root].connections.push_back(pin);
      }
    }
    if (!fp.nets[root].wires.empty() || !fp.nets[net].wires.empty()) {
      throw std::runtime_error("clock rebuild requires a pre-route placement");
    }
  }
  for (const auto& [name, node] : marked) {
    auto inputs = node.inp_sorted_pins();
    if (std::distance(inputs.begin(), inputs.end()) != 1) {
      throw std::runtime_error("non-unary native clock buffer " + name);
    }
    auto root = (*inputs.begin()).get_driver_pin();
    while (root.get_master_node().attr(attrs::pcrs_clock_buffer).has()) {
      root = (*root.get_master_node().inp_sorted_pins().begin()).get_driver_pin();
    }
    for (auto out : node.out_pins_snapshot()) {
      std::vector<Pin> sinks;
      for (const auto& edge : out.out_edges()) {
        sinks.push_back(edge.sink);
      }
      for (auto sink : sinks) {
        if (!sink.get_master_node().attr(attrs::pcrs_clock_buffer).has()) {
          gu::drop_drivers(sink);
          sink.connect_driver(root);
        }
      }
    }
  }
  for (auto& [_, node] : marked) {
    node.del_node();
  }
  for (auto& net : fp.nets) {
    std::erase_if(net.connections, [&](const auto& pin) { return marked.contains(pin.instance.view()); });
  }
  size_t index = 0;
  std::erase_if(fp.nets, [&](const auto&) { return removed.contains(index++); });
  std::erase_if(fp.components, [&](const auto& cell) { return marked.contains(cell.name.view()); });
  work.parasitics.reset();
  work.preroute_design.reset();
  return previous;
}
// Work from the leaves up. Reserve a previous identity for the greatest
// overlap before assigning fresh hashes, with deterministic ties between siblings.
void assign_clock_names(Clock_distribution& d, const Previous_clock_branches& previous,
                        const std::map<std::string, Node, std::less<>>& nodes, std::set<std::string>& assigned) {
  const auto history = previous.find({d.source_connection->instance, d.source_connection->pin});
  std::map<Pin_label, const Previous_clock_branch*> owner;
  if (history != previous.end()) {
    for (const auto& branch : history->second) {
      for (const auto& child : branch.children) {
        owner.emplace(child, &branch);
      }
    }
  }
  std::vector<unsigned>            height(d.tree.branches.size());
  std::vector<std::vector<size_t>> levels;
  d.names.resize(height.size());
  for (size_t i = 0; i < height.size(); ++i) {
    const auto& branch = d.tree.branches[i];
    if (!branch.leaf) {
      for (auto child : branch.children) {
        height[i] = std::max(height[i], 1 + height.at(child));
      }
    }
    if (levels.size() <= height[i]) {
      levels.resize(height[i] + 1);
    }
    levels[height[i]].push_back(i);
  }
  for (const auto& level : levels) {
    struct Match {
      size_t      branch, overlap;
      std::string name;
      bool        exact;
    };
    std::vector<Match> matches;
    for (auto i : level) {
      const auto&                                    branch = d.tree.branches[i];
      uint64_t                                       hash   = hash_util::fnv1a64(d.net);
      std::map<const Previous_clock_branch*, size_t> votes;
      for (auto child : branch.children) {
        if (branch.leaf) {
          const auto& id = d.loads.at(child).name;
          hash           = hash_util::fnv1a64_u64(id.size(), hash);
          if (!id.bare()) {
            hash = hash_util::fnv1a64(id.instance().view(), hash);
            hash = hash_util::fnv1a64(":", hash);
          }
          hash = hash_util::fnv1a64(id.pin().view(), hash);
          if (const auto it = owner.find(id); it != owner.end() && it->second->leaf) {
            ++votes[it->second];
          }
        } else {
          const auto& id = d.names.at(child);
          hash           = hash_util::fnv1a64_u64(id.size(), hash);
          hash           = hash_util::fnv1a64(id, hash);
          if (!owner.empty()) {
            if (const auto it = owner.find(id); it != owner.end() && !it->second->leaf) {
              ++votes[it->second];
            }
          }
        }
      }
      d.names[i] = std::format("__pcrs_clk_{:016x}", hash);
      for (const auto& [old, overlap] : votes) {
        matches.push_back({i, overlap, old->instance, overlap == old->children.size() && overlap == branch.children.size()});
      }
    }
    std::ranges::sort(matches, [](const Match& a, const Match& b) {
      if (a.exact != b.exact) {
        return a.exact;
      }
      if (a.overlap != b.overlap) {
        return a.overlap > b.overlap;
      }
      return std::tie(a.name, a.branch) < std::tie(b.name, b.branch);
    });
    std::set<size_t> reused;
    for (const auto& m : matches) {
      if (reused.contains(m.branch) || nodes.contains(m.name) || !assigned.insert(m.name).second) {
        continue;
      }
      d.names[m.branch] = m.name;
      reused.insert(m.branch);
    }
    for (auto i : level) {
      if (reused.contains(i)) {
        continue;
      }
      // A fresh name is a hash of the members, and a retained name stays with the branch that kept most of its old
      // members: after a regrouping the two can meet (a third build in one workdir). Take the next free identity.
      auto& name = d.names[i];
      auto  hash = hash_util::fnv1a64(name);
      for (uint64_t salt = 1; nodes.contains(name) || !assigned.insert(name).second; ++salt) {
        hash = hash_util::fnv1a64_u64(salt, hash);
        name = std::format("__pcrs_clk_{:016x}", hash);
      }
    }
  }
}
Ann_place clock_pose(const pcrs::Clock_branch& branch, const physical::Macro& m, int64_t dbu) {
  Ann_place pose((branch.x_um - m.width / 2) * dbu, (branch.y_um - m.height / 2) * dbu, m.width * dbu, m.height * dbu);
  pose.status = Ann_place::Status::PLACED;
  return pose;
}

void place_clock_branches(physical::Floorplan& fp, const physical::Technology& tech, const std::vector<Clock_distribution>& plans,
                          const std::vector<pcrs::Clock_driver>& drivers) {
  std::map<std::string, physical::Terminal*, std::less<>> terminals;
  for (auto& t : fp.terminals) {
    terminals.emplace(t.name, &t);
  }
  std::set<std::string_view> net_names;
  for (const auto& net : fp.nets) {
    net_names.insert(net.name.view());
  }
  for (const auto& terminal : fp.terminals) {
    net_names.insert(terminal.name);
  }
  for (const auto& d : plans) {
    if (d.merged_into) {
      continue;
    }
    if (d.tree.branches.empty()) {
      auto& root = fp.nets.at(d.net_index);
      if (d.gate_leaf) {
        root.connections = {*d.source_connection};
        root.connections.insert(root.connections.end(), d.sinks.begin(), d.sinks.end());
      }
      root.use = "CLOCK";
      continue;
    }
    const auto& names       = d.names;
    const auto  child_input = [&](size_t child) -> physical::Connection {
      return {names.at(child), drivers.at(d.tree.branches.at(child).driver).input};
    };
    for (size_t i = 0; i < names.size(); ++i) {
      const auto& branch = d.tree.branches[i];
      const auto& name   = names[i];
      const auto& driver = drivers[branch.driver];
      fp.components.push_back({name, driver.master, clock_pose(branch, tech.macros.at(driver.master), fp.dbu)});
      const std::string base = name + "_" + driver.output;
      physical::Net     branch_net{base, {{name, driver.output}}, {}, "CLOCK"};
      for (unsigned suffix = 1; !net_names.insert(branch_net.name.view()).second; ++suffix) {
        branch_net.name = base + "_" + std::to_string(suffix);
      }
      for (auto child : branch.children) {
        const auto conn = branch.leaf ? d.sinks.at(child) : child_input(child);
        branch_net.connections.push_back(conn);
        if (conn.instance == "PIN") {
          terminals.at(conn.pin.str())->net = branch_net.name.str();
        }
      }
      if (const auto rule = fp.snapshot_ndr.net_rules.find(d.net); rule != fp.snapshot_ndr.net_rules.end()) {
        fp.snapshot_ndr.net_rules.emplace(branch_net.name.str(), rule->second);
      }
      fp.nets.push_back(std::move(branch_net));
    }
    auto& root_net       = fp.nets.at(d.net_index);
    root_net.connections = {*d.source_connection};
    for (auto root : d.tree.roots) {
      root_net.connections.push_back(child_input(root));
    }
    root_net.connections.insert(root_net.connections.end(), d.direct.begin(), d.direct.end());
    root_net.wires.clear();
    root_net.use = "CLOCK";
  }
}

double max_input_cap(const liberty::timing::Pin& pin) {
  double cap = 0;
  for (size_t t = 0; t < 2; ++t) {
    cap = std::max(cap, pin.capacitance_range_ff[t] ? (*pin.capacitance_range_ff[t])[1] : pin.capacitance_ff[t].value_or(0));
  }
  return cap;
}

std::vector<pcrs::Clock_driver> clock_drivers(const Technology& tech, const liberty::timing::Library& timing) {
  std::vector<pcrs::Clock_driver> drivers;
  for (const auto& cell : tech.cells.combinational) {
    if (cell.inputs.size() != 1 || cell.physical_barrier || liberty::cell_truth(cell) != 0xaaaaaaaaaaaaaaaaULL) {
      continue;
    }
    const auto master = tech.lef.macros.find(cell.name);
    if (master == tech.lef.macros.end() || !master->second.klass.starts_with("CORE")
        || !master->second.pins.contains(cell.inputs.front()) || !master->second.pins.contains(cell.output)) {
      continue;
    }
    drivers.push_back({cell.name,
                       cell.inputs.front(),
                       cell.output,
                       max_input_cap(timing.cells.at(cell.name).pins.at(cell.inputs.front())),
                       cell.max_capacitance_ff.value_or(0),
                       cell.area,
                       cell.resistance_ohm,
                       cell.max_transition_ps.value_or(0),
                       static_cast<size_t>(std::max(0.0, cell.max_fanout.value_or(0))),
                       cell.intrinsic_ps});
    pcrs::characterize_clock_driver(drivers.back(), timing.cells.at(cell.name));
  }
  return drivers;
}

const liberty::timing::Pin& load_pin(const liberty::timing::Library& timing, const physical::Component& instance,
                                     const physical::Connection& conn) {
  const auto cell = timing.cells.find(instance.macro);
  if (cell != timing.cells.end()) {
    const auto pin = cell->second.pins.find(conn.pin);
    if (pin != cell->second.pins.end()) {
      return pin->second;
    }
  }
  throw std::runtime_error("clock sink absent from timing library: " + physical::connection_label(conn));
}
// Evaluate Tcl object queries on the same physical names the tree will use.
// The existing placement holds the pins and cells, so parsing drive constraints
// does not require binding another copy of the timing graph.

struct Phase_moves {
  std::map<size_t, Ann_place>  poses;
  std::map<std::string, Point> pins;
};

Phase_moves place_private_phases(const Result& placed, const physical::Technology& tech, const pcrs::Gate_clock_paths& gates,
                                 const Clock_instances& instances) {
  Phase_moves result;
  const auto& fp = placed.floorplan;
  if (gates.private_phase_targets.empty() || fp.rows.empty()) {
    return result;
  }
  auto rows = unoccupied_rows(tech, fp);
  for (const auto& [name, target] : gates.private_phase_targets) {
    const auto& c = *instances.at(name);
    const auto& m = tech.macros.at(c.macro);
    if (c.place.status != physical::Status::PLACED || !m.klass.starts_with("CORE") || m.origin_x || m.origin_y
        || std::ranges::any_of(fp.blockages, [&](const auto& b) { return b.component == name; })) {
      continue;
    }
    const auto&  pin = placed.pin_locations.at(target);
    const double x = pin.first - m.width * fp.dbu / 2, y = pin.second - m.height * fp.dbu / 2;
    const auto   pose = closest_free_site(fp, rows, m, x, y);
    if (!pose || std::abs(pose->x - x) + std::abs(pose->y - y) >= std::abs(c.place.x - x) + std::abs(c.place.y - y)) {
      continue;
    }
    reserve_gaps(rows, physical::placed_rect(m, *pose, fp.dbu, {0, 0, m.width, m.height}));
    result.poses.emplace(&c - fp.components.data(), *pose);
    for (const auto& [terminal, info] : m.pins) {
      const auto label = physical::connection_label(c.name, terminal);
      if (placed.pin_locations.contains(label)) {
        const auto off = placement_pin_offset(m, terminal, pose->orientation, fp.dbu);
        result.pins.emplace(label, Point{pose->x + off.first, pose->y + off.second});
      }
    }
  }
  return result;
}

class Clock_sdc_design final : public timing::fast::Sdc_design {
public:
  Clock_sdc_design(const physical::Floorplan& fp, const physical::Technology& tech, const Clock_instances& cells)
      : floorplan(fp), technology(tech), instances(cells) {
    for (const auto& net : fp.nets) {
      net_names.insert(net.name);
    }
  }

  mutable bool queried_internals = false;
  void         ports(const std::function<void(std::string_view, bool)>& visit) const override {
    for (const auto& port : floorplan.terminals) {
      visit(port.name, port.direction == "INPUT");
    }
  }
  void pins(const std::function<void(std::string_view, std::string_view)>& visit) const override {
    queried_internals = true;
    for (const auto& [instance, component] : instances) {
      for (const auto& [name, pin] : technology.macros.at(component->macro).pins) {
        visit(instance, name);
      }
    }
  }
  bool has_pin(std::string_view instance, std::string_view pin) const override {
    queried_internals = true;
    const auto it     = instances.find(instance);
    return it != instances.end() && technology.macros.at(it->second->macro).pins.contains(std::string(pin));
  }
  void cells(const std::function<void(std::string_view)>& visit) const override {
    queried_internals = true;
    for (const auto& [name, component] : instances) {
      visit(name);
    }
  }
  bool has_cell(std::string_view name) const override {
    queried_internals = true;
    return instances.contains(name);
  }
  void nets(const std::function<void(std::string_view)>& visit) const override {
    queried_internals = true;
    for (const auto& net : floorplan.nets) {
      visit(net.name);
    }
  }
  bool has_net(std::string_view name) const override {
    queried_internals = true;
    return net_names.contains(name);
  }

private:
  const physical::Floorplan&  floorplan;
  const physical::Technology& technology;
  const Clock_instances&      instances;
  std::set<std::string_view>  net_names;
};

pcrs::Clock_driver source_cell(const Technology& tech, const liberty::timing::Cell& cell, const liberty::timing::Pin& out,
                               const Prects_library& roles, std::optional<double> input_slew, std::string_view clock_input = {}) {
  pcrs::Clock_driver model;
  model.master      = cell.name;
  model.output      = out.name;
  model.max_load_ff = out.max_capacitance_ff.value_or(0);
  model.max_slew_ps = out.max_transition_ps.value_or(0);
  for (const auto& drive : tech.cells.drives) {
    if (drive.name == cell.name && drive.output == out.name) {
      model.max_fanout     = static_cast<size_t>(drive.max_fanout.value_or(0));
      model.max_load_ff    = out.max_capacitance_ff.value_or(drive.max_capacitance_ff.value_or(0));
      model.max_slew_ps    = out.max_transition_ps.value_or(drive.max_transition_ps.value_or(0));
      model.resistance_ohm = drive.resistance_ohm;
      model.intrinsic_ps   = drive.intrinsic_ps;
      break;
    }
  }
  // For a gate with a known clock input, exclude its enable arc. At another
  // generated clock output, bound the waveform over its characterized inputs.
  const auto            links = roles.links.find(cell.name);
  std::set<std::string> inputs;
  for (const auto& arc : out.arcs) {
    if (!clock_input.empty() && arc.related_pin != clock_input) {
      continue;
    }
    if (links == roles.links.end()
        || std::ranges::find(links->second, std::pair{arc.related_pin, out.name}) != links->second.end()) {
      inputs.insert(arc.related_pin);
    }
  }
  for (const auto& name : inputs) {
    const auto input = cell.pins.find(name);
    if (input == cell.pins.end()) {
      continue;
    }
    pcrs::Clock_driver arc_driver;
    arc_driver.input    = name;
    arc_driver.output   = out.name;
    arc_driver.input_ff = max_input_cap(input->second);
    pcrs::characterize_clock_driver(arc_driver, cell, input_slew);
    model.arcs.insert(model.arcs.end(),
                      std::make_move_iterator(arc_driver.arcs.begin()),
                      std::make_move_iterator(arc_driver.arcs.end()));
  }
  model.reference_slew_ps = input_slew.value_or(0);
  return model;
}
}  // namespace
Clock_chunk_homes::Clock_chunk_homes(const std::shared_ptr<const std::string>& plan) {
  if (!plan) {
    return;
  }
  rapidjson::Document doc;
  doc.Parse(plan->c_str());
  if (!doc.IsObject() || !doc.HasMember("chunks") || !doc["chunks"].IsArray()) {
    throw std::runtime_error("invalid accepted chunk plan at clock synthesis");
  }
  if (doc["chunks"].Size() < 2) {
    return;
  }
  for (const auto& row : doc["chunks"].GetArray()) {
    std::string home = row["id"].GetString();
    for (const auto& member : row["members"].GetArray()) {
      if (!members.emplace(std::pair{member["macro"].GetBool(), std::string(member["id"].GetString())}, home).second) {
        throw std::runtime_error("ambiguous floorplan owner at clock synthesis");
      }
    }
    auto& boxes = regions[home];
    auto  add   = [&](const auto& b) { boxes.push_back({b[0].GetDouble(), b[1].GetDouble(), b[2].GetDouble(), b[3].GetDouble()}); };
    if (row.HasMember("region_dbu")) {
      for (const auto& b : row["region_dbu"].GetArray()) {
        add(b);
      }
    }
    if (boxes.empty()) {
      add(row["box_dbu"]);
    }
  }
}

std::string Clock_chunk_homes::locate(bool macro, std::string_view key, Point pos) const {
  if (regions.empty()) {
    return {};
  }
  auto owner = members.find({macro, std::string(key)});
  if (owner == members.end()) {
    owner = members.find({macro, gu::logical_hier_name(std::string(key), true)});
  }
  if (owner != members.end()) {
    return owner->second;
  }
  double           best = std::numeric_limits<double>::infinity();
  std::string_view result;
  for (const auto& [name, boxes] : regions) {
    for (const auto& b : boxes) {
      const double distance
          = std::max({b[0] - pos.first, pos.first - b[2], 0.}) + std::max({b[1] - pos.second, pos.second - b[3], 0.});
      if (distance < best) {
        best   = distance;
        result = name;
      }
    }
  }
  return std::string(result);
}

std::string Clock_chunk_homes::home(const physical::Connection& pin, const std::map<std::string, Node, std::less<>>& nodes,
                                    const Clock_instances& instances, bool macro, Point pos) const {
  if (regions.empty()) {
    return {};
  }
  std::string key;
  if (pin.instance != "PIN") {
    auto node = nodes.at(pin.instance.str());
    if (macro) {
      key = pin.instance.str();
    } else if (gu::Color_group::has(node)) {
      key = gu::Color_group::text(node);
    } else if (gu::Placement_block::has(node)) {
      key = gu::Placement_block::text(node);
    }
    const auto& pose = instances.at(pin.instance.str())->place;
    pos              = {pose.x + .5 * pose.width, pose.y + .5 * pose.height};
  }
  return locate(macro, key, pos);
}

Clock_planning_model clock_planning_model(const Options& options, const liberty::timing::Library& timing, double dbu) {
  const auto& tech = options.technology();

  const auto  wires  = technology_net_wire_rc(tech.lef,
                                            options.liberty,
                                            options.tech_rc_file,
                                            options.resistance_ohm_um,
                                            options.capacitance_ff_um,
                                            options.rc_pessimism,
                                            tech.cells.contents,
                                            options.clock_rc_layers());
  const auto& rc     = wires.clock;
  const auto  scaled = [&](const std::array<double, 2>& factors) {
    return Wire_rc{rc.horizontal_r * factors[0],
                   rc.horizontal_c * factors[1],
                   rc.vertical_r * factors[0],
                   rc.vertical_c * factors[1],
                   rc.source};
  };
  const auto         leaf_rc = scaled(options.clock_leaf_rc_scale), trunk_rc = scaled(options.clock_trunk_rc_scale);
  pcrs::Clock_limits limits{options.clock_leaf_fanout,
                            std::max(rc.horizontal_c, rc.vertical_c),
                            std::max(rc.horizontal_r, rc.vertical_r),
                            options.clock_leaf_radius_um};
  limits.balance        = options.clock_balance;
  limits.target_slew_ps = options.clock_max_slew_ps;
  limits.trunk_drive_steps = options.clock_trunk_drive_steps;
  limits.split_scheduled_leaves
      = (options.clock_balance || options.clock_split_scheduled_leaves) && !options.clock_leaf_schedule.empty();
  limits.leaf_rc                = per_um(leaf_rc);
  limits.trunk_rc               = per_um(trunk_rc);
  limits.dbu_per_um             = dbu;
  limits.directional_rc         = per_um(rc);

  return {clock_drivers(tech, timing), wires, leaf_rc, trunk_rc, limits};
}

std::set<std::string> build_clock_tree(Result& placed, const Options& options, const std::vector<std::string>& macro_libraries,
                                       std::string_view sdc_text) {
  return build_clock_tree(placed, options, macro_libraries, sdc_text, nullptr);
}

std::set<std::string> build_clock_tree(Result& placed, const Options& options, const std::vector<std::string>& macro_libraries,
                                       std::string_view sdc_text, std::unique_ptr<timing::fast::Physical_ranker> timer) {
  return build_clock_handoff(placed, options, macro_libraries, sdc_text, std::move(timer)).buffers;
}

static Clock_handoff build_clock_handoff_impl(Result& placed, const Options& options,
                                              const std::vector<std::string>& macro_libraries, std::string_view sdc_text,
                                              std::unique_ptr<timing::fast::Physical_ranker> timer,
                                              const Previous_clock_branches&                 previous) {
  Clock_driver_pins driver_pins;
  const auto&       tech = options.technology();
  // Round up the largest per-design 99th percentile of pre-route optimism on near-hold paths (minimax, litedram, LiteEth).
  const double      hold_margin_ps
      = options.clock_skew_hold_margin_ps.value_or(tech.lef.macros.contains("BUFx2_ASAP7_75t_R") ? 29. : 0.);
  auto&                 graph          = *placed.graph;
  auto&        fp             = placed.floorplan;
  std::string  extra;
  std::set<std::string> loaded{options.liberty};
  for (const auto& path : macro_libraries) {
    if (loaded.insert(path).second) {
      const auto contents = file_utils::read_file(path);
      if (!contents) {
        throw std::runtime_error("cannot read PCRS macro timing library: " + path);
      }
      extra += "\n" + *contents;
    }
  }
  const auto  clock_library = tech.clock_library(std::move(extra));
  const auto& timing        = *clock_library;
  auto                               roles = prects_networks(tech.lef, fp, placed.prects);
  const auto                         gates = pcrs::discrete_gate_paths(fp, tech.cells, roles.clock_nets);
  std::map<size_t, std::string_view> gate_clock_arcs;
  std::set<Pin_label> discrete_clock_inputs;
  for (const auto& gate : gates.gates) {
    // The common fork and the actual gate input can be on different nets.
    // Both carry clock insertion to the gated sinks, including when an
    // opposite-edge enable permits buffering each arm independently.
    discrete_clock_inputs.emplace(gate.clock_fork_pin);
    discrete_clock_inputs.emplace(gate.instance, gate.clock_pin);
    roles.clock_nets[gate.output_net]   = true;
    roles.clock_nets[gate.upstream_net] = true;
    gate_clock_arcs.emplace(gate.output_net, gate.clock_pin);
  }
  for (const auto& [net, pins] : gates.direct_sinks) {
    roles.clock_nets[net] = true;
  }
  auto unmatched_targets                           = options.clock_arrival_targets;
  auto [drivers, wires, leaf_rc, trunk_rc, limits] = clock_planning_model(options, timing, fp.dbu);
  limits.leaf_drive_steps                          = options.clock_leaf_drive_steps;
  if (!fp.rows.empty()) {
    limits.repeater_sites = 0;
    for (const auto& row : fp.rows) {
      limits.repeater_sites += std::max<int64_t>(0, row.count);
    }
  }
  std::map<std::string, Node, std::less<>> nodes;
  for (auto node : graph.body().nodes()) {
    if (!node.get_name().empty()) {
      nodes.emplace(std::string(node.get_name()), node);
    }
  }
  Clock_instances instances;
  for (const auto& c : fp.components) {
    instances.emplace(c.name, &c);
  }
  const auto phases       = place_private_phases(placed, tech.lef, gates, instances);
  const auto pin_position = [&](const std::string& label) -> Point {
    if (const auto it = phases.pins.find(label); it != phases.pins.end()) {
      return it->second;
    }
    const auto it = placed.pin_locations.find(label);
    if (it == placed.pin_locations.end()) {
      throw std::runtime_error("missing placed clock pin: " + label);
    }
    return it->second;
  };
  std::map<std::string, physical::Terminal*, std::less<>> terminals;
  for (auto& port : fp.terminals) {
    terminals.emplace(port.name, &port);
  }
  timing::fast::Sdc             sdc;
  bool                          internal_constraints = false;
  const timing::fast::Sdc_units units{timing.time_unit_ps, timing.capacitance_unit_ff};
  if (!sdc_text.empty()) {
    Clock_sdc_design design(fp, tech.lef, instances);
    sdc                  = timing::fast::read_sdc(sdc_text, units, design);
    internal_constraints = design.queried_internals;
  }
  {
    // Half of the skew the period tolerates is the gap between sibling subtrees that balancing leaves alone.
    double period = std::numeric_limits<double>::infinity();
    for (const auto& clock : sdc.clocks) {
      if (clock.period_ps > 0) {
        period = std::min(period, clock.period_ps);
      }
    }
    limits.skew_bound_ps = std::isfinite(period) ? options.clock_skew_period_fraction * period / 2 : 0.;
  }
  std::map<std::string, double> port_slews;
  for (const auto& t : sdc.transitions) {
    port_slews[t.port.name] = t.slew_ps;
  }
  std::map<std::string, const timing::fast::Sdc::Drive*> external_drives;
  for (const auto& drive : sdc.drives) {
    external_drives[drive.port.name] = &drive;
  }

  std::vector<Clock_distribution> plans;
  const auto&             accepted_plan = placed.accepted_chunk_plan ? placed.accepted_chunk_plan : options.accepted_chunk_plan;
  const Clock_chunk_homes chunk_homes(accepted_plan);
  std::set<std::string>   buffers;
  for (size_t i = 0; i < fp.nets.size(); ++i) {
    if (!roles.clock_nets.at(i)) {
      continue;
    }
    const auto&        net = fp.nets[i];
    Clock_distribution d;
    const auto         retained                   = gates.direct_sinks.find(i);
    d.net_index                                   = i;
    d.net                                         = net.name;
    d.serves_gate                                 = gate_clock_arcs.contains(i);
    std::vector<physical::Connection> connections = net.connections;
    std::ranges::sort(connections,
                      [](const auto& a, const auto& b) { return std::tie(a.instance, a.pin) < std::tie(b.instance, b.pin); });
    // Two pins at a discrete gate fork must see the same clock edge. If the
    // floorplan separates them, retain both on their existing common source.
    std::set<std::string> cross_chunk_forks;
    if (!chunk_homes.regions.empty() && !gates.leaf_group.empty()) {
      std::map<std::string, std::string> homes;
      for (const auto& c : connections) {
        const auto label = physical::connection_label(c);
        if (auto g = gates.leaf_group.find(label); g != gates.leaf_group.end()) {
          const auto home  = chunk_homes.home(c, nodes, instances, false, pin_position(label));
          auto [it, added] = homes.try_emplace(g->second, home);
          if (!added && it->second != home) {
            cross_chunk_forks.insert(g->second);
          }
        }
      }
    }
    for (const auto& c : connections) {
      const bool source = c.instance == "PIN"
                              ? terminals.at(c.pin.str())->direction == "INPUT"
                              : tech.lef.macros.at(instances.at(c.instance.str())->macro).pins.at(c.pin).direction == "OUTPUT";
      if (source) {
        if (d.source_connection && *d.source_connection != c) {
          throw std::runtime_error("multiple physical clock drivers on " + d.net);
        }
        d.source_connection = c;
        continue;
      }
      Pin    sink;
      bool   macro = false;
      double cap = 0, max_slew = 0, insertion = 0;
      if (c.instance == "PIN") {
        sink = graph.get_output_pin(c.pin);
      } else {
        const auto& inst   = *instances.at(c.instance.str());
        const auto& master = tech.lef.macros.at(inst.macro);
        sink               = nodes.at(c.instance.str()).get_sink_pin(c.pin);
        macro              = physical::hard_macro(master);
        const auto& pin    = load_pin(timing, inst, c);
        if (discrete_clock_inputs.contains({c.instance, c.pin})) {
          d.discrete_gate_feeder = true;
        }
        if (timing.cells.at(inst.macro).clock_gate && pin.clock) {
          d.serves_gate = true;
        }
        if (macro) {
          if (const auto* model = liberty::timing::clock_tree_path(pin, liberty::timing::Transition::rise, false)) {
            insertion = model->lookup({.input_slew_ps = drivers.empty() ? 0.0 : drivers.front().reference_slew_ps});
          }
        }
        cap      = max_input_cap(pin);
        max_slew = pin.max_transition_ps.value_or(0);
      }
      const auto label = physical::connection_label(c);
      if (sink.is_invalid() || sink.get_driver_pins().size() != 1) {
        throw std::runtime_error("clock sink needs a single mapped driver: " + label);
      }
      const auto driver = sink.get_driver_pin();
      if (!d.source.is_invalid() && d.source != driver) {
        throw std::runtime_error("multiple clock sources on " + d.net);
      }
      d.source             = driver;
      const auto       pos = pin_position(label);
      pcrs::Clock_sink load{Pin_label(c.instance, c.pin), pos.first / fp.dbu, pos.second / fp.dbu, cap, macro, max_slew};
      load.graph_pin    = sink.get_class_index().value;
      load.insertion_ps = insertion;
      if (const auto target = unmatched_targets.find(label); target != unmatched_targets.end()) {
        load.insertion_ps = target->second.first.value_or(insertion);
        load.wanted_ps    = target->second.second;
        unmatched_targets.erase(target);
      }
      load.chunk       = chunk_homes.home(c, nodes, instances, macro, pos);
      const auto group = gates.leaf_group.find(label);
      if ((retained != gates.direct_sinks.end() && retained->second.contains(label))
          || (group != gates.leaf_group.end() && cross_chunk_forks.contains(group->second))) {
        d.direct.push_back(c);
        d.source_model.direct.push_back(std::move(load));
        continue;
      }
      if (group != gates.leaf_group.end()) {
        d.serves_gate      = true;
        load.coupled_group = group->second;
        if (phases.pins.contains(label)) {
          const auto& old        = placed.pin_locations.at(label);
          load.previous_position = Point{old.first / fp.dbu, old.second / fp.dbu};
        }
      }
      d.loads.push_back(std::move(load));
      d.sinks.push_back(c);
      d.pins.push_back(sink);
    }
    if (!d.loads.empty() || !d.direct.empty()) {
      if (!d.source_connection) {
        throw std::runtime_error("missing physical clock source on " + d.net);
      }
      const auto& conn  = *d.source_connection;
      const auto  label = physical::connection_label(conn);
      const auto  pos   = pin_position(label);
      if (conn.instance != "PIN") {
        const bool macro = physical::hard_macro(tech.lef.macros.at(instances.at(conn.instance.str())->macro));
        d.source_chunk   = chunk_homes.home(conn, nodes, instances, macro, pos);
      }
      pcrs::Clock_source source{pos.first / fp.dbu, pos.second / fp.dbu, {}};
      source.chunk  = d.source_chunk;
      source.direct = std::move(d.source_model.direct);
      if (conn.instance != "PIN") {
        const auto& inst  = *instances.at(conn.instance.str());
        d.source_master   = inst.macro;
        const auto& cell  = timing.cells.at(inst.macro);
        const auto  arc   = gate_clock_arcs.find(i);
        d.serves_gate    |= cell.clock_gate;
        source.driver     = source_cell(tech,
                                    cell,
                                    cell.pins.at(conn.pin.str()),
                                    placed.prects,
                                        {},
                                    arc == gate_clock_arcs.end() ? std::string_view{} : arc->second);
      } else {
        const auto name = conn.pin.str();
        if (const auto slew = port_slews.find(name); slew != port_slews.end()) {
          source.driver.reference_slew_ps = slew->second;
        }
        if (const auto it = external_drives.find(name); it != external_drives.end()) {
          const auto& drive = *it->second;
          const auto  cell  = timing.cells.find(drive.cell);
          if (cell == timing.cells.end()) {
            throw std::runtime_error("unknown SDC driving cell for clock source " + label + ": " + drive.cell);
          }
          auto pin = std::ranges::find_if(cell->second.pins, [&](const auto& item) {
            return item.second.direction == "output" && (drive.pin.empty() || drive.pin == item.first);
          });
          if (pin == cell->second.pins.end()) {
            throw std::runtime_error("unknown SDC driving pin for clock source " + label + ": " + drive.pin);
          }
          source.driver = source_cell(tech, cell->second, pin->second, placed.prects, drive.input_slew_ps);
        }
      }
      d.source_model = std::move(source);
      plans.push_back(std::move(d));
    }
  }
  if (!unmatched_targets.empty()) {
    throw std::invalid_argument("unknown PCRS arrival target pin " + unmatched_targets.begin()->first);
  }
  const bool                          reuse_trial_plans = timer && options.clock_incremental_trials;
  std::vector<pcrs::Clock_plan_cache> plan_cache(plans.size());
  std::unordered_map<uint64_t, Pin_label> pin_names;
  std::unordered_map<Pin_label, uint64_t> pin_ids;
  for (const auto& domain : plans) {
    for (const auto& sink : domain.loads) {
      pin_names.emplace(sink.graph_pin, sink.name);
      if (!options.clock_leaf_schedule.empty() || !options.clock_leaf_delays_ps.empty()) pin_ids.emplace(sink.name, sink.graph_pin);
    }
  }
  Clock_schedule schedule;
  schedule.balance            = options.clock_balance;
  schedule.balance_unverified = options.clock_balance_unverified;
  for (const auto& [name, steps] : options.clock_leaf_schedule) {
    const auto pin = pin_ids.find(name);
    if (pin == pin_ids.end()) {
      throw std::invalid_argument("clock leaf schedule names an unknown sink: " + name);
    }
    schedule.drive_steps.emplace(pin->second, steps);
  }
  for (const auto& [name, delay] : options.clock_leaf_delays_ps) {
    const auto pin = pin_ids.find(name);
    if (pin == pin_ids.end() || !schedule.drive_steps.contains(pin->second)) {
      throw std::invalid_argument("clock leaf delay has no corresponding scheduled pin: " + name);
    }
    schedule.delay_ps.emplace(pin->second, delay);
    if (delay > 0) {
      schedule.separate.insert(pin->second);
    }
  }
  const auto fixed_steps = schedule.drive_steps;
  const auto plan = [&](const auto& requested) {
    auto unmatched_schedule = requested.drive_steps;
    auto remaining          = limits;
    remaining.balance       = requested.balance;
    buffers.clear();
    // Follow only characterized clock arcs and the recognized clock pin of
    // a discrete gate. Its enable can be driven by an unrelated data path.
    std::unordered_multimap<Pin_label, size_t> downstream_domains;
    if (requested.balance) {
      for (size_t i = 0; i < plans.size(); ++i) {
        const auto& d = plans[i];
        if (d.merged_into || d.source_connection->instance == "PIN") {
          continue;
        }
        const auto& source = *d.source_connection;
        if (const auto arc = gate_clock_arcs.find(d.net_index); arc != gate_clock_arcs.end()) {
          downstream_domains.emplace(Pin_label{source.instance, Name(arc->second)}, i);
        } else if (const auto links = placed.prects.links.find(d.source_master.str()); links != placed.prects.links.end()) {
          for (const auto& [input, output] : links->second) {
            if (source.pin == output) {
              downstream_domains.emplace(Pin_label{source.instance, input}, i);
            }
          }
        } else if (const auto& cell = timing.cells.at(d.source_master.str()); cell.clock_gate) {
          for (const auto& [input, pin] : cell.pins) {
            if (pin.clock && pin.direction == "input") {
              downstream_domains.emplace(Pin_label{source.instance, input}, i);
            }
          }
        }
      }
    }
    std::vector<unsigned> visited(plans.size());
    std::vector<double>   latency(plans.size(), -std::numeric_limits<double>::infinity());
    std::vector<double>   latency_min(plans.size(), std::numeric_limits<double>::infinity());
    const auto            fit_domain = [&](auto&& self, size_t index) -> void {
      auto& d = plans[index];
      if (d.merged_into || visited[index] == 2) {
        return;
      }
      if (visited[index] == 1) {
        throw std::invalid_argument("cycle in clock-gate domains at " + d.net);
      }
      visited[index] = 1;
      const auto propagate = [&](pcrs::Clock_sink& pin) {
        pin.downstream_ps = 0;
        pin.downstream_min_ps.reset();
        const auto [begin, end] = downstream_domains.equal_range(pin.name);
        double earliest         = std::numeric_limits<double>::infinity();
        for (auto it = begin; it != end; ++it) {
          self(self, it->second);
          pin.downstream_ps = it == begin ? latency[it->second] : std::max(pin.downstream_ps, latency[it->second]);
          earliest          = std::min(earliest, latency_min[it->second]);
        }
        if (std::isfinite(earliest) && earliest < pin.downstream_ps) {
          pin.downstream_min_ps = earliest;
        }
      };
      std::ranges::for_each(d.loads, propagate);
      std::ranges::for_each(d.source_model.direct, propagate);
      d.names.clear();
      for (auto& sink : d.loads) {
        const auto delay   = requested.delay_ps.find(sink.graph_pin);
        sink.leaf_delay_ps = delay == requested.delay_ps.end() ? 0 : delay->second;
        sink.separate_leaf = requested.separate.contains(sink.graph_pin);
        sink.drive_steps.reset();
        if (const auto it = unmatched_schedule.find(sink.graph_pin); it != unmatched_schedule.end()) {
          sink.drive_steps = it->second;
          unmatched_schedule.erase(it);
        }
      }
      remaining.minimize_source_latency = d.minimize_source_latency(requested.balance);
      remaining.source_drive_stages     = d.source_connection->instance == "PIN" ? requested.port_source_stages : 0;
      remaining.allow_source_fork       = options.clock_source_fork && !d.source_model.driver.arcs.empty();
      remaining.source_fork_cap_ff.reset();
      if (options.clock_source_fork && !chunk_homes.regions.empty() && d.source_connection->instance == "PIN"
          && !external_drives.contains(d.source_connection->pin.str()) && d.source_model.direct.empty()) {
        const auto                             dbu = remaining.dbu_per_um;
        std::vector<std::pair<double, double>> pins{
                       {d.source_model.x_um * dbu, d.source_model.y_um * dbu}
        };
        double pin_cap = 0;
        for (const auto& sink : d.loads) {
          pins.emplace_back(sink.x_um * dbu, sink.y_um * dbu);
          pin_cap += sink.cap_ff;
        }
        const auto net               = steiner_net(pins, 0, remaining.directional_rc.value(), dbu);
        remaining.source_fork_cap_ff = std::accumulate(net.node_cap_ff.begin(), net.node_cap_ff.end(), pin_cap);
        remaining.allow_source_fork  = true;
      }
      if (d.gate_leaf) {
        auto source          = d.source_model;
        source.direct        = d.loads;
        auto direct_limits   = remaining;
        direct_limits.fanout = 30;
        d.tree               = pcrs::plan_clock_tree({}, drivers, direct_limits, source);
      } else {
        d.tree
            = pcrs::plan_clock_tree(d.loads, drivers, remaining, d.source_model, reuse_trial_plans ? &plan_cache[index] : nullptr);
      }
      remaining.repeater_sites -= d.tree.wire_repeaters + d.tree.balance_buffers + d.tree.schedule_buffers;
      assign_clock_names(d, previous, nodes, buffers);
      const auto record_latency = [&](const auto& pins, const auto& arrivals) {
        for (size_t i = 0; i < pins.size(); ++i) {
          const auto& pin = pins[i];
          latency[index]     = std::max(latency[index], d.tree.source_cell_delay_ps + arrivals.at(i) + pin.tail_hi_ps());
          latency_min[index] = std::min(latency_min[index], d.tree.source_cell_delay_ps + arrivals.at(i) + pin.tail_lo_ps());
        }
      };
      record_latency(d.loads, d.gate_leaf ? d.tree.retained_arrival_ps : d.tree.arrival_ps);
      record_latency(d.source_model.direct, d.tree.retained_arrival_ps);
      visited[index] = 2;
    };
    for (size_t i = 0; i < plans.size(); ++i) {
      fit_domain(fit_domain, i);
    }
    if (!unmatched_schedule.empty()) {
      throw std::invalid_argument("clock leaf schedule names an unknown sink: "
                                  + pin_names.at(unmatched_schedule.begin()->first).str());
    }
  };
  Clock_gate_search        gate_search;
  std::map<size_t, size_t> plan_index;
  struct Original_distributions {
    Clock_distribution parent, keep, drop;
  };
  std::optional<Original_distributions> original;
  const auto                            policy = options.mutation_policy();
  // A path exception through a gate output can reach more flops after a merge
  // even if the SDC still resolves to the same net or pin name.
  std::set<std::string>                 constrained_cells, constrained_nets;
  const auto                            protect = [&](const auto& objects) {
    using Kind = timing::fast::Sdc_object::Kind;
    for (const auto& obj : objects) {
      if (obj.kind == Kind::cell) {
        constrained_cells.insert(obj.name);
      }
      if (obj.kind == Kind::pin) {
        constrained_cells.insert(obj.name.substr(0, obj.name.rfind('/')));
      }
      if (obj.kind == Kind::net) {
        constrained_nets.insert(obj.name);
      }
    }
  };
  for (const auto& clock : sdc.clocks) {
    protect(clock.sources);
  }
  for (const auto& path : sdc.false_paths) {
    protect(path.from);
    protect(path.through);
    protect(path.to);
  }
  for (const auto& path : sdc.max_delays) {
    protect(path.from);
    protect(path.to);
  }
  protect(sdc.disabled);
  if (options.clock_gate_merge_trials && !policy.no_resynthesis && !policy.no_buffer_edits && !sdc.clocks.empty()
      && sdc.unsupported.empty()) {
    for (size_t i = 0; i < plans.size(); ++i) {
      plan_index.emplace(plans[i].net_index, i);
    }
    gate_search.candidates = pcrs::gate_merges(fp, tech.cells, roles.clock_nets);
    gate_search.budget     = options.clock_gate_merge_trials;
  }
  gate_search.prepare = [&](Clock_merge_trial& trial) {
    auto& gate = trial.gate;
    if (!plan_index.contains(gate.clock_net) || !plan_index.contains(gate.keep_net) || !plan_index.contains(gate.drop_net)) {
      return false;
    }
    // Follow contractions only between accepted pairs. The next neighbouring
    // groups may now belong to different physical survivors than at discovery.
    const auto survivor = [&](size_t net) -> const Clock_distribution& {
      size_t index = plan_index.at(net);
      while (plans[index].merged_into) {
        index = *plans[index].merged_into;
      }
      return plans[index];
    };
    const auto& keep = survivor(gate.keep_net);
    const auto& drop = survivor(gate.drop_net);
    if (&keep == &drop || !keep.direct.empty() || !drop.direct.empty()) {
      return false;
    }
    gate.keep_net = keep.net_index;
    gate.drop_net = drop.net_index;
    gate.keep     = keep.source_connection->instance.str();
    gate.drop     = drop.source_connection->instance.str();
    if (constrained_cells.contains(gate.keep) || constrained_cells.contains(gate.drop) || constrained_nets.contains(keep.net)
        || constrained_nets.contains(drop.net)) {
      return false;
    }
    const auto& parent = plans.at(plan_index.at(gate.clock_net));
    const auto  pin    = gate.drop + ":" + gate.clock_pin;
    const auto  load   = std::ranges::find(parent.loads, pin, &pcrs::Clock_sink::name);
    if (load == parent.loads.end() || !load->coupled_group.empty() || options.clock_leaf_schedule.contains(pin)) {
      return false;
    }
    for (const auto* instance : {&gate.keep, &gate.drop}) {
      trial.clock_sinks.push_back(*instance + ":" + gate.clock_pin);
      trial.controls.push_back(*instance + ":" + gate.enable_pin);
      if (!gate.test_pin.empty()) {
        trial.controls.push_back(*instance + ":" + gate.test_pin);
      }
    }
    for (const auto* dist : {&keep, &drop}) {
      for (const auto& sink : dist->loads) {
        trial.clock_sinks.push_back(sink.name);
      }
    }
    return true;
  };
  if ((options.clock_gate_leaf_trials || options.clock_gate_raise_trials) && !options.clock_leaf_drive_steps
      && !policy.no_buffer_edits && !tech.cells.icgs.empty() && !sdc.clocks.empty() && sdc.unsupported.empty()) {
    gate_search.leaf_candidates = [&]() {
      std::map<std::string_view, const liberty::Icg_cell*> gates_by_master;
      std::map<std::string_view, std::string_view>         flop_clock;
      for (const auto& gate : tech.cells.icgs) {
        gates_by_master.emplace(gate.name, &gate);
      }
      for (const auto* family : {&tech.cells.dffs, &tech.cells.async_dffs}) {
        for (const auto& flop : *family) {
          flop_clock.emplace(flop.name, flop.clk_pin);
        }
      }
      std::vector<Gate_leaf_trial> candidates;
      for (size_t i = 0; options.clock_gate_leaf_trials && i < plans.size(); ++i) {
        const auto& d = plans[i];
        if (d.merged_into || !d.source_connection || d.source_connection->instance == "PIN" || !d.direct.empty()
            || std::ranges::any_of(d.loads, [&](const auto& sink) { return sink.chunk != d.source_chunk; }) || d.loads.size() < 6
            || d.loads.size() > 30 || !fp.nets[d.net_index].wires.empty() || fp.snapshot_ndr.net_rules.contains(d.net)) {
          continue;
        }
        const auto gate = gates_by_master.find(instances.at(d.source_connection->instance.str())->macro);
        if (gate == gates_by_master.end() || gate->second->out_pin != d.source_connection->pin) {
          continue;
        }
        const bool eligible = std::ranges::all_of(d.sinks, [&](const auto& sink) {
          if (sink.instance == "PIN" || options.clock_leaf_schedule.contains(physical::connection_label(sink))) {
            return false;
          }
          const auto flop = flop_clock.find(instances.at(sink.instance.str())->macro);
          return flop != flop_clock.end() && flop->second == sink.pin;
        });
        if (!eligible) {
          continue;
        }
        auto& candidate        = candidates.emplace_back();
        candidate.distribution = i;
        candidate.instance     = d.source_connection->instance.str();
        candidate.controls.push_back(candidate.instance + ":" + gate->second->en_pin);
        if (!gate->second->test_pin.empty()) {
          candidate.controls.push_back(candidate.instance + ":" + gate->second->test_pin);
        }
        candidate.clock_sinks.push_back(candidate.instance + ":" + gate->second->clk_pin);
        for (const auto& sink : d.loads) {
          candidate.clock_sinks.push_back(sink.name);
        }
        if (candidates.size() == options.clock_gate_leaf_trials) {
          break;
        }
      }
      // After same-function gates have merged, an upstream net with one remaining gate
      // can start the gated subtree at its source, ahead of any planned distribution buffers.
      unsigned raised = 0;
      for (size_t i = 0; i < plans.size() && raised < options.clock_gate_raise_trials; ++i) {
        const auto& parent = plans[i];
        if (parent.merged_into || !parent.direct.empty() || parent.sinks.size() != 1 || parent.loads.size() != 1
            || !fp.nets[parent.net_index].wires.empty() || fp.snapshot_ndr.net_rules.contains(parent.net)) {
          continue;
        }
        const auto& sink = parent.sinks.front();
        if (sink.instance == "PIN" || fixed_steps.contains(parent.loads.front().graph_pin)
            || constrained_nets.contains(parent.net) || constrained_cells.contains(sink.instance.str())) {
          continue;
        }
        const auto master = gates_by_master.find(instances.at(sink.instance.str())->macro);
        if (master == gates_by_master.end() || master->second->clk_pin != sink.pin) {
          continue;
        }
        const auto child = std::ranges::find_if(plans, [&](const auto& d) {
          return !d.merged_into && d.source_connection && d.source_connection->instance == sink.instance
                 && d.source_connection->pin == master->second->out_pin;
        });
        if (child == plans.end() || child->loads.empty() || !parent.loads.front().coupled_group.empty()) {
          continue;
        }
        auto& trial        = candidates.emplace_back();
        trial.distribution = i;
        trial.instance     = sink.instance.str();
        trial.raised       = true;
        trial.controls.push_back(trial.instance + ":" + master->second->en_pin);
        if (!master->second->test_pin.empty()) {
          trial.controls.push_back(trial.instance + ":" + master->second->test_pin);
        }
        trial.clock_sinks.push_back(parent.loads.front().name);
        for (const auto& load : child->loads) {
          trial.clock_sinks.push_back(load.name);
        }
        ++raised;
      }
      return candidates;
    };
    gate_search.direct_leaf = [&](size_t index, bool enable) { plans.at(index).gate_leaf = enable; };
  }
  // Without any possible merge, all direct-leaf candidates are already
  // known. An empty set needs neither a baseline RC snapshot nor an STA run.
  if (gate_search.candidates.empty() && gate_search.leaf_candidates) {
    gate_search.leaves          = gate_search.leaf_candidates();
    gate_search.leaf_candidates = {};
  }
  gate_search.apply = [&](const pcrs::Gate_merge& gate) {
    auto&      parent = plans.at(plan_index.at(gate.clock_net));
    auto&      keep   = plans.at(plan_index.at(gate.keep_net));
    auto&      drop   = plans.at(plan_index.at(gate.drop_net));
    const auto pos
        = std::ranges::find(parent.loads, gate.drop + ":" + gate.clock_pin, &pcrs::Clock_sink::name) - parent.loads.begin();
    original.emplace(parent, keep, std::move(drop));
    parent.loads.erase(parent.loads.begin() + pos);
    parent.pins.erase(parent.pins.begin() + pos);
    parent.sinks.erase(parent.sinks.begin() + pos);
    const auto& retired = original->drop;
    keep.loads.insert(keep.loads.end(), retired.loads.begin(), retired.loads.end());
    keep.sinks.insert(keep.sinks.end(), retired.sinks.begin(), retired.sinks.end());
    keep.pins.insert(keep.pins.end(), retired.pins.begin(), retired.pins.end());
    drop             = {};
    drop.merged_into = plan_index.at(gate.keep_net);
    gate_search.active.push_back(gate);
  };
  gate_search.undo = [&]() {
    const auto& gate                        = gate_search.active.back();
    plans.at(plan_index.at(gate.clock_net)) = std::move(original->parent);
    plans.at(plan_index.at(gate.keep_net))  = std::move(original->keep);
    plans.at(plan_index.at(gate.drop_net))  = std::move(original->drop);
    original.reset();
    gate_search.active.pop_back();
  };
  std::unordered_set<std::string> gate_sources;
  for (const auto& gate : gates.gates) {
    gate_sources.emplace(fp.nets[gate.output_net].name.str());
  }
  for (const auto& d : plans) {
    if (!d.source_master.empty()) {
      auto model = timing.cells.find(d.source_master.str());
      if (model != timing.cells.end() && model->second.clock_gate) {
        gate_sources.emplace(d.net);
      }
    }
  }
  gate_search.measured_activity = !options.clock_activity.empty();
  gate_search.switched_cap      = [&]() {
    double total = 0;
    for (const auto& d : plans) {
      if (d.merged_into) {
        continue;
      }
      auto   specified = options.clock_activity.find(d.net);
      double activity  = specified != options.clock_activity.end() ? specified->second : (gate_sources.contains(d.net) ? .5 : 1.);
      double cap       = d.tree.source_load_ff;
      for (const auto& b : d.tree.branches) {
        cap += b.load_ff;
      }
      total += activity * cap;
    }
    return total;
  };
  gate_search.clock_area = [&]() {
    double area = 0;
    for (const auto& c : fp.components) {
      auto model = timing.cells.find(c.macro.str());
      if (model == timing.cells.end() || !model->second.clock_gate) {
        continue;
      }
      const auto& m  = tech.lef.macros.at(c.macro);
      area          += m.width * m.height;
    }
    for (const auto& edit : gate_search.active) {
      const auto& master  = tech.lef.macros.at(instances.at(edit.drop)->macro);
      area               -= master.width * master.height;
    }
    for (const auto& dist : plans) {
      for (const auto& branch : dist.tree.branches) {
        const auto& master  = tech.lef.macros.at(drivers.at(branch.driver).master);
        area               += master.width * master.height;
      }
    }
    return area;
  };
  const auto retire_gates = [&](physical::Floorplan& target) {
    if (gate_search.active.empty()) {
      return;
    }
    std::set<Name> cells, nets;
    for (const auto& edit : gate_search.active) {
      cells.emplace(edit.drop);
      nets.insert(target.nets.at(edit.drop_net).name);
    }
    std::erase_if(target.nets, [&](const auto& net) { return nets.contains(net.name); });
    for (auto& net : target.nets) {
      std::erase_if(net.connections, [&](const auto& pin) { return cells.contains(pin.instance); });
    }
    std::erase_if(target.components, [&](const auto& cell) { return cells.contains(cell.name); });
  };
  std::map<std::string, std::vector<pcrs::Clock_driver>> clone_drives;
  if (options.clock_gate_clone_trials && !policy.no_resynthesis && !policy.no_buffer_edits && !tech.cells.icgs.empty()
      && !sdc.clocks.empty() && sdc.unsupported.empty()) {
    gate_search.clone_budget = options.clock_gate_clone_trials;
    gate_search.clone_limits = limits;
    for (const auto& output : tech.cells.drives) {
      if (output.max_fanout && *output.max_fanout > 0) {
        gate_search.input_limits.emplace(std::pair{Name(output.name), Name(output.output)}, *output.max_fanout);
      }
    }
    // An SDC driving cell sits outside the physical netlist. Check its
    // characterized drive on the edited port net, including earlier copies.
    gate_search.external_clock_legal = [&](const physical::Floorplan& layout, const Clock_clone& copy) {
      const auto port = std::ranges::find_if(plans, [&](const auto& d) { return d.source_connection == copy.clock_driver; });
      if (port == plans.end()) {
        return false;
      }
      const auto      input = std::ranges::find_if(copy.inputs, [&](const auto& item) { return item.first == copy.clk; });
      const auto      net   = std::ranges::find(layout.nets, input->second, &physical::Net::name);
      Clock_instances cells;
      for (const auto& c : layout.components) {
        cells.emplace(c.name, &c);
      }
      cells.emplace(copy.cell.name, &copy.cell);
      pcrs::Clock_source source{port->source_model.x_um, port->source_model.y_um, port->source_model.driver};
      const auto         add_load = [&](const physical::Connection& conn) {
        auto label = physical::connection_label(conn);
        if (conn.instance == "PIN") {
          const auto pos = pin_position(label);
          source.direct.push_back({label, pos.first / layout.dbu, pos.second / layout.dbu, 0});
          return;
        }
        const auto& c      = *cells.at(conn.instance.str());
        const auto& macro  = tech.lef.macros.at(c.macro);
        const auto  offset = placement_pin_offset(macro, conn.pin, c.place.orientation, layout.dbu);
        const auto& pin    = load_pin(timing, c, conn);
        source.direct.push_back({label,
                                 (c.place.x + offset.first) / layout.dbu,
                                 (c.place.y + offset.second) / layout.dbu,
                                 max_input_cap(pin),
                                 physical::hard_macro(macro),
                                 pin.max_transition_ps.value_or(0)});
      };
      for (const auto& conn : net->connections) {
        if (conn != copy.clock_driver) {
          add_load(conn);
        }
      }
      add_load({copy.cell.name, copy.clk});
      try {
        (void)pcrs::plan_clock_tree({}, {}, limits, source);
        return true;
      } catch (const pcrs::Clock_infeasible&) {
        return false;
      }
    };
    gate_search.clone_candidates = [&](const Endpoint_hold& setup, const Endpoint_hold& hold) {
      std::map<std::string_view, const liberty::Icg_cell*> gate_cells;
      for (const auto& g : tech.cells.icgs) {
        gate_cells.emplace(g.name, &g);
      }
      std::map<std::string_view, const liberty::Dff_cell*> flops;
      for (const auto* family : {&tech.cells.dffs, &tech.cells.async_dffs}) {
        for (const auto& f : *family) {
          flops.emplace(f.name, &f);
        }
      }
      std::map<std::pair<Name, Name>, physical::Connection> clock_taps;
      for (const auto& parent : plans) {
        if (parent.merged_into || constrained_nets.contains(parent.net)) {
          continue;
        }
        for (size_t i = 0; i < parent.tree.branches.size(); ++i) {
          const auto& branch = parent.tree.branches[i];
          if (!branch.leaf) {
            continue;
          }
          for (auto child : branch.children) {
            const auto& sink = parent.sinks.at(child);
            if (sink.instance == "PIN") {
              continue;
            }
            const auto gate = gate_cells.find(instances.at(sink.instance.str())->macro);
            if (gate != gate_cells.end() && gate->second->clk_pin == sink.pin) {
              clock_taps.emplace(std::pair{sink.instance, sink.pin},
                                 physical::Connection{parent.names[i], drivers[branch.driver].input});
            }
          }
        }
      }
      std::vector<Clock_clone_leaf> choices;
      for (const auto& d : plans) {
        if (d.merged_into || d.gate_leaf || d.tree.branches.size() < 2 || !d.direct.empty() || !d.source_connection
            || std::ranges::any_of(d.loads, [&](const auto& sink) { return sink.chunk != d.source_chunk; })
            || d.source_connection->instance == "PIN" || constrained_nets.contains(d.net)) {
          continue;
        }
        const auto& gate_instance = *instances.at(d.source_connection->instance.str());
        const auto  gi            = gate_cells.find(gate_instance.macro);
        if (gi == gate_cells.end() || gate_instance.place.status != physical::Status::PLACED
            || constrained_cells.contains(gate_instance.name.str()) || gi->second->out_pin != d.source_connection->pin) {
          continue;
        }
        const auto& g = *gi->second;
        if (detail::matches_dont_use(g.name, options)) {
          continue;
        }
        auto [ladder, inserted] = clone_drives.try_emplace(g.name);
        if (inserted && !policy.no_sizing && !g.state_model.empty()) {
          for (const auto& other : tech.cells.icgs) {
            if (other.name == g.name || other.state_model != g.state_model || other.clk_pin != g.clk_pin || other.en_pin != g.en_pin
                || other.out_pin != g.out_pin || other.test_pin != g.test_pin || detail::matches_dont_use(other.name, options)) {
              continue;
            }
            const auto macro = tech.lef.macros.find(other.name);
            if (macro == tech.lef.macros.end() || !macro->second.klass.starts_with("CORE")
                || !macro->second.pins.contains(other.clk_pin) || !macro->second.pins.contains(other.en_pin)
                || !macro->second.pins.contains(other.out_pin)
                || (!other.test_pin.empty() && !macro->second.pins.contains(other.test_pin))) {
              continue;
            }
            const auto& cell  = timing.cells.at(other.name);
            auto        model = source_cell(tech, cell, cell.pins.at(other.out_pin), placed.prects, {}, other.clk_pin);
            if (model.arcs.empty()) {
              continue;
            }
            model.area_um2 = macro->second.width * macro->second.height;
            ladder->second.push_back(std::move(model));
          }
          std::ranges::sort(ladder->second, {}, [](const auto& m) { return std::tie(m.area_um2, m.master); });
        }
        const auto parents = d.branch_parents();
        if (std::ranges::count_if(d.tree.branches, [](const auto& b) { return b.leaf; }) < 2) {
          continue;
        }
        for (size_t i = 0; i < d.names.size(); ++i) {
          const auto& b = d.tree.branches[i];
          if (!b.leaf || b.children.size() < 6 || b.children.size() > 30) {
            continue;
          }
          Clock_clone_leaf leaf{gate_instance.name.str(),
                                g.clk_pin,
                                g.en_pin,
                                g.test_pin,
                                g.out_pin,
                                drivers[b.driver].output,
                                {},
                                {},
                                &d.source_model.driver,
                                std::numeric_limits<double>::infinity(),
                                b.wire_um,
                                ladder->second};
          if (const auto tap = clock_taps.find({gate_instance.name, Name(g.clk_pin)}); tap != clock_taps.end()) {
            leaf.upstream_tap = tap->second;
          }
          for (auto child : b.children) {
            const auto& pin = d.sinks.at(child);
            if (pin.instance == "PIN" || fixed_steps.contains(d.loads.at(child).graph_pin)) {
              break;
            }
            const auto f = flops.find(instances.at(pin.instance.str())->macro);
            if (f == flops.end() || f->second->clk_pin != pin.pin) {
              break;
            }
            leaf.sinks.push_back(d.loads.at(child));
            const auto label = pin.instance.str() + ":" + f->second->d_pin;
            for (const auto* slacks : {&hold, &setup}) {
              if (const auto e = slacks->find(label); e != slacks->end()) {
                leaf.priority_ps = std::min<double>(leaf.priority_ps, e->second);
              }
            }
          }
          if (leaf.sinks.size() != b.children.size()) {
            continue;
          }
          for (size_t n = i; n < d.names.size(); n = parents[n]) {
            leaf.path.push_back({d.names[n], drivers[d.tree.branches[n].driver].input});
          }
          choices.push_back(std::move(leaf));
        }
      }
      std::ranges::sort(choices, {}, [](const auto& c) {
        return std::tuple{c.priority_ps, -c.wire_um, c.gate, c.path.front().instance};
      });
      return choices;
    };
  }
  std::optional<Clock_schedule_result> useful_skew;
  if (((timer && options.clock_balance) || options.clock_skew_rounds || options.clock_hold_area_um2 > 0
       || !gate_search.candidates.empty() || !gate_search.leaves.empty() || gate_search.leaf_candidates
       || gate_search.clone_candidates)
      && !plans.empty()) {
    if (!timer) {
      throw std::invalid_argument("PCRS clock optimization requires a timing session");
    }
    const auto build = [&](const Clock_schedule& requested) {
      plan(requested);
      Clock_trial_layout trial{fp, {}};
      for (const auto& [index, pose] : phases.poses) {
        trial.floorplan.components.at(index).place = pose;
      }
      place_clock_branches(trial.floorplan, tech.lef, plans, drivers);
      retire_gates(trial.floorplan);
      for (const auto& copy : gate_search.copies) {
        apply_clock_clone(trial.floorplan, copy);
      }
      for (const auto& d : plans) {
        if (d.merged_into) {
          continue;
        }
        const auto parents = options.clock_skew_rounds ? d.branch_parents() : std::vector<size_t>{};
        trial.port_source_pair |= d.tree.source_pair_available && d.source_connection->instance == "PIN";
        for (size_t i = 0; i < d.tree.branches.size(); ++i) {
          const auto& branch = d.tree.branches[i];
          trial.branch_rc.emplace(d.names.at(i) + ":" + drivers[branch.driver].output, branch.leaf ? leaf_rc : trunk_rc);
          if (!branch.leaf) {
            continue;
          }
          const Name parent = !parents.empty() && parents[i] < d.names.size() ? Name(d.names[parents[i]]) : Name{};
          const auto anchor = d.loads.at(branch.children.front()).graph_pin;
          const bool fixed  = std::ranges::any_of(branch.children, [&](auto child) {
            return fixed_steps.contains(d.loads.at(child).graph_pin);
          });
          for (auto child : branch.children) {
            trial.sinks.emplace(d.loads.at(child).name,
                                Schedule_leaf{anchor, branch.drive_steps, fixed, d.loads.at(child).graph_pin, 0, parent});
          }
        }
      }
      return trial;
    };
    const auto resolve_sdc = [&](Clock_trial_layout& trial) {
      if (internal_constraints
          && (options.clock_gate_merge_trials || options.clock_gate_leaf_trials || options.clock_gate_clone_trials
              || options.clock_gate_raise_trials || options.clock_hold_area_um2 > 0)) {
        trial.internal_sdc.clear();
        Clock_instances trial_instances;
        for (const auto& cell : trial.floorplan.components) {
          trial_instances.emplace(cell.name, &cell);
        }
        const auto resolved = timing::fast::read_sdc(sdc_text, units, Clock_sdc_design(trial.floorplan, tech.lef, trial_instances));
        trial.sdc_supported = resolved.unsupported.empty();
        for (const auto& command : resolved.commands) {
          trial.internal_sdc.push_back(command.text);
        }
      }
    };
    Hold_repair_options hold_repair{policy.no_buffer_edits ? 0 : options.clock_hold_area_um2, timing, {}};
    if (hold_repair.area_um2 > 0) {
      hold_repair.pin_access    = placed.hold_pin_access ? placed.hold_pin_access.get() : options.satlegalize_options.routing.get();
      hold_repair.padding_sites = options.terminal_legalize_options.padding_sites;
      if (!fp.rows.empty()) {
        const auto& site                                         = tech.lef.sites.at(fp.rows.front().site);
        std::tie(hold_repair.halo_x_dbu, hold_repair.halo_y_dbu) = placement_cell_halo(options, site, fp.dbu);
      }
      for (const auto& driver : drivers) {
        if (!detail::matches_dont_use(driver.master, options)) {
          hold_repair.drivers.push_back(&driver);
        }
      }
    }
    useful_skew = choose_clock_schedule(schedule,
                                        options.clock_skew_rounds,
                                        options.clock_skew_period_fraction,
                                        hold_margin_ps,
                                        hold_repair,
                                        tech.lef,
                                        wires,
                                        std::move(timer),
                                        build,
                                        resolve_sdc,
                                        &gate_search,
                                        options.clock_incremental_trials);
    if (options.clock_hold_area_um2 > 0 && options.mutation_policy().no_buffer_edits) {
      useful_skew->delay_stop   = "buffer_edits_disabled";
      useful_skew->after_delays = useful_skew->selected;
    }
    schedule = useful_skew->schedule;
  }
  plan(schedule);
  for (const auto& [index, pose] : phases.poses) {
    auto& c = fp.components.at(index);
    c.place = pose;
    nodes.at(c.name.str()).attr(attrs::place).set(pose);
  }
  for (const auto& [label, pos] : phases.pins) {
    placed.pin_locations[label] = pos;
  }
  place_clock_branches(fp, tech.lef, plans, drivers);
  const auto path = std::filesystem::path(options.output_dir) / "pcrs_clock.json";
  Pending_clock_report pending(path);
  std::ofstream        file(pending.path);
  file.exceptions(std::ios::badbit | std::ios::failbit);
  rapidjson::OStreamWrapper stream(file);
  rapidjson::Writer         writer(stream);
  writer.StartObject();
  writer.Key("mode");
  writer.String("tree");
  if (!chunk_homes.regions.empty()) {
    writer.Key("chunk_plan_revision");
    writer.String(std::format("{:016x}", hash_util::fnv1a64(*accepted_plan)).c_str());
    writer.Key("floorplan_chunks");
    writer.StartArray();
    for (const auto& [id, boxes] : chunk_homes.regions) {
      writer.String(id.c_str());
    }
    writer.EndArray();
  }
  auto source_entry = [&](const std::string& chunk, const std::string& instance, const std::string& pin) {
    writer.Key("chunk_entries");
    writer.StartArray();
    writer.StartObject();
    writer.Key("chunk");
    writer.String(chunk.c_str());
    writer.Key("instance");
    writer.String(instance.c_str());
    writer.Key("output_pin");
    writer.String(pin.c_str());
    writer.Key("estimated_from_source_ps");
    writer.Double(0);
    writer.EndObject();
    writer.EndArray();
  };
  writer.Key("skew_search_status");
  writer.String(useful_skew ? useful_skew->skew_stop.c_str()
                            : (options.clock_skew_rounds ? "no_clock_domains" : "no_rounds_requested"));
  writer.Key("requested_leaf_drive_steps");
  writer.Uint(options.clock_leaf_drive_steps);
  const auto violation = [&](const char* key, const std::optional<Clock_bound_violation>& item) {
    if (!item) {
      return;
    }
    writer.Key(key);
    writer.StartObject();
    writer.Key("pin");
    writer.String(item->pin.c_str());
    writer.Key("reference_ps");
    writer.Double(item->reference_ps);
    writer.Key("trial_ps");
    writer.Double(item->trial_ps);
    writer.Key("limit_ps");
    writer.Double(item->limit_ps);
    writer.Key("excess_ps");
    writer.Double(item->excess_ps);
    writer.EndObject();
  };
  if (useful_skew) {
    writer.Key("useful_skew");
    writer.StartObject();
    size_t groups_reused = 0, stages_reused = 0, stages_fitted = 0;
    for (const auto& cache : plan_cache) {
      groups_reused += cache.grouping_hits;
      stages_reused += cache.reused_stages;
      stages_fitted += cache.fitted_stages;
    }
    writer.Key("grouping_reuses");
    writer.Uint64(groups_reused);
    writer.Key("trunk_fit_reuses");
    writer.Uint64(stages_reused);
    writer.Key("trunk_fits");
    writer.Uint64(stages_fitted);
    if (!useful_skew->unsupported_checks.empty()) {
      writer.Key("unsupported_checks");
      writer.StartArray();
      for (const auto& reason : useful_skew->unsupported_checks) {
        writer.String(reason.c_str());
      }
      writer.EndArray();
    }
    if (!useful_skew->balance_decision.empty()) {
      writer.Key("balance_decision");
      writer.String(useful_skew->balance_decision.c_str());
      writer.Key("unbalanced_setup_ps");
      if (std::isfinite(useful_skew->unbalanced_score->setup)) {
        writer.Double(useful_skew->unbalanced_score->setup);
      } else {
        writer.Null();
      }
      if (useful_skew->balanced_score) {
        writer.Key("balanced_setup_ps");
        if (std::isfinite(useful_skew->balanced_score->setup)) {
          writer.Double(useful_skew->balanced_score->setup);
        } else {
          writer.Null();
        }
      }
    }
    writer.Key("journal_trials");
    writer.Uint(useful_skew->journal_trials);
    writer.Key("full_evaluations");
    writer.Uint(useful_skew->full_evaluations);
    writer.Key("timed_plans");
    writer.Uint(useful_skew->timed);
    writer.Key("infeasible_plans");
    writer.Uint(useful_skew->infeasible);
    writer.Key("accepted_rounds");
    writer.Uint(useful_skew->accepted);
    writer.Key("max_offset_period_fraction");
    writer.Double(options.clock_skew_period_fraction);
    writer.Key("hold_margin_ps");
    writer.Double(hold_margin_ps);
    writer.Key("selected_max_shift_ps");
    writer.Double(useful_skew->selected_max_shift_ps);
    writer.Key("offset_rejections");
    writer.Uint(useful_skew->offset_rejections);
    writer.Key("hold_margin_rejections");
    writer.Uint(useful_skew->hold_rejections);
    violation("worst_offset_rejection", useful_skew->worst_offset);
    violation("worst_hold_margin_rejection", useful_skew->worst_hold);
    const auto score = [&](const char* key, const std::optional<Clock_score>& value) {
      writer.Key(key);
      if (!value) {
        writer.Null();
        return;
      }
      writer.StartObject();
      writer.Key("worst_setup_ps");
      writer.Double(value->setup);
      writer.Key("worst_hold_ps");
      writer.Double(value->hold);
      writer.Key("setup_tns_ps");
      writer.Double(value->setup_tns);
      writer.Key("hold_tns_ps");
      writer.Double(value->hold_tns);
      writer.EndObject();
    };
    score("baseline", useful_skew->baseline);
    score("selected", useful_skew->selected);
    if (options.clock_hold_area_um2 > 0) {
      writer.Key("hold_repair");
      writer.StartObject();
      writer.Key("budget_um2");
      writer.Double(options.clock_hold_area_um2);
      writer.Key("added_area_um2");
      writer.Double(useful_skew->added_area_um2);
      writer.Key("added_hpwl_um");
      writer.Double(std::accumulate(useful_skew->delays.begin(), useful_skew->delays.end(), 0., [](double n, const Hold_delay& e) {
        return n + e.hpwl_delta_um;
      }));
      writer.Key("timed_trials");
      writer.Uint(useful_skew->delay_trials);
      writer.Key("batch_trials");
      writer.Uint(useful_skew->delay_batch_trials);
      writer.Key("accepted_batches");
      writer.Uint(useful_skew->delay_batches);
      writer.Key("stop");
      writer.String(useful_skew->delay_stop.c_str());
      score("selected", useful_skew->after_delays);
      writer.Key("buffers");
      writer.StartArray();
      for (const auto& edit : useful_skew->delays) {
        writer.StartObject();
        writer.Key("instance");
        writer.String(edit.cell.name.c_str());
        writer.Key("master");
        writer.String(edit.cell.macro.c_str());
        writer.Key("source_net");
        writer.String(edit.source_net.c_str());
        writer.Key("output_net");
        writer.String(edit.new_net.c_str());
        writer.Key("x_dbu");
        writer.Int64(edit.cell.place.x);
        writer.Key("y_dbu");
        writer.Int64(edit.cell.place.y);
        writer.Key("sinks");
        writer.StartArray();
        for (const auto& sink : edit.sinks) {
          writer.String(physical::connection_label(sink).c_str());
        }
        writer.EndArray();
        writer.EndObject();
      }
      writer.EndArray();
      writer.EndObject();
    }
    violation("balance_limiting_pin", useful_skew->balance_bound);
    if (!useful_skew->source_drive_trials.empty()) {
      writer.Key("source_drive_trials");
      writer.StartArray();
      for (const auto& trial : useful_skew->source_drive_trials) {
        writer.StartObject();
        writer.Key("stages");
        writer.Uint(trial.stages);
        writer.Key("decision");
        writer.String(trial.decision.c_str());
        score("timing", trial.score);
        violation("limiting_pin", trial.bound);
        writer.EndObject();
      }
      writer.EndArray();
    }
    writer.Key("trials");
    writer.StartArray();
    for (const auto& event : useful_skew->trials) {
      writer.StartObject();
      writer.Key("round");
      writer.Uint(event.round);
      writer.Key("target");
      writer.String(event.targets_hold ? "hold" : "setup");
      writer.Key("launch_pin");
      writer.String(event.launch_pin.c_str());
      writer.Key("endpoint_pin");
      writer.String(event.endpoint_pin.c_str());
      if (!event.compensated_endpoint.empty()) {
        writer.Key("compensated_setup_endpoint");
        writer.String(event.compensated_endpoint.c_str());
      }
      writer.Key("changes");
      writer.StartObject();
      for (const auto& [pin, steps] : event.changes.drive_steps) {
        writer.Key(pin_names.at(pin).str().c_str());
        writer.Uint(steps);
      }
      writer.EndObject();
      writer.Key("joint_leaf_schedule");
      writer.Bool(event.joint);
      writer.Key("delay_ps");
      writer.StartObject();
      for (const auto& [pin, delay] : event.changes.delay_ps) {
        writer.Key(pin_names.at(pin).str().c_str());
        writer.Double(delay);
      }
      writer.EndObject();
      writer.Key("separate_leaf_pins");
      writer.StartArray();
      for (const auto& pin : event.changes.separate) {
        writer.String(pin_names.at(pin).str().c_str());
      }
      writer.EndArray();
      writer.Key("decision");
      writer.String(event.decision.c_str());
      violation("limiting_pin", event.bound);
      score("timing", event.score);
      writer.EndObject();
    }
    writer.EndArray();
    writer.EndObject();
  }
  writer.Key("leaf_schedule");
  writer.StartObject();
  for (const auto& [pin, steps] : schedule.drive_steps) {
    writer.Key(pin_names.at(pin).str().c_str());
    writer.Uint(steps);
  }
  writer.EndObject();
  writer.Key("leaf_scheduled_delay_ps");
  writer.StartObject();
  for (const auto& [pin, delay] : schedule.delay_ps) {
    writer.Key(pin_names.at(pin).str().c_str());
    writer.Double(delay);
  }
  writer.EndObject();
  writer.Key("separate_leaf_pins");
  writer.StartArray();
  for (const auto& pin : schedule.separate) {
    writer.String(pin_names.at(pin).str().c_str());
  }
  writer.EndArray();
  writer.Key("relocated_phase_cells");
  writer.StartArray();
  for (const auto& [index, pose] : phases.poses) {
    writer.String(fp.components.at(index).name.c_str());
  }
  writer.EndArray();
  writer.Key("discrete_clock_gates");
  writer.StartArray();
  for (const auto& gate : gates.gates) {
    writer.StartObject();
    writer.Key("instance");
    writer.String(gate.instance.c_str());
    writer.Key("clock_pin");
    writer.String(gate.clock_pin.c_str());
    writer.Key("control_pin");
    writer.String(gate.control_pin.c_str());
    writer.Key(gate.edge_triggered_enable ? "enable_flop" : "latch");
    writer.String(gate.latch.c_str());
    writer.Key(gate.edge_triggered_enable ? "enable_flop_clock_pin" : "latch_clock_pin");
    writer.String(gate.latch_clock.c_str());
    writer.Key(gate.edge_triggered_enable ? "enable_flop_data_pin" : "latch_data_pin");
    writer.String(gate.latch_data.c_str());
    writer.Key("fork_net");
    writer.String(fp.nets[gate.upstream_net].name.c_str());
    writer.Key("output_net");
    writer.String(fp.nets[gate.output_net].name.c_str());
    writer.Key("fork_pins");
    writer.StartArray();
    writer.String(gate.clock_fork_pin.c_str());
    writer.String(gate.latch_fork_pin.c_str());
    writer.EndArray();
    writer.EndObject();
  }
  writer.EndArray();
  const auto write_gate_baseline = [&](const char* key, const std::optional<Clock_score>& score) {
    if (!score || !std::isfinite(score->setup) || !std::isfinite(score->hold)) {
      return;
    }
    writer.Key(key);
    writer.StartObject();
    writer.Key("setup_ps");
    writer.Double(score->setup);
    writer.Key("hold_ps");
    writer.Double(score->hold);
    writer.EndObject();
  };
  write_gate_baseline("before_gate_merge", gate_search.baseline);
  write_gate_baseline("before_gate_leaf", gate_search.before_leaves);
  write_gate_baseline("before_gate_clone", gate_search.before_clones);
  const auto write_gate_trial = [&](const Clock_gate_trial& trial) {
    writer.Key("decision");
    writer.String(trial.decision.c_str());
    writer.Key("saved_area_um2");
    writer.Double(trial.saved_area_um2);
    writer.Key("saved_activity_weighted_cap_ff");
    writer.Double(trial.saved_switched_ff);
    writer.Key("minimum_gating_slack_ps");
    writer.Double(trial.gating_slack_ps);
    writer.Key("normalized_cost_gain");
    writer.Double(trial.cost_gain);
    if (trial.score && std::isfinite(trial.score->setup) && std::isfinite(trial.score->hold)) {
      writer.Key("setup_ps");
      writer.Double(trial.score->setup);
      writer.Key("hold_ps");
      writer.Double(trial.score->hold);
    }
  };
  writer.Key("gate_merge_trials");
  writer.StartArray();
  for (const auto& trial : gate_search.trials) {
    writer.StartObject();
    writer.Key("keep");
    writer.String(trial.gate.keep.c_str());
    writer.Key("drop");
    writer.String(trial.gate.drop.c_str());
    write_gate_trial(trial);
    writer.EndObject();
  }
  writer.EndArray();
  writer.Key("gate_leaf_trials");
  writer.StartArray();
  for (const auto& trial : gate_search.leaves) {
    writer.StartObject();
    writer.Key("gate");
    writer.String(trial.instance.c_str());
    writer.Key("operation");
    writer.String(trial.raised ? "raise_gate" : "gate_as_leaf");
    write_gate_trial(trial);
    writer.EndObject();
  }
  writer.EndArray();
  writer.Key("gate_clone_trials");
  writer.StartArray();
  for (const auto& trial : gate_search.clone_trials) {
    writer.StartObject();
    writer.Key("gate");
    writer.String(trial.gate.c_str());
    writer.Key("leaf");
    writer.String(trial.leaf.c_str());
    writer.Key("sinks");
    writer.Uint(trial.loads);
    if (trial.upstream_tap) {
      writer.Key("clock_tap");
      writer.String(physical::connection_label(*trial.upstream_tap).c_str());
    }
    writer.Key("decision");
    writer.String(trial.decision.c_str());
    writer.Key("area_delta_um2");
    writer.Double(trial.area_delta_um2);
    if (trial.cell) {
      writer.Key("instance");
      writer.String(trial.cell->name.c_str());
      writer.Key("master");
      writer.String(trial.cell->macro.c_str());
      writer.Key("x_dbu");
      writer.Int64(trial.cell->place.x);
      writer.Key("y_dbu");
      writer.Int64(trial.cell->place.y);
      writer.Key("orientation");
      writer.String(physical::orientation_name(trial.cell->place.orientation).c_str());
      violation("limiting_pin", trial.bound);
    }
    if (trial.score && std::isfinite(trial.score->setup) && std::isfinite(trial.score->hold)) {
      writer.Key("setup_ps");
      writer.Double(trial.score->setup);
      writer.Key("hold_ps");
      writer.Double(trial.score->hold);
    }
    writer.EndObject();
  }
  writer.EndArray();
  retire_gates(fp);
  std::set<std::string>                           retired_buffers;
  std::map<std::string, size_t>                   moved_sinks;
  std::map<std::string, std::vector<std::string>> added_sinks;
  for (const auto& copy : gate_search.copies) {
    retired_buffers.insert(copy.removed_buffers.begin(), copy.removed_buffers.end());
    moved_sinks[copy.gate] += copy.sinks.size();
    added_sinks[physical::connection_label(copy.clock_driver)].push_back(physical::connection_label({copy.cell.name, copy.clk}));
  }
  writer.Key("networks");
  writer.StartArray();
  auto*                                                 library = graph.get_io()->get_library();
  std::map<std::string, std::shared_ptr<hhds::GraphIO>> ios;
  const auto                                            buffer_node = [&](const std::string& name,
                               const std::string& master,
                               const std::string& input,
                               const std::string& output,
                               const Ann_place&   pose) {
    auto& io = ios[master];
    if (!io) {
      io = library->find_io(master);
      if (!io) {
        io = library->create_io(master);
        io->add_input(input, 0);
        io->set_bits(input, 1);
        io->add_output(output, 0);
        io->set_bits(output, 1);
      }
    }
    auto node = gu::create_typed_node(graph, Ntype_op::Sub);
    node.set_subnode(io);
    node.set_name(name);
    nodes.emplace(name, node);
    node.attr(attrs::place).set(pose);
    for (const auto& pin : {input, output}) {
      const auto [x, y] = placement_pin_offset(tech.lef.macros.at(master), pin, pose.orientation, fp.dbu);
      placed.pin_locations[physical::connection_label(name, pin)] = {pose.x + x, pose.y + y};
    }
    return node;
  };
  for (const auto& d : plans) {
    if (d.merged_into) {
      continue;
    }
    writer.StartObject();
    writer.Key("source_net");
    writer.String(d.net.c_str());
    writer.Key("source_instance");
    writer.String(d.source_connection->instance.c_str());
    writer.Key("source_pin");
    writer.String(d.source_connection->pin.c_str());
    driver_pins.emplace(d.source_connection->instance, d.source_connection->pin);
    if (!d.source_master.empty()) {
      writer.Key("source_master");
      writer.String(d.source_master.c_str());
    }
    writer.Key("sinks");
    const auto& added_at_source = added_sinks[physical::connection_label(*d.source_connection)];
    size_t      added           = added_at_source.size();
    for (size_t i = 0; i < d.names.size(); ++i) {
      const auto& driver = drivers.at(d.tree.branches[i].driver);
      const auto  it     = added_sinks.find(physical::connection_label(d.names[i], driver.output));
      if (it != added_sinks.end()) {
        added += it->second.size();
      }
    }
    const auto moved = moved_sinks[d.source_connection->instance.str()];
    writer.Uint64(d.loads.size() + d.direct.size() + added - moved);
    if (!d.source_chunk.empty()) {
      writer.Key("source_chunk");
      writer.String(d.source_chunk.c_str());
    }
    if (!d.tree.chunk_entries.empty()) {
      writer.Key("chunk_entries");
      writer.StartArray();
      for (const auto& [home, entry] : d.tree.chunk_entries) {
        const auto& name = d.names.at(entry);
        if (retired_buffers.contains(name)) {
          continue;
        }
        const auto& branch = d.tree.branches.at(entry);
        writer.StartObject();
        writer.Key("chunk");
        writer.String(home.c_str());
        writer.Key("instance");
        writer.String(name.c_str());
        writer.Key("input_pin");
        writer.String(drivers[branch.driver].input.c_str());
        writer.Key("estimated_from_source_ps");
        writer.Double(branch.input_arrival_ps);
        writer.EndObject();
      }
      writer.EndArray();
    } else if (!d.loads.empty() && !d.source_chunk.empty()
               && std::ranges::all_of(d.loads, [&](const auto& sink) { return sink.chunk == d.source_chunk; })) {
      source_entry(d.source_chunk, d.source_connection->instance.str(), d.source_connection->pin.str());
    }
    if (added || moved) {
      writer.Key("estimates_before_gate_cloning");
      writer.Bool(true);
    }
    writer.Key("retained_sinks");
    writer.StartArray();
    for (const auto& pin : d.direct) {
      writer.String(physical::connection_label(pin).c_str());
    }
    for (const auto& child : added_at_source) {
      writer.String(child.c_str());
    }
    writer.EndArray();
    if (added == 0 && moved == 0 && !d.tree.retained_arrival_ps.empty()) {
      // A phase-sensitive pin or direct gate leaf has no buffer branch in the
      // report. The planner still computes its wire delay from the source pin.
      writer.Key("estimated_direct_sink_arrivals_ps");
      writer.StartObject();
      for (size_t i = 0; i < d.tree.retained_arrival_ps.size(); ++i) {
        const auto label = d.gate_leaf ? d.loads.at(i).name.str() : physical::connection_label(d.direct.at(i));
        writer.Key(label.c_str());
        writer.Double(d.tree.retained_arrival_ps[i]);
      }
      writer.EndObject();
    }
    writer.Key("wire_repeaters");
    writer.Uint64(d.tree.wire_repeaters);
    writer.Key("source_wire_um");
    writer.Double(d.tree.source_wire_um);
    writer.Key("source_load_ff");
    writer.Double(d.tree.source_load_ff);
    writer.Key("source_far_slew_ps");
    writer.Double(d.tree.source_far_slew_ps);
    writer.Key("minimize_source_latency");
    writer.Bool(d.minimize_source_latency(schedule.balance));
    writer.Key("source_repeaters");
    writer.Uint64(d.tree.source_repeaters);
    if (d.tree.branches.empty()) {
      if (d.gate_leaf) {
        writer.Key("gate_leaf_sinks");
        writer.StartArray();
        for (size_t i = 0; i < d.pins.size(); ++i) {
          auto pin = d.pins[i];
          if (pin.get_driver_pin() != d.source) {
            gu::drop_drivers(pin);
            pin.connect_driver(d.source);
          }
          writer.String(d.loads[i].name.str().c_str());
        }
        writer.EndArray();
      }
      writer.Key("branches");
      writer.StartArray();
      writer.EndArray();
      writer.Key("root_buffer");
      writer.Null();
      writer.EndObject();
      continue;
    }
    const auto [earliest, latest] = std::ranges::minmax(d.tree.arrival_ps);
    writer.Key("estimated_insertion");
    writer.StartObject();
    writer.Key("min_ps");
    writer.Double(earliest);
    writer.Key("max_ps");
    writer.Double(latest);
    writer.Key("global_skew_ps");
    writer.Double(latest - earliest);
    writer.Key("max_within_leaf_skew_ps");
    writer.Double(d.tree.max_leaf_skew_ps);
    writer.EndObject();
    writer.Key("used_previous_leaf_regions");
    writer.Bool(d.tree.used_previous_leaf_regions);
    writer.Key("target_skew_ps");
    writer.Double(d.tree.target_skew_ps);
    writer.Key("scheduled_buffers");
    writer.Uint64(d.tree.schedule_buffers);
    writer.Key("balance_buffers");
    writer.Uint64(d.tree.balance_buffers);
    writer.Key("border_moves");
    writer.Uint64(d.tree.border_moves);
    const auto write_leaves = [&](const char* key, const pcrs::Clock_tree::Leaf_metrics& metrics) {
      writer.Key(key);
      writer.StartObject();
      writer.Key("min_stage_delay_ps");
      writer.Double(metrics.min_delay_ps);
      writer.Key("max_stage_delay_ps");
      writer.Double(metrics.max_delay_ps);
      writer.Key("stage_delay_stddev_ps");
      writer.Double(metrics.delay_stddev_ps);
      writer.Key("star_wire_um");
      writer.Double(metrics.star_wire_um);
      writer.Key("mean_member_distance_um");
      writer.Double(metrics.members ? metrics.star_wire_um / metrics.members : 0.);
      writer.Key("max_member_distance_um");
      writer.Double(metrics.max_member_distance_um);
      writer.Key("steiner_wire_um");
      writer.Double(metrics.wire_um);
      writer.Key("leaf_size_histogram");
      writer.StartObject();
      for (const auto& [size, count] : metrics.size_histogram) {
        writer.Key(std::to_string(size).c_str());
        writer.Uint64(count);
      }
      writer.EndObject();
      writer.Key("driver_histogram");
      writer.StartObject();
      for (const auto& [master, count] : metrics.driver_histogram) {
        writer.Key(master.c_str());
        writer.Uint64(count);
      }
      writer.EndObject();
      writer.EndObject();
    };
    write_leaves("initial_flop_leaves", d.tree.initial_leaves);
    write_leaves("refined_flop_leaves", d.tree.refined_leaves);
    writer.Key("branches");
    writer.StartArray();
    const auto&      names = d.names;
    std::vector<Pin> inputs;
    for (const auto& branch : d.tree.branches) {
      const auto& name   = d.names.at(inputs.size());
      const auto& driver = drivers.at(branch.driver);
      auto        node   = buffer_node(name,
                              driver.master,
                              driver.input,
                              driver.output,
                              clock_pose(branch, tech.lef.macros.at(driver.master), fp.dbu));
      node.attr(attrs::pcrs_clock_buffer).set({});
      inputs.push_back(node.create_sink_pin(driver.input));
      auto output = node.create_driver_pin(driver.output);
      gu::set_bits(output, 1);
      for (auto child : branch.children) {
        auto sink = branch.leaf ? d.pins.at(child) : inputs.at(child);
        if (branch.leaf) {
          gu::drop_drivers(sink);
        }
        sink.connect_driver(output);
      }
      if (retired_buffers.contains(name)) {
        continue;
      }
      driver_pins.emplace(name, driver.output);
      const auto new_pins = added_sinks.find(physical::connection_label(name, driver.output));
      writer.StartObject();
      writer.Key("instance");
      writer.String(name.c_str());
      writer.Key("master");
      writer.String(driver.master.c_str());
      writer.Key("leaf");
      writer.Bool(branch.leaf);
      if (branch.leaf) {
        const auto& home = d.loads.at(branch.children.front()).chunk;
        if (!home.empty()) {
          writer.Key("chunk");
          writer.String(home.c_str());
        }
        writer.Key("drive_steps");
        writer.Uint(branch.drive_steps);
      }
      if (branch.leaf && new_pins == added_sinks.end()) {
        double star_um = 0;
        writer.Key("estimated_sink_arrivals_ps");
        writer.StartArray();
        for (auto child : branch.children) {
          writer.Double(d.tree.arrival_ps.at(child));
          star_um += branch.member_distance_um(d.loads.at(child));
        }
        writer.EndArray();
        writer.Key("star_wire_um");
        writer.Double(star_um);
      }
      writer.Key("load_ff");
      writer.Double(branch.load_ff);
      writer.Key("radius_um");
      writer.Double(branch.radius_um);
      writer.Key("estimated_stage_delay_ps");
      writer.Double(branch.delay_ps);
      writer.Key("estimated_far_slew_ps");
      writer.Double(branch.far_slew_ps);
      writer.Key("reference_input_slew_ps");
      writer.Double(driver.reference_slew_ps);
      writer.Key("driver_model");
      writer.String(driver.arcs.empty() ? "linear_rc" : "liberty_nldm");
      writer.Key("children");
      writer.StartArray();
      for (auto child : branch.children) {
        if (branch.leaf || !retired_buffers.contains(names.at(child))) {
          if (branch.leaf) {
            writer.String(d.loads.at(child).name.str().c_str());
          } else {
            writer.String(names.at(child).c_str());
          }
        }
      }
      if (!branch.leaf && new_pins != added_sinks.end()) {
        writer.EndArray();
        writer.Key("direct_sinks");
        writer.StartArray();
      }
      if (new_pins != added_sinks.end()) {
        for (const auto& child : new_pins->second) {
          writer.String(child.c_str());
        }
      }
      writer.EndArray();
      writer.EndObject();
    }
    writer.EndArray();
    std::vector<size_t> surviving_roots;
    for (auto root : d.tree.roots) {
      inputs.at(root).connect_driver(d.source);
      if (!retired_buffers.contains(names.at(root))) {
        surviving_roots.push_back(root);
      }
    }
    if (surviving_roots.size() == 1) {
      writer.Key("root_buffer");
      writer.String(names.at(surviving_roots.front()).c_str());
    } else {
      writer.Key("root_buffers");
      writer.StartArray();
      for (auto root : surviving_roots) {
        writer.String(names.at(root).c_str());
      }
      writer.EndArray();
    }
    writer.EndObject();
  }
  if (!gate_search.active.empty()) {
    std::set<Name> retired;
    for (const auto& edit : gate_search.active) {
      nodes.at(edit.drop).del_node();
      retired.emplace(edit.drop);
    }
    std::erase_if(placed.pin_locations, [&](const auto& entry) { return retired.contains(entry.first.instance()); });
  }
  for (const auto& copy : gate_search.copies) {
    auto source_gate = nodes.at(copy.gate);
    auto node        = gu::create_typed_node(graph, Ntype_op::Sub);
    if (source_gate.get_subnode_io()->get_name() == copy.cell.macro.view()) {
      node.set_subnode(source_gate.get_subnode_io());
    } else {
      const auto master = std::ranges::find(tech.cells.icgs, copy.cell.macro.str(), &liberty::Icg_cell::name);
      if (master == tech.cells.icgs.end()) {
        throw std::logic_error("lost characterized PCRS gate master");
      }
      node.set_subnode(liberty::create_icg_io(*library, *master));
    }
    node.set_name(copy.cell.name.str());
    node.attr(attrs::place).set(copy.cell.place);
    nodes.emplace(copy.cell.name.str(), node);
    for (const auto& input : copy.inputs) {
      const auto source
          = input.first == copy.clk && copy.upstream_tap
                ? nodes.at(copy.upstream_tap->instance.str()).get_sink_pin(copy.upstream_tap->pin.str()).get_driver_pin()
                : source_gate.get_sink_pin(input.first).get_driver_pin();
      node.create_sink_pin(input.first).connect_driver(source);
    }
    auto driver = node.create_driver_pin(copy.output);
    gu::set_bits(driver, 1);
    for (const auto& conn : copy.sinks) {
      auto pin = nodes.at(conn.instance.str()).get_sink_pin(conn.pin);
      gu::drop_drivers(pin);
      pin.connect_driver(driver);
    }
    for (const auto& name : copy.removed_buffers) {
      nodes.at(name).del_node();
      nodes.erase(name);
      buffers.erase(name);
    }
    apply_clock_clone(fp, copy);
    const auto& master = tech.lef.macros.at(copy.cell.macro);
    for (const auto& [name, pin] : master.pins) {
      if (pin.use == "POWER" || pin.use == "GROUND") {
        continue;
      }
      const auto [x, y] = placement_pin_offset(master, name, copy.cell.place.orientation, fp.dbu);
      placed.pin_locations[physical::connection_label({copy.cell.name, name})] = {copy.cell.place.x + x, copy.cell.place.y + y};
    }
    writer.StartObject();
    writer.Key("source_net");
    writer.String(copy.leaf_net.c_str());
    writer.Key("source_instance");
    writer.String(copy.cell.name.c_str());
    writer.Key("source_pin");
    writer.String(copy.output.c_str());
    driver_pins.emplace(copy.cell.name, copy.output);
    writer.Key("source_master");
    writer.String(copy.cell.macro.c_str());
    writer.Key("cloned_from");
    writer.String(copy.gate.c_str());
    if (!copy.chunk.empty()) {
      source_entry(copy.chunk, copy.cell.name.str(), copy.output);
    }
    writer.Key("sinks");
    writer.Uint64(copy.sinks.size());
    writer.Key("gate_leaf_sinks");
    writer.StartArray();
    for (const auto& sink : copy.sinks) {
      writer.String(physical::connection_label(sink).c_str());
    }
    writer.EndArray();
    writer.Key("retained_sinks");
    writer.StartArray();
    writer.EndArray();
    writer.Key("branches");
    writer.StartArray();
    writer.EndArray();
    writer.Key("wire_repeaters");
    writer.Uint(0);
    writer.Key("root_buffer");
    writer.Null();
    writer.EndObject();
  }
  if (useful_skew) {
    for (const auto& edit : useful_skew->delays) {
      std::vector<Pin> sinks;
      Pin              source;
      for (const auto& conn : edit.sinks) {
        const auto sink
            = conn.instance == "PIN" ? graph.get_output_pin(conn.pin) : nodes.at(conn.instance.str()).get_sink_pin(conn.pin);
        if (sink.is_invalid() || sink.get_driver_pins().size() != 1 || (!source.is_invalid() && source != sink.get_driver_pin())) {
          throw std::runtime_error("hold repair needs the same single driver on " + edit.source_net);
        }
        source = sink.get_driver_pin();
        sinks.push_back(sink);
      }
      if (source.is_invalid() || nodes.contains(edit.cell.name.str())) {
        throw std::runtime_error("invalid hold buffer " + edit.cell.name);
      }
      auto node   = buffer_node(edit.cell.name.str(), edit.cell.macro.str(), edit.input, edit.output, edit.cell.place);
      auto input  = node.create_sink_pin(edit.input);
      auto output = node.create_driver_pin(edit.output);
      gu::set_bits(output, 1);
      input.connect_driver(source);
      for (auto sink : sinks) {
        gu::drop_drivers(sink);
        sink.connect_driver(output);
      }
      insert_hold_delay(fp, edit);
    }
  }
  writer.EndArray();
  writer.EndObject();
  file << '\n';
  file.close();
  placed.prects.published_clock_drivers.clear();
  for (const auto& [instance, port] : driver_pins) {
    placed.prects.published_clock_drivers.emplace(instance, port);
    const auto pin = instance == "PIN" ? graph.get_input_pin(port) : nodes.at(instance.str()).get_driver_pin(port);
    pin.attr(attrs::pcrs_clock_driver).set(1);
    gu::add_signal_roles(pin, attrs::signal_clock);
  }
  std::filesystem::rename(pending.path, path);
  return {std::move(buffers), std::move(driver_pins)};
}

void record_final_clock_leaf_geometry(const Result& placed, const std::string& output_dir) {
  const auto path = std::filesystem::path(output_dir) / "pcrs_clock.json";
  const auto input = file_utils::read_file(path.string());
  rapidjson::Document report;
  if (!input || report.Parse(input->c_str()).HasParseError() || !report.IsObject() || !report.HasMember("networks")) {
    throw std::runtime_error("cannot read the clock handoff at " + path.string());
  }
  auto& allocator = report.GetAllocator();
  const auto measure = [&](rapidjson::Value& owner, const rapidjson::Value& members) {
    if (members.Empty()) {
      return;
    }
    using Pin_position = std::pair<double, double>;
    std::vector<Pin_position> pins;
    pins.reserve(members.Size());
    for (const auto& member : members.GetArray()) {
      pins.push_back(placed.pin_locations.at(member.GetString()));
    }
    const auto [left, right] = std::ranges::minmax_element(pins, {}, &Pin_position::first);
    const auto [bottom, top] = std::ranges::minmax_element(pins, {}, &Pin_position::second);
    const auto x = std::midpoint(left->first, right->first);
    const auto y = std::midpoint(bottom->second, top->second);
    double star = 0, radius = 0;
    for (auto [u, v] : pins) {
      const auto distance = std::abs(u - x) + std::abs(v - y);
      star += distance;
      radius = std::max(radius, distance);
    }
    rapidjson::Value geometry(rapidjson::kObjectType);
    geometry.AddMember("center_x_um", x / placed.floorplan.dbu, allocator);
    geometry.AddMember("center_y_um", y / placed.floorplan.dbu, allocator);
    geometry.AddMember("star_wire_um", star / placed.floorplan.dbu, allocator);
    geometry.AddMember("max_member_distance_um", radius / placed.floorplan.dbu, allocator);
    if (owner.HasMember("final_placement")) {
      owner["final_placement"] = geometry;
    } else {
      owner.AddMember("final_placement", geometry, allocator);
    }
  };
  for (auto& network : report["networks"].GetArray()) {
    for (auto& branch : network["branches"].GetArray()) {
      if (branch["leaf"].GetBool()) {
        measure(branch, branch["children"]);
      }
    }
    if (network.HasMember("gate_leaf_sinks")) {
      measure(network, network["gate_leaf_sinks"]);
    }
  }
  Pending_clock_report pending(path);
  std::ofstream file(pending.path);
  file.exceptions(std::ios::badbit | std::ios::failbit);
  rapidjson::OStreamWrapper stream(file);
  rapidjson::Writer writer(stream);
  report.Accept(writer);
  file << '\n';
  file.close();
  std::filesystem::rename(pending.path, path);
}

Clock_handoff build_clock_handoff(Result& placed, const Options& options, const std::vector<std::string>& macro_libraries,
                                  std::string_view sdc_text, std::unique_ptr<timing::fast::Physical_ranker> timer,
                                  const std::string& previous_clock_report) {
  bool rebuild = !placed.prects.published_clock_drivers.empty();
  for (auto node : placed.graph->body().nodes()) {
    if (node.attr(attrs::pcrs_clock_buffer).has()) {
      rebuild = true;
      break;
    }
  }
  if (!rebuild) {
    return build_clock_handoff_impl(placed, options, macro_libraries, sdc_text, std::move(timer),
                                    read_clock_history(previous_clock_report));
  }
  // Keep the accepted graph, placement and extracted model together until the
  // replacement has passed the same planning and timing checks as an initial tree.
  Result work     = placed;
  work.fp_library = std::make_shared<hhds::GraphLibrary>();
  work.graph      = partition::flatten_hierarchy(placed.graph.get(),
                                            work.fp_library.get(),
                                            placed.graph->get_name(),
                                            nullptr,
                                            false,
                                                 {},
                                                 {},
                                            true);
  if (!work.graph) {
    throw std::runtime_error("could not clone native clock tree for rebuilding");
  }
  gu::visit_published_clock_drivers(*work.graph, [](const Pin& p) { p.attr(attrs::pcrs_clock_driver).del(); });
  work.prects.published_clock_drivers.clear();
  const auto previous = strip_clock_buffers(work, options.technology().lef);
  auto       result   = build_clock_handoff_impl(work, options, macro_libraries, sdc_text, std::move(timer), previous);
  placed              = std::move(work);
  return result;
}
}  // namespace livehd::placement

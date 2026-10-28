// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include <algorithm>
#include <cmath>
#include <format>
#include <map>
#include <stdexcept>
#include <unordered_set>

#include "attrs.hpp"
#include "node_util.hpp"
#include "pass_opentimer.hpp"
#include "rc_batch.hpp"

const ot::Pin& Pass_opentimer::physical_timing_pin(std::string_view physical) const {
  const auto index = physical_feedback_index_ ? physical_feedback_index_->find(physical) : physical_timing_pins_.size();
  if (index >= physical_timing_pins_.size()) {
    throw std::out_of_range("physical timing pin not found: " + std::string(physical));
  }
  return *physical_timing_pins_[index];
}

void Pass_opentimer::apply_physical_timing_cuts(const std::set<std::string>& cuts) {
  if (cuts.empty()) {
    return;
  }
  if (!physical_rc_) {
    throw std::invalid_argument("physical timing cuts require placement RC");
  }
  timer.update_netlist();
  std::unordered_set<const ot::Pin*> roots;
  for (const auto& net : physical_rc_->nets) {
    if (cuts.contains(net.driver)) {
      roots.insert(&physical_timing_pin(net.driver));
    }
  }
  std::vector<std::pair<size_t, uint8_t>> masks;
  for (const auto& arc : timer.arcs()) {
    if (arc.is_net_arc() && roots.contains(&arc.from())) {
      masks.emplace_back(arc.idx(), 0);
    }
  }
  if (!masks.empty()) {
    timer.refine_case_masks(std::move(masks));
  }
}

void Pass_opentimer::validate_physical_rc(const std::shared_ptr<hhds::Graph>& graph) const {
  if (!physical_rc_) {
    return;
  }
  if (!spef_file_list.empty()) {
    throw std::invalid_argument("placed RC and synth.spef cannot be combined");
  }
  for (const auto& net : physical_rc_->nets) {
    livehd::physical::validate_rc_tree(net);
  }
  std::map<std::string, Ann_place>   positions;
  std::map<std::string, std::string> masters;
  for (auto node : graph->body().nodes()) {
    if (!node.get_subnode_io() && livehd::graph_util::type_op_of(node) != Ntype_op::Memory) {
      continue;
    }
    // Every physical cell was placed; one without a position was added later.
    auto p = node.attr(livehd::attrs::place);
    if (!p.has()) {
      throw std::runtime_error("placement geometry changed; regenerate parasitics before STA: unplaced "
                               + std::string(node.get_name()));
    }
    positions.emplace(std::string(node.get_name()), p.get());
    if (const auto io = node.get_subnode_io()) {
      masters.emplace(std::string(node.get_name()), std::string(io->get_name()));
    }
  }
  for (const auto& cell : physical_rc_->cells) {
    auto it = positions.find(cell.name);
    if (it == positions.end() || it->second.x != cell.x || it->second.y != cell.y
        || static_cast<unsigned>(it->second.orientation) != cell.orientation
        || (cell.width && (it->second.width != cell.width || it->second.height != cell.height))
        || (masters.contains(cell.name) && !cell.master.empty() && masters.at(cell.name) != cell.master)) {
      throw std::runtime_error("placement geometry changed; regenerate parasitics before STA: " + cell.name);
    }
  }
  if (positions.size() != physical_rc_->cells.size()) {
    throw std::runtime_error("placement geometry changed; regenerate parasitics before STA: cell count differs");
  }
}
// Runs after prepare_critical_targets: a `pass.opentimer.targets` clock with
// no SDC only exists from then on. Critical mode assigns these RATs itself
// (annotate_critical times every native capture).
void Pass_opentimer::apply_physical_captures() {
  if (!physical_rc_ || critical_) {
    return;
  }
  for (const auto& [pin, clock] : physical_memory_captures_) {
    float period = 0;
    for (const auto& [name, target] : timing_targets_) {
      if (name == clock || std::find(target.ports.begin(), target.ports.end(), clock) != target.ports.end()
          || (timing_targets_.size() == 1 && target.ports.empty())) {
        period = target.period;
      }
    }
    if (period > 0) {
      for (const auto tran : {ot::RISE, ot::FALL}) {
        timer.set_rat(pin, ot::MAX, tran, period);
      }
    } else {
      constraints_complete_ = false;
    }
  }
}
void Pass_opentimer::apply_physical_rc(const std::shared_ptr<hhds::Graph>& graph) {
  if (!physical_rc_) {
    return;
  }
  validate_physical_rc(graph);
  timer.update_netlist();  // materialize pin/net builders before matching identities; RC is attached below
  if (previous_physical_rc_ && previous_physical_rc_->nets == physical_rc_->nets) {
    physical_updated_nets_ = 0;
    return;  // Master changes retain pin identities; unchanged wire RC stays attached.
  }
  std::map<std::string, std::string> ports;
  std::map<std::string, std::string> instances = physical_instances_;
  std::vector<std::pair<std::string_view, const ot::Pin*>> bindings;
  size_t                                                   binding_count = physical_rc_->nets.size();
  for (const auto& net : physical_rc_->nets) {
    binding_count += std::count_if(net.nodes.begin(), net.nodes.end(), [](const auto& node) { return !node.pin.empty(); });
  }
  bindings.reserve(binding_count);
  for (auto node : graph->body().nodes()) {
    if (!node.get_subnode_io() && livehd::graph_util::type_op_of(node) != Ntype_op::Memory) {
      continue;
    }
    const auto name = std::string(node.get_name());
    instances.emplace(
        name,
        hier_mode_ ? name : std::format("{}__n{}", livehd::graph_util::default_instance_name(node), node.get_debug_nid()));
  }
  const auto add_ports = [&](const auto& decls) {
    for (const auto& d : decls) {
      const auto bits = std::max(1, static_cast<int>(d.bits));
      for (int bit = 0; bit < bits; ++bit) {
        ports.emplace(bits > 1 ? std::format("{}[{}]", d.name, bit) : d.name, bit > 0 ? std::format("{}.{}", d.name, bit) : d.name);
      }
    }
  };
  add_ports(graph->get_io()->get_input_pin_decls());
  add_ports(graph->get_io()->get_output_pin_decls());
  auto timer_pin = [&](const std::string& physical) {
    if (auto it = physical_native_pins_.find(physical); it != physical_native_pins_.end()) {
      return it->second;
    }
    if (auto it = ports.find(physical); it != ports.end()) {
      return it->second;
    }
    if (const auto colon = physical.rfind(':'); colon != std::string::npos) {
      if (auto it = instances.find(physical.substr(0, colon)); it != instances.end()) {
        return it->second + physical.substr(colon);
      }
    }
    return physical;
  };
  std::map<std::string, const livehd::physical::Rc_net*> previous;
  if (previous_physical_rc_) {
    for (const auto& net : previous_physical_rc_->nets) {
      previous.emplace(net.driver, &net);
    }
  }
  livehd::opentimer::Rc_batch batch(timer);
  for (const auto& net : physical_rc_->nets) {
    const auto old                    = previous.find(net.driver);
    const bool unchanged              = old != previous.end() && *old->second == net;
    const auto root                   = timer_pin(net.driver);
    const auto driver                 = timer.pins().find(root);
    if (driver == timer.pins().end() || !driver->second.net()) {
      throw std::runtime_error("placement RC driver missing from STA: " + root);
    }
    bindings.emplace_back(net.driver, &driver->second);
    const auto               name = driver->second.net()->name();
    spef::Net                mapped(name, 0);
    std::vector<std::string> nodes;
    for (size_t i = 0; i < net.nodes.size(); ++i) {
      const auto& node  = net.nodes[i];
      auto        label = node.pin.empty() ? std::format("{}:__lhd_steiner_{}", name, i) : timer_pin(node.pin);
      if (!node.pin.empty()) {
        const auto pin                  = timer.pins().find(label);
        if (pin == timer.pins().end() || !pin->second.net() || pin->second.net()->name() != name) {
          throw std::runtime_error("placement RC endpoint differs from STA connectivity: " + label + " on " + name + ", actual "
                                   + (pin == timer.pins().end() ? std::string("missing pin")
                                      : pin->second.net()       ? pin->second.net()->name()
                                                                : std::string("no net")));
        }
        bindings.emplace_back(node.pin, &pin->second);
      }
      if (!std::isfinite(node.capacitance_ff) || node.capacitance_ff < 0) {
        throw std::invalid_argument("invalid placement capacitance");
      }
      if (!unchanged) {
        nodes.push_back(label);
        mapped.caps.emplace_back(label, "", node.capacitance_ff);
        mapped.lcap += node.capacitance_ff;
      }
    }
    if (unchanged) {
      continue;
    }
    for (const auto& edge : net.edges) {
      if (edge.from >= nodes.size() || edge.to >= nodes.size() || !std::isfinite(edge.resistance_ohm) || edge.resistance_ohm < 0) {
        throw std::invalid_argument("invalid placement RC edge");
      }
      mapped.ress.emplace_back(nodes[edge.from], nodes[edge.to], edge.resistance_ohm);
    }
    batch.add(std::move(mapped));
  }
  batch.flush();
  physical_updated_nets_ = batch.size();
  // Borrow labels only during this call; the RC snapshot can be released on
  // the next evaluation. Identical physical labels resolve to the same pin.
  std::sort(bindings.begin(), bindings.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
  bindings.erase(std::unique(bindings.begin(), bindings.end(), [](const auto& a, const auto& b) { return a.first == b.first; }),
                 bindings.end());
  if (!physical_feedback_index_ || physical_feedback_index_->labels.size() != bindings.size()
      || !std::equal(physical_feedback_index_->labels.begin(),
                     physical_feedback_index_->labels.end(),
                     bindings.begin(),
                     [](const auto& label, const auto& entry) { return label == entry.first; })) {
    auto index = std::make_shared<livehd::timing::Feedback::Pin_index>();
    index->labels.reserve(bindings.size());
    for (const auto& [physical, pin] : bindings) {
      (void)pin;
      index->labels.emplace_back(physical);
    }
    physical_feedback_index_ = std::move(index);
  }
  physical_timing_pins_.clear();
  physical_timing_pins_.reserve(bindings.size());
  for (const auto& [physical, pin] : bindings) {
    (void)physical;
    physical_timing_pins_.push_back(pin);
  }
}

// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include <cmath>
#include <format>
#include <map>
#include <stdexcept>

#include "attrs.hpp"
#include "node_util.hpp"
#include "pass_opentimer.hpp"

void Pass_opentimer::apply_physical_rc(const std::shared_ptr<hhds::Graph>& graph) {
  if (!physical_rc_) {
    return;
  }
  if (!spef_file_list.empty()) {
    throw std::invalid_argument("placed RC and synth.spef cannot be combined");
  }
  std::map<std::string, Ann_place> positions;
  for (auto node : graph->body().nodes()) {
    if (!node.get_subnode_io()) {
      continue;
    }
    // Every physical cell was placed; one without a position was added later.
    auto p = node.attr(livehd::attrs::place);
    if (!p.has()) {
      throw std::runtime_error("placement geometry changed; regenerate parasitics before STA: unplaced "
                               + std::string(node.get_name()));
    }
    positions.emplace(std::string(node.get_name()), p.get());
  }
  for (const auto& cell : physical_rc_->cells) {
    auto it = positions.find(cell.name);
    if (it == positions.end() || it->second.x != cell.x || it->second.y != cell.y
        || static_cast<unsigned>(it->second.orientation) != cell.orientation) {
      throw std::runtime_error("placement geometry changed; regenerate parasitics before STA: " + cell.name);
    }
  }
  if (positions.size() != physical_rc_->cells.size()) {
    throw std::runtime_error("placement geometry changed; regenerate parasitics before STA: cell count differs");
  }
  timer.update_timing();  // materialize queued pin/net builders before matching identities
  std::map<std::string, std::string> ports;
  std::map<std::string, std::string> instances;
  for (auto node : graph->body().nodes()) {
    if (!node.get_subnode_io()) {
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
  spef::Spef data;
  data.capacitance_unit = "1 FF";
  data.resistance_unit  = "1 OHM";
  for (const auto& net : physical_rc_->nets) {
    const auto root   = timer_pin(net.driver);
    const auto driver = timer.pins().find(root);
    if (driver == timer.pins().end() || !driver->second.net()) {
      throw std::runtime_error("placement RC driver missing from STA: " + root);
    }
    const auto               name = driver->second.net()->name();
    spef::Net                mapped(name, 0);
    std::vector<std::string> nodes;
    for (size_t i = 0; i < net.nodes.size(); ++i) {
      const auto& node  = net.nodes[i];
      auto        label = node.pin.empty() ? std::format("{}:__lhd_steiner_{}", name, i) : timer_pin(node.pin);
      if (!node.pin.empty()) {
        const auto pin = timer.pins().find(label);
        if (pin == timer.pins().end() || !pin->second.net() || pin->second.net()->name() != name) {
          throw std::runtime_error("placement RC endpoint differs from STA connectivity: " + label + " on " + name);
        }
      }
      if (!std::isfinite(node.capacitance_ff) || node.capacitance_ff < 0) {
        throw std::invalid_argument("invalid placement capacitance");
      }
      nodes.push_back(label);
      mapped.caps.emplace_back(label, "", node.capacitance_ff);
      mapped.lcap += node.capacitance_ff;
    }
    for (const auto& edge : net.edges) {
      if (edge.from >= nodes.size() || edge.to >= nodes.size() || !std::isfinite(edge.resistance_ohm) || edge.resistance_ohm < 0) {
        throw std::invalid_argument("invalid placement RC edge");
      }
      mapped.ress.emplace_back(nodes[edge.from], nodes[edge.to], edge.resistance_ohm);
    }
    data.nets.push_back(std::move(mapped));
  }
  timer.read_parasitics(std::move(data));
}

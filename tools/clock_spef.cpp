// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "clock_spef.hpp"

#include <cmath>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

#include "ot/parser-spef/parser-spef.hpp"

namespace livehd::physical {
namespace {
double scale(const std::string& units, const std::map<std::string, double>& factors) {
  std::istringstream in(units);
  double             magnitude;
  std::string        unit, extra;
  if (!(in >> magnitude >> unit) || in >> extra || !factors.contains(unit) || !std::isfinite(magnitude) || magnitude <= 0) {
    throw std::invalid_argument("unsupported SPEF units: " + units);
  }
  return magnitude * factors.at(unit);
}

std::string unescape(std::string_view name) {
  std::string decoded;
  for (size_t i = 0; i < name.size(); ++i) {
    if (name[i] == '\\' && i + 1 < name.size()) {
      ++i;
    }
    decoded += name[i];
  }
  return decoded;
}

Rc_net read_net(const spef::Net& source, const Net& def, char delimiter, double cap_scale, double res_scale,
                const std::unordered_map<std::string, const Macro*>& instances,
                const std::unordered_map<std::string, std::string>&  ports) {
  Rc_net                                             net;
  std::unordered_map<std::string, const Connection*> endpoints;
  for (const auto& c : def.connections) {
    const auto name = c.instance == "PIN" ? unescape(c.pin.str()) : unescape(c.instance.str()) + delimiter + unescape(c.pin.str());
    if (!endpoints.emplace(name, &c).second) {
      throw std::invalid_argument("ambiguous DEF endpoint on " + def.name);
    }
  }
  std::unordered_map<std::string, uint32_t> ids;
  const auto                                node = [&](const std::string& key) {
    const auto [it, inserted] = ids.emplace(key, net.nodes.size());
    if (inserted) {
      net.nodes.emplace_back();
    }
    return it->second;
  };
  for (const auto& connection : source.connections) {
    const auto name  = unescape(connection.name);
    const auto label = endpoints.find(name);
    if (label == endpoints.end()) {
      throw std::invalid_argument("SPEF pin absent or duplicated on " + def.name + ": " + name);
    }
    const auto& endpoint  = *label->second;
    const auto  pin_label = connection_label(endpoint);
    const bool  port      = endpoint.instance == "PIN";
    const auto& direction
        = port ? ports.at(endpoint.pin.str()) : instances.at(endpoint.instance.str())->pins.at(endpoint.pin).direction;
    const auto expected = direction == "INPUT" ? spef::ConnectionDirection::INPUT : spef::ConnectionDirection::OUTPUT;
    if ((direction != "INPUT" && direction != "OUTPUT") || connection.direction != expected
        || port != (connection.type == spef::ConnectionType::EXTERNAL)) {
      throw std::invalid_argument("SPEF direction disagrees with DEF/LEF on " + def.name + ": " + name);
    }
    net.nodes[node(connection.name)].pin = pin_label;
    if (port == (direction == "INPUT")) {
      if (!net.driver.empty()) {
        throw std::invalid_argument("multiple SPEF drivers on " + def.name);
      }
      net.driver = pin_label;
    }
    endpoints.erase(label);
  }
  if (!endpoints.empty() || net.driver.empty()) {
    throw std::invalid_argument("SPEF lacks DEF endpoint or driver on " + def.name);
  }
  for (const auto& [first, second, ohms] : source.ress) {
    if (!std::isfinite(ohms) || ohms < 0) {
      throw std::invalid_argument("invalid SPEF resistance on " + def.name);
    }
    net.edges.push_back({node(first), node(second), ohms * res_scale});
  }
  for (const auto& [first, second, cap] : source.caps) {
    if (!std::isfinite(cap) || cap < 0) {
      throw std::invalid_argument("invalid SPEF capacitance on " + def.name);
    }
    if (second.empty()) {
      net.nodes[node(first)].capacitance_ff += cap * cap_scale;
    } else {
      bool local = false;
      for (const auto* plate : {&first, &second}) {
        if (const auto it = ids.find(*plate); it != ids.end()) {
          net.nodes[it->second].capacitance_ff += cap * cap_scale;
          local                                 = true;
        }
      }
      if (!local) {
        throw std::invalid_argument("coupling capacitor has no local plate on " + def.name);
      }
    }
  }
  validate_rc_tree(net);
  return net;
}
}  // namespace

Parasitics clock_report_spef(const std::string& path, const Floorplan& fp, const Technology& tech,
                             const std::set<std::string>* selected_nets) {
  spef::Spef spef;
  if (!spef.read(path) || spef.error) {
    throw std::runtime_error("cannot read SPEF " + path
                             + (spef.error ? " at line " + std::to_string(spef.error->line_number) : ""));
  }
  if (spef.delimiter.size() != 1
      || (spef.design_flow.find("PIN_CAP ") != std::string::npos && spef.design_flow.find("PIN_CAP NONE") == std::string::npos)) {
    throw std::invalid_argument("SPEF requires one pin delimiter and wire-only capacitances");
  }
  spef.expand_name();
  const double                                cap_scale = scale(spef.capacitance_unit,
                                                                {
                                     {"FF",    1},
                                     {"PF", 1000},
                                     {"NF",  1e6},
                                     { "F", 1e15}
  });
  const double                                res_scale = scale(spef.resistance_unit,
                                                                {
                                     { "OHM",    1},
                                     {"KOHM", 1000},
                                     {"MOHM",  1e6}
  });
  std::unordered_map<std::string, const Net*> nets;
  for (const auto& net : fp.nets) {
    if (selected_nets && !selected_nets->contains(net.name.str())) {
      continue;
    }
    if (!nets.emplace(unescape(net.name.str()), &net).second) {
      throw std::invalid_argument("ambiguous DEF net " + net.name);
    }
  }
  std::unordered_map<std::string, const Macro*> instances;
  for (const auto& cell : fp.components) {
    instances.emplace(cell.name.str(), &tech.macros.at(cell.macro));
  }
  std::unordered_map<std::string, std::string> ports;
  for (const auto& terminal : fp.terminals) {
    ports.emplace(terminal.name, terminal.direction);
  }
  Parasitics rc;
  rc.identity    = path;
  rc.approximate = false;
  std::set<std::string> selected;
  if (selected_nets) {
    for (const auto& [name, net] : nets) {
      selected.insert(name);
    }
    if (selected.size() != selected_nets->size()) {
      throw std::invalid_argument("selected SPEF net absent from DEF");
    }
  }
  for (const auto& net : spef.nets) {
    const auto name = unescape(net.name);
    if (selected_nets && !selected.contains(name)) {
      continue;
    }
    const auto found = nets.find(name);
    if (found == nets.end()) {
      throw std::invalid_argument("missing or duplicate SPEF net " + net.name);
    }
    rc.nets.push_back(read_net(net, *found->second, spef.delimiter.front(), cap_scale, res_scale, instances, ports));
    nets.erase(found);
  }
  for (const auto& [name, net] : nets) {
    if (net->connections.size() > 1) {
      throw std::invalid_argument("missing SPEF annotation for " + name);
    }
  }
  return rc;
}
}  // namespace livehd::physical

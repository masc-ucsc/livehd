// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include <array>
#include <filesystem>
#include <format>
#include <fstream>
#include <unordered_map>

#include "absl/container/flat_hash_set.h"
#include "core/hash_util.hpp"
#include "graph/attrs.hpp"
#include "route_store.hpp"
#include "routing_connectivity.hpp"

namespace livehd::physical {
namespace {
class Bindings {
  std::unordered_map<std::string, hhds::Node_class> nodes;
  std::set<std::string>                             ambiguous;

public:
  explicit Bindings(hhds::Graph& graph) {
    for (auto node : graph.body().nodes()) {
      const auto name = std::string(node.get_name());
      if (!name.empty() && !nodes.emplace(name, node).second) {
        ambiguous.insert(name);
      }
    }
  }
  std::optional<hhds::Pin_class> driver(const Net& net) const {
    if (net.connections.empty()) {
      return {};
    }
    const auto& source = net.connections.front();
    auto        found  = nodes.find(source.instance);
    if (found == nodes.end() || ambiguous.contains(source.instance)) {
      return {};
    }
    for (auto pin : found->second.out_sorted_pins()) {
      std::string name(pin.get_pin_name());
      if (const auto io = found->second.get_subnode_io()) {
        for (const auto& declaration : io->get_output_pin_decls()) {
          if (declaration.port_id == pin.get_port_id()) {
            name = declaration.name;
            break;
          }
        }
      }
      if (name == source.pin) {
        return pin;
      }
    }
    return {};
  }
};
std::string context(const std::string& technology, const Floorplan& frame, const Net& net, const Route_components& components) {
  if (technology.empty()) {
    throw std::invalid_argument("missing route technology identity");
  }
  auto       hash = hash_util::fnv1a64(technology);
  const auto text = [&](std::string_view value) {
    hash = hash_util::fnv1a64_u64(value.size(), hash);
    hash = hash_util::fnv1a64(value, hash);
  };
  hash = hash_util::fnv1a64_u64(frame.dbu, hash);
  for (const auto& endpoint : net.connections) {
    const auto found = components.find(endpoint.instance);
    if (found == components.end()) {
      throw std::invalid_argument("graph route requires cell endpoints");
    }
    const auto& cell = *found->second;
    text(endpoint.instance);
    text(endpoint.pin);
    text(cell.macro);
    // DEF carries master, origin and orientation, but not body dimensions.
    // The LEF-content identity above protects master dimensions. Canonicalize
    // LOCKED to FIXED, as write_def/read_def do for endpoint locks.
    const auto status = cell.place.status == Status::LOCKED ? Status::FIXED : cell.place.status;
    for (auto value : {cell.place.x, cell.place.y, int64_t(cell.place.orientation), int64_t(status)}) {
      hash = hash_util::fnv1a64_u64(value, hash);
    }
  }
  return std::format("signal-route-v1:{:016x}", hash);
}
}  // namespace
std::string route_technology_identity(const std::vector<std::string>& lefs) {
  if (lefs.empty()) {
    throw std::invalid_argument("missing route technology files");
  }
  auto hash = hash_util::kFnv1a64_offset;
  for (const auto& path : lefs) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
      throw std::runtime_error("cannot read route technology: " + path);
    }
    hash = hash_util::fnv1a64_u64(std::filesystem::file_size(path), hash);
    std::array<char, 65536> buffer;
    while (file.read(buffer.data(), buffer.size()) || file.gcount()) {
      hash = hash_util::fnv1a64(std::string_view(buffer.data(), file.gcount()), hash);
    }
    if (!file.eof()) {
      throw std::runtime_error("failed reading route technology: " + path);
    }
  }
  return std::format("lef-v1:{:016x}", hash);
}
size_t publish_routes(hhds::Graph& graph, const Floorplan& frame, const std::string& technology) {
  Bindings         bindings(graph);
  Route_components components(frame);
  struct Pending {
    hhds::Pin_class   driver;
    const Net*        net;
    std::string       identity;
    Route_width_rules rules;
  };
  std::vector<Pending>                   pending;
  absl::flat_hash_set<hhds::Class_index> drivers;
  for (const auto& net : frame.nets) {
    if (net.wires.empty()) {
      continue;
    }
    const auto driver = bindings.driver(net);
    if (!driver) {
      throw std::invalid_argument("missing graph route driver: " + net.name);
    }
    if (!drivers.insert(driver->get_class_index()).second) {
      throw std::invalid_argument("duplicate graph route driver: " + net.name);
    }
    Route_width_rules rules;
    for (const auto& w : net.wires) {
      for (const auto& p : w.paths) {
        if (!p.taper_rule.empty()) {
          rules.emplace(p.taper_rule, frame.nondefault_rules.at(p.taper_rule));
        }
      }
    }
    pending.push_back({*driver, &net, context(technology, frame, net, components), std::move(rules)});
  }
  // Publication belongs on the host's private candidate graph. Drop obsolete
  // records before installing this generation; never retain a removed route.
  for (auto node : graph.body().nodes()) {
    for (auto pin : node.out_sorted_pins()) {
      clear_route(pin);
    }
  }
  for (const auto& item : pending) {
    store_route(item.driver, frame.dbu, *item.net, item.rules, item.identity);
  }
  return pending.size();
}
size_t replay_routes(hhds::Graph& graph, Floorplan& frame, const std::string& technology) {
  Bindings                                     bindings(graph);
  Route_components                             components(frame);
  std::vector<std::pair<size_t, Stored_route>> pending;
  auto                                         rules = frame.nondefault_rules;
  for (size_t i = 0; i < frame.nets.size(); ++i) {
    auto&      net    = frame.nets[i];
    const auto driver = bindings.driver(net);
    if (!driver || !driver->attr(attrs::route_records_v1).has()) {
      continue;
    }
    auto route = load_route(*driver, frame.dbu, net, context(technology, frame, net, components));
    if (!route) {
      throw std::invalid_argument("stale graph route: " + net.name);
    }
    if (!net.wires.empty() && net.wires != route->net.wires) {
      throw std::invalid_argument("graph/DEF route mismatch: " + net.name);
    }
    for (const auto& [name, widths] : route->rules) {
      const auto [found, inserted] = rules.emplace(name, widths);
      if (!inserted && found->second != widths) {
        throw std::invalid_argument("graph route width-rule conflict: " + name);
      }
    }
    pending.emplace_back(i, std::move(*route));
  }
  // Complete validation before changing any route or rule in the frame.
  for (auto& [i, route] : pending) {
    frame.nets[i].wires = std::move(route.net.wires);
  }
  frame.nondefault_rules = std::move(rules);
  return pending.size();
}
}  // namespace livehd::physical

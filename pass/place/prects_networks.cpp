// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "prects_networks.hpp"

#include <algorithm>
#include <sstream>
#include <string_view>
#include <unordered_map>

#include "pass/liberty/liberty_dff.hpp"

namespace livehd::placement {
Prects_library prects_library(const std::string& library, const std::string& control_ports) {
  return prects_library(liberty::Cell_library(library), control_ports);
}

Prects_library prects_library(const liberty::Cell_library& library, const std::string& control_ports) {
  Prects_library result;
  const auto     parse = [](std::string text, auto& names) {
    std::replace(text.begin(), text.end(), ',', ' ');
    std::istringstream input(text);
    for (std::string name; input >> name;) {
      names.insert(name);
    }
  };
  parse(control_ports, result.ports);
  result.override_controls = !control_ports.empty();
  auto state               = library.dffs;
  for (const auto& cell : library.async_dffs) {
    state.push_back(cell);
  }
  for (const auto& cell : library.latches) {
    state.push_back(cell);
  }
  for (const auto& cell : state) {
    auto& pins = result.controls[cell.name];
    if (!cell.clk_pin.empty()) {
      result.clocks[cell.name].insert(cell.clk_pin);
    }
    for (const auto& pin : {cell.clk_pin, cell.reset0_pin, cell.reset1_pin}) {
      if (!pin.empty()) {
        pins.insert(pin);
      }
    }
    for (const auto& pin : {cell.reset0_pin, cell.reset1_pin}) {
      if (!pin.empty()) {
        result.resets[cell.name].insert(pin);
      }
    }
  }
  for (const auto& cell : library.memories) {
    for (const auto& pin : cell.pins) {
      if (pin.clock_input) {
        result.controls[cell.name].insert(pin.name);
        result.clocks[cell.name].insert(pin.name);
      }
    }
  }
  for (const auto& cell : library.combinational) {
    if (cell.inputs.size() == 1) {
      const auto truth = liberty::cell_truth(cell);
      if (truth && (*truth == 0xaaaaaaaaaaaaaaaaULL || *truth == 0x5555555555555555ULL)) {
        result.links[cell.name].emplace_back(cell.inputs.front(), cell.output);
      }
    }
  }
  for (const auto& cell : library.icgs) {
    result.controls[cell.name].insert({cell.clk_pin, cell.out_pin});
    result.clocks[cell.name].insert({cell.clk_pin, cell.out_pin});
    result.links[cell.name].emplace_back(cell.clk_pin, cell.out_pin);
    // Enable/test are functional data, not part of the distributed clock.
  }
  return result;
}

Prects_networks prects_networks(const physical::Technology& tech, const physical::Floorplan& fp, const Prects_library& roles,
                                const timing::Feedback& timing) {
  Prects_networks result;
  result.ignored.resize(fp.nets.size());
  struct Instance_roles {
    const physical::Macro*       master   = nullptr;
    const std::set<std::string>* controls = nullptr;
    const std::set<std::string>* clocks   = nullptr;
    const std::set<std::string>* resets   = nullptr;
  };
  // All referenced names and role sets remain owned by the immutable inputs.
  // Resolve each library master once, then use one instance lookup per pin.
  std::map<std::string_view, Instance_roles>                  master_roles;
  std::unordered_map<std::string_view, const Instance_roles*> instances;
  instances.reserve(fp.components.size());
  for (const auto& c : fp.components) {
    auto [it, inserted] = master_roles.try_emplace(c.macro);
    auto& info          = it->second;
    if (inserted) {
      if (auto m = tech.macros.find(c.macro); m != tech.macros.end()) {
        info.master = &m->second;
      }
      const auto lookup = [&](const auto& map) -> const std::set<std::string>* {
        const auto found = map.find(c.macro);
        return found == map.end() ? nullptr : &found->second;
      };
      info.controls = lookup(roles.controls);
      info.clocks   = lookup(roles.clocks);
      info.resets   = lookup(roles.resets);
    }
    instances.emplace(c.name, &info);
  }
  // Only distribution-cell links query pin_net. Indexing every data pin
  // duplicates the whole design's labels at the final RC memory peak.
  // Preseed the queried identities, then retain their first connection just as
  // the full index did, including aliases with the same physical label.
  const auto                              unbound = fp.nets.size();
  std::unordered_map<std::string, size_t> pin_net;
  for (const auto& c : fp.components) {
    const auto links = roles.links.find(c.macro);
    if (links == roles.links.end()) {
      continue;
    }
    for (const auto& [a, b] : links->second) {
      pin_net.try_emplace(c.name + ":" + a, unbound);
      pin_net.try_emplace(c.name + ":" + b, unbound);
    }
  }
  std::vector<std::vector<size_t>> adjacent(fp.nets.size());
  std::vector<size_t>              pending, pending_clocks;
  std::vector<bool>                clock_nets(fp.nets.size());
  const auto                       mark_clock = [&](size_t n) {
    if (!clock_nets[n]) {
      clock_nets[n] = true;
      pending_clocks.push_back(n);
    }
  };
  const auto mark = [&](size_t n) {
    if (!result.ignored[n]) {
      result.ignored[n] = true;
      pending.push_back(n);
    }
  };
  for (size_t n = 0; n < fp.nets.size(); ++n) {
    for (const auto& c : fp.nets[n].connections) {
      const auto label = physical::connection_label(c);
      if (const auto entry = pin_net.find(label); entry != pin_net.end() && entry->second == unbound) {
        entry->second = n;
      }
      if ((c.instance == "PIN" && roles.reset_ports.contains(c.pin))
          || (c.instance != "PIN" && (roles.reset_pins.contains(label) || roles.reset_pins.contains(c.instance + "/" + c.pin)))) {
        mark(n);
      }
      if ((c.instance == "PIN" && roles.clock_ports.contains(c.pin)) || (c.instance != "PIN" && roles.clock_pins.contains(label))) {
        mark_clock(n);
      }
      if (!roles.override_controls && c.instance != "PIN"
          && (roles.pins.contains(label) || roles.pins.contains(c.instance + "/" + c.pin))) {
        mark(n);
      }
      if (timing.is_clock_pin(c.instance, c.pin)) {
        mark_clock(n);
        if (!roles.override_controls) {
          mark(n);
        }
      }
      if (c.instance == "PIN") {
        if (roles.ports.contains(c.pin)) {
          mark(n);
        }
        continue;
      }
      const auto instance = instances.find(c.instance);
      if (instance == instances.end()) {
        continue;
      }
      const auto& info    = *instance->second;
      const auto  matches = [&](const std::set<std::string>* pins) {
        return pins && (pins->contains(c.pin) || pins->contains(c.pin.substr(0, c.pin.find('['))));
      };
      if (matches(info.clocks)) {
        mark_clock(n);
      }
      if (info.master) {
        if (auto pin = info.master->pins.find(c.pin); pin != info.master->pins.end()) {
          const auto& use = pin->second.use;
          if (use == "CLOCK") {
            mark_clock(n);
          }
          if ((!roles.override_controls && use == "CLOCK") || use == "POWER" || use == "GROUND") {
            mark(n);
          }
        }
      }
      if ((!roles.override_controls && matches(info.controls)) || matches(info.resets)) {
        mark(n);
      }
    }
  }
  for (const auto& c : fp.components) {
    const auto links = roles.links.find(c.macro);
    if (links == roles.links.end()) {
      continue;
    }
    for (const auto& [a, b] : links->second) {
      const auto x = pin_net.find(c.name + ":" + a), y = pin_net.find(c.name + ":" + b);
      if (x != pin_net.end() && y != pin_net.end() && x->second != unbound && y->second != unbound && x->second != y->second) {
        adjacent[x->second].push_back(y->second);
        adjacent[y->second].push_back(x->second);
      }
    }
  }
  // Bidirectional closure includes both upstream buffers and sibling branches.
  // It stops at arbitrary combinational logic, state and ICG enable/test pins.
  for (size_t p = 0; p < pending.size(); ++p) {
    for (size_t next : adjacent[pending[p]]) {
      mark(next);
    }
  }
  for (size_t p = 0; p < pending_clocks.size(); ++p) {
    for (size_t next : adjacent[pending_clocks[p]]) {
      mark_clock(next);
    }
  }
  for (size_t n : pending) {
    for (const auto& c : fp.nets[n].connections) {
      const auto label = physical::connection_label(c);
      result.pins.insert(label);
      if (!clock_nets[n]) {
        result.timing_cuts.insert(label);
      }
      if (c.instance != "PIN") {
        result.pins.insert(c.instance + "/" + c.pin);
      }
    }
  }
  return result;
}

timing::Feedback prects_data_timing(const timing::Feedback& source, const Prects_networks& networks) {
  auto result = source;
  for (const auto& pin : networks.pins) {
    result.pins.erase(pin);
    if (result.ranking) {
      const auto index = result.ranking->layout->find(pin);
      if (index < result.ranking->pins.size()) {
        result.ranking->pins[index] = {};
      }
    }
  }
  std::erase_if(result.path, [&](const auto& pin) { return networks.pins.contains(pin); });
  return result;
}
}  // namespace livehd::placement

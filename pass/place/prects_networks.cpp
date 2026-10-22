// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "prects_networks.hpp"

#include <algorithm>
#include <sstream>
#include <unordered_map>

#include "pass/liberty/liberty_dff.hpp"

namespace livehd::placement {
Prects_library prects_library(const std::string& library, const std::string& control_ports) {
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
  if (library.empty()) {
    return result;
  }
  auto state = liberty::scan_dff_cells(library);
  for (const auto& cell : liberty::scan_async_dff_cells(library)) {
    state.push_back(cell);
  }
  for (const auto& cell : liberty::scan_latch_cells(library)) {
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
  for (const auto& cell : liberty::scan_memory_cells(library)) {
    for (const auto& pin : cell.pins) {
      if (pin.clock_input) {
        result.controls[cell.name].insert(pin.name);
        result.clocks[cell.name].insert(pin.name);
      }
    }
  }
  for (const auto& cell : liberty::scan_comb_cells(library)) {
    if (cell.inputs.size() == 1) {
      const auto truth = liberty::cell_truth(cell);
      if (truth && (*truth == 0xaaaaaaaaaaaaaaaaULL || *truth == 0x5555555555555555ULL)) {
        result.links[cell.name].emplace_back(cell.inputs.front(), cell.output);
      }
    }
  }
  for (const auto& cell : liberty::scan_icg_cells(library)) {
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
  std::unordered_map<std::string, const physical::Macro*> masters;
  for (const auto& c : fp.components) {
    if (auto m = tech.macros.find(c.macro); m != tech.macros.end()) {
      masters.emplace(c.name, &m->second);
    }
  }
  std::unordered_map<std::string, size_t> pin_net;
  std::vector<std::vector<size_t>>        adjacent(fp.nets.size());
  std::vector<size_t>                     pending, pending_clocks;
  std::vector<bool>                       clock_nets(fp.nets.size());
  const auto                              mark_clock = [&](size_t n) {
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
  std::unordered_map<std::string, const std::set<std::string>*> controls, clocks, resets;
  for (const auto& c : fp.components) {
    if (auto found = roles.clocks.find(c.macro); found != roles.clocks.end()) {
      clocks.emplace(c.name, &found->second);
    }
    if (auto found = roles.controls.find(c.macro); found != roles.controls.end()) {
      controls.emplace(c.name, &found->second);
    }
    if (auto found = roles.resets.find(c.macro); found != roles.resets.end()) {
      resets.emplace(c.name, &found->second);
    }
  }
  for (size_t n = 0; n < fp.nets.size(); ++n) {
    for (const auto& c : fp.nets[n].connections) {
      const auto label = c.instance == "PIN" ? c.pin : c.instance + ":" + c.pin;
      pin_net.emplace(label, n);
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
      auto t = timing.pins.find(label);
      if (t == timing.pins.end() && c.instance != "PIN") {
        t = timing.pins.find(c.instance + "/" + c.pin);
      }
      if (t != timing.pins.end() && t->second.clock) {
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
      if (auto p = clocks.find(c.instance);
          p != clocks.end() && (p->second->contains(c.pin) || p->second->contains(c.pin.substr(0, c.pin.find('['))))) {
        mark_clock(n);
      }
      if (auto m = masters.find(c.instance); m != masters.end()) {
        if (auto pin = m->second->pins.find(c.pin); pin != m->second->pins.end() && pin->second.use == "CLOCK") {
          mark_clock(n);
        }
        if (auto pin = m->second->pins.find(c.pin); pin != m->second->pins.end()
                                                    && ((!roles.override_controls && pin->second.use == "CLOCK")
                                                        || pin->second.use == "POWER" || pin->second.use == "GROUND")) {
          mark(n);
        }
      }
      if (auto p = controls.find(c.instance);
          !roles.override_controls && p != controls.end()
          && (p->second->contains(c.pin) || p->second->contains(c.pin.substr(0, c.pin.find('['))))) {
        mark(n);
      }
      if (auto p = resets.find(c.instance);
          p != resets.end() && (p->second->contains(c.pin) || p->second->contains(c.pin.substr(0, c.pin.find('['))))) {
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
      if (x != pin_net.end() && y != pin_net.end() && x->second != y->second) {
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
      const auto label = c.instance == "PIN" ? c.pin : c.instance + ":" + c.pin;
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
  }
  std::erase_if(result.path, [&](const auto& pin) { return networks.pins.contains(pin); });
  return result;
}
}  // namespace livehd::placement

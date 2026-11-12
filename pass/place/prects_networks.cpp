// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "prects_networks.hpp"

#include <algorithm>
#include <sstream>
#include <string_view>
#include <unordered_map>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
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
  absl::flat_hash_map<std::string_view, const Instance_roles*> instances;
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
  absl::flat_hash_map<std::string, size_t> pin_net;
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
  // Per net: the connections that carry a distribution role, the distribution-cell (link) pins among all of its
  // connections and among the role ones, and whether a named root (a control, clock or reset port or pin) rather than
  // a cell's pin role put the net here.
  std::vector<uint32_t> role_pins(fp.nets.size()), link_pins(fp.nets.size()), role_link_pins(fp.nets.size());
  std::vector<bool>     named(fp.nets.size());
  bool                  in_connection = false, counted = false, link_pin = false;
  const auto            role          = [&](size_t n) {
    if (in_connection && !counted) {
      counted = true;
      ++role_pins[n];
      role_link_pins[n] += link_pin;
    }
  };
  const auto mark_clock = [&](size_t n) {
    role(n);
    if (!clock_nets[n]) {
      clock_nets[n] = true;
      pending_clocks.push_back(n);
    }
  };
  const auto mark = [&](size_t n) {
    role(n);
    if (!result.ignored[n]) {
      result.ignored[n] = true;
      pending.push_back(n);
    }
  };
  // The pin-name sets are ordered sets of strings: one lookup compares a label with about log2(size) others. A
  // connection asks several of them, so hash views of the same strings answer them (the sets own the text).
  using Name_set = absl::flat_hash_set<std::string_view>;
  const auto view = [](const std::set<std::string>& names) {
    Name_set out;
    out.reserve(names.size());
    for (const auto& name : names) {
      out.insert(name);
    }
    return out;
  };
  const Name_set reset_pin_names = view(roles.reset_pins), clock_pin_names = view(roles.clock_pins), pin_names = view(roles.pins);
  // A ranking names every pin it knows as "instance:pin" (a terminal by its bare name): the pins a clock reaches are collected
  // once, instead of a string search over all its labels for every connection.
  const bool ranked_clocks = timing.ranking && timing.ranking->layout
                             && timing.ranking->layout->labels.size() == timing.ranking->pins.size();
  Name_set   clock_labels;
  if (ranked_clocks) {
    const auto& labels = timing.ranking->layout->labels;
    for (size_t i = 0; i < labels.size(); ++i) {
      if (timing.ranking->pins[i].clock) {
        clock_labels.insert(labels[i]);
      }
    }
  }
  std::string    conn_label, conn_slashed;  // "instance:pin" or the bare terminal name; "instance/pin"
  for (size_t n = 0; n < fp.nets.size(); ++n) {
    for (const auto& c : fp.nets[n].connections) {
      const bool terminal = c.instance == "PIN";
      conn_label.clear();
      if (!terminal) {
        conn_label.append(c.instance).append(1, ':');
      }
      conn_label.append(c.pin);
      conn_slashed.clear();
      if (!terminal) {
        conn_slashed.append(c.instance).append(1, '/').append(c.pin);
      }
      const auto entry = pin_net.empty() ? pin_net.end() : pin_net.find(conn_label);
      if (entry != pin_net.end() && entry->second == unbound) {
        entry->second = n;
      }
      in_connection = true;
      counted       = false;
      link_pin      = entry != pin_net.end();
      link_pins[n] += link_pin;
      if (!roles.reset_is_data
          && ((terminal && roles.reset_ports.contains(c.pin))
              || (!terminal && (reset_pin_names.contains(conn_label) || reset_pin_names.contains(conn_slashed))))) {
        named[n] = true;
        mark(n);
      }
      if ((terminal && roles.clock_ports.contains(c.pin)) || (!terminal && clock_pin_names.contains(conn_label))) {
        named[n] = true;
        mark_clock(n);
      }
      if (!roles.override_controls && !terminal && (pin_names.contains(conn_label) || pin_names.contains(conn_slashed))) {
        named[n] = true;
        mark(n);
      }
      // STA flags a pin a clock reaches and every state cell's own clock pin alike. Only the first is a named root:
      // a port, or a pin that is not its cell's clock pin (decided below, once the cell is known).
      // pin_rank reads "instance:pin", and "instance/pin" only when the first spelling is not in the ranking at all.
      const bool sta_clock = !ranked_clocks ? timing.is_clock_pin(c.instance, c.pin)
                             : clock_labels.contains(conn_label)
                                 || (!terminal && clock_labels.contains(conn_slashed) && timing.ranking->find(conn_label) == nullptr);
      if (sta_clock) {
        mark_clock(n);
        if (!roles.override_controls) {
          mark(n);
        }
      }
      if (c.instance == "PIN") {
        if (roles.ports.contains(c.pin)) {
          named[n] = true;
          mark(n);
        }
        named[n] = named[n] || sta_clock;
        continue;
      }
      const auto instance = instances.find(c.instance);
      if (instance == instances.end()) {
        named[n] = named[n] || sta_clock;
        continue;
      }
      const auto& info    = *instance->second;
      const auto  matches = [&](const std::set<std::string>* pins) {
        return pins && (pins->contains(c.pin) || pins->contains(c.pin.substr(0, c.pin.find('['))));
      };
      named[n] = named[n] || (sta_clock && !matches(info.clocks));
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
      // A flop's asynchronous reset pins are in `controls` as well as `resets`: as data they stay out of both.
      const auto reset_pin = [&] { return info.resets && (info.resets->contains(c.pin) || info.resets->contains(c.pin.substr(0, c.pin.find('[')))); };
      if ((!roles.override_controls && matches(info.controls) && !(roles.reset_is_data && reset_pin()))
          || (!roles.reset_is_data && matches(info.resets))) {
        // An excluded reset stays a false path whatever else it feeds (the pre-pcrs policy): only clock evidence
        // is weighed against the ordinary pins below.
        named[n] = named[n] || (!roles.reset_is_data && matches(info.resets));
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
  in_connection = false;
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
  // A data signal that also clocks a latch or feeds a clock gate (an FSM state behind its fanout buffers) is reached
  // by the closure like a clock tree. Without a named root, a connected distribution whose ordinary pins outnumber
  // its role pins is data: left excluded, nothing pulls its buffers toward their loads and no path through it is
  // ever timed. The count is over the whole distribution, so a tree is one decision and never half a clock.
  {
    std::vector<int> group(fp.nets.size(), -1);
    std::vector<size_t> members;
    std::vector<bool>   data(fp.nets.size());
    for (size_t seed : pending) {
      if (group[seed] >= 0) {
        continue;
      }
      members.assign(1, seed);
      group[seed]     = static_cast<int>(seed);
      bool   has_name = false;
      size_t roles_n = 0, ordinary = 0;
      for (size_t p = 0; p < members.size(); ++p) {
        const size_t n  = members[p];
        has_name        = has_name || named[n];
        roles_n        += role_pins[n];
        ordinary       += fp.nets[n].connections.size() + role_link_pins[n] - role_pins[n] - link_pins[n];
        for (size_t next : adjacent[n]) {
          if (group[next] < 0 && result.ignored[next]) {
            group[next] = static_cast<int>(seed);
            members.push_back(next);
          }
        }
      }
      if (!has_name && ordinary > roles_n) {
        for (size_t n : members) {
          data[n] = true;
        }
      }
    }
    std::erase_if(pending, [&](size_t n) {
      if (data[n]) {
        result.ignored[n] = false;
      }
      return data[n];
    });
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

timing::Feedback prects_data_timing(timing::Feedback result, const Prects_networks& networks) {
  for (const auto& pin : networks.pins) {
    result.erase_pin(pin);
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

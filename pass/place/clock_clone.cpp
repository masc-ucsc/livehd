// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "clock_clone.hpp"

#include <algorithm>
#include <format>
#include <map>
#include <set>

#include "core/hash_util.hpp"
#include "row_occupancy.hpp"
#include "technology_rc.hpp"

namespace livehd::placement {
std::optional<Clock_clone> plan_clock_clone(const physical::Floorplan& fp, const physical::Technology& tech,
                                            const Clock_clone_leaf& leaf, const pcrs::Clock_limits& limits,
                                            const Output_fanout_limits& input_limits, std::string& decision, bool upstream,
                                            const pcrs::Clock_driver* requested_drive) {
  Clock_clone edit;
  if (!leaf.sinks.empty()) {
    edit.chunk = leaf.sinks.front().chunk;
  }
  edit.gate         = leaf.gate;
  edit.clk          = leaf.clk;
  edit.enable       = leaf.enable;
  edit.test         = leaf.test;
  edit.output       = leaf.output;
  const auto reject = [&](const char* reason) -> std::optional<Clock_clone> {
    decision = reason;
    return {};
  };
  if (leaf.path.empty() || !leaf.driver) {
    return reject("missing_clone_leaf");
  }
  const auto&                                            buffer = leaf.path.front().instance;
  std::map<std::string_view, const physical::Component*> cells;
  for (const auto& c : fp.components) {
    cells.emplace(c.name, &c);
  }
  if (!cells.contains(buffer)) {
    return reject("retired_leaf");
  }
  const std::string name = std::format("__pcrs_icg_{:016x}", hash_util::fnv1a64(buffer));
  if (cells.contains(name)) {
    return reject("instance_name_in_use");
  }
  std::map<std::pair<std::string_view, std::string_view>, const physical::Net*> nets;
  const auto watch = [&](const std::string& cell, const std::string& pin) {
    nets.emplace(std::pair{std::string_view(cell), std::string_view(pin)}, nullptr);
  };
  if (upstream) {
    if (!leaf.upstream_tap) {
      return reject("missing_clock_tap");
    }
    edit.upstream_tap = leaf.upstream_tap;
    nets.emplace(std::pair{edit.upstream_tap->instance.view(), edit.upstream_tap->pin.view()}, nullptr);
  }
  watch(leaf.gate, leaf.clk);
  watch(leaf.gate, leaf.enable);
  if (!leaf.test.empty()) {
    watch(leaf.gate, leaf.test);
  }
  watch(buffer, leaf.buffer_output);
  for (const auto& branch : leaf.path) {
    watch(branch.instance, branch.input);
  }
  for (const auto& net : fp.nets) {
    for (const auto& c : net.connections) {
      auto it = nets.find({c.instance.view(), c.pin.view()});
      if (it != nets.end()) {
        if (it->second) {
          return reject("ambiguous_gate_binding");
        }
        it->second = &net;
      }
    }
  }
  const auto get      = [&](const std::string& cell, const std::string& pin) { return nets.at({cell, pin}); };
  const auto editable = [&](const physical::Net* net) {
    return net && net->wires.empty() && !fp.snapshot_ndr.net_rules.contains(net->name.view())
           && (net->use.empty() || net->use == "SIGNAL" || net->use == "CLOCK");
  };
  std::map<Name, size_t> new_loads;
  for (const auto& pin : {leaf.clk, leaf.enable, leaf.test}) {
    if (pin.empty()) {
      continue;
    }
    const auto* net = get(leaf.gate, pin);
    if (pin == leaf.clk && edit.upstream_tap) {
      // The alternative crosses just the verified identity buffer on the
      // original gate clock. Recheck the binding against earlier accepted edits.
      if (!net || !std::ranges::any_of(net->connections, [&](const auto& c) {
            return c.instance == edit.upstream_tap->instance
                   && tech.macros.at(cells.at(c.instance)->macro).pins.at(c.pin).direction == "OUTPUT";
          })) {
        return reject("changed_clock_tap");
      }
      net = nets.at({edit.upstream_tap->instance.view(), edit.upstream_tap->pin.view()});
    }
    if (!editable(net)) {
      return reject("protected_gate_input");
    }
    edit.inputs.emplace_back(pin, net->name.str());
    const size_t                        sinks = net->connections.size() - 1 + ++new_loads[net->name];
    std::optional<physical::Connection> source;
    for (const auto& conn : net->connections) {
      if (conn.instance == "PIN") {
        if (!std::ranges::any_of(fp.terminals, [&](const auto& t) { return t.name == conn.pin && t.direction == "INPUT"; })) {
          continue;
        }
      } else {
        const auto master = cells.at(conn.instance)->macro;
        if (tech.macros.at(master).pins.at(conn.pin).direction != "OUTPUT") {
          continue;
        }
        const auto limit = input_limits.find({master, conn.pin});
        if (limit != input_limits.end() && sinks > limit->second) {
          return reject("clone_input_fanout");
        }
      }
      if (source) {
        return reject("ambiguous_gate_binding");
      }
      source = conn;
    }
    if (!source) {
      return reject("missing_gate_input_driver");
    }
    if (pin == leaf.clk) {
      if (sinks > 30) {
        return reject("clone_input_fanout");
      }
      edit.clock_driver = *source;
    }
  }
  const auto* output = get(buffer, leaf.buffer_output);
  if (!editable(output)) {
    return reject("protected_leaf");
  }
  edit.leaf_net = output->name.str();
  for (const auto& c : output->connections) {
    if (c.instance != buffer) {
      edit.sinks.push_back(c);
    }
  }
  std::set<std::string> expected;
  for (const auto& sink : leaf.sinks) {
    expected.insert(sink.name);
  }
  for (const auto& sink : edit.sinks) {
    if (!expected.erase(physical::connection_label(sink))) {
      return reject("leaf_membership_changed");
    }
  }
  if (!expected.empty()) {
    return reject("leaf_membership_changed");
  }
  for (size_t i = 0; i < leaf.path.size(); ++i) {
    const auto& branch = leaf.path[i];
    const auto* net    = get(branch.instance, branch.input);
    if (!editable(net)) {
      return reject("protected_clock_branch");
    }
    edit.removed_buffers.push_back(branch.instance);
    if (net->connections.size() > 2) {
      edit.detached_net = net->name.str();
      edit.detached_pin = {branch.instance, branch.input};
      break;
    }
    // Keep the original gate and its last active group.
    if (i + 1 == leaf.path.size()) {
      return reject("last_gate_leaf");
    }
    edit.removed_nets.push_back(net->name.str());
  }

  const auto&         old = *cells.at(buffer);
  // Removed buffers free their sites; all other cells and blockages remain
  // occupied, including clock copies accepted earlier.
  physical::Floorplan frame;
  frame.dbu       = fp.dbu;
  frame.die       = fp.die;
  frame.rows      = fp.rows;
  frame.blockages = fp.blockages;
  std::set<std::string_view> removed(edit.removed_buffers.begin(), edit.removed_buffers.end());
  for (const auto& net : fp.special_nets) {
    for (const auto& conn : net.connections) {
      if (removed.contains(conn.instance) || conn.instance == leaf.gate) {
        return reject("explicit_special_connection");
      }
    }
  }
  for (const auto& cell : fp.components) {
    if (!removed.contains(cell.name)) {
      frame.components.push_back(cell);
      continue;
    }
    const auto& b        = tech.macros.at(cell.macro);
    edit.area_delta_um2 -= b.width * b.height;
  }
  const auto& bm        = tech.macros.at(old.macro);
  const auto  rows      = unoccupied_rows(tech, frame);
  auto        direct    = limits;
  direct.fanout         = 30;
  bool       site_found = false;
  const auto fit        = [&](const pcrs::Clock_driver& driver) {
    const auto& master = tech.macros.at(driver.master);
    auto        site   = closest_free_site(frame,
                                  rows,
                                  master,
                                  old.place.x + (bm.width - master.width) * fp.dbu / 2,
                                  old.place.y + (bm.height - master.height) * fp.dbu / 2);
    if (!site) {
      return false;
    }
    site_found        = true;
    const auto [x, y] = placement_pin_offset(master, leaf.output, site->orientation, fp.dbu);
    pcrs::Clock_source source{(site->x + x) / fp.dbu, (site->y + y) / fp.dbu, driver, leaf.sinks};
    try {
      (void)pcrs::plan_clock_tree({}, {}, direct, source);
    } catch (const pcrs::Clock_infeasible&) {
      return false;
    }
    edit.cell            = {name, driver.master, *site};
    edit.area_delta_um2 += master.width * master.height;
    return true;
  };
  if (requested_drive ? fit(*requested_drive) : (fit(*leaf.driver) || std::ranges::any_of(leaf.alternative_drives, fit))) {
    return edit;
  }
  return reject(site_found ? "clone_drive_limit" : "no_clone_site");
}

void apply_clock_clone(physical::Floorplan& fp, const Clock_clone& edit) {
  for (auto& net : fp.nets) {
    if (net.name == edit.detached_net && std::erase(net.connections, edit.detached_pin) != 1) {
      throw std::logic_error("clock clone parent changed");
    }
    if (net.name == edit.leaf_net) {
      net.connections = {
          {edit.cell.name, edit.output}
      };
      net.connections.insert(net.connections.end(), edit.sinks.begin(), edit.sinks.end());
    }
    for (const auto& [pin, input] : edit.inputs) {
      if (net.name == input) {
        net.connections.push_back({edit.cell.name, pin});
      }
    }
  }
  std::set<std::string_view> nets(edit.removed_nets.begin(), edit.removed_nets.end()),
      cells(edit.removed_buffers.begin(), edit.removed_buffers.end());
  std::erase_if(fp.nets, [&](const auto& n) { return nets.contains(n.name); });
  std::erase_if(fp.components, [&](const auto& c) { return cells.contains(c.name); });
  fp.components.push_back(edit.cell);
}
}  // namespace livehd::placement

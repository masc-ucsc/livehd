// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "clock_hold.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <limits>
#include <set>

#include "core/hash_util.hpp"
#include "row_occupancy.hpp"
#include "technology_rc.hpp"

namespace livehd::placement {
namespace {
bool editable(const physical::Net& net) {
  return net.wires.empty() && net.use != "CLOCK" && net.use != "POWER" && net.use != "GROUND";
}

bool better(const Hold_candidate& a, const Hold_candidate& b) {
  return std::tuple{-a.gain_per_area, a.worst_hold, a.area, a.net, std::string_view(a.driver->master)}
         < std::tuple{-b.gain_per_area, b.worst_hold, b.area, b.net, std::string_view(b.driver->master)};
}
}  // namespace

void reserve_hold_pin_access(std::vector<Row_gaps>& rows, const Satlegalize_route_grid& grid) {
  if (!grid.bins_x || !grid.bins_y || size_t(grid.bins_x) * grid.bins_y != grid.pin_utilization.size()
      || !(grid.bounds.xh > grid.bounds.xl) || !(grid.bounds.yh > grid.bounds.yl)) {
    throw std::invalid_argument("invalid pin access grid for hold repair");
  }
  const double dx = (grid.bounds.xh - grid.bounds.xl) / grid.bins_x;
  const double dy = (grid.bounds.yh - grid.bounds.yl) / grid.bins_y;
  for (unsigned x = 0; x < grid.bins_x; ++x) {
    for (unsigned y = 0; y < grid.bins_y; ++y) {
      const double utilization = grid.pin_utilization[size_t(x) * grid.bins_y + y];
      if (!std::isfinite(utilization) || utilization < 0) {
        throw std::invalid_argument("invalid hold pin utilization");
      }
      if (utilization < 1) {
        continue;
      }
      reserve_gaps(
          rows,
          {grid.bounds.xl + dx * x, grid.bounds.yl + dy * y, grid.bounds.xl + dx * (x + 1), grid.bounds.yl + dy * (y + 1)});
    }
  }
}

std::vector<Hold_candidate> rank_hold_delays(const physical::Floorplan& fp, const physical::Technology& tech,
                                             const Hold_repair_options& options, const Endpoint_hold& hold, double remaining_area,
                                             const Endpoint_hold* setup, const std::set<std::pair<size_t, std::string>>& rejected) {
  std::map<Name, Name> masters;
  for (const auto& c : fp.components) {
    masters.emplace(c.name, c.macro);
  }
  std::set<Name> outputs;
  for (const auto& t : fp.terminals) {
    if (t.direction == "OUTPUT") {
      outputs.emplace(t.name);
    }
  }
  std::vector<Hold_candidate> choices;
  for (size_t i = 0; i < fp.nets.size(); ++i) {
    const auto& net = fp.nets[i];
    if (!editable(net)) {
      continue;
    }
    double              cap = 0;
    std::vector<double> deficits;
    double              worst = 0;
    for (const auto& pin : net.connections) {
      const auto it = hold.find(Pin_label(pin.instance, pin.pin));
      if (it == hold.end() || it->second >= 0) {
        continue;
      }
      if (pin.instance == "PIN") {
        if (!outputs.contains(pin.pin)) {
          continue;
        }
      } else {
        const auto& master = masters.at(pin.instance);
        if (tech.macros.at(master).pins.at(pin.pin).direction != "INPUT") {
          continue;
        }
        const auto cell = options.timing.cells.find(master);
        if (cell != options.timing.cells.end()) {
          const auto load = cell->second.pins.find(pin.pin);
          if (load != cell->second.pins.end()) {
            cap += std::max(load->second.capacitance_ff[0].value_or(0), load->second.capacitance_ff[1].value_or(0));
          }
        }
      }
      const auto   headroom = setup ? setup->find(it->first) : hold.end();
      const double credit
          = setup && headroom != setup->end() ? std::min<double>(-it->second, std::max(0.f, headroom->second)) : -it->second;
      worst = std::min<double>(worst, it->second);
      deficits.push_back(credit);
    }
    if (deficits.empty()) {
      continue;
    }
    std::vector<Hold_candidate> on_net;
    for (const auto* driver : options.drivers) {
      if (rejected.contains({i, driver->master})) {
        continue;
      }
      const auto&  macro = tech.macros.at(driver->master);
      const double area  = macro.width * macro.height;
      if (area <= 0 || area > remaining_area + 1e-12 || (driver->max_load_ff > 0 && cap > driver->max_load_ff)
          || (driver->max_fanout && deficits.size() > driver->max_fanout)) {
        continue;
      }
      // Rank by the faster transition. The acceptance pass times the actual
      // legalized cell and its wires across the design's timing corners.
      double delay = driver->intrinsic_ps + .001 * driver->resistance_ohm * cap;
      if (!driver->arcs.empty()) {
        delay = std::numeric_limits<double>::infinity();
        for (const auto& arc : driver->arcs) {
          delay = std::min(delay, arc.delay.lookup({driver->reference_slew_ps, cap}));
        }
      }
      double gain = 0;
      for (double deficit : deficits) {
        gain += std::clamp(delay, 0., deficit);
      }
      if (gain > 0) {
        on_net.push_back({i, driver, gain / area, worst, area});
      }
    }
    // Keep an alternative master on each net so a site or timing rejection
    // can still be repaired. Bound the number of full STA evaluations per edit.
    const auto end = on_net.begin() + std::min<size_t>(2, on_net.size());
    std::ranges::partial_sort(on_net, end, better);
    for (auto it = on_net.begin(); it != end; ++it) {
      const auto pos = std::ranges::lower_bound(choices, *it, better);
      if (choices.size() < 6 || pos != choices.end()) {
        choices.insert(pos, *it);
        if (choices.size() > 6) {
          choices.pop_back();
        }
      }
    }
  }
  return choices;
}

std::optional<Hold_delay> plan_hold_delay(const physical::Floorplan& fp, const physical::Technology& tech,
                                          const pcrs::Clock_driver& buffer, const Endpoint_hold& hold, size_t index,
                                          const std::vector<Row_gaps>* rows, unsigned padding_sites) {
  if (fp.rows.empty() || index >= fp.nets.size()) {
    return {};
  }
  const auto& net = fp.nets[index];
  if (!editable(net)) {
    return {};
  }
  const auto& master = tech.macros.at(buffer.master);
  const auto  width = std::llround(master.width * fp.dbu), height = std::llround(master.height * fp.dbu);
  if (width <= 0 || height <= 0) {
    return {};
  }
  Hold_delay edit{
      {{}, buffer.master, {}},
      buffer.input,
      buffer.output,
      net.name.str(),
      {},
      index,
      {},
      master.width * master.height
  };
  std::map<std::string_view, const physical::Component*> cells;
  for (const auto& c : fp.components) {
    cells.emplace(c.name.view(), &c);
  }
  double cx = 0, cy = 0;
  for (const auto& pin : net.connections) {
    const auto it = hold.find(Pin_label(pin.instance, pin.pin));
    if (it == hold.end() || it->second >= 0) {
      continue;
    }
    if (pin.instance == "PIN") {
      const auto port = std::ranges::find(fp.terminals, pin.pin.view(), &physical::Terminal::name);
      if (port == fp.terminals.end() || port->direction != "OUTPUT") {
        continue;
      }
      cx += port->x;
      cy += port->y;
    } else {
      const auto& c = *cells.at(pin.instance.view());
      const auto& m = tech.macros.at(c.macro);
      if (m.pins.at(pin.pin).direction != "INPUT") {
        continue;
      }
      const auto off  = placement_pin_offset(m, pin.pin, c.place.orientation, fp.dbu);
      cx             += c.place.x + off.first;
      cy             += c.place.y + off.second;
    }
    edit.sinks.push_back(pin);
  }
  if (edit.sinks.empty()) {
    return {};
  }
  cx = cx / edit.sinks.size() - width / 2.;
  cy = cy / edit.sinks.size() - height / 2.;
  std::vector<Row_gaps> local;
  if (!rows) {
    local = unoccupied_rows(tech, fp, padding_sites);
    rows  = &local;
  }
  auto site = closest_free_site(fp, *rows, master, cx, cy, padding_sites);
  if (!site) {
    return {};
  }
  edit.cell.place = *site;
  struct Box {
    double xl = std::numeric_limits<double>::infinity(), yl = xl, xh = -xl, yh = -xl;
    void   add(double x, double y) {
      xl = std::min(xl, x);
      yl = std::min(yl, y);
      xh = std::max(xh, x);
      yh = std::max(yh, y);
    }
    double hpwl() const { return xh - xl + yh - yl; }
  };
  const auto hpwl_offset
      = [&](const physical::Macro& m, const Name& pin, physical::Orientation orient) -> std::pair<double, double> {
    if (!m.pins.at(pin).shapes.empty()) {
      return placement_pin_offset(m, pin, orient, fp.dbu);
    }
    // Abstract LEF pins can omit shapes. In that case HPWL uses the cell centre.
    Ann_place origin(0, 0, 0, 0);
    origin.orientation = orient;
    const auto rect
        = physical::placed_rect(m, origin, fp.dbu, {-m.origin_x, -m.origin_y, m.width - m.origin_x, m.height - m.origin_y});
    return {(rect.xl + rect.xh) * .5, (rect.yl + rect.yh) * .5};
  };
  Box before, source, output;
  for (const auto& pin : net.connections) {
    double x, y;
    if (pin.instance == "PIN") {
      auto port = std::ranges::find(fp.terminals, pin.pin, &physical::Terminal::name);
      if (port == fp.terminals.end()) {
        throw std::invalid_argument("unknown hold net terminal " + pin.pin.str());
      }
      x = port->x;
      y = port->y;
    } else {
      const auto& c       = *cells.at(pin.instance.view());
      const auto [dx, dy] = hpwl_offset(tech.macros.at(c.macro), pin.pin, c.place.orientation);
      x                   = c.place.x + dx;
      y                   = c.place.y + dy;
    }
    before.add(x, y);
    (std::ranges::find(edit.sinks, pin) == edit.sinks.end() ? source : output).add(x, y);
  }
  const auto add_delay_pin = [&](Box& box, const std::string& pin) {
    const auto [dx, dy] = hpwl_offset(master, pin, site->orientation);
    box.add(site->x + dx, site->y + dy);
  };
  add_delay_pin(source, buffer.input);
  add_delay_pin(output, buffer.output);
  edit.hpwl_delta_um = (source.hpwl() + output.hpwl() - before.hpwl()) / fp.dbu;
  const auto base    = std::format("__pcrs_hold_{:016x}", hash_util::fnv1a64(net.name.view()));
  for (unsigned i = 0;; ++i) {
    const auto name    = i ? base + "_" + std::to_string(i) : base;
    auto       new_net = name + "_" + edit.output;
    const auto named   = [&](const auto& item) { return item.name == new_net; };
    if (!cells.contains(name) && std::ranges::none_of(fp.nets, named) && std::ranges::none_of(fp.special_nets, named)
        && std::ranges::none_of(fp.terminals, named)) {
      edit.cell.name = name;
      edit.new_net   = std::move(new_net);
      return edit;
    }
  }
}
void insert_hold_delay(physical::Floorplan& fp, const Hold_delay& edit) {
  auto& source = fp.nets.at(edit.net_index);
  if (source.name != edit.source_net) {
    throw std::logic_error("hold repair source net changed");
  }
  physical::Net         output{edit.new_net, {{edit.cell.name, edit.output}}, {}, source.use};
  std::set<std::string> selected;
  for (const auto& sink : edit.sinks) {
    selected.insert(physical::connection_label(sink));
    output.connections.push_back(sink);
    if (sink.instance == "PIN") {
      auto port = std::ranges::find(fp.terminals, sink.pin.view(), &physical::Terminal::name);
      if (port == fp.terminals.end()) {
        throw std::logic_error("hold repair output port disappeared");
      }
      port->net = edit.new_net;
    }
  }
  if (std::erase_if(source.connections, [&](const auto& pin) { return selected.contains(physical::connection_label(pin)); })
      != selected.size()) {
    throw std::logic_error("hold repair sink set changed");
  }
  source.connections.push_back({edit.cell.name, edit.input});
  if (auto it = fp.snapshot_ndr.net_rules.find(edit.source_net); it != fp.snapshot_ndr.net_rules.end()) {
    fp.snapshot_ndr.net_rules.emplace(edit.new_net, it->second);
  }
  fp.nets.push_back(std::move(output));
  fp.components.push_back(edit.cell);
}
}  // namespace livehd::placement

// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "clock_gate.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <unordered_map>

#include "pass/liberty/clock_topology.hpp"

namespace livehd::pcrs {
namespace {
constexpr uint32_t missing = liberty::no_clock_id;
using Kind                 = liberty::Clock_cell_kind;
struct Shape {
  Kind                     kind;
  std::string              output;
  std::vector<std::string> inputs;
  // Inverting wire, transparent-low latch, or an active-high (AND) gate.
  bool                     polarity;
  std::vector<bool>        levels{};
};
struct Instance {
  const Shape*        shape;
  Name                name;
  std::vector<size_t> nets;
};
std::map<std::string, Shape, std::less<>> shapes(const liberty::Cell_library& lib) {
  std::map<std::string, Shape, std::less<>> result;
  for (const auto& cell : lib.combinational) {
    if (cell.inputs.size() == 1) {
      const auto truth = liberty::cell_truth(cell);
      if (truth == 0xaaaaaaaaaaaaaaaaULL || truth == 0x5555555555555555ULL) {
        result.emplace(cell.name, Shape{Kind::wire, cell.output, cell.inputs, truth == 0x5555555555555555ULL});
      }
    } else if (auto form = liberty::clock_gate_form(cell)) {
      result.emplace(cell.name, Shape{Kind::gate, cell.output, cell.inputs, form->active_output, std::move(form->levels)});
    }
  }
  for (const auto& cell : lib.dffs) {
    result.emplace(cell.name,
                   Shape{
                       Kind::flip_flop,
                       cell.q_pin,
                       {cell.clk_pin, cell.d_pin},
                       false
    });
  }
  for (const auto& cell : lib.latches) {
    if (!cell.is_async()) {
      result.emplace(cell.name,
                     Shape{
                         Kind::latch,
                         cell.q_pin,
                         {cell.clk_pin, cell.d_pin},
                         cell.en_low
      });
    }
  }
  return result;
}
}  // namespace
std::vector<Gate_merge> gate_merges(const physical::Floorplan& fp, const liberty::Cell_library& lib,
                                    const std::vector<bool>& clocks) {
  if (clocks.size() != fp.nets.size()) {
    throw std::invalid_argument("clock gate classification requires one flag per net");
  }
  if (lib.icgs.empty()) {
    return {};
  }
  std::map<std::string_view, const liberty::Icg_cell*> masters;
  for (const auto& master : lib.icgs) {
    masters.emplace(master.name, &master);
  }
  struct Gate {
    const physical::Component*         cell;
    const liberty::Icg_cell*           master;
    std::map<std::string_view, size_t> pins;
    bool                               eligible = true;
  };
  std::map<std::string_view, Gate>             gates;
  std::map<std::string_view, std::string_view> flop_clock, sinks;
  for (const auto* family : {&lib.dffs, &lib.async_dffs}) {
    for (const auto& flop : *family) {
      flop_clock.emplace(flop.name, flop.clk_pin);
    }
  }
  for (const auto& cell : fp.components) {
    if (const auto it = masters.find(cell.macro); it != masters.end() && cell.place.status == physical::Status::PLACED) {
      gates.emplace(cell.name, Gate{&cell, it->second, {}});
    } else if (const auto flop = flop_clock.find(cell.macro); flop != flop_clock.end()) {
      sinks.emplace(cell.name, flop->second);
    }
  }
  if (gates.size() < 2) {
    return {};
  }
  const auto exclude = [&](std::string_view instance) {
    if (auto it = gates.find(instance); it != gates.end()) {
      it->second.eligible = false;
    }
  };
  for (const auto& block : fp.blockages) {
    exclude(block.component);
  }
  for (const auto& block : fp.snapshot_blockages) {
    exclude(block.component);
  }
  for (const auto& net : fp.special_nets) {
    for (const auto& pin : net.connections) {
      exclude(pin.instance);
    }
  }
  for (size_t i = 0; i < fp.nets.size(); ++i) {
    const auto& net = fp.nets[i];
    for (const auto& pin : net.connections) {
      if (auto it = gates.find(pin.instance); it != gates.end()) {
        auto& gate = it->second;
        if (!gate.pins.emplace(pin.pin, i).second || !net.wires.empty() || fp.snapshot_ndr.net_rules.contains(net.name.view())) {
          gate.eligible = false;
        }
      }
    }
  }
  using Group = std::tuple<std::string_view, size_t, size_t, size_t>;
  std::map<Group, std::vector<const Gate*>> groups;
  for (const auto& [name, gate] : gates) {
    const auto& m    = *gate.master;
    const auto& pins = gate.pins;
    if (!gate.eligible || !pins.contains(m.clk_pin) || !pins.contains(m.en_pin) || !pins.contains(m.out_pin)
        || (!m.test_pin.empty() && !pins.contains(m.test_pin)) || pins.size() != (m.test_pin.empty() ? 3u : 4u)) {
      continue;
    }
    const auto clk = pins.at(m.clk_pin), out = pins.at(m.out_pin);
    if (!clocks[clk] || !clocks[out] || fp.nets[out].connections.size() < 2
        || !std::ranges::all_of(fp.nets[out].connections, [&](const auto& pin) {
             if (pin.instance == name && pin.pin == m.out_pin) {
               return true;
             }
             const auto s = sinks.find(pin.instance);
             return s != sinks.end() && s->second == pin.pin;
           })) {
      continue;
    }
    groups[{m.name, clk, pins.at(m.en_pin), m.test_pin.empty() ? missing : pins.at(m.test_pin)}].push_back(&gate);
  }
  std::vector<Gate_merge> proposals;
  for (auto& [key, group] : groups) {
    // Sweep along the wider dimension. Bound each search by its fourth
    // nearest Manhattan distance, including stable ties along rows or columns.
    auto [xmin, xmax] = std::ranges::minmax(group, {}, [](const auto* g) { return g->cell->place.x; });
    auto [ymin, ymax] = std::ranges::minmax(group, {}, [](const auto* g) { return g->cell->place.y; });
    bool by_x         = (xmax->cell->place.x - xmin->cell->place.x) >= (ymax->cell->place.y - ymin->cell->place.y);
    auto axis         = [&](const Gate* g) { return by_x ? g->cell->place.x : g->cell->place.y; };
    std::ranges::sort(group, {}, [&](const Gate* g) { return std::tuple{axis(g), g->cell->name.view()}; });
    std::set<std::pair<size_t, size_t>> pairs;
    for (size_t i = 0; i < group.size(); ++i) {
      std::vector<std::tuple<double, std::string_view, size_t>> near;
      auto                                                      visit = [&](size_t n) {
        const auto& a = *group[i]->cell;
        const auto& b = *group[n]->cell;
        if (near.size() == 4 && std::abs(double(axis(group[n])) - axis(group[i])) > std::get<0>(near.back())) {
          return false;
        }
        auto entry
            = std::tuple{std::abs(double(a.place.x) - b.place.x) + std::abs(double(a.place.y) - b.place.y), b.name.view(), n};
        near.insert(std::ranges::lower_bound(near, entry), entry);
        if (near.size() > 4) {
          near.pop_back();
        }
        return true;
      };
      for (size_t n = i; n > 0 && visit(n - 1); --n) {
      }
      for (size_t n = i + 1; n < group.size() && visit(n); ++n) {
      }
      for (const auto& [distance, name, n] : near) {
        pairs.emplace(std::min(i, n), std::max(i, n));
      }
    }
    for (auto [first, second] : pairs) {
      for (bool reverse : {false, true}) {
        const auto&  a = *group[reverse ? second : first];
        const auto&  b = *group[reverse ? first : second];
        const auto&  m = *a.master;
        const double distance
            = std::abs(double(a.cell->place.x) - b.cell->place.x) + std::abs(double(a.cell->place.y) - b.cell->place.y);
        proposals.push_back({a.cell->name.str(),
                             b.cell->name.str(),
                             m.clk_pin,
                             m.en_pin,
                             m.test_pin,
                             std::get<1>(key),
                             a.pins.at(m.out_pin),
                             b.pins.at(m.out_pin),
                             distance / fp.dbu});
      }
    }
  }
  // Keep the two directions together, including ties between equally spaced pairs.
  std::ranges::sort(proposals, {}, [](const Gate_merge& m) {
    return std::tie(m.distance_um, std::min(m.keep, m.drop), std::max(m.keep, m.drop), m.keep);
  });
  return proposals;
}
Gate_clock_paths discrete_gate_paths(const physical::Floorplan& fp, const liberty::Cell_library& lib,
                                     const std::vector<bool>& clocks) {
  if (clocks.size() != fp.nets.size()) {
    throw std::invalid_argument("clock gate classification requires one flag per net");
  }
  Gate_clock_paths                             result;
  const auto                                   masters = shapes(lib);
  std::vector<Instance>                        cells;
  std::unordered_map<std::string_view, size_t> index;
  for (const auto& c : fp.components) {
    if (const auto it = masters.find(c.macro); it != masters.end()) {
      index.emplace(c.name, cells.size());
      cells.push_back({&it->second, c.name, std::vector<size_t>(it->second.inputs.size(), missing)});
    }
  }
  std::vector<size_t> drivers(fp.nets.size(), missing);
  std::vector<bool>   ambiguous(fp.nets.size());
  for (size_t n = 0; n < fp.nets.size(); ++n) {
    for (const auto& c : fp.nets[n].connections) {
      if (c.instance == "PIN") {
        continue;
      }
      const auto it = index.find(c.instance);
      if (it == index.end()) {
        continue;
      }
      auto&       inst  = cells[it->second];
      const auto& shape = *inst.shape;
      if (c.pin == shape.output) {
        if (drivers[n] != missing && drivers[n] != it->second) {
          ambiguous[n] = true;
        }
        drivers[n] = it->second;
      } else {
        for (size_t i = 0; i < shape.inputs.size(); ++i) {
          if (c.pin == shape.inputs[i]) {
            inst.nets[i] = n;
          }
        }
      }
    }
  }
  std::map<std::string, std::set<std::string>> coupled;
  const auto                                   label = [&](const liberty::Clock_path_step& step) {
    const auto& src = cells[step.cell];
    return physical::connection_label(src.name, src.shape->inputs[step.input]);
  };
  liberty::visit_discrete_clock_gates(
      clocks,
      [&](uint32_t n) -> uint32_t { return ambiguous[n] ? liberty::conflicting_clock_driver : drivers[n]; },
      [&](uint32_t c) { return cells[c].shape->kind; },
      [&](uint32_t c) { return cells[c].nets.size(); },
      [&](uint32_t c, uint32_t i) -> uint32_t { return cells[c].nets[i]; },
      [&](uint32_t c) { return cells[c].shape->polarity; },
      [&](uint32_t c, uint32_t i) { return bool(cells[c].shape->levels[i]); },
      [&](uint32_t net, uint32_t driver, uint32_t clock, uint32_t control, const auto& en, const auto& cp, const auto& lp) {
        const auto& gate  = cells[driver];
        const auto& latch = cells[en.driver];
        size_t      c = cp.steps.size() - 1, l = lp.steps.size() - 1;
        while (c > 0 && l > 0 && cp.steps[c - 1].net == lp.steps[l - 1].net) {
          --c;
          --l;
        }
        const auto& cf     = cp.steps[c];
        const auto& lf     = lp.steps[l];
        const auto  retain = [&](const liberty::Clock_input_path& path, size_t end) {
          bool exclusive = true;
          for (size_t i = 0; i < end; ++i) {
            const auto& step = path.steps[i];
            result.direct_sinks[step.net].insert(label(step));
            const auto& output_net = fp.nets[step.net];
            const auto& input_net  = fp.nets[path.steps[i + 1].net];
            exclusive = exclusive && output_net.connections.size() == 2 && output_net.wires.empty() && input_net.wires.empty();
            if (exclusive) {
              result.private_phase_targets.emplace(cells[drivers[step.net]].name, label(path.steps.front()));
            }
          }
        };
        // A transparent latch closes on the gate edge. Preserve the two paths
        // from their common fork. An opposite-edge enable FF has an ordinary
        // half-cycle data path, so either clock arm can use electrical repeaters.
        if (latch.shape->kind == Kind::latch) {
          retain(cp, c);
          retain(lp, l);
          coupled[label(cf)].insert(label(lf));
          coupled[label(lf)].insert(label(cf));
        }
        result.gates.push_back({gate.name,
                                gate.shape->output,
                                gate.shape->inputs[clock],
                                gate.shape->inputs[control],
                                latch.name,
                                latch.shape->inputs[0],
                                latch.shape->inputs[1],
                                cf.net,
                                net,
                                label(cf),
                                label(lf),
                                gate.shape->polarity != gate.shape->levels[clock],
                                latch.shape->kind == Kind::flip_flop});
      });
  for (const auto& [seed, neighbors] : coupled) {
    if (result.leaf_group.contains(seed)) {
      continue;
    }
    std::vector<std::string> work{seed};
    result.leaf_group[seed] = seed;
    for (size_t i = 0; i < work.size(); ++i) {
      for (const auto& next : coupled.at(work[i])) {
        if (result.leaf_group.try_emplace(next, seed).second) {
          work.push_back(next);
        }
      }
    }
  }
  // A shared fork may itself be on the private phase path of another gate.
  // In that case every coupled load stays at the existing source.
  for (auto& [net, pins] : result.direct_sinks) {
    std::vector<std::string> work(pins.begin(), pins.end());
    for (size_t i = 0; i < work.size(); ++i) {
      const auto it = coupled.find(work[i]);
      if (it == coupled.end()) {
        continue;
      }
      for (const auto& peer : it->second) {
        if (pins.insert(peer).second) {
          work.push_back(peer);
        }
      }
    }
  }
  std::ranges::sort(result.gates, {}, &Discrete_clock_gate::instance);
  return result;
}
}  // namespace livehd::pcrs

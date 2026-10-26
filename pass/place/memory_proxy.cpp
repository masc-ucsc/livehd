// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "memory_proxy.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <limits>
#include <set>
#include <stdexcept>

#include "hash_util.hpp"
#include "node_util.hpp"
#include "pass/liberty/liberty_dff.hpp"

namespace livehd::placement {
bool memory_runtime_pin(hhds::Port_id pid) {
  const auto off = pid % Ntype::Memory_port_stride;
  return off == 0 || off == 2 || off == 3 || off == 4 || off == 12 || off == 13 || off == 14;
}
std::string memory_pin(hhds::Port_id pid, int bit, bool output) {
  return std::format("{}{}[{}]", output ? "out_" : "in_", pid, bit);
}
std::map<uint64_t, std::string> memory_proxies(hhds::Graph& graph, physical::Technology& tech, const std::string& liberty) {
  namespace gu = graph_util;
  std::map<uint64_t, std::string> result;
  double                          flop_area = std::numeric_limits<double>::infinity();
  for (auto node : graph.body().nodes()) {
    if (gu::type_op_of(node) != Ntype_op::Memory) {
      continue;
    }
    if (!std::isfinite(flop_area)) {
      flop_area = liberty::smallest_flop_area(liberty).value_or(std::numeric_limits<double>::infinity());
      if (!std::isfinite(flop_area)) {
        throw std::runtime_error("memory proxy needs a positive-area flop in synth.liberty");
      }
    }
    uint64_t           bits = 0, depth = 0;
    std::set<unsigned> ports;
    for (auto sink : node.inp_sorted_pins()) {
      const auto off = sink.get_port_id() % Ntype::Memory_port_stride;
      if (off == 0) {
        ports.insert(sink.get_port_id() / Ntype::Memory_port_stride);
      }
      if (off == 1 || off == 9) {
        const auto driver = sink.get_driver_pin();
        if (!driver.is_const() || !gu::const_of(driver).is_just_i64() || gu::const_of(driver).to_just_i64() <= 0) {
          throw std::runtime_error("memory proxy needs positive constant width and depth");
        }
        (off == 1 ? bits : depth) = gu::const_of(driver).to_just_i64();
      }
    }
    if (!bits || !depth || ports.empty()) {
      throw std::runtime_error("memory proxy needs width, depth and address ports");
    }
    const double area     = double(bits) * double(depth) * flop_area * std::pow(1.1, ports.size());
    const double raw_side = std::sqrt(area) * tech.dbu;
    if (!std::isfinite(raw_side) || raw_side > std::numeric_limits<int>::max() / 8.0) {
      throw std::runtime_error("memory proxy dimensions exceed physical coordinate range");
    }
    const auto      side = static_cast<int64_t>(std::ceil(raw_side));
    physical::Macro macro;
    macro.width = macro.height = double(side) / tech.dbu;
    macro.klass                = "BLOCK";
    macro.virtual_proxy        = true;
    std::vector<std::string> address, data, control;
    for (auto sink : node.inp_sorted_pins()) {
      if (!memory_runtime_pin(sink.get_port_id())) {
        continue;
      }
      const auto width = std::max(1, gu::bits_of(sink.get_driver_pin()));
      auto&      edge  = sink.get_port_id() % Ntype::Memory_port_stride == 0   ? address
                         : sink.get_port_id() % Ntype::Memory_port_stride == 3 ? data
                                                                               : control;
      for (int bit = 0; bit < width; ++bit) {
        auto name                  = memory_pin(sink.get_port_id(), bit, false);
        macro.pins[name].direction = "INPUT";
        edge.push_back(std::move(name));
      }
    }
    for (auto output : node.out_sorted_pins()) {
      for (int bit = 0; bit < std::max(1, gu::bits_of(output)); ++bit) {
        auto name                  = memory_pin(output.get_port_id(), bit, true);
        macro.pins[name].direction = "OUTPUT";
        data.push_back(std::move(name));
      }
    }
    if (tech.routing_layers.empty()) {
      throw std::runtime_error("memory proxy needs a routing layer");
    }
    const auto pins = [&](const std::vector<std::string>& names, int edge) {
      // Route away from the edge on the layer's preferred direction. Putting
      // every edge on the first layer can leave only off-grid planar access.
      const auto   preferred = edge == 1 ? "VERTICAL" : "HORIZONTAL";
      const auto   found     = std::find_if(tech.routing_layers.begin(), tech.routing_layers.end(), [&](const auto& name) {
        return tech.layers.at(name).direction == preferred;
      });
      const auto&  layer     = found == tech.routing_layers.end() ? tech.routing_layers.front() : *found;
      const double half      = std::min(macro.width / 100, std::max(1.0 / tech.dbu, tech.layers.at(layer).width / 2));
      for (size_t i = 0; i < names.size(); ++i) {
        const double   pos  = macro.width * (i + 1) / (names.size() + 1);
        physical::Rect rect = edge == 0   ? physical::Rect{0, pos - half, 2 * half, pos + half}
                              : edge == 1 ? physical::Rect{pos - half, 0, pos + half, 2 * half}
                                          : physical::Rect{macro.width - 2 * half, pos - half, macro.width, pos + half};
        macro.pins.at(names[i]).shapes.push_back({layer, rect});
      }
    };
    pins(address, 0);
    pins(data, 1);
    pins(control, 2);
    const auto master = std::format("__livehd_memory_{}_{}_{}", bits, depth, hash_util::fnv1a64(node.get_name()));
    tech.macros.emplace(master, std::move(macro));
    result.emplace(node.get_debug_nid(), master);
  }
  return result;
}

void align_memory_proxy_pins(physical::Technology& tech, const physical::Floorplan& floorplan) {
  for (auto& [name, macro] : tech.macros) {
    if (!macro.virtual_proxy) {
      continue;
    }
    // Each edge uses a preferred escape layer; varying pin coordinates must
    // share one phase so a rigid origin correction can serve every pin.
    std::map<std::pair<std::string, int>, std::vector<physical::Rect*>> edges;
    for (auto& [label, pin] : macro.pins) {
      (void)label;
      for (auto& shape : pin.shapes) {
        const auto& layer = tech.layers.at(shape.layer);
        const int   edge  = shape.rect.yl == 0 ? 1 : shape.rect.xl == 0 ? 0 : 2;
        if ((edge == 1 && layer.direction == "VERTICAL") || (edge != 1 && layer.direction == "HORIZONTAL")) {
          edges[{shape.layer, edge}].push_back(&shape.rect);
        }
      }
    }
    for (auto& [key, rectangles] : edges) {
      const bool                               x     = key.second == 1;
      const auto&                              layer = tech.layers.at(key.first);
      std::vector<std::pair<int64_t, int64_t>> patterns;
      for (const auto& track : floorplan.tracks) {
        if (track.layer == key.first && track.axis == (x ? "X" : "Y") && track.step > 0 && track.count > 0) {
          patterns.emplace_back(track.start, track.step);
        }
      }
      if (patterns.empty()) {
        const auto pitch = std::llround((x ? layer.pitch_x : layer.pitch_y) * tech.dbu);
        if (pitch > 0) {
          patterns.emplace_back(std::llround((x ? layer.offset_x : layer.offset_y) * tech.dbu), pitch);
        }
      }
      if (patterns.empty()) {
        continue;  // No known lattice; do not invent a technology pitch.
      }
      std::sort(rectangles.begin(), rectangles.end(), [&](const auto* a, const auto* b) {
        return x ? a->xl < b->xl : a->yl < b->yl;
      });
      const auto&       first = *rectangles.front();
      const double      half  = (x ? first.xh - first.xl : first.yh - first.yl) * tech.dbu / 2;
      const int64_t     low = std::ceil(half - 1e-7), high = std::floor(macro.width * tech.dbu - half + 1e-7);
      std::set<int64_t> candidates;
      const auto        add = [&](int64_t point) {
        if (point >= low && point <= high) {
          candidates.insert(point);
        }
      };
      for (const auto& [start, pitch] : patterns) {
        auto phase = start % pitch;
        if (phase < 0) {
          phase += pitch;
        }
        if (phase > high) {
          continue;
        }
        const auto steps = phase < low ? 1 + (low - phase - 1) / pitch : 0;
        if (steps > (high - phase) / pitch) {
          continue;
        }
        const auto first_track = phase + steps * pitch;
        // Bound storage by pin count, not macro size. Include enough early
        // tracks to fit every pin, plus neighbors of the desired even spacing.
        // All interleaved patterns participate, including ASAP7's M2 rows.
        auto       point       = first_track;
        for (size_t i = 0; i < rectangles.size(); ++i) {
          add(point);
          if (pitch > high - point) {
            break;
          }
          point += pitch;
        }
        for (size_t i = 0; i < rectangles.size(); ++i) {
          const auto wanted = std::llround(macro.width * tech.dbu * (i + 1) / (rectangles.size() + 1));
          if (wanted < phase) {
            add(phase);
            continue;
          }
          const auto before = phase + (wanted - phase) / pitch * pitch;
          add(before);
          if (pitch <= high - before) {
            add(before + pitch);
          }
        }
      }
      if (candidates.size() < rectangles.size()) {
        throw std::runtime_error("memory proxy edge has too few routing tracks for its pins: " + name);
      }
      const std::vector<int64_t> positions(candidates.begin(), candidates.end());
      size_t                     next = 0;
      for (size_t i = 0; i < rectangles.size(); ++i) {
        auto&        rect   = *rectangles[i];
        const double wanted = macro.width * tech.dbu * (i + 1) / (rectangles.size() + 1);
        const auto   last   = positions.size() - (rectangles.size() - i);
        auto         chosen = static_cast<size_t>(std::lower_bound(positions.begin(), positions.end(), wanted) - positions.begin());
        chosen              = std::clamp(chosen, next, last);
        if (chosen > next && std::abs(positions[chosen - 1] - wanted) < std::abs(positions[chosen] - wanted)) {
          --chosen;
        }
        next                = chosen + 1;
        const double center = double(positions[chosen]) / tech.dbu;
        const double radius = half / tech.dbu;
        if (x) {
          rect.xl = center - radius;
          rect.xh = center + radius;
        } else {
          rect.yl = center - radius;
          rect.yh = center + radius;
        }
      }
    }
  }
}
}  // namespace livehd::placement

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
    const auto&  layer = tech.routing_layers.front();
    const double half  = std::min(macro.width / 100, std::max(1.0 / tech.dbu, tech.layers.at(layer).width / 2));
    const auto   pins  = [&](const std::vector<std::string>& names, int edge) {
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
}  // namespace livehd::placement

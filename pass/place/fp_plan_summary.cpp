// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
// Run the same floorplanner as pass.place directly on a mapper's color graph.
#include <sys/resource.h>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <format>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>

#include "fp_chunks.hpp"

namespace lp = livehd::placement;
namespace {
const rapidjson::Value& field(const rapidjson::Value& r, const char* key) {
  if (!r.IsObject() || !r.HasMember(key)) {
    throw std::invalid_argument(std::string("missing color graph field: ") + key);
  }
  return r[key];
}
std::string str(const rapidjson::Value& r, const char* key) {
  const auto& v = field(r, key);
  if (!v.IsString()) {
    throw std::invalid_argument(key);
  }
  return {v.GetString(), v.GetStringLength()};
}
uint64_t count(const rapidjson::Value& r, const char* key) {
  const auto& v = field(r, key);
  if (!v.IsUint64()) {
    throw std::invalid_argument(key);
  }
  return v.GetUint64();
}
double number(const rapidjson::Value& r, const char* key) {
  const auto& v = field(r, key);
  if (!v.IsNumber() || !std::isfinite(v.GetDouble())) {
    throw std::invalid_argument(key);
  }
  return v.GetDouble();
}
lp::Chunk_box box(const rapidjson::Value& v) {
  lp::Chunk_box b;
  if (!v.IsArray() || v.Size() != 4) {
    throw std::invalid_argument("invalid rectangle");
  }
  for (int i = 0; i < 4; ++i) {
    if (!v[i].IsNumber() || !std::isfinite(v[i].GetDouble())) {
      throw std::invalid_argument("invalid rectangle coordinate");
    }
    b[i] = v[i].GetDouble();
  }
  if (!(b[2] > b[0] && b[3] > b[1])) {
    throw std::invalid_argument("empty rectangle");
  }
  return b;
}
uint64_t peak_bytes() {
#ifdef __linux__
  // ru_maxrss may include the Python benchmark driver's footprint before exec.
  // VmHWM belongs to this process image, including transient JSON allocations.
  std::ifstream status("/proc/self/status");
  for (std::string line; std::getline(status, line);) {
    if (line.starts_with("VmHWM:")) {
      return std::stoull(line.substr(6)) * 1024;
    }
  }
#endif
  rusage u{};
  if (getrusage(RUSAGE_SELF, &u)) {
    throw std::runtime_error("getrusage failed");
  }
#ifdef __APPLE__
  return u.ru_maxrss;
#else
  return uint64_t(u.ru_maxrss) * 1024;
#endif
}
}  // namespace
int main(int argc, char** argv) {
  if (argc != 5) {
    std::cerr << "usage: fp_plan_summary INPUT.json DESIGN DOWNSTREAM_GB OUTPUT.json\n";
    return 2;
  }
  try {
    const std::string_view arg = argv[3];
    double                 gb;
    const auto [end, ec] = std::from_chars(arg.data(), arg.data() + arg.size(), gb);
    if (ec != std::errc{} || end != arg.data() + arg.size() || !std::isfinite(gb) || gb < 0 || gb >= double(UINT64_MAX) / 1e9
        || (gb > 0 && gb < 1.e-9)) {
      throw std::invalid_argument("invalid downstream budget (decimal GB)");
    }
    const auto start   = std::chrono::steady_clock::now();
    const auto input   = livehd::metadata::Bundle::read(argv[1]);
    const auto records = input.select("livehd.placement.color_graph", argv[2]);
    if (records.size() != 1) {
      throw std::invalid_argument("expected one matching color graph");
    }
    const auto& graph = *records.front();
    if (count(graph, "schema_version") != 1) {
      throw std::invalid_argument("unsupported color graph version");
    }
    const auto core = box(field(graph, "core_dbu"));
    const auto site = number(graph, "site_dbu"), row = number(graph, "row_dbu"), density = number(graph, "density");
    if (site <= 0 || row <= 0 || density <= 0 || density > 1) {
      throw std::invalid_argument("invalid floorplan grid or density");
    }
    lp::Chunk_model                         model;
    std::map<std::string, int, std::less<>> ids;
    uint64_t                                cells = 0, macros = 0;
    const auto&                             objects = field(graph, "objects");
    if (graph.HasMember("repeated_scopes")) {
      if (!graph["repeated_scopes"].IsBool()) {
        throw std::invalid_argument("expected boolean repeated_scopes");
      }
      model.repeated_scopes = graph["repeated_scopes"].GetBool();
    }
    if (!objects.IsArray() || objects.Empty()) {
      throw std::invalid_argument("expected nonempty objects array");
    }
    for (const auto& o : objects.GetArray()) {
      if (!field(o, "macro").IsBool()) {
        throw std::invalid_argument("expected macro flag");
      }
      lp::Chunk_object obj{.id       = str(o, "id"),
                           .instance = str(o, "instance"),
                           .module   = str(o, "module"),
                           .local    = {},
                           .macro    = o["macro"].GetBool(),
                           .cells    = count(o, "cells"),
                           .pins     = count(o, "pins"),
                           .nets     = count(o, "nets"),
                           .area     = number(o, "area"),
                           .width    = number(o, "width"),
                           .height   = number(o, "height")};
      if (o.HasMember("fixed_dbu")) {
        obj.fixed = box(o["fixed_dbu"]);
      }
      if (obj.macro) {
        ++macros;
      } else {
        cells += obj.cells;
      }
      auto id = obj.id;
      if (!ids.emplace(id, model.add_summary(std::move(obj))).second) {
        throw std::invalid_argument("duplicate object: " + id);
      }
    }
    const auto& nets = field(graph, "nets");
    if (!nets.IsArray()) {
      throw std::invalid_argument("expected nets array");
    }
    uint64_t edges = 0;
    for (const auto& net : nets.GetArray()) {
      if (!net.IsArray() || net.Size() < 2) {
        throw std::invalid_argument("net needs at least two pins");
      }
      std::vector<int> nodes;
      for (const auto& pin : net.GetArray()) {
        if (!pin.IsString()) {
          throw std::invalid_argument("invalid net pin");
        }
        auto it = ids.find(std::string_view(pin.GetString(), pin.GetStringLength()));
        if (it == ids.end()) {
          throw std::invalid_argument(std::string("unknown net object: ") + pin.GetString());
        }
        nodes.push_back(it->second);
      }
      model.net(nodes, true, {}, std::format("net{}", edges++));
    }
    // A block-level terminal is a real boundary position, not another modeled
    // cell or an extra chunk. It guides the home chunk's crossing contact.
    if (graph.HasMember("terminals")) {
      const auto& terminals = graph["terminals"];
      if (!terminals.IsArray()) {
        throw std::invalid_argument("expected terminals array");
      }
      std::set<std::string> names;
      for (const auto& t : terminals.GetArray()) {
        const auto  name  = str(t, "name");
        const auto  owner = str(t, "object");
        const auto& xy    = field(t, "xy_dbu");
        if (name.empty() || !names.insert(name).second || !xy.IsArray() || xy.Size() != 2 || !xy[0].IsNumber()
            || !xy[1].IsNumber()) {
          throw std::invalid_argument("invalid block terminal");
        }
        if (name.starts_with("net")) {
          const std::string_view suffix = std::string_view(name).substr(3);
          uint64_t               id     = 0;
          const auto [suffix_end, err]         = std::from_chars(suffix.data(), suffix.data() + suffix.size(), id);
          if (err == std::errc{} && suffix_end == suffix.data() + suffix.size() && id < edges && name == std::format("net{}", id)) {
            throw std::invalid_argument("terminal collides with a crossing net: " + name);
          }
        }
        const auto it = ids.find(owner);
        if (it == ids.end()) {
          throw std::invalid_argument("unknown terminal object: " + owner);
        }
        const double x = xy[0].GetDouble(), y = xy[1].GetDouble();
        if (!std::isfinite(x) || !std::isfinite(y) || x < core[0] || x > core[2] || y < core[1] || y > core[3]
            || (x != core[0] && x != core[2] && y != core[1] && y != core[3])) {
          throw std::invalid_argument("terminal is not on the block edge: " + name);
        }
        std::vector<int> nodes{it->second};
        model.net(nodes, true, std::array{x, y}, name);
      }
    }
    const auto ready     = std::chrono::steady_clock::now();
    const auto predictor = graph.HasMember("memory_context") ? lp::load_chunk_predictor(str(graph, "memory_context"), "", &input)
                                                             : lp::Chunk_predictor::fit("color_graph:cold", {});
    const auto plan      = model.plan(core, site, row, uint64_t(gb * 1e9), density, &input, argv[2], predictor);
    const auto done      = std::chrono::steady_clock::now();
    const auto rss       = peak_bytes();
    uint64_t   largest   = 0;
    std::set<std::string> partitions;
    for (const auto& c : plan.chunks) {
      largest = std::max(largest, c.bytes);
      partitions.insert(c.partition);
    }
    livehd::metadata::Bundle output;
    output.replace(model.record(plan, argv[2]));
    output.replace_selected(lp::chunk_guidance_class, argv[2], input);
    output.replace_selected("livehd.synth.chunk_source", argv[2], input);
    output.write(argv[4]);
    using ms = std::chrono::milliseconds;
    std::cout << std::format(
        "{{\"budget_gb\":{},\"modeled_logic_cells\":{},\"colors\":{},\"macros\":{},\"cross_color_nets\":{},\"chunks\":{},"
        "\"partition_templates\":{},\"max_predicted_chunk_bytes\":{},\"planner_process_peak_rss_bytes\":{},\"including_export_peak_"
        "rss_bytes\":{},\"load_ms\":{},\"plan_ms\":{}}}\n",
        gb,
        cells,
        ids.size() - macros,
        macros,
        edges,
        plan.chunks.size(),
        partitions.size(),
        largest,
        rss,
        peak_bytes(),
        std::chrono::duration_cast<ms>(ready - start).count(),
        std::chrono::duration_cast<ms>(done - ready).count());
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}

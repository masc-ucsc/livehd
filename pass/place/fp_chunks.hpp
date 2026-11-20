// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "chunk_memory.hpp"
#include "core/metadata.hpp"

namespace livehd::placement {
using Chunk_box = std::array<double, 4>;  // xl, yl, xh, yh in DBU
struct Chunk_object {
  std::string              id, instance, module, local;
  bool                     macro = false;
  size_t                   cells = 0, pins = 0, nets = 0;
  double                   area = 0, width = 0, height = 0;
  std::optional<Chunk_box> fixed{};
  std::array<double, 2>    worker_halo{};  // per-side clearance for an independently placed macro
  std::array<double, 2>    route_blockage{};       // fraction of preferred tracks blocked by this master
  bool                     worker_memory = false;  // Release the parent memory pose inside its assigned home.
  Chunk_box macro_footprint(const Chunk_box& slot) const {
    const double x = std::round(slot[0] + .5 * std::max(0., slot[2] - slot[0] - width));
    const double y = std::round(slot[1] + .5 * std::max(0., slot[3] - slot[1] - height));
    return fixed.value_or(Chunk_box{x, y, x + width, y + height});
  }
};
struct Chunk_port {
  std::string           net;
  std::array<double, 2> xy;
  size_t                net_index = 0;
  std::string           role{};  // Stable local interface only when identifiable across instances.
  size_t                copy_source_net = SIZE_MAX;
};
struct Interface_copy {
  std::string source, requested, orientation, matching;
  size_t      matched = 0, source_ports = 0, target_ports = 0;
};
using Contact_taps = std::vector<std::pair<size_t, int>>;  // physical net ordinal, home
struct Fp_chunk {
  std::string                   id, partition, target_source;
  std::string                   implementation, implementation_mode;
  std::optional<Interface_copy> interface_copy;
  double                        target_aspect = 0;
  std::vector<int>              objects;
  std::vector<Chunk_port>       ports;
  Chunk_box                     box{};
  // Disjoint site-aligned slabs cover the stepped home. The original box remains its replay scaffold.
  std::vector<Chunk_box>        region, exclusions;
  std::vector<Chunk_box>        perimeter;  // axis, constant coordinate, low, high
  std::array<double, 2>         project_port(std::array<double, 2> point) const;
  Chunk_box                     worker_box() const {
    auto b = box;
    for (const auto& r : region) {
      for (int k = 0; k < 2; ++k) {
        b[k]     = std::min(b[k], r[k]);
        b[k + 2] = std::max(b[k + 2], r[k + 2]);
      }
    }
    return b;
  }
  uint64_t                      bytes = 0, allowance = 0;
  double                        density = 0, reserved = 0;
  std::optional<Chunk_box>      reference_box;  // Disjoint pre-edit layout for replay.
  bool                          aspect_locked = false;
  double                        shared_dbu2 = 0, incident_hpwl = 0, longest_span = 0;
};
struct Chunk_overlap {
  int       a, b;
  Chunk_box box;
};
struct Chunk_route_grid {
  static constexpr int                           bins = 16;
  Chunk_box                                      frame{};
  std::array<std::array<double, 2>, bins * bins> load{};
  double                                         peak_h = 0, peak_v = 0;
};
struct Chunk_plan {
  std::vector<std::pair<double,std::vector<Chunk_box>>> shape_candidates;
  Chunk_route_grid            route;
  std::vector<Chunk_overlap>  overlaps;
  unsigned                    overlap_cycles = 0;
  std::map<size_t, Chunk_box> fixed_contacts;
  size_t                      crossing_nets = 0;
  double                      center_hpwl = 0, port_hpwl = 0;  // Includes external block terminals.
  unsigned                    pin_sweeps     = 0;
  bool                        pins_converged = false;
  Chunk_predictor             predictor;
  Memory_counts               counts{};
  std::vector<Fp_chunk>       chunks;
  bool                        macro_islands = false;
  std::vector<Chunk_box>      seeds;  // color working areas and independent macro slots
  std::vector<double>         density;
  std::vector<std::string>    density_source;
  std::vector<int>            home;
  uint64_t                    budget = 0, input_key = 0;
};
class Chunk_model {
  std::map<std::pair<bool, std::string>, int> index;
  struct Edge {
    std::vector<int>                     nodes;
    std::optional<std::array<double, 2>> terminal;
    std::string                          name;
    size_t                               net_index;
    std::optional<Chunk_box>             terminal_bounds;
  };
  std::vector<Edge>         edges;
  size_t                    net_count = 0;
  void order_connected(std::vector<int>& ids, uint64_t capacity, const Chunk_predictor& predictor) const;
  std::optional<Chunk_plan> plan_with(Chunk_box core, double site, double row, uint64_t budget, double density,
                                      const metadata::Bundle* input, const std::string& design, const Chunk_predictor& predictor,
                                      bool only_saved) const;

public:
  std::map<std::vector<int>, unsigned> shared_nets() const;
  int                                  target_homes = 0;
  unsigned                             shape_rank = 0;
  double                               area_weight = 0;
  struct Routing_pattern { std::string layer; int axis; double start,step; int64_t count; };
  std::vector<Routing_pattern>                  routing_patterns;
  bool                                          spread     = false;
  double                                        dbu_per_um = 0;
  std::map<size_t, std::pair<std::string, int>> bus_bits;
  double                                        track_pitch = 0;
  std::array<double, 2>                         tracks_per_dbu{};
  bool pack_scopes  = false;  // group whole colors and macros across instance boundaries up to the worker budget
  bool connectivity = true;
  std::vector<Chunk_object>                                                       objects;
  // Only mapped floorplans can identify the full set of physical pins on a cut.
  // Materialize keys on demand, never while planning a color-only design.
  std::function<std::vector<std::string>(const Chunk_plan&, const Contact_taps&)> contact_keys;
  // Every participating physical cell is counted once. A macro is its own object.
  int        cell(std::string id, std::string instance, std::string module, bool macro, double w, double h,
                  std::optional<Chunk_box> fixed = {});
  // Direct mapper/benchmark input: all internal cells, pin and net incidences of one color
  // are accounted for in one record. Add its cross-color edges separately with net().
  int        add_summary(Chunk_object object);
  void       add_member(int object, double w, double h, std::optional<Chunk_box> fixed);
  void       net(std::vector<int>& pins, bool place, std::optional<std::array<double, 2>> terminal = {}, std::string_view name = {},
                 std::optional<Chunk_box> terminal_bounds = {});
  Chunk_plan plan(Chunk_box core, double site, double row, uint64_t budget, double density, const metadata::Bundle* guidance,
                  const std::string& design, const Chunk_predictor& predictor = {}) const;
  rapidjson::Document record(const Chunk_plan&, const std::string& design) const;
};
inline constexpr char chunk_guidance_class[] = "livehd.placement.chunk_guidance";
inline constexpr char chunk_plan_class[]     = "livehd.placement.chunk_plan";
}  // namespace livehd::placement

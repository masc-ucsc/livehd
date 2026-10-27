// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <unordered_map>

#include "interval_occupancy.hpp"
#include "physical.hpp"

namespace livehd::physical {
struct Occupied_shape {
  Shape       shape;
  // Empty means an unowned blockage, which must never be skipped as same-net.
  std::string net;
  // True only for decoded route/PG metal, never a pin or obstruction. Needed
  // to distinguish an existing pin connection from the pin's own rectangle.
  bool        routed = false;
};
// Spatial acceleration only: stored geometry and overlap queries retain exact
// DEF DBU resolution. Callers expand queries by their rule-derived clearance,
// then apply the metal/cut oracle to returned shapes. No DRC verdict here.
class Routing_occupancy {
  struct Key {
    int64_t x, y;
    bool    operator==(const Key&) const = default;
  };
  struct Hash {
    size_t operator()(const Key&) const;
  };
  // Conservative coverage only: a clear region proves there are no shapes;
  // marked regions still use exact ownership/geometry filtering.
  struct Layer_bitmap {
    int64_t                         grid, xl, yl;
    size_t                          nx, ny;
    bool                            vertical;
    // Each strip stores coordinates along the preferred routing direction.
    std::vector<Interval_occupancy> strips;
    Layer_bitmap(const Rect&, int64_t, bool);
    void   insert(const Rect&);
    bool   maybe_occupied(const Rect&) const;
    void   compact();
    size_t bytes() const;
  };
  struct Layer_index {
    std::optional<Layer_bitmap>                        bitmap;
    std::unordered_map<Key, std::vector<size_t>, Hash> buckets;
    // Long narrow rails use only their short-axis buckets. Broad blockages
    // remain in large, keeping insertion bounded independently of rail length.
    std::unordered_map<int64_t, std::vector<size_t>>   x_bands, y_bands;
    std::vector<size_t>                                large, all;
  };
  struct Bitmap_config {
    Rect                  bounds;
    int64_t               grid;
    std::set<std::string> vertical_layers;
  };
  std::optional<Bitmap_config>             bitmap_config_;
  int64_t                                  bucket_size_;
  std::shared_ptr<const Routing_occupancy> base_;
  size_t                                   base_size_ = 0;
  size_t local_size() const { return shapes_.empty() ? 0 : (shapes_.size() - 1) * shapes_per_chunk + shapes_.back().size(); }
  size_t logical_size_ = 0;
  struct Shape_run {
    Rect    first;
    int64_t dx, dy;
    size_t  count;
  };
  // Physical record IDs index bounding boxes; public IDs still enumerate every
  // individual rectangle, in insertion order. Empty starts keeps ordinary
  // occupancy on its original constant-time access path.
  std::vector<size_t>                   starts_;
  std::unordered_map<size_t, Shape_run> runs_;
  // Chunked storage avoids duplicating all expanded PG via shapes during growth.
  static constexpr size_t               shapes_per_chunk = 4096;
  // Pack route provenance in the owner ID; no per-shape storage growth.
  static constexpr uint32_t             routed_bit       = uint32_t{1} << 31;
  static constexpr uint32_t             name_mask        = routed_bit - 1;
  struct Stored_shape {
    int32_t  xl, yl, xh, yh;
    uint32_t layer, net;
  };
  std::vector<std::vector<Stored_shape>>       shapes_;
  std::unordered_map<std::string, uint32_t>    name_ids_;
  std::vector<std::string>                     names_;
  uint32_t                                     intern(const std::string&);
  std::unordered_map<std::string, Layer_index> layers_;

public:
  explicit Routing_occupancy(int64_t bucket_size_dbu);
  Routing_occupancy(const Routing_occupancy&)            = default;
  Routing_occupancy& operator=(const Routing_occupancy&) = default;
  Routing_occupancy(Routing_occupancy&&) noexcept;
  Routing_occupancy& operator=(Routing_occupancy&&) noexcept;
  // Append-only overlay. The shared base must remain immutable for its lifetime.
  // Copies share only that base; local insertions never affect sibling trials.
  explicit Routing_occupancy(std::shared_ptr<const Routing_occupancy> base);
  // Runtime-only interval broad phase, retained until this owner is destroyed.
  // Insertions (including new layers) update it; no ownership/DRC verdict is
  // cached. Imported geometry is compacted once, never serialized.
  // Geometry outside supported bounds retains the exact index.
  void                enable_bitmap(const Rect& bounds, int64_t grid_dbu, const std::set<std::string>& vertical_layers = {});
  // Choose a conservative grid from DEF tracks; no benchmark-specific tuning.
  void                enable_bitmap(const Technology&, const Floorplan&);
  void                disable_bitmap();  // Exact reference for measurements.
  // Interval/word capacity plus strip objects, excluding allocator overhead.
  size_t              bitmap_bytes() const;
  size_t              insert(Occupied_shape);
  // Exact arithmetic progression of identical rectangles, including reversed
  // or diagonal progressions. Queries return individual cut IDs, never a hull.
  size_t              insert_run(Occupied_shape first, int64_t dx, int64_t dy, size_t count);
  // Validate a complete proposal before changing occupancy. The caller should
  // submit only fully accepted nets; failed search trials do not mutate this.
  void                insert_many(const std::vector<Occupied_shape>&);
  std::vector<size_t> query(const std::string& layer, const Rect&, int64_t clearance_dbu, const std::string& ignore_net = {}) const;
  // Reconstruct owning strings at the API boundary; stored coordinates remain exact DBU.
  Occupied_shape      at(size_t id) const;
  size_t              size() const { return base_size_ + logical_size_; }
};
// Same special-net wiring, ignoring connections: new cells may join existing rails.
bool same_supply_wiring(const std::vector<Net>&, const std::vector<Net>&);
// Immutable decoded PG geometry for trials that change cells and connectivity,
// but preserve the supply wiring. Technology must outlive this object and remain
// unchanged. Reuse checks DBU, via definitions, NDRs and actual supply paths;
// supply connections may change because new repeater pins join existing rails.
class Routing_power_grid {
  const Technology*                        technology_;
  Floorplan                                signature_;
  struct Bitmap_config {
    Rect bounds;
    int64_t grid;
    std::set<std::string> vertical_layers;
  };
  std::optional<Bitmap_config> bitmap_config_;
  int64_t                                  bucket_size_;
  std::shared_ptr<const Routing_occupancy> occupancy_;

public:
  Routing_power_grid(const Technology&, const Floorplan&, int64_t bucket_size_dbu, const std::function<void()>& checkpoint = {});
  Routing_occupancy overlay(const Technology&, const Floorplan&, int64_t bucket_size_dbu) const;
};
enum class Signal_wires { include, exclude };
// Seed real pins, LEF obstructions, terminals and all signal/PG wire and via
// geometry. Unsupported pin polygons and virtual proxies become conservative
// body blockages rather than disappearing from routing occupancy.
// Optional checkpoint may throw to cancel construction; no partial index escapes.
// Excluding signal wires retains pin ownership and all PG geometry. This builds
// static capacity obstacles without copying and clearing the input floorplan.
// A nonempty layer selection indexes only those layers (including their via
// landings and conservative obstructions). It cannot reuse an unfiltered PG index.
Routing_occupancy routing_occupancy(const Technology&, const Floorplan&, int64_t bucket_size_dbu,
                                    const std::function<void()>& checkpoint = {}, const Routing_power_grid* power_grid = nullptr,
                                    Signal_wires                 signal_wires    = Signal_wires::include,
                                    const std::set<std::string>& selected_layers = {});
}  // namespace livehd::physical

// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "routing_occupancy.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

#include "core/hash_util.hpp"
#include "routing_geometry.hpp"

namespace livehd::physical {
namespace {
void validate(const Rect& r) {
  if (!integral_dbu(r)) {
    throw std::invalid_argument("routing occupancy requires integer DEF coordinates");
  }
  if (r.empty()) {
    throw std::invalid_argument("empty routing obstacle");
  }
}
struct Bins {
  int64_t xl, yl, xh, yh;
};
Bins bins(const Rect& r, int64_t size) {
  return {int64_t(std::floor(r.xl / size)),
          int64_t(std::floor(r.yl / size)),
          int64_t(std::floor(r.xh / size)),
          int64_t(std::floor(r.yh / size))};
}
bool oversized(const Bins& b, int64_t maximum) {
  const auto nx = b.xh - b.xl + 1, ny = b.yh - b.yl + 1;
  return nx > maximum || ny > maximum || nx > maximum / ny;
}
}  // namespace
namespace {
Rect     bitmap_rect(const Rect& r, bool vertical) { return vertical ? Rect{r.yl, r.xl, r.yh, r.xh} : r; }
uint64_t bit_range(size_t first, size_t last) { return (~uint64_t{0} << first) & (~uint64_t{0} >> (63 - last)); }
}  // namespace
Routing_occupancy::Layer_bitmap::Layer_bitmap(const Rect& bounds, int64_t pitch, bool transpose)
    : grid(pitch), vertical(transpose) {
  const auto b               = bins(bitmap_rect(bounds, vertical), grid);
  xl                         = b.xl;
  yl                         = b.yl;
  nx                         = b.xh - b.xl + 1;
  ny                         = b.yh - b.yl + 1;
  stride                     = (nx + 63) / 64;
  // Bound each optional layer to 32 MiB. Empty storage means exact fallback.
  constexpr size_t max_words = 32 * 1024 * 1024 / sizeof(uint64_t);
  if (stride <= max_words / ny) {
    words.resize(stride * ny);
  }
}
void Routing_occupancy::Layer_bitmap::insert(const Rect& rect) {
  if (words.empty()) {
    return;
  }
  const auto b  = bins(bitmap_rect(rect, vertical), grid);
  const auto x0 = std::max(b.xl, xl), x1 = std::min(b.xh, xl + int64_t(nx) - 1);
  const auto y0 = std::max(b.yl, yl), y1 = std::min(b.yh, yl + int64_t(ny) - 1);
  if (x1 < x0 || y1 < y0) {
    return;
  }
  const size_t first = x0 - xl, last = x1 - xl;
  for (auto y = y0; y <= y1; ++y) {
    const auto offset = size_t(y - yl) * stride;
    for (size_t w = first / 64; w <= last / 64; ++w) {
      words[offset + w] |= bit_range(w == first / 64 ? first % 64 : 0, w == last / 64 ? last % 64 : 63);
    }
  }
}
bool Routing_occupancy::Layer_bitmap::maybe_occupied(const Rect& rect) const {
  if (words.empty()) {
    return true;
  }
  const auto b = bins(bitmap_rect(rect, vertical), grid);
  if (b.xl < xl || b.yl < yl || b.xh >= xl + int64_t(nx) || b.yh >= yl + int64_t(ny)) {
    return true;  // Geometry outside the bitmap is still represented by the exact index.
  }
  const size_t first = b.xl - xl, last = b.xh - xl;
  for (auto y = b.yl; y <= b.yh; ++y) {
    const auto offset = size_t(y - yl) * stride;
    for (size_t w = first / 64; w <= last / 64; ++w) {
      if (words[offset + w] & bit_range(w == first / 64 ? first % 64 : 0, w == last / 64 ? last % 64 : 63)) {
        return true;
      }
    }
  }
  return false;
}
void Routing_occupancy::enable_bitmap(const Rect& bounds, int64_t grid, const std::set<std::string>& vertical_layers) {
  validate(bounds);
  if (grid <= 0 || grid > std::numeric_limits<int32_t>::max()) {
    throw std::invalid_argument("invalid routing bitmap grid");
  }
  for (auto& [name, layer] : layers_) {
    layer.bitmap.emplace(bounds, grid, vertical_layers.contains(name));
    for (auto id : layer.all) {
      const auto& s = shapes_[id / shapes_per_chunk][id % shapes_per_chunk];
      layer.bitmap->insert({double(s.xl), double(s.yl), double(s.xh), double(s.yh)});
    }
  }
}
size_t Routing_occupancy::bitmap_bytes() const {
  size_t total = 0;
  for (const auto& [name, layer] : layers_) {
    if (layer.bitmap) {
      total += layer.bitmap->words.size() * sizeof(uint64_t);
    }
  }
  return total;
}
size_t Routing_occupancy::Hash::operator()(const Key& key) const {
  // Grid coordinates are highly correlated. Mix the first coordinate before
  // combining: identity integer hashes otherwise collapse a dense 2-D grid
  // into a narrow range and create long bucket chains. Keep adjacent y bins
  // adjacent in the hash to retain locality during the inner insertion loop.
  return hash_util::mix64(static_cast<uint64_t>(key.x)) + static_cast<uint64_t>(key.y);
}
Routing_occupancy::Routing_occupancy(int64_t size) : bucket_size_(size) {
  if (size <= 0 || size > std::numeric_limits<int32_t>::max()) {
    throw std::invalid_argument("invalid occupancy bucket size");
  }
}
Routing_occupancy::Routing_occupancy(std::shared_ptr<const Routing_occupancy> base)
    : bucket_size_(base ? base->bucket_size_ : 1), base_(std::move(base)), base_size_(base_ ? base_->size() : 0) {
  if (!base_) {
    throw std::invalid_argument("routing occupancy overlay requires an immutable base");
  }
}
Routing_occupancy::Routing_occupancy(Routing_occupancy&& other) noexcept : bucket_size_(other.bucket_size_) {
  *this = std::move(other);
}
Routing_occupancy& Routing_occupancy::operator=(Routing_occupancy&& other) noexcept {
  if (this == &other) {
    return *this;
  }
  bucket_size_  = other.bucket_size_;
  base_         = std::move(other.base_);
  base_size_    = std::exchange(other.base_size_, 0);
  logical_size_ = std::exchange(other.logical_size_, 0);
  starts_       = std::move(other.starts_);
  runs_         = std::move(other.runs_);
  shapes_       = std::move(other.shapes_);
  name_ids_     = std::move(other.name_ids_);
  names_        = std::move(other.names_);
  layers_       = std::move(other.layers_);
  other.starts_.clear();
  other.runs_.clear();
  other.shapes_.clear();
  other.name_ids_.clear();
  other.names_.clear();
  other.layers_.clear();
  return *this;
}
uint32_t Routing_occupancy::intern(const std::string& name) {
  const auto found = name_ids_.find(name);
  if (found != name_ids_.end()) {
    return found->second;
  }
  if (names_.size() >= routed_bit) {
    throw std::length_error("too many routing occupancy names");
  }
  const auto id = static_cast<uint32_t>(names_.size());
  names_.push_back(name);
  name_ids_.emplace(name, id);
  return id;
}
Occupied_shape Routing_occupancy::at(size_t id) const {
  if (id < base_size_) {
    return base_->at(id);
  }
  id -= base_size_;
  if (id >= logical_size_) {
    throw std::out_of_range("routing occupancy shape ID");
  }
  const auto  physical = starts_.empty() ? id : size_t(std::upper_bound(starts_.begin(), starts_.end(), id) - starts_.begin() - 1);
  const auto& s        = shapes_.at(physical / shapes_per_chunk).at(physical % shapes_per_chunk);
  Rect        rect{double(s.xl), double(s.yl), double(s.xh), double(s.yh)};
  if (const auto run = runs_.find(physical); run != runs_.end()) {
    const auto  offset = id - starts_[physical];
    const auto& r      = run->second;
    rect               = {r.first.xl + double(offset) * r.dx,
                          r.first.yl + double(offset) * r.dy,
                          r.first.xh + double(offset) * r.dx,
                          r.first.yh + double(offset) * r.dy};
  }
  return {
      {names_[s.layer], rect},
      names_[s.net & name_mask],
      bool(s.net & routed_bit)
  };
}
size_t Routing_occupancy::insert(Occupied_shape shape) {
  validate(shape.shape.rect);
  if (shape.shape.layer.empty()) {
    throw std::invalid_argument("routing obstacle has no layer");
  }
  if (logical_size_ == std::numeric_limits<size_t>::max() - base_size_) {
    throw std::length_error("too many routing shapes");
  }
  const auto b     = bins(shape.shape.rect, bucket_size_);
  const auto id    = local_size();
  auto&      layer = layers_[shape.shape.layer];
  if (shapes_.empty() || shapes_.back().size() == shapes_per_chunk) {
    shapes_.emplace_back();
    shapes_.back().reserve(shapes_per_chunk);
  }
  const auto&        r = shape.shape.rect;
  const Stored_shape stored{static_cast<int32_t>(r.xl),
                            static_cast<int32_t>(r.yl),
                            static_cast<int32_t>(r.xh),
                            static_cast<int32_t>(r.yh),
                            intern(shape.shape.layer),
                            intern(shape.net) | (shape.routed ? routed_bit : 0)};
  if (!starts_.empty()) {
    starts_.push_back(logical_size_);
  }
  shapes_.back().push_back(stored);
  layer.all.push_back(id);
  if (layer.bitmap) {
    layer.bitmap->insert(r);
  }
  // A macro or long rail must not allocate one entry for every small bucket.
  if (oversized(b, 256)) {
    const auto nx = b.xh - b.xl + 1, ny = b.yh - b.yl + 1;
    if (std::min(nx, ny) <= 256) {
      auto&      bands = nx <= ny ? layer.x_bands : layer.y_bands;
      const auto lo = nx <= ny ? b.xl : b.yl, hi = nx <= ny ? b.xh : b.yh;
      for (auto coordinate = lo; coordinate <= hi; ++coordinate) {
        bands[coordinate].push_back(id);
      }
    } else {
      layer.large.push_back(id);
    }
  } else {
    for (auto x = b.xl; x <= b.xh; ++x) {
      for (auto y = b.yl; y <= b.yh; ++y) {
        layer.buckets[{x, y}].push_back(id);
      }
    }
  }
  return base_size_ + logical_size_++;
}
size_t Routing_occupancy::insert_run(Occupied_shape first, int64_t dx, int64_t dy, size_t count) {
  validate(first.shape.rect);
  if (first.shape.layer.empty() || count == 0) {
    throw std::invalid_argument("invalid routing shape run");
  }
  if (count > std::numeric_limits<size_t>::max() - base_size_ - logical_size_) {
    throw std::length_error("too many routing shapes");
  }
  if (count == 1) {
    return insert(std::move(first));
  }
  // Coordinate limits make a nonzero progression's valid length small enough
  // for exact integer arithmetic. Reject overflow before multiplying.
  constexpr int64_t extent = 2LL * std::numeric_limits<int32_t>::max();
  for (const auto step : {dx, dy}) {
    if (step < -extent || step > extent || (step != 0 && count - 1 > size_t(extent / std::abs(step)))) {
      throw std::invalid_argument("routing shape run coordinate overflow");
    }
  }
  const auto tx = dx == 0 ? 0 : dx * int64_t(count - 1);
  const auto ty = dy == 0 ? 0 : dy * int64_t(count - 1);
  const auto r    = first.shape.rect;
  const auto last = r.translated(tx, ty);
  validate(last);
  first.shape.rect = r.hull(last);
  if (starts_.empty()) {
    starts_.reserve(local_size() + 1);
    for (size_t i = 0; i < local_size(); ++i) {
      starts_.push_back(i);
    }
  }
  const auto physical = local_size();
  // An array as the very first record needs an initial starts entry too.
  const auto id       = insert(std::move(first));
  if (starts_.empty()) {
    starts_.push_back(0);
  }
  runs_.emplace(physical, Shape_run{r, dx, dy, count});
  logical_size_ += count - 1;
  return id;
}
void Routing_occupancy::insert_many(const std::vector<Occupied_shape>& shapes) {
  if (shapes.size() > std::numeric_limits<size_t>::max() - size()) {
    throw std::length_error("too many routing shapes");
  }
  for (const auto& s : shapes) {
    validate(s.shape.rect);
    if (s.shape.layer.empty()) {
      throw std::invalid_argument("routing obstacle has no layer");
    }
  }
  for (const auto& s : shapes) {
    insert(s);
  }
}
std::vector<size_t> Routing_occupancy::query(const std::string& name, const Rect& rect, int64_t clearance,
                                             const std::string& ignore_net) const {
  validate(rect);
  if (clearance < 0 || clearance > std::numeric_limits<int32_t>::max()) {
    throw std::invalid_argument("invalid occupancy clearance");
  }
  const auto it = layers_.find(name);
  if (it == layers_.end()) {
    return base_ ? base_->query(name, rect, clearance, ignore_net) : std::vector<size_t>{};
  }
  const Rect  expanded{rect.xl - clearance, rect.yl - clearance, rect.xh + clearance, rect.yh + clearance};
  const auto  b     = bins(expanded, bucket_size_);
  const auto& layer = it->second;
  if (layer.bitmap && !layer.bitmap->maybe_occupied(expanded)) {
    return base_ ? base_->query(name, rect, clearance, ignore_net) : std::vector<size_t>{};
  }
  std::vector<size_t> candidates;
  if (oversized(b, 4096)) {
    candidates = layer.all;
  } else {
    candidates              = layer.large;
    const auto append_bands = [&](const auto& bands, int64_t lo, int64_t hi) {
      if (bands.empty()) {
        return;
      }
      for (auto coordinate = lo; coordinate <= hi; ++coordinate) {
        const auto found = bands.find(coordinate);
        if (found != bands.end()) {
          candidates.insert(candidates.end(), found->second.begin(), found->second.end());
        }
      }
    };
    append_bands(layer.x_bands, b.xl, b.xh);
    append_bands(layer.y_bands, b.yl, b.yh);
    for (auto x = b.xl; x <= b.xh; ++x) {
      for (auto y = b.yl; y <= b.yh; ++y) {
        const auto found = layer.buckets.find({x, y});
        if (found != layer.buckets.end()) {
          candidates.insert(candidates.end(), found->second.begin(), found->second.end());
        }
      }
    }
  }
  // Long rails are shared by all queries on a layer. Discard distant and
  // same-net shapes before sorting, so local pin access does not sort the
  // entire power grid. Filtering depends only on ID, so duplicates agree.
  const auto ignored    = name_ids_.find(ignore_net);
  const bool skip_owner = !ignore_net.empty() && ignored != name_ids_.end();
  std::erase_if(candidates, [&](size_t id) {
    const auto& s = shapes_[id / shapes_per_chunk][id % shapes_per_chunk];
    return (skip_owner && (s.net & name_mask) == ignored->second)
           || !(s.xl <= expanded.xh && s.xh >= expanded.xl && s.yl <= expanded.yh && s.yh >= expanded.yl);
  });
  std::sort(candidates.begin(), candidates.end());
  candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end());
  if (!starts_.empty()) {
    std::vector<size_t> expanded_ids;
    for (const auto physical : candidates) {
      const auto found = runs_.find(physical);
      if (found == runs_.end()) {
        expanded_ids.push_back(starts_[physical]);
        continue;
      }
      const auto& run = found->second;
      size_t      lo = 0, hi = run.count - 1;
      const auto  restrict_axis = [&](double first_lo, double first_hi, int64_t step, double query_lo, double query_hi) {
        if (step == 0) {
          return first_lo <= query_hi && first_hi >= query_lo;
        }
        double lower = (query_lo - first_hi) / double(step), upper = (query_hi - first_lo) / double(step);
        if (step < 0) {
          std::swap(lower, upper);
        }
        lower = std::ceil(lower);
        upper = std::floor(upper);
        if (upper < 0 || lower > double(hi)) {
          return false;
        }
        lo = std::max(lo, size_t(std::max(0.0, lower)));
        if (upper < double(hi)) {
          hi = size_t(upper);
        }
        return lo <= hi;
      };
      if (!restrict_axis(run.first.xl, run.first.xh, run.dx, expanded.xl, expanded.xh)
          || !restrict_axis(run.first.yl, run.first.yh, run.dy, expanded.yl, expanded.yh)) {
        continue;
      }
      for (size_t i = lo;; ++i) {
        expanded_ids.push_back(starts_[physical] + i);
        if (i == hi) {
          break;
        }
      }
    }
    candidates = std::move(expanded_ids);
  }
  if (base_) {
    for (auto& id : candidates) {
      id += base_size_;
    }
    auto inherited = base_->query(name, rect, clearance, ignore_net);
    inherited.insert(inherited.end(), candidates.begin(), candidates.end());
    return inherited;
  }
  return candidates;
}
bool same_supply_wiring(const std::vector<Net>& a, const std::vector<Net>& b) {
  return std::ranges::equal(a, b, [](const Net& x, const Net& y) {
    return x.name == y.name && x.use == y.use && x.wires == y.wires;
  });
}
Routing_power_grid::Routing_power_grid(const Technology& tech, const Floorplan& fp, int64_t size,
                                       const std::function<void()>& checkpoint)
    : technology_(&tech), bucket_size_(size) {
  if (fp.dbu <= 0 || fp.dbu > std::numeric_limits<int32_t>::max()) {
    throw std::invalid_argument("invalid power-grid occupancy DBU");
  }
  if (checkpoint) {
    checkpoint();
  }
  signature_.dbu              = fp.dbu;
  signature_.vias             = fp.vias;
  signature_.nondefault_rules = fp.nondefault_rules;
  Routing_occupancy occupied(size);
  size_t            visits = 0;
  for (const auto& net : fp.special_nets) {
    signature_.special_nets.push_back({net.name, {}, net.wires, net.use});
    Shape      first, previous;
    size_t     count = 0;
    int64_t    dx = 0, dy = 0;
    const auto flush = [&] {
      if (count >= 8) {
        occupied.insert_run({first, net.name, true}, dx, dy, count);
      } else {
        // Short progressions cost less as ordinary records and avoid indexing
        // a large empty hull between two unrelated same-sized rectangles.
        for (size_t i = 0; i < count; ++i) {
          occupied.insert({
              {first.layer, first.rect.translated(double(i) * dx, double(i) * dy)},
              net.name,
              true
          });
        }
      }
      count = 0;
    };
    visit_route_geometry(
        tech,
        fp,
        net,
        [&](const Shape& shape) {
          if (checkpoint && visits++ % 1024 == 0) {
            checkpoint();
          }
          // The route decoder validates path layers and every cached via layer.
          // Rechecking both technology maps for each expanded PG cut is redundant.
          const auto& r       = shape.rect;
          const auto& p       = previous.rect;
          const bool  same    = count && shape.layer == previous.layer && r.xh - r.xl == p.xh - p.xl && r.yh - r.yl == p.yh - p.yl;
          const auto  next_dx = same ? int64_t(r.xl - p.xl) : 0;
          const auto  next_dy = same ? int64_t(r.yl - p.yl) : 0;
          if (same && (count == 1 || (next_dx == dx && next_dy == dy))) {
            dx = next_dx;
            dy = next_dy;
            ++count;
          } else {
            flush();
            first = shape;
            count = 1;
            dx    = 0;
            dy    = 0;
          }
          previous = shape;
        },
        true);
    flush();
  }
  if (checkpoint) {
    checkpoint();
  }
  occupancy_ = std::make_shared<const Routing_occupancy>(std::move(occupied));
}
Routing_occupancy Routing_power_grid::overlay(const Technology& tech, const Floorplan& fp, int64_t size) const {
  if (&tech != technology_ || fp.dbu != signature_.dbu || size != bucket_size_ || fp.vias != signature_.vias
      || fp.nondefault_rules != signature_.nondefault_rules || fp.special_nets.size() != signature_.special_nets.size()) {
    throw std::invalid_argument("power-grid occupancy snapshot mismatch");
  }
  if (!same_supply_wiring(fp.special_nets, signature_.special_nets)) {
    throw std::invalid_argument("power-grid occupancy wiring changed");
  }
  return Routing_occupancy(occupancy_);
}
Routing_occupancy routing_occupancy(const Technology& tech, const Floorplan& fp, int64_t size,
                                    const std::function<void()>& checkpoint, const Routing_power_grid* power_grid,
                                    Signal_wires signal_wires, const std::set<std::string>& selected_layers) {
  if (power_grid && !selected_layers.empty()) {
    throw std::invalid_argument("layer-selected occupancy cannot reuse an unfiltered power grid");
  }
  size_t     visits = 0;
  const auto check  = [&] {
    if (checkpoint && (visits++ % 1024 == 0)) {
      checkpoint();
    }
  };
  check();
  if (fp.dbu <= 0 || fp.dbu > std::numeric_limits<int32_t>::max()) {
    throw std::invalid_argument("invalid occupancy DBU");
  }
  Routing_occupancy out = power_grid ? power_grid->overlay(tech, fp, size) : Routing_occupancy(size);
  std::map<std::pair<std::string, std::string>, std::string> owners;
  std::map<std::string, std::string>                         wildcard;
  const auto                                                 connect = [&](const Net& net) {
    for (const auto& c : net.connections) {
      check();
      auto& owner = c.instance == "*" ? wildcard[c.pin] : owners[{c.instance, c.pin}];
      if (!owner.empty() && owner != net.name) {
        throw std::invalid_argument("conflicting physical pin ownership");
      }
      owner = net.name;
    }
  };
  for (const auto& net : fp.nets) {
    connect(net);
  }
  for (const auto& net : fp.special_nets) {
    connect(net);
  }
  const auto append = [&](const Shape& s, const std::string& net, bool routed = false) {
    check();
    if (!selected_layers.empty() && !selected_layers.contains(s.layer)) {
      return;
    }
    if (tech.layers.contains(s.layer) || tech.cut_layers.contains(s.layer)) {
      out.insert({s, net, routed});
    }
  };
  for (const auto& cell : fp.components) {
    check();
    const auto& m = tech.macros.at(cell.macro);
    if (cell.place.status == Status::UNPLACED) {
      throw std::invalid_argument("unplaced cell in routing occupancy");
    }
    const auto transform = [&](const Shape& s) {
      const auto r = placed_rect(m, cell.place, fp.dbu, s.rect);
      // Occupancy rounds outwards, never shrinking a physical obstacle.
      return Shape{
          s.layer,
          {std::floor(r.xl + 1e-9), std::floor(r.yl + 1e-9), std::ceil(r.xh - 1e-9), std::ceil(r.yh - 1e-9)}
      };
    };
    const auto body = [&](const std::string& layer) {
      append(transform({
                 layer,
                 {-m.origin_x, -m.origin_y, m.width - m.origin_x, m.height - m.origin_y}
      }),
             {});
    };
    if (m.virtual_proxy) {
      for (const auto& [layer, rule] : tech.layers) {
        body(layer);
      }
      for (const auto& [layer, rule] : tech.cut_layers) {
        body(layer);
      }
      continue;
    }
    for (const auto& [pin_name, pin] : m.pins) {
      std::string net;
      const auto  specific = owners.find({cell.name, pin_name});
      if (specific != owners.end()) {
        net = specific->second;
      } else if (wildcard.contains(pin_name)) {
        net = wildcard.at(pin_name);
      }
      if (specific != owners.end() && wildcard.contains(pin_name) && wildcard.at(pin_name) != net) {
        throw std::invalid_argument("wildcard conflicts with physical pin ownership");
      }
      for (const auto& s : pin.shapes) {
        if (s.rect.empty()) {
          body(s.layer);
        } else {
          append(transform(s), net);
        }
      }
    }
    for (const auto& s : m.obstructions) {
      append(transform(s), {});
    }
    for (const auto& layer : m.conservative_obstructions) {
      body(layer);
    }
    for (const auto& layer : m.blocked_layers) {
      if (!m.conservative_obstructions.contains(layer)
          && std::none_of(m.obstructions.begin(), m.obstructions.end(), [&](const auto& s) { return s.layer == layer; })) {
        body(layer);
      }
    }
  }
  for (const auto& terminal : fp.terminals) {
    if (terminal.status == Status::UNPLACED && !terminal.shapes.empty()) {
      throw std::invalid_argument("unplaced terminal in routing occupancy");
    }
    for (const auto& s : terminal.shapes) {
      append({s.layer, orient_rect(s.rect, 0, 0, terminal.orientation).translated(terminal.x, terminal.y)}, terminal.net);
    }
  }
  if (signal_wires == Signal_wires::include) {
    for (const auto& net : fp.nets) {
      visit_route_geometry(tech, fp, net, [&](const Shape& shape) { append(shape, net.name, true); });
    }
  }
  if (!power_grid) {
    for (const auto& net : fp.special_nets) {
      visit_route_geometry(tech, fp, net, [&](const Shape& shape) { append(shape, net.name, true); }, true);
    }
  }
  if (checkpoint) {
    checkpoint();
  }
  return out;
}
}  // namespace livehd::physical

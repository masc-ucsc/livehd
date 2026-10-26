// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "power_grid.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <tuple>

#include "inou/lefdef/routing_rules.hpp"

namespace livehd::placement {
namespace {
namespace ph = physical;
int64_t integer(double value) {
  if (!std::isfinite(value) || std::abs(value) > std::numeric_limits<int32_t>::max()
      || std::abs(value - std::round(value)) > 1e-7) {
    throw std::invalid_argument("power grid requires exact integral DBU within the OpenDB coordinate range");
  }
  return std::llround(value);
}
int64_t dimension(double um, int64_t dbu) { return integer(um * dbu); }
void    require_rect(const ph::Rect& r) {
  integer(r.xl);
  integer(r.yl);
  integer(r.xh);
  integer(r.yh);
  if (r.xl >= r.xh || r.yl >= r.yh) {
    throw std::invalid_argument("empty power-grid rectangle");
  }
}
struct Rails {
  int64_t width  = 0;
  int     bottom = -1, top = -1;
};
// Read the physical lower/upper rails from ordinary standard-cell PG pins.
// A second followpin layer may have no PG pins (ASAP7 M2); it uses the same rail
// width. Conflicting masters are unsupported instead of arbitrarily choosing one.
Rails site_rails(const ph::Technology& tech, const Pg_profile& p, const std::string& site, int64_t dbu) {
  Rails result;
  for (const auto& [name, master] : tech.macros) {
    if (master.site != site || master.klass != "CORE" || master.virtual_proxy) {
      continue;
    }
    Rails found;
    for (const auto& [pin_name, pin] : master.pins) {
      for (int net = 0; net < 2; ++net) {
        if (std::find(p.pin_names[net].begin(), p.pin_names[net].end(), pin_name) == p.pin_names[net].end()) {
          continue;
        }
        if (!pin.use.empty() && pin.use != (net == 0 ? "POWER" : "GROUND")) {
          throw std::invalid_argument("power-grid profile disagrees with LEF pin use: " + name + "/" + pin_name);
        }
        for (const auto& shape : pin.shapes) {
          if (std::find(p.followpin_layers.begin(), p.followpin_layers.end(), shape.layer) == p.followpin_layers.end()) {
            continue;
          }
          const auto& r      = shape.rect;
          const auto  center = dimension((r.yl + r.yh) / 2 + master.origin_y, dbu);
          const auto  height = dimension(master.height, dbu);
          if (center != 0 && center != height) {
            continue;
          }
          const auto width = dimension(r.yh - r.yl, dbu);
          if (width <= 0 || width % 2 || (found.width != 0 && found.width != width)) {
            throw std::invalid_argument("unsupported standard-cell PG rail width: " + name);
          }
          found.width    = width;
          auto& polarity = center == 0 ? found.bottom : found.top;
          if (polarity != -1 && polarity != net) {
            throw std::invalid_argument("overlapping power/ground rails in LEF master: " + name);
          }
          polarity = net;
        }
      }
    }
    if (found.bottom < 0 || found.top < 0) {
      continue;
    }
    if (found.bottom == found.top
        || (result.width && (result.width != found.width || result.bottom != found.bottom || result.top != found.top))) {
      throw std::invalid_argument("incompatible standard-cell rail geometry for site: " + site);
    }
    result = found;
  }
  if (!result.width) {
    throw std::invalid_argument("no standard-cell PG edge rails for site: " + site);
  }
  return result;
}
}  // namespace

Power_grid_plan plan_power_grid(const ph::Technology& tech, const ph::Floorplan& fp, const Pg_profile& profile) {
  if (fp.dbu <= 0 || fp.rows.empty() || profile.followpin_layers.empty() || profile.nets[0].empty() || profile.nets[1].empty()
      || profile.nets[0] == profile.nets[1] || !profile.unsupported.empty()) {
    throw std::invalid_argument("invalid or unsupported native power-grid profile/frame");
  }
  require_rect(fp.die);
  Power_grid_plan plan;
  plan.core = {fp.die.xh, fp.die.yh, fp.die.xl, fp.die.yl};
  std::map<std::string, Rails> rails;
  // Group and merge coincident followpins (including adjacent row segments).
  using Key = std::tuple<std::string, int, int64_t, int64_t>;  // layer, net, y, width
  std::map<Key, std::vector<std::pair<int64_t, int64_t>>>     segments;
  int64_t                                                     row_height = 0;
  const auto                                                  row_origin = fp.rows.front().y;
  std::map<int64_t, std::vector<std::pair<int64_t, int64_t>>> row_spans;
  for (const auto& row : fp.rows) {
    const auto site = tech.sites.find(row.site);
    if (site == tech.sites.end() || row.count <= 0 || row.step <= 0 || row.step != dimension(site->second.width, fp.dbu)
        || (row.orientation != ph::Orientation::N && row.orientation != ph::Orientation::FS)) {
      throw std::invalid_argument("power-grid followpins require horizontal N/FS site rows");
    }
    const auto height = dimension(site->second.height, fp.dbu);
    const auto xend   = integer(double(row.x) + double(row.count) * row.step);
    const auto yend   = integer(double(row.y) + height);
    if (height <= 0 || row.x < fp.die.xl || xend > fp.die.xh || row.y < fp.die.yl || yend > fp.die.yh) {
      throw std::invalid_argument("power-grid row lies outside die");
    }
    if ((row_height && row_height != height) || (row.y - row_origin) % height != 0) {
      throw std::invalid_argument("power-grid rows require one aligned row height");
    }
    row_height = height;
    row_spans[row.y].emplace_back(row.x, xend);
    plan.core.xl = std::min(plan.core.xl, double(row.x));
    plan.core.yl = std::min(plan.core.yl, double(row.y));
    plan.core.xh = std::max(plan.core.xh, double(xend));
    plan.core.yh = std::max(plan.core.yh, double(yend));
    if (!rails.contains(row.site)) {
      rails.emplace(row.site, site_rails(tech, profile, row.site, fp.dbu));
    }
    const auto& rail = rails.at(row.site);
    if (rail.width >= height) {
      throw std::invalid_argument("PG rail width must be smaller than the row height");
    }
    for (const auto& layer : profile.followpin_layers) {
      if (!tech.layers.contains(layer)) {
        throw std::invalid_argument("unknown power-grid followpin layer: " + layer);
      }
      for (int edge = 0; edge < 2; ++edge) {
        const auto net = (edge == (row.orientation == ph::Orientation::N ? 0 : 1)) ? rail.bottom : rail.top;
        segments[{layer, net, edge == 0 ? row.y : yend, rail.width}].emplace_back(row.x, xend);
      }
    }
  }
  for (auto& [y, spans] : row_spans) {
    std::sort(spans.begin(), spans.end());
    for (size_t i = 1; i < spans.size(); ++i) {
      if (spans[i].first < spans[i - 1].second) {
        throw std::invalid_argument("overlapping power-grid site rows");
      }
    }
  }
  for (auto& [key, intervals] : segments) {
    const auto& [layer, net, y, width] = key;
    std::sort(intervals.begin(), intervals.end());
    auto       current = intervals.front();
    const auto append  = [&] {
      const ph::Rect rect{double(current.first), double(y - width / 2), double(current.second), double(y + width / 2)};
      if (rect.yl < fp.die.yl || rect.yh > fp.die.yh) {
        throw std::invalid_argument("PG edge rail extends outside die; provide a core inset before placement");
      }
      plan.metal.push_back({profile.nets[net], layer, rect, true});
      ++plan.rail_count[layer];
    };
    for (size_t i = 1; i < intervals.size(); ++i) {
      if (intervals[i].first <= current.second) {
        current.second = std::max(current.second, intervals[i].second);
      } else {
        append();
        current = intervals[i];
      }
    }
    append();
  }
  // Opposite polarities at a shared row edge would create a short.
  for (const auto& [key, intervals] : segments) {
    const auto& [layer, net, y, width] = key;
    if (net != 0) {
      continue;
    }
    const auto other = segments.find({layer, 1, y, width});
    if (other == segments.end()) {
      continue;
    }
    size_t i = 0, j = 0;
    while (i < intervals.size() && j < other->second.size()) {
      const auto& a = intervals[i];
      const auto& b = other->second[j];
      if (a.first < b.second && b.first < a.second) {
        throw std::invalid_argument("incompatible row orientations short power and ground followpins");
      }
      if (a.second <= b.second) {
        ++i;
      } else {
        ++j;
      }
    }
  }
  auto boundary = plan.core;
  for (const auto& metal : plan.metal) {
    boundary.yl = std::min(boundary.yl, metal.rect.yl);
    boundary.yh = std::max(boundary.yh, metal.rect.yh);
  }
  std::vector<std::pair<ph::Rect, std::set<std::string>>> obstacles;
  const auto                                              halo = dimension(profile.macro_halo, fp.dbu);
  if (halo < 0) {
    throw std::invalid_argument("negative power-grid macro halo");
  }
  for (const auto& cell : fp.components) {
    const auto master = tech.macros.find(cell.macro);
    if (master == tech.macros.end()) {
      throw std::invalid_argument("unknown power-grid component master: " + cell.macro);
    }
    if (!master->second.klass.starts_with("BLOCK") && !master->second.virtual_proxy) {
      continue;
    }
    const auto& loc = cell.place;
    if (loc.status != ph::Status::LOCKED && loc.status != ph::Status::FIXED) {
      throw std::invalid_argument("native power grid requires fixed macro locations");
    }
    auto width = dimension(master->second.width, fp.dbu), height = dimension(master->second.height, fp.dbu);
    if (ph::swaps_axes(loc.orientation)) {
      std::swap(width, height);
    }
    ph::Rect rect{double(loc.x) - halo, double(loc.y) - halo, double(loc.x) + width + halo, double(loc.y) + height + halo};
    require_rect(rect);
    auto layers = master->second.blocked_layers;
    layers.insert(master->second.conservative_obstructions.begin(), master->second.conservative_obstructions.end());
    for (const auto& shape : master->second.obstructions) {
      layers.insert(shape.layer);
    }
    for (const auto& [name, pin] : master->second.pins) {
      for (const auto& shape : pin.shapes) {
        layers.insert(shape.layer);
      }
    }
    if (master->second.virtual_proxy) {
      for (const auto& [name, layer] : tech.layers) {
        layers.insert(name);
      }
    }
    obstacles.emplace_back(rect, std::move(layers));
  }
  for (const auto& stripe : profile.stripes) {
    const auto layer = tech.layers.find(stripe.layer);
    if (layer == tech.layers.end() || (layer->second.direction != "HORIZONTAL" && layer->second.direction != "VERTICAL")) {
      throw std::invalid_argument("unknown power-grid stripe layer/direction: " + stripe.layer);
    }
    const bool vertical = layer->second.direction == "VERTICAL";
    const auto width = dimension(stripe.width, fp.dbu), spacing = dimension(stripe.spacing, fp.dbu);
    const auto pitch = dimension(stripe.pitch, fp.dbu), offset = dimension(stripe.offset, fp.dbu);
    if (width <= 0 || width % 2 || spacing < 0 || pitch < 2 * width + spacing || offset < 0) {
      throw std::invalid_argument("invalid power-grid stripe dimensions");
    }
    const auto lo = integer(vertical ? plan.core.xl : plan.core.yl), hi = integer(vertical ? plan.core.xh : plan.core.yh);
    if ((hi - lo) / pitch > 1000000) {
      throw std::invalid_argument("oversized power-grid stripe plan");
    }
    for (int64_t base = lo + offset; base <= hi; base += pitch) {
      for (int index = 0; index < 2; ++index) {
        const auto center = base + index * (width + spacing);
        if (center > hi || center - width / 2 < (vertical ? fp.die.xl : fp.die.yl)
            || center + width / 2 > (vertical ? fp.die.xh : fp.die.yh)) {
          continue;
        }
        const auto                             net  = profile.stripes_start_with_power ? index : 1 - index;
        const ph::Rect                         rect = vertical
                                                          ? ph::Rect{double(center - width / 2), boundary.yl, double(center + width / 2), boundary.yh}
                                                          : ph::Rect{plan.core.xl, double(center - width / 2), plan.core.xh, double(center + width / 2)};
        std::vector<std::pair<double, double>> spans{{vertical ? rect.yl : rect.xl, vertical ? rect.yh : rect.xh}};
        for (const auto& [obstacle, blocked_layers] : obstacles) {
          if (!blocked_layers.contains(stripe.layer) || !rect.overlaps(obstacle)) {
            continue;
          }
          const auto                             a = vertical ? obstacle.yl : obstacle.xl, b = vertical ? obstacle.yh : obstacle.xh;
          std::vector<std::pair<double, double>> next;
          for (const auto& [start, end] : spans) {
            if (b <= start || a >= end) {
              next.emplace_back(start, end);
            } else {
              if (a > start) {
                next.emplace_back(start, a);
              }
              if (b < end) {
                next.emplace_back(b, end);
              }
            }
          }
          spans = std::move(next);
        }
        for (const auto& [start, end] : spans) {
          auto clipped = rect;
          if (vertical) {
            clipped.yl = start;
            clipped.yh = end;
          } else {
            clipped.xl = start;
            clipped.xh = end;
          }
          plan.metal.push_back({profile.nets[net], stripe.layer, clipped, false});
          ++plan.stripe_count[stripe.layer];
        }
      }
    }
  }
  // Row cuts can leave rail segments between the periodic feeder stripes.
  // Add feeders before channel patches, so a channel cannot silently heal
  // itself. Existing adequately served grids retain their exact geometry.
  for (const auto& pair : profile.connect) {
    if (std::find(profile.followpin_layers.begin(), profile.followpin_layers.end(), pair[0]) == profile.followpin_layers.end()) {
      continue;
    }
    const auto spec = std::find_if(profile.stripes.begin(), profile.stripes.end(), [&](const auto& stripe) {
      return stripe.layer == pair[1] && tech.layers.at(stripe.layer).direction == "VERTICAL";
    });
    if (spec == profile.stripes.end()) {
      continue;
    }
    const auto width = dimension(spec->width, fp.dbu), spacing = dimension(spec->spacing, fp.dbu);
    const auto grid_dbu = ph::manufacturing_grid_dbu(tech, fp.dbu);
    if (!grid_dbu) {
      throw std::invalid_argument("invalid PG manufacturing grid");
    }
    const auto grid = *grid_dbu;
    const auto rail_count = plan.metal.size();
    for (size_t i = 0; i < rail_count; ++i) {
      const auto rail = plan.metal[i];
      if (!rail.followpin || rail.layer != pair[0]) {
        continue;
      }
      const auto served = [&](const Pg_metal& feeder) {
        return feeder.layer == pair[1] && feeder.net == rail.net && feeder.rect.xl >= rail.rect.xl && feeder.rect.xh <= rail.rect.xh
               && feeder.rect.yl <= rail.rect.yl && feeder.rect.yh >= rail.rect.yh;
      };
      if (std::any_of(plan.metal.begin(), plan.metal.end(), served)) {
        continue;
      }
      const auto        low = integer(rail.rect.xl) + width / 2, high = integer(rail.rect.xh) - width / 2;
      const auto        center = (low + high) / 2;
      std::set<int64_t> candidates;
      const auto        candidate = [&](int64_t value) {
        // Try both neighboring grid points; negative coordinates are allowed.
        const auto down = static_cast<int64_t>(std::floor(double(value) / grid)) * grid;
        for (auto x : {down, down + grid}) {
          if (x >= low && x <= high) {
            candidates.insert(x);
          }
        }
      };
      candidate(center);
      candidate(low);
      candidate(high);
      for (const auto& metal : plan.metal) {
        if (metal.layer != pair[1]) {
          continue;
        }
        candidate(integer(metal.rect.xl) - width / 2 - spacing);
        candidate(integer(metal.rect.xh) + width / 2 + spacing);
      }
      std::vector<int64_t> ordered(candidates.begin(), candidates.end());
      std::sort(ordered.begin(), ordered.end(), [&](auto a, auto b) {
        return std::pair{std::abs(a - center), a} < std::pair{std::abs(b - center), b};
      });
      bool connected = false;
      for (auto x : ordered) {
        const ph::Rect rect{double(x - width / 2), boundary.yl, double(x + width / 2), boundary.yh};
        bool           blocked = false;
        for (const auto& [obstacle, layers] : obstacles) {
          if (layers.contains(pair[1]) && rect.overlaps(obstacle)) {
            blocked = true;
            break;
          }
        }
        for (const auto& metal : plan.metal) {
          if (blocked) {
            break;
          }
          if (metal.layer != pair[1]) {
            continue;
          }
          const auto gap = std::max(rect.xl - metal.rect.xh, metal.rect.xl - rect.xh);
          if (gap < spacing || gap <= 0) {
            blocked = true;
          }
        }
        if (blocked) {
          continue;
        }
        plan.metal.push_back({rail.net, pair[1], rect, false});
        ++plan.stripe_count[pair[1]];
        connected = true;
        break;
      }
      if (!connected) {
        throw std::invalid_argument("cannot feed isolated PG rail on " + rail.layer + " for " + rail.net);
      }
    }
  }
  // Patch the stripe plan before generating via intersections. A skip is a
  // whole slot; a gap may retain two separately connected stripe segments.
  if (!profile.channels.empty()) {
    const ph::Signal_metal_rules rules(tech, fp);
    for (const auto& channel : profile.channels) {
      if ((channel.style != "skip" && channel.style != "gap")
          || std::none_of(profile.stripes.begin(),
                          profile.stripes.end(),
                          [&](const auto& stripe) { return stripe.layer == channel.layer; })
          || std::find(profile.followpin_layers.begin(), profile.followpin_layers.end(), channel.layer)
                 != profile.followpin_layers.end()) {
        throw std::invalid_argument("channel requires a stripe-only layer and skip/gap style");
      }
      const ph::Rect region{double(dimension(channel.region.xl, fp.dbu)),
                            double(dimension(channel.region.yl, fp.dbu)),
                            double(dimension(channel.region.xh, fp.dbu)),
                            double(dimension(channel.region.yh, fp.dbu))};
      require_rect(region);
      const auto grid = tech.manufacturing_grid > 0 ? ph::manufacturing_grid_dbu(tech, fp.dbu) : std::nullopt;
      if (!grid) {
        throw std::invalid_argument("channel requires an integer manufacturing grid");
      }
      for (auto coordinate : {region.xl, region.yl, region.xh, region.yh}) {
        if (std::fmod(coordinate, double(*grid)) != 0) {
          throw std::invalid_argument("off-grid channel boundary");
        }
      }
      const auto& layer = tech.layers.at(channel.layer);
      if (!layer.rules.loaded || !std::isfinite(layer.rules.area) || layer.rules.area < 0) {
        throw std::invalid_argument("channel requires characterized minimum area");
      }
      const bool            vertical = layer.direction == "VERTICAL";
      std::vector<Pg_metal> next;
      for (const auto& metal : plan.metal) {
        if (metal.layer != channel.layer || !metal.rect.overlaps(region)) {
          next.push_back(metal);
          continue;
        }
        const auto& r = metal.rect;
        if (metal.followpin || (vertical ? region.xl > r.xl || region.xh < r.xh : region.yl > r.yl || region.yh < r.yh)) {
          throw std::invalid_argument("channel must cover the complete stripe width");
        }
        const double lo = vertical ? r.yl : r.xl, hi = vertical ? r.yh : r.xh;
        const double cut_lo = std::max(lo, vertical ? region.yl : region.xl);
        const double cut_hi = std::min(hi, vertical ? region.yh : region.xh);
        if (channel.style == "skip" && (cut_lo != lo || cut_hi != hi)) {
          throw std::invalid_argument("slot skip must cover the complete stripe extent; use gap for a partial cut");
        }
        std::vector<Pg_metal> pieces;
        for (const auto& [begin, end] : {std::pair{lo, cut_lo}, std::pair{cut_hi, hi}}) {
          if (begin == end) {
            continue;
          }
          auto piece = metal;
          if (vertical) {
            piece.rect.yl = begin;
            piece.rect.yh = end;
          } else {
            piece.rect.xl = begin;
            piece.rect.xh = end;
          }
          const double area = (piece.rect.xh - piece.rect.xl) * (piece.rect.yh - piece.rect.yl);
          if (area + 1e-9 < layer.rules.area * fp.dbu * fp.dbu) {
            throw std::invalid_argument("channel leaves a stripe below minimum area");
          }
          pieces.push_back(std::move(piece));
        }
        if (pieces.size() == 2 && !rules.spacing({channel.layer, pieces[0].rect}, {channel.layer, pieces[1].rect}).legal) {
          throw std::invalid_argument("channel gap violates stripe end spacing");
        }
        plan.channel_removed_length_um[channel.layer] += (cut_hi - cut_lo) / fp.dbu;
        next.insert(next.end(), pieces.begin(), pieces.end());
      }
      plan.metal = std::move(next);
    }
    plan.stripe_count.clear();
    for (const auto& metal : plan.metal) {
      if (!metal.followpin) {
        ++plan.stripe_count[metal.layer];
      }
    }
  }
  std::sort(plan.metal.begin(), plan.metal.end(), [](const auto& a, const auto& b) {
    return std::tie(a.net, a.layer, a.rect.yl, a.rect.xl, a.rect.yh, a.rect.xh)
           < std::tie(b.net, b.layer, b.rect.yl, b.rect.xl, b.rect.yh, b.rect.xh);
  });
  return plan;
}
}  // namespace livehd::placement

// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include <algorithm>
#include <cmath>
#include <numeric>
#include <stdexcept>

#include "chunk_geometry.hpp"

namespace livehd::placement {
namespace {
using Tiles = std::vector<Chunk_box>;

void require(bool ok, const std::string& reason) {
  if (!ok) {
    throw std::invalid_argument("chunk regions: " + reason);
  }
}
double area(const Chunk_box& b) { return (b[2] - b[0]) * (b[3] - b[1]); }
bool   overlaps(const Chunk_box& a, const Chunk_box& b) { return a[0] < b[2] && a[2] > b[0] && a[1] < b[3] && a[3] > b[1]; }
bool   joined(const Chunk_box& a, const Chunk_box& b) {
  for (int k : {0, 1}) {
    int j = 1 - k;
    if ((a[k] == b[k + 2] || a[k + 2] == b[k]) && std::max(a[j], b[j]) < std::min(a[j + 2], b[j + 2])) {
      return true;
    }
  }
  return false;
}
// On each grid line the union boundary contains exactly those intervals with one contributing tile.
void edges(Fp_chunk& c) {
  std::map<std::pair<int, double>, std::map<double, int>> events;
  for (const auto& t : c.region) {
    for (int k : {0, 1}) {
      for (double at : {t[1 - k], t[3 - k]}) {
        auto& line = events[{k, at}];
        ++line[t[k]];
        --line[t[k + 2]];
      }
    }
  }
  c.perimeter.clear();
  for (const auto& [key, line] : events) {
    int    active = 0;
    double start  = 0;
    for (auto [pos, delta] : line) {
      if (active == 1 && delta != 0) {
        c.perimeter.push_back({double(key.first), key.second, start, pos});
      }
      active += delta;
      if (active == 1 && delta != 0) {
        start = pos;
      }
    }
  }
}
void exclusions(Fp_chunk& c) {
  const auto          box = c.worker_box();
  std::vector<double> cuts{box[0], box[2]};
  for (const auto& t : c.region) {
    cuts.push_back(t[0]);
    cuts.push_back(t[2]);
  }
  std::sort(cuts.begin(), cuts.end());
  cuts.erase(std::unique(cuts.begin(), cuts.end()), cuts.end());
  c.exclusions.clear();
  for (size_t i = 1; i < cuts.size(); ++i) {
    std::vector<std::pair<double, double>> bands;
    for (const auto& t : c.region) {
      if (t[0] <= cuts[i - 1] && cuts[i] <= t[2]) {
        bands.emplace_back(t[1], t[3]);
      }
    }
    std::sort(bands.begin(), bands.end());
    double low = box[1];
    for (auto [bottom, top] : bands) {
      if (bottom > low) {
        c.exclusions.push_back({cuts[i - 1], low, cuts[i], bottom});
      }
      low = top;
    }
    if (low < box[3]) {
      c.exclusions.push_back({cuts[i - 1], low, cuts[i], box[3]});
    }
  }
}
}  // namespace

void apply_chunk_regions(Chunk_plan& plan, const std::map<std::string, Tiles>& masks, const std::vector<Chunk_object>& objects,
                         Chunk_box core, std::array<double, 2> pitch) {
  if (masks.empty()) {
    return;
  }
  size_t found = 0;
  for (auto& c : plan.chunks) {
    auto it = masks.find(c.id);
    if (it == masks.end()) {
      continue;
    }
    ++found;
    require(c.region.empty() && c.shared_dbu2 == 0, "conflicting overlap or stepped boundary on " + c.id);
    const auto& tiles = it->second;
    require(!tiles.empty(), "empty mask for " + c.id);
    double available = 0;
    for (size_t i = 0; i < tiles.size(); ++i) {
      const auto& t = tiles[i];
      for (int k : {0, 1}) {
        require(t[k] >= core[k] && t[k] < t[k + 2] && t[k + 2] <= core[k + 2], "tile outside core or empty in " + c.id);
        for (double end : {t[k], t[k + 2]}) {
          double steps = (end - core[k]) / pitch[k];
          require(end == core[k + 2] || std::abs(steps - std::round(steps)) < 1e-7, "off-grid tile edge in " + c.id);
        }
      }
      for (size_t j = 0; j < i; ++j) {
        require(!overlaps(t, tiles[j]), "overlapping tiles in " + c.id);
      }
      available += area(t);
    }
    require(available + 1e-7 >= c.reserved, "insufficient usable area for " + c.id);
    std::vector<bool>   reached(tiles.size());
    std::vector<size_t> pending{0};
    reached[0] = true;
    for (size_t i = 0; i < pending.size(); ++i) {
      for (size_t j = 0; j < tiles.size(); ++j) {
        if (!reached[j] && joined(tiles[pending[i]], tiles[j])) {
          reached[j] = true;
          pending.push_back(j);
        }
      }
    }
    require(pending.size() == tiles.size(), "disconnected mask for " + c.id);
    c.region = tiles;
    edges(c);
    exclusions(c);
    for (int obj : c.objects) {
      const auto& o = objects[obj];
      if (!o.macro && !o.fixed) {
        continue;
      }
      auto body = o.fixed ? *o.fixed : o.macro_footprint(plan.seeds[obj]);
      for (int k : {0, 1}) {
        body[k]     -= o.worker_halo[k];
        body[k + 2] += o.worker_halo[k];
      }
      const auto frame = c.worker_box();
      require(body[0] >= frame[0] && body[1] >= frame[1] && body[2] <= frame[2] && body[3] <= frame[3]
                  && std::ranges::none_of(c.exclusions, [&](const auto& hole) { return overlaps(body, hole); }),
              "mask cuts a seeded macro, halo or fixed cell in " + c.id);
    }
    for (auto& port : c.ports) {
      const auto next = c.project_port(port.xy);
      if (auto fixed = plan.fixed_contacts.find(port.net_index); fixed != plan.fixed_contacts.end()) {
        const auto& b = fixed->second;
        require(next == port.xy || b != Chunk_box{port.xy[0], port.xy[1], port.xy[0], port.xy[1]},
                "mask displaces a fixed terminal");
      }
      port.xy = next;
    }
  }
  require(found == masks.size(), "unknown home in tile assignment");
  for (const auto& c : plan.chunks) {
    if (!masks.contains(c.id)) {
      continue;
    }
    for (const auto& peer : plan.chunks) {
      if (&peer == &c || !overlaps(c.worker_box(), peer.worker_box())) {
        continue;
      }
      for (const auto& tile : c.region) {
        const bool claim = peer.region.empty()
                               ? overlaps(tile, peer.box)
                               : std::ranges::any_of(peer.region, [&](const auto& other) { return overlaps(tile, other); });
        require(!claim, "tile claimed by both " + c.id + " and " + peer.id);
      }
    }
  }
}
}  // namespace livehd::placement

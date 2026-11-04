// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <set>

#include "legroute_internal.hpp"

namespace livehd::placement::lr {
namespace {
struct Work_limit {};
struct Work {
  size_t used = 0, limit = 0;
  void   take(size_t n = 1) {
    if (n > limit - used) {
      throw Work_limit{};
    }
    used += n;
  }
};
struct Region_fit {
  std::vector<int>   tiles, boxes;
  std::map<int, int> assignment;  // valid previous matching, box -> tile
  size_t             cells = 0, missing = 0;
  double             cell_area = 0, free_area = 0;
  double             ceiling = .90;  // Legroute_options::density_ceiling
};
double excess_area(const Region_fit& region) { return std::max(0., region.cell_area - region.ceiling * region.free_area); }
size_t excess_cells(const Region_fit& region, unsigned target) {
  return target && region.cells > target ? region.cells - target : 0;
}
bool deficient(const Region_fit& region, unsigned target) {
  return region.missing || excess_area(region) > 1e-9 || excess_cells(region, target);
}
std::array<int, 4> neighbors(const Grid& grid, int tile) {
  return {grid.col(tile) > 0 ? tile - 1 : -1,
          grid.col(tile) + 1 < grid.cols ? tile + 1 : -1,
          grid.row(tile) > 0 ? tile - grid.cols : -1,
          grid.row(tile) + 1 < grid.rows ? tile + grid.cols : -1};
}
}  // namespace

Partition_fit_report repair_partition_fit(const Design& d, Grid& g, Boxes& boxes, const Route_cuts& cuts) {
  Partition_fit_report report;
  if (!movable_route_partition(d.opt) || boxes.boxes.empty()) {
    return report;
  }
  const auto started = Clock::now();
  if (!connected_regions(g, g.region) || !cuts.allows(g, g.region)) {
    throw std::logic_error("legroute fit repair requires connected regions and valid original coarse cuts");
  }
  const auto target = d.opt.partition_cells;
  // Deterministic work bounds keep the plan and local handoff reproducible.
  // The cap covers matching and mesh checks, not SAT. An interrupted candidate
  // is private; only fully assessed improvements commit.
  Work       work{0, std::max(size_t(1'000'000), std::min(size_t(500'000'000), g.size() * size_t(262144)))};
  report.work_limit                     = work.limit;
  const size_t            attempt_limit = std::min(size_t(32'768), std::max(size_t(256), g.size() * size_t(8)));
  std::vector<Region_fit> state(size_t(g.regions));
  for (auto& region : state) {
    region.ceiling = d.opt.density_ceiling;
  }
  std::vector<std::vector<int>> home(g.size());
  std::vector<double>           tile_area(g.size()), box_area(boxes.boxes.size());
  std::vector<size_t>           tile_cells(g.size());
  std::vector<double>           tile_cell_area(g.size());
  for (size_t t = 0; t < g.size(); ++t) {
    auto& region = state[size_t(g.region[t])];
    region.tiles.push_back(int(t));
    int64_t sites = 0;
    if (g.placeable_sites.size() == g.size()) {
      sites = g.placeable_sites[t];
    } else {
      for (const auto& segment : g.segs(int(t))) {
        sites += segment.hi - segment.lo;
      }
    }
    tile_area[t]      = double(sites) * double(g.site) * double(g.row_h);
    region.free_area += tile_area[t];
  }
  for (size_t b = 0; b < boxes.boxes.size(); ++b) {
    const auto& box  = boxes.boxes[b];
    const int   tile = box.tile >= 0 ? box.tile : g.tile_of(box.cx, box.cy);
    home[size_t(tile)].push_back(int(b));
    auto& region = state[size_t(g.region[size_t(tile)])];
    region.boxes.push_back(int(b));
    region.cells             += box.cells.size();
    tile_cells[size_t(tile)] += box.cells.size();
    for (int cell : box.cells) {
      const auto& master  = *d.macro[size_t(d.movable[size_t(cell)])];
      box_area[b]        += master.width * master.height * double(d.fp.dbu) * double(d.fp.dbu);
    }
    region.cell_area             += box_area[b];
    tile_cell_area[size_t(tile)] += box_area[b];
  }
  // Raw box fit depends only on its cell widths/tallness and the exact free
  // segment pattern. Placed-box exceptions are evaluated at their actual tile.
  std::vector<std::map<uint32_t, bool>> fit_cache(boxes.boxes.size());
  const auto                            fits = [&](int b, int t) {
    work.take();
    const auto& box = boxes.boxes[size_t(b)];
    if (g.blocked[size_t(t)] || (box.fixed && box.tile != t)) {
      return false;
    }
    if (box.placed) {
      return box_fits(d, g, box, t);
    }
    auto&      cache = fit_cache[size_t(b)];
    const auto key   = g.key[size_t(t)];
    if (auto found = cache.find(key); found != cache.end()) {
      return found->second;
    }
    work.take(box.cells.size());
    return cache.emplace(key, box_fits(d, g, box, t)).first->second;
  };
  const auto match = [&](Region_fit& region) {
    std::vector<int>  owner(region.tiles.size(), -1);
    std::vector<char> seen(region.tiles.size());
    std::set<int>     assigned;
    for (const auto& [b, tile] : region.assignment) {
      work.take();
      const auto at = std::lower_bound(region.tiles.begin(), region.tiles.end(), tile);
      if (at != region.tiles.end() && *at == tile && std::binary_search(region.boxes.begin(), region.boxes.end(), b)
          && fits(b, tile)) {
        owner[size_t(at - region.tiles.begin())] = b;
        assigned.insert(b);
      }
    }
    const std::function<bool(int)> augment = [&](int b) {
      for (bool occupied : {false, true}) {
        for (size_t j = 0; j < region.tiles.size(); ++j) {
          work.take();
          if (seen[j] || (owner[j] >= 0) != occupied || !fits(b, region.tiles[j])) {
            continue;
          }
          seen[j] = true;
          if (!occupied || augment(owner[j])) {
            owner[j] = b;
            return true;
          }
        }
      }
      return false;
    };
    region.missing = 0;
    for (int b : region.boxes) {
      if (assigned.contains(b)) {
        continue;
      }
      std::fill(seen.begin(), seen.end(), false);
      region.missing += !augment(b);
    }
    region.assignment.clear();
    for (size_t j = 0; j < owner.size(); ++j) {
      if (owner[j] >= 0) {
        region.assignment.emplace(owner[j], region.tiles[j]);
      }
    }
  };
  const auto monotone = [&](const Region_fit& before, const Region_fit& after) {
    return after.missing <= before.missing && excess_cells(after, target) <= excess_cells(before, target)
           && excess_area(after) <= excess_area(before) + 1e-9;
  };
  const auto improves = [&](const Region_fit& before, const Region_fit& after) {
    return after.missing < before.missing || excess_cells(after, target) < excess_cells(before, target)
           || excess_area(after) + 1e-9 < excess_area(before);
  };
  std::vector<int> seen(g.size());
  int              generation = 0;
  const auto       connected  = [&](const Region_fit& region) {
    if (region.tiles.empty()) {
      return false;
    }
    ++generation;
    std::vector<int> queue{region.tiles.front()};
    seen[size_t(queue.front())] = generation;
    for (size_t at = 0; at < queue.size(); ++at) {
      for (int q : neighbors(g, queue[at])) {
        work.take();
        if (q < 0 || seen[size_t(q)] == generation || !std::binary_search(region.tiles.begin(), region.tiles.end(), q)) {
          continue;
        }
        seen[size_t(q)] = generation;
        queue.push_back(q);
      }
    }
    return queue.size() == region.tiles.size();
  };
  // A neutral first tile can hide useful row capacity one tile farther in.
  // Evaluate the whole bounded patch (or exchange) before changing ownership.
  const auto transfer = [&](std::vector<int> tiles, int recipient, bool exchange) {
    const int   donor            = g.region[size_t(tiles.front())];
    const auto& before_donor     = state[size_t(donor)];
    const auto& before_recipient = state[size_t(recipient)];
    if (!deficient(before_donor, target) && !deficient(before_recipient, target)) {
      return false;
    }
    if (++report.attempts > attempt_limit) {
      throw Work_limit{};
    }
    auto from = before_donor, to = before_recipient;
    for (size_t i = 0; i < tiles.size(); ++i) {
      const int t  = tiles[i];
      auto&     a  = exchange && i ? to : from;
      auto&     b  = exchange && i ? from : to;
      a.cells     -= tile_cells[size_t(t)];
      b.cells     += tile_cells[size_t(t)];
      a.cell_area -= tile_cell_area[size_t(t)];
      b.cell_area += tile_cell_area[size_t(t)];
      a.free_area -= tile_area[size_t(t)];
      b.free_area += tile_area[size_t(t)];
      std::erase(a.tiles, t);
      b.tiles.insert(std::lower_bound(b.tiles.begin(), b.tiles.end(), t), t);
      for (int id : home[size_t(t)]) {
        std::erase(a.boxes, id);
        b.boxes.insert(std::lower_bound(b.boxes.begin(), b.boxes.end(), id), id);
      }
    }
    if (!monotone(before_donor, from) || !monotone(before_recipient, to)) {
      return false;
    }
    const auto owner = [&](int t) {
      for (size_t i = 0; i < tiles.size(); ++i) {
        if (tiles[i] == t) {
          return exchange && i ? donor : recipient;
        }
      }
      return g.region[size_t(t)];
    };
    for (int t : tiles) {
      for (int q : neighbors(g, t)) {
        work.take();
        if (q < 0 || owner(t) == owner(q)) {
          continue;
        }
        if ((g.row(t) == g.row(q) ? cuts.forbidden_h : cuts.forbidden_v)[size_t(std::min(t, q))]) {
          return false;
        }
      }
    }
    if (!connected(from) || !connected(to)) {
      return false;
    }
    match(from);
    match(to);
    if (!monotone(before_donor, from) || !monotone(before_recipient, to)
        || (!improves(before_donor, from) && !improves(before_recipient, to))) {
      return false;
    }
    state[size_t(donor)]     = std::move(from);
    state[size_t(recipient)] = std::move(to);
    for (size_t i = 0; i < tiles.size(); ++i) {
      g.region[size_t(tiles[i])] = exchange && i ? donor : recipient;
    }
    report.transfers += tiles.size();
    report.patches   += tiles.size() > 1 && !exchange;
    report.exchanges += exchange;
    return true;
  };
  size_t     relays     = 0;
  // Only ownership changes: moving a whole tile also moves its home boxes'
  // region membership. No box geometry, cell pose or coarse path is rebuilt.
  const auto move_patch = [&](Region_fit& from, Region_fit& to, const std::vector<int>& patch) {
    for (int t : patch) {
      work.take();
      from.cells     -= tile_cells[size_t(t)];
      to.cells       += tile_cells[size_t(t)];
      from.cell_area -= tile_cell_area[size_t(t)];
      to.cell_area   += tile_cell_area[size_t(t)];
      from.free_area -= tile_area[size_t(t)];
      to.free_area   += tile_area[size_t(t)];
      std::erase(from.tiles, t);
      to.tiles.insert(std::lower_bound(to.tiles.begin(), to.tiles.end(), t), t);
      for (int b : home[size_t(t)]) {
        std::erase(from.boxes, b);
        to.boxes.insert(std::lower_bound(to.boxes.begin(), to.boxes.end(), b), b);
      }
    }
  };
  const auto patches_between = [&](const Region_fit& from, const Region_fit& to) {
    std::set<std::vector<int>> patches;
    for (int t : from.tiles) {
      bool boundary = false;
      for (int q : neighbors(g, t)) {
        work.take();
        boundary = boundary || (q >= 0 && std::binary_search(to.tiles.begin(), to.tiles.end(), q));
      }
      if (!boundary) {
        continue;
      }
      patches.insert({t});
      for (int q : neighbors(g, t)) {
        work.take();
        if (q >= 0 && std::binary_search(from.tiles.begin(), from.tiles.end(), q)) {
          patches.insert({std::min(t, q), std::max(t, q)});
        }
      }
    }
    std::vector<std::vector<int>> result(patches.begin(), patches.end());
    std::ranges::sort(result, [](const auto& a, const auto& b) { return a.size() != b.size() ? a.size() < b.size() : a < b; });
    return result;
  };
  const auto useful_gain = [&](const std::vector<int>& patch, const Region_fit& recipient) {
    int gain = 0;
    for (int t : patch) {
      gain -= int(home[size_t(t)].size());
      for (int b : recipient.boxes) {
        if (fits(b, t)) {
          ++gain;
          break;
        }
      }
    }
    return gain;
  };
  // A deficient region can be surrounded by exactly saturated neighbors even
  // when useful slack is just two mesh hops away. The mediator's neutral gain
  // is private until a second patch yields a strict, fully checked improvement.
  const auto relay = [&] {
    std::vector<std::set<int>> adjacent(size_t(g.regions));
    for (size_t t = 0; t < g.size(); ++t) {
      for (int q : neighbors(g, int(t))) {
        work.take();
        if (q >= 0 && g.region[t] != g.region[size_t(q)]) {
          adjacent[size_t(g.region[t])].insert(g.region[size_t(q)]);
        }
      }
    }
    for (int recipient = 0; recipient < g.regions; ++recipient) {
      const auto& target_before = state[size_t(recipient)];
      if (!target_before.missing) {
        continue;
      }
      for (int mediator : adjacent[size_t(recipient)]) {
        const auto& via_before = state[size_t(mediator)];
        if (deficient(via_before, target)) {
          continue;
        }
        for (int donor : adjacent[size_t(mediator)]) {
          if (donor == recipient) {
            continue;
          }
          const auto& source_before = state[size_t(donor)];
          if (deficient(source_before, target) || source_before.tiles.size() <= source_before.boxes.size()) {
            continue;
          }
          for (const auto& first : patches_between(source_before, via_before)) {
            if (useful_gain(first, via_before) <= 0) {
              continue;
            }
            if (++report.attempts > attempt_limit) {
              throw Work_limit{};
            }
            auto source = source_before, via = via_before;
            move_patch(source, via, first);
            if (!monotone(source_before, source) || !monotone(via_before, via) || !connected(source)) {
              continue;
            }
            match(source);
            if (!monotone(source_before, source)) {
              continue;
            }
            for (const auto& second : patches_between(via, target_before)) {
              if (useful_gain(second, target_before) <= 0) {
                continue;
              }
              if (++report.attempts > attempt_limit) {
                throw Work_limit{};
              }
              auto middle = via, destination = target_before;
              move_patch(middle, destination, second);
              if (!monotone(via_before, middle) || !monotone(target_before, destination)) {
                continue;
              }
              std::map<int, int> changed;
              for (int t : first) {
                changed[t] = mediator;
              }
              for (int t : second) {
                changed[t] = recipient;
              }
              const auto owner = [&](int t) {
                const auto at = changed.find(t);
                return at == changed.end() ? g.region[size_t(t)] : at->second;
              };
              bool permitted = true;
              for (const auto& [t, region] : changed) {
                (void)region;
                for (int q : neighbors(g, t)) {
                  work.take();
                  if (q >= 0 && owner(t) != owner(q)
                      && (g.row(t) == g.row(q) ? cuts.forbidden_h : cuts.forbidden_v)[size_t(std::min(t, q))]) {
                    permitted = false;
                  }
                }
              }
              if (!permitted || !connected(middle) || !connected(destination)) {
                continue;
              }
              match(middle);
              match(destination);
              if (!monotone(via_before, middle) || !monotone(target_before, destination)
                  || (!improves(source_before, source) && !improves(via_before, middle) && !improves(target_before, destination))) {
                continue;
              }
              // All work, including both exact matchings, precedes publication.
              // A work-limit exception anywhere above discards the whole relay.
              state[size_t(donor)]     = std::move(source);
              state[size_t(mediator)]  = std::move(middle);
              state[size_t(recipient)] = std::move(destination);
              for (const auto& [t, region] : changed) {
                g.region[size_t(t)] = region;
              }
              report.transfers += changed.size();
              ++relays;
              return true;
            }
          }
        }
      }
    }
    return false;
  };
  const auto totals = [&](size_t& missing, size_t& dense, size_t& oversized) {
    missing = dense = oversized = 0;
    for (const auto& region : state) {
      missing   += region.missing;
      dense     += excess_area(region) > 1e-9;
      oversized += excess_cells(region, target) > 0;
    }
  };
  try {
    for (auto& region : state) {
      match(region);
    }
    report.assessed = true;
    totals(report.missing_before, report.dense_before, report.oversized_before);
    for (;;) {
      bool accepted = false;
      // Prefer single-tile repairs, then adjacent pairs, then boundary exchanges.
      for (int mode = 0; mode < 3 && !accepted; ++mode) {
        for (int tile = 0; tile < int(g.size()) && !accepted; ++tile) {
          const int     donor = g.region[size_t(tile)];
          std::set<int> adjacent;
          for (int q : neighbors(g, tile)) {
            if (q >= 0 && g.region[size_t(q)] != donor) {
              adjacent.insert(g.region[size_t(q)]);
            }
          }
          for (int recipient : adjacent) {
            work.take();
            if (mode == 0) {
              accepted = transfer({tile}, recipient, false);
            } else {
              for (int q : neighbors(g, tile)) {
                if (q < 0) {
                  continue;
                }
                if (mode == 1 && g.region[size_t(q)] == donor) {
                  accepted = transfer({tile, q}, recipient, false);
                }
                if (mode == 2 && g.region[size_t(q)] == recipient) {
                  accepted = transfer({tile, q}, recipient, true);
                }
                if (accepted) {
                  break;
                }
              }
            }
            if (accepted) {
              break;
            }
          }
        }
      }
      if (!accepted) {
        accepted = relay();
      }
      if (!accepted) {
        break;
      }
    }
  } catch (const Work_limit&) {
    report.limited = true;
  }
  if (report.assessed) {
    totals(report.missing_after, report.dense_after, report.oversized_after);
    report.resolved = !report.missing_after && !report.dense_after && !report.oversized_after;
  }
  g.region_tiles.assign(size_t(g.regions), {});
  for (size_t t = 0; t < g.size(); ++t) {
    g.region_tiles[size_t(g.region[t])].push_back(int(t));
  }
  for (auto& box : boxes.boxes) {
    box.region = g.region[size_t(box.tile >= 0 ? box.tile : g.tile_of(box.cx, box.cy))];
  }
  report.work    = work.used;
  report.seconds = seconds_since(started);
  std::fprintf(stderr,
               "legroute partition fit: assessed %d, missing %zu -> %zu, dense %zu -> %zu, oversized %zu -> %zu; "
               "%zu transfers, %zu attempts, %zu/%zu work%s, %.3f s\n",
               int(report.assessed),
               report.missing_before,
               report.missing_after,
               report.dense_before,
               report.dense_after,
               report.oversized_before,
               report.oversized_after,
               report.transfers,
               report.attempts,
               report.work,
               report.work_limit,
               report.limited ? " limited" : "",
               report.seconds);
  if (!d.opt.output_dir.empty()) {
    std::filesystem::create_directories(d.opt.output_dir);
    std::ofstream out(d.opt.output_dir + "/partition_fit.json");
    out << std::boolalpha << std::setprecision(17) << "{\"assessed\":" << report.assessed << ",\"resolved\":" << report.resolved
        << ",\"limited\":" << report.limited << ",\"missing_before\":" << report.missing_before
        << ",\"missing_after\":" << report.missing_after << ",\"dense_before\":" << report.dense_before
        << ",\"dense_after\":" << report.dense_after << ",\"oversized_before\":" << report.oversized_before
        << ",\"oversized_after\":" << report.oversized_after << ",\"transfers\":" << report.transfers << ",\"relays\":" << relays
        << ",\"patches\":" << report.patches << ",\"exchanges\":" << report.exchanges << ",\"attempts\":" << report.attempts
        << ",\"work\":" << report.work << ",\"work_limit\":" << report.work_limit << ",\"seconds\":" << report.seconds
        << ",\"scope\":\"boundary ownership repair only; unchanged boxes, pose, paths and capacities; final gate "
           "authoritative\"}\n";
    if (!out) {
      throw std::runtime_error("cannot write partition fitting repair report");
    }
  }
  return report;
}
}  // namespace livehd::placement::lr

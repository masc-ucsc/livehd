// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "fnv1a.hpp"
#include "fp_chunks.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <numeric>
#include <set>
#include <sstream>
#include <stdexcept>
#include <tuple>

#include "chunk_demand.hpp"
#include "chunk_templates.hpp"
#include "chunk_topology.hpp"
#include "io_spacing.hpp"

namespace livehd::placement {
namespace {
using Point = std::array<double, 2>;
struct Demand {
  double                   area = 0;
  Point                    minimum{};
  std::optional<Chunk_box> fixed{};
  double                   occupied = 0;  // Actual cell/macro area: a chunk below this size cannot be legalized.
};
void include(Demand& a, const Demand& b) {
  a.area     += b.area;
  a.occupied += b.occupied;
  for (int k = 0; k < 2; ++k) {
    a.minimum[k] = std::max(a.minimum[k], b.minimum[k]);
  }
  if (!b.fixed) {
    return;
  }
  if (!a.fixed) {
    a.fixed = b.fixed;
  }
  for (int k = 0; k < 2; ++k) {
    (*a.fixed)[k]     = std::min((*a.fixed)[k], (*b.fixed)[k]);
    (*a.fixed)[k + 2] = std::max((*a.fixed)[k + 2], (*b.fixed)[k + 2]);
  }
}
double unit(std::string_view key, uint64_t h) {
  h  = livehd::hash_util::fnv1a64(key, h);
  h ^= h >> 33;
  h *= 0xff51afd7ed558ccdULL;
  h ^= h >> 33;
  return double(h >> 11) / 9007199254740992.;
}
// Recursive area-weighted cuts on the row/site lattice. Fixed objects constrain which cuts are legal.
bool tile_impl(std::vector<int> ids, Chunk_box box, const Point& pitch, const std::vector<Demand>& demand,
               const std::vector<Point>& centers, std::vector<Chunk_box>& out, size_t& attempts) {
  if (!attempts) {
    return false;
  }
  --attempts;
  if (ids.size() == 1) {
    const auto& d = demand[ids[0]];
    if ((box[2] - box[0]) * (box[3] - box[1]) < d.occupied) {
      return false;
    }
    for (int k = 0; k < 2; ++k) {
      if (box[k + 2] - box[k] < d.minimum[k] || (d.fixed && ((*d.fixed)[k] < box[k] || (*d.fixed)[k + 2] > box[k + 2]))) {
        return false;
      }
    }
    out[ids[0]] = box;
    return true;
  }
  int first = box[2] - box[0] >= box[3] - box[1] ? 0 : 1;
  for (int attempt = 0; attempt < 2; ++attempt) {
    int k = first ^ attempt;
    std::stable_sort(ids.begin(), ids.end(), [&](int i, int j) { return centers[i][k] < centers[j][k]; });
    size_t              n = ids.size();
    std::vector<Demand> right(n);
    for (size_t i = n; i-- > 0;) {
      if (i + 1 < n) {
        right[i] = right[i + 1];
      }
      include(right[i], demand[ids[i]]);
    }
    Demand left;
    size_t best = 0;
    double cut = 0, score = std::numeric_limits<double>::infinity();
    for (size_t i = 1; i < n; ++i) {
      include(left, demand[ids[i - 1]]);
      const auto& r = right[i];
      if (std::max(left.minimum[k ^ 1], r.minimum[k ^ 1]) > box[(k ^ 1) + 2] - box[k ^ 1]) {
        continue;
      }
      double       lo = box[k] + std::max(pitch[k], left.minimum[k]), hi = box[k + 2] - std::max(pitch[k], r.minimum[k]);
      const double cross = box[(k ^ 1) + 2] - box[k ^ 1];
      lo                 = std::max(lo, box[k] + left.occupied / cross);
      hi                 = std::min(hi, box[k + 2] - r.occupied / cross);
      if (left.fixed) {
        lo = std::max(lo, (*left.fixed)[k + 2]);
      }
      if (r.fixed) {
        hi = std::min(hi, (*r.fixed)[k]);
      }
      lo = std::ceil((lo - box[k]) / pitch[k] - 1e-9);
      hi = std::floor((hi - box[k]) / pitch[k] + 1e-9);
      if (lo > hi) {
        continue;
      }
      double frac = left.area / (left.area + r.area);
      // Balance object count as well as area to bound recursion for very uneven macros.
      double loss = std::abs(frac - .5) + std::abs(double(i) / double(n) - .5);
      if (loss >= score) {
        continue;
      }
      score = loss;
      best  = i;
      cut   = box[k] + pitch[k] * std::clamp(std::round(frac * (box[k + 2] - box[k]) / pitch[k]), lo, hi);
    }
    if (!best) {
      continue;
    }
    auto a = box, b = box;
    a[k + 2] = b[k] = cut;
    if (tile_impl({ids.begin(), ids.begin() + best}, a, pitch, demand, centers, out, attempts)
        && tile_impl({ids.begin() + best, ids.end()}, b, pitch, demand, centers, out, attempts)) {
      return true;
    }
  }
  return false;
}
bool tile(std::vector<int> ids, Chunk_box box, const Point& pitch, const std::vector<Demand>& demand,
          const std::vector<Point>& centers, std::vector<Chunk_box>& out) {
  size_t attempts = 8 * ids.size() + 64;
  return tile_impl(std::move(ids), box, pitch, demand, centers, out, attempts);
}
// Logic can occupy disconnected whitespace; each hard macro needs one contiguous
// footprint. A failed guillotine cut must not put every SRAM at the same center.
void seed_dense_home(const Fp_chunk& c, const std::vector<Chunk_object>& objects, Point pitch, std::vector<Chunk_box>& seeds) {
  std::vector<int> macros;
  bool             fixed = false;
  for (int i : c.objects) {
    const auto& o = objects[i];
    seeds[i]      = o.fixed.value_or(c.box);
    if (o.macro) {
      fixed |= o.fixed.has_value();
      if (!o.fixed) {
        macros.push_back(i);
      }
    }
  }
  auto extent = [&](int i, int k) { return std::ceil((k ? objects[i].height : objects[i].width) / pitch[k]) * pitch[k]; };
  // Try shelves along both axes. Spread their spare sites between footprints,
  // leaving whitespace for halo legalization and for the logic in this home.
  for (int x : {0, 1}) {
    if (fixed || macros.empty()) {
      break;
    }
    const int    y = 1 - x;
    const double w = c.box[x + 2] - c.box[x], h = c.box[y + 2] - c.box[y];
    struct Shelf {
      double           used, height;
      std::vector<int> ids;
    };
    std::vector<Shelf> rows;
    std::stable_sort(macros.begin(), macros.end(), [&](int a, int b) {
      return std::pair{extent(a, y), extent(a, x)} > std::pair{extent(b, y), extent(b, x)};
    });
    double used = 0;
    bool   fits = true;
    for (int i : macros) {
      const double dx = extent(i, x), dy = extent(i, y);
      auto         best = rows.end();
      for (auto r = rows.begin(); r != rows.end(); ++r) {
        if (dx <= w - r->used && dy <= r->height && (best == rows.end() || r->used > best->used)) {
          best = r;
        }
      }
      if (best == rows.end()) {
        if (dx > w || dy > h - used) {
          fits = false;
          break;
        }
        rows.push_back({0, dy, {}});
        used += dy;
        best  = std::prev(rows.end());
      }
      best->used += dx;
      best->ids.push_back(i);
    }
    if (!fits) {
      continue;
    }
    const double gap_y  = pitch[y] * std::floor((h - used) / (pitch[y] * (rows.size() + 1)));
    double       bottom = c.box[y] + gap_y;
    for (const auto& r : rows) {
      const double gap_x = pitch[x] * std::floor((w - r.used) / (pitch[x] * (r.ids.size() + 1)));
      double       left  = c.box[x] + gap_x;
      for (int i : r.ids) {
        auto& b  = seeds[i];
        b[x]     = left;
        b[y]     = bottom;
        b[x + 2] = left + extent(i, x);
        b[y + 2] = bottom + extent(i, y);
        left     = b[x + 2] + gap_x;
      }
      bottom += r.height + gap_y;
    }
    return;
  }
  // A fully packed or obstructed home may have no shelf solution. Preserve
  // fixed poses and provide distinct soft starts; only downstream placement can certify legality.
  for (int i : macros) {
    for (int k = 0; k < 2; ++k) {
      const double size = extent(i, k), room = c.box[k + 2] - c.box[k] - size;
      if (room < 0) {
        continue;
      }
      const double at
          = c.box[k]
            + pitch[k]
                  * std::floor(unit(objects[i].local.empty() ? objects[i].id : objects[i].local, k ? 15485863 : 32452843) * room
                               / pitch[k]);
      seeds[i][k]     = at;
      seeds[i][k + 2] = at + size;
    }
  }
}
// Reserve whitespace for logic between fixed macro islands before cutting the chip. Sampling
// x coordinates bounds the search; the y sweep stays exact for every sampled pair.
void seed_free_chunks(const Chunk_box& core, Point pitch, std::vector<Demand>& demand, std::vector<Point>& centers) {
  std::vector<Chunk_box> occupied;
  std::vector<size_t>    free;
  for (size_t i = 0; i < demand.size(); ++i) {
    if (demand[i].fixed) {
      occupied.push_back(*demand[i].fixed);
    } else {
      free.push_back(i);
    }
  }
  if (occupied.empty()) {
    return;
  }
  std::stable_sort(free.begin(), free.end(), [&](size_t a, size_t b) { return demand[a].area > demand[b].area; });
  if (free.size() > 32) {
    free.resize(32);
  }
  constexpr size_t limit = 64;
  for (size_t id : free) {
    std::vector<double> xs{core[0], core[2]};
    for (auto& b : occupied) {
      xs.push_back(b[0]);
      xs.push_back(b[2]);
    }
    std::sort(xs.begin(), xs.end());
    xs.erase(std::unique(xs.begin(), xs.end()), xs.end());
    if (xs.size() > limit) {
      std::vector<double> sampled;
      for (size_t k = 0; k < limit; ++k) {
        sampled.push_back(xs[k * (xs.size() - 1) / (limit - 1)]);
      }
      xs.swap(sampled);
    }
    std::sort(occupied.begin(), occupied.end(), [](auto& a, auto& b) { return a[1] < b[1]; });
    Chunk_box best{};
    double    best_area = 0;
    auto      snap      = [&](double x, int k, bool up) {
      double g = (x - core[k]) / pitch[k];
      return core[k] + pitch[k] * (up ? std::ceil(g - 1e-9) : std::floor(g + 1e-9));
    };
    for (size_t l = 0; l + 1 < xs.size(); ++l) {
      for (size_t r = l + 1; r < xs.size(); ++r) {
        double x = snap(xs[l], 0, true), X = snap(xs[r], 0, false), y = core[1];
        if (X - x < demand[id].minimum[0]) {
          continue;
        }
        auto test = [&](double edge) {
          double Y = snap(edge, 1, false), floor = snap(y, 1, true);
          double area = (X - x) * (Y - floor);
          if (Y - floor >= demand[id].minimum[1] && area > best_area) {
            best_area = area;
            best      = {x, floor, X, Y};
          }
        };
        for (const auto& b : occupied) {
          if (b[0] < X && b[2] > x) {
            test(b[1]);
            y = std::max(y, b[3]);
          }
        }
        test(core[3]);
      }
    }
    if (best_area < demand[id].occupied || best_area <= 0) {
      continue;
    }
    const auto target = std::min(best_area, demand[id].area);
    double width  = std::clamp(std::sqrt(target), std::max(demand[id].minimum[0], target / (best[3] - best[1])), best[2] - best[0]);
    width         = std::min(best[2] - best[0], pitch[0] * std::ceil(width / pitch[0]));
    double height = std::min(best[3] - best[1], pitch[1] * std::ceil(std::max(target / width, demand[id].minimum[1]) / pitch[1]));
    auto   x = snap((best[0] + best[2] - width) / 2, 0, false), y = snap((best[1] + best[3] - height) / 2, 1, false);
    Chunk_box seed{x, y, x + width, y + height};
    demand[id].fixed = seed;
    centers[id]      = {(seed[0] + seed[2]) / 2, (seed[1] + seed[3]) / 2};
    occupied.push_back(seed);
  }
}
struct Guidance {
  std::map<std::pair<std::string, std::string>, double> values;
  std::set<std::pair<std::string, std::string>>         used;
  double                                                get(const std::string& kind, const std::string& id, double fallback) {
    auto key = std::pair{kind, id};
    if (auto it = values.find(key); it != values.end()) {
      used.insert(key);
      return it->second;
    }
    return fallback;
  }
};
Guidance read_guidance(const metadata::Bundle* bundle, const std::string& design) {
  Guidance g;
  if (bundle) {
    for (auto* r : bundle->select(chunk_guidance_class, design)) {
      if (!r->HasMember("targets")
          && (r->HasMember("implementations") || r->HasMember("geometry") || r->HasMember("topology")
              || r->HasMember("interface_copies"))) {
        continue;
      }
      if (!r->HasMember("targets") || !(*r)["targets"].IsArray()) {
        throw std::invalid_argument("chunk guidance needs a targets array");
      }
      for (const auto& t : (*r)["targets"].GetArray()) {
        if (!t.IsObject() || !t.HasMember("kind") || !t["kind"].IsString() || !t.HasMember("selector") || !t["selector"].IsString()
            || !t.HasMember("density") || !t["density"].IsNumber()) {
          throw std::invalid_argument("invalid chunk density target");
        }
        std::string kind = t["kind"].GetString();
        double      d    = t["density"].GetDouble();
        if (!(d > 0 && d <= 1) || (kind != "color" && kind != "chunk" && kind != "partition" && kind != "template_color")) {
          throw std::invalid_argument("invalid chunk guidance kind or density");
        }
        if (!g.values.emplace(std::pair{kind, std::string(t["selector"].GetString())}, d).second) {
          throw std::invalid_argument("duplicate chunk density selector");
        }
      }
    }
  }
  return g;
}
Memory_counts features(const Chunk_object& o) {
  return o.macro ? Memory_counts{0, o.cells, 0, o.pins + o.nets} : Memory_counts{o.cells, 0, o.pins + o.nets, 0};
}
uint64_t    add_bytes(uint64_t a, uint64_t b) { return UINT64_MAX - a < b ? UINT64_MAX : a + b; }
std::string qualified(const std::string& scope, const std::string& name) {
  return std::to_string(scope.size()) + ":" + scope + name;
}
struct Packed {
  std::vector<std::string>         scope;
  std::map<std::string, Chunk_box> boxes;
};
std::string packed_scope(const std::vector<int>& ids, const std::vector<Chunk_object>& objects) {
  const auto i = *std::min_element(ids.begin(), ids.end(), [&](int a, int b) {
    return std::tie(objects[a].macro, objects[a].id) < std::tie(objects[b].macro, objects[b].id);
  });
  return "@joined:" + qualified(objects[i].macro ? "macro" : "color", objects[i].id);
}
// The island tiling is a geometric scaffold. Remove every affordable full-edge
// seam we can, leaving repeated templates and indivisible oversized colors intact.
Packed pack_islands(const Chunk_plan& fine, const std::vector<Chunk_object>& objects) {
  auto parts = fine.chunks;
  for (size_t before = 0; before != parts.size();) {
    before = parts.size();
    for (int k : {0, 1}) {
      const int t = k ^ 1;
      std::sort(parts.begin(), parts.end(), [&](const auto& a, const auto& b) {
        return std::tie(a.box[t], a.box[t + 2], a.box[k]) < std::tie(b.box[t], b.box[t + 2], b.box[k]);
      });
      std::vector<Fp_chunk> next;
      for (auto& b : parts) {
        if (!next.empty()) {
          auto& a = next.back();
          if (a.partition == a.id && b.partition == b.id && a.box[t] == b.box[t] && a.box[t + 2] == b.box[t + 2]
              && a.box[k + 2] == b.box[k] && a.bytes <= fine.budget && b.bytes - fine.predictor.baseline <= fine.budget - a.bytes) {
            a.box[k + 2]  = b.box[k + 2];
            a.bytes      += b.bytes - fine.predictor.baseline;
            a.objects.insert(a.objects.end(), b.objects.begin(), b.objects.end());
            continue;
          }
        }
        next.push_back(std::move(b));
      }
      parts = std::move(next);
    }
  }
  Packed result;
  result.scope.resize(objects.size());
  for (const auto& c : parts) {
    std::string id = c.id;
    if (c.id == c.partition) {
      auto s = packed_scope(c.objects, objects);
      for (int i : c.objects) {
        result.scope[i] = s;
      }
      id = qualified(s, "0");
    }
    result.boxes.emplace(id, c.box);
  }
  return result;
}
}  // namespace
int Chunk_model::cell(std::string id, std::string instance, std::string module, bool macro, double w, double h,
                      std::optional<Chunk_box> fixed) {
  auto [it, added] = index.try_emplace({macro, id}, static_cast<int>(objects.size()));
  if (added) {
    auto local = !instance.empty() && id.starts_with(instance + ".") ? id.substr(instance.size() + 1) : id;
    objects.push_back({std::move(id), std::move(instance), std::move(module), std::move(local), macro});
  }
  add_member(it->second, w, h, fixed);
  return it->second;
}
int Chunk_model::add_summary(Chunk_object o) {
  if (!o.cells || o.id.empty() || !(o.width > 0 && o.height > 0 && o.area >= o.width * o.height) || !std::isfinite(o.area)
      || (o.macro && o.cells != 1)) {
    throw std::invalid_argument("invalid fp_plan object summary");
  }
  if (!index.emplace(std::pair{o.macro, o.id}, objects.size()).second) {
    throw std::invalid_argument("duplicate fp_plan object summary");
  }
  if (!o.macro) {
    o.fixed.reset();
  }
  o.local = !o.instance.empty() && o.id.starts_with(o.instance + ".") ? o.id.substr(o.instance.size() + 1) : o.id;
  objects.push_back(std::move(o));
  return objects.size() - 1;
}
void Chunk_model::add_member(int object, double w, double h, std::optional<Chunk_box> fixed) {
  auto& o = objects[object];
  ++o.cells;
  o.width   = std::max(w, o.width);
  o.height  = std::max(h, o.height);
  o.area   += w * h;
  // A few fixed tap/tie cells can span the die. They are obstacles at their actual positions,
  // not an envelope around all movable cells sharing their synthesis color.
  if (o.macro && fixed) {
    Demand d{0, {}, o.fixed};
    include(d, {0, {}, fixed});
    o.fixed = d.fixed;
  }
}
void Chunk_model::net(std::vector<int>& pins, bool place, std::optional<Point> terminal, std::string_view name,
                      std::optional<Chunk_box> terminal_bounds) {
  const size_t ordinal = net_count++;
  for (int p : pins) {
    ++objects.at(p).pins;
  }
  std::sort(pins.begin(), pins.end());
  pins.erase(std::unique(pins.begin(), pins.end()), pins.end());
  for (int p : pins) {
    ++objects[p].nets;
  }
  if (place && !pins.empty() && pins.size() <= 1000 && (pins.size() > 1 || terminal)) {
    edges.push_back({pins, terminal, std::string(name), ordinal, terminal_bounds});
  }
}
std::map<std::vector<int>, unsigned> Chunk_model::shared_nets() const {
  std::map<std::vector<int>, unsigned> result;
  for (const auto& edge : edges) {
    if (edge.nodes.size() > 1) ++result[edge.nodes];
  }
  return result;
}
// Grow each capacity cut toward the unassigned object sharing the most two-object nets.
// Each net adds two incidences regardless of how many physical pins it contains.
// The existing hierarchy order resolves ties and seeds disconnected components.
void Chunk_model::order_connected(std::vector<int>& ids, uint64_t capacity, const Chunk_predictor& predictor) const {
  const auto          order = ids;
  std::vector<size_t> rank(objects.size());
  for (size_t i = 0; i < order.size(); ++i) {
    rank[order[i]] = i;
  }
  std::vector<std::map<size_t, size_t>> neighbors(order.size());
  for (const auto& edge : edges) {
    if (edge.nodes.size() == 2) {
      const auto a = rank[edge.nodes[0]], b = rank[edge.nodes[1]];
      ++neighbors[a][b];
      ++neighbors[b][a];
    }
  }
  std::vector<bool>                   used(order.size());
  std::vector<size_t>                 affinity(order.size());
  std::set<std::pair<size_t, size_t>> frontier;  // strongest affinity first, then lowest rank
  size_t                              first = 0;
  uint64_t                            load  = 0;
  const auto priority = [](size_t weight, size_t i) { return std::pair{std::numeric_limits<size_t>::max() - weight, i}; };
  for (auto& id : ids) {
    while (used[first]) {
      ++first;
    }
    const size_t pick = frontier.empty() ? first : frontier.begin()->second;
    id                = order[pick];
    used[pick]        = true;
    const auto cost   = predictor.variable(features(objects[id]));
    if (load && (load >= capacity || cost > capacity - load)) {
      for (const auto& entry : frontier) {
        affinity[entry.second] = 0;
      }
      frontier.clear();
      load = 0;
    }
    load = add_bytes(load, cost);
    frontier.erase(priority(affinity[pick], pick));
    for (const auto& [i, weight] : neighbors[pick]) {
      if (!used[i]) {
        frontier.erase(priority(affinity[i], i));
        affinity[i] += weight;
        frontier.insert(priority(affinity[i], i));
      }
    }
  }
}
Chunk_plan Chunk_model::plan(Chunk_box core, double site, double row, uint64_t budget, double density,
                             const metadata::Bundle* input, const std::string& design, const Chunk_predictor& predictor) const {
  // Replay an explicit floorplan using the sizing decision captured when it was
  // made. New observations (including the result of that very run) guide fresh
  // exploration, not an implicit repartition of saved collateral. The topology,
  // frame, membership and guidance must still match the saved fingerprint.
  if (input && !predictor.context.empty()) {
    auto records = input->select(chunk_plan_class, design);
    if (records.size() == 1) {
      const auto& r = *records[0];
      if (r.HasMember("budget_bytes") && r["budget_bytes"].IsUint64() && r["budget_bytes"].GetUint64() == budget
          && r.HasMember("memory_prediction") && r["memory_prediction"].IsObject()) {
        const auto& m      = r["memory_prediction"];
        auto        array4 = [&](const char* key) { return m.HasMember(key) && m[key].IsArray() && m[key].Size() == 4; };
        if (m.HasMember("context") && m["context"].IsString() && predictor.context == m["context"].GetString()
            && m.HasMember("samples") && m["samples"].IsUint() && m.HasMember("baseline_bytes") && m["baseline_bytes"].IsUint64()
            && m.HasMember("extrapolating") && m["extrapolating"].IsBool() && array4("bytes_per_unit") && array4("counts")
            && array4("observed_max_counts")) {
          Chunk_predictor saved;
          saved.context  = predictor.context;
          saved.samples  = m["samples"].GetUint();
          saved.baseline = m["baseline_bytes"].GetUint64();
          Memory_counts original{};
          for (unsigned j = 0; j < 4; ++j) {
            auto& cost = m["bytes_per_unit"][j];
            if (!cost.IsNumber() || !std::isfinite(cost.GetDouble()) || cost.GetDouble() < 0 || !m["counts"][j].IsUint64()
                || !m["observed_max_counts"][j].IsUint64()) {
              throw std::invalid_argument("invalid saved fp_plan memory prediction");
            }
            saved.rate[j]         = cost.GetDouble();
            original[j]           = m["counts"][j].GetUint64();
            saved.observed_max[j] = m["observed_max_counts"][j].GetUint64();
          }
          if (!m["extrapolating"].GetBool()) {
            saved.observed.push_back(original);
          }
          if (auto replay = plan_with(core, site, row, budget, density, input, design, saved, true)) {
            return std::move(*replay);
          }
        }
      }
    }
  }
  return std::move(*plan_with(core, site, row, budget, density, input, design, predictor, false));
}
std::optional<Chunk_plan> Chunk_model::plan_with(Chunk_box core, double site, double row, uint64_t budget, double density,
                                                 const metadata::Bundle* input, const std::string& design,
                                                 const Chunk_predictor& predictor, bool only_saved) const {
  if (!(density > 0 && density <= 1 && site > 0 && row > 0 && core[2] > core[0] && core[3] > core[1]) || objects.empty()) {
    throw std::invalid_argument("invalid fp_plan frame, density or empty design");
  }
  const auto all_edits = read_chunk_edits(input, design);
  // The latest depth replaces the previous one for the same physical seam. Keeping
  // the original intent history lets undo restore the prior profile.
  std::map<std::pair<std::string, std::string>, const Chunk_edit*> steps;
  for (const auto& edit : all_edits) {
    if (!edit.step.empty()) {
      steps[std::minmax(edit.step, edit.peer)] = &edit;
    }
  }
  auto edits = all_edits;
  std::erase_if(edits, [](const auto& op) { return !op.step.empty(); });
  const auto    home_edits = read_home_edits(input, design);
  const auto    copies     = read_interface_copies(input, design);
  Memory_counts counts{};
  uint64_t      whole    = predictor.baseline;
  const auto    estimate = [&](const Chunk_object& o) { return predictor.variable(features(o)); };
  for (const auto& o : objects) {
    auto f = features(o);
    for (int i = 0; i < 4; ++i) {
      counts[i] += f[i];
    }
    whole = add_bytes(whole, estimate(o));
  }
  const uint64_t capacity = budget > predictor.baseline ? budget - predictor.baseline : 0;
  const bool     single   = target_homes <= 1 && (!budget || whole <= budget || !capacity);
  auto           solve    = [&](bool          macro_islands,
                                const Packed* packed     = nullptr,
                                bool          scaffold   = false,
                                bool          exact_only = false) -> std::optional<Chunk_plan> {
    auto       hints = read_guidance(input, design);
    Chunk_plan p;
    p.predictor     = predictor;
    p.counts        = counts;
    p.macro_islands = macro_islands;
    p.budget        = budget;
    const auto n    = objects.size();
    const bool pack_hierarchy = (pack_scopes || target_homes > 0) && !single && !macro_islands;
    p.seeds.resize(n);
    p.density.resize(n);
    p.density_source.resize(n);
    p.home.resize(n);
    std::vector<Point>                      centers(n), anchors(n);
    std::vector<Demand>                     demands(n);
    std::map<std::string, std::vector<int>> scopes;
    for (size_t i = 0; i < n; ++i) {
      const auto& o     = objects[i];
      const auto  scope = packed && !packed->scope[i].empty()              ? packed->scope[i]
                                       : macro_islands && o.macro && o.instance.empty() ? "@physical:" + o.id
                                                                                        : o.instance;
      scopes[(single || pack_hierarchy) ? "" : scope].push_back(i);
      const double inherited = o.module.empty() ? density : hints.get("template_color", qualified(o.module, o.local), density);
      p.density[i]        = o.macro ? 1 : hints.get("color", o.id, inherited);
      p.density_source[i] = o.macro                                  ? "footprint"
                                         : hints.values.contains({"color", o.id}) ? "color"
                                         : hints.values.contains({"template_color", qualified(o.module, o.local)}) ? "template_color"
                                                                                                                   : "design";
      demands[i]          = {
          o.area / p.density[i],
          {o.width, o.height},
          o.fixed
      };
      for (int k = 0; k < 2; ++k) {
        anchors[i][k] = o.fixed ? ((*o.fixed)[k] + (*o.fixed)[k + 2]) / 2
                                             : core[k] + (core[k + 2] - core[k]) * unit(o.id, k ? 15485863 : 32452843);
      }
    }
    // A trivial wrapper has little internal layout to reuse. Absorb it into the root cut
    // unless its family is a significant repeated array. Preserve all original color IDs.
    std::map<std::string, size_t> family_cells;
    size_t                        total_cells = 0;
    for (const auto& o : objects) {
      family_cells[o.module] += o.cells;
      total_cells            += o.cells;
    }
    std::vector<int> glue;
    for (auto it = scopes.begin(); it != scopes.end();) {
      const auto& ids = it->second;
      const auto& o   = objects[ids.front()];
      if (!it->first.empty() && (!packed || packed->scope[ids.front()].empty()) && ids.size() == 1 && !o.macro
          && (o.cells == 1 || (o.cells <= 16 && family_cells[o.module] <= total_cells / 100))) {
        glue.push_back(ids.front());
        it = scopes.erase(it);
      } else {
        ++it;
      }
    }
    if (!glue.empty()) {
      auto& root = scopes[""];
      root.insert(root.end(), glue.begin(), glue.end());
    }
    // Give a split instance a shared starting neighborhood. Independent random anchors
    // per color scatter the pieces of a core across the chip before relaxation even
    // begins. Work from the first differing scope component; a common top prefix
    // carries no placement information. These are soft seeds, never fences.
    if (!single && !macro_islands && scopes.size() > 1) {
      auto first = scopes.upper_bound("");
      if (first != scopes.end() && first->first != scopes.rbegin()->first) {
        const std::string& lo   = first->first;
        const std::string& hi   = scopes.rbegin()->first;
        size_t             skip = 0;
        for (size_t k = 0; k < std::min(lo.size(), hi.size()) && lo[k] == hi[k]; ++k) {
          if (lo[k] == '.') {
            skip = k + 1;
          }
        }
        for (auto it = first; it != scopes.end(); ++it) {
          const std::string_view scope = it->first;
          Point                  center{.5, .5};
          double                 span = 1.;
          for (size_t k = skip; k < scope.size();) {
            const auto dot    = scope.find('.', k);
            const auto prefix = scope.substr(0, dot);
            for (int axis = 0; axis < 2; ++axis) {
              center[axis] += .72 * span * (unit(prefix, axis ? 15485863 : 32452843) - .5);
            }
            span *= .28;
            if (dot == std::string_view::npos) {
              break;
            }
            k = dot + 1;
          }
          for (int i : it->second) {
            if (demands[i].fixed) {
              continue;
            }
            for (int axis = 0; axis < 2; ++axis) {
              const auto width = core[axis + 2] - core[axis];
              anchors[i][axis] = core[axis] + width * center[axis] + span * (anchors[i][axis] - (core[axis] + .5 * width));
            }
          }
        }
      }
    }
    // Bounded hyperedge relaxation on colors/macros. One barycenter per crossing net, no pin clique, RC or STA.
    centers = anchors;
    for (int round = 0; round < 12; ++round) {
      auto                next = anchors;
      std::vector<double> count(n, 1);
      for (const auto& e : edges) {
        Point sum = e.terminal.value_or(Point{});
        for (int i : e.nodes) {
          for (int k = 0; k < 2; ++k) {
            sum[k] += centers[i][k];
          }
        }
        for (int i : e.nodes) {
          ++count[i];
          for (int k = 0; k < 2; ++k) {
            next[i][k] += sum[k] / (e.nodes.size() + bool(e.terminal));
          }
        }
      }
      for (size_t i = 0; i < n; ++i) {
        if (!objects[i].fixed) {
          for (int k = 0; k < 2; ++k) {
            centers[i][k] = next[i][k] / count[i];
          }
        }
      }
    }
    std::vector<Point>  chunk_centers;
    std::vector<Demand> chunk_demand;
    // The home partition must not change when replay supplies refined physical macro or IO poses.
    // Order root colors by connectivity alone; use the anchored coordinates only for the spatial cut.
    std::vector<double> logical_order(n), logical_seed(n), degree(n, 1);
    for (size_t i = 0; i < n; ++i) {
      logical_order[i] = logical_seed[i] = unit(objects[i].id, 32452843);
    }
    for (const auto& edge : edges) {
      double center = 0;
      for (int i : edge.nodes) {
        center += logical_seed[i];
      }
      center /= edge.nodes.size();
      for (int i : edge.nodes) {
        logical_order[i] += center;
        ++degree[i];
      }
    }
    for (size_t i = 0; i < n; ++i) {
      logical_order[i] /= degree[i];
    }
    // Canonical local order makes the cut reusable independently of instance placement and external nets.
    // Only reuse a partition when the local object identities and resource counts match.
    using Shape = std::vector<std::tuple<bool, std::string, uint64_t>>;
    std::map<std::pair<std::string, Shape>, std::vector<size_t>>   partitions;
    // Size every copy with the largest instance interface of that module, so an extra external
    // fanout on one core cannot silently produce a different partition for its siblings.
    std::map<std::tuple<std::string, bool, std::string>, uint64_t> max_cost;
    for (const auto& o : objects) {
      if (!o.module.empty()) {
        auto& cost = max_cost[{o.module, o.macro, o.local}];
        cost       = std::max(cost, estimate(o));
      }
    }
    for (auto& [scope, ids] : scopes) {
      std::sort(ids.begin(), ids.end(), [&](int i, int j) {
        if (pack_hierarchy) {
          return std::tie(objects[i].instance, objects[i].id, objects[i].macro)
                 < std::tie(objects[j].instance, objects[j].id, objects[j].macro);
        }
        return std::tie(objects[i].local, objects[i].macro, objects[i].id)
               < std::tie(objects[j].local, objects[j].macro, objects[j].id);
      });
      if (!pack_hierarchy && scope.empty() && budget
          && std::none_of(ids.begin(), ids.end(), [&](int i) { return objects[i].macro; })) {
        // For unstructured logic, put objects near one another in the quotient placement in the same chunk.
        std::stable_sort(ids.begin(), ids.end(), [&](int i, int j) {
          return std::tie(logical_order[i], objects[i].id) < std::tie(logical_order[j], objects[j].id);
        });
      }
      if (pack_hierarchy && connectivity) {
        order_connected(ids, capacity, predictor);
      }
      Shape shape;
      for (int i : ids) {
        const auto& o = objects[i];
        shape.emplace_back(o.macro,
                           o.local,
                           (packed && !packed->scope[i].empty()) || scope.empty() || o.module.empty()
                               ? estimate(o)
                               : max_cost.at({o.module, o.macro, o.local}));
      }
      const auto module = (packed && !packed->scope[ids[0]].empty()) || scope.empty() ? std::string{} : objects[ids[0]].module;
      auto [it, added] = partitions.try_emplace({module, std::move(shape)});
      if (added && target_homes && pack_hierarchy) {
        // Keep contiguous hierarchy together. Near the desired load, prefer ending at a shallower instance boundary.
        std::vector<uint64_t> prefix(ids.size() + 1);
        std::vector<double> area_prefix(ids.size() + 1);
        for (size_t j = 0; j < ids.size(); ++j) {
          prefix[j + 1]      = add_bytes(prefix[j], estimate(objects[ids[j]]));
          area_prefix[j + 1] = area_prefix[j] + demands[ids[j]].area;
        }
        const uint64_t limit = budget ? capacity : std::numeric_limits<uint64_t>::max();
        for (size_t begin = 0; begin < ids.size();) {
          const size_t bins
              = std::min(ids.size() - begin,
                         std::max<size_t>(1, target_homes > int(it->second.size()) ? target_homes - it->second.size() : 1));
          double goal = std::min<double>(limit, double(prefix.back() - prefix[begin]) / bins);
          const double area_goal = (area_prefix.back() - area_prefix[begin]) / bins;
          double best = INFINITY;
          size_t cut  = begin + 1;
          for (size_t end = begin + 1; end <= ids.size() - (bins - 1); ++end) {
            const double cost = prefix[end] - prefix[begin];
            if (cost > limit && end > begin + 1) {
              break;
            }
            unsigned depth = 0;
            if (end < ids.size()) {
              const auto& l = objects[ids[end - 1]].id;
              const auto& r = objects[ids[end]].id;
              for (size_t pos = 0; pos < std::min(l.size(), r.size()) && l[pos] == r[pos]; ++pos) {
                depth += l[pos] == '.';
              }
            }
            double load = cost / std::max(1., goal);
            if (area_goal > 0) {
              load = std::lerp(load, (area_prefix[end] - area_prefix[begin]) / area_goal, area_weight);
            }
            double loss = std::abs(load - 1.) + .12 * depth;
            if (bins == 1 && end == ids.size()) {
              loss = -1;
            }
            if (loss < best) {
              best = loss;
              cut  = end;
            }
            if (load > 1.5) {
              break;
            }
          }
          it->second.push_back(cut);
          begin = cut;
        }
      } else if (added) {
        uint64_t load = 0;
        for (size_t j = 0; j < ids.size(); ++j) {
          uint64_t cost = std::get<2>(it->first.second[j]);
          if (!single && load && (load >= capacity || cost > capacity - load)) {
            it->second.push_back(j);
            load = 0;
          }
          load = add_bytes(load, cost);
        }
        it->second.push_back(ids.size());
      }
      size_t start = 0;
      for (auto end : it->second) {
        Fp_chunk c;
        c.bytes = predictor.baseline;
        c.objects.assign(ids.begin() + start, ids.begin() + end);
        std::string slot = std::to_string(start);
        c.id             = qualified(scope, slot);
        c.partition      = module.empty() ? c.id : qualified(module, slot);
        p.chunks.push_back(std::move(c));
        start = end;
      }
    }
    Home_history history;
    if (!scaffold && !home_edits.empty()) {
      // A fixed-macro island can use different IDs from the regular tiling.
      // Probe the matching scaffold before diagnosing an absent selector.
      bool source_exists
          = std::any_of(p.chunks.begin(), p.chunks.end(), [&](const auto& c) { return c.id == home_edits.front().first; });
      if (!source_exists && (exact_only || (!macro_islands && std::any_of(objects.begin(), objects.end(), [](const auto& o) {
                               return o.fixed.has_value();
                             })))) {
        return {};
      }
      history = repartition(p.chunks, home_edits, objects, core, {site, row});
    }
    for (size_t j = 0; j < p.chunks.size(); ++j) {
      auto& c         = p.chunks[j];
      c.bytes         = predictor.baseline;
      c.density       = hints.get("chunk", c.id, hints.get("partition", c.partition, density));
      c.target_source = hints.values.contains({"chunk", c.id})              ? "chunk"
                                     : hints.values.contains({"partition", c.partition}) ? "partition"
                                                                                         : "design";
      Point  xy{};
      Demand d;
      double logic = 0, macro = 0, total = 0;
      for (int i : c.objects) {
        p.home[i]     = j;
        const auto& o = objects[i];
        if (!o.macro && p.density_source[i] == "design") {
          p.density[i]        = c.density;
          p.density_source[i] = c.target_source;
          demands[i].area     = o.area / c.density;
        }
        c.bytes = add_bytes(c.bytes, estimate(o));
        include(d, demands[i]);
        // The independent solver expands a movable macro by its halo before legalization.
        // Keep seed rectangles physical, but reserve the complete envelope in the home.
        if (o.worker_halo[0] || o.worker_halo[1]) {
          const double w = o.width + 2 * o.worker_halo[0], h = o.height + 2 * o.worker_halo[1];
          d.minimum[0]  = std::max(d.minimum[0], w);
          d.minimum[1]  = std::max(d.minimum[1], h);
          d.occupied   += w * h - o.width * o.height;
        }
        (o.macro ? macro : logic) += o.area;
        total                     += o.area;
        for (int k = 0; k < 2; ++k) {
          xy[k] += centers[i][k] * o.area;
        }
      }
      d.occupied += total;
      c.reserved = d.area = std::max(d.area, macro + logic / c.density);
      c.allowance         = budget ? std::max(budget, c.bytes) : 0;
      chunk_centers.push_back({xy[0] / total, xy[1] / total});
      chunk_demand.push_back(d);

      if (!scaffold && !home_edits.empty() && budget && c.objects.size() > 1 && c.bytes > budget) {
        throw std::invalid_argument("chunk topology: " + c.id + " predicts " + std::to_string(c.bytes) + " bytes, above budget "
                                    + std::to_string(budget));
      }
    }
    // Preserve retired selectors in the authored metadata for undo. A name
    // that never existed is still diagnosed, even in a structural edit.
    std::map<std::string, std::string> active_homes;
    std::set<std::string>              active_partitions, retired;
    if (!history.empty()) {
      for (const auto& c : p.chunks) {
        active_homes[c.id] = c.partition;
        active_partitions.insert(c.partition);
      }
      for (const auto& [family, ids] : history) {
        if (!active_partitions.contains(family) && hints.values.contains({"partition", family})) {
          hints.used.emplace("partition", family);
        }
        for (const auto& id : ids) {
          if (!active_homes.contains(id)) {
            retired.insert(id);
            if (hints.values.contains({"chunk", id})) {
              hints.used.emplace("chunk", id);
            }
          }
        }
      }
    }
    auto validate_targets = [&] {
      if (hints.values.size() != hints.used.size()) {
        for (const auto& [key, value] : hints.values) {
          if (!hints.used.contains(key)) {
            throw std::invalid_argument("unknown chunk guidance selector " + key.first + ": " + key.second);
          }
        }
      }
    };
    // Hash logical/resource inputs in canonical object order; fixed macro poses are separately replayed physical state.
    uint64_t key  = 14695981039346656037ULL;
    auto     mix  = [&](uint64_t v) { key = (key ^ v) * 1099511628211ULL; };
    auto     text = [&](const std::string& s) {
      mix(s.size());
      key = livehd::hash_util::fnv1a64(s, key);
    };
    for (double v : core) {
      mix(std::bit_cast<uint64_t>(v));
    }
    mix(std::bit_cast<uint64_t>(site));
    mix(std::bit_cast<uint64_t>(row));
    mix(budget);
    if (input) {
      for (auto* rec : input->select(chunk_guidance_class, design)) {
        if (rec->HasMember("topology") && !home_edits.empty()) {
          text(metadata::canonical((*rec)["topology"]));
        }
        if (rec->HasMember("interface_copies") && !(*rec)["interface_copies"].Empty()) {
          text(metadata::canonical((*rec)["interface_copies"]));
        }
        if (rec->HasMember("geometry") && !all_edits.empty()) {
          text(metadata::canonical((*rec)["geometry"]));
        }
        if (rec->HasMember("implementations") && (*rec)["implementations"].IsArray() && !(*rec)["implementations"].Empty()) {
          text(metadata::canonical((*rec)["implementations"]));
        }
      }
    }
    if (!predictor.context.empty()) {
      text(predictor.context);
    }
    for (const auto& c : p.chunks) {
      text(c.id);
      text(c.partition);
      mix(std::bit_cast<uint64_t>(c.density));
      for (int i : c.objects) {
        const auto& o = objects[i];
        text(o.id);
        mix(o.macro);
        mix(o.cells);
        mix(o.pins);
        mix(o.nets);
        if (o.worker_halo[0] || o.worker_halo[1]) {
          mix(std::bit_cast<uint64_t>(o.worker_halo[0]));
          mix(std::bit_cast<uint64_t>(o.worker_halo[1]));
        }
        for (double v : {o.area, o.width, o.height, p.density[i]}) {
          mix(std::bit_cast<uint64_t>(v));
        }
      }
    }
    // Detect a rewire even when it leaves all the pin counts unchanged. The fingerprint is independent of solver node order.
    std::vector<uint64_t> object_keys(n);
    for (size_t i = 0; i < n; ++i) {
      auto prior = key;
      key        = 14695981039346656037ULL;
      text(objects[i].id);
      mix(objects[i].macro);
      object_keys[i] = key;
      key            = prior;
    }
    uint64_t net_hash = 0;
    for (const auto& e : edges) {
      uint64_t sum = 0;
      for (int i : e.nodes) {
        sum += object_keys[i];
      }
      if (e.terminal) {
        // A moved IO can change the chunk touching it, even if all cell counts
        // and endpoints are identical. Keep the edge set order-independent.
        const auto salt = key;
        key             = 14695981039346656037ULL;
        text(e.name);
        if (e.terminal_bounds) {
          for (double xy : *e.terminal_bounds) {
            mix(std::bit_cast<uint64_t>(xy));
          }
        }
        mix(std::bit_cast<uint64_t>((*e.terminal)[0]));
        mix(std::bit_cast<uint64_t>((*e.terminal)[1]));
        sum ^= key;
        key  = salt;
      }
      net_hash += std::rotl(sum ^ (sum >> 27), int(e.nodes.size() % 64));
    }
    mix(net_hash);
    mix(edges.size());
    p.input_key            = key;
    // An explicit saved plan is the pre-refinement home partition. A companion placement.state may already
    // have moved macros across its boundaries. Retain that partition when the design and guidance still match.
    auto compatible_record = [&](const rapidjson::Value& r) {
      if (!r.HasMember("input_key") || !r["input_key"].IsUint64() || r["input_key"].GetUint64() != p.input_key) {
        return false;
      }
      if (!r.HasMember("chunks") || !r["chunks"].IsArray() || r["chunks"].Size() != p.chunks.size()) {
        throw std::invalid_argument("invalid saved fp_plan chunks");
      }
      auto read_box = [&](const rapidjson::Value& v, const Chunk_box& outer, const char* field = "box_dbu") {
        if (!v.IsObject() || !v.HasMember(field) || !v[field].IsArray() || v[field].Size() != 4) {
          throw std::invalid_argument("invalid saved fp_plan box");
        }
        Chunk_box b;
        for (unsigned k = 0; k < 4; ++k) {
          const auto& x = v[field][k];
          if (!x.IsNumber() || !std::isfinite(x.GetDouble())) {
            throw std::invalid_argument("invalid fp_plan coordinate");
          }
          b[k] = x.GetDouble();
        }
        for (int k = 0; k < 2; ++k) {
          if (b[k] < outer[k] || b[k + 2] > outer[k + 2] || b[k] >= b[k + 2]) {
            throw std::invalid_argument("saved fp_plan box outside parent");
          }
          const double pitch = k ? row : site;
          for (int side : {k, k + 2}) {
            if (std::abs(std::remainder(b[side] - core[k], pitch)) > 1e-6) {
              throw std::invalid_argument("saved fp_plan box is off the placement lattice");
            }
          }
        }
        return b;
      };
      // Sweep chunk edges: children may share an edge, but never overlap or leave a hole.
      using Event = std::tuple<double, int, double, double>;
      std::vector<Event> events;
      double             covered = 0;
      for (unsigned j = 0; j < p.chunks.size(); ++j) {
        auto&       c  = p.chunks[j];
        const auto& in = r["chunks"][j];
        if (!in.IsObject() || !in.HasMember("id") || !in["id"].IsString() || c.id != in["id"].GetString()
            || !in.HasMember("members") || !in["members"].IsArray() || in["members"].Size() != c.objects.size()) {
          throw std::invalid_argument("saved fp_plan assignment mismatch");
        }
        c.box = read_box(in, core);
        if (!edits.empty()) {
          c.reference_box = read_box(in, core, "reference_box_dbu");
        }
        const auto home = c.reference_box.value_or(c.box);
        events.emplace_back(home[0], 1, home[1], home[3]);
        events.emplace_back(home[2], 0, home[1], home[3]);
        covered += (home[2] - home[0]) * (home[3] - home[1]);
        for (unsigned k = 0; k < c.objects.size(); ++k) {
          const auto& m = in["members"][k];
          int         i = c.objects[k];
          if (!m.IsObject() || !m.HasMember("id") || !m["id"].IsString() || objects[i].id != m["id"].GetString()
              || !m.HasMember("macro") || !m["macro"].IsBool() || m["macro"].GetBool() != objects[i].macro) {
            throw std::invalid_argument("saved fp_plan color or macro does not match its home");
          }
          p.seeds[i] = read_box(m, c.box);
          if (objects[i].width > p.seeds[i][2] - p.seeds[i][0] || objects[i].height > p.seeds[i][3] - p.seeds[i][1]) {
            throw std::invalid_argument("saved fp_plan slot is smaller than its object");
          }
        }
      }
      std::sort(events.begin(), events.end());
      std::map<double, double> active;
      for (auto [x, add, y, h] : events) {
        if (!add) {
          active.erase(y);
          continue;
        }
        auto it = active.lower_bound(y);
        if ((it != active.end() && it->first < h) || (it != active.begin() && std::prev(it)->second > y)) {
          throw std::invalid_argument("overlapping saved fp_plan chunks");
        }
        active.emplace_hint(it, y, h);
      }
      if (std::abs(covered - (core[2] - core[0]) * (core[3] - core[1])) > std::max(1., covered) * 1e-12) {
        throw std::invalid_argument("saved fp_plan chunks leave uncovered area");
      }
      return true;
    };
    bool replayed = false;
    if (input) {
      auto records = input->select(chunk_plan_class, design);
      if (records.size() > 1) {
        throw std::invalid_argument("multiple fp_plan records for the design");
      }
      replayed = !records.empty() && compatible_record(*records.front());
    }
    if (exact_only && !replayed) {
      return {};
    }
    bool kept_frame = false;
    if (!replayed) {
      std::vector<int> ids(p.chunks.size());
      std::iota(ids.begin(), ids.end(), 0);
      std::vector<Chunk_box> boxes(ids.size());
      if (macro_islands) {
        seed_free_chunks(core, {site, row}, chunk_demand, chunk_centers);
      }
      const bool have_boxes = home_edits.empty() && packed && std::all_of(p.chunks.begin(), p.chunks.end(), [&](const auto& c) {
                                return packed->boxes.contains(c.id);
                              });
      // The scaffold already proved these cuts. Retile only if guidance requests new space.
      if (!scaffold && !home_edits.empty() && !home_edits.back().before.empty()) {
        const auto& op = home_edits.back();
        int         j = -1, child = -1;
        for (size_t i = 0; i < p.chunks.size(); ++i) {
          const auto& c = p.chunks[i];
          if (c.id == op.first) {
            j = i;
          }
          if (c.id == op.second) {
            child = i;
          }
          auto b           = op.before.at(c.id == op.second && op.kind == Home_edit::split ? op.first : c.id);
          boxes[i]         = b;
          chunk_centers[i] = {(b[0] + b[2]) / 2, (b[1] + b[3]) / 2};
        }
        if (op.kind == Home_edit::transfer) {
          kept_frame = true;
        } else if (op.kind == Home_edit::split) {
          kept_frame = tile({j, child}, boxes[j], {site, row}, chunk_demand, chunk_centers, boxes);
        } else {
          auto      a = boxes[j], b = op.before.at(op.second);
          Chunk_box u{std::min(a[0], b[0]), std::min(a[1], b[1]), std::max(a[2], b[2]), std::max(a[3], b[3])};
          auto      area   = [](const auto& r) { return (r[2] - r[0]) * (r[3] - r[1]); };
          kept_frame       = std::abs(area(a) + area(b) - area(u)) <= 1e-12 * area(u);
          boxes[j]         = u;
          chunk_centers[j] = {(u[0] + u[2]) / 2, (u[1] + u[3]) / 2};
        }
        if (kept_frame) {
          for (size_t i = 0; i < boxes.size(); ++i) {
            const auto& b = boxes[i];
            const auto& d = chunk_demand[i];
            if ((b[2] - b[0]) * (b[3] - b[1]) < d.occupied) {
              kept_frame = false;
            }
            for (int k = 0; k < 2; ++k) {
              if (b[k + 2] - b[k] < d.minimum[k] || (d.fixed && ((*d.fixed)[k] < b[k] || (*d.fixed)[k + 2] > b[k + 2]))) {
                kept_frame = false;
              }
            }
          }
        }
      }
      if (kept_frame
          || (!(have_boxes && hints.values.empty()) && tile(ids, core, {site, row}, chunk_demand, chunk_centers, boxes))) {
        // All boxes were set by tile.
      } else if (have_boxes) {
        for (size_t j = 0; j < boxes.size(); ++j) {
          boxes[j] = packed->boxes.at(p.chunks[j].id);
        }
      } else {
        return {};
      }
      if (target_homes && !kept_frame && !have_boxes && ids.size() > 1 && home_edits.empty()) {
        auto wire_cost = [&](const std::vector<Chunk_box>& frame) {
          double cost = 0;
          for (const auto& e : edges) {
            if (e.nodes.empty()) {
              continue;
            }
            Chunk_box b   = {INFINITY, INFINITY, -INFINITY, -INFINITY};
            auto      add = [&](Point xy) {
              for (int k = 0; k < 2; ++k) {
                b[k]     = std::min(b[k], xy[k]);
                b[k + 2] = std::max(b[k + 2], xy[k]);
              }
            };
            for (int obj : e.nodes) {
              const auto& f = frame[p.home[obj]];
              add({(f[0] + f[2]) / 2, (f[1] + f[3]) / 2});
            }
            if (e.terminal) {
              add(*e.terminal);
            }
            cost += b[2] - b[0] + b[3] - b[1];
          }
          return cost;
        };
        p.shape_candidates.emplace_back(wire_cost(boxes), boxes);
        // Screen mirrored home assignments and alternative area splits on the same physical lattice.
        for (int mask = 0; mask < 4; ++mask) {
          for (double bias : {.9, 1., 1.1}) {
            auto centers_try = chunk_centers;
            auto demand_try  = chunk_demand;
            for (size_t j = 0; j < ids.size(); ++j) {
              for (int k = 0; k < 2; ++k) {
                if (mask & (1 << k)) {
                  centers_try[j][k] = -centers_try[j][k];
                }
              }
              demand_try[j].area *= j % 2 ? bias : 1 / bias;
            }
            auto trial = boxes;
            if (tile(ids, core, {site, row}, demand_try, centers_try, trial)) {
              double cost = wire_cost(trial);
              p.shape_candidates.emplace_back(cost, std::move(trial));
            }
          }
        }
        std::sort(p.shape_candidates.begin(), p.shape_candidates.end());
        auto distinct = std::unique(p.shape_candidates.begin(), p.shape_candidates.end());
        p.shape_candidates.erase(distinct, p.shape_candidates.end());
        if (shape_rank >= p.shape_candidates.size()) {
          throw std::invalid_argument("requested chunk shape rank exceeds distinct feasible frames");
        }
        boxes = p.shape_candidates[shape_rank].second;
      }
      for (size_t j = 0; j < boxes.size(); ++j) {
        p.chunks[j].box = boxes[j];
      }
    }
    if (scaffold) {
      return p;
    }
    struct Piece {
      std::vector<int>     nodes;
      std::optional<Point> port;
    };
    std::vector<std::vector<Piece>>  pieces(p.chunks.size());
    std::vector<Interface_net>       wires;
    // Track which local seed gets the final, standardized boundary contact.
    std::vector<std::vector<size_t>> piece_ids;
    for (const auto& edge : edges) {
      std::map<int, std::vector<int>> homes;
      for (int i : edge.nodes) {
        homes[p.home[i]].push_back(i);
      }
      if (homes.size() == 1 && !edge.terminal) {
        if (!replayed) {
          pieces[homes.begin()->first].push_back({std::move(homes.begin()->second), {}});
        }
        continue;
      }
      Interface_net       wire{edge.name, edge.net_index, edge.terminal, {}, edge.terminal_bounds};
      std::vector<size_t> indices;
      for (auto& [j, ids] : homes) {
        std::string role;
        const auto& scope = objects[ids.front()].instance;
        if (!scope.empty() && !edge.terminal) {
          // Prefer the real driver name when local to this instance. Color-only summaries
          // have no pin names: match their local/remote endpoints, never a global net ordinal.
          if (edge.name.starts_with(scope + ".")) {
            role = "pin:" + edge.name.substr(scope.size() + 1);
          } else if (edge.nodes.size() <= 32) {
            std::vector<std::string> ends;
            for (int i : edge.nodes) {
              const auto& o = objects[i];
              ends.push_back(std::string(p.home[i] == j        ? "in:"
                                         : o.instance == scope ? "local:"
                                                               : "remote:")
                             + qualified(o.module, o.local) + (o.macro ? ":macro" : ":logic"));
            }
            std::sort(ends.begin(), ends.end());
            for (const auto& e : ends) {
              role += qualified(e, "");
            }
          }
        }
        wire.contacts.push_back({j, std::move(role), {}});
        if (!replayed) {
          indices.push_back(pieces[j].size());
          pieces[j].push_back({std::move(ids), {}});
        }
      }
      wires.push_back(std::move(wire));
      piece_ids.push_back(std::move(indices));
    }
    auto rules       = read_template_rules(input, design);
    auto valid_edits = edits;
    if (!history.empty()) {
      std::erase_if(rules,
                    [&](const auto& entry) { return history.contains(entry.first) && !active_partitions.contains(entry.first); });
      for (auto& [family, rule] : rules) {
        std::set<std::string> bound;
        std::erase_if(rule.instances, [&](const auto& kv) {
          return history.contains(family) && history.at(family).contains(kv.first)
                 && (!active_homes.contains(kv.first) || active_homes.at(kv.first) != family);
        });
        for (const auto& [id, name] : rule.instances) {
          bound.insert(name);
        }
        std::erase_if(rule.forms, [&](const auto& kv) { return !bound.contains(kv.first); });
      }
      for (auto& e : valid_edits) {
        std::erase_if(e.locks, [&](const auto& id) { return retired.contains(id); });
      }
      std::erase_if(valid_edits, [&](const auto& e) {
        return retired.contains(e.move) || retired.contains(e.all_but) || retired.contains(e.reshape)
               || (e.all_but.empty() && e.move.empty() && e.reshape.empty() && e.locks.empty());
      });
    }
    // Keep a reference frame even if all geometry rules are currently dormant.
    if (!edits.empty() && valid_edits.empty()) {
      valid_edits.emplace_back();
    }
    std::vector<Chunk_box> saved_geometry;
    if (replayed && !edits.empty()) {
      for (const auto& c : p.chunks) {
        saved_geometry.push_back(c.box);
      }
    }
    refine_templates(p,
                     wires,
                     objects,
                     rules,
                     core,
                     {site, row},
                     !replayed && !macro_islands && !kept_frame,
                     valid_edits,
                     copies,
                     contact_keys);
    for (size_t i = 0; i < saved_geometry.size(); ++i) {
      if (p.chunks[i].box != saved_geometry[i]) {
        throw std::invalid_argument("saved chunk box disagrees with its geometry edits");
      }
    }
    for (size_t n_wire = 0; n_wire < wires.size(); ++n_wire) {
      const auto& w = wires[n_wire];
      for (size_t i = 0; i < w.contacts.size(); ++i) {
        const auto& tap = w.contacts[i];
        p.chunks[tap.home].ports.push_back({w.name, tap.xy, w.index, tap.role, tap.copy_source_net});
        if (!replayed) {
          pieces[tap.home][piece_ids[n_wire][i]].port = tap.xy;
        }
      }
    }
    if (!replayed) {
      std::vector<Point> local(n);
      for (size_t j = 0; j < p.chunks.size(); ++j) {
        auto&       c = p.chunks[j];
        const auto& b = c.box;
        for (int i : c.objects) {
          for (int k = 0; k < 2; ++k) {
            local[i][k]
                = objects[i].fixed ? centers[i][k] : b[k] + (b[k + 2] - b[k]) * unit(objects[i].local, k ? 15485863 : 32452843);
          }
        }
      }
      // Timing-free macro planning inside each rectangle: optimize only the color/macro nodes
      // against local nets and the chunk's boundary contacts, then give the cell placer a spatial seed.
      auto                base = local;
      std::vector<double> count(n);
      for (int round = 0; round < 8; ++round) {
        auto next = base;
        std::fill(count.begin(), count.end(), 1.);
        for (const auto& group : pieces) {
          for (const auto& piece : group) {
            Point sum = piece.port.value_or(Point{});
            for (int i : piece.nodes) {
              for (int k = 0; k < 2; ++k) {
                sum[k] += local[i][k];
              }
            }
            for (int i : piece.nodes) {
              ++count[i];
              for (int k = 0; k < 2; ++k) {
                next[i][k] += sum[k] / (piece.nodes.size() + bool(piece.port));
              }
            }
          }
        }
        for (size_t i = 0; i < n; ++i) {
          if (objects[i].fixed) {
            next[i] = local[i];
          } else {
            for (int k = 0; k < 2; ++k) {
              next[i][k] /= count[i];
            }
          }
        }
        relax_shared_colors(p, objects, local, next);
        local.swap(next);
      }
      for (const auto& c : p.chunks) {
        if (!tile(c.objects, c.box, {site, row}, demands, local, p.seeds)) {
          seed_dense_home(c, objects, {site, row}, p.seeds);
        }
      }
    }
    validate_targets();
    for (const auto& [pair, edit] : steps) {
      apply_chunk_step(p, *edit, objects, {site, row});
    }
    if (all_edits.size() != edits.size()) {
      std::vector<size_t> tap_index(p.chunks.size());
      for (auto& w : wires) {
        for (auto& tap : w.contacts) {
          tap.xy = p.chunks[tap.home].ports[tap_index[tap.home]++].xy;
        }
      }
      measure_chunk_ports(p, wires, core);
    }
    return p;
  };
  // A low memory target can separate logic from a group of fixed SRAMs spanning the core.
  // Give the independent root macros physical islands; never split a color or change a fixed pose.
  if (input && !single) {
    const auto rec = input->select(chunk_plan_class, design);
    if (!rec.empty() && rec[0]->HasMember("macro_islands") && (*rec[0])["macro_islands"].IsBool()
        && (*rec[0])["macro_islands"].GetBool() && rec[0]->HasMember("budget_bytes") && (*rec[0])["budget_bytes"].IsUint64()
        && (*rec[0])["budget_bytes"].GetUint64() == budget && rec[0]->HasMember("chunks") && (*rec[0])["chunks"].IsArray()) {
      Packed saved;
      saved.scope.resize(objects.size());
      std::map<std::pair<bool, std::string>, int> lookup;
      for (size_t i = 0; i < objects.size(); ++i) {
        lookup[{objects[i].macro, objects[i].id}] = i;
      }
      for (const auto& c : (*rec[0])["chunks"].GetArray()) {
        if (!c.IsObject() || !c.HasMember("members") || !c["members"].IsArray() || !c.HasMember("id") || !c["id"].IsString()) {
          continue;
        }
        std::vector<int> ids;
        for (const auto& m : c["members"].GetArray()) {
          if (!m.IsObject() || !m.HasMember("id") || !m["id"].IsString() || !m.HasMember("macro") || !m["macro"].IsBool()) {
            continue;
          }
          auto it = lookup.find({m["macro"].GetBool(), m["id"].GetString()});
          if (it != lookup.end()) {
            ids.push_back(it->second);
          }
        }
        if (!ids.empty()) {
          auto s = packed_scope(ids, objects);
          if (qualified(s, "0") == c["id"].GetString()) {
            for (auto i : ids) {
              saved.scope[i] = s;
            }
          }
        }
      }
      if (auto p = solve(true, &saved, false, true)) {
        return std::move(*p);
      }
    }
  }
  if (auto p = solve(false, nullptr, false, only_saved)) {
    return std::move(*p);
  }
  if (only_saved) {
    return {};
  }
  if (!single) {
    if (auto fine = solve(true, nullptr, true)) {
      auto packed = pack_islands(*fine, objects);
      if (auto p = solve(true, &packed)) {
        return std::move(*p);
      }
    }
  }
  std::ostringstream reason;
  reason << "fp_plan: no rectangular cut can accommodate the color and fixed macro footprints inside " << core[0] << ',' << core[1]
         << ".." << core[2] << ',' << core[3] << " DBU";
  size_t shown = 0;
  for (const auto& o : objects) {
    if (shown++ == 32) {
      break;
    }
    reason << "; " << o.id << " (" << o.width << 'x' << o.height << ", " << o.cells << " cells";
    if (!o.instance.empty()) {
      reason << ", scope " << o.instance;
    }
    if (o.fixed) {
      reason << ", fixed " << (*o.fixed)[0] << ',' << (*o.fixed)[1] << ".." << (*o.fixed)[2] << ',' << (*o.fixed)[3];
    }
    reason << ')';
  }
  throw std::runtime_error(reason.str());
}
rapidjson::Document Chunk_model::record(const Chunk_plan& p, const std::string& design) const {
  rapidjson::Document d(rapidjson::kObjectType);
  auto&               a   = d.GetAllocator();
  auto                str = [&](const std::string& s) { return rapidjson::Value(s.data(), s.size(), a); };
  auto                box = [&](const Chunk_box& b) {
    rapidjson::Value v(rapidjson::kArrayType);
    for (auto n : b) {
      v.PushBack(n, a);
    }
    return v;
  };
  d.AddMember("class", rapidjson::StringRef(chunk_plan_class), a)
      .AddMember("id", "fp_plan", a)
      .AddMember("design", str(design), a)
      .AddMember("schema_version", 1, a)
      .AddMember("budget_bytes", p.budget, a)
      .AddMember("input_key", p.input_key, a);
  if (!p.predictor.context.empty()) {
    rapidjson::Value mem(rapidjson::kObjectType), feature(rapidjson::kArrayType), rate(rapidjson::kArrayType),
        maxima(rapidjson::kArrayType);
    for (unsigned j = 0; j < 4; ++j) {
      feature.PushBack(p.counts[j], a);
      rate.PushBack(p.predictor.rate[j], a);
      maxima.PushBack(p.predictor.observed_max[j], a);
    }
    const bool extrapolated = !p.predictor.covers(p.counts);
    mem.AddMember("context", str(p.predictor.context), a)
        .AddMember("samples", p.predictor.samples, a)
        .AddMember("baseline_bytes", p.predictor.baseline, a)
        .AddMember("counts", feature, a)
        .AddMember("bytes_per_unit", rate, a)
        .AddMember("observed_max_counts", maxima, a)
        .AddMember("extrapolating", extrapolated, a)
        .AddMember("whole_design_bytes", p.predictor.peak(p.counts), a)
        .AddMember("baseline_exhausts_budget", p.budget && p.predictor.baseline >= p.budget, a);
    d.AddMember("memory_prediction", mem, a);
  }
  if (!p.shape_candidates.empty()) {
    rapidjson::Value alternatives(rapidjson::kArrayType);
    for (auto& [cost, frames] : p.shape_candidates) {
      rapidjson::Value row(rapidjson::kObjectType), rects(rapidjson::kArrayType);
      for (auto& frame : frames) {
        rects.PushBack(box(frame), a);
      }
      row.AddMember("center_hpwl_dbu", cost, a).AddMember("boxes_dbu", rects, a);
      alternatives.PushBack(row, a);
    }
    d.AddMember("shape_candidates", alternatives, a).AddMember("shape_rank", shape_rank, a);
  }
  rapidjson::Value communication(rapidjson::kObjectType);
  communication.AddMember("crossing_nets", uint64_t(p.crossing_nets), a)
      .AddMember("center_hpwl_dbu", p.center_hpwl, a)
      .AddMember("pin_hpwl_dbu", p.port_hpwl, a);
  communication.AddMember("pin_sweeps", p.pin_sweeps, a).AddMember("pins_converged", p.pins_converged, a);
  rapidjson::Value anchors(rapidjson::kArrayType);
  for (const auto& [id, b] : p.fixed_contacts) {
    rapidjson::Value t(rapidjson::kObjectType);
    t.AddMember("net_index", uint64_t(id), a).AddMember("box_dbu", box(b), a);
    anchors.PushBack(t, a);
  }
  communication.AddMember("fixed_contacts", anchors, a);
  d.AddMember("communication", communication, a);
  rapidjson::Value route(rapidjson::kObjectType), loads(rapidjson::kArrayType);
  for (const auto& cell : p.route.load) {
    rapidjson::Value v(rapidjson::kArrayType);
    v.PushBack(cell[0], a).PushBack(cell[1], a);
    loads.PushBack(v, a);
  }
  route.AddMember("bins", Chunk_route_grid::bins, a)
      .AddMember("frame_dbu", box(p.route.frame), a)
      .AddMember("peak_horizontal", p.route.peak_h, a)
      .AddMember("peak_vertical", p.route.peak_v, a)
      .AddMember("loads", loads, a);
  d.AddMember("route_demand", route, a);
  if (track_pitch > 0 && !p.home.empty()) {
    Chunk_demand map(p.route.frame, 128 * track_pitch, tracks_per_dbu);
    for (size_t i = 0; i < objects.size(); ++i) {
      const auto& b = p.seeds[i];
      if (objects[i].macro) {
        map.reserve(b, objects[i].route_blockage);
      } else {
        double utilization = objects[i].area / std::max(1., (b[2] - b[0]) * (b[3] - b[1]));
        double reserve     = std::clamp(utilization, 0., .8);
        map.reserve(b, {reserve, reserve}, true);
      }
    }
    Chunk_demand                                      boundary_map(p.route.frame, 128 * track_pitch, tracks_per_dbu);
    std::map<size_t, std::vector<Chunk_demand::Pair>> taps;
    for (const auto& c : p.chunks) {
      for (const auto& port : c.ports) {
        taps[port.net_index].push_back(port.xy);
      }
      for (int i : c.objects) {
        const auto& seed = p.seeds[i];
        if (objects[i].macro) {
          boundary_map.reserve(objects[i].macro_footprint(seed), objects[i].route_blockage);
          continue;
        }
        std::vector<Chunk_box> parts;
        double                 area = 0;
        const auto             add  = [&](const Chunk_box& r) {
          Chunk_box b{std::max(r[0], seed[0]), std::max(r[1], seed[1]), std::min(r[2], seed[2]), std::min(r[3], seed[3])};
          if (b[2] > b[0] && b[3] > b[1]) {
            parts.push_back(b);
            area += (b[2] - b[0]) * (b[3] - b[1]);
          }
        };
        if (c.region.empty()) {
          add(c.box);
        } else {
          for (const auto& r : c.region) {
            add(r);
          }
        }
        if (parts.empty()) {
          // A seed wholly inside a donated tab deposits demand in the nearest owned slab.
          const double x = (seed[0] + seed[2]) * .5, y = (seed[1] + seed[3]) * .5;
          const auto   dist = [&](const Chunk_box& b) {
            return std::abs(x - std::clamp(x, b[0], b[2])) + std::abs(y - std::clamp(y, b[1], b[3]));
          };
          const auto r = c.region.empty()
                             ? c.box
                             : *std::min_element(c.region.begin(), c.region.end(), [&](const auto& lhs, const auto& rhs) {
                                 return dist(lhs) < dist(rhs);
                               });
          parts.push_back(r);
          area = (r[2] - r[0]) * (r[3] - r[1]);
        }
        double reserve = std::clamp(objects[i].area / std::max(1., area), 0., .8);
        for (const auto& b : parts) {
          boundary_map.reserve(b, {reserve, reserve}, true);
        }
      }
    }
    for (const auto& edge : edges) {
      if (edge.nodes.empty()) {
        continue;
      }
      const int home = p.home[edge.nodes.front()];
      if (!edge.terminal && std::ranges::all_of(edge.nodes, [&](int i) { return p.home[i] == home; })) {
        continue;
      }
      std::vector<Chunk_demand::Pair> points;
      for (int i : edge.nodes) {
        const auto& b = p.seeds[i];
        points.push_back({(b[0] + b[2]) / 2, (b[1] + b[3]) / 2});
      }
      if (edge.terminal) {
        points.push_back(*edge.terminal);
      }
      map.route(points);
      if (auto it = taps.find(edge.net_index); it != taps.end()) {
        points.insert(points.end(), it->second.begin(), it->second.end());
      }
      boundary_map.route(std::move(points));
    }
    d.AddMember("boundary_routing", boundary_map.record(d, 128), a);
    auto             grid = map.record(d, 128);
    rapidjson::Value rates(rapidjson::kArrayType);
    for (auto rate : tracks_per_dbu) {
      rates.PushBack(rate, a);
    }
    rapidjson::Value patterns(rapidjson::kArrayType);
    for (auto& t : routing_patterns) {
      rapidjson::Value pattern(rapidjson::kObjectType);
      pattern.AddMember("layer", str(t.layer), a)
          .AddMember("direction", t.axis, a)
          .AddMember("start", t.start, a)
          .AddMember("step", t.step, a)
          .AddMember("count", t.count, a);
      patterns.PushBack(pattern, a);
    }
    if (dbu_per_um > 0) {
      rapidjson::Value policy(rapidjson::kObjectType);
      policy.AddMember("enabled", spread, a)
          .AddMember("wide_count", Io_spread_policy::wide_count, a)
          .AddMember("tracks", Io_spread_policy::tracks, a)
          .AddMember("corner_tracks", Io_spread_policy::corner_tracks, a)
          .AddMember("corner_cap_dbu", Io_spread_policy::max_corner_um * dbu_per_um, a)
          .AddMember("share", Io_spread_policy::share, a)
          .AddMember("max_pitch_dbu", Io_spread_policy::max_pitch_um * dbu_per_um, a);
      grid.AddMember("io_spacing", policy, a);
    }
    grid.AddMember("track_patterns", patterns, a);
    grid.AddMember("track_pitch_dbu", track_pitch, a).AddMember("tracks_per_dbu", rates, a);
    d.AddMember("coarse_routing", grid, a);
  }
  if (!p.chunks.empty() && p.chunks.front().reference_box) {
    rapidjson::Value overlaps(rapidjson::kArrayType), geom(rapidjson::kObjectType);
    double           shared = 0;
    for (const auto& o : p.overlaps) {
      rapidjson::Value e(rapidjson::kObjectType);
      e.AddMember("a", str(p.chunks[o.a].id), a).AddMember("b", str(p.chunks[o.b].id), a).AddMember("box_dbu", box(o.box), a);
      overlaps.PushBack(e, a);
      shared += (o.box[2] - o.box[0]) * (o.box[3] - o.box[1]);
    }
    geom.AddMember("overlaps", overlaps, a)
        .AddMember("shared_dbu2", shared, a)
        .AddMember("independent_cycles", p.overlap_cycles, a)
        .AddMember("overlap_feedback_rounds", p.overlaps.empty() ? 0 : 8, a);
    d.AddMember("geometry", geom, a);
  }
  if (p.macro_islands) {
    d.AddMember("macro_islands", true, a);
  }
  rapidjson::Value chunks(rapidjson::kArrayType);
  for (const auto& c : p.chunks) {
    rapidjson::Value      v(rapidjson::kObjectType), members(rapidjson::kArrayType);
    double                cells = 0, macros = 0;
    unsigned              fixed_macros = 0;
    std::set<std::string> scopes;
    for (int i : c.objects) {
      const auto& o               = objects[i];
      (o.macro ? macros : cells) += o.area;
      fixed_macros += o.macro && o.fixed.has_value();
      if (!o.instance.empty()) {
        scopes.insert(o.instance);
      }
      rapidjson::Value m(rapidjson::kObjectType);
      m.AddMember("id", str(o.id), a)
          .AddMember("macro", o.macro, a)
          .AddMember("cells", uint64_t(o.cells), a)
          .AddMember("box_dbu", box(p.seeds[i]), a)
          .AddMember("area_dbu2", o.area, a)
          .AddMember("density", p.density[i], a)
          .AddMember("density_source", str(p.density_source[i]), a)
          .AddMember("reserved_dbu2", o.area / p.density[i], a);
      if (o.macro && track_pitch > 0) {
        rapidjson::Value obs(rapidjson::kArrayType);
        for (auto rate : o.route_blockage) {
          obs.PushBack(rate, a);
        }
        m.AddMember("blocked_fraction", obs, a);
      }
      if (!o.instance.empty()) {
        m.AddMember("instance", str(o.instance), a);
      }
      if (!o.module.empty() && !o.macro) {
        m.AddMember("template_color", str(qualified(o.module, o.local)), a);
      }
      members.PushBack(m, a);
    }
    if (fixed_macros) {
      v.AddMember("fixed_macros", fixed_macros, a);
    }
    const double     usable = (c.box[2] - c.box[0]) * (c.box[3] - c.box[1]) - .5 * c.shared_dbu2 - macros;
    rapidjson::Value ports(rapidjson::kArrayType);
    for (const auto& port : c.ports) {
      rapidjson::Value contact(rapidjson::kObjectType), xy(rapidjson::kArrayType);
      xy.PushBack(port.xy[0], a).PushBack(port.xy[1], a);
      contact.AddMember("net", str(port.net), a).AddMember("net_index", uint64_t(port.net_index), a).AddMember("xy_dbu", xy, a);
      if (port.copy_source_net != SIZE_MAX) {
        contact.AddMember("copy_source_net", uint64_t(port.copy_source_net), a);
      }
      if (!port.role.empty()) {
        contact.AddMember("role", str(port.role), a);
      }
      if (auto bit = bus_bits.find(port.net_index); bit != bus_bits.end()) {
        contact.AddMember("bus", str(bit->second.first), a).AddMember("bit", bit->second.second, a);
      }
      ports.PushBack(contact, a);
    }
    rapidjson::Value instances(rapidjson::kArrayType);
    for (const auto& scope : scopes) {
      instances.PushBack(str(scope), a);
    }
    if (c.reference_box) {
      v.AddMember("reference_box_dbu", box(*c.reference_box), a)
          .AddMember("aspect_locked", c.aspect_locked, a)
          .AddMember("shared_dbu2", c.shared_dbu2, a)
          .AddMember("effective_area_dbu2", usable + macros, a);
    }
    if (c.interface_copy) {
      const auto&      copy = *c.interface_copy;
      rapidjson::Value result(rapidjson::kObjectType);
      result.AddMember("source", str(copy.source), a)
          .AddMember("requested", str(copy.requested), a)
          .AddMember("orientation", str(copy.orientation), a)
          .AddMember("matching", str(copy.matching), a)
          .AddMember("matched", uint64_t(copy.matched), a)
          .AddMember("source_ports", uint64_t(copy.source_ports), a)
          .AddMember("target_ports", uint64_t(copy.target_ports), a);
      v.AddMember("interface_copy", result, a);
    }
    v.AddMember("incident_pin_hpwl_dbu", c.incident_hpwl, a).AddMember("longest_net_span_dbu", c.longest_span, a);
    if (!c.region.empty()) {
      auto rects = [&](const auto& boxes) {
        rapidjson::Value out(rapidjson::kArrayType);
        for (const auto& b : boxes) out.PushBack(box(b), a);
        return out;
      };
      v.AddMember("region_dbu", rects(c.region), a).AddMember("perimeter_dbu", rects(c.perimeter), a)
          .AddMember("worker_box_dbu", box(c.worker_box()), a);
    }
    v.AddMember("instances", instances, a).AddMember("ports", ports, a);
    v.AddMember("id", str(c.id), a)
        .AddMember("partition", str(c.partition), a)
        .AddMember("implementation", str(c.implementation), a)
        .AddMember("implementation_mode", str(c.implementation_mode), a)
        .AddMember("target_aspect", c.target_aspect, a)
        .AddMember("box_dbu", box(c.box), a)
        .AddMember("density", c.density, a)
        .AddMember("density_source", str(c.target_source), a)
        .AddMember("reserved_dbu2", c.reserved, a)
        .AddMember("estimated_bytes", c.bytes, a)
        .AddMember("allowance_bytes", c.allowance, a)
        .AddMember("members", members, a);
    if (p.budget && c.bytes > p.budget && c.objects.size() == 1) {
      v.AddMember("oversized_object", str(objects[c.objects.front()].id), a);
    }
    if (usable > 0) {
      v.AddMember("planned_fill", cells / usable, a);
    }
    chunks.PushBack(v, a);
  }
  d.AddMember("chunks", chunks, a);
  return d;
}
}  // namespace livehd::placement

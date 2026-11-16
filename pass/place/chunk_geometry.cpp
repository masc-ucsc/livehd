// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "chunk_geometry.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <numeric>
#include <set>
#include <stdexcept>

namespace livehd::placement {
namespace {
using Box   = Chunk_box;
using Point = std::array<double, 2>;
void check(bool ok, const std::string& msg) {
  if (!ok) {
    throw std::invalid_argument("chunk geometry: " + msg);
  }
}
double dim(const Box& b, int k) { return b[k + 2] - b[k]; }
double area(const Box& b) { return dim(b, 0) * dim(b, 1); }
bool   same(double a, double b) { return std::abs(a - b) <= 1e-12 * std::max({1., std::abs(a), std::abs(b)}); }
bool   inside(const Box& a, const Box& b) { return a[0] >= b[0] && a[1] >= b[1] && a[2] <= b[2] && a[3] <= b[3]; }
Box    join(const Box& a, const Box& b) {
  return {std::min(a[0], b[0]), std::min(a[1], b[1]), std::max(a[2], b[2]), std::max(a[3], b[3])};
}
Box intersect(const Box& a, const Box& b) {
  return {std::max(a[0], b[0]), std::max(a[1], b[1]), std::min(a[2], b[2]), std::min(a[3], b[3])};
}
bool positive(const Box& b) { return dim(b, 0) > 0 && dim(b, 1) > 0; }
Box  at(const Box& b, Point p) { return {p[0], p[1], p[0] + dim(b, 0), p[1] + dim(b, 1)}; }
struct Capacity {
  std::vector<double>        overlap;
  std::vector<Chunk_overlap> pairs;
  unsigned                   cycles = 0;
};
// Only pairwise physical sharing is admitted. Distinct overlap strips may form
// a ring. Each physical strip then has exactly two owners, including in a ring.
bool capacity(const std::vector<Box>& boxes, Capacity& out, std::string& reason) {
  const int n = boxes.size();
  out.overlap.assign(n, 0);
  std::vector<int> order(n), active, parent(n);
  std::iota(order.begin(), order.end(), 0);
  parent    = order;
  auto root = [&](int i) {
    while (i != parent[i]) {
      parent[i] = parent[parent[i]];
      i         = parent[i];
    }
    return i;
  };
  std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return boxes[a][0] < boxes[b][0]; });
  std::vector<std::vector<Box>> strips(n);
  for (int i : order) {
    const auto& b = boxes[i];
    std::erase_if(active, [&](int j) { return boxes[j][2] <= b[0]; });
    for (int j : active) {
      auto strip = intersect(b, boxes[j]);
      if (!positive(strip)) {
        continue;
      }
      for (auto& s : strips[i]) {
        if (positive(intersect(s, strip))) {
          reason = "three chunks claim the same physical area";
          return false;
        }
      }
      strips[i].push_back(strip);
      strips[j].push_back(strip);
      out.pairs.push_back({j, i, strip});
      out.overlap[i] += area(strip);
      out.overlap[j] += area(strip);
      auto a = root(i), c = root(j);
      if (a == c) {
        ++out.cycles;
      } else {
        parent[a] = c;
      }
    }
    active.push_back(i);
  }
  return true;
}
struct Mover {
  Chunk_plan&                      p;
  const std::vector<Chunk_object>& objects;
  Box                              core;
  Point                            pitch;
  std::vector<Box>                 boxes;
  std::vector<bool>                hard, macro;
  std::vector<Point>               minimum;
  Capacity                         current;
  Mover(Chunk_plan& p_, const std::vector<Chunk_object>& o, Box b, Point step) : p(p_), objects(o), core(b), pitch(step) {
    for (auto& c : p.chunks) {
      boxes.push_back(c.reference_box.value_or(c.box));
      c.reference_box = boxes.back();
      Point dims{};
      bool  fixed = false, has_macro = false;
      for (int n : c.objects) {
        const auto& v  = o[n];
        fixed         |= v.fixed.has_value();
        has_macro     |= v.macro;
        dims[0]        = std::max(dims[0], v.width);
        dims[1]        = std::max(dims[1], v.height);
      }
      minimum.push_back(dims);
      hard.push_back(fixed);
      macro.push_back(has_macro);
    }
    current.overlap.assign(boxes.size(), 0);
  }
  bool fits(int id, const Box& b) const {
    const auto& old = boxes[id];
    return positive(b) && inside(b, core) && same(area(b), area(old)) && (!hard[id] || b == old)
           && (!(p.chunks[id].aspect_locked || macro[id]) || (same(dim(b, 0), dim(old, 0)) && same(dim(b, 1), dim(old, 1))))
           && dim(b, 0) >= minimum[id][0] && dim(b, 1) >= minimum[id][1];
  }
  double snap(double v, int k) const { return core[k] + std::round((v - core[k]) / pitch[k]) * pitch[k]; }
  using Change = std::vector<std::pair<int, Box>>;
  bool take(const Change& change, double cap, std::string& reason) {
    auto trial = boxes;
    for (const auto& [id, b] : change) {
      if (!fits(id, b)) {
        return false;
      }
      trial[id] = b;
    }
    Capacity next;
    if (!capacity(trial, next, reason)) {
      return false;
    }
    for (size_t i = 0; i < trial.size(); ++i) {
      if (next.overlap[i] > 0) {
        const auto a = area(trial[i]);
        if (next.overlap[i] > std::max(cap * a, boxes[i] == trial[i] ? current.overlap[i] : 0.) + 1e-7) {
          reason = "overlap limit exceeded";
          return false;
        }
        if (a - .5 * next.overlap[i] + 1e-7 < p.chunks[i].reserved) {
          reason = "shared capacity is below the reserved area for " + p.chunks[i].id;
          return false;
        }
        if (macro[i]) {
          reason = "overlap at a hard macro chunk is not supported";
          return false;
        }
      }
    }
    current = std::move(next);
    boxes   = std::move(trial);
    return true;
  }
  // An exact local retile. Cut the space around the moved (rigid) rectangle
  // into at most four slabs, then guillotine-pack the displaced neighbors.
  // Work limits are per candidate, independent of the size of the full design.
  bool repack(int id, const Box& dest, Change& out) {
    Box              region = join(boxes[id], dest);
    std::vector<int> neighbors;
    for (;;) {
      auto before = region;
      neighbors.clear();
      for (size_t i = 0; i < boxes.size(); ++i) {
        if (int(i) != id && positive(intersect(region, boxes[i]))) {
          neighbors.push_back(i);
          if (neighbors.size() > 9) {
            return false;
          }
          region = join(region, boxes[i]);
        }
      }
      if (region == before) {
        break;
      }
    }
    const unsigned      all = (1u << neighbors.size()) - 1;
    std::vector<double> areas(all + 1);
    for (unsigned m = 1; m <= all; ++m) {
      areas[m] = areas[m & (m - 1)] + area(boxes[neighbors[std::countr_zero(m)]]);
    }
    if (!same(areas[all] + area(dest), area(region))) {
      return false;
    }
    int  budget = 3500;
    auto pack   = [&](auto&& self, unsigned mask, const Box& b) -> bool {
      if (--budget < 0 || !same(areas[mask], area(b))) {
        return false;
      }
      if (std::has_single_bit(mask)) {
        int j = neighbors[std::countr_zero(mask)];
        if (!fits(j, b)) {
          return false;
        }
        out.emplace_back(j, b);
        return true;
      }
      auto size = out.size();
      for (int k = 0; k < 2; ++k) {
        for (unsigned sub = (mask - 1) & mask; sub; sub = (sub - 1) & mask) {
          double edge = b[k] + areas[sub] / dim(b, 1 - k), cut = snap(edge, k);
          if (!same(cut, edge) || cut <= b[k] || cut >= b[k + 2]) {
            continue;
          }
          auto a = b, c = b;
          a[k + 2] = c[k] = cut;
          if (self(self, sub, a) && self(self, mask ^ sub, c)) {
            return true;
          }
          out.resize(size);
          if (budget <= 0) {
            return false;
          }
        }
      }
      return false;
    };
    for (int k = 0; k < 2; ++k) {
      auto             remain = region;
      std::vector<Box> free;
      for (int d : {k, 1 - k}) {
        auto lo = remain, hi = remain;
        lo[d + 2] = dest[d];
        hi[d]     = dest[d + 2];
        if (positive(lo)) {
          free.push_back(lo);
        }
        if (positive(hi)) {
          free.push_back(hi);
        }
        remain[d]     = dest[d];
        remain[d + 2] = dest[d + 2];
      }
      out = {
          {id, dest}
      };
      auto fill = [&](auto&& self, unsigned mask, size_t s) -> bool {
        if (s == free.size()) {
          return mask == 0;
        }
        if (--budget < 0) {
          return false;
        }
        auto size = out.size();
        for (unsigned part = mask; part; part = (part - 1) & mask) {
          if (same(areas[part], area(free[s])) && pack(pack, part, free[s]) && self(self, mask ^ part, s + 1)) {
            return true;
          }
          out.resize(size);
          if (budget <= 0) {
            return false;
          }
        }
        return false;
      };
      if (fill(fill, all, 0)) {
        return true;
      }
    }
    return false;
  }
  void move(int id, Point wish, double cap) {
    auto old = boxes[id];
    check(!hard[id], "chunk contains a fixed macro: " + p.chunks[id].id);
    Point goal{snap(wish[0], 0), snap(wish[1], 1)};
    Box   dest = at(old, goal);
    check(inside(dest, core), "destination lies outside the block");
    if (dest == old) {
      return;
    }
    auto        distance = [&](const Box& b) { return std::hypot(b[0] - goal[0], b[1] - goal[1]); };
    std::string reason;
    // Try the exact requested translation first. With sharing off, the same
    // check permits a move into unused space left by an earlier overlap.
    if (take(
            {
                {id, dest}
    },
            cap,
            reason)) {
      return;
    }
    Change packing;
    if (repack(id, dest, packing) && take(packing, cap, reason)) {
      return;
    }
    struct Candidate {
      double d;
      int    j;
      Box    a, b;
    };
    std::vector<Candidate> choices;
    auto                   add = [&](int j, Box a, Box b) {
      double d = distance(a);
      if (d < distance(old) && fits(j, b) && same(dim(a, 0), dim(old, 0)) && same(dim(a, 1), dim(old, 1))) {
        choices.push_back({d, j, a, b});
      }
    };
    for (size_t n = 0; n < boxes.size(); ++n) {
      if (int(n) != id) {
        const auto b = boxes[n];
        if (same(dim(old, 0), dim(b, 0)) && same(dim(old, 1), dim(b, 1))) {
          add(n, b, old);
        }
        for (int k = 0; k < 2; ++k) {
          int h = 1 - k;
          if (old[h] != b[h] || old[h + 2] != b[h + 2] || (old[k + 2] != b[k] && b[k + 2] != old[k])) {
            continue;
          }
          Point a{old[0], old[1]}, q{b[0], b[1]};
          if (old[k + 2] == b[k]) {
            a[k] += dim(b, k);
            q[k] -= dim(old, k);
          } else {
            a[k] -= dim(b, k);
            q[k] += dim(old, k);
          }
          add(n, at(old, a), at(b, q));
        }
      }
    }
    std::stable_sort(choices.begin(), choices.end(), [](const auto& a, const auto& b) { return a.d < b.d; });
    for (auto& c : choices) {
      if (take(
              {
                  { id, c.a},
                  {c.j, c.b}
      },
              cap,
              reason)) {
        return;
      }
    }
    check(false, "no feasible area-preserving move toward the drop" + (reason.empty() ? std::string{} : " (" + reason + ")"));
  }
};
}  // namespace

Chunk_edits read_chunk_edits(const metadata::Bundle* input, const std::string& design) {
  Chunk_edits ops;
  bool        seen = false;
  if (!input) {
    return ops;
  }
  for (auto* r : input->select(chunk_guidance_class, design)) {
    if (r->HasMember("geometry")) {
      const auto& list = (*r)["geometry"];
      check(!seen && list.IsArray(), "expected one ordered geometry array");
      seen = true;
      for (const auto& v : list.GetArray()) {
        check(v.IsObject(), "invalid operation");
        Chunk_edit e;
        if (v.HasMember("lock_others")) {
          check(!v.HasMember("move") && !v.HasMember("lock_aspect") && v["lock_others"].IsString()
                    && v["lock_others"].GetStringLength(),
                "invalid lock_others selector");
          e.all_but = v["lock_others"].GetString();
        } else if (v.HasMember("move")) {
          check(v["move"].IsString() && v["move"].GetStringLength() && v.HasMember("to_dbu") && v["to_dbu"].IsArray()
                    && v["to_dbu"].Size() == 2 && !v.HasMember("lock_aspect"),
                "invalid move");
          e.move = v["move"].GetString();
          for (unsigned k = 0; k < 2; ++k) {
            auto& n = v["to_dbu"][k];
            check(n.IsNumber() && std::isfinite(n.GetDouble()), "invalid destination");
            e.to[k] = n.GetDouble();
          }
          if (v.HasMember("max_overlap")) {
            auto& n = v["max_overlap"];
            check(n.IsNumber() && n.GetDouble() >= 0 && n.GetDouble() <= .25, "max_overlap must be in [0, 0.25]");
            e.overlap = n.GetDouble();
          }
        } else {
          check(v.HasMember("lock_aspect") && v["lock_aspect"].IsBool() && v.HasMember("chunks") && v["chunks"].IsArray()
                    && !v["chunks"].Empty(),
                "invalid aspect lock");
          e.lock = v["lock_aspect"].GetBool();
          for (const auto& i : v["chunks"].GetArray()) {
            check(i.IsString(), "invalid lock selector");
            e.locks.emplace_back(i.GetString());
          }
        }
        ops.push_back(std::move(e));
      }
    }
  }
  return ops;
}
void edit_chunk_boxes(Chunk_plan& p, const Chunk_edits& edits, const std::vector<Chunk_object>& objects, Box core, Point pitch) {
  if (edits.empty()) {
    return;
  }
  Mover                      m(p, objects, core, pitch);
  std::map<std::string, int> ids;
  for (size_t i = 0; i < p.chunks.size(); ++i) {
    ids.emplace(p.chunks[i].id, i);
  }
  auto id = [&](const std::string& name) {
    auto it = ids.find(name);
    check(it != ids.end(), "unknown physical chunk " + name);
    return it->second;
  };
  for (const auto& e : edits) {
    if (!e.all_but.empty()) {
      int free = id(e.all_but);
      for (size_t i = 0; i < p.chunks.size(); ++i) {
        p.chunks[i].aspect_locked = int(i) != free;
      }
    } else if (e.move.empty()) {
      for (auto& n : e.locks) {
        p.chunks[id(n)].aspect_locked = e.lock;
      }
      check(std::any_of(p.chunks.begin(), p.chunks.end(), [](const auto& c) { return !c.aspect_locked; }),
            "leave at least one aspect ratio unlocked");
    } else {
      m.move(id(e.move), e.to, e.overlap);
    }
  }
  for (size_t i = 0; i < p.chunks.size(); ++i) {
    auto& c       = p.chunks[i];
    c.box         = m.boxes[i];
    c.shared_dbu2 = m.current.overlap[i];
  }
  p.overlaps       = std::move(m.current.pairs);
  p.overlap_cycles = m.current.cycles;
}
void relax_shared_colors(const Chunk_plan& p, const std::vector<Chunk_object>& objects, const std::vector<Point>& previous,
                         std::vector<Point>& next) {
  if (p.overlaps.empty()) {
    return;
  }
  std::vector<Point>    force(previous.size());
  std::vector<unsigned> degree(previous.size());
  // Estimate each color's working footprint in last round's coordinates. Push
  // excess demand towards the exclusive part of its own box, never across homes.
  for (const auto& e : p.overlaps) {
    const auto& strip = e.box;
    for (int h : {e.a, e.b}) {
      const auto& c   = p.chunks[h];
      const auto& box = c.box;
      Point       dir{(box[0] + box[2] - strip[0] - strip[2]) * .5, (box[1] + box[3] - strip[1] - strip[3]) * .5};
      double      scale = std::hypot(dir[0], dir[1]);
      if (scale == 0) {
        continue;
      }
      double                              load = 0;
      std::vector<std::pair<int, double>> weights;
      for (int i : c.objects) {
        double need   = objects[i].area / p.density[i],
               width  = std::clamp(std::sqrt(need), std::min(dim(box, 0), need / dim(box, 1)), dim(box, 0));
        double height = std::min(dim(box, 1), need / width);
        auto   x      = std::clamp(previous[i][0] - .5 * width, box[0], std::max(box[0], box[2] - width));
        auto   y      = std::clamp(previous[i][1] - .5 * height, box[1], std::max(box[1], box[3] - height));
        auto   shared = intersect(strip, {x, y, x + width, y + height});
        if (!positive(shared)) {
          continue;
        }
        double part  = area(shared);
        load        += part;
        weights.emplace_back(i, part / need);
      }
      if (load <= .5 * area(strip)) {
        continue;
      }
      double step = .25 * std::min(dim(box, 0), dim(box, 1)) * (1 - .5 * area(strip) / load);
      for (auto [i, f] : weights) {
        for (int k = 0; k < 2; ++k) {
          force[i][k] += step * f * dir[k] / scale;
        }
        ++degree[i];
      }
    }
  }
  for (size_t i = 0; i < next.size(); ++i) {
    if (degree[i]) {
      const auto& b = p.chunks[p.home[i]].box;
      for (int k = 0; k < 2; ++k) {
        next[i][k] = std::clamp(.5 * (next[i][k] + previous[i][k]) + force[i][k] / degree[i], b[k], b[k + 2]);
      }
    }
  }
}
}  // namespace livehd::placement

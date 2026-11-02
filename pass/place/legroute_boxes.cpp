// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
// legroute L2: cell-boxes. Connected cells merge cheapest first, cost = (1 + d (1 + 4 crit) / u) / (1 + conn +
// nets made internal), no merge above the cost cap; then part-full boxes pack with the box of their nearest member
// cell, the radius growing from 1.5 to 5 box widths (prototype variant c1_T20_pa_cd4).
#include <algorithm>
#include <cmath>
#include <numeric>
#include <queue>
#include <unordered_map>

#include "legroute_internal.hpp"

namespace livehd::placement::lr {
namespace {
using Count = std::vector<std::pair<int, int>>;  // (net, pins of the net in the box), sorted by net

struct Builder {
  const Design&                 d;
  const Grid&                   g;
  double                        fill, unit, pin_limit;
  std::vector<int>              parent;
  std::vector<std::vector<int>> widths;  // per root: cell widths (sites)
  std::vector<int>              sites;   // per root
  std::vector<double>           pins, sx, sy, w, bcrit;
  std::vector<int>              slot, ext;
  std::vector<Count>            nets;
  std::vector<int>              npins;   // per net: all pins
  std::vector<double>           cx, cy;  // per movable cell center

  Builder(const Design& dd, const Grid& gg) : d(dd), g(gg) {}
  int find(int i) {
    while (parent[i] != i) {
      parent[i] = parent[parent[i]];
      i         = parent[i];
    }
    return i;
  }
  bool packs(const std::vector<int>& ws) const {
    std::array<double, 2> room{g.sites * fill + 1e-9, g.sites * fill + 1e-9};
    std::vector<int>      sorted;
    for (int x : ws) {
      if (x < 0) {  // a two-row cell (negated width): the same sites in both rows
        room[0] += x;
        room[1] += x;
        if (room[0] < 0 || room[1] < 0) {
          return false;
        }
      } else {
        sorted.push_back(x);
      }
    }
    std::ranges::sort(sorted, std::greater<>());
    for (int x : sorted) {
      auto& r = room[0] >= room[1] ? room[0] : room[1];
      if (r < x) {
        return false;
      }
      r -= x;
    }
    return true;
  }
  bool fits(int a, int b) const {
    if (sites[a] + sites[b] > 2 * g.sites * fill + 1e-9 || pins[a] + pins[b] > pin_limit || (slot[a] >= 0 && slot[b] >= 0)) {
      return false;
    }
    auto ws = widths[a];
    ws.insert(ws.end(), widths[b].begin(), widths[b].end());
    return packs(ws);
  }
  // external nets of the merged box; conn (connection weight) and gone (nets made internal) through the pointers
  int merged_ext(int a, int b, double* conn = nullptr, int* gone = nullptr) const {
    const auto& A = nets[a];
    const auto& B = nets[b];
    int         e = ext[a] + ext[b], ng = 0;
    double      cw = 0;
    for (size_t i = 0, j = 0; i < A.size() && j < B.size();) {
      if (A[i].first < B[j].first) {
        ++i;
      } else if (B[j].first < A[i].first) {
        ++j;
      } else {
        const int n = A[i].first, x = A[i].second, y = B[j].second;
        e  += (x + y < npins[n]) - (x < npins[n]) - (y < npins[n]);
        ng += x + y == npins[n];
        cw += double(x) * y / (npins[n] - 1);
        ++i;
        ++j;
      }
    }
    if (conn) {
      *conn = cw;
    }
    if (gone) {
      *gone = ng;
    }
    return e;
  }
  double cost(int a, int b) const {
    double dd    = std::abs(sx[a] / w[a] - sx[b] / w[b]) + std::abs(sy[a] / w[a] - sy[b] / w[b]);
    dd          *= 1 + 4 * std::max(bcrit[a], bcrit[b]);
    double conn  = 0;
    int    gone  = 0;
    merged_ext(a, b, &conn, &gone);
    return (1 + dd / unit) / (1 + conn + gone);
  }
  void join(int a, int b) {  // b into a
    ext[a]    = merged_ext(a, b);
    parent[b] = a;
    widths[a].insert(widths[a].end(), widths[b].begin(), widths[b].end());
    sites[a] += sites[b];
    pins[a]  += pins[b];
    sx[a]    += sx[b];
    sy[a]    += sy[b];
    w[a]     += w[b];
    bcrit[a]  = std::max(bcrit[a], bcrit[b]);
    slot[a]   = std::max(slot[a], slot[b]);
    Count merged;
    std::ranges::merge(nets[a], nets[b], std::back_inserter(merged));
    Count out;
    for (const auto& [n, c] : merged) {
      if (!out.empty() && out.back().first == n) {
        out.back().second += c;
      } else {
        out.emplace_back(n, c);
      }
    }
    nets[a] = std::move(out);
    nets[b].clear();
    widths[b].clear();
  }
};
}  // namespace

int cell_sites(const Design& d, const Grid& g, int m) { return int((d.width[m] + g.site - 1) / g.site); }

// A box's two-row cells take the same sites in both rows of the tile: each goes to the leftmost span free in both
// rows that holds it (widest first), and the single-row cells pack into what is left. False when a two-row cell has
// no span; tall_at gets each cell's site offset (-1 for a single-row cell) and rest the free segments left.
bool tall_layout(const Design& d, const Grid& g, const Box& b, int tile, std::vector<int>& tall_at, std::vector<Seg>& rest) {
  const auto& segs = g.segs(tile);
  rest.assign(segs.begin(), segs.end());
  tall_at.assign(b.cells.size(), -1);
  std::vector<std::pair<int, size_t>> talls;  // (sites, position in b.cells)
  for (size_t k = 0; k < b.cells.size(); ++k) {
    if (d.tall[size_t(b.cells[k])]) {
      talls.emplace_back(cell_sites(d, g, b.cells[k]), k);
    }
  }
  std::ranges::sort(talls, std::greater<>());
  for (const auto& [w, k] : talls) {
    int    at = -1;
    size_t i = 0, j = 0;
    for (size_t a = 0; a < rest.size() && at < 0; ++a) {
      for (size_t c = 0; c < rest.size() && at < 0; ++c) {
        if (rest[a].row == 0 && rest[c].row == 1) {
          const int lo = std::max(rest[a].lo, rest[c].lo), hi = std::min(rest[a].hi, rest[c].hi);
          if (hi - lo >= w) {
            at = lo;
            i  = a;
            j  = c;
          }
        }
      }
    }
    if (at < 0) {
      return false;
    }
    tall_at[k] = at;
    for (const size_t s : {i, j}) {  // cut [at, at + w) out of both segments
      const Seg a = rest[s];
      rest[s]     = {a.row, at + w, a.hi};
      if (a.lo < at) {
        rest.push_back({a.row, a.lo, at});
      }
    }
  }
  std::erase_if(rest, [](const Seg& s) { return s.hi <= s.lo; });
  return true;
}

bool box_fits(const Design& d, const Grid& g, const Box& b, int tile) {
  if (b.placed && tile == g.tile_of(b.cx, b.cy)) {  // legal_boxes: its cells are there already
    return true;
  }
  if (g.blocked[tile]) {
    return false;
  }
  std::vector<int> tall_at;
  std::vector<Seg> rest;
  if (!tall_layout(d, g, b, tile, tall_at, rest)) {
    return false;
  }
  std::vector<int> ws, room;
  for (size_t k = 0; k < b.cells.size(); ++k) {
    if (tall_at[k] < 0) {
      ws.push_back(cell_sites(d, g, b.cells[k]));
    }
  }
  for (const auto& s : rest) {
    room.push_back(s.hi - s.lo);
  }
  return ws.empty() || !pack_segments(ws, room).empty();
}

Boxes build_boxes(const Design& d, const Grid& g, const std::map<int, int>& fixed_slot) {
  Builder    b(d, g);
  const auto n = int(d.movable.size());
  b.fill       = d.opt.box_fill;
  b.unit       = double(*std::min_element(d.width.begin(), d.width.end()));
  // pin room: half a track per signal pin over a two-site-wide box, against the H tracks over the tile (box layer
  // and the routed H layers, one reserved each) with two to spare
  {
    double h = 0;
    for (const auto& l : g.layers) {
      h += l.horizontal ? double(g.tile_h) / l.pitch - d.opt.reserved_tracks : 0;
    }
    const auto box = d.tech.layers.find(g.box_layer);
    if (box != d.tech.layers.end() && box->second.pitch_y > 0) {
      h += double(g.tile_h) / std::llround(box->second.pitch_y * d.fp.dbu) - d.opt.reserved_tracks;
    }
    b.pin_limit = std::floor(h) - 2;
  }
  b.parent.resize(n);
  std::iota(b.parent.begin(), b.parent.end(), 0);
  b.widths.resize(n);
  b.sites.resize(n);
  b.pins.resize(n);
  b.sx.resize(n);
  b.sy.resize(n);
  b.w.resize(n);
  b.bcrit.assign(n, 0);
  b.slot.assign(n, -1);
  b.ext.assign(n, 0);
  b.nets.resize(n);
  b.cx.resize(n);
  b.cy.resize(n);
  b.npins.resize(d.nets.size());
  for (size_t k = 0; k < d.nets.size(); ++k) {
    b.npins[k] = int(d.nets[k].pins.size());
  }
  std::vector<int> region(n);
  for (int i = 0; i < n; ++i) {
    const auto& c = d.fp.components[d.movable[i]];
    b.cx[i]       = c.place.x + d.width[i] / 2.0;
    b.cy[i]       = c.place.y + g.row_h / 2.0;
    if (const auto s = fixed_slot.find(i); s != fixed_slot.end()) {
      b.cx[i]   = g.cx(g.col(s->second));
      b.cy[i]   = g.cy(g.row(s->second));
      b.slot[i] = s->second;
    }
    region[i]   = g.region[g.tile_of(b.cx[i], b.cy[i])];
    b.sites[i]  = cell_sites(d, g, i) * (d.tall[size_t(i)] ? 2 : 1);  // a two-row cell takes its sites in both rows
    b.widths[i] = {d.tall[size_t(i)] ? -cell_sites(d, g, i) : cell_sites(d, g, i)};
    b.w[i]      = double(d.width[i]);
    b.sx[i]     = b.cx[i] * b.w[i];
    b.sy[i]     = b.cy[i] * b.w[i];
    int signal  = 0;
    for (const auto& [_, p] : d.macro[d.movable[i]]->pins) {
      signal += p.use != "CLOCK" && p.use != "POWER" && p.use != "GROUND";
    }
    b.pins[i] = std::ceil(.5 * signal) / 2;
    for (int net : d.nets_of[i]) {
      const auto& N = d.nets[net];
      if (N.clock || N.power) {
        continue;
      }
      int in_cell = 0;
      for (const auto& p : N.pins) {
        in_cell += p.comp == d.movable[i];
      }
      b.nets[i].emplace_back(net, in_cell);
      b.ext[i]   += in_cell < b.npins[net];
      b.bcrit[i]  = std::max(b.bcrit[i], N.ramp);
    }
  }
  // cells legroute.global put on one slot share a box
  std::map<int, int> slot_root;
  for (const auto& [cell, tile] : fixed_slot) {
    const auto [it, fresh] = slot_root.try_emplace(tile, cell);
    if (!fresh && b.find(cell) != b.find(it->second)) {
      b.slot[cell] = -1;
      b.join(b.find(it->second), b.find(cell));
    }
  }
  Boxes out;
  if (d.opt.legal_boxes) {  // the tiles of the legal placement are the boxes: the cells on one tile join
    std::unordered_map<int, int> root;  // tile -> the first cell on it
    for (int i = 0; i < n; ++i) {
      if (const auto [it, fresh] = root.try_emplace(g.tile_of(b.cx[i], b.cy[i]), i); !fresh) {
        b.join(b.find(it->second), i);
      }
    }
  } else {
    // connected pairs in one region, nets of at most box_net_max cells, any distance
    using Item = std::tuple<double, int, int>;
    std::vector<Item> items;
    std::vector<int>  cells;
    for (const auto& N : d.nets) {
      if (N.clock || N.power) {
        continue;
      }
      cells.clear();
      for (const auto& p : N.pins) {
        if (p.comp >= 0 && d.cell[p.comp] >= 0) {
          cells.push_back(d.cell[p.comp]);
        }
      }
      std::ranges::sort(cells);
      cells.erase(std::unique(cells.begin(), cells.end()), cells.end());
      if (cells.size() < 2 || cells.size() > d.opt.box_net_max) {
        continue;
      }
      for (size_t x = 0; x < cells.size(); ++x) {
        for (size_t y = x + 1; y < cells.size(); ++y) {
          if (region[cells[x]] == region[cells[y]]) {
            items.emplace_back(0, cells[x], cells[y]);
          }
        }
      }
    }
    std::ranges::sort(items);
    items.erase(std::unique(items.begin(), items.end()), items.end());
    for (auto& [c, u, v] : items) {
      c = b.cost(b.find(u), b.find(v));
    }
    std::priority_queue<Item, std::vector<Item>, std::greater<>> heap(std::greater<>(), std::move(items));
    while (!heap.empty()) {
      const auto [c0, u, v] = heap.top();
      heap.pop();
      const int ru = b.find(u), rv = b.find(v);
      if (ru == rv) {
        continue;
      }
      const double c1 = b.cost(ru, rv);
      if (c1 > c0 + 1e-9) {  // stale: one of the boxes grew since this pair was scored
        heap.emplace(c1, u, v);
        continue;
      }
      if (c1 > d.opt.merge_cost) {
        break;
      }
      if (!b.fits(ru, rv)) {
        continue;  // boxes only grow: never fits again
      }
      b.join(ru, rv);
      ++out.merges;
    }
    // pack-all: part-full boxes merge with the box of their nearest member cell; each radius repeats while it
    // still merges, then grows by 1.5 up to 5 box widths
    for (double cell = 1.5 * g.tile_w; cell < 5.0 * g.tile_w;) {
      std::unordered_map<int64_t, std::vector<int>> grid;
      const auto                                    key = [&](int64_t gx, int64_t gy) { return gx * 1000003 + gy; };
      std::vector<std::vector<int>>                 members(n);
      for (int i = 0; i < n; ++i) {
        members[b.find(i)].push_back(i);
        grid[key(int64_t(b.cx[i] / cell), int64_t(b.cy[i] / cell))].push_back(i);
      }
      std::vector<int> roots;
      for (int i = 0; i < n; ++i) {
        if (b.find(i) == i) {
          roots.push_back(i);
        }
      }
      std::ranges::stable_sort(roots, [&](int x, int y) { return b.sites[x] < b.sites[y]; });
      std::vector<char> merged(n, 0);
      bool              any = false;
      for (int r : roots) {
        if (merged[r] || b.find(r) != r) {
          continue;
        }
        double best = INFINITY;
        int    to   = -1;
        for (int i : members[r]) {
          const auto ix = int64_t(b.cx[i] / cell), iy = int64_t(b.cy[i] / cell);
          for (auto ax = ix - 1; ax <= ix + 1; ++ax) {
            for (auto ay = iy - 1; ay <= iy + 1; ++ay) {
              const auto it = grid.find(key(ax, ay));
              if (it == grid.end()) {
                continue;
              }
              for (int j : it->second) {
                const int q = b.find(j);
                if (q == r || merged[q] || q == to || region[j] != region[i]) {
                  continue;
                }
                const double dd = std::abs(b.cx[i] - b.cx[j]) + std::abs(b.cy[i] - b.cy[j]);
                if (dd <= cell && dd < best && b.fits(q, r)) {
                  best = dd;
                  to   = q;
                }
              }
            }
          }
        }
        if (to >= 0) {
          b.join(to, r);
          merged[to] = merged[r] = 1;
          any                    = true;
        }
      }
      if (!any) {
        cell *= 1.5;
      }
    }
  }
  std::vector<int> id(n, -1);
  out.box_of.assign(n, -1);
  for (int i = 0; i < n; ++i) {
    const int r = b.find(i);
    if (id[r] < 0) {
      id[r] = int(out.boxes.size());
      out.boxes.emplace_back();
    }
    auto& box = out.boxes[id[r]];
    box.cells.push_back(i);
    out.box_of[i] = id[r];
  }
  for (size_t k = 0; k < out.boxes.size(); ++k) {
    auto&  box = out.boxes[k];
    double ws  = 0;
    for (int i : box.cells) {
      box.cx    += b.cx[i] * d.width[i];
      box.cy    += b.cy[i] * d.width[i];
      ws        += d.width[i];
      box.sites += cell_sites(d, g, i) * (d.tall[size_t(i)] ? 2 : 1);
      box.clock  = box.clock || d.flop[i];
      if (b.slot[i] >= 0) {
        box.fixed = true;
        box.tile  = b.slot[i];
      }
    }
    box.cx     /= ws;
    box.cy     /= ws;
    box.region  = g.region[g.tile_of(box.cx, box.cy)];
    if (d.opt.legal_boxes) {  // where the legalization put its cells: fixed there, or within legal_window tiles of it
      box.tile   = g.tile_of(b.cx[box.cells[0]], b.cy[box.cells[0]]);
      box.region = g.region[box.tile];
      box.placed = true;
      box.fixed  = d.opt.legal_window == 0;
      box.window = d.opt.legal_window == 0 ? -1 : int(d.opt.legal_window);
    }
    std::unordered_map<int, int> count;
    for (int i : box.cells) {
      for (int net : d.nets_of[i]) {
        const auto& N = d.nets[net];
        if (N.clock || N.power) {
          continue;
        }
        for (const auto& p : N.pins) {
          count[net] += p.comp == d.movable[i];
        }
      }
    }
    for (const auto& [net, c] : count) {
      const auto& N = d.nets[net];
      box.crit      = std::max(box.crit, N.crit);
      box.ramp      = std::max(box.ramp, N.ramp);
      box.critical  = box.critical || N.critical;
      if (c == int(N.pins.size())) {
        ++box.internal;
      } else {
        ++box.external;
      }
    }
  }
  return out;
}

void pack_cells(const Design& d, const Grid& g, const Boxes& boxes) {
  for (const auto& b : boxes.boxes) {
    if (b.placed && b.tile == g.tile_of(b.cx, b.cy)) {  // legal_boxes: on its legal tile, the cells stay as they are
      continue;
    }
    const int        c = g.col(b.tile), r = g.row(b.tile);
    std::vector<int> tall_at;
    std::vector<Seg> segs;  // the free segments left once the two-row cells have their spans
    if (!tall_layout(d, g, b, b.tile, tall_at, segs)) {
      throw std::logic_error("legroute: a snapped box's two-row cell does not fit its tile");
    }
    std::vector<int> ws, room, prefer, cells;
    // the box moved by (dx, dy) from its centroid: keep each cell's relative place
    const double     dx = g.cx(c) - b.cx;
    for (size_t k = 0; k < b.cells.size(); ++k) {
      if (tall_at[k] < 0) {
        cells.push_back(b.cells[k]);
        continue;
      }
      auto& p       = d.fp.components[d.movable[b.cells[k]]].place;  // a two-row cell: the tile's bottom row, its orientation
      p.x           = g.left(c) + int64_t(tall_at[k]) * g.site;
      p.y           = g.bottom(r);
      p.orientation = g.row_orient[2 * r];
      p.status      = physical::Status::PLACED;
    }
    if (cells.empty()) {
      continue;
    }
    for (int i : cells) {
      ws.push_back(cell_sites(d, g, i));
      const auto& p    = d.fp.components[d.movable[i]].place;
      const int   row  = std::clamp(int(std::lround(double(p.y - g.bottom(r)) / g.row_h)), 0, 1);
      const auto  x    = (p.x + dx - g.left(c)) / g.site;
      int         pref = -1;
      for (size_t s = 0; s < segs.size(); ++s) {
        if (segs[s].row == row && x >= segs[s].lo && x < segs[s].hi) {
          pref = int(s);
        }
      }
      prefer.push_back(pref);
    }
    for (const auto& s : segs) {
      room.push_back(s.hi - s.lo);
    }
    auto got = pack_segments(ws, room, &prefer);
    if (got.empty()) {
      got = pack_segments(ws, room);
    }
    if (got.empty()) {
      throw std::logic_error("legroute: a snapped box does not fit its tile");
    }
    for (size_t s = 0; s < segs.size(); ++s) {
      std::vector<int> in;
      int              total = 0;
      double           want  = INFINITY;
      for (size_t k = 0; k < cells.size(); ++k) {
        if (got[k] == int(s)) {
          in.push_back(int(k));
          total += ws[k];
          want   = std::min(want, (d.fp.components[d.movable[cells[k]]].place.x + dx - g.left(c)) / double(g.site));
        }
      }
      if (in.empty()) {
        continue;
      }
      std::ranges::sort(in, [&](int a, int e) {
        return d.fp.components[d.movable[cells[a]]].place.x < d.fp.components[d.movable[cells[e]]].place.x;
      });
      int x = std::clamp(int(std::lround(want)), segs[s].lo, segs[s].hi - total);
      for (int k : in) {
        auto& p        = d.fp.components[d.movable[cells[k]]].place;
        p.x            = g.left(c) + int64_t(x) * g.site;
        p.y            = g.bottom(r) + segs[s].row * g.row_h;
        p.orientation  = g.row_orient[2 * r + segs[s].row];
        p.status       = physical::Status::PLACED;
        x             += ws[k];
      }
    }
  }
}
}  // namespace livehd::placement::lr

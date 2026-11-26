// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// rf_route: experimental track-interval detailed router for the route-first study (todo/livehd/3r-routefirst.html).
//
// Model. Every signal wire is a standard-width interval on a routing track of a unidirectional layer, and every via
// is the technology default via at a crossing of two tracks. With that restriction the ASAP7 rules measured with
// OpenROAD's checker (repros/3r-routefirst, "rule oracle") reduce to four checks:
//   R1 same track: two pieces touch (same net, merged) or keep the end-to-end spacing;
//   R2 adjacent tracks: opposite-facing ends may not sit diagonally with a gap in [0, corner spacing);
//   R3 every piece has the minimum length;
//   R4 cuts of one cut layer keep the Euclidean cut spacing.
// A wire is legal when it is inserted, so the router never negotiates over shorts.
#include "route.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <deque>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <numeric>
#include <queue>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <vector>

#include "inou/lefdef/physical.hpp"
#include "inou/lefdef/routing_connectivity.hpp"
#include "inou/lefdef/routing_geometry.hpp"
#include "pass/route/route_identity.hpp"

namespace ph = livehd::physical;

namespace {
double now_s() {
  using namespace std::chrono;
  return duration<double>(steady_clock::now().time_since_epoch()).count();
}
double cpu_s() { return double(std::clock()) / CLOCKS_PER_SEC; }

// Values keyed by search-state id. A chip has far more states than one search reaches, so the table holds only what
// was touched since the last clear(), which is a generation bump.
template <typename V>
struct Id_map {
  struct E {
    int64_t  id  = 0;
    uint32_t gen = 0;
    V        v{};
  };
  std::vector<E> e    = std::vector<E>(size_t(1) << 12);
  int            bits = 12;
  uint32_t       gen  = 1;
  size_t         live = 0;
  void           clear() {
    if (++gen == 0) {
      for (auto& x : e) {
        x.gen = 0;
      }
      gen = 1;
    }
    live = 0;
  }
  // Neighbouring stops of a track share a run of slots: a search walks along tracks, so its probes stay in cache.
  size_t slot(int64_t id) const {
    return ((size_t((uint64_t(id >> 5) * 0x9E3779B97F4A7C15ull) >> (64 - bits)) & ~size_t(31)) | size_t(id & 31));
  }
  const V* find(int64_t id) const {
    if (live == 0) {
      return nullptr;
    }
    for (size_t i = slot(id);; i = (i + 1) & (e.size() - 1)) {
      if (e[i].gen != gen) {
        return nullptr;
      }
      if (e[i].id == id) {
        return &e[i].v;
      }
    }
  }
  V*   find(int64_t id) { return const_cast<V*>(static_cast<const Id_map*>(this)->find(id)); }
  bool has(int64_t id) const {
    const V* p = find(id);
    return p && *p;
  }
  V& at(int64_t id) {  // the entry of `id`, added with a default value when absent
    if ((live + 1) * 2 > e.size()) {
      std::vector<E> old(size_t(1) << ++bits);
      old.swap(e);
      live = 0;
      for (const auto& x : old) {
        if (x.gen == gen) {
          at(x.id) = x.v;
        }
      }
    }
    for (size_t i = slot(id);; i = (i + 1) & (e.size() - 1)) {
      if (e[i].gen != gen) {
        e[i] = {id, gen, V{}};
        ++live;
        return e[i].v;
      }
      if (e[i].id == id) {
        return e[i].v;
      }
    }
  }
};

struct Seg {
  int lo, hi, net;  // net >= 0: signal id, -1: obstacle
};
struct Track {
  std::vector<Seg> s;  // sorted by lo, disjoint
};
struct Cut {
  int xl, yl, xh, yh, net;
};
struct Layer {
  std::string        name;
  bool               horiz = false;
  int                w = 0, e2e = 0, cs = 0, minlen = 0, ext = 0, pitch = 0, wide_clear = 0;
  double             cost = 1.0;
  std::vector<int>   tc;       // track coordinate, perpendicular to the wires
  std::vector<int>   stops;    // along coordinates where a via may sit
  std::vector<int>   stop_up;  // stop index -> track index in the layer above, or -1
  std::vector<int>   stop_dn;
  std::vector<int>   tk_up;  // track index -> index of tc[t] in stops of the layer above
  std::vector<int>   tk_dn;
  std::vector<Track> tr;
  int64_t            base = 0;
};
struct Cut_layer {
  std::string                                    name, via;
  int                                            hx = 0, hy = 0, space = 0;  // half sizes of the default cut
  std::unordered_map<uint64_t, std::vector<Cut>> buckets;
};
constexpr int bucket = 256;
uint64_t      bkey(int bx, int by) { return (uint64_t(uint32_t(bx)) << 32) | uint32_t(by); }

struct Cand {
  int  L = 0, t = 0, s = 0;  // state on the first routing layer the pin reaches
  int  x = 0, y = 0;
  bool v1   = false;  // needs a V1 from the M1 pin
  int  stub = -1;     // selected macro-pin extension, if this access needs one
  int  cut  = -1;     // cut layer of the via at (x, y) that joins the pin to this state; 0 with v1
};
struct Pin {
  int               comp = -1;  // component index, -1 for a terminal
  std::string       name, lef;
  int               cx = 0, cy = 0;
  std::vector<Cand> cands;
  int               used = -1;  // chosen candidate once connected
  bool              conn = false, extra_access = false;
};
struct Via_rec {
  int cl, x, y;
};
struct Net {
  std::string          name;
  std::vector<Pin>     pins;
  bool                 skip = false, routed = false, failed = false, clock = false;
  bool                 spaced   = false;  // a nondefault rule asks for an empty track on each side
  int64_t              expanded = 0;      // search expansions spent on the net, over every attempt
  int                  calls    = 0;
  std::vector<Via_rec> vias;
  int                  bbox_hp    = 0;
  int                  fail_pin   = -1;
  int                  fail_other = -1;  // the tree-side pin the failed one was aiming at
  std::string          fail_why;
  int                  rips  = 0;
  double               slack = 1e30;
  bool                 gr_on = false;
};

using livehd::routing::Settings;

struct Router {
  ph::Technology         tech;
  ph::Floorplan          fp;
  std::vector<Layer>     L;   // M2..M7
  std::vector<Cut_layer> CL;  // V1..V6 ; CL[k] joins L[k-1] (or M1) and L[k]
  std::vector<Net>       nets;
  Settings               opt;
  int64_t                nstates = 0;
  struct Node {
    float   g     = std::numeric_limits<float>::infinity();
    int     entry = 0;       // along coordinate where the path came onto this track
    int     fl = 0, fh = 0;  // free stretch of the track around the state
    int64_t parent = -1;
  };
  Id_map<Node>                   node;       // states the running search reached
  Id_map<uint8_t>                mark, ban;  // tree states of the net being routed; states a rejected commit ruled out
  Id_map<uint8_t>                pocket_seen, pocket_goal;  // bounded BFS tables for walled-in pocket detection
  int64_t                        expansions = 0, search_budget = 0;
  const char*                    why     = "";
  double                         time_ok = 0, time_fail = 0, anchor_slack = -1e30;
  bool                           leftovers   = false;
  double                         space_slack = -1e30;
  double                         crit_slack  = -1e30;
  int64_t                        exp_ok = 0, exp_fail = 0;
  // statistics
  std::map<std::string, int64_t> stat;

  // ---------- geometry helpers ----------
  static int find(const std::vector<int>& v, int c) {
    auto it = std::lower_bound(v.begin(), v.end(), c);
    return it != v.end() && *it == c ? int(it - v.begin()) : -1;
  }
  int64_t sid(int l, int t, int s) const { return L[l].base + int64_t(t) * int64_t(L[l].stops.size()) + s; }
  void    unsid(int64_t id, int& l, int& t, int& s) const {
    l = int(L.size()) - 1;
    while (id < L[l].base) {
      --l;
    }
    id -= L[l].base;
    t   = int(id / int64_t(L[l].stops.size()));
    s   = int(id % int64_t(L[l].stops.size()));
  }
  void xy(int l, int t, int s, int& x, int& y) const {
    if (L[l].horiz) {
      x = L[l].stops[s];
      y = L[l].tc[t];
    } else {
      x = L[l].tc[t];
      y = L[l].stops[s];
    }
  }

  // Range of centre coordinates around `a` where a piece of `net` keeps R1 against other owners.
  // During a move probe the wires of the nets on the probed cell do not count: they would be ripped with the move.
  std::vector<uint32_t> probe_skip;
  uint32_t              probe_tag     = 0;
  bool                  probing       = false;
  // Spacing kept beside a critical wire: a keep-out piece on the neighbouring track, owned by that net.
  // Pin-escape corridors on shared-track cells use `pin_keep_base` so adjacent tracks also keep R2 corner clearance.
  static constexpr int  keep_base     = -100000000;
  static constexpr int  pin_keep_base = -500000000;
  static int            keep_of(int net) { return keep_base - net; }
  static int            pin_keep_of(int net) { return pin_keep_base - net; }
  static bool           is_pin_keep(int owner) { return owner <= pin_keep_base; }
  static bool           is_keep(int owner) { return owner <= keep_base; }
  static bool           own_keep(int owner, int net) { return net >= 0 && (owner == keep_of(net) || owner == pin_keep_of(net)); }
  static int            owner_net(int owner) {
    return is_pin_keep(owner) ? pin_keep_base - owner : (is_keep(owner) ? keep_base - owner : owner);
  }
  bool skipped(int owner) const {
    if (!probing) {
      return false;
    }
    const int n = owner_net(owner);
    return n >= 0 && size_t(n) < probe_skip.size() && probe_skip[n] == probe_tag;
  }
  bool free_range(int l, int t, int net, int a, int& fl, int& fh) const {
    const auto& T = L[l].tr[t].s;
    const int   m = L[l].e2e + L[l].ext;
    fl            = -(1 << 28);
    fh            = 1 << 28;
    auto it       = std::upper_bound(T.begin(), T.end(), a, [](int v, const Seg& s) { return v < s.lo; });
    for (auto r = it; r != T.end(); ++r) {
      if (r->net != net && !skipped(r->net) && !own_keep(r->net, net)) {
        fh = r->lo - m;
        break;
      }
    }
    for (auto l2 = it; l2 != T.begin();) {
      --l2;
      if (l2->net != net && !skipped(l2->net) && !own_keep(l2->net, net)) {
        fl = l2->hi + m;
        break;
      }
    }
    return fl <= a && a <= fh;
  }
  bool own_at(int l, int t, int net, int a) const {
    const auto& T  = L[l].tr[t].s;
    auto        it = std::upper_bound(T.begin(), T.end(), a, [](int v, const Seg& s) { return v < s.lo; });
    if (it == T.begin()) {
      return false;
    }
    --it;
    return it->net == net && it->hi >= a;
  }
  bool cut_ok(int k, int x, int y, int net, bool* already_owned = nullptr) const {
    const auto& c  = CL[k];
    const int   xl = x - c.hx, xh = x + c.hx, yl = y - c.hy, yh = y + c.hy;
    const int   r = c.space;
    for (int bx = (xl - r) / bucket; bx <= (xh + r) / bucket; ++bx) {
      for (int by = (yl - r) / bucket; by <= (yh + r) / bucket; ++by) {
        auto it = c.buckets.find(bkey(bx, by));
        if (it == c.buckets.end()) {
          continue;
        }
        for (const auto& o : it->second) {
          if (o.net == net && o.xl == xl && o.yl == yl && o.xh == xh && o.yh == yh) {
            if (already_owned) {
              *already_owned = true;
            }
            continue;
          }
          if (own_keep(o.net, net) || skipped(o.net)) {
            continue;
          }
          const int64_t dx = std::max({0, o.xl - xh, xl - o.xh});
          const int64_t dy = std::max({0, o.yl - yh, yl - o.yh});
          if (dx * dx + dy * dy < int64_t(r) * r) {
            return false;
          }
        }
      }
    }
    return true;
  }
  void add_cut_rect(int k, int xl, int yl, int xh, int yh, int net) {
    auto& c = CL[k];
    for (int bx = xl / bucket; bx <= xh / bucket; ++bx) {
      for (int by = yl / bucket; by <= yh / bucket; ++by) {
        c.buckets[bkey(bx, by)].push_back({xl, yl, xh, yh, net});
      }
    }
  }
  void add_cut(int k, int x, int y, int net) { add_cut_rect(k, x - CL[k].hx, y - CL[k].hy, x + CL[k].hx, y + CL[k].hy, net); }
  void del_cut(int k, int x, int y, int net) {
    auto&     c  = CL[k];
    const int xl = x - c.hx, xh = x + c.hx, yl = y - c.hy, yh = y + c.hy;
    for (int bx = xl / bucket; bx <= xh / bucket; ++bx) {
      for (int by = yl / bucket; by <= yh / bucket; ++by) {
        auto& v = c.buckets[bkey(bx, by)];
        for (size_t i = 0; i < v.size(); ++i) {
          if (v[i].net == net && v[i].xl == xl && v[i].yl == yl) {
            v[i] = v.back();
            v.pop_back();
            break;
          }
        }
      }
    }
  }

  // ---------- obstacles ----------
  void block(int l, int t, int lo, int hi, int net) {
    auto& T  = L[l].tr[t].s;
    auto  it = std::lower_bound(T.begin(), T.end(), lo, [](const Seg& s, int v) { return s.hi < v; });
    auto  e  = it;
    while (e != T.end() && e->lo <= hi) {
      lo = std::min(lo, e->lo);
      hi = std::max(hi, e->hi);
      if (e->net != net) {
        for (int owner : {e->net, net}) {  // a cell whose shape fused with something else stays where it is
          if (owner <= -10) {
            pinned.insert(-10 - owner);
          }
        }
        net = -1;
      }
      ++e;
    }
    it = T.erase(it, e);
    T.insert(it, {lo, hi, net});
  }
  void obstacle(const std::string& layer, const ph::Rect& r, int net) {
    for (size_t k = 0; k < CL.size(); ++k) {
      if (CL[k].name == layer) {
        add_cut_rect(int(k), int(r.xl), int(r.yl), int(r.xh), int(r.yh), net);
        return;
      }
    }
    for (size_t l = 0; l < L.size(); ++l) {
      auto& ly = L[l];
      if (ly.name != layer) {
        continue;
      }
      const int  plo = int(ly.horiz ? r.yl : r.xl), phi = int(ly.horiz ? r.yh : r.xh);
      const int  alo = int(ly.horiz ? r.xl : r.yl), ahi = int(ly.horiz ? r.xh : r.yh);
      const bool wide     = phi - plo > ly.w;
      const int  clear    = wide ? ly.wide_clear : ly.w;  // minimum spacing equals the width on every routed layer
      // A standard wire centred on a track obeys the interval rules, so only that track is touched.
      const int  on_track = phi - plo == ly.w ? find(ly.tc, (plo + phi) / 2) : -1;
      if (on_track >= 0 && plo == ly.tc[on_track] - ly.w / 2) {
        block(int(l), on_track, alo, ahi, net);
        return;
      }
      const int reach = std::max(clear, ly.cs);  // ends near an off-track corner violate the corner spacing
      auto      b     = std::lower_bound(ly.tc.begin(), ly.tc.end(), plo - reach - ly.w);
      for (; b != ly.tc.end() && *b <= phi + reach + ly.w; ++b) {
        const int c   = *b;
        const int gap = std::max(plo - (c + ly.w / 2), (c - ly.w / 2) - phi);
        if (gap >= reach) {
          continue;
        }
        // Wide metal keeps the wide spacing past its ends as well; R1 adds the end-to-end spacing to this margin.
        const int m = wide ? std::max(0, ly.wide_clear - ly.e2e) + (gap <= 0 ? 0 : ly.cs) : gap <= 0 ? 0 : ly.cs;
        block(int(l), int(b - ly.tc.begin()), alo - m, ahi + m, net <= -10 ? net : -1);
      }
      return;
    }
  }

  // Upper-layer shapes of a cell (obstructions and non-M1 pin metal) are owned by the cell so that they can move
  // with it: owner id -10 - component.
  bool comp_obstacles(int ci, bool insert) {
    const auto& c    = fp.components[ci];
    const auto& m    = tech.macros.at(c.macro.str());
    const int   id   = -10 - ci;
    bool        ok   = true;
    const auto  each = [&](const ph::Shape& sh) {
      if (sh.layer == "M1" || (sh.layer[0] != 'M' && sh.layer[0] != 'V')) {
        return;
      }
      const auto r = ph::placed_rect(m, c.place, fp.dbu, sh.rect);
      if (insert) {
        obstacle(sh.layer, r, id);
      } else {
        ok = ok && obstacle_fits(sh.layer, r, id);
      }
    };
    for (const auto& o : m.obstructions) {
      each(o);
    }
    for (const auto& [pn, p] : m.pins) {
      const bool wired = pin_net.count(c.name.str() + "/" + pn) > 0;
      for (const auto& sh : p.shapes) {
        if (wired && sh.layer[0] == 'M' && sh.layer != "M1") {
          if (insert) {
            pinned.insert(ci);  // its pin metal is routed on directly; the cell stays
          } else {              // the strap must clear the wires already there, like any other shape of the cell
            ok = ok && obstacle_fits(sh.layer, ph::placed_rect(m, c.place, fp.dbu, sh.rect), id);
          }
          continue;
        }
        each(sh);
      }
    }
    return ok;
  }
  // Take the shapes a cell owns out of the tracks and cut buckets. They all lie within the cell footprint plus the
  // margin an obstacle is inflated by, so only that neighbourhood is searched.
  void remove_owner(int id) {
    const int   c  = -10 - id;
    const auto& pl = fp.components[c].place;
    const int   m  = 240;
    const int   xl = int(pl.x) - m, xh = int(pl.x) + cinfo[c].w + m, yl = int(pl.y) - m, yh = int(pl.y) + 270 + m;
    for (auto& ly : L) {
      auto b = std::lower_bound(ly.tc.begin(), ly.tc.end(), ly.horiz ? yl : xl);
      for (; b != ly.tc.end() && *b <= (ly.horiz ? yh : xh); ++b) {
        auto& t = ly.tr[b - ly.tc.begin()];
        if (!t.s.empty()) {
          t.s.erase(std::remove_if(t.s.begin(), t.s.end(), [&](const Seg& sg) { return sg.net == id; }), t.s.end());
        }
      }
    }
    for (auto& cl : CL) {
      for (int bx = xl / bucket - 1; bx <= xh / bucket + 1; ++bx) {
        for (int by = yl / bucket - 1; by <= yh / bucket + 1; ++by) {
          const auto it = cl.buckets.find(bkey(bx, by));
          if (it != cl.buckets.end()) {
            auto& v = it->second;
            v.erase(std::remove_if(v.begin(), v.end(), [&](const Cut& k) { return k.net == id; }), v.end());
          }
        }
      }
    }
  }
  // Would this obstacle keep its distance from the wires already there?
  bool obstacle_fits(const std::string& layer, const ph::Rect& r, int id) const {
    for (size_t k = 0; k < CL.size(); ++k) {
      if (CL[k].name == layer) {
        const auto& c = CL[k];
        for (int bx = (int(r.xl) - c.space) / bucket; bx <= (int(r.xh) + c.space) / bucket; ++bx) {
          for (int by = (int(r.yl) - c.space) / bucket; by <= (int(r.yh) + c.space) / bucket; ++by) {
            auto it = c.buckets.find(bkey(bx, by));
            if (it == c.buckets.end()) {
              continue;
            }
            for (const auto& o : it->second) {
              const int64_t dx = std::max({0, o.xl - int(r.xh), int(r.xl) - o.xh}),
                            dy = std::max({0, o.yl - int(r.yh), int(r.yl) - o.yh});
              if (o.net != id && !skipped(o.net) && dx * dx + dy * dy < int64_t(c.space) * c.space) {
                return false;
              }
            }
          }
        }
        return true;
      }
    }
    for (const auto& ly : L) {
      if (ly.name != layer) {
        continue;
      }
      const int  plo = int(ly.horiz ? r.yl : r.xl), phi = int(ly.horiz ? r.yh : r.xh);
      const int  alo = int(ly.horiz ? r.xl : r.yl), ahi = int(ly.horiz ? r.xh : r.yh);
      const auto taken = [&](size_t track) {
        for (const auto& sg : ly.tr[track].s) {
          if (sg.net != id && !skipped(sg.net) && sg.hi > alo - ly.e2e - ly.cs && sg.lo < ahi + ly.e2e + ly.cs) {
            return true;
          }
        }
        return false;
      };
      // The same reading of the shape as obstacle() makes when it enters it: a bar of the standard width centred
      // on a track is a wire of that track and meets only what is on it; other narrow metal keeps the minimum
      // spacing, wide metal the wide one. With the wide spacing for every shape, the M2 bars of a flop were
      // refused beside the M2 power rail of every row, 27 nm away (NVDLA O: 1,046 floating flops without a site).
      const bool wide     = phi - plo > ly.w;
      const int  on_track = phi - plo == ly.w ? find(ly.tc, (plo + phi) / 2) : -1;
      if (on_track >= 0 && plo == ly.tc[on_track] - ly.w / 2) {
        return !taken(size_t(on_track));
      }
      const int reach = std::max(wide ? ly.wide_clear : ly.w, ly.cs);
      auto      b     = std::lower_bound(ly.tc.begin(), ly.tc.end(), plo - reach - ly.w);
      for (; b != ly.tc.end() && *b <= phi + reach + ly.w; ++b) {
        if (std::max(plo - (*b + ly.w / 2), (*b - ly.w / 2) - phi) >= reach) {
          continue;
        }
        if (taken(size_t(b - ly.tc.begin()))) {
          return false;
        }
      }
    }
    return true;
  }

  // ---------- rows and what cells may not use ----------
  std::map<int, std::vector<std::pair<int, int>>> row_parts;  // row y -> its ROW statements as [lo, hi), sorted
  std::map<int, int>                              gap_width;  // pseudo cell -> width
  // A row can come as several ROW statements with space between them, and a placement blockage or a macro can cover
  // part of it. Each such stretch becomes a fixed pseudo cell of that row, so the row planner, the slides and the
  // pushes treat it like any other cell that does not move.
  // A macro (a block, or anything taller than a row) is not a cell of a row: the stretches it covers are closed.
  bool                                            off_rows(int c) const {
    const auto m = tech.macros.find(fp.components[c].macro.str());
    return m != tech.macros.end() && !gap_width.contains(c)
           && (m->second.height * fp.dbu > 270 + 1 || m->second.klass.starts_with("BLOCK"));
  }
  static ph::Rect footprint(const ph::Macro& macro, const Ann_place& pose, int64_t dbu) {
    const double w = std::llround(macro.width * dbu), h = std::llround(macro.height * dbu);
    return ph::orient_rect({0, 0, w, h}, w, h, pose.orientation).translated(pose.x, pose.y);
  }
  void site_obstacles() {
    const int site = 54;
    for (const auto& r : fp.rows) {
      if (r.step != site) {
        throw std::runtime_error("rf_route requires 54-DBU site rows");
      }
      row_parts[int(r.y)].emplace_back(int(r.x), int(r.x + r.count * r.step));
    }
    tech.macros["__rf_gap"];
    std::map<int, std::map<int, int>> closed;  // row y -> closed stretches, low end -> high end, disjoint
    const auto                        close = [&](int y, int lo, int hi, const char* why) {
      const auto& parts = row_parts.at(y);
      const int   x0 = parts.front().first, xe = parts.back().second;
      lo = std::max(x0, x0 + (lo - x0 >= 0 ? (lo - x0) / site : -((x0 - lo + site - 1) / site)) * site);
      hi = std::min(xe, x0 + (hi - x0 + site - 1) / site * site);
      if (lo >= hi) {
        return;
      }
      // A stretch is closed once: a row split around a macro is closed by the split, and the macro adds nothing.
      auto&                            done = closed[y];
      std::vector<std::pair<int, int>> fresh;
      int                              at = lo;
      for (auto it = done.begin(); it != done.end() && it->first < hi; ++it) {
        if (it->second <= at) {
          continue;
        }
        if (it->first > at) {
          fresh.emplace_back(at, it->first);
        }
        at = std::max(at, it->second);
      }
      if (at < hi) {
        fresh.emplace_back(at, hi);
      }
      for (const auto& [flo, fhi] : fresh) {
        done[flo] = fhi;
        ph::Component c;
        c.name                               = "__rf_gap_" + std::to_string(fp.components.size());
        c.macro                              = "__rf_gap";
        c.place.x                            = flo;
        c.place.y                            = y;
        c.place.status                       = ph::Status::FIXED;
        gap_width[int(fp.components.size())] = fhi - flo;
        fp.components.push_back(std::move(c));
        ++stat[std::string("row_stretches_closed_by_") + why];
      }
    };
    for (auto& [y, parts] : row_parts) {
      std::sort(parts.begin(), parts.end());
      for (size_t i = 1; i < parts.size(); ++i) {
        if (parts[i].first < parts[i - 1].second || (parts[i].first - parts.front().first) % site != 0) {
          throw std::runtime_error("rf_route requires the ROW statements of one Y to share a site grid without overlap");
        }
      }
    }
    for (const auto& [y, parts] : row_parts) {
      for (size_t i = 1; i < parts.size(); ++i) {
        close(y, parts[i - 1].second, parts[i].first, "split_rows");
      }
    }
    const auto over_rows = [&](const ph::Rect& r, int skip_y, const char* why) {
      for (const auto& [y, parts] : row_parts) {
        if (y != skip_y && y < r.yh && y + 270 > r.yl) {
          close(y, int(r.xl), int(r.xh), why);
        }
      }
    };
    for (const auto& b : fp.snapshot_blockages) {
      if (b.placement && !b.soft) {
        for (const auto& r : b.rectangles) {
          over_rows(r, INT32_MIN, "blockages");
        }
      }
    }
    const size_t real = fp.components.size() - gap_width.size();
    for (size_t i = 0; i < real; ++i) {  // a cell taller than a row closes the other rows it covers
      const auto& c = fp.components[i];
      const auto  m = tech.macros.find(c.macro.str());
      if (m != tech.macros.end() && (m->second.height * fp.dbu > 270 + 1 || m->second.klass.starts_with("BLOCK"))) {
        // every row it covers, its own included: the macro is then no cell of any row, only closed stretches
        over_rows(footprint(m->second, c.place, fp.dbu), INT32_MIN, "macros");
      }
    }
  }

  // ---------- setup ----------
  void setup(const std::vector<std::string>& lefs, const std::string& def) {
    tech = ph::read_lef(lefs);
    fp   = ph::read_def_snapshot(def);
    if (fp.dbu != 1000) {
      throw std::runtime_error("rf_route requires 1000 DEF units per micron for its ASAP7 rules");
    }
    for (const auto& b : fp.snapshot_blockages) {
      if (!b.polygons.empty() || b.spacing >= 0 || b.design_rule_width >= 0 || b.partial_density >= 0 || b.mask != 0) {
        throw std::runtime_error("rf_route models rectangular DEF BLOCKAGES without spacing, width, density or mask only");
      }
    }
    site_obstacles();
    struct Spec {
      const char* name;
      int         w, e2e, cs, minlen, ext, wide;
    };
    // Calibrated with the rule oracle (drt::check_drc on synthetic two-wire layouts).
    const Spec specs[] = {
        {"M2", 18, 31, 20, 37, 19, 36},
        {"M3", 18, 31, 20, 37, 19, 36},
        {"M4", 24, 48, 40, 84, 23, 72},
        {"M5", 24, 48, 40, 84, 27, 72},
        {"M6", 32, 48, 40, 69, 27, 72},
        {"M7", 32, 48, 40, 69, 27, 72}
    };
    for (const auto& sp : specs) {
      Layer ly;
      ly.name       = sp.name;
      ly.horiz      = tech.layers.at(sp.name).direction == "HORIZONTAL";
      ly.w          = sp.w;
      ly.e2e        = sp.e2e;
      ly.cs         = sp.cs;
      ly.minlen     = sp.minlen;
      ly.ext        = sp.ext;
      ly.wide_clear = sp.wide;
      std::set<int> coords;
      for (const auto& t : fp.tracks) {
        if (t.layer == sp.name && t.axis == (ly.horiz ? "Y" : "X")) {
          for (int64_t i = 0; i < t.count; ++i) {
            coords.insert(int(t.start + i * t.step));
          }
        }
      }
      if (coords.empty()) {
        throw std::runtime_error("missing preferred-direction tracks for " + ly.name);
      }
      ly.tc.assign(coords.begin(), coords.end());
      ly.pitch = INT32_MAX;
      for (size_t i = 1; i < ly.tc.size(); ++i) {
        ly.pitch = std::min(ly.pitch, ly.tc[i] - ly.tc[i - 1]);
      }
      if (ly.tc.size() == 1) {
        const auto& layer = tech.layers.at(sp.name);
        ly.pitch          = int(std::llround((ly.horiz ? layer.pitch_y : layer.pitch_x) * fp.dbu));
      }
      ly.tr.resize(ly.tc.size());
      L.push_back(std::move(ly));
    }
    L[0].cost = opt.m2_cost;
    L[1].cost = opt.m3_cost;
    L[4].cost = L[5].cost = opt.hi_cost;
    opt.hmin              = std::min({1.0, opt.m2_cost, opt.m3_cost, opt.hi_cost});
    struct Cspec {
      const char *name, *via;
      int         hx, hy, space;
    };
    const Cspec cs[] = {
        {"V1", "VIA12",  9,  9, 18},
        {"V2", "VIA23",  9,  9, 18},
        {"V3", "VIA34",  9, 12, 34},
        {"V4", "VIA45", 12, 12, 34},
        {"V5", "VIA56", 12, 16, 34},
        {"V6", "VIA67", 16, 16, 34}
    };
    for (const auto& c : cs) {
      Cut_layer cl;
      cl.name  = c.name;
      cl.via   = c.via;
      cl.hx    = c.hx;
      cl.hy    = c.hy;
      cl.space = c.space;
      far_run  = std::max(far_run, cl.space + 2 * std::max(cl.hx, cl.hy));
      if (far_run > 127) {
        throw std::runtime_error("cut spacing too wide for the search key");
      }
      CL.push_back(std::move(cl));
    }
    // stops
    for (size_t l = 0; l < L.size(); ++l) {
      std::set<int> st;
      if (l == 0) {  // V1 sites: the M1 grid and its half positions
        const auto& m3 = L[1].tc;
        for (int x = m3.front() - 18; x <= m3.back() + 18; x += 18) {
          st.insert(x);
        }
      } else {
        st.insert(L[l - 1].tc.begin(), L[l - 1].tc.end());
      }
      if (l + 1 < L.size()) {
        st.insert(L[l + 1].tc.begin(), L[l + 1].tc.end());
      }
      L[l].stops.assign(st.begin(), st.end());
    }
    for (size_t l = 0; l < L.size(); ++l) {
      auto& ly = L[l];
      ly.stop_up.assign(ly.stops.size(), -1);
      ly.stop_dn.assign(ly.stops.size(), -1);
      for (size_t s = 0; s < ly.stops.size(); ++s) {
        if (l + 1 < L.size()) {
          ly.stop_up[s] = find(L[l + 1].tc, ly.stops[s]);
        }
        if (l > 0) {
          ly.stop_dn[s] = find(L[l - 1].tc, ly.stops[s]);
        }
      }
      ly.tk_up.assign(ly.tc.size(), -1);
      ly.tk_dn.assign(ly.tc.size(), -1);
      for (size_t t = 0; t < ly.tc.size(); ++t) {
        if (l + 1 < L.size()) {
          ly.tk_up[t] = find(L[l + 1].stops, ly.tc[t]);
        }
        if (l > 0) {
          ly.tk_dn[t] = find(L[l - 1].stops, ly.tc[t]);
        }
      }
      ly.base  = nstates;
      nstates += int64_t(ly.tc.size()) * int64_t(ly.stops.size());
    }

    // obstacles: power grid, routed nets, cell obstructions and foreign-layer pins
    for (const auto& n : fp.special_nets) {
      ph::visit_route_geometry(tech, fp, n, [&](const ph::Shape& s) { obstacle(s.layer, s.rect, -2); }, true);
    }
    for (const auto& b : fp.snapshot_blockages) {
      if (!b.placement) {
        for (const auto& r : b.rectangles) {
          obstacle(b.layer, r, -1);
          ++stat["routing_blockage_rects"];
        }
      }
    }
    std::unordered_map<std::string, int> comp_index;
    for (size_t i = 0; i < fp.components.size(); ++i) {
      comp_index.emplace(fp.components[i].name.str(), int(i));
    }
    // A nondefault rule is served when it keeps the default widths and asks for a spacing that one empty neighbour
    // track gives: such a net keeps the tracks beside its wires empty where they still are. Others are refused.
    std::set<std::string> spaced_rules;
    for (const auto& [net_name, rule] : fp.snapshot_ndr.net_rules) {
      if (spaced_rules.contains(rule)) {
        continue;
      }
      const auto widths  = fp.nondefault_rules.find(rule);
      const auto spacing = fp.snapshot_ndr.spacing.find(rule);
      bool       ok = widths != fp.nondefault_rules.end() && (spacing == fp.snapshot_ndr.spacing.end() || !spacing->second.hard);
      for (const auto& ly : L) {
        if (!ok) {
          break;
        }
        const auto w = widths->second.find(ly.name);
        ok           = w == widths->second.end() || w->second == ly.w;
        if (ok && spacing != fp.snapshot_ndr.spacing.end()) {
          const auto s = spacing->second.layers.find(ly.name);
          ok           = s == spacing->second.layers.end() || s->second <= 2 * ly.pitch - ly.w;
        }
      }
      if (!ok) {
        throw std::runtime_error("rf_route serves nondefault rules of default width and at most double spacing only: " + rule);
      }
      spaced_rules.insert(rule);
    }
    std::set<std::string> clock_names;  // a pose written before the clock nets were tagged can name them in a file
    if (!opt.clock_nets.empty()) {
      std::ifstream f(opt.clock_nets);
      if (!f) {
        throw std::runtime_error("cannot open clock-net list: " + opt.clock_nets);
      }
      std::string name;
      while (f >> name) {
        clock_names.insert(name);
      }
    }
    cinfo.resize(fp.components.size());
    for (size_t i = 0; i < fp.components.size(); ++i) {
      cinfo[i].movable = fp.components[i].place.status == ph::Status::PLACED && !off_rows(int(i));
    }
    for (size_t ni = 0; ni < fp.nets.size(); ++ni) {
      const auto& n           = fp.nets[ni];
      bool        named_clock = clock_names.erase(n.name.str()) > 0;
      for (const auto& c : n.connections) {  // or by an endpoint, which keeps its name when the net is renamed
        named_clock
            = clock_names.erase(c.instance.str() == "PIN" ? c.pin.str() : c.instance.str() + "/" + c.pin.str()) > 0 || named_clock;
      }
      const bool clock           = n.use.str() == "CLOCK" || named_clock;
      nets.emplace_back().clock  = clock;
      nets.back().spaced         = fp.snapshot_ndr.net_rules.contains(n.name.str());
      stat["ndr_spaced_nets"]   += nets.back().spaced;
      for (const auto& c : n.connections) {
        if (c.instance.str() == "PIN") {
          continue;
        }
        const int ci = comp_index.at(c.instance.str());
        if (!n.wires.empty()) {
          cinfo[ci].fixed_wire = true;
          cinfo[ci].movable    = false;  // its existing conductor stays at this pin
        } else if (n.connections.size() >= 2) {
          pin_net.emplace(c.instance.str() + "/" + c.pin.str(), int(ni));
          if (clock) {
            const auto& m = tech.macros.at(fp.components[ci].macro.str());
            if (m.pins.at(c.pin.str()).direction == "OUTPUT") {
              cinfo[ci].movable = false;
            } else {
              cinfo[ci].clock_sink = true;
            }
          }
        }
      }
    }
    if (!clock_names.empty()) {
      throw std::runtime_error("clock-net list names a net or endpoint absent from the DEF: " + *clock_names.begin());
    }
    def_place.resize(fp.components.size());
    for (const auto& [y, parts] : row_parts) {
      row_span[y] = {parts.front().first, parts.back().second};
      row_cells[y];
    }
    for (const auto& r : fp.rows) {
      row_orient[int(r.y)] = r.orientation;
    }
    for (const auto& [y, sp] : row_span) {
      row_ys.push_back(y);
    }
    tiles_init();
    for (size_t i = 0; i < fp.components.size(); ++i) {
      const auto& c = fp.components[i];
      const auto& m = tech.macros.at(c.macro.str());
      cinfo[i].x0 = cinfo[i].gx = int(c.place.x);
      cinfo[i].y0 = cinfo[i].gy = int(c.place.y);
      cinfo[i].w = cinfo[i].macro_w = int(std::llround(m.width * fp.dbu));
      if (const auto gap = gap_width.find(int(i)); gap != gap_width.end()) {
        cinfo[i].w = gap->second;  // a stretch of a row no cell may use
      }
      def_place[i] = c.place;
      if (const auto row = row_orient.find(int(c.place.y)); row != row_orient.end()) {
        cinfo[i].flipped = c.place.orientation == mirrored(row->second)
                           && ph::orientation_allowed(m, c.place.orientation);  // the input may hold mirrored cells already
      }
      cinfo[i].settled = !opt.prelegal || c.place.status != ph::Status::PLACED || cinfo[i].fixed_wire || off_rows(int(i));
    }
    if (opt.prelegal) {
      // A grid-aligned clock cell can still overlap a tap or another anchor in global
      // placement. Reserve immutable cells first, then legal drivers, then legal sinks.
      std::map<int, std::map<int, int>> occupied;
      const auto                        reserve_site = [&](const Comp_info& cell) {
        auto&      sites = occupied[cell.gy];
        const int  end   = cell.gx + cell.w;
        const auto next  = sites.lower_bound(cell.gx);
        if ((next != sites.end() && end > next->first) || (next != sites.begin() && std::prev(next)->second > cell.gx)) {
          return false;
        }
        sites.emplace_hint(next, cell.gx, end);
        return true;
      };
      for (size_t i = 0; i < cinfo.size(); ++i) {
        if (cinfo[i].settled && !off_rows(int(i)) && !reserve_site(cinfo[i])) {
          // A row split around a macro closes a stretch that holds the macro: the stretch takes what is still free
          // of it, and a macro under a closed stretch has nothing left to take.
          const auto m = tech.macros.find(fp.components[i].macro.str());
          if (gap_width.contains(int(i)) || (m != tech.macros.end() && m->second.height * fp.dbu > 270 + 1)) {
            auto&     sites = occupied[cinfo[i].gy];
            int       at    = cinfo[i].gx;
            const int end   = cinfo[i].gx + cinfo[i].w;
            auto      it    = sites.upper_bound(at);
            if (it != sites.begin() && std::prev(it)->second > at) {
              at = std::prev(it)->second;
            }
            while (at < end) {
              const int stop = it == sites.end() ? end : std::min(end, it->first);
              if (stop > at) {
                sites.emplace(at, stop);
              }
              if (it == sites.end()) {
                break;
              }
              at = std::max(at, it->second);
              ++it;
            }
            continue;
          }
          throw std::runtime_error("overlapping immutable components near " + fp.components[i].name.str());
        }
      }
      for (bool drivers : {true, false}) {
        for (size_t i = 0; i < cinfo.size(); ++i) {
          const auto& ci = cinfo[i];
          if (ci.settled || (drivers ? ci.movable : !ci.clock_sink)) {
            continue;
          }
          const int  x = ci.gx, y = ci.gy, end = x + ci.w;
          const auto row = row_span.find(y);
          if (row == row_span.end() || x < row->second.first || end > row->second.second || (x - row->second.first) % 54) {
            continue;
          }
          const auto& c = fp.components[i];
          if (c.place.orientation == site_orientation(int(i), y)
              && ph::orientation_allowed(tech.macros.at(c.macro.str()), c.place.orientation)) {
            cinfo[i].settled = reserve_site(ci);
          }
        }
      }
    }
    for (size_t i = 0; i < cinfo.size(); ++i) {
      if (cinfo[i].settled) {
        comp_obstacles(int(i), true);
        if (!off_rows(int(i))) {
          row_cells[cinfo[i].gy].push_back(int(i));
        }
      }
    }
    for (auto& [y, v] : row_cells) {
      std::sort(v.begin(), v.end(), [&](int a, int b) { return fp.components[a].place.x < fp.components[b].place.x; });
    }
    std::unordered_map<std::string, const ph::Terminal*> terms;
    for (const auto& t : fp.terminals) {
      terms.emplace(t.name, &t);
    }
    // nets
    std::set<std::string> only;  // a debugging run routes the listed nets alone
    if (!opt.only.empty()) {
      std::ifstream f(opt.only);
      if (!f) {
        throw std::runtime_error("cannot open net list: " + opt.only);
      }
      std::string name;
      while (f >> name) {
        only.insert(name);
      }
    }
    for (size_t ni = 0; ni < fp.nets.size(); ++ni) {
      const auto& n        = fp.nets[ni];
      auto&       net      = nets[ni];
      net.name             = n.name.str();
      const bool requested = only.erase(net.name) != 0;
      if (!n.wires.empty()) {
        ph::visit_route_geometry(tech, fp, n, [&](const ph::Shape& s) { obstacle(s.layer, s.rect, -2); });
        net.skip = true;
        ++stat["fixed_nets"];
      } else if (n.connections.size() < 2 || (!opt.only.empty() && !requested)) {
        net.skip = true;
      }
    }
    if (!only.empty()) {
      throw std::runtime_error("net list names a net absent from the DEF: " + *only.begin());
    }
    {  // ports of nets this run does not route still own their metal
      std::unordered_map<std::string, int> by_name;
      for (size_t ni = 0; ni < nets.size(); ++ni) {
        by_name.emplace(nets[ni].name, int(ni));
      }
      for (const auto& t : fp.terminals) {
        const auto it = by_name.find(t.net);
        if (it != by_name.end() && !nets[it->second].skip) {
          continue;
        }
        for (const auto& sh : t.shapes) {
          obstacle(sh.layer, ph::orient_rect(sh.rect, 0, 0, t.orientation).translated(t.x, t.y), -2);
        }
      }
    }
    own.resize(nets.size());
    pin_keeps.resize(nets.size());
    resv.resize(nets.size());
    esc_keeps.resize(nets.size());
    esc_saved.resize(nets.size());
    for (size_t ni = 0; ni < fp.nets.size(); ++ni) {
      auto& net = nets[ni];
      if (net.skip) {
        continue;
      }
      int                      xl = INT32_MAX, yl = INT32_MAX, xh = INT32_MIN, yh = INT32_MIN;
      std::vector<Own>         own_tmp;
      std::vector<std::string> pin_names_tmp;
      for (const auto& c : fp.nets[ni].connections) {
        Pin p;
        if (c.instance.str() == "PIN") {
          const auto& t = *terms.at(c.pin.str());
          p.name        = c.pin.str();
          for (const auto& s : t.shapes) {
            const auto r = ph::orient_rect(s.rect, 0, 0, t.orientation).translated(t.x, t.y);
            p.cx         = int((r.xl + r.xh) / 2);
            p.cy         = int((r.yl + r.yh) / 2);
            for (size_t l = 0; l < L.size(); ++l) {
              auto& ly = L[l];
              if (ly.name != s.layer) {
                continue;
              }
              const int plo = int(ly.horiz ? r.yl : r.xl), phi = int(ly.horiz ? r.yh : r.xh);
              const int alo = int(ly.horiz ? r.xl : r.yl), ahi = int(ly.horiz ? r.xh : r.yh);
              for (size_t t2 = 0; t2 < ly.tc.size(); ++t2) {
                if (ly.tc[t2] - ly.w / 2 < plo || ly.tc[t2] + ly.w / 2 > phi) {
                  continue;
                }
                block(int(l), int(t2), alo, ahi, int(ni));
                term_segs.push_back({int(ni), int(l), int(t2), alo, ahi});
                own_tmp.push_back({int(ni), int(l), int(t2), alo, ahi, int(net.pins.size())});
                for (size_t s2 = 0; s2 < ly.stops.size(); ++s2) {
                  if (ly.stops[s2] >= alo && ly.stops[s2] <= ahi) {
                    Cand cd;
                    cd.L = int(l);
                    cd.t = int(t2);
                    cd.s = int(s2);
                    xy(cd.L, cd.t, cd.s, cd.x, cd.y);
                    p.cands.push_back(cd);
                  }
                }
              }
            }
          }
        } else {
          p.comp = comp_index.at(c.instance.str());
          p.name = c.instance.str() + "/" + c.pin.str();
          p.lef  = c.pin.str();
          pin_access(p, c.pin.str());
        }
        xl = std::min(xl, p.cx);
        xh = std::max(xh, p.cx);
        yl = std::min(yl, p.cy);
        yh = std::max(yh, p.cy);
        pin_names_tmp.push_back(p.name);
        net.pins.push_back(std::move(p));
        ++stat["pins"];
      }
      net.bbox_hp = (xh - xl) + (yh - yl);
      for (size_t i = 1; i < net.pins.size(); ++i) {  // driver first: the tree grows from it
        const auto& pi = net.pins[i];
        if (pi.comp >= 0 && tech.macros.at(fp.components[pi.comp].macro.str()).pins.at(pi.lef).direction == "OUTPUT") {
          std::swap(net.pins[0], net.pins[i]);
          break;
        }
      }
      // (component pin lists are built after the swap below)
      // A terminal owns metal from the start, so it has to be the root of the tree.
      for (size_t i = 1; i < net.pins.size(); ++i) {
        if (net.pins[i].comp < 0 && net.pins[0].comp >= 0) {
          std::swap(net.pins[0], net.pins[i]);
        } else if (net.pins[i].comp < 0) {
          ++stat["extra_terminals"];
        }
      }
      for (auto o : own_tmp) {  // pin positions changed with the swaps above; find them again by name
        const auto& name = pin_names_tmp[o.pin];
        for (size_t i = 0; i < net.pins.size(); ++i) {
          if (net.pins[i].name == name) {
            o.pin = int(i);
          }
        }
        own[ni].push_back(o);
      }
      for (size_t i = 0; i < net.pins.size(); ++i) {
        if (net.pins[i].comp >= 0) {
          auto& ci = cinfo[net.pins[i].comp];
          ci.pins.emplace_back(int(ni), int(i));
        }
      }
    }
    for (size_t c = 0; c < cinfo.size(); ++c) {
      if (cinfo[c].settled) {
        attach_cell(int(c));
      } else {
        for (const auto& [n2, pi] : cinfo[c].pins) {
          nets[n2].pins[pi].cands.clear();
        }
        ++stat["cells_floating"];
      }
    }
    for (size_t c = 0; c < cinfo.size(); ++c) {  // counted once the straps and macro pins have their accesses
      if (!cinfo[c].settled) {
        continue;
      }
      for (const auto& [n2, pi] : cinfo[c].pins) {
        if (nets[n2].pins[pi].cands.empty()) {
          ++stat["pins_without_access"];
          if (opt.verbose) {
            std::cerr << "no access " << nets[n2].name << ' ' << nets[n2].pins[pi].name << ' ' << fp.components[c].macro.str()
                      << '\n';
          }
        }
      }
    }
    // Route-first: a floating cell asks its row for the room its pins need, not only for its own width, so cells with
    // many pins on few sites do not settle side by side (NVDLA O: the pins the router cannot reach sit where the pins
    // alone take most of the M3 tracks). The extra room is on the right of the cell. If the rows cannot hold the
    // design that way, the plan is made again with the plain widths.
    std::vector<std::pair<int, int>> roomy;  // cell, its own width
    if (opt.prelegal && opt.pin_footprint > 0) {
      for (size_t c = 0; c < cinfo.size(); ++c) {
        const auto m = tech.macros.find(fp.components[c].macro.str());
        if (cinfo[c].settled || !cinfo[c].movable || m == tech.macros.end() || !m->second.klass.starts_with("CORE")
            || gap_width.contains(int(c))) {
          continue;
        }
        const auto   pins    = std::count_if(m->second.pins.begin(), m->second.pins.end(), [](const auto& pin) {
          return pin.second.use != "POWER" && pin.second.use != "GROUND";
        });
        const double site_um = 54.0 / fp.dbu, row_um = m->second.height;
        const double requested = std::ceil(pins / (opt.pin_footprint * site_um * row_um)) * 54;
        const int    need      = int(std::min(requested, double(cinfo[c].w) + 8 * 54));
        if (need > cinfo[c].w) {
          roomy.emplace_back(int(c), cinfo[c].w);
          cinfo[c].w = need;
        }
      }
    }
    spread_targets();
    plan_rows();
    if (!roomy.empty() && stat["plan_cells_left_out"] > 0) {
      for (const auto& [c, w] : roomy) {
        cinfo[c].w = w;
      }
      roomy.clear();
      stat["pin_footprint_given_up"] = 1;
      stat.erase("plan_cells_without_row");
      plan_rows();
    }
    stat["pin_footprint_cells"] = int64_t(roomy.size());
    // A cell with metal of its own above M1 cannot take a site under wires that are already there, so these take
    // their sites now, before anything is routed.
    for (size_t c = 0; c < cinfo.size(); ++c) {
      if (cinfo[c].settled) {
        continue;
      }
      if (upper_metal(int(c)) || !cinfo[c].movable || cinfo[c].clock_sink) {
        stat["cells_settled_up_front"] += settle(int(c));
      }
    }
    if (!opt.reserve_cells.empty()) {
      std::ifstream cells(opt.reserve_cells);
      if (!cells) {
        throw std::runtime_error("cannot open pin-access cell list: " + opt.reserve_cells);
      }
      std::set<int> requested;
      for (std::string name; cells >> name;) {
        auto it = comp_index.find(name);
        if (it == comp_index.end()) {
          throw std::runtime_error("unknown pin-access cell: " + name);
        }
        requested.insert(it->second);
      }
      for (int c : requested) {
        if (cinfo[c].settled && reserve_cell(c)) {
          ++stat["cells_reserved_up_front"];
        }
      }
    }
    cell_grid_init();
    for (size_t c = 0; c < cinfo.size(); ++c) {
      cinfo[c].shared_tracks = is_shared_track_cell(int(c));
      if (cinfo[c].shared_tracks) {
        ++stat["shared_track_cells"];
        if (cinfo[c].settled) {
          cell_grid_update(int(c), 1);
        }
      }
    }
    if (opt.access_patterns) {
      // Order shared-track cells with the most shared-track pins first so the tightest cells pick patterns first.
      std::vector<int> shared_cells;
      for (size_t c = 0; c < cinfo.size(); ++c) {
        if (cinfo[c].settled && cinfo[c].shared_tracks && !cinfo[c].reserved) {
          shared_cells.push_back(int(c));
        }
      }
      std::stable_sort(shared_cells.begin(), shared_cells.end(), [&](int a, int b) {
        return cinfo[a].pins.size() > cinfo[b].pins.size();
      });
      for (int c : shared_cells) {
        if (reserve_cell(c)) {
          ++stat["pattern_cells_reserved"];
        }
      }
    }
    tiles_capacity();
  }

  // Candidates and own metal of every pin of a cell at its current pose.
  // A macro pin is a short bar at the macro edge, on a routing layer and off its track grid. The bar is extended out
  // of the macro, as metal of the macro, and reached by a via from a track of the layer above or below that crosses
  // it. The layer below is covered by the macro, so it reaches the part outside only.
  // Other nets may not wall a macro pin in: one track above and one below keep a short stretch in front of the pin
  // for its net. A keep-out is not metal; the net's own wire takes it over, and a rip-up puts it back.
  struct Pin_keep {
    int l, t, lo, hi;
  };
  std::vector<std::vector<Pin_keep>> pin_keeps;  // per net
  bool                               keep_piece(int l, int t, int lo, int hi, int at, int ni) {
    const auto&      ly   = L[l];
    auto&            T    = L[l].tr[t].s;
    int              from = lo;
    std::vector<Seg> add;
    for (const auto& sg : T) {  // the free stretches of [lo, hi], an end-to-end spacing away from what is there
      if (sg.hi < lo - ly.e2e || sg.lo > hi + ly.e2e) {
        continue;
      }
      if (sg.lo - ly.e2e - from >= ly.minlen) {
        add.push_back({from, sg.lo - ly.e2e, keep_of(ni)});
      }
      from = std::max(from, sg.hi + ly.e2e);
    }
    if (hi - from >= ly.minlen) {
      add.push_back({from, hi, keep_of(ni)});
    }
    bool covers = false;
    for (const auto& k : add) {
      if (k.lo <= at - ly.ext && k.hi >= at + ly.ext) {  // only the stretch that holds the landing is worth keeping
        T.insert(std::lower_bound(T.begin(), T.end(), k.lo, [](const Seg& sg, int v) { return sg.lo < v; }), k);
        covers = true;
      }
    }
    return covers;
  }
  struct Stub {
    int net, pin, l;
    int plo, phi, pin_lo, pin_hi, lo, hi;  // across the layer direction; the pin and the whole bar along it
  };
  std::vector<Stub> stubs;
  // Does the stub keep its distance from what is there? What lies over the macro and its keep-out margin is the
  // macro's own, also where a power via fused with it into an anonymous piece.
  bool              stub_fits(size_t l, int plo, int phi, int alo, int ahi, int box_lo, int box_hi, int id) const {
    const auto& ly    = L[l];
    const int   reach = std::max(ly.wide_clear, ly.cs);
    const int   own   = std::max(0, ly.wide_clear - ly.e2e) + ly.cs;
    auto        b     = std::lower_bound(ly.tc.begin(), ly.tc.end(), plo - reach - ly.w);
    for (; b != ly.tc.end() && *b <= phi + reach + ly.w; ++b) {
      if (std::max(plo - (*b + ly.w / 2), (*b - ly.w / 2) - phi) >= reach) {
        continue;
      }
      for (const auto& sg : ly.tr[b - ly.tc.begin()].s) {
        if (sg.net == id) {
          continue;
        }
        const int lo = alo - ly.e2e - ly.cs, hi = ahi + ly.e2e + ly.cs;
        if ((sg.lo < box_lo - own && std::min(sg.hi, box_lo - own) > lo && sg.lo < hi)
            || (sg.hi > box_hi + own && sg.hi > lo && std::max(sg.lo, box_hi + own) < hi)) {
          return false;
        }
      }
    }
    return true;
  }
  void edge_pin_access(int c, int ni, int pi, size_t l, const ph::Rect& r0) {
    const ph::Rect r{std::round(r0.xl), std::round(r0.yl), std::round(r0.xh), std::round(r0.yh)};
    const auto&    comp = fp.components[c];
    const auto&    m    = tech.macros.at(comp.macro.str());
    auto&          ly   = L[l];
    auto&          p    = nets[ni].pins[pi];
    const auto     box  = footprint(m, comp.place, fp.dbu);
    const int      plo = int(ly.horiz ? r.yl : r.xl), phi = int(ly.horiz ? r.yh : r.xh);
    const int      alo = int(ly.horiz ? r.xl : r.yl), ahi = int(ly.horiz ? r.xh : r.yh);
    const int      box_lo = int(std::lround(ly.horiz ? box.xl : box.yl)), box_hi = int(std::lround(ly.horiz ? box.xh : box.yh));
    p.cx = int((r.xl + r.xh) / 2);
    p.cy = int((r.yl + r.yh) / 2);
    if (phi - plo != ly.w) {
      ++stat["edge_pin_width_unsupported"];
      if (opt.verbose) {
        std::cerr << "edge pin width " << p.name << ' ' << ly.name << ' ' << int(r.xl) << ' ' << int(r.yl) << ' ' << int(r.xh)
                  << ' ' << int(r.yh) << '\n';
      }
      return;
    }
    int lo = alo, hi = ahi;
    if (alo <= box_lo || ahi >= box_hi) {
      const bool down     = alo <= box_lo;
      bool       done     = false;
      // A power strap can block the long extension while leaving room for
      // the nearest landing. Every shorter trial must still meet the layer's
      // minimum length together with the LEF pin and pass the same spacing test.
      const int  shortest = std::max(ly.ext, ly.minlen - (ahi - alo));
      for (int reach : {200, 120, std::max(shortest, ly.pitch), shortest}) {
        if (done) {
          break;
        }
        const int      slo = down ? alo - reach : ahi, shi = down ? alo : ahi + reach;
        const ph::Rect stub = ly.horiz ? ph::Rect{double(slo), double(plo), double(shi), double(phi)}
                                       : ph::Rect{double(plo), double(slo), double(phi), double(shi)};
        if (stub_fits(l, plo, phi, slo, shi, box_lo, box_hi, -10 - c)) {
          obstacle(ly.name, stub, -10 - c);
          lo   = std::min(lo, slo);
          hi   = std::max(hi, shi);
          done = true;
        }
      }
      stat[done ? "edge_pin_stubs" : "edge_pin_stub_blocked"] += 1;
      if (!done && opt.verbose) {
        std::cerr << "stub blocked " << p.name << ' ' << ly.name << " pin " << int(r.xl) << ' ' << int(r.yl) << ' ' << int(r.xh)
                  << ' ' << int(r.yh) << '\n';
      }
    }
    stubs.push_back({ni, pi, int(l), plo, phi, alo, ahi, lo, hi});
    const int pc = (plo + phi) / 2;
    for (int d : {1, -1}) {
      const int la = int(l) + d;
      if (la < 0 || la >= int(L.size())) {
        continue;
      }
      const auto& a     = L[la];
      const int   cut   = std::max(int(l), la);
      int         first = lo + ly.ext, last = hi - ly.ext;
      auto        tk     = std::lower_bound(a.tc.begin(), a.tc.end(), first);
      const bool  direct = tk == a.tc.end() || *tk > last;
      if (direct) {
        // An existing macro pin supplies its own metal. A via whose complete
        // enclosure fits can land even when a new wire's end margin would not.
        const auto via = tech.vias.find(CL[cut].via);
        if (via == tech.vias.end() || !via->second.unsupported.empty()) {
          continue;
        }
        ph::Rect pad{1e18, 1e18, -1e18, -1e18};
        for (const auto& shape : via->second.shapes) {
          if (shape.layer == ly.name) {
            pad = pad.hull(shape.rect);
          }
        }
        if (pad.empty() || pc + std::lround((ly.horiz ? pad.yl : pad.xl) * fp.dbu) < plo
            || pc + std::lround((ly.horiz ? pad.yh : pad.xh) * fp.dbu) > phi) {
          continue;
        }
        first = alo - int(std::lround((ly.horiz ? pad.xl : pad.yl) * fp.dbu));
        last  = ahi - int(std::lround((ly.horiz ? pad.xh : pad.yh) * fp.dbu));
        tk    = std::lower_bound(a.tc.begin(), a.tc.end(), first);
      }
      for (; tk != a.tc.end() && *tk <= last; ++tk) {
        auto above = std::lower_bound(a.stops.begin(), a.stops.end(), pc);
        for (auto st : {above, above == a.stops.begin() ? above : above - 1}) {
          if (st == a.stops.end()) {
            continue;
          }
          Cand cd;
          cd.L    = la;
          cd.t    = int(tk - a.tc.begin());
          cd.s    = int(st - a.stops.begin());
          cd.cut  = cut;
          cd.stub = direct ? -1 : int(stubs.size()) - 1;
          cd.x    = ly.horiz ? *tk : pc;
          cd.y    = ly.horiz ? pc : *tk;
          p.cands.push_back(cd);
          if (*st == pc) {
            break;
          }
        }
      }
      // the landing kept for this pin: the crossing track furthest out of the macro that is still free there
      std::vector<int> order;
      for (auto u = std::lower_bound(a.tc.begin(), a.tc.end(), first); u != a.tc.end() && *u <= last; ++u) {
        order.push_back(int(u - a.tc.begin()));
      }
      if (hi > ahi) {
        std::reverse(order.begin(), order.end());
      }
      for (const int u : order) {
        int fl, fh;
        if (free_range(la, u, ni, pc, fl, fh) && cut_ok(std::max(int(l), la), ly.horiz ? a.tc[u] : pc, ly.horiz ? pc : a.tc[u], ni)
            && keep_piece(la, u, pc - 150, pc + 150, pc, ni)) {
          pin_keeps[ni].push_back({la, u, pc - 150, pc + 150});
          ++stat["edge_pin_landings_kept"];
          break;
        }
      }
    }
  }
  void attach_cell(int c) {
    const auto& comp = fp.components[c];
    const auto& m    = tech.macros.at(comp.macro.str());
    for (const auto& [ni, pi] : cinfo[c].pins) {
      auto& p = nets[ni].pins[pi];
      pin_access(p, p.lef);
      bool       direct = false;
      const bool macro  = m.klass.starts_with("BLOCK");
      for (const auto& sh : m.pins.at(p.lef).shapes) {
        for (size_t l = 0; l < L.size(); ++l) {
          auto& ly = L[l];
          if (ly.name != sh.layer) {
            continue;
          }
          const auto r = ph::placed_rect(m, comp.place, fp.dbu, sh.rect);
          if (macro) {
            edge_pin_access(c, ni, pi, l, r);
            continue;
          }
          const int plo = int(ly.horiz ? r.yl : r.xl), phi = int(ly.horiz ? r.yh : r.xh);
          const int alo = int(ly.horiz ? r.xl : r.yl), ahi = int(ly.horiz ? r.xh : r.yh);
          const int t2 = find(ly.tc, (plo + phi) / 2);
          if (t2 < 0 || phi - plo != ly.w) {
            continue;
          }
          if (!direct) {
            p.cands.clear();  // the strap joins the M1 islands; reach the pin on the strap
            direct = true;
          }
          block(int(l), t2, alo, ahi, ni);
          term_segs.push_back({ni, int(l), t2, alo, ahi});
          own[ni].push_back({ni, int(l), t2, alo, ahi, pi});
          auto s2 = std::lower_bound(ly.stops.begin(), ly.stops.end(), alo + ly.ext);
          for (; s2 != ly.stops.end() && *s2 <= ahi - ly.ext; ++s2) {
            Cand cd;
            cd.L = int(l);
            cd.t = t2;
            cd.s = int(s2 - ly.stops.begin());
            xy(cd.L, cd.t, cd.s, cd.x, cd.y);
            p.cands.push_back(cd);
          }
        }
      }
    }
    count_pins(c, 1);
  }
  void count_pins(int c, int sign) {
    for (const auto& [ni, pi] : cinfo[c].pins) {
      const auto& p                   = nets[ni].pins[pi];
      tile_pins[tile_of(p.cx, p.cy)] += sign;
    }
  }

  // ---------- global tiles ----------
  // A tile is 32 M2 tracks on a side. It carries the routing capacity left by the fixed metal, the wires routed so
  // far, an estimate for the nets still to route, and the pins that need an access inside it.
  int                tile = 1152, tnx = 1, tny = 1;
  std::vector<float> cap_h, cap_v, gr_h, gr_v;
  std::vector<int>   tile_pins;
  int  tile_of(int x, int y) const { return std::clamp(y / tile, 0, tny - 1) * tnx + std::clamp(x / tile, 0, tnx - 1); }
  void tiles_init() {
    tnx = int(fp.die.xh) / tile + 1;
    tny = int(fp.die.yh) / tile + 1;
    cap_h.assign(size_t(tnx) * tny, 0);
    cap_v = gr_h = gr_v = cap_h;
    tile_pins.assign(size_t(tnx) * tny, 0);
  }
  // Wire length of one direction inside a tile: obstacles (owner < 0) or signal wires (owner >= 0).
  double tile_metal(int tx, int ty, bool horiz, bool signal, int only_layer = -1) const {
    double sum = 0;
    for (size_t l = 0; l < L.size(); ++l) {
      const auto& ly = L[l];
      if (ly.horiz != horiz || (only_layer >= 0 && int(l) != only_layer)) {
        continue;
      }
      const int plo = (horiz ? ty : tx) * tile, alo = (horiz ? tx : ty) * tile;
      auto      b = std::lower_bound(ly.tc.begin(), ly.tc.end(), plo);
      for (; b != ly.tc.end() && *b < plo + tile; ++b) {
        const auto& T  = ly.tr[b - ly.tc.begin()].s;
        auto        it = std::lower_bound(T.begin(), T.end(), alo, [](const Seg& sg, int v) { return sg.hi < v; });
        for (; it != T.end() && it->lo < alo + tile; ++it) {
          if (is_keep(it->net)) {
            continue;
          }
          if ((it->net >= 0) == signal) {
            sum += std::min(it->hi, alo + tile) - std::max(it->lo, alo);
          }
        }
      }
    }
    return sum / tile;
  }
  void tiles_capacity() {
    for (int ty = 0; ty < tny; ++ty) {
      for (int tx = 0; tx < tnx; ++tx) {
        for (bool horiz : {true, false}) {
          double tracks = 0;
          for (const auto& ly : L) {
            if (ly.horiz == horiz) {
              const int plo  = (horiz ? ty : tx) * tile;
              tracks        += double(std::lower_bound(ly.tc.begin(), ly.tc.end(), plo + tile)
                                      - std::lower_bound(ly.tc.begin(), ly.tc.end(), plo));
            }
          }
          (horiz ? cap_h : cap_v)[size_t(ty) * tnx + tx] = float(std::max(1.0, tracks - tile_metal(tx, ty, horiz, false)));
        }
      }
    }
  }
  double tile_util(int tx, int ty) const {
    const size_t i = size_t(ty) * tnx + tx;
    return std::max((tile_metal(tx, ty, true, true) + gr_h[i]) / cap_h[i], (tile_metal(tx, ty, false, true) + gr_v[i]) / cap_v[i]);
  }
  // Pins waiting for a pad against the M2 pad slots (two columns each) the tile still has.
  double tile_access(int tx, int ty) const {
    double      tracks    = 0;
    const auto& m2        = L[0];
    tracks                = double(std::lower_bound(m2.tc.begin(), m2.tc.end(), (ty + 1) * tile)
                                   - std::lower_bound(m2.tc.begin(), m2.tc.end(), ty * tile));
    const double free_len = std::max(1.0, (tracks - tile_metal(tx, ty, true, false, 0)) * tile);
    return tile_pins[size_t(ty) * tnx + tx] / (free_len / 72.0);
  }
  // Cost in nm of putting one more cell at (x, y): crowded tiles push the choice elsewhere.
  double tile_pen(int x, int y) const {
    const int tx = std::clamp(x / tile, 0, tnx - 1), ty = std::clamp(y / tile, 0, tny - 1);
    return tile * (2.0 * std::max(0.0, tile_util(tx, ty) - 0.8) + std::max(0.0, tile_access(tx, ty) - 0.5));
  }
  // Estimated demand of an unrouted net: half of each L between the pins a spanning tree joins.
  void gr_net(int ni, float sign) {
    const auto&       pins = nets[ni].pins;
    const size_t      n    = pins.size();
    std::vector<int>  best(n, INT32_MAX), from(n, 0);
    std::vector<char> in(n, 0);
    in[0] = 1;
    for (size_t i = 1; i < n; ++i) {
      best[i] = std::abs(pins[i].cx - pins[0].cx) + std::abs(pins[i].cy - pins[0].cy);
    }
    for (size_t k = 1; k < n; ++k) {
      int b = -1;
      for (size_t i = 0; i < n; ++i) {
        if (!in[i] && (b < 0 || best[i] < best[b])) {
          b = int(i);
        }
      }
      in[b]        = 1;
      const int ax = std::clamp(pins[from[b]].cx / tile, 0, tnx - 1), ay = std::clamp(pins[from[b]].cy / tile, 0, tny - 1);
      const int bx = std::clamp(pins[b].cx / tile, 0, tnx - 1), by = std::clamp(pins[b].cy / tile, 0, tny - 1);
      for (int x = std::min(ax, bx); x <= std::max(ax, bx) && ax != bx; ++x) {
        gr_h[size_t(ay) * tnx + x] += 0.5f * sign;
        gr_h[size_t(by) * tnx + x] += 0.5f * sign;
      }
      for (int y = std::min(ay, by); y <= std::max(ay, by) && ay != by; ++y) {
        gr_v[size_t(y) * tnx + ax] += 0.5f * sign;
        gr_v[size_t(y) * tnx + bx] += 0.5f * sign;
      }
      for (size_t i = 0; i < n; ++i) {
        const int d = std::abs(pins[i].cx - pins[b].cx) + std::abs(pins[i].cy - pins[b].cy);
        if (!in[i] && d < best[i]) {
          best[i] = d;
          from[i] = b;
        }
      }
    }
  }
  // Every pin needs a way up through M2 and M3, so a tile with many pins has little of both to spare. Wires that only
  // pass pay more for M2 and M3 there and go over it on M4 and above (NVDLA O: over a cell with unroutable pins every
  // M3 track was taken, most by wires one to two microns long). The factor is at least one, so the heuristic stays a
  // lower bound. It is read once, before routing: cell moves later change the counts only a little.
  std::vector<float> tile_cost;
  void               tile_costs_init() {
    tile_cost.clear();
    if (opt.pin_cost <= 0) {
      return;
    }
    tile_cost.assign(tile_pins.size(), 1.0f);
    int64_t dense = 0;
    for (size_t i = 0; i < tile_pins.size(); ++i) {
      if (tile_pins[i] > opt.pin_tile) {
        tile_cost[i] = float(1.0 + opt.pin_cost * (double(tile_pins[i]) / opt.pin_tile - 1.0));
        ++dense;
      }
    }
    stat["pin_dense_tiles"] = dense;
  }
  void tiles_report(const std::string& tag) {
    int    over  = 0;
    double worst = 0, access = 0;
    for (int ty = 0; ty < tny; ++ty) {
      for (int tx = 0; tx < tnx; ++tx) {
        const double u  = tile_util(tx, ty);
        over           += u > 1.0;
        worst           = std::max(worst, u);
        access          = std::max(access, tile_access(tx, ty));
      }
    }
    stat["tiles"]                      = int64_t(tnx) * tny;
    stat["tiles_over_" + tag]          = over;
    stat["tile_util_max_pct_" + tag]   = int64_t(worst * 100);
    stat["tile_access_max_pct_" + tag] = int64_t(access * 100);
  }

  // Fine 4-site by 1-row grid counting unrouted signal pins on shared-track cells, so passing M2/M3 wires can pay extra
  // directly over cells whose shared-track pins are still waiting to escape, and pay nothing once those pins are out.
  static constexpr int cbin_w = 216, cbin_h = 270;
  int                  cbnx = 1, cbny = 1;
  std::vector<int16_t> cell_unrouted;
  void                 cell_grid_init() {
    cbnx = int(fp.die.xh) / cbin_w + 2;
    cbny = int(fp.die.yh) / cbin_h + 2;
    cell_unrouted.assign(size_t(cbnx) * cbny, 0);
  }
  size_t cbin_of(int x, int y) const {
    return size_t(std::clamp(y / cbin_h, 0, cbny - 1)) * cbnx + std::clamp(x / cbin_w, 0, cbnx - 1);
  }
  void cell_grid_span(int x, int y, int w, int delta) {
    if (cell_unrouted.empty() || delta == 0) {
      return;
    }
    const int by  = std::clamp((y + 135) / cbin_h, 0, cbny - 1);
    const int bx0 = std::clamp(x / cbin_w, 0, cbnx - 1);
    const int bx1 = std::clamp(std::max(x, x + w - 1) / cbin_w, 0, cbnx - 1);
    for (int bx = bx0; bx <= bx1; ++bx) {
      auto& v = cell_unrouted[size_t(by) * cbnx + bx];
      v       = int16_t(std::max(0, int(v) + delta));
    }
  }
  void cell_grid_update(int c, int sign) {
    if (!cinfo[c].shared_tracks || cell_unrouted.empty()) {
      return;
    }
    int unrouted = 0;
    for (const auto& [ni, pi] : cinfo[c].pins) {
      if (!nets[ni].skip && !nets[ni].routed) {
        ++unrouted;
      }
    }
    cell_grid_span(int(fp.components[c].place.x), int(fp.components[c].place.y), cinfo[c].macro_w, sign * unrouted);
  }
  void cell_grid_net(int ni, int sign) {
    if (cell_unrouted.empty()) {
      return;
    }
    for (const auto& p : nets[ni].pins) {
      if (p.comp >= 0 && cinfo[p.comp].settled && cinfo[p.comp].shared_tracks) {
        cell_grid_span(int(fp.components[p.comp].place.x), int(fp.components[p.comp].place.y), cinfo[p.comp].macro_w, sign);
      }
    }
  }

  // ---------- sites ----------
  struct Site_pick {
    bool   ok = false;
    int    x = 0, y = 0;
    double cost = 1e30;
  };
  bool pins_reachable_basic(int c) {
    for (const auto& [n2, pi] : cinfo[c].pins) {
      Pin tmp = nets[n2].pins[pi];
      pin_access(tmp, tmp.lef);
      bool any = tmp.cands.empty();  // strap pins are judged after they are attached
      for (const auto& cd : tmp.cands) {
        any = any || cand_usable(n2, cd);
      }
      if (!any) {
        return false;
      }
    }
    return true;
  }
  bool pins_reachable(int c) {
    if (!pins_reachable_basic(c)) {
      return false;
    }
    if (opt.access_patterns && cinfo[c].shared_tracks) {
      return check_cell_escapes_at_pose(c);
    }
    return true;
  }
  // Nearest free site to (ax, ay) for an unplaced cell: displacement plus the crowding of the tile it lands in.
  // `keep_off` excludes the neighbourhood the cell is leaving.
  Site_pick find_site(int c, int ax, int ay, bool need_access, int max_dist, int off_y = INT32_MIN, int off_lo = 0,
                      int off_hi = 0) {
    Site_pick   best;
    auto&       comp   = fp.components[c];
    const auto& master = tech.macros.at(comp.macro.str());
    const auto  keep   = comp.place;
    const int   w = cinfo[c].w, site = 54;
    const int   base = int(std::lower_bound(row_ys.begin(), row_ys.end(), ay) - row_ys.begin());
    for (int k = 0; k < int(row_ys.size()); ++k) {
      bool any_row = false;
      for (int ri : {base - 1 - k, base + k}) {
        if (ri < 0 || ri >= int(row_ys.size())) {
          continue;
        }
        const int ry = row_ys[ri], dy = std::abs(ry - ay);
        if (dy >= best.cost || dy > max_dist) {
          continue;
        }
        any_row = true;
        if (!ph::orientation_allowed(master, site_orientation(c, ry))) {
          continue;
        }
        const auto& v  = row_cells[ry];
        const auto  sp = row_span.at(ry);
        const int   idx
            = int(std::lower_bound(v.begin(), v.end(), ax, [&](int a, int x) { return fp.components[a].place.x < x; }) - v.begin());
        for (int step = 0; step <= 2 * int(v.size()) + 1 && step < 24; ++step) {
          const int g = idx + ((step & 1) ? -(step + 1) / 2 : step / 2);
          if (g < 0 || g > int(v.size())) {
            continue;
          }
          const int a = g == 0 ? sp.first : int(fp.components[v[g - 1]].place.x) + cinfo[v[g - 1]].w;
          const int b = g == int(v.size()) ? sp.second : int(fp.components[v[g]].place.x);
          if (b - a < w || pending_between(ry, g == 0 ? -1 : v[g - 1], g == int(v.size()) ? -1 : v[g]) > 0) {
            continue;
          }
          const int lo = sp.first + (a - sp.first + site - 1) / site * site, hi = sp.first + (b - w - sp.first) / site * site;
          if (lo > hi) {
            continue;
          }
          int x = std::clamp(sp.first + (ax - sp.first + site / 2) / site * site, lo, hi);
          if (ry == off_y && x >= off_lo && x < off_hi) {
            continue;
          }
          const double cost = std::abs(x - ax) + dy + tile_pen(x, ry);
          if (cost >= best.cost || std::abs(x - ax) + dy > max_dist) {
            continue;
          }
          comp.place.x           = x;
          comp.place.y           = ry;
          comp.place.orientation = site_orientation(c, ry);
          if (comp_obstacles(c, false) && (!need_access || pins_reachable(c))) {
            best = {true, x, ry, cost};
          }
        }
      }
      if (!any_row && k > 0) {
        break;
      }
    }
    comp.place = keep;
    return best;
  }
  // The same row orientation seen in a mirror: the pins of the cell trade sides.
  static ph::Orientation mirrored(ph::Orientation o) {
    switch (o) {
      case ph::Orientation::N : return ph::Orientation::FN;
      case ph::Orientation::FN: return ph::Orientation::N;
      case ph::Orientation::FS: return ph::Orientation::S;
      case ph::Orientation::S : return ph::Orientation::FS;
      default                 : return o;
    }
  }
  ph::Orientation site_orientation(int c, int y) const {
    const auto o = row_orient.at(y);
    return cinfo[c].flipped ? mirrored(o) : o;
  }
  void apply_site(int c, int x, int y) {
    auto& comp             = fp.components[c];
    comp.place.x           = x;
    comp.place.y           = y;
    comp.place.orientation = site_orientation(c, y);
    auto& v                = row_cells[y];
    v.insert(std::lower_bound(v.begin(), v.end(), x, [&](int a, int xx) { return fp.components[a].place.x < xx; }), c);
    comp_obstacles(c, true);
    attach_cell(c);
    cinfo[c].settled = true;
    cell_grid_update(c, 1);
    if (opt.access_patterns && cinfo[c].shared_tracks && !cinfo[c].reserved && reserve_cell(c)) {
      ++stat["pattern_cells_settled"];
    }
  }
  // First use of a floating cell: it takes the free site nearest to its input pose.
  void reindex(int y) {
    const auto& p = plan[y];
    for (size_t i = 0; i < p.size(); ++i) {
      plan_pos[p[i]] = int(i);
      plan_y[p[i]]   = y;
    }
  }
  // Room of a cell in its row: between the nearest placed neighbours in plan order, less the widths of the cells that
  // still have to come in between. `hi` is the limit of the right edge.
  void plan_bounds(int c, int& lo, int& hi) const {
    const int   y   = plan_y[c];
    const auto& p   = plan.at(y);
    const auto  sp  = row_span.at(y);
    int         sum = 0;
    lo              = sp.first;
    for (int i = plan_pos[c] - 1; i >= 0; --i) {
      if (cinfo[p[i]].settled) {
        lo = int(fp.components[p[i]].place.x) + cinfo[p[i]].w;
        break;
      }
      sum += cinfo[p[i]].w;
    }
    lo  += sum;
    sum  = 0;
    hi   = sp.second;
    for (size_t i = plan_pos[c] + 1; i < p.size(); ++i) {
      if (cinfo[p[i]].settled) {
        hi = int(fp.components[p[i]].place.x);
        break;
      }
      sum += cinfo[p[i]].w;
    }
    hi -= sum;
  }
  // Cells planned between two placed neighbours of a row (-1 for a row end) that have no site yet.
  int pending_between(int y, int left, int right) const {
    const auto it = plan.find(y);
    if (it == plan.end()) {
      return 0;
    }
    return (right >= 0 ? plan_pos[right] : int(it->second.size())) - (left >= 0 ? plan_pos[left] : -1) - 1;
  }
  // Route-first: where a floating cell would like to go is its input pose, and the input has clouds with several
  // times more cells than their rows hold. This moves the wishes, not the cells: tile boundaries shift from a tile
  // that is over the target fill toward a neighbour that is under it, and the wishes inside move with them, first
  // along x, then along y, a little each round (the cell shifting of FastPlace). A tile under the target counts as
  // being at the target, so two sparse tiles do not trade and only the surroundings of a cloud are compressed. The
  // row plan and the sites still decide the real places.
  void spread_targets() {
    if (!opt.prelegal || opt.spread <= 0) {
      return;
    }
    const size_t        tiles = size_t(tnx) * tny;
    std::vector<double> cap(tiles, 0.0);
    for (int y : row_ys) {
      const auto sp       = row_span.at(y);
      const int  ty       = std::clamp((y + 135) / tile, 0, tny - 1);
      int        at       = sp.first;
      const auto free_run = [&](int lo, int hi) {
        for (int tx = std::clamp(lo / tile, 0, tnx - 1); tx <= std::clamp((hi - 1) / tile, 0, tnx - 1) && hi > lo; ++tx) {
          cap[size_t(ty) * tnx + tx] += 270.0 * std::max(0, std::min(hi, (tx + 1) * tile) - std::max(lo, tx * tile));
        }
      };
      for (int k : row_cells[y]) {
        free_run(at, std::min(sp.second, int(fp.components[k].place.x)));
        at = std::max(at, int(fp.components[k].place.x) + cinfo[k].w);
      }
      free_run(at, sp.second);
    }
    std::vector<int>    cells;
    std::vector<double> px, py, area;
    for (size_t c = 0; c < cinfo.size(); ++c) {
      if (!cinfo[c].settled) {
        cells.push_back(int(c));
        px.push_back(cinfo[c].gx + cinfo[c].w / 2.0);
        py.push_back(cinfo[c].gy + 135.0);
        area.push_back(cinfo[c].w * 270.0);
      }
    }
    if (cells.empty()) {
      return;
    }
    const double        xmax = double(tnx) * tile - 1, ymax = double(tny) * tile - 1, delta = 0.25;
    const double        total = std::accumulate(area.begin(), area.end(), 0.0);
    std::vector<double> fill(tiles);
    // The share of the cell area that lies above the target fill of its tile.
    const auto          measure = [&] {
      std::fill(fill.begin(), fill.end(), 0.0);
      for (size_t i = 0; i < cells.size(); ++i) {
        fill[tile_of(int(px[i]), int(py[i]))] += area[i];
      }
      double over = 0;
      for (size_t k = 0; k < tiles; ++k) {
        over    += std::max(0.0, fill[k] - cap[k] * opt.spread);
        // a tile with no free row length is full whatever it holds; under the target it reads as at the target
        fill[k]  = cap[k] > 0 ? fill[k] / (cap[k] * opt.spread) : (fill[k] > 0 ? 8.0 : 1.0);
        fill[k]  = std::max(1.0, std::min(fill[k], 8.0));
      }
      return over / total;
    };
    double first = measure(), worst = first;
    int    rounds = 0;
    for (; rounds < opt.spread_rounds && worst > 0.02; ++rounds) {
      for (const bool along_x : {true, false}) {
        const int           n = along_x ? tnx : tny, m = along_x ? tny : tnx;
        std::vector<double> edge(size_t(n + 1) * m);  // new position of the boundary before tile i, per line
        for (int j = 0; j < m; ++j) {
          const auto at                 = [&](int i) { return along_x ? fill[size_t(j) * tnx + i] : fill[size_t(i) * tnx + j]; };
          edge[size_t(j) * (n + 1)]     = 0;
          edge[size_t(j) * (n + 1) + n] = double(n) * tile;
          for (int i = 1; i < n; ++i) {
            const double a = at(i - 1), b = at(i);  // the boundary between tiles i-1 and i moves toward the emptier one
            edge[size_t(j) * (n + 1) + i]
                = (double(i - 1) * tile * (b + delta) + double(i + 1) * tile * (a + delta)) / (a + b + 2 * delta);
          }
        }
        for (size_t k = 0; k < cells.size(); ++k) {
          auto&        v  = along_x ? px[k] : py[k];
          const int    i  = std::clamp(int(v) / tile, 0, n - 1);
          const int    j  = std::clamp(int(along_x ? py[k] : px[k]) / tile, 0, m - 1);
          const double lo = edge[size_t(j) * (n + 1) + i], hi = edge[size_t(j) * (n + 1) + i + 1];
          const double want = lo + (v - double(i) * tile) * (hi - lo) / tile;
          v                 = std::clamp(v + 0.5 * (want - v), 0.0, along_x ? xmax : ymax);
        }
        worst = measure();
      }
    }
    double moved = 0;
    for (size_t k = 0; k < cells.size(); ++k) {
      const int c  = cells[k];
      const int gx = int(std::lround(px[k] - cinfo[c].w / 2.0)), gy = int(std::lround(py[k] - 135.0));
      moved       += std::abs(gx - cinfo[c].gx) + std::abs(gy - cinfo[c].gy);
      cinfo[c].gx  = gx;
      cinfo[c].gy  = gy;
    }
    stat["spread_rounds"]              = rounds;
    stat["spread_over_permille_start"] = int64_t(first * 1000);
    stat["spread_over_permille_end"]   = int64_t(worst * 1000);
    stat["spread_mean_shift_nm"]       = int64_t(moved / cells.size());
  }
  // Metal of its own above M1: such a cell cannot sit under a strap or a wire on those layers.
  bool upper_metal(int c) const {
    const auto& m = tech.macros.at(fp.components[c].macro.str());
    for (const auto& o : m.obstructions) {
      if (o.layer != "M1" && (o.layer[0] == 'M' || o.layer[0] == 'V')) {
        return true;
      }
    }
    for (const auto& [pn, pp] : m.pins) {
      for (const auto& sh : pp.shapes) {
        if (sh.layer != "M1") {
          return true;
        }
      }
    }
    return false;
  }
  // Give every floating cell a row and a place in the left-to-right order of that row so that the widths between any
  // two placed cells always fit. This is fixed-order row legalization with the positions left open: a cell picks its
  // site when a net first needs it, and the cells that come later keep their room.
  void plan_rows() {
    struct Seg {
      int              lo, hi, load = 0;
      std::vector<int> cells;
    };
    plan_pos.assign(cinfo.size(), -1);
    plan_y.assign(cinfo.size(), 0);
    std::vector<int> floating;
    for (size_t c = 0; c < cinfo.size(); ++c) {
      if (!cinfo[c].settled) {
        floating.push_back(int(c));
      }
    }
    std::map<int, int> row_free;  // free length of a row, then minus what is assigned
    for (int y : row_ys) {
      int used = 0;
      for (int k : row_cells[y]) {
        used += cinfo[k].w;
      }
      row_free[y] = row_span.at(y).second - row_span.at(y).first - used;
    }
    std::vector<int>                row_of(cinfo.size(), INT32_MIN);
    std::vector<std::set<int>>      banned(cinfo.size());
    std::map<int, std::vector<Seg>> segs;
    std::vector<int>                todo = floating;
    for (int round = 0; round < 60 && !todo.empty(); ++round) {
      std::set<int> dirty;
      for (int c : todo) {  // nearest row with room that has not refused the cell
        const auto& master = tech.macros.at(fp.components[c].macro.str());
        const int   base   = int(std::lower_bound(row_ys.begin(), row_ys.end(), cinfo[c].gy) - row_ys.begin());
        int         pick   = INT32_MIN;
        for (int k = 0; k < int(row_ys.size()) && pick == INT32_MIN; ++k) {
          int best = INT32_MIN;
          for (int ri : {base - 1 - k, base + k}) {
            if (ri < 0 || ri >= int(row_ys.size())) {
              continue;
            }
            const int y = row_ys[ri];
            if (row_free[y] >= cinfo[c].w && !banned[c].count(y) && ph::orientation_allowed(master, site_orientation(c, y))
                && (best == INT32_MIN || std::abs(y - cinfo[c].gy) < std::abs(best - cinfo[c].gy))) {
              best = y;
            }
          }
          pick = best;
        }
        if (pick == INT32_MIN) {
          ++stat["plan_cells_without_row"];
          continue;
        }
        row_of[c]       = pick;
        row_free[pick] -= cinfo[c].w;
        dirty.insert(pick);
      }
      todo.clear();
      for (int y : dirty) {
        const auto&      v  = row_cells[y];
        const auto       sp = row_span.at(y);
        std::vector<Seg> ss;
        int              at = sp.first;
        for (size_t i = 0; i <= v.size(); ++i) {
          const int end = i < v.size() ? int(fp.components[v[i]].place.x) : sp.second;
          ss.push_back({at, end});
          if (i < v.size()) {
            at = end + cinfo[v[i]].w;
          }
        }
        std::vector<int> mine;
        for (int c : floating) {
          if (row_of[c] == y) {
            mine.push_back(c);
          }
        }
        std::sort(mine.begin(), mine.end(), [&](int a, int b) {
          return cinfo[a].gx + cinfo[a].w / 2 < cinfo[b].gx + cinfo[b].w / 2;
        });
        size_t k = 0;
        for (int c : mine) {  // segment under the cell centre, never left of the previous cell's segment
          const int mid = cinfo[c].gx + cinfo[c].w / 2;
          while (k + 1 < ss.size() && mid >= ss[k].hi && (mid >= ss[k + 1].lo || ss[k + 1].lo - mid < mid - ss[k].hi)) {
            ++k;
          }
          ss[k].cells.push_back(c);
          ss[k].load += cinfo[c].w;
        }
        for (size_t i = 0; i + 1 < ss.size(); ++i) {  // spill right, keeping the order
          while (ss[i].load > ss[i].hi - ss[i].lo && !ss[i].cells.empty()) {
            const int c = ss[i].cells.back();
            ss[i].cells.pop_back();
            ss[i].load -= cinfo[c].w;
            ss[i + 1].cells.insert(ss[i + 1].cells.begin(), c);
            ss[i + 1].load += cinfo[c].w;
          }
        }
        for (size_t i = ss.size(); i-- > 0;) {  // then spill left; what is still too much leaves the row
          while (ss[i].load > ss[i].hi - ss[i].lo && !ss[i].cells.empty()) {
            const int c = ss[i].cells.front();
            ss[i].cells.erase(ss[i].cells.begin());
            ss[i].load -= cinfo[c].w;
            if (i > 0) {
              ss[i - 1].cells.push_back(c);
              ss[i - 1].load += cinfo[c].w;
            } else {
              banned[c].insert(y);
              row_free[y] += cinfo[c].w;
              row_of[c]    = INT32_MIN;
              todo.push_back(c);
            }
          }
        }
        segs[y] = std::move(ss);
      }
    }
    for (int y : row_ys) {
      auto&       p = plan[y];
      const auto& v = row_cells[y];
      p.clear();
      const auto it = segs.find(y);
      for (size_t i = 0; i <= v.size(); ++i) {
        if (it != segs.end()) {
          p.insert(p.end(), it->second[i].cells.begin(), it->second[i].cells.end());
        }
        if (i < v.size()) {
          p.push_back(v[i]);
        }
      }
      reindex(y);
    }
    stat["plan_cells_left_out"] = std::count_if(floating.begin(), floating.end(), [&](int c) { return plan_pos[c] < 0; });
  }
  // First use of a floating cell: the site of its planned row nearest to its input pose, within the room the plan
  // leaves it, preferring a site where its pins can be reached.
  bool settle(int c) {
    if (plan_pos[c] < 0) {
      return false;
    }
    int lo, hi;
    plan_bounds(c, lo, hi);
    hi          -= cinfo[c].w;
    const int y = plan_y[c], site = 54, x0 = row_span.at(y).first;
    if (lo > hi) {
      if (stat["settle_fail_bounds"]++ < 3) {
        std::cerr << "settle bounds " << fp.components[c].name.str() << " row " << y << " lo " << lo << " hi " << hi << " w "
                  << cinfo[c].w << " gx " << cinfo[c].gx << " plan " << plan_pos[c] << '/' << plan.at(y).size() << " placed "
                  << row_cells[y].size() << '\n';
        const auto& pp = plan.at(y);
        for (int i = std::max(0, plan_pos[c] - 6); i < std::min<int>(int(pp.size()), plan_pos[c] + 7); ++i) {
          std::cerr << "   " << i << (cinfo[pp[i]].settled ? " S " : " . ") << fp.components[pp[i]].place.x << " gx "
                    << cinfo[pp[i]].gx << " w " << cinfo[pp[i]].w << '\n';
        }
      }
      return false;
    }
    auto&      comp        = fp.components[c];
    const auto keep        = comp.place;
    const int  want        = std::clamp(x0 + (cinfo[c].gx - x0 + site / 2) / site * site, lo, hi);
    int        pick        = INT32_MIN;
    comp.place.y           = y;
    comp.place.orientation = site_orientation(c, y);
    const int max_pass     = (opt.access_patterns && cinfo[c].shared_tracks) ? 3 : 2;
    for (int pass = 0; pass < max_pass && pick == INT32_MIN; ++pass) {  // pattern access, then per-pin access, then any fit
      for (int k = 0; k <= (hi - lo) / site && pick == INT32_MIN && (pass == max_pass - 1 || k <= 8); ++k) {
        for (int x : {want - k * site, want + k * site}) {
          if (x < lo || x > hi || pick != INT32_MIN) {
            continue;
          }
          comp.place.x = x;
          // The obstacle check is cheap; pin reachability (escape DFS on shared-track cells) only runs on a fit.
          if (comp_obstacles(c, false)
              && (pass == max_pass - 1 || (pass == 0 ? pins_reachable(c) : pins_reachable_basic(c)))) {
            pick = x;
          }
        }
      }
    }
    comp.place = keep;
    if (pick == INT32_MIN) {
      if (opt.verbose && stat["settle_fail_fit"] < 12) {
        std::cerr << "settle fit " << comp.name.str() << ' ' << comp.macro.str() << " row " << y << " room " << lo << ' ' << hi
                  << " want " << want << " w " << cinfo[c].w << " pose " << cinfo[c].gx << ' ' << cinfo[c].gy << '\n';
      }
      ++stat["settle_fail_fit"];
      return false;
    }
    apply_site(c, pick, y);
    cinfo[c].x0                 = pick;
    cinfo[c].y0                 = y;
    const int64_t d             = std::abs(pick - int64_t(def_place[c].x)) + std::abs(y - int64_t(def_place[c].y));
    stat["settle_cells"]       += 1;
    stat["settle_disp_nm"]     += d;
    stat["settle_disp_max_nm"]  = std::max<int64_t>(stat["settle_disp_max_nm"], d);
    stat["settle_moved"]       += d != 0;
    return true;
  }
  // A routed net at or below the anchor slack fixes its cells: later, less critical nets may not move them.
  bool anchored(int c, int ni) const {
    if (leftovers) {
      return false;  // the final passes may move anything that is not a clock driver
    }
    for (const auto& [n2, pi] : cinfo[c].pins) {
      if (n2 != ni && nets[n2].routed && nets[n2].slack <= anchor_slack) {
        return true;
      }
    }
    return false;
  }
  // The cell cannot be served where it is: move it to the cheapest free site of a nearby tile that still has room for
  // its wires. The nets of the cell are already ripped.
  bool tile_move(int ni, int c) {
    if (!opt.tile_move || pinned.count(c) || !cinfo[c].movable || anchored(c, ni)) {
      return false;
    }
    auto&     comp = fp.components[c];
    const int x = int(comp.place.x), y = int(comp.place.y), site = 54;
    const int reach = cinfo[c].clock_sink ? opt.flop_reach * site : 3 * tile;
    if (reach == 0) {
      return false;
    }
    count_pins(c, -1);
    cell_grid_update(c, -1);
    clear_cell_keeps(c);
    remove_owner(-10 - c);
    auto& v = row_cells[y];
    v.erase(std::find(v.begin(), v.end(), c));
    const auto pick = find_site(c, x, y, true, reach, y, x - opt.shift_reach * site, x + opt.shift_reach * site + 1);
    const int  nx = pick.ok ? pick.x : x, ny = pick.ok ? pick.y : y;
    bool       ok = pick.ok && (!cinfo[c].clock_sink || std::abs(nx - cinfo[c].x0) + std::abs(ny - cinfo[c].y0) <= reach);
    if (ok) {  // room for the wires of this cell in the tile it enters
      const int    tx = std::clamp(nx / tile, 0, tnx - 1), ty = std::clamp(ny / tile, 0, tny - 1);
      const size_t i = size_t(ty) * tnx + tx;
      ok             = tile_util(tx, ty) + double(cinfo[c].pins.size()) / std::min(cap_h[i], cap_v[i]) <= 1.05;
      stat["tile_move_refused_by_demand"] += !ok;
    }
    if (ok) {  // the cell changes its place in the order of the rows
      auto& from = plan[plan_y[c]];
      from.erase(std::find(from.begin(), from.end(), c));
      reindex(y);
      auto&       to   = plan[ny];
      const auto& rv   = row_cells[ny];
      const auto  next = std::lower_bound(rv.begin(), rv.end(), nx, [&](int a, int xx) { return fp.components[a].place.x < xx; });
      to.insert(next == rv.end() ? to.end() : to.begin() + plan_pos[*next], c);
      reindex(ny);
    }
    apply_site(c, ok ? nx : x, ok ? ny : y);
    if (!ok) {
      return false;
    }
    ++cinfo[c].moves;
    ++stat["tile_moves"];
    stat["tile_moves_cross_tile"] += tile_of(nx, ny) != tile_of(x, y);
    stat["tile_moves_cross_row"]  += ny != y;
    stat["tile_move_nm"]          += std::abs(nx - x) + std::abs(ny - y);
    if (cinfo[c].clock_sink) {
      ++stat["flop_moves"];
    }
    return true;
  }
  // A pad is only worth holding if a wire can leave it: some M3 column within four columns must take a via.
  bool pad_escapes(int ni, const Cand& cd) const {
    int fl, fh;
    if (!free_range(0, cd.t, ni, L[0].stops[cd.s], fl, fh)) {
      return false;
    }
    const auto& m2 = L[0];
    const int   s3 = m2.tk_up[cd.t];
    if (s3 < 0) {
      return false;
    }
    auto it = std::lower_bound(m2.stops.begin(), m2.stops.end(), std::max(fl, cd.x - 144));
    for (; it != m2.stops.end() && *it <= std::min(fh, cd.x + 144); ++it) {
      const int t3 = m2.stop_up[it - m2.stops.begin()];
      int       gl, gh;
      if (t3 >= 0 && cut_ok(1, *it, cd.y, ni) && free_range(1, t3, ni, L[1].stops[s3], gl, gh)
          && gh - gl + 2 * L[1].ext >= L[1].minlen) {
        return true;
      }
    }
    return false;
  }
  bool cand_usable(int ni, const Cand& cd) const { return cand_ok(ni, cd) && (!cd.v1 || pad_escapes(ni, cd)); }
  // Does `c` have 3 or more M1 signal pins on close columns sharing a tight band of M2 tracks (e.g. AO222x2, AO221x1,
  // AO32x1, OR4x1)? Computed from the LEF pin geometry alone so no technology cell names are hardcoded.
  bool is_shared_track_cell(int c) const {
    if (off_rows(c) || gap_width.contains(c) || cinfo[c].pins.size() < 3) {
      return false;
    }
    const auto& comp = fp.components[c];
    const auto  mit  = tech.macros.find(comp.macro.str());
    if (mit == tech.macros.end() || !mit->second.klass.starts_with("CORE")) {
      return false;
    }
    const auto& m    = mit->second;
    Ann_place   pose = comp.place;
    if (!row_ys.empty() && row_span.find(int(pose.y)) == row_span.end()) {
      pose.y           = row_ys.front();
      pose.x           = row_span.at(row_ys.front()).first;
      pose.orientation = site_orientation(c, row_ys.front());
    }
    struct Pin_col {
      int              cx;
      std::vector<int> tracks;
    };
    std::vector<Pin_col> cols;
    const auto&          m2 = L[0];
    for (const auto& [ni, pi] : cinfo[c].pins) {
      if (nets[ni].skip) {
        continue;
      }
      const auto& pdef = m.pins.at(nets[ni].pins[pi].lef);
      if (pdef.use == "POWER" || pdef.use == "GROUND") {
        continue;
      }
      std::vector<ph::Rect> rs;
      ph::Rect              hull{1e18, 1e18, -1e18, -1e18};
      bool                  upper = false;
      for (const auto& s : pdef.shapes) {
        if (s.layer == "M1") {
          rs.push_back(ph::placed_rect(m, pose, fp.dbu, s.rect));
          hull = hull.hull(rs.back());
        } else if (s.layer[0] == 'M') {
          upper = true;
        }
      }
      if (upper || rs.empty()) {
        continue;
      }
      Pin_col pc{int((hull.xl + hull.xh) / 2), {}};
      auto    t0 = std::lower_bound(m2.tc.begin(), m2.tc.end(), int(hull.yl));
      for (; t0 != m2.tc.end() && *t0 <= hull.yh; ++t0) {
        for (const auto& r : rs) {
          if (r.yl <= *t0 - 11 && r.yh >= *t0 + 11 && r.xh - r.xl >= 18) {
            pc.tracks.push_back(int(t0 - m2.tc.begin()));
            break;
          }
        }
      }
      if (!pc.tracks.empty()) {
        cols.push_back(std::move(pc));
      }
    }
    if (cols.size() < 3) {
      return false;
    }
    std::sort(cols.begin(), cols.end(), [](const Pin_col& a, const Pin_col& b) { return a.cx < b.cx; });
    const int tight_dx = 2 * L[1].pitch;  // 72 nm on ASAP7 (adjacent 54 nm poly columns)
    for (size_t i = 0; i < cols.size(); ++i) {
      std::set<int> trs(cols[i].tracks.begin(), cols[i].tracks.end());
      size_t        j = i;
      while (j + 1 < cols.size() && cols[j + 1].cx - cols[j].cx <= tight_dx) {
        ++j;
        trs.insert(cols[j].tracks.begin(), cols[j].tracks.end());
      }
      const size_t run = j - i + 1;
      if (run >= 3 && trs.size() <= run + 1) {
        return true;
      }
      if (run >= 2 && cols[i].tracks.size() <= 3 && cols.size() >= 4) {
        return true;
      }
    }
    return false;
  }

  struct Esc_item {
    int c = -1, pin = -1, cell_x = 0, cell_y = 0;
    int t2 = 0, lo2 = 0, hi2 = 0;
    int x1 = 0, y2 = 0;
    int t3 = -1, x3 = 0, lo3 = 0, hi3 = 0;
  };
  struct Esc_active {
    std::vector<std::pair<int, int>> tracks;
    std::vector<Via_rec>             cuts;
  };
  std::vector<Esc_active>            esc_keeps;  // per net: live pin_keep_of(ni) segments and cuts
  std::vector<std::vector<Esc_item>> esc_saved;  // per net: escape geometry to put back if `ni` is ripped

  void clear_esc_keeps(int ni) {
    if (ni < 0 || size_t(ni) >= esc_keeps.size()) {
      return;
    }
    const int owner = pin_keep_of(ni);
    for (const auto& [kl, kt] : esc_keeps[ni].tracks) {
      auto& T = L[kl].tr[kt].s;
      T.erase(std::remove_if(T.begin(), T.end(), [&](const Seg& sg) { return sg.net == owner; }), T.end());
    }
    for (const auto& v : esc_keeps[ni].cuts) {
      del_cut(v.cl, v.x, v.y, owner);
    }
    esc_keeps[ni].tracks.clear();
    esc_keeps[ni].cuts.clear();
  }
  void restore_esc_keeps(int ni) {
    if (ni < 0 || size_t(ni) >= esc_saved.size() || nets[ni].routed || nets[ni].skip) {
      return;
    }
    clear_esc_keeps(ni);
    const int owner = pin_keep_of(ni);
    for (const auto& e : esc_saved[ni]) {
      if (e.c < 0 || int(fp.components[e.c].place.x) != e.cell_x || int(fp.components[e.c].place.y) != e.cell_y) {
        continue;
      }
      if (!cut_ok(0, e.x1, e.y2, ni) || (e.t3 >= 0 && !cut_ok(1, e.x3, e.y2, ni))) {
        continue;
      }
      Undo u;
      bool ok = place(0, e.t2, e.lo2, e.hi2, owner, u);
      if (ok && e.t3 >= 0) {
        ok = place(1, e.t3, e.lo3, e.hi3, owner, u);
      }
      if (!ok) {
        restore(u);
        continue;
      }
      add_cut(0, e.x1, e.y2, owner);
      esc_keeps[ni].cuts.push_back({0, e.x1, e.y2});
      esc_keeps[ni].tracks.emplace_back(0, e.t2);
      if (e.t3 >= 0) {
        add_cut(1, e.x3, e.y2, owner);
        esc_keeps[ni].cuts.push_back({1, e.x3, e.y2});
        esc_keeps[ni].tracks.emplace_back(1, e.t3);
      }
    }
  }
  void clear_cell_keeps(int c, bool reset_tries = true) {
    cinfo[c].reserved = false;
    if (reset_tries) {
      cinfo[c].reserve_tries = 0;
    }
    for (const auto& [n2, pi] : cinfo[c].pins) {
      if (size_t(n2) >= esc_saved.size() || esc_saved[n2].empty()) {
        continue;
      }
      auto&      sv     = esc_saved[n2];
      const auto before = sv.size();
      sv.erase(std::remove_if(sv.begin(), sv.end(), [&](const Esc_item& e) { return e.c == c; }), sv.end());
      if (sv.size() != before && !nets[n2].routed) {
        clear_esc_keeps(n2);
        restore_esc_keeps(n2);
      }
    }
  }
  // Once `ni` has routed, update any saved escape on a reserved cell to the exact M2 pad and M3 riser `ni` actually
  // committed, so if `ni` is later ripped up, `restore_esc_keeps(ni)` locks the vacated access against blockers.
  void record_used_escapes(int ni) {
    if (ni < 0 || size_t(ni) >= esc_saved.size()) {
      return;
    }
    const auto& net = nets[ni];
    const auto& m2  = L[0];
    const auto& m3  = L[1];
    for (size_t pi = 0; pi < net.pins.size(); ++pi) {
      const auto& p = net.pins[pi];
      if (p.comp < 0 || !cinfo[p.comp].reserved || p.used < 0 || size_t(p.used) >= p.cands.size() || !p.cands[p.used].v1) {
        continue;
      }
      const Cand& cd  = p.cands[size_t(p.used)];
      const Seg*  sg2 = nullptr;
      for (const auto& sg : m2.tr[cd.t].s) {
        if (sg.net == ni && sg.lo <= cd.x && cd.x <= sg.hi) {
          sg2 = &sg;
          break;
        }
      }
      if (!sg2) {
        continue;
      }
      int lo2 = std::max(sg2->lo, cd.x - 108 - m2.ext), hi2 = std::min(sg2->hi, cd.x + 108 + m2.ext);
      if (hi2 - lo2 < m2.minlen) {
        lo2 = std::max(sg2->lo, std::min(lo2, sg2->hi - m2.minlen));
        hi2 = std::min(sg2->hi, lo2 + m2.minlen);
      }
      int t3 = -1, x3 = 0, lo3 = 0, hi3 = 0;
      for (const auto& v : net.vias) {
        if (v.cl == 1 && v.y == cd.y && v.x >= lo2 && v.x <= hi2) {
          const int tk3 = find(m3.tc, v.x);
          if (tk3 < 0) {
            continue;
          }
          for (const auto& sg : m3.tr[tk3].s) {
            if (sg.net == ni && sg.lo <= cd.y && cd.y <= sg.hi) {
              t3  = tk3;
              x3  = v.x;
              lo3 = std::max(sg.lo, cd.y - 120 - m3.ext);
              hi3 = std::min(sg.hi, cd.y + 120 + m3.ext);
              if (hi3 - lo3 < m3.minlen) {
                lo3 = std::max(sg.lo, std::min(lo3, sg.hi - m3.minlen));
                hi3 = std::min(sg.hi, lo3 + m3.minlen);
              }
              break;
            }
          }
          if (t3 >= 0) {
            break;
          }
        }
      }
      auto& sv = esc_saved[ni];
      sv.erase(std::remove_if(sv.begin(), sv.end(), [&](const Esc_item& e) { return e.c == p.comp && e.pin == int(pi); }),
               sv.end());
      sv.push_back({p.comp,
                    int(pi),
                    int(fp.components[p.comp].place.x),
                    int(fp.components[p.comp].place.y),
                    cd.t,
                    lo2,
                    hi2,
                    cd.x,
                    cd.y,
                    t3,
                    x3,
                    lo3,
                    hi3});
    }
  }

  // Build candidate M1->V1->M2->V2->M3 (to an open M4 crossing) escapes for one pin and check/commit a joint assignment
  // across all unrouted M1 pins of `c`. Because every pin's M2 jog and M3 way-up are placed together during DFS, two
  // pins of one cell can never claim colliding M2 pads, diagonal R2 ends, V2 cuts, or M3 columns.
  bool solve_cell_escapes(int c, const std::vector<std::pair<int, int>>& pins_list,
                          const std::vector<std::vector<Cand>>& cand_lists, bool commit_keeps) {
    if (pins_list.empty()) {
      return true;
    }
    std::vector<size_t> idx(pins_list.size());
    std::iota(idx.begin(), idx.end(), 0);
    // Scarcest pins first; break ties by proximity to the middle of the pin cluster (inner pins have neighbours on both sides).
    int mid_x = 0;
    for (size_t i : idx) {
      mid_x += cand_lists[i].empty() ? 0 : cand_lists[i].front().x;
    }
    mid_x /= int(idx.size());
    std::sort(idx.begin(), idx.end(), [&](size_t a, size_t b) {
      if (cand_lists[a].size() != cand_lists[b].size()) {
        return cand_lists[a].size() < cand_lists[b].size();
      }
      const int xa = cand_lists[a].empty() ? 0 : cand_lists[a].front().x;
      const int xb = cand_lists[b].empty() ? 0 : cand_lists[b].front().x;
      return std::abs(xa - mid_x) < std::abs(xb - mid_x);
    });

    const auto&                        m2     = L[0];
    const auto&                        m3     = L[1];
    const bool                         has_m4 = L.size() > 2;
    std::vector<std::vector<Esc_item>> options(idx.size());
    const int                          cell_x = int(fp.components[c].place.x), cell_y = int(fp.components[c].place.y);
    for (size_t order_i = 0; order_i < idx.size(); ++order_i) {
      const auto [n2, pi] = pins_list[idx[order_i]];
      const auto& cands   = cand_lists[idx[order_i]];
      struct Scored {
        int      cost;
        Esc_item item;
      };
      std::vector<Scored> scored;
      for (const auto& cd : cands) {
        if (cd.L != 0 || !cd.v1) {
          continue;
        }
        int fl2, fh2;
        if (!free_range(0, cd.t, n2, m2.stops[cd.s], fl2, fh2) || cd.x < fl2 || cd.x > fh2 || !cut_ok(0, cd.x, cd.y, n2)) {
          continue;
        }
        const int s3 = m2.tk_up[cd.t];
        if (s3 < 0) {
          continue;
        }
        auto it = std::lower_bound(m2.stops.begin(), m2.stops.end(), std::max(fl2, cd.x - 108));
        for (; it != m2.stops.end() && *it <= std::min(fh2, cd.x + 108); ++it) {
          const int t3 = m2.stop_up[it - m2.stops.begin()];
          if (t3 < 0) {
            continue;
          }
          const int x3 = *it;
          int       fl3, fh3;
          if (!cut_ok(1, x3, cd.y, n2) || !free_range(1, t3, n2, m3.stops[s3], fl3, fh3) || fh3 - fl3 + 2 * m3.ext < m3.minlen) {
            continue;
          }
          const int lo2      = std::min({cd.x, x3, m2.stops[cd.s]}) - m2.ext;
          const int hi2      = std::max({cd.x, x3, m2.stops[cd.s]}) + m2.ext;
          bool      added_m4 = false;
          if (has_m4) {
            const auto& m4 = L[2];
            auto        t4 = std::lower_bound(m4.tc.begin(), m4.tc.end(), std::max(fl3, cd.y - 120));
            for (; t4 != m4.tc.end() && *t4 <= std::min(fh3, cd.y + 120); ++t4) {
              const int y4 = *t4;
              int       fl4, fh4;
              if (!cut_ok(2, x3, y4, n2) || !free_range(2, int(t4 - m4.tc.begin()), n2, x3, fl4, fh4)
                  || fh4 - fl4 + 2 * m4.ext < m4.minlen) {
                continue;
              }
              int lo3 = std::min(cd.y, y4) - m3.ext, hi3 = std::max(cd.y, y4) + m3.ext;
              if (hi3 - lo3 < m3.minlen) {
                const int pad = (m3.minlen - (hi3 - lo3) + 1) / 2;
                lo3           = std::max(fl3 - m3.ext, lo3 - pad);
                hi3           = std::min(fh3 + m3.ext, lo3 + m3.minlen);
              }
              scored.push_back({
                  2 * std::abs(x3 - cd.x) + std::abs(y4 - cd.y),
                  {c, pi, cell_x, cell_y, cd.t, lo2, hi2, cd.x, cd.y, t3, x3, lo3, hi3}
              });
              added_m4 = true;
            }
          }
          if (!added_m4 && fh3 - fl3 >= 96) {
            const int half = std::max(m3.ext, (m3.minlen + 1) / 2);
            const int lo3  = std::max(fl3 - m3.ext, cd.y - half);
            const int hi3  = std::min(fh3 + m3.ext, std::max(cd.y + half, lo3 + m3.minlen));
            if (hi3 - lo3 >= m3.minlen) {
              scored.push_back({
                  2 * std::abs(x3 - cd.x) + 160,
                  {c, pi, cell_x, cell_y, cd.t, lo2, hi2, cd.x, cd.y, t3, x3, lo3, hi3}
              });
            }
          }
        }
      }
      if (scored.empty()) {
        return false;
      }
      std::stable_sort(scored.begin(), scored.end(), [](const Scored& a, const Scored& b) { return a.cost < b.cost; });
      if (scored.size() > 14) {
        scored.resize(14);
      }
      options[order_i].reserve(scored.size());
      for (const auto& s : scored) {
        options[order_i].push_back(s.item);
      }
    }

    std::vector<int>                  pick(idx.size(), -1);
    std::vector<Undo>                 undo(idx.size());
    int                               nodes = 0;
    const std::function<bool(size_t)> dfs   = [&](size_t depth) {
      if (depth == idx.size()) {
        return true;
      }
      const int n2    = pins_list[idx[depth]].first;
      const int owner = pin_keep_of(n2);
      for (size_t k = 0; k < options[depth].size() && nodes < 3000; ++k) {
        const auto& e = options[depth][k];
        ++nodes;
        if (!cut_ok(0, e.x1, e.y2, n2) || (e.t3 >= 0 && !cut_ok(1, e.x3, e.y2, n2))) {
          continue;
        }
        undo[depth] = Undo{};
        add_cut(0, e.x1, e.y2, owner);
        if (e.t3 >= 0) {
          add_cut(1, e.x3, e.y2, owner);
        }
        if (place(0, e.t2, e.lo2, e.hi2, owner, undo[depth]) && (e.t3 < 0 || place(1, e.t3, e.lo3, e.hi3, owner, undo[depth]))) {
          pick[depth] = int(k);
          if (dfs(depth + 1)) {
            return true;
          }
        }
        restore(undo[depth]);
        if (e.t3 >= 0) {
          del_cut(1, e.x3, e.y2, owner);
        }
        del_cut(0, e.x1, e.y2, owner);
      }
      return false;
    };

    const bool ok = dfs(0);
    if (!ok || !commit_keeps) {
      if (ok) {
        for (size_t depth = idx.size(); depth-- > 0;) {
          const int   n2    = pins_list[idx[depth]].first;
          const int   owner = pin_keep_of(n2);
          const auto& e     = options[depth][size_t(pick[depth])];
          restore(undo[depth]);
          if (e.t3 >= 0) {
            del_cut(1, e.x3, e.y2, owner);
          }
          del_cut(0, e.x1, e.y2, owner);
        }
      }
      return ok;
    }

    for (size_t depth = 0; depth < idx.size(); ++depth) {
      const int   n2 = pins_list[idx[depth]].first;
      const auto& e  = options[depth][size_t(pick[depth])];
      esc_keeps[n2].tracks.emplace_back(0, e.t2);
      esc_keeps[n2].cuts.push_back({0, e.x1, e.y2});
      if (e.t3 >= 0) {
        esc_keeps[n2].tracks.emplace_back(1, e.t3);
        esc_keeps[n2].cuts.push_back({1, e.x3, e.y2});
      }
      esc_saved[n2].push_back(e);
    }
    return true;
  }

  bool check_cell_escapes_at_pose(int c) {
    std::vector<std::pair<int, int>> pins_list;
    std::vector<std::vector<Cand>>   cand_lists;
    for (const auto& [n2, pi] : cinfo[c].pins) {
      if (nets[n2].skip || nets[n2].routed) {
        continue;
      }
      Pin tmp = nets[n2].pins[pi];
      pin_access(tmp, tmp.lef);
      if (!tmp.cands.empty() && tmp.cands[0].v1) {
        pins_list.emplace_back(n2, pi);
        cand_lists.push_back(std::move(tmp.cands));
      }
    }
    return solve_cell_escapes(c, pins_list, cand_lists, false);
  }

  // The pins of one cell take compatible M1->V1->M2->V2->M3 escape corridors together, held as per-net keep-outs until
  // each net routes (and put back if ripped) without throwing away the pin's other candidates or freezing the cell.
  bool reserve_cell(int c) {
    clear_cell_keeps(c, false);
    std::vector<std::pair<int, int>> todo;
    std::vector<std::vector<Cand>>   cand_lists;
    for (const auto& [n2, pi] : cinfo[c].pins) {
      const auto& p = nets[n2].pins[pi];
      if (!p.cands.empty() && p.cands[0].v1 && !nets[n2].skip && !nets[n2].routed) {
        todo.emplace_back(n2, pi);
        cand_lists.push_back(p.cands);
      }
    }
    if (todo.empty() || !solve_cell_escapes(c, todo, cand_lists, true)) {
      return false;
    }
    cinfo[c].reserved = true;
    ++stat["cells_reserved"];
    return true;
  }

  // V1 sites whose M1 landing pad stays inside the pin metal.
  void pin_access(Pin& p, const std::string& pin_name) {
    p.cands.clear();
    const auto&           c = fp.components[p.comp];
    const auto&           m = tech.macros.at(c.macro.str());
    std::vector<ph::Rect> rs;
    ph::Rect              hull{1e18, 1e18, -1e18, -1e18};
    for (const auto& s : m.pins.at(pin_name).shapes) {
      if (s.layer == "M1") {
        rs.push_back(ph::placed_rect(m, c.place, fp.dbu, s.rect));
        hull = hull.hull(rs.back());
      }
    }
    if (rs.empty()) {  // no M1: the centre of whatever metal the pin has, for the net box and the pin order
      for (const auto& s : m.pins.at(pin_name).shapes) {
        hull = hull.hull(ph::placed_rect(m, c.place, fp.dbu, s.rect));
      }
      if (hull.xl <= hull.xh) {
        p.cx = int((hull.xl + hull.xh) / 2);
        p.cy = int((hull.yl + hull.yh) / 2);
      }
      return;
    }
    p.cx           = int((hull.xl + hull.xh) / 2);
    p.cy           = int((hull.yl + hull.yh) / 2);
    const auto& m2 = L[0];
    auto        t0 = std::lower_bound(m2.tc.begin(), m2.tc.end(), int(hull.yl));
    for (; t0 != m2.tc.end() && *t0 <= hull.yh; ++t0) {
      // x positions whose landing pad is covered: grid stops first, else the centre of a covering bar
      std::vector<int> xs;
      auto             s0 = std::lower_bound(m2.stops.begin(), m2.stops.end(), int(hull.xl));
      for (; s0 != m2.stops.end() && *s0 <= hull.xh; ++s0) {
        const ph::Rect pad{double(*s0 - 9), double(*t0 - 11), double(*s0 + 9), double(*t0 + 11)};
        if (ph::rect_union_covers(pad, rs)) {
          xs.push_back(*s0);
        }
      }
      if (xs.empty()) {
        for (const auto& r : rs) {
          if (r.yl <= *t0 - 11 && r.yh >= *t0 + 11 && r.xh - r.xl >= 18) {
            xs.push_back(int(std::floor((r.xl + r.xh) / 2)));
          }
        }
      }
      if (p.extra_access) {
        // Try additional covered V1 positions when a pin's usual landing is occupied.
        for (const auto& r : rs) {
          for (int x : {int(std::ceil(r.xl)) + 9, int(std::floor((r.xl + r.xh) / 2)), int(std::floor(r.xh)) - 9}) {
            const ph::Rect pad{double(x - 9), double(*t0 - 11), double(x + 9), double(*t0 + 11)};
            if (std::find(xs.begin(), xs.end(), x) == xs.end() && ph::rect_union_covers(pad, rs)) {
              xs.push_back(x);
            }
          }
        }
      }
      for (const int x : xs) {
        auto hi = std::lower_bound(m2.stops.begin(), m2.stops.end(), x);
        for (auto st : {hi, hi == m2.stops.begin() ? hi : hi - 1}) {
          if (st == m2.stops.end() || std::abs(*st - x) >= 18) {
            continue;
          }
          Cand cd;
          cd.L   = 0;
          cd.t   = int(t0 - m2.tc.begin());
          cd.s   = int(st - m2.stops.begin());
          cd.x   = x;
          cd.y   = *t0;
          cd.v1  = true;
          cd.cut = 0;
          p.cands.push_back(cd);
          if (*st == x) {
            break;
          }
        }
      }
    }
  }

  // ---------- committing wires ----------
  struct Undo {
    std::vector<std::pair<Track*, std::vector<Seg>>> tracks;
    std::vector<Via_rec>                             cuts;
    void                                             save(Track* t) {
      for (const auto& [p, v] : tracks) {
        if (p == t) {
          return;
        }
      }
      tracks.emplace_back(t, t->s);
    }
  };
  bool adjacent(int l, int t, int u) const {
    return u >= 0 && u < int(L[l].tc.size()) && std::abs(L[l].tc[u] - L[l].tc[t]) <= L[l].pitch;
  }
  // Insert [lo, hi] for `net`: merge with its own pieces, keep R1, reach the minimum length and repair R2 by lengthening.
  bool place(int l, int t, int lo, int hi, int net, Undo& undo) {
    auto& ly = L[l];
    auto& T  = ly.tr[t].s;
    for (int round = 0; round < 8; ++round) {
      bool changed = false;
      // R1 and merging
      for (bool again = true; again;) {
        again = false;
        for (const auto& s : T) {
          if (s.hi <= lo - ly.e2e || s.lo >= hi + ly.e2e) {
            continue;
          }
          if (own_keep(s.net, net)) {
            continue;  // its own spacing or pin-escape keep-out gives way to the net itself
          }
          if (s.net != net) {
            ++stat[std::string("reject_r1_") + (round ? "after_fix_" : "") + ly.name];
            return false;
          }
          if (s.lo < lo || s.hi > hi) {
            lo    = std::min(lo, s.lo);
            hi    = std::max(hi, s.hi);
            again = true;
          }
        }
      }
      if (hi - lo < ly.minlen) {  // R3: lengthen toward the side with room
        int need    = ly.minlen - (hi - lo);
        int room_hi = INT32_MAX / 2, room_lo = INT32_MAX / 2;
        for (const auto& s : T) {
          if (s.net == net || own_keep(s.net, net)) {
            continue;
          }
          if (s.lo >= hi) {
            room_hi = std::min(room_hi, s.lo - ly.e2e - hi);
          }
          if (s.hi <= lo) {
            room_lo = std::min(room_lo, lo - ly.e2e - s.hi);
          }
        }
        const int up  = std::min(need, std::max(0, room_hi));
        hi           += up;
        need         -= up;
        if (need > std::max(0, room_lo)) {
          ++stat["reject_minlen_" + ly.name];
          return false;
        }
        lo      -= need;
        changed  = true;
      }
      // R2: an opposite-facing end on a neighbouring track within the corner spacing
      for (int u : {t - 1, t + 1}) {
        if (!adjacent(l, t, u)) {
          continue;
        }
        for (const auto& s : ly.tr[u].s) {
          if (own_keep(s.net, net) || (is_keep(s.net) && !is_pin_keep(s.net))) {
            continue;  // soft spacing keep-outs and our own keep-outs do not force corner lengthening
          }
          if (s.lo >= hi && s.lo - hi < ly.cs) {
            hi      = s.lo + 2;
            changed = true;
          }
          if (lo >= s.hi && lo - s.hi < ly.cs) {
            lo      = s.hi - 2;
            changed = true;
          }
        }
      }
      if (!changed) {
        undo.save(&ly.tr[t]);
        auto it = std::lower_bound(T.begin(), T.end(), lo, [](const Seg& s, int v) { return s.hi < v; });
        auto e  = it;
        while (e != T.end() && e->lo <= hi) {
          ++e;
        }
        it = T.erase(it, e);
        T.insert(it, {lo, hi, net});
        return true;
      }
    }
    ++stat["reject_rounds_" + ly.name];
    return false;
  }
  void restore(Undo& undo) {
    for (auto& [t, v] : undo.tracks) {
      t->s = std::move(v);
    }
    for (const auto& c : undo.cuts) {
      del_cut(c.cl, c.x, c.y, -9);
    }
  }

  // ---------- search ----------
  struct Window {
    int xl, yl, xh, yh;
  };
  // A* from the candidates of one pin to anything marked for the net. Returns the state path, target first.
  // While the tree is empty the targets are the pin accesses of the first pin. One that brings a via cannot take a
  // path whose last cut on that cut layer sits within the cut spacing of it.
  // The search keeps the cheapest arrival at a position. That hides a case: the cheapest way onto a track is often
  // a via from the next track over, and such an arrival can never take a via of the same cut layer one track on,
  // while the dearer arrival from two tracks back could. A connection that fails is searched again with arrivals
  // kept apart by how far they are into their run: exact offset while the first via of the run can still clash
  // (near), one node once it cannot (far).
  static constexpr int64_t far_bit = int64_t(1) << 61, id_mask = (int64_t(1) << 48) - 1;
  bool                     exact_runs = false;
  int64_t                  run_key(int64_t id, int delta) const {
    return !exact_runs ? id : std::abs(delta) > far_run ? id | far_bit : id | (int64_t(delta + far_run) << 48);
  }
  int                      far_run = 0;  // run length past which no cut of the run's first via can clash
  std::vector<const Cand*> goal_cands;
  bool                     goal_cut_clash(int64_t id) {
    const Cand* c = nullptr;
    for (const auto* g : goal_cands) {
      if (sid(g->L, g->t, g->s) == (id & id_mask)) {
        c = g;
      }
    }
    if (!c || c->cut < 0) {
      return false;
    }
    const auto& cl = CL[c->cut];
    int64_t     at = id;
    for (int step = 0; step < 16; ++step) {
      const int64_t pa = node.find(at)->parent;
      if (pa < 0) {
        break;
      }
      int la, ta, sa, lb, tb, sb, xa, ya;
      unsid(at & id_mask, la, ta, sa);
      unsid(pa & id_mask, lb, tb, sb);
      xy(la, ta, sa, xa, ya);
      if (std::abs(xa - c->x) + std::abs(ya - c->y) > 160) {
        break;
      }
      if (la != lb && std::max(la, lb) == c->cut && (xa != c->x || ya != c->y)) {
        const int64_t dx = std::max(0, std::abs(xa - c->x) - 2 * cl.hx), dy = std::max(0, std::abs(ya - c->y) - 2 * cl.hy);
        if (dx * dx + dy * dy < int64_t(cl.space) * cl.space) {
          return true;
        }
      }
      at = pa;
    }
    return false;
  }
  bool search(int ni, const std::vector<Cand>& src, const Window& w, const Window& goal, std::vector<int64_t>& path) {
    node.clear();
    using Item = std::pair<float, int64_t>;
    std::priority_queue<Item, std::vector<Item>, std::greater<>> pq;
    int src_xl = INT32_MAX, src_yl = INT32_MAX, src_xh = INT32_MIN, src_yh = INT32_MIN;
    for (const auto& c : src) {
      src_xl = std::min(src_xl, c.x);
      src_xh = std::max(src_xh, c.x);
      src_yl = std::min(src_yl, c.y);
      src_yh = std::max(src_yh, c.y);
    }
    const bool use_cell_cost = opt.cell_cost > 0 && !cell_unrouted.empty();
    const auto heur          = [&](int l, int x, int y) {
      const int dx = std::max({0, goal.xl - x, x - goal.xh}), dy = std::max({0, goal.yl - y, y - goal.yh});
      int       min_vias = 0;
      if (mark_layers != 0) {
        int dl = 8;
        for (int m = 0; m < int(L.size()); ++m) {
          if ((mark_layers >> m) & 1u) {
            dl = std::min(dl, std::abs(l - m));
          }
        }
        min_vias = dl;
        if (dl == 0 && mark_box.xl <= mark_box.xh) {
          const int mdx = std::max({0, mark_box.xl - x, x - mark_box.xh});
          const int mdy = std::max({0, mark_box.yl - y, y - mark_box.yh});
          if (L[l].horiz ? mdy > 0 : mdx > 0) {
            min_vias = (mark_layers == uint8_t(1u << l)) ? 2 : 1;
          }
        }
      }
      return float(((dx + dy) * opt.hmin + min_vias * opt.via_cost) * opt.hweight);
    };
    // A state is queued twice: first for its moves along the track, then, one via cost later, for its vias. A via
    // from it could not be taken from the queue before that, so the order is the same, and a long straight run
    // does not pay for the vias it never takes.
    constexpr int64_t via_phase = int64_t(1) << 62;
    const float       via_cost  = float(opt.via_cost);
    const auto        min_via_f = [&](int l, int t, int s, int x, int y, float g) {
      float       best = std::numeric_limits<float>::infinity();
      const auto& ly   = L[l];
      if (l + 1 < int(L.size()) && ly.stop_up[s] >= 0 && ly.tk_up[t] >= 0) {
        best = std::min(best, g + via_cost + heur(l + 1, x, y));
      }
      if (l > 0 && ly.stop_dn[s] >= 0 && ly.tk_dn[t] >= 0) {
        best = std::min(best, g + via_cost + heur(l - 1, x, y));
      }
      return best;
    };
    const auto push = [&](int64_t id, int l, float g, int64_t from, int x, int y, int entry, int fl, int fh) {
      if (ban.has(id & id_mask)) {
        return;
      }
      if (exact_runs && !(id & far_bit)) {  // the far arrival can do everything this one can
        if (const Node* far = node.find((id & id_mask) | far_bit); far && far->g <= g) {
          return;
        }
      }
      auto& n = node.at(id);
      if (n.g <= g) {
        return;
      }
      n.g      = g;
      n.parent = from;
      n.entry  = entry;
      n.fl     = fl;
      n.fh     = fh;
      pq.emplace(g + heur(l, x, y), id);
    };
    why = "source-blocked";
    for (const auto& c : src) {
      if (!cand_ok(ni, c)) {
        continue;
      }
      why = "no-path";
      int       sx, sy, fl, fh;
      const int a = L[c.L].stops[c.s];
      xy(c.L, c.t, c.s, sx, sy);
      free_range(c.L, c.t, ni, a, fl, fh);
      push(run_key(sid(c.L, c.t, c.s), 0), c.L, 0, -1, sx, sy, a, fl, fh);
    }
    int64_t budget = search_budget > 0 ? search_budget : opt.max_expand;
    while (!pq.empty()) {
      const auto [f, key] = pq.top();
      pq.pop();
      const bool    vias = (key & via_phase) != 0;
      const int64_t id   = key & ~via_phase;  // the node: position and the far flag
      int           l, t, s;
      unsid(id & id_mask, l, t, s);
      int x, y;
      xy(l, t, s, x, y);
      const Node  n        = *node.find(id);  // a copy: a push may move the table
      const float g        = n.g;
      const float expected = vias ? min_via_f(l, t, s, x, y, g) : (g + heur(l, x, y));
      if (f > expected + 0.5f) {
        continue;
      }
      const auto& ly = L[l];
      const int   a  = ly.stops[s];
      if (!vias) {
        if (mark.has(id & id_mask) && !goal_cut_clash(id)) {
          path.clear();
          for (int64_t at = id; at >= 0; at = node.find(at)->parent) {
            path.push_back(at & id_mask);
          }
          return true;
        }
        if (--budget < 0) {
          ++stat["budget_exhausted"];
          why = "budget";
          return false;
        }
        ++expansions;
        for (int d : {-1, 1}) {  // the free stretch around a state is that of every state inside it
          int s2 = s + d;
          while (l == 0 && s2 >= 0 && s2 < int(ly.stops.size()) && ly.stop_up[s2] < 0 && !mark.has(sid(0, t, s2))) {
            if (ban.has(sid(0, t, s2))) {
              s2 = -1;
              break;
            }
            s2 += d;
          }
          if (s2 < 0 || s2 >= int(ly.stops.size())) {
            continue;
          }
          const int a2 = ly.stops[s2];
          if (a2 < n.fl || a2 > n.fh) {
            continue;
          }
          int x2 = x, y2 = y;
          (ly.horiz ? x2 : y2) = a2;
          if (x2 < w.xl || x2 > w.xh || y2 < w.yl || y2 > w.yh) {
            continue;
          }
          float step = float(std::abs(a2 - a) * ly.cost);
          if (l <= 1 && !tile_cost.empty()) {
            step *= tile_cost[tile_of(x2, y2)];
          }
          if (l <= 1 && use_cell_cost) {
            if (const int cu = cell_unrouted[cbin_of(x2, y2)]; cu > 0) {
              const bool near_src  = x2 >= src_xl - 162 && x2 <= src_xh + 162 && y2 >= src_yl - 135 && y2 <= src_yh + 135;
              const bool near_goal = x2 >= goal.xl - 162 && x2 <= goal.xh + 162 && y2 >= goal.yl - 135 && y2 <= goal.yh + 135;
              if (!near_src && !near_goal) {
                step *= float(1.0 + opt.cell_cost * double(std::min(cu, 4)));
              }
            }
          }
          push(run_key(sid(l, t, s2), a2 - n.entry), l, g + step, id, x2, y2, n.entry, n.fl, n.fh);
        }
        if (const float vf = min_via_f(l, t, s, x, y, g); std::isfinite(vf)) {
          pq.emplace(vf, id | via_phase);
        }
        continue;
      }
      for (int d : {-1, 1}) {
        const int l2 = l + d;
        if (l2 < 0 || l2 >= int(L.size())) {
          continue;
        }
        const int t2 = d > 0 ? ly.stop_up[s] : ly.stop_dn[s];
        const int s2 = d > 0 ? ly.tk_up[t] : ly.tk_dn[t];
        if (t2 < 0 || s2 < 0) {
          continue;
        }
        const int k = std::max(l, l2);  // cut layer index: V(k+1) joins L[k-1] and L[k]
        if (!cut_ok(k, x, y, ni)) {
          continue;
        }
        if (std::abs(a - n.entry) <= 160) {  // the path's own recent cuts on this cut layer; none this far into a run
          bool    clash = false;
          int64_t at    = id;
          for (int step = 0; step < 16 && !clash; ++step) {
            const int64_t pa = node.find(at)->parent;
            if (pa < 0) {  // the source state: the via of the pin access the commit will take sits here
              for (const auto& c : src) {
                if (sid(c.L, c.t, c.s) == (at & id_mask) && cand_ok(ni, c)) {
                  if (c.cut == k && (c.x != x || c.y != y)) {
                    const auto&   cl = CL[k];
                    const int64_t dx = std::max(0, std::abs(c.x - x) - 2 * cl.hx), dy = std::max(0, std::abs(c.y - y) - 2 * cl.hy);
                    clash = dx * dx + dy * dy < int64_t(cl.space) * cl.space;
                  }
                  break;
                }
              }
              break;
            }
            int la, ta, sa, lb, tb, sb, xa, ya;
            unsid(at & id_mask, la, ta, sa);
            unsid(pa & id_mask, lb, tb, sb);
            xy(la, ta, sa, xa, ya);
            if (std::abs(xa - x) + std::abs(ya - y) > 160) {
              break;
            }
            if (la != lb && std::max(la, lb) == k && (xa != x || ya != y)) {
              const auto&   c  = CL[k];
              const int64_t dx = std::max(0, std::abs(xa - x) - 2 * c.hx), dy = std::max(0, std::abs(ya - y) - 2 * c.hy);
              clash = dx * dx + dy * dy < int64_t(c.space) * c.space;
            }
            at = pa;
          }
          if (clash) {
            continue;
          }
        }
        int       gl, gh;
        const int a2 = L[l2].stops[s2];
        if (!free_range(l2, t2, ni, a2, gl, gh) || gh - gl + 2 * L[l2].ext < L[l2].minlen) {
          continue;
        }
        push(run_key(sid(l2, t2, s2), 0), l2, g + via_cost, id, x, y, a2, gl, gh);
      }
    }
    return false;
  }

  bool cand_ok(int ni, const Cand& c, bool* already_owned = nullptr) const {
    int       fl, fh;
    const int a = L[c.L].stops[c.s];
    if (!free_range(c.L, c.t, ni, a, fl, fh)) {
      return false;
    }
    if (c.cut >= 0) {
      const int at = L[c.L].horiz ? c.x : c.y;
      if (at < fl || at > fh || !cut_ok(c.cut, c.x, c.y, ni, already_owned)) {
        return false;
      }
    }
    return true;
  }
  uint8_t              mark_layers = 0;
  Window               mark_box{INT32_MAX, INT32_MAX, INT32_MIN, INT32_MIN};
  std::vector<int64_t> tree_sids;
  void                 clear_marks() {
    mark.clear();
    mark_layers = 0;
    mark_box    = {INT32_MAX, INT32_MAX, INT32_MIN, INT32_MIN};
    tree_sids.clear();
  }
  void mark_state(int l, int t, int s) {
    const int64_t id = sid(l, t, s);
    auto&         mv = mark.at(id);
    if (mv) {
      return;
    }
    mv           = 1;
    mark_layers |= uint8_t(1u << l);
    int x, y;
    xy(l, t, s, x, y);
    mark_box.xl = std::min(mark_box.xl, x);
    mark_box.xh = std::max(mark_box.xh, x);
    mark_box.yl = std::min(mark_box.yl, y);
    mark_box.yh = std::max(mark_box.yh, y);
    if (tree_sids.size() < 512) {
      tree_sids.push_back(id);
    }
  }
  bool in_unconn_own(int ni, int l, int t, int pos) const {
    const int m = L[l].e2e + L[l].ext - 1;
    for (const auto& o : own[ni]) {
      if (o.l == l && o.t == t && !nets[ni].pins[o.pin].conn && pos >= o.lo - m && pos <= o.hi + m) {
        return true;
      }
    }
    return false;
  }
  void mark_net(int ni) {
    clear_marks();
    for (size_t l = 0; l < L.size(); ++l) {
      const auto& ly = L[l];
      const int   m  = ly.e2e + ly.ext - 1;
      for (int t : touched[ni][l]) {
        for (const auto& s : ly.tr[t].s) {
          if (s.net == ni) {  // a piece this close is merged by R1, so reaching it already connects
            auto s0 = std::lower_bound(ly.stops.begin(), ly.stops.end(), s.lo - m);
            for (; s0 != ly.stops.end() && *s0 <= s.hi + m; ++s0) {
              if (!in_unconn_own(ni, int(l), t, *s0)) {
                mark_state(int(l), t, int(s0 - ly.stops.begin()));
              }
            }
          }
        }
      }
    }
  }
  // Relaxed unweighted BFS from `starts` (ignoring search window, self-cut-clash and ban): if the whole connected
  // component of legal track/via states has fewer than `limit` states and none is in `goals`, no retry can ever connect.
  bool pocket_trapped(int ni, const std::vector<int64_t>& starts, const Id_map<uint8_t>& goals, int limit = 256) {
    if (starts.empty()) {
      return true;
    }
    if (int(starts.size()) >= limit) {
      return false;
    }
    pocket_seen.clear();
    std::vector<int64_t> q;
    q.reserve(size_t(limit));
    for (int64_t id : starts) {
      if (goals.has(id)) {
        return false;
      }
      if (!pocket_seen.has(id)) {
        pocket_seen.at(id) = 1;
        q.push_back(id);
      }
    }
    for (size_t head = 0; head < q.size() && int(q.size()) < limit; ++head) {
      int l, t, s;
      unsid(q[head], l, t, s);
      const auto& ly = L[l];
      const int   a  = ly.stops[s];
      int         fl, fh;
      if (!free_range(l, t, ni, a, fl, fh)) {
        continue;
      }
      for (int d : {-1, 1}) {
        const int s2 = s + d;
        if (s2 < 0 || s2 >= int(ly.stops.size()) || ly.stops[s2] < fl || ly.stops[s2] > fh) {
          continue;
        }
        const int64_t id2 = sid(l, t, s2);
        if (goals.has(id2)) {
          return false;
        }
        if (!pocket_seen.has(id2)) {
          pocket_seen.at(id2) = 1;
          q.push_back(id2);
          if (int(q.size()) >= limit) {
            return false;
          }
        }
      }
      int x, y;
      xy(l, t, s, x, y);
      for (int d : {-1, 1}) {
        const int l2 = l + d;
        if (l2 < 0 || l2 >= int(L.size())) {
          continue;
        }
        const int t2 = d > 0 ? ly.stop_up[s] : ly.stop_dn[s];
        const int s2 = d > 0 ? ly.tk_up[t] : ly.tk_dn[t];
        int       gl, gh;
        if (t2 < 0 || s2 < 0 || !cut_ok(std::max(l, l2), x, y, ni) || !free_range(l2, t2, ni, L[l2].stops[s2], gl, gh)
            || gh - gl + 2 * L[l2].ext < L[l2].minlen) {
          continue;
        }
        const int64_t id2 = sid(l2, t2, s2);
        if (goals.has(id2)) {
          return false;
        }
        if (!pocket_seen.has(id2)) {
          pocket_seen.at(id2) = 1;
          q.push_back(id2);
          if (int(q.size()) >= limit) {
            return false;
          }
        }
      }
    }
    return int(q.size()) < limit;
  }
  std::vector<std::array<std::set<int>, 6>> touched;
  struct Term_seg {
    int net, l, t, lo, hi;
  };
  std::vector<Term_seg> term_segs;
  struct Comp_info {
    bool                             movable = false, clock_sink = false, fixed_wire = false, shared_tracks = false;
    int                              moves = 0, x0 = 0, y0 = 0, w = 0, macro_w = 0;
    std::vector<std::pair<int, int>> pins;  // net, pin index
    bool                             settled = true, reserved = false;
    bool                             flipped       = false;  // mirrored about its vertical axis against the orientation of its row
    int                              reserve_tries = 0;
    int                              gx = 0, gy = 0;  // pose of the input DEF
  };
  std::vector<Comp_info>               cinfo;
  std::map<int, std::vector<int>>      row_cells;  // row y -> components by x
  std::map<int, std::pair<int, int>>   row_span;
  std::set<int>                        pinned;
  std::vector<Ann_place>               def_place;
  std::map<int, ph::Orientation>       row_orient;
  std::vector<int>                     row_ys;
  std::map<int, std::vector<int>>      plan;  // row y -> every cell of the row in left-to-right order, placed or not
  std::vector<int>                     plan_pos, plan_y;
  std::vector<std::vector<Via_rec>>    resv;  // per net: vias of reserved pads
  std::unordered_map<std::string, int> pin_net;
  struct Own {
    int net, l, t, lo, hi, pin;
  };
  std::vector<std::vector<Own>> own;  // per net: metal a pin or terminal brings along

  // Turn a state path into wires and vias. `first` and `last` are the pin candidates at its ends, if any.
  bool commit(int ni, const std::vector<int64_t>& path, const Cand* first, const Cand* last, Undo& undo) {
    auto& net = nets[ni];
    struct Run {
      int l, t, lo, hi;
    };
    std::vector<Run>     runs;
    std::vector<Via_rec> vias;
    int                  pl = -1, pt = -1;
    for (const auto id : path) {
      int l, t, s;
      unsid(id, l, t, s);
      const int a = L[l].stops[s];
      if (l == pl && t == pt) {
        runs.back().lo = std::min(runs.back().lo, a);
        runs.back().hi = std::max(runs.back().hi, a);
      } else {
        if (pl >= 0) {
          int x, y;
          xy(l, t, s, x, y);
          vias.push_back({std::max(l, pl), x, y});
        }
        runs.push_back({l, t, a, a});
        pl = l;
        pt = t;
      }
    }
    if (first && first->cut >= 0) {  // the source end of the path is the last run
      const int at = L[runs.back().l].horiz ? first->x : first->y;
      vias.push_back({first->cut, first->x, first->y});
      runs.back().lo = std::min(runs.back().lo, at);
      runs.back().hi = std::max(runs.back().hi, at);
    }
    if (last && last->cut >= 0) {
      const int at = L[runs.front().l].horiz ? last->x : last->y;
      vias.push_back({last->cut, last->x, last->y});
      runs.front().lo = std::min(runs.front().lo, at);
      runs.front().hi = std::max(runs.front().hi, at);
    }
    for (size_t i = 0; i < vias.size(); ++i) {
      const auto& v             = vias[i];
      bool        already_owned = false;
      if (!cut_ok(v.cl, v.x, v.y, ni, &already_owned)) {
        ++stat["reject_cut_" + CL[v.cl].name];
        for (const auto& r : runs) {  // forbid the runs that meet at this cut
          const int c = L[r.l].horiz ? v.x : v.y, tcoord = L[r.l].horiz ? v.y : v.x;
          if (L[r.l].tc[r.t] == tcoord && r.lo <= c && c <= r.hi && (r.l == v.cl || r.l + 1 == v.cl)) {
            ban_run(r.l, r.t, c, c);
          }
        }
        return false;
      }
      if (already_owned) {
        continue;  // Two branches can meet at one via. Its original owner handles rip-up.
      }
      add_cut(v.cl, v.x, v.y, -9);  // provisional owner so the path checks against itself
      undo.cuts.push_back(v);
    }
    for (const auto& r : runs) {
      if (!place(r.l, r.t, r.lo - L[r.l].ext, r.hi + L[r.l].ext, ni, undo)) {
        ban_run(r.l, r.t, r.lo, r.hi);
        return false;
      }
    }
    for (const auto& v : undo.cuts) {
      del_cut(v.cl, v.x, v.y, -9);
      add_cut(v.cl, v.x, v.y, ni);
      net.vias.push_back(v);
    }
    undo.cuts.clear();
    for (const auto& r : runs) {
      touched[ni][r.l].insert(r.t);
    }
    return true;
  }

  void ban_run(int l, int t, int lo, int hi) {
    const auto& ly = L[l];
    auto        s0 = std::lower_bound(ly.stops.begin(), ly.stops.end(), lo);
    for (; s0 != ly.stops.end() && *s0 <= hi; ++s0) {
      ban.at(sid(l, t, int(s0 - ly.stops.begin()))) = 1;
    }
  }
  std::vector<std::vector<std::pair<int, int>>> keep_tracks;  // per net: layer and track of its keep-outs
  // Keep the tracks next to the long wires of a critical net empty where they still are: less coupling, at the price
  // of two tracks per wire. A later net sees the keep-out as an obstacle; the owner itself may still use it.
  void                                          keep_space(int ni, int from_layer) {
    int64_t kept = 0, wanted = 0;
    for (size_t l = size_t(from_layer); l < L.size(); ++l) {
      const auto& ly = L[l];
      for (int t : touched[ni][l]) {
        std::vector<std::pair<int, int>> mine;
        for (const auto& sg : ly.tr[t].s) {
          if (sg.net == ni && sg.hi - sg.lo >= 8 * ly.pitch) {
            mine.emplace_back(sg.lo, sg.hi);
          }
        }
        for (const auto& [lo, hi] : mine) {
          for (int u : {t - 1, t + 1}) {
            if (!adjacent(int(l), t, u)) {
              continue;
            }
            wanted              += hi - lo;
            auto&            T   = L[l].tr[u].s;
            int              at  = lo;
            std::vector<Seg> add;
            for (const auto& sg : T) {  // the free stretches of [lo, hi], an end-to-end spacing away from what is there
              if (sg.hi < lo - ly.e2e || sg.lo > hi + ly.e2e) {
                continue;
              }
              if (sg.lo - ly.e2e - at >= ly.minlen) {
                add.push_back({at, sg.lo - ly.e2e, keep_of(ni)});
              }
              at = std::max(at, sg.hi + ly.e2e);
            }
            if (hi - at >= ly.minlen) {
              add.push_back({at, hi, keep_of(ni)});
            }
            for (const auto& k : add) {
              T.insert(std::lower_bound(T.begin(), T.end(), k.lo, [](const Seg& sg, int v) { return sg.lo < v; }), k);
              kept += k.hi - k.lo;
            }
            if (!add.empty()) {
              keep_tracks[ni].emplace_back(int(l), u);
            }
          }
        }
      }
    }
    stat["space_kept_nm"]   += kept;
    stat["space_wanted_nm"] += wanted;
    stat["space_nets"]      += 1;
  }
  void rip(int ni) {
    auto& net = nets[ni];
    for (size_t l = 0; l < L.size(); ++l) {
      for (int t : touched[ni][l]) {
        auto& T = L[l].tr[t].s;
        T.erase(std::remove_if(T.begin(), T.end(), [&](const Seg& s) { return s.net == ni; }), T.end());
      }
      touched[ni][l].clear();
    }
    for (const auto& [kl, kt] : keep_tracks[ni]) {
      auto& T = L[kl].tr[kt].s;
      T.erase(std::remove_if(T.begin(), T.end(), [&](const Seg& sg) { return sg.net == keep_of(ni); }), T.end());
    }
    keep_tracks[ni].clear();
    for (const auto& k : pin_keeps[ni]) {  // the landings of its macro pins are kept again
      keep_piece(k.l, k.t, k.lo, k.hi, (k.lo + k.hi) / 2, ni);
    }
    for (const auto& v : net.vias) {
      del_cut(v.cl, v.x, v.y, ni);
    }
    net.vias.clear();
    for (auto& p : net.pins) {
      p.used = -1;
      p.conn = false;
    }
    if (net.routed) {
      cell_grid_net(ni, 1);
      if (!net.gr_on && !net.skip && !gr_h.empty()) {
        gr_net(ni, 1);
        net.gr_on = true;
      }
    }
    net.routed = false;
    for (const auto& ts : term_segs) {
      if (ts.net == ni) {
        block(ts.l, ts.t, ts.lo, ts.hi, ni);
      }
    }
    restore_esc_keeps(ni);
  }

  bool route_net(int ni) {
    auto&        net = nets[ni];
    const size_t n   = net.pins.size();
    net.fail_pin     = -1;
    net.fail_other   = -1;
    net.fail_why.clear();
    for (size_t i = 0; i < n; ++i) {  // route-first: a cell takes its site when the first net needs it
      const int c = net.pins[i].comp;
      if (c >= 0 && !cinfo[c].settled && !settle(c)) {
        net.failed   = true;
        net.fail_why = "no-site";
        return false;
      }
    }
    for (size_t i = 0; i < n; ++i) {
      if (net.pins[i].cands.empty()) {
        net.fail_pin = int(i);
        net.failed   = true;
        net.fail_why = "no-candidates";
        return false;
      }
    }
    // Prim-Dijkstra order over pin centres from the driver: alpha 0 is the shortest tree, 1 the shortest paths.
    std::vector<int>     order{0}, link{-1};
    std::vector<int64_t> best(n, INT64_MAX), plen(n, 0);
    std::vector<int>     from(n, 0);
    std::vector<char>    in(n, 0);
    const double         alpha = n >= size_t(opt.pd_min_pins) ? opt.pd_alpha : 0.0;
    in[0]                      = 1;
    for (size_t i = 1; i < n; ++i) {
      best[i] = std::abs(net.pins[i].cx - net.pins[0].cx) + std::abs(net.pins[i].cy - net.pins[0].cy);
    }
    for (size_t k = 1; k < n; ++k) {
      int b = -1;
      for (size_t i = 0; i < n; ++i) {
        if (!in[i] && (b < 0 || best[i] < best[b])) {
          b = int(i);
        }
      }
      in[b] = 1;
      order.push_back(b);
      link.push_back(from[b]);
      plen[b] = plen[from[b]] + std::abs(net.pins[b].cx - net.pins[from[b]].cx) + std::abs(net.pins[b].cy - net.pins[from[b]].cy);
      for (size_t i = 0; i < n; ++i) {
        const int64_t d = std::abs(net.pins[i].cx - net.pins[b].cx) + std::abs(net.pins[i].cy - net.pins[b].cy)
                          + int64_t(alpha * double(plen[b]));
        if (!in[i] && d < best[i]) {
          best[i] = d;
          from[i] = b;
        }
      }
    }
    clear_marks();
    const double base_hweight = opt.hweight;
    struct Restore_weight {
      double& ref;
      double  value;
      ~Restore_weight() { ref = value; }
    } restore_weight{opt.hweight, base_hweight};
    Window tree{net.pins[0].cx, net.pins[0].cy, net.pins[0].cx, net.pins[0].cy};
    // terminals bring their own metal
    for (const auto& o : own[ni]) {
      touched[ni][o.l].insert(o.t);
    }
    net.pins[0].conn = true;
    for (size_t k = 1; k < n; ++k) {
      auto&       p            = net.pins[order[k]];
      const auto& other        = net.pins[link[k]];
      bool        ok           = false;
      bool        tree_trapped = false;
      ban.clear();
      int grow = 0, hw_steps = 0, commit_tries = 0;
      opt.hweight = base_hweight;
      exact_runs  = false;
      for (int attempt = 0; attempt < 12 && !ok && grow < 3; ++attempt) {
        // A long connection gets a budget and a window in proportion: its direct path alone is one expansion per
        // stop, and a strip as narrow as the default margin can be full over that length.
        const int span = std::abs(p.cx - other.cx) + std::abs(p.cy - other.cy);
        const int m0   = std::max(opt.margin, span / 16);
        const int m    = grow == 0 ? m0 : grow == 1 ? m0 * 6 : INT32_MAX / 4;
        search_budget  = opt.max_expand + int64_t(span) * 3 / 2;
        if (attempt == 0 && hw_steps == 0 && !exact_runs && opt.hweight <= 1.15) {
          search_budget = std::min<int64_t>(search_budget, 12000 + int64_t(span) / 2);
        } else if (exact_runs && grow >= 2) {
          search_budget = std::min<int64_t>(search_budget, 60000);
        }
        Window                   w{std::min(p.cx, other.cx) - m,
                                   std::min(p.cy, other.cy) - m,
                                   std::max(p.cx, other.cx) + m,
                                   std::max(p.cy, other.cy) + m};
        // targets: the tree, or the candidates of the first pin while the tree is empty
        const bool               first = k == 1 && net.pins[order[0]].used < 0 && net.pins[order[0]].comp >= 0;
        std::vector<const Cand*> seeds;
        if (first) {
          clear_marks();
          for (const auto& c : net.pins[order[0]].cands) {
            if (cand_ok(ni, c)) {
              mark_state(c.L, c.t, c.s);
              seeds.push_back(&c);
            }
          }
        } else if (k == 1) {
          clear_marks();
          for (const auto& c : net.pins[order[0]].cands) {
            mark_state(c.L, c.t, c.s);
          }
        }
        std::vector<int64_t> path;
        goal_cands         = first ? seeds : std::vector<const Cand*>{};
        // Aim at the tree-side pin this one links to; any piece of the tree met on the way ends the search.
        const Window aim   = opt.aim_link ? Window{other.cx, other.cy, other.cx, other.cy} : tree;
        const bool   found = search(ni, p.cands, w, aim, path);
        goal_cands.clear();
        if (!found && !opt.trace.empty() && net.name == opt.trace) {  // where a failed search went
          std::array<int64_t, 6> cnt{};
          int                    bx0 = INT32_MAX, bx1 = INT32_MIN, by0 = INT32_MAX, by1 = INT32_MIN;
          for (const auto& entry : node.e) {
            if (entry.gen == node.gen) {
              int l2, t2, s2, x, y;
              unsid(entry.id & id_mask, l2, t2, s2);
              xy(l2, t2, s2, x, y);
              ++cnt[l2];
              bx0 = std::min(bx0, x);
              bx1 = std::max(bx1, x);
              by0 = std::min(by0, y);
              by1 = std::max(by1, y);
            }
          }
          std::cerr << "search-failed k " << k << " pin " << p.name << " (" << p.cx << ' ' << p.cy << ") to " << other.name << " ("
                    << other.cx << ' ' << other.cy << ") why " << why << " attempt " << attempt << " grow " << grow << " hweight "
                    << opt.hweight << " budget " << search_budget << " marks " << mark.live << " window " << w.xl << ' ' << w.yl
                    << ' ' << w.xh << ' ' << w.yh << " states";
          for (const auto c : cnt) {
            std::cerr << ' ' << c;
          }
          std::cerr << " bbox " << bx0 << ' ' << by0 << ' ' << bx1 << ' ' << by1 << '\n';
          {  // per layer: the reached state nearest to the aim, and how many reached states are tree states
            for (size_t l = 0; l < L.size(); ++l) {
              int64_t best = -1, bd = INT64_MAX, marked = 0;
              float   bg = 0;
              for (const auto& entry : node.e) {
                if (entry.gen != node.gen) {
                  continue;
                }
                int l2, t2, s2, x, y;
                unsid(entry.id & id_mask, l2, t2, s2);
                if (l2 != int(l)) {
                  continue;
                }
                xy(l2, t2, s2, x, y);
                marked          += mark.has(entry.id & id_mask);
                const int64_t d  = std::abs(x - other.cx) + std::abs(y - other.cy);
                if (d < bd) {
                  bd   = d;
                  best = entry.id;
                  bg   = entry.v.g;
                }
              }
              if (best >= 0) {
                int l2, t2, s2, x, y;
                unsid(best & id_mask, l2, t2, s2);  // the key of an exact-run node carries its run offset above the id
                xy(l2, t2, s2, x, y);
                std::cerr << "   " << L[l].name << " nearest " << x << ' ' << y << " dist " << bd << " g " << bg
                          << " tree states reached " << marked << '\n';
                for (const int64_t key : {best}) {  // why it did not step down
                  const Node* nd = node.find(key);
                  if (!nd || l == 0) {
                    continue;
                  }
                  const auto& ly = L[l];
                  const int   td = ly.stop_dn[s2], sd = ly.tk_dn[t2];
                  int         gl = 0, gh = 0;
                  const bool  fr = td >= 0 && sd >= 0 && free_range(int(l) - 1, td, ni, L[l - 1].stops[sd], gl, gh);
                  std::cerr << "      g " << nd->g << " entry " << nd->entry << " down track " << td << " stop " << sd << " cut_ok "
                            << cut_ok(int(l), x, y, ni) << " free " << fr << " [" << gl << ',' << gh << "] marked "
                            << (td >= 0 && sd >= 0 ? mark.has(sid(int(l) - 1, td, sd)) : false) << " banned "
                            << (td >= 0 && sd >= 0 ? ban.has(sid(int(l) - 1, td, sd)) : false) << '\n';
                }
              }
            }
          }
          if (attempt == 0) {  // what the tracks hold around the pin it aims at
            for (size_t l = 2; l < L.size(); ++l) {
              const auto& ly = L[l];
              const int   pc = ly.horiz ? other.cy : other.cx, ac = ly.horiz ? other.cx : other.cy;
              auto        b = std::lower_bound(ly.tc.begin(), ly.tc.end(), pc - 220);
              for (; b != ly.tc.end() && *b <= pc + 220; ++b) {
                std::cerr << "  " << ly.name << " track " << *b << ':';
                for (const auto& s : ly.tr[b - ly.tc.begin()].s) {
                  if (s.hi > ac - 700 && s.lo < ac + 700) {
                    std::cerr << " [" << s.lo - ac << ',' << s.hi - ac << ']' << (s.net == ni ? "own" : s.net >= 0 ? "net" : "OBS");
                  }
                }
                std::cerr << '\n';
              }
            }
          }
        }
        if (!found) {
          if (std::string_view(why) == "source-blocked") {
            break;
          }
          if (attempt == 0 && std::string_view(why) != "commit") {
            std::vector<int64_t> src_sids;
            pocket_goal.clear();
            for (const auto& c : p.cands) {
              if (cand_ok(ni, c)) {
                const int64_t id = sid(c.L, c.t, c.s);
                src_sids.push_back(id);
                pocket_goal.at(id) = 1;
              }
            }
            if (pocket_trapped(ni, src_sids, mark, 512)) {
              why = "pocket-blocked";
              ++stat["pocket_source_aborts"];
              break;
            }
            if (tree_sids.empty() || (tree_sids.size() < 512 && pocket_trapped(ni, tree_sids, pocket_goal, 512))) {
              why          = "root-blocked";
              tree_trapped = true;
              ++stat["pocket_tree_aborts"];
              break;
            }
          }
          if (std::string_view(why) == "budget" && hw_steps < 3) {  // greedier before wider
            ++hw_steps;
            opt.hweight = std::max(1.3, opt.hweight * 1.4);
            ++stat["greedy_retries"];
          } else if (grow < 2) {
            ++grow;
          } else if (!exact_runs) {  // last resort: keep near-via run offsets distinct
            exact_runs  = true;
            grow        = 1;
            opt.hweight = base_hweight;
            ++stat["exact_run_retries"];
          } else {
            ++grow;
          }
          continue;
        }
        // identify the candidates at both ends
        const Cand* src = nullptr;
        const Cand* dst = nullptr;
        for (const auto& c : p.cands) {
          if (sid(c.L, c.t, c.s) == path.back() && cand_ok(ni, c)) {
            src = &c;
            break;
          }
        }
        if (first) {
          for (const auto* c : seeds) {
            if (sid(c->L, c->t, c->s) == path.front()) {
              dst = c;
            }
          }
        }
        if (!opt.trace.empty() && net.name == opt.trace) {
          int l, t, s2;
          unsid(path.front(), l, t, s2);
          std::cerr << "pre-commit k " << k << " root " << net.pins[order[0]].name << " seeds " << seeds.size() << " dst "
                    << (dst != nullptr) << " src " << (src != nullptr) << " front layer " << L[l].name << " mark "
                    << mark.has(path.front()) << " own_at " << own_at(l, t, ni, L[l].stops[s2]) << '\n';
        }
        if (!src || (first && !dst)) {
          throw std::logic_error("route search returned a path without a selected pin access");
        }
        Undo undo;
        if (!commit(ni, path, src, dst, undo)) {
          restore(undo);
          ++stat["commit_rejects"];
          why = "commit";
          // The rejected run is banned, so the next search differs. A rejection is cheap and says nothing about the
          // window or the weight, so it draws on its own allowance instead of the attempts.
          if (++commit_tries <= 24) {
            --attempt;
          }
          continue;
        }
        if (first) {
          // un-mark the seeds that were not used
          clear_marks();
          net.pins[order[0]].used = int(dst - net.pins[order[0]].cands.data());
        }
        p.used                  = int(src - p.cands.data());
        p.conn                  = true;
        net.pins[order[0]].conn = true;
        if (!opt.trace.empty() && net.name == opt.trace) {
          std::cerr << "trace " << p.name << " attempt " << attempt << " first " << first << '\n';
          for (const auto id : path) {
            int l, t, s, x, y;
            unsid(id, l, t, s);
            xy(l, t, s, x, y);
            std::cerr << "   " << L[l].name << ' ' << x << ' ' << y << (mark.has(id) ? " marked" : "") << '\n';
          }
        }
        mark_net(ni);
        for (const auto id : path) {
          int l, t, s, x, y;
          unsid(id, l, t, s);
          xy(l, t, s, x, y);
          tree.xl = std::min(tree.xl, x);
          tree.xh = std::max(tree.xh, x);
          tree.yl = std::min(tree.yl, y);
          tree.yh = std::max(tree.yh, y);
        }
        ok = true;
      }
      if (!ok) {
        net.failed     = true;
        net.fail_pin   = order[k];
        net.fail_other = link[k];
        net.fail_why   = why;
        if (tree_trapped) {
          int blamed = k == 1 ? order[0] : link[k];
          if (net.pins[blamed].comp < 0) {
            for (size_t j = 0; j < k; ++j) {
              if (net.pins[order[j]].comp >= 0) {
                blamed = order[j];
                break;
              }
            }
          }
          if (net.pins[blamed].comp >= 0) {
            net.fail_pin   = blamed;
            net.fail_other = order[k];
          }
        } else if (k == 1 && std::string_view(why) == "no-path") {  // was the root reachable at all?
          bool any = false;
          for (const auto& c : net.pins[order[0]].cands) {
            any = any || cand_ok(ni, c);
          }
          if (!any && net.pins[order[0]].comp >= 0) {
            net.fail_why = "root-blocked";
            net.fail_pin = order[0];
          }
        }
        return false;
      }
    }
    net.routed = true;
    record_used_escapes(ni);
    clear_esc_keeps(ni);
    cell_grid_net(ni, -1);
    return true;
  }

  void run(bool resume = false) {
    const auto base_budget = opt.max_expand;
    leftovers              = false;
    const double run_start = cpu_s();
    int64_t      popped    = 0;
    touched.resize(nets.size());
    probe_skip.assign(nets.size(), 0);
    keep_tracks.resize(nets.size());
    std::vector<int> order;
    for (size_t i = 0; i < nets.size(); ++i) {
      if (!nets[i].skip) {
        order.push_back(int(i));
      }
    }
    if (!opt.timing.empty()) {  // "inst:pin" or port, slack in ps; the net takes its worst pin
      std::unordered_map<std::string, double> slack;
      std::ifstream                           f(opt.timing);
      if (!f) {
        throw std::runtime_error("cannot open pin timing: " + opt.timing);
      }
      std::string line;
      while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') {
          continue;
        }
        // The placer writes the label with std::quoted, so a name with a backslash or a quote comes escaped.
        std::istringstream fields(line);
        std::string        name, number;
        double             value = 0;
        if (!(fields >> std::quoted(name) >> number)) {
          continue;
        }
        try {
          value = std::stod(number);  // also inf and nan, which stream extraction refuses
        } catch (const std::exception&) {
          continue;
        }
        const auto colon = name.rfind(':');
        if (colon != std::string::npos) {
          name[colon] = '/';
        }
        slack[name] = value;
      }
      std::vector<double> all;
      for (int i : order) {
        for (const auto& p : nets[i].pins) {
          const auto it = slack.find(p.name);
          if (it != slack.end()) {
            nets[i].slack = std::min(nets[i].slack, it->second);
          }
        }
        if (nets[i].slack != 1e30) {
          all.push_back(nets[i].slack);
        }
      }
      std::sort(all.begin(), all.end());
      const auto threshold = [&](double fraction) {
        if (all.empty() || fraction == 0) {
          return -std::numeric_limits<double>::infinity();
        }
        return all[static_cast<size_t>(std::ceil(fraction * double(all.size()))) - 1];
      };
      crit_slack              = threshold(opt.crit_frac);
      anchor_slack            = threshold(opt.anchor_frac);
      space_slack             = threshold(opt.space_frac);
      stat["nets_with_slack"] = all.size();
    }
    const auto key = [&](int i) -> int64_t {
      const auto& n = nets[i];
      if (opt.order == "short") {
        return n.bbox_hp;
      }
      if (opt.order == "long") {
        return -int64_t(n.bbox_hp);
      }
      if (opt.order == "pins") {
        return -int64_t(n.pins.size()) * 1000000 + n.bbox_hp;
      }
      if (opt.order == "fewpins") {
        return int64_t(n.pins.size()) * 1000000 + n.bbox_hp;
      }
      return i;
    };
    std::vector<bool> long_first(nets.size());
    if (opt.long_frac > 0) {
      std::vector<int> data;
      for (int ni : order) {
        if (!nets[ni].clock) {
          data.push_back(ni);
        }
      }
      const size_t count = size_t(std::ceil(opt.long_frac * double(data.size())));
      std::partial_sort(data.begin(), data.begin() + count, data.end(), [&](int a, int b) {
        return nets[a].bbox_hp != nets[b].bbox_hp ? nets[a].bbox_hp > nets[b].bbox_hp : a < b;
      });
      for (size_t i = 0; i < count; ++i) {
        long_first[data[i]] = true;
      }
      stat["long_first_nets"] = count;
    }
    std::vector<int> shared_pri;
    if (opt.complex_first) {
      shared_pri.assign(nets.size(), 0);
      int64_t complex_nets = 0;
      for (int ni : order) {
        if (nets[ni].pins.size() > 8 || nets[ni].bbox_hp > 24 * tile) {
          continue;  // global fanouts keep their place; local shared-track pin escapes go ahead
        }
        for (const auto& p : nets[ni].pins) {
          if (p.comp >= 0 && cinfo[p.comp].shared_tracks) {
            shared_pri[ni] = 1;
            ++complex_nets;
            break;
          }
        }
      }
      stat["complex_first_nets"] = complex_nets;
    }
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
      if (nets[a].clock != nets[b].clock) {
        return nets[a].clock;  // the clock tree takes its tracks before data
      }
      const bool la = long_first[a], lb = long_first[b];
      if (la != lb) {
        return la;
      }
      if (la && nets[a].bbox_hp != nets[b].bbox_hp) {
        return nets[a].bbox_hp > nets[b].bbox_hp;
      }
      const bool aa = nets[a].slack <= anchor_slack, ab = nets[b].slack <= anchor_slack;
      if (aa != ab) {
        return aa;
      }
      if (aa && nets[a].slack != nets[b].slack) {
        return nets[a].slack < nets[b].slack;
      }
      if (opt.complex_first) {
        const int sa = shared_pri[a], sb = shared_pri[b];
        if (sa != sb) {
          return sa > sb;
        }
      }
      const bool ca = nets[a].slack <= crit_slack, cb = nets[b].slack <= crit_slack;
      if (ca != cb) {
        return ca;  // the critical share goes ahead, worst slack first
      }
      if (ca && nets[a].slack != nets[b].slack) {
        return nets[a].slack < nets[b].slack;
      }
      return key(a) < key(b);
    });
    if (opt.limit >= 0 && int(order.size()) > opt.limit) {
      order.resize(opt.limit);
    }
    if (resume) {
      // Preserve the selected cohort and clock priority. Within each group
      // give nets deferred by the deadline one search before retrying failures.
      auto data     = std::find_if(order.begin(), order.end(), [&](int i) { return !nets[i].clock; });
      auto deferred = [&](int i) { return nets[i].calls == 0 && nets[i].fail_why == "time"; };
      std::stable_partition(order.begin(), data, deferred);
      std::stable_partition(data, order.end(), deferred);
    }
    std::vector<bool> selected(nets.size());
    for (int ni : order) {
      selected[ni] = true;
      if (!nets[ni].gr_on && !nets[ni].routed) {
        gr_net(ni, 1);
        nets[ni].gr_on = true;
      }
    }
    tiles_report("start");
    tile_costs_init();
    std::deque<int> queue(order.begin(), order.end());
    for (int pass = 0; pass <= opt.final_passes; ++pass) {
      if (pass > 0) {  // leftovers get a fresh repair allowance
        for (auto& n : nets) {
          n.rips = 0;
        }
        for (auto& c : cinfo) {
          c.moves = 0;
        }
        for (int ni : order) {
          if (!nets[ni].routed) {
            queue.push_back(ni);
          }
        }
        stat["final_pass_nets_" + std::to_string(pass)] = int64_t(queue.size());
        leftovers                                       = true;
        if (pass == 1) {
          opt.max_expand *= 6;  // the few nets left may search wide
        }
      }
      while (!queue.empty()) {
        const int ni = queue.front();
        queue.pop_front();
        auto& net = nets[ni];
        if (net.routed || !selected[ni]) {
          continue;  // repairs may queue a neighbor outside the selected cohort
        }
        if (opt.time_limit > 0 && cpu_s() - run_start > opt.time_limit) {  // out of time: what is left stays unrouted
          net.failed = true;
          if (net.fail_why.empty()) {
            net.fail_why = "time";
          }
          ++stat["nets_stopped_by_time_limit"];
          continue;
        }
        if ((++popped & 2047) == 0 && opt.verbose) {
          std::cerr << "progress " << popped << " pops, queue " << queue.size() << ", cpu " << int(cpu_s() - run_start) << " s\n";
        }
        net.failed                  = false;
        const double  ts            = cpu_s();
        const int64_t e0            = expansions;
        const bool    ok            = route_net(ni);
        (ok ? time_ok : time_fail) += cpu_s() - ts;
        (ok ? exp_ok : exp_fail)   += expansions - e0;
        net.expanded               += expansions - e0;
        ++net.calls;
        if (ok) {
          if (net.spaced) {
            keep_space(ni, 0);
          } else if (net.slack <= space_slack && net.bbox_hp >= opt.space_min_len && !net.clock) {
            keep_space(ni, std::max(0, opt.space_min_layer));
          }
          if (net.gr_on) {  // its real wires now count in the tiles
            gr_net(ni, -1);
            net.gr_on = false;
          }
          continue;
        }
        const int pin = net.fail_pin;
        rip(ni);
        net.failed = true;
        if (pin < 0) {
          continue;
        }
        auto&      failed_pin = net.pins[pin];
        const bool access_blocked
            = net.fail_why == "source-blocked" || net.fail_why == "root-blocked" || net.fail_why == "pocket-blocked";
        if (!failed_pin.extra_access && failed_pin.comp >= 0 && (net.fail_why == "no-candidates" || access_blocked)) {
          const auto& m      = tech.macros.at(fp.components[failed_pin.comp].macro.str());
          const auto& shapes = m.pins.at(failed_pin.lef).shapes;
          if (!ph::hard_macro(m) && !shapes.empty()
              && std::all_of(shapes.begin(), shapes.end(), [](const auto& s) { return s.layer == "M1"; })) {
            const auto old          = failed_pin.cands.size();
            failed_pin.extra_access = true;
            pin_access(failed_pin, failed_pin.lef);
            if (failed_pin.cands.size() > old && std::any_of(failed_pin.cands.begin(), failed_pin.cands.end(), [&](const Cand& cd) {
                  return cand_usable(ni, cd);
                })) {
              ++stat["pin_access_retries"];
              queue.push_front(ni);
              continue;
            }
          }
        }
        if (opt.shift > 0 && (access_blocked || net.fail_why == "no-path")) {
          const auto again = shift_repair(ni, net.pins[pin]);
          if (!again.empty()) {
            for (auto it = again.rbegin(); it != again.rend(); ++it) {
              if (*it != ni) {
                queue.push_front(*it);
              }
            }
            queue.push_front(ni);
            continue;
          }
        }
        if (opt.reserve && pass > 0 && net.pins[pin].comp >= 0 && cinfo[net.pins[pin].comp].reserve_tries < 2 && access_blocked) {
          // Leftover: clear the tracks over the cell, give all its pins a pad at once, then let everything come back.
          const int        c = net.pins[pin].comp;
          std::vector<int> again{ni};
          for (const auto& [n2, pi] : cinfo[c].pins) {
            if (std::find(again.begin(), again.end(), n2) == again.end() && !nets[n2].skip) {
              again.push_back(n2);
            }
          }
          for (const auto& [n2, pi] : cinfo[c].pins) {
            for (int b : find_blockers(n2, nets[n2].pins[pi], true)) {
              if (std::find(again.begin(), again.end(), b) == again.end()) {
                again.push_back(b);
              }
            }
          }
          ++cinfo[c].reserve_tries;
          std::vector<char> was_routed;
          for (int n2 : again) {
            was_routed.push_back(nets[n2].routed);
            rip(n2);
          }
          stat["reserve_attempts"] += 1;
          const bool held           = reserve_cell(c);
          for (size_t k = again.size(); k-- > 0;) {  // without pads only the nets that were routed have to come back
            if (held || was_routed[k]) {
              queue.push_front(again[k]);
            }
          }
          if (held) {
            continue;
          }
        }
        if (net.rips >= opt.ripup) {
          continue;
        }
        auto blockers = find_blockers(ni, net.pins[pin], pass > 0);
        if (net.fail_other >= 0 && (net.fail_why == "budget" || net.fail_why == "no-path")) {
          // The pin could leave but found nothing: the other end may be the enclosed one (a port at the die edge).
          for (int b : find_blockers(ni, net.pins[net.fail_other], true)) {
            if (std::find(blockers.begin(), blockers.end(), b) == blockers.end()) {
              blockers.push_back(b);
            }
          }
        }
        if (pass > 0) {  // leftovers: the nets with the fewest access choices go back first
          const auto scarce = [&](int n) {
            size_t least = SIZE_MAX;
            for (const auto& pp : nets[n].pins) {
              least = std::min(least, pp.cands.size());
            }
            return least;
          };
          std::stable_sort(blockers.begin(), blockers.end(), [&](int a, int b) { return scarce(a) < scarce(b); });
        }
        if (blockers.empty()) {
          continue;
        }
        ++net.rips;
        ++stat["ripup_rounds"];
        for (auto it = blockers.rbegin(); it != blockers.rend(); ++it) {
          rip(*it);
          ++nets[*it].rips;
          ++stat["ripped_nets"];
          queue.push_front(*it);
        }
        queue.push_front(ni);
      }
    }
    for (size_t c = 0; c < cinfo.size(); ++c) {  // cells no routed net asked for still need a legal site
      if (!cinfo[c].settled && !settle(int(c))) {
        ++stat["cells_without_site"];
      }
    }
    tiles_report("end");
    int done = 0, failed = 0;
    for (int ni : order) {
      nets[ni].failed = !nets[ni].routed;
      (nets[ni].routed ? done : failed)++;
      if (!nets[ni].routed && opt.explain > 0 && nets[ni].fail_pin >= 0) {
        --opt.explain;
        explain(ni, nets[ni].pins[nets[ni].fail_pin]);
      }
    }
    stat["ms_in_routed_calls"] = int64_t(time_ok * 1000);
    stat["ms_in_failed_calls"] = int64_t(time_fail * 1000);
    stat["expansions_routed"]  = exp_ok;
    stat["expansions_failed"]  = exp_fail;
    stat["nets_routed"]        = done;
    stat["nets_failed"]        = failed;
    opt.max_expand             = base_budget;
  }

  // Route-first repair: slide the cell of a blocked pin into free sites where every pin of the cell has an access,
  // and send its nets back to the queue. Returns the nets to reroute (empty when no move helps).
  std::vector<int> shift_repair(int ni, const Pin& pin) {
    const int c = pin.comp;
    if (c < 0 || !cinfo[c].movable || cinfo[c].moves >= opt.shift || pinned.count(c) || (cinfo[c].clock_sink && opt.flop_reach == 0)
        || anchored(c, ni)) {
      stat["moves_refused_anchored"] += c >= 0 && anchored(c, ni);
      return {};
    }
    const int cap = cinfo[c].clock_sink ? opt.flop_reach : opt.shift_cap;  // total sites away from the input pose
    // Judge new positions with the wires of the cell's nets left out; they are ripped only if a move is found.
    ++probe_tag;
    probing = true;
    for (const auto& [n2, pi] : cinfo[c].pins) {
      probe_skip[n2] = probe_tag;
    }
    const auto take_nets = [&] {
      std::vector<int> out{ni};
      for (const auto& [n2, pi] : cinfo[c].pins) {
        if (std::find(out.begin(), out.end(), n2) == out.end() && !nets[n2].skip) {
          stat["nets_ripped_for_moves"] += nets[n2].routed;
          rip(n2);
          out.push_back(n2);
        }
      }
      return out;
    };
    ++cinfo[c].moves;
    auto& comp = fp.components[c];
    int   lo, hi;  // room among the placed neighbours, less the cells still planned in between
    plan_bounds(c, lo, hi);
    const int  x    = int(comp.place.x);
    const int  site = 54;
    int        best = 0, best_score = 0;
    // A position is as good as its worst pin: the fewest access points any pin of the cell keeps there.
    const auto score_here = [&] {
      if (!comp_obstacles(c, false)) {
        return 0;
      }
      int score = INT32_MAX;
      for (const auto& [n2, pi] : cinfo[c].pins) {
        Pin tmp = nets[n2].pins[pi];
        pin_access(tmp, tmp.lef);
        int ok = 0;
        for (const auto& cd : tmp.cands) {
          ok += cand_usable(n2, cd);
        }
        score = std::min(score, ok);
      }
      return score;
    };
    for (int k = 1; k <= opt.shift_reach && best_score < 2; ++k) {
      for (int d : {-k, k}) {
        const int nx = x + d * site;
        if (nx < lo || nx + cinfo[c].w > hi || std::abs(nx - cinfo[c].x0) > cap * site) {
          continue;
        }
        comp.place.x = nx;
        if (const int score = score_here(); score > best_score) {
          best_score = score;
          best       = d;
        }
      }
    }
    comp.place.x = x;
    if (best == 0) {
      auto more = opt.chain > 0 ? chain_push(ni, c) : std::vector<int>{};
      if (more.empty() && tile_move(ni, c)) {  // no room in this tile: the cheapest nearby tile takes the cell
        more = take_nets();
      }
      probing                           = false;
      stat["move_probes_without_move"] += more.empty();
      return more;  // nothing was ripped when no move was found
    }
    probing        = false;
    const auto out = take_nets();
    count_pins(c, -1);
    cell_grid_update(c, -1);
    clear_cell_keeps(c);
    remove_owner(-10 - c);
    comp.place.x = x + best * site;
    comp_obstacles(c, true);
    if (cinfo[c].clock_sink) {
      ++stat["flop_moves"];
    }
    for (const auto& [n2, pi] : cinfo[c].pins) {
      pin_access(nets[n2].pins[pi], nets[n2].pins[pi].lef);
    }
    count_pins(c, 1);
    cell_grid_update(c, 1);
    if (opt.access_patterns && cinfo[c].shared_tracks) {
      reserve_cell(c);
    }
    ++stat["cell_moves"];
    stat["cell_move_sites"] += std::abs(best);
    return out;
  }
  // No free site next to the cell: push the run of cells between it and the nearest gap by one site.
  std::vector<int> chain_push(int ni, int c) {
    const int   site = 54;
    const auto& row  = row_cells.at(int(fp.components[c].place.y));
    const auto  span = row_span.at(int(fp.components[c].place.y));
    const int   at   = int(std::find(row.begin(), row.end(), c) - row.begin());
    if (plan.at(int(fp.components[c].place.y)).size() != row.size()) {
      return {};  // cells without a site are still planned into this row
    }
    for (int dir : {-1, 1}) {
      std::vector<int> chain;
      bool             ok = false;
      for (int i = at; i >= 0 && i < int(row.size()) && int(chain.size()) < opt.chain; i += dir) {
        const int   k  = row[i];
        const auto& ci = cinfo[k];
        const int   nx = int(fp.components[k].place.x) + dir * site;
        if (!ci.movable || pinned.count(k) || anchored(k, ni) || ci.moves >= opt.shift + 1
            || std::abs(nx - ci.x0) > (ci.clock_sink ? opt.flop_reach : opt.shift_cap) * site) {
          break;
        }
        chain.push_back(k);
        const int j    = i + dir;
        const int edge = dir > 0 ? (j < int(row.size()) ? int(fp.components[row[j]].place.x) : span.second)
                                 : (j >= 0 ? int(fp.components[row[j]].place.x) + cinfo[row[j]].w : span.first);
        const int gap  = dir > 0 ? edge - (int(fp.components[k].place.x) + ci.w) : int(fp.components[k].place.x) - edge;
        if (gap >= site) {
          ok = true;
          break;
        }
      }
      if (!ok) {
        continue;
      }
      // does the blocked cell get an access for every pin at the new site?
      auto&     comp = fp.components[c];
      const int x    = int(comp.place.x);
      comp.place.x   = x + dir * site;
      int score      = INT32_MAX;
      for (const auto& [n2, pi] : cinfo[c].pins) {
        Pin tmp = nets[n2].pins[pi];
        pin_access(tmp, tmp.lef);
        int good = 0;
        for (const auto& cd : tmp.cands) {
          good += cand_usable(n2, cd);
        }
        score = std::min(score, good);
      }
      comp.place.x = x;
      if (score < 1) {
        continue;
      }
      bool fits = true;  // upper-layer shapes of every pushed cell must clear the wires that stay
      for (int k : chain) {
        fp.components[k].place.x += dir * site;
        fits                      = fits && comp_obstacles(k, false);
        fp.components[k].place.x -= dir * site;
      }
      if (!fits) {
        ++stat["chain_refused_by_shapes"];
        continue;
      }
      std::vector<int> out{ni};
      for (int k : chain) {
        for (const auto& [n2, pi] : cinfo[k].pins) {
          if (std::find(out.begin(), out.end(), n2) == out.end() && !nets[n2].skip) {
            rip(n2);
            out.push_back(n2);
          }
        }
      }
      for (int k : chain) {
        count_pins(k, -1);
        cell_grid_update(k, -1);
        clear_cell_keeps(k);
        remove_owner(-10 - k);
        fp.components[k].place.x += dir * site;
      }
      for (int k : chain) {
        comp_obstacles(k, true);
        for (const auto& [n2, pi] : cinfo[k].pins) {
          pin_access(nets[n2].pins[pi], nets[n2].pins[pi].lef);
        }
        count_pins(k, 1);
        cell_grid_update(k, 1);
        ++cinfo[k].moves;
        ++stat["cell_moves"];
        ++stat["cell_move_sites"];
        if (cinfo[k].clock_sink) {
          ++stat["flop_moves"];
        }
      }
      if (opt.access_patterns) {
        for (int k : chain) {
          if (cinfo[k].shared_tracks) {
            reserve_cell(k);
          }
        }
      }
      ++stat["chain_pushes"];
      return out;
    }
    return {};
  }
  void explain(int ni, const Pin& pin) {
    std::cerr << "FAIL " << nets[ni].name << ' ' << pin.name << ' ' << nets[ni].fail_why << " at " << pin.cx << ',' << pin.cy
              << " cands " << pin.cands.size() << '\n';
    for (const auto& c : pin.cands) {
      std::cerr << "  cand track y " << c.y << " x " << c.x << " stop " << L[c.L].stops[c.s]
                << (cand_ok(ni, c) ? " ok" : " BLOCKED") << (c.v1 && !cut_ok(0, c.x, c.y, ni) ? " cut" : "") << '\n';
    }
    {  // how far can the pin reach with nothing to find?
      clear_marks();
      ban.clear();
      const int64_t        before = expansions;
      std::vector<int64_t> path;
      const Window         all{INT32_MIN / 2, INT32_MIN / 2, INT32_MAX / 2, INT32_MAX / 2}, here{pin.cx, pin.cy, pin.cx, pin.cy};
      search(ni, pin.cands, all, here, path);
      std::cerr << "  reach " << expansions - before << " states (" << why << ")";
      if (expansions - before < 5000) {  // stamp marks what the search touched
        std::array<int, 6> cnt{};
        int                bx0 = INT32_MAX, bx1 = INT32_MIN, by0 = INT32_MAX, by1 = INT32_MIN;
        for (const auto& entry : node.e) {
          if (entry.gen == node.gen) {
            int l2, t2, s2, x, y;
            unsid(entry.id & id_mask, l2, t2, s2);
            xy(l2, t2, s2, x, y);
            ++cnt[l2];
            bx0 = std::min(bx0, x);
            bx1 = std::max(bx1, x);
            by0 = std::min(by0, y);
            by1 = std::max(by1, y);
          }
        }
        std::cerr << " per layer " << cnt[0] << ' ' << cnt[1] << ' ' << cnt[2] << ' ' << cnt[3] << " bbox dx " << bx0 - pin.cx
                  << ".." << bx1 - pin.cx << " dy " << by0 - pin.cy << ".." << by1 - pin.cy;
      }
      std::cerr << '\n';
    }
    for (int l = 0; l < 2; ++l) {
      const auto& ly = L[l];
      const int   pc = ly.horiz ? pin.cy : pin.cx, ac = ly.horiz ? pin.cx : pin.cy;
      const int   rp = l == 0 ? 200 : 130, ra = l == 0 ? 300 : 400;
      auto        b = std::lower_bound(ly.tc.begin(), ly.tc.end(), pc - rp);
      for (; b != ly.tc.end() && *b <= pc + rp; ++b) {
        std::cerr << "  " << ly.name << " track " << *b << ':';
        for (const auto& s : ly.tr[b - ly.tc.begin()].s) {
          if (s.hi > ac - ra && s.lo < ac + ra) {
            std::cerr << " [" << s.lo - ac << ',' << s.hi - ac << ']'
                      << (s.net < 0 ? std::string("OBS") : nets[s.net].name.substr(std::min<size_t>(4, nets[s.net].name.size())));
          }
        }
        std::cerr << '\n';
      }
    }
  }
  // Nets whose metal or cuts keep `pin` from being reached: owners of whatever makes its candidates illegal, else the
  // owners of the lower-metal pieces around it.
  std::vector<int> find_blockers(int ni, const Pin& pin, bool wide = false) {
    std::map<int, int> score;
    bool               any_ok = false;
    for (const auto& c : pin.cands) {
      if (cand_ok(ni, c)) {
        any_ok = true;
        continue;
      }
      const auto& ly    = L[c.L];
      const int   m     = ly.e2e + ly.ext;
      const int   along = ly.horiz ? c.x : c.y;
      for (const auto& s : ly.tr[c.t].s) {
        if (s.net >= 0 && s.net != ni && s.hi > along - m && s.lo < along + m) {
          score[s.net] += 4;
        }
      }
      if (c.v1) {
        const auto& cl = CL[0];
        for (int bx = (c.x - 64) / bucket; bx <= (c.x + 64) / bucket; ++bx) {
          for (int by = (c.y - 64) / bucket; by <= (c.y + 64) / bucket; ++by) {
            auto it = cl.buckets.find(bkey(bx, by));
            if (it == cl.buckets.end()) {
              continue;
            }
            for (const auto& o : it->second) {
              if (o.net >= 0 && o.net != ni && std::abs((o.xl + o.xh) / 2 - c.x) < 40 && std::abs((o.yl + o.yh) / 2 - c.y) < 40) {
                score[o.net] += 4;
              }
            }
          }
        }
      }
    }
    if (any_ok || score.empty() || wide) {  // enclosed: whoever holds metal next to the pin on its access layers
      const int l0 = pin.cands.empty() ? 0 : pin.cands[0].L;
      for (int l = l0; l < std::min<int>(l0 + 2, int(L.size())); ++l) {
        const auto& ly = L[l];
        const int   pc = ly.horiz ? pin.cy : pin.cx, ac = ly.horiz ? pin.cx : pin.cy;
        const int   rp = 4 * ly.pitch + ly.pitch / 2, ra = 8 * ly.pitch;
        auto        b = std::lower_bound(ly.tc.begin(), ly.tc.end(), pc - rp);
        for (; b != ly.tc.end() && *b <= pc + rp; ++b) {
          for (const auto& sg : ly.tr[b - ly.tc.begin()].s) {
            if (sg.net >= 0 && sg.net != ni && sg.hi > ac - ra && sg.lo < ac + ra) {
              score[sg.net] += 1;
            }
          }
        }
      }
    }
    std::vector<std::pair<int, int>> v;
    for (const auto& [n, sc] : score) {
      if (nets[n].rips < opt.ripup && (wide || nets[n].slack > anchor_slack)) {  // anchored nets stay in the main pass
        v.emplace_back(-sc, n);
      }
    }
    std::sort(v.begin(), v.end());
    std::vector<int> out;
    for (size_t i = 0; i < v.size() && int(i) < (wide ? 4 * opt.rip_width : opt.rip_width); ++i) {
      out.push_back(v[i].second);
    }
    return out;
  }

  // ---------- native check ----------
  // Re-read a written DEF and test the four rules on its geometry alone: nothing of the router's own bookkeeping is
  // used, so a wrong wire in the file is found even when the tracks in memory look right.
  void verify(const std::string& def) {
    const auto out = ph::read_def_snapshot(def);
    // Every finding names its nets and its geometry, so a count can be traced to a place in the layout.
    struct Finding {
      std::string kind, layer, nets;
      int         xl, yl, xh, yh;
    };
    std::vector<Finding> found;
    const auto           net_name = [&](int n) { return n > 0 ? out.nets[n - 1].name.str() : std::string("fixed"); };
    const auto note = [&](const char* kind, const std::string& layer, const std::string& names, int xl, int yl, int xh, int yh) {
      found.push_back({kind, layer, names, xl, yl, xh, yh});
    };
    const auto on_track = [&](const char* kind, size_t l, int t, int lo, int hi, const std::string& names) {
      const auto& ly = L[l];
      const int   c  = ly.tc[t];
      ly.horiz ? note(kind, ly.name, names, lo, c - ly.w / 2, hi, c + ly.w / 2)
               : note(kind, ly.name, names, c - ly.w / 2, lo, c + ly.w / 2, hi);
    };
    struct Piece {
      int lo, hi, net;
    };
    std::vector<std::map<int, std::vector<Piece>>> on(L.size());  // layer -> track -> pieces
    struct Loose {
      int      l;
      ph::Rect r;
      int      net;
    };
    std::vector<Loose>            loose;  // wide or off-track metal
    std::vector<std::vector<Cut>> cuts(CL.size());
    const auto                    add = [&](const ph::Shape& sh, int net) {
      for (size_t k = 0; k < CL.size(); ++k) {
        if (CL[k].name == sh.layer) {
          cuts[k].push_back({int(sh.rect.xl), int(sh.rect.yl), int(sh.rect.xh), int(sh.rect.yh), net});
          return;
        }
      }
      for (size_t l = 0; l < L.size(); ++l) {
        const auto& ly = L[l];
        if (ly.name != sh.layer) {
          continue;
        }
        const int plo = int(ly.horiz ? sh.rect.yl : sh.rect.xl), phi = int(ly.horiz ? sh.rect.yh : sh.rect.xh);
        const int alo = int(ly.horiz ? sh.rect.xl : sh.rect.yl), ahi = int(ly.horiz ? sh.rect.xh : sh.rect.yh);
        const int t = phi - plo == ly.w ? find(ly.tc, (plo + phi) / 2) : -1;
        if (t >= 0 && plo == ly.tc[t] - ly.w / 2) {
          on[l][t].push_back({alo, ahi, net});
        } else {
          loose.push_back({int(l), sh.rect, net});
        }
      }
    };
    int id = 0;
    for (const auto& n : out.special_nets) {
      ph::visit_route_geometry(tech, out, n, [&](const ph::Shape& sh) { add(sh, -1); }, true);
    }
    std::unordered_map<std::string, int>                  net_id;
    std::unordered_map<std::string, const ph::Component*> cell_of;
    for (const auto& c : out.components) {
      cell_of.emplace(c.name.str(), &c);
    }
    for (size_t i = 0; i < out.nets.size(); ++i) {
      net_id.emplace(out.nets[i].name.str(), int(i) + 1);
    }
    for (const auto& t : out.terminals) {  // a port's metal belongs to its net
      const auto it = net_id.find(t.net);
      for (const auto& sh : t.shapes) {
        add({sh.layer, ph::orient_rect(sh.rect, 0, 0, t.orientation).translated(t.x, t.y)}, it == net_id.end() ? -1 : it->second);
      }
    }
    // Conductor check from the library the separate checker uses, on a copy of the technology with the cuts that lie
    // inside one pin removed (one pin is one terminal).
    ph::Technology conn_tech = tech;
    {
      std::map<std::string, std::set<std::set<std::string>>> stacks;
      for (const auto& [name, via] : conn_tech.vias) {
        std::set<std::string> metals, cut_layers;
        for (const auto& shape : via.shapes) {
          (conn_tech.layers.contains(shape.layer) ? metals : cut_layers).insert(shape.layer);
        }
        if (metals.size() == 2 && cut_layers.size() == 1 && conn_tech.cut_layers.contains(*cut_layers.begin())) {
          stacks[*cut_layers.begin()].insert(metals);
        }
      }
      for (auto& [name, macro] : conn_tech.macros) {
        for (auto& [pin_name, pin] : macro.pins) {
          const auto original = pin.shapes;
          std::erase_if(pin.shapes, [&](const auto& shape) {
            const auto st = stacks.find(shape.layer);
            if (st == stacks.end() || shape.rect.empty()) {
              return false;
            }
            std::set<std::string> landings;
            for (const auto& rect : original) {
              if (conn_tech.layers.contains(rect.layer) && rect.rect.contains(shape.rect)) {
                landings.insert(rect.layer);
              }
            }
            return st->second.contains(landings);
          });
        }
      }
    }
    const ph::Route_components                  cells(out);
    int64_t                                     open_nets = 0, bare_nets = 0, off_pin = 0;
    // A macro pin sits at the macro edge inside the macro's own blockage: the wire that continues the pin outwards
    // touches that blockage by construction. Such a piece is recognised by the pin of its net that it covers.
    std::unordered_map<int, std::vector<Loose>> edge_pins;
    for (const auto& n : out.nets) {
      ++id;
      if (n.wires.empty()) {
        if (n.connections.size() >= 2) {  // a count of zero violations must not hide a net that was never wired
          ++bare_nets;
          note("no-wires", "", n.name.str(), 0, 0, 0, 0);
        }
        continue;
      }
      std::vector<ph::Rect> pin_m1;  // M1 metal of the pins this net connects
      for (const auto& c : n.connections) {
        const auto it = cell_of.find(c.instance.str());
        if (it == cell_of.end()) {
          continue;
        }
        const auto& mc = tech.macros.at(it->second->macro.str());
        const auto  pp = mc.pins.find(c.pin.str());
        if (pp != mc.pins.end()) {
          for (const auto& sh : pp->second.shapes) {
            if (sh.layer == "M1") {
              pin_m1.push_back(ph::placed_rect(mc, it->second->place, out.dbu, sh.rect));
            } else if (mc.klass.starts_with("BLOCK")) {
              for (size_t l = 0; l < L.size(); ++l) {
                if (L[l].name == sh.layer) {
                  edge_pins[id].push_back({int(l), ph::placed_rect(mc, it->second->place, out.dbu, sh.rect), id});
                }
              }
            }
          }
        }
      }
      ph::visit_route_geometry(tech, out, n, [&](const ph::Shape& sh) {
        add(sh, id);
        if (sh.layer == "M1" && !ph::rect_union_covers(sh.rect, pin_m1)) {  // a via must land inside the pin it reaches
          ++off_pin;
          note("v1-off-pin", "M1", n.name.str(), int(sh.rect.xl), int(sh.rect.yl), int(sh.rect.xh), int(sh.rect.yh));
        }
      });
      if (n.connections.size() >= 2) {
        const auto r = ph::check_route_connectivity(conn_tech, out, n, &cells);
        if (!r.connected) {
          ++open_nets;
          note("open", "", n.name.str() + " (" + std::to_string(r.components) + " parts: " + r.reason + ")", 0, 0, 0, 0);
        }
      }
    }
    for (const auto& c : out.components) {  // cell obstructions above M1 are fixed metal too
      const auto& mc = tech.macros.at(c.macro.str());
      for (const auto& o : mc.obstructions) {
        if (o.layer != "M1" && (o.layer[0] == 'M' || o.layer[0] == 'V')) {
          add({o.layer, ph::placed_rect(mc, c.place, out.dbu, o.rect)}, -1);
        }
      }
    }
    int64_t shorts = 0, end_to_end = 0, facing = 0, min_len = 0, cut_space = 0, cut_dup = 0, wide = 0;
    for (size_t l = 0; l < L.size(); ++l) {
      const auto& ly = L[l];
      for (auto& [t, v] : on[l]) {  // merge what touches inside one net
        std::sort(v.begin(), v.end(), [](const Piece& a, const Piece& b) { return std::tie(a.net, a.lo) < std::tie(b.net, b.lo); });
        std::vector<Piece> merged;
        for (const auto& p : v) {
          if (!merged.empty() && merged.back().net == p.net && merged.back().hi >= p.lo) {
            merged.back().hi = std::max(merged.back().hi, p.hi);
          } else {
            merged.push_back(p);
          }
        }
        v = std::move(merged);
        std::sort(v.begin(), v.end(), [](const Piece& a, const Piece& b) { return a.lo < b.lo; });
        for (size_t i = 0; i < v.size(); ++i) {
          if (v[i].net > 0 && v[i].hi - v[i].lo < ly.minlen) {
            // A via landing entirely on an existing macro pin adds no
            // conductor outside the pin. A pad protruding even one DBU is
            // still subject to the routed minimum-length rule.
            const auto pins = edge_pins.find(v[i].net);
            const int  low = ly.tc[t] - ly.w / 2, high = ly.tc[t] + ly.w / 2;
            const auto covered = [&](const Loose& p) {
              const auto& r = p.r;
              return p.l == int(l) && std::lround(ly.horiz ? r.xl : r.yl) <= v[i].lo
                     && std::lround(ly.horiz ? r.xh : r.yh) >= v[i].hi && std::lround(ly.horiz ? r.yl : r.xl) <= low
                     && std::lround(ly.horiz ? r.yh : r.xh) >= high;
            };
            if (pins == edge_pins.end() || std::none_of(pins->second.begin(), pins->second.end(), covered)) {
              ++min_len;
              on_track("min-length", l, t, v[i].lo, v[i].hi, net_name(v[i].net));
            }
          }
          for (size_t j = i + 1; j < v.size() && v[j].lo - v[i].hi < ly.e2e; ++j) {
            const auto both = net_name(v[i].net) + " " + net_name(v[j].net);
            if (v[j].lo <= v[i].hi) {
              if (v[i].net != v[j].net) {
                ++shorts;
                on_track("short", l, t, v[j].lo, std::min(v[i].hi, v[j].hi), both);
              }
            } else {
              ++end_to_end;
              on_track("end-to-end", l, t, v[i].hi, v[j].lo, both);
            }
          }
        }
      }
      for (const auto& [t, v] : on[l]) {  // facing ends on the next track
        const auto up = on[l].find(t + 1);
        if (up == on[l].end() || ly.tc[t + 1] - ly.tc[t] > ly.pitch) {
          continue;
        }
        // Both lists are sorted by their low end: the pieces that start just past an end are found by a search.
        const auto ends = [&](const std::vector<Piece>& first, const std::vector<Piece>& second, bool swapped) {
          for (const auto& a : first) {
            auto b = std::lower_bound(second.begin(), second.end(), a.hi, [](const Piece& x, int at) { return x.lo < at; });
            for (; b != second.end() && b->lo - a.hi < ly.cs; ++b) {
              ++facing;
              on_track("facing-ends",
                       l,
                       t,
                       a.hi,
                       b->lo,
                       swapped ? net_name(b->net) + " " + net_name(a.net) : net_name(a.net) + " " + net_name(b->net));
            }
          }
        };
        ends(v, up->second, false);
        ends(up->second, v, true);
      }
    }
    for (const auto& w : loose) {  // signal wires against wide or off-track metal: plain spacing
      const auto& ly  = L[w.l];
      const int   plo = int(ly.horiz ? w.r.yl : w.r.xl), phi = int(ly.horiz ? w.r.yh : w.r.xh);
      const int   alo = int(ly.horiz ? w.r.xl : w.r.yl), ahi = int(ly.horiz ? w.r.xh : w.r.yh);
      const int   first = int(std::lower_bound(ly.tc.begin(), ly.tc.end(), plo - ly.w - ly.w / 2) - ly.tc.begin());
      for (auto it = on[w.l].lower_bound(first); it != on[w.l].end(); ++it) {
        const int   t = it->first;
        const auto& v = it->second;
        const int   c = ly.tc[t];
        if (c - ly.w / 2 - phi >= ly.w) {
          break;
        }
        if (std::max(plo - (c + ly.w / 2), (c - ly.w / 2) - phi) >= ly.w) {
          continue;
        }
        auto p = std::lower_bound(v.begin(), v.end(), alo - ly.w, [](const Piece& x, int at) { return x.hi <= at; });
        for (; p != v.end() && p->lo < ahi + ly.w; ++p) {
          if (p->net != w.net && p->net > 0 && p->hi > alo - ly.w) {
            if (w.net < 0) {
              const auto pins = edge_pins.find(p->net);
              bool       own  = false;
              if (pins != edge_pins.end()) {
                for (const auto& e : pins->second) {
                  const int elo = int(std::lround(ly.horiz ? e.r.xl : e.r.yl)), ehi = int(std::lround(ly.horiz ? e.r.xh : e.r.yh));
                  const int ec = int(std::lround(ly.horiz ? e.r.yl + e.r.yh : e.r.xl + e.r.xh)) / 2;
                  own          = own || (e.l == w.l && std::abs(ec - c) <= 1 && p->lo <= ehi && p->hi >= elo);
                }
              }
              if (own) {
                continue;
              }
            }
            ++wide;
            on_track("wide-spacing", w.l, t, p->lo, p->hi, net_name(p->net) + " " + net_name(w.net));
          }
        }
      }
    }
    for (size_t k = 0; k < CL.size(); ++k) {
      auto& v = cuts[k];
      std::sort(v.begin(), v.end(), [](const Cut& a, const Cut& b) { return std::tie(a.xl, a.yl) < std::tie(b.xl, b.yl); });
      // A power strap stacks thousands of cuts on one x, so neighbours are looked up in a grid keyed by the low corner.
      const int                                           cell = 512, space = CL[k].space;
      int                                                 maxw = 0, maxh = 0;
      std::unordered_map<uint64_t, std::vector<uint32_t>> grid;
      for (size_t i = 0; i < v.size(); ++i) {
        maxw = std::max(maxw, v[i].xh - v[i].xl);
        maxh = std::max(maxh, v[i].yh - v[i].yl);
        if (v[i].net >= 0) {  // two fixed cuts are never compared
          grid[bkey((v[i].xl + (1 << 24)) / cell, (v[i].yl + (1 << 24)) / cell)].push_back(uint32_t(i));
        }
      }
      for (size_t i = 0; i < v.size(); ++i) {
        for (int bx = (v[i].xl - space - maxw + (1 << 24)) / cell; bx <= (v[i].xh + space + (1 << 24)) / cell; ++bx) {
          for (int by = (v[i].yl - space - maxh + (1 << 24)) / cell; by <= (v[i].yh + space + (1 << 24)) / cell; ++by) {
            const auto bucket_it = grid.find(bkey(bx, by));
            if (bucket_it == grid.end()) {
              continue;
            }
            for (const uint32_t j : bucket_it->second) {
              if (j == i || (v[i].net >= 0 && j < i)) {  // a pair of signal cuts is seen from its first cut only
                continue;
              }
              const int64_t dx   = std::max({0, v[j].xl - v[i].xh, v[i].xl - v[j].xh}),
                            dy   = std::max({0, v[j].yl - v[i].yh, v[i].yl - v[j].yh});
              const auto    both = net_name(v[i].net) + " " + net_name(v[j].net);
              if (v[i].xl == v[j].xl && v[i].yl == v[j].yl && v[i].xh == v[j].xh && v[i].yh == v[j].yh && v[i].net == v[j].net) {
                ++cut_dup;
                note("cut-duplicate", CL[k].name, both, v[i].xl, v[i].yl, v[i].xh, v[i].yh);
              } else if (dx * dx + dy * dy < int64_t(space) * space) {
                ++cut_space;
                note("cut-spacing",
                     CL[k].name,
                     both,
                     std::min(v[i].xl, v[j].xl),
                     std::min(v[i].yl, v[j].yl),
                     std::max(v[i].xh, v[j].xh),
                     std::max(v[i].yh, v[j].yh));
              }
            }
          }
        }
      }
    }
    // placement: a cell sits on the site grid inside one ROW statement and overlaps no other cell, no hard placement
    // blockage and no taller cell that reaches into its row
    int64_t overlaps = 0, off_site = 0;
    struct Used {
      int  lo, hi;
      bool cell;
    };
    std::map<int, std::vector<Used>> rows;
    const auto                       cover = [&](const ph::Rect& r, int skip_y) {
      for (const auto& [y, parts] : row_parts) {
        if (y != skip_y && y < r.yh && y + 270 > r.yl) {
          rows[y].push_back({int(r.xl), int(r.xh), false});
        }
      }
    };
    for (const auto& b : out.snapshot_blockages) {
      if (b.placement && !b.soft) {
        for (const auto& r : b.rectangles) {
          cover(r, INT32_MIN);
        }
      }
    }
    for (const auto& c : out.components) {
      const auto& m   = tech.macros.at(c.macro.str());
      const auto  box = footprint(m, c.place, out.dbu);
      const int   w = int(box.xh - box.xl), h = int(box.yh - box.yl);
      rows[int(c.place.y)].push_back({int(c.place.x), int(c.place.x) + w, true});
      if (h > 271 || m.klass.starts_with("BLOCK")) {  // a macro is not a row cell, whatever its height
        cover(box, int(c.place.y));
        continue;
      }
      const auto parts = row_parts.find(int(c.place.y));
      bool       legal = false;
      if (parts != row_parts.end() && ph::orientation_allowed(m, c.place.orientation)
          && (c.place.orientation == row_orient.at(int(c.place.y))
              || c.place.orientation == mirrored(row_orient.at(int(c.place.y))))) {
        for (const auto& [lo, hi] : parts->second) {
          legal = legal || (c.place.x >= lo && c.place.x + w <= hi && (int(c.place.x) - parts->second.front().first) % 54 == 0);
        }
      }
      if (!legal) {
        ++off_site;
        note("cell-off-site", "", c.name.str(), int(c.place.x), int(c.place.y), int(c.place.x) + w, int(c.place.y) + h);
      }
    }
    for (auto& [y, v] : rows) {
      std::sort(v.begin(), v.end(), [](const Used& a, const Used& b) { return a.lo < b.lo; });
      for (size_t i = 0; i < v.size(); ++i) {
        for (size_t j = i + 1; j < v.size() && v[j].lo < v[i].hi; ++j) {
          if (v[i].cell || v[j].cell) {
            ++overlaps;
            note("cell-overlap", "", "", v[j].lo, y, std::min(v[i].hi, v[j].hi), y + 270);
          }
        }
      }
    }
    stat["verify_short"]              = shorts;
    stat["verify_end_to_end"]         = end_to_end;
    stat["verify_facing_ends"]        = facing;
    stat["verify_min_length"]         = min_len;
    stat["verify_cut_spacing"]        = cut_space;
    stat["verify_cut_duplicate"]      = cut_dup;
    stat["verify_wide_spacing"]       = wide;
    stat["verify_cell_overlap"]       = overlaps;
    stat["verify_cell_off_site"]      = off_site;
    stat["verify_open_nets"]          = open_nets;
    stat["verify_nets_without_wires"] = bare_nets;
    stat["verify_v1_off_pin"]         = off_pin;
    if (!opt.verify_report.empty()) {
      std::ofstream f(opt.verify_report);
      f << "kind\tlayer\tnets\txl\tyl\txh\tyh\n";
      for (const auto& x : found) {
        f << x.kind << '\t' << x.layer << '\t' << x.nets << '\t' << x.xl << '\t' << x.yl << '\t' << x.xh << '\t' << x.yh << '\n';
      }
    }
    for (size_t i = 0; opt.verbose && i < found.size() && i < 20; ++i) {  // the report file has every finding
      const auto& x = found[i];
      std::cerr << "verify " << x.kind << ' ' << x.layer << ' ' << x.nets << " (" << x.xl << ' ' << x.yl << ") (" << x.xh << ' '
                << x.yh << ")\n";
    }
  }

  // ---------- output ----------
  void write_def(const std::string& in, const std::string& out) {
    std::ifstream     f(in);
    std::stringstream ss;
    ss << f.rdbuf();
    const std::string        src = ss.str();
    // per-net wires from the tracks
    std::vector<std::string> wires(nets.size());
    for (size_t l = 0; l < L.size(); ++l) {
      const auto& ly = L[l];
      for (size_t t = 0; t < ly.tc.size(); ++t) {
        for (const auto& s : ly.tr[t].s) {
          if (s.net < 0 || !nets[s.net].routed) {
            continue;
          }
          auto& o      = wires[s.net];
          o           += o.empty() ? "\n      + ROUTED " : "\n      NEW ";
          const int c  = ly.tc[t];
          if (ly.horiz) {
            o += ly.name + " ( " + std::to_string(s.lo) + " " + std::to_string(c) + " 0 ) ( " + std::to_string(s.hi) + " * 0 )";
          } else {
            o += ly.name + " ( " + std::to_string(c) + " " + std::to_string(s.lo) + " 0 ) ( * " + std::to_string(s.hi) + " 0 )";
          }
          stat["wl_" + ly.name] += s.hi - s.lo;
        }
      }
    }
    for (size_t i = 0; i < stubs.size(); ++i) {
      const auto& sb  = stubs[i];
      const auto& net = nets[sb.net];
      const auto& pin = net.pins[sb.pin];
      if (!net.routed || pin.used < 0 || pin.cands[pin.used].stub != int(i) || (sb.lo == sb.pin_lo && sb.hi == sb.pin_hi)) {
        continue;
      }
      // Emit the selected extension in full. If the via lands directly on existing pin metal, the LEF already
      // supplies that conductor; duplicating a short pin as a signal wire creates a false minimum-length segment.
      const auto& ly = L[sb.l];
      const int   lo = sb.lo, hi = sb.hi;
      const int   c  = (sb.plo + sb.phi) / 2;
      auto&       o  = wires[sb.net];
      o             += o.empty() ? "\n      + ROUTED " : "\n      NEW ";
      if (ly.horiz) {
        o += ly.name + " ( " + std::to_string(lo) + " " + std::to_string(c) + " 0 ) ( " + std::to_string(hi) + " * 0 )";
      } else {
        o += ly.name + " ( " + std::to_string(c) + " " + std::to_string(lo) + " 0 ) ( * " + std::to_string(hi) + " 0 )";
      }
      stat["wl_" + ly.name] += hi - lo;
      ++stat["edge_pin_stub_wires"];
    }
    const char* lower[] = {"M1", "M2", "M3", "M4", "M5", "M6"};
    for (size_t i = 0; i < nets.size(); ++i) {
      if (!nets[i].routed) {
        continue;
      }
      auto all_vias = nets[i].vias;
      all_vias.insert(all_vias.end(), resv[i].begin(), resv[i].end());
      for (const auto& v : all_vias) {
        auto& o  = wires[i];
        o       += o.empty() ? "\n      + ROUTED " : "\n      NEW ";
        o       += std::string(lower[v.cl]) + " ( " + std::to_string(v.x) + " " + std::to_string(v.y) + " ) " + CL[v.cl].via;
        ++stat["vias_" + CL[v.cl].name];
      }
    }
    // Read records in the same order as the DEF reader callbacks. Preserve names, comments,
    // properties and unsupported sections verbatim. Only replace the three moved pose fields
    // and insert wires before their record terminator.
    enum class Section { other, components, nets };
    Section section = Section::other;
    size_t  cursor = 0, copied = 0, component = 0, net = 0;
    auto    next = [&]() -> std::string_view {
      while (cursor < src.size()) {
        if (src[cursor] == '#') {
          cursor = std::min(src.size(), src.find('\n', cursor));
        } else if (std::isspace(static_cast<unsigned char>(src[cursor]))) {
          ++cursor;
        } else {
          break;
        }
      }
      const size_t first = cursor;
      if (cursor == src.size()) {
        return {};
      }
      const char lead = src[cursor++];
      if (lead == '"') {
        while (cursor < src.size()) {
          const char c = src[cursor++];
          if (c == '"') {
            break;
          }
          if (c == '\\' && cursor < src.size()) {
            ++cursor;
          }
        }
      } else if (lead != ';') {
        if (lead == '\\' && cursor < src.size()) {
          ++cursor;
        }
        while (cursor < src.size() && !std::isspace(static_cast<unsigned char>(src[cursor])) && src[cursor] != ';') {
          if (src[cursor++] == '\\' && cursor < src.size()) {
            ++cursor;
          }
        }
      }
      return std::string_view(src).substr(first, cursor - first);
    };
    std::ostringstream result;
    const auto         replace = [&](std::string_view field, const std::string& value) {
      const size_t offset = field.data() - src.data();
      result.write(src.data() + copied, offset - copied);
      result << value;
      copied = offset + field.size();
    };
    bool grid_needed = fp.gcell_grid.empty();
    for (auto token = next(); !token.empty(); token = next()) {
      if (section == Section::other) {
        if (token == "NETS" || token == "COMPONENTS") {
          const auto start = cursor;
          const auto count = next();
          if (!count.empty() && count.find_first_not_of("0123456789") == std::string_view::npos && next() == ";") {
            section = token == "NETS" ? Section::nets : Section::components;
            if (grid_needed) {
              replace(token,
                      "GCELLGRID X 0 DO " + std::to_string(int(fp.die.xh) / 540 + 1) + " STEP 540 ;\nGCELLGRID Y 0 DO "
                          + std::to_string(int(fp.die.yh) / 540 + 1) + " STEP 540 ;\n" + std::string(token));
              grid_needed = false;
            }
          } else {
            cursor = start;
          }
        }
      } else if (token == "END") {
        next();  // section name
        section = Section::other;
      } else if (token == "-") {
        const bool is_cell    = section == Section::components;
        const bool moved      = is_cell && def_place.at(component) != fp.components.at(component).place;
        bool       wrote_pose = false;
        for (token = next(); !token.empty() && token != ";"; token = next()) {
          if (moved && token == "+" && next() == "PLACED") {
            const auto& pose = fp.components[component].place;
            if (next() != "(") {
              throw std::runtime_error("missing placement coordinates");
            }
            replace(next(), std::to_string(pose.x));
            replace(next(), std::to_string(pose.y));
            if (next() != ")") {
              throw std::runtime_error("missing placement orientation");
            }
            replace(next(), ph::orientation_name(pose.orientation));
            wrote_pose = true;
          }
        }
        if (token.empty() || (moved && !wrote_pose)) {
          throw std::runtime_error("incomplete DEF output record");
        }
        if (is_cell) {
          stat["cells_repositioned"] += moved;
          ++component;
        } else {
          const auto& wire = wires.at(net++);
          if (!wire.empty()) {
            replace(token, wire + " ;");
          }
        }
      }
    }
    if (net != nets.size() || component != fp.components.size() - gap_width.size()) {
      throw std::runtime_error("DEF output record count differs from parsed floorplan");
    }
    result << std::string_view(src).substr(copied);
    std::ofstream out_file(out);
    out_file.exceptions(std::ios::failbit | std::ios::badbit);
    out_file << result.view();
    out_file.close();
  }
};
void parse_options(Settings& opt, const std::vector<std::string>& args, std::vector<std::string>& pos) {
  for (size_t i = 0; i < args.size(); ++i) {
    const auto& a    = args[i];
    const auto  next = [&] {
      if (i + 1 >= args.size()) {
        throw std::runtime_error("missing value for " + a);
      }
      return args[++i];
    };
    if (a == "--agent") {
      opt.agent = true;
    } else if (a == "--order") {
      opt.order = next();
    } else if (a == "--m2-cost") {
      opt.m2_cost = std::stod(next());
    } else if (a == "--m3-cost") {
      opt.m3_cost = std::stod(next());
    } else if (a == "--hweight") {
      opt.hweight = std::stod(next());
    } else if (a == "--pd-alpha") {
      opt.pd_alpha = std::stod(next());
    } else if (a == "--pin-cost") {
      opt.pin_cost = std::stod(next());
    } else if (a == "--pin-footprint") {
      opt.pin_footprint = std::stod(next());
    } else if (a == "--spread") {
      opt.spread = std::stod(next());
    } else if (a == "--spread-rounds") {
      opt.spread_rounds = std::stoi(next());
    } else if (a == "--pin-tile") {
      opt.pin_tile = std::stoi(next());
    } else if (a == "--hi-cost") {
      opt.hi_cost = std::stod(next());
    } else if (a == "--via-cost") {
      opt.via_cost = std::stod(next());
    } else if (a == "--margin") {
      opt.margin = std::stoi(next());
    } else if (a == "--limit") {
      opt.limit = std::stoi(next());
    } else if (a == "--report") {
      opt.report = next();
    } else if (a == "--ripup") {
      opt.ripup = std::stoi(next());
    } else if (a == "--rip-width") {
      opt.rip_width = std::stoi(next());
    } else if (a == "--budget") {
      opt.max_expand = std::stoll(next());
    } else if (a == "--shift") {
      opt.shift = std::stoi(next());
    } else if (a == "--shift-reach") {
      opt.shift_reach = std::stoi(next());
    } else if (a == "--flop-reach") {
      opt.flop_reach = std::stoi(next());
    } else if (a == "--shift-cap") {
      opt.shift_cap = std::stoi(next());
    } else if (a == "--chain") {
      opt.chain = std::stoi(next());
    } else if (a == "--final-passes") {
      opt.final_passes = std::stoi(next());
    } else if (a == "--explain") {
      opt.explain = std::stoi(next());
    } else if (a == "--aim-tree") {
      opt.aim_link = false;
    } else if (a == "--clock-nets") {
      opt.clock_nets = next();
    } else if (a == "--long-frac") {
      opt.long_frac = std::stod(next());
    } else if (a == "--only") {
      opt.only = next();
    } else if (a == "--prelegal") {
      opt.prelegal = true;
    } else if (a == "--tile-move") {
      opt.tile_move = true;
    } else if (a == "--reserve") {
      opt.reserve = true;
    } else if (a == "--reserve-cells") {
      opt.reserve_cells = next();
    } else if (a == "--access-patterns") {
      opt.access_patterns = true;
    } else if (a == "--complex-first") {
      opt.complex_first = true;
    } else if (a == "--cell-cost") {
      opt.cell_cost = std::stod(next());
    } else if (a == "--verify-report") {
      opt.verify        = true;
      opt.verify_report = next();
    } else if (a == "--verify") {
      opt.verify = true;
    } else if (a == "--space-frac") {
      opt.space_frac = std::stod(next());
    } else if (a == "--space-min-len") {
      opt.space_min_len = std::stoi(next());
    } else if (a == "--space-min-layer") {
      opt.space_min_layer = std::stoi(next());
    } else if (a == "--time-limit") {
      opt.time_limit = std::stod(next());
    } else if (a == "--anchor-frac") {
      opt.anchor_frac = std::stod(next());
    } else if (a == "--timing") {
      opt.timing = next();
    } else if (a == "--crit-frac") {
      opt.crit_frac = std::stod(next());
    } else if (a == "--trace") {
      opt.trace = next();
    } else if (a == "-v") {
      opt.verbose = true;
    } else if (a.starts_with("--")) {
      throw std::runtime_error("unknown rf_route option: " + a);
    } else {
      pos.push_back(a);
    }
  }
}

void run(Router& r, const livehd::routing::Frame& frame, const std::string& report, bool agent, std::ostream& summary) {
  for (double fraction : {r.opt.anchor_frac, r.opt.crit_frac, r.opt.space_frac, r.opt.long_frac, r.opt.pd_alpha}) {
    if (!(fraction >= 0 && fraction <= 1)) {
      throw std::runtime_error("routing fractions and pd-alpha must be between 0 and 1");
    }
  }
  if (r.opt.spread_rounds < 0) {
    throw std::invalid_argument("spread-rounds must be nonnegative");
  }
  if (r.opt.pin_tile < 1) {
    throw std::runtime_error("pin-tile must be at least one pin");
  }
  for (double cost : {r.opt.m2_cost,
                      r.opt.m3_cost,
                      r.opt.hi_cost,
                      r.opt.via_cost,
                      r.opt.hweight,
                      r.opt.pin_cost,
                      r.opt.cell_cost,
                      r.opt.pin_footprint,
                      r.opt.spread}) {
    if (!std::isfinite(cost) || cost < 0) {
      throw std::runtime_error("routing costs and heuristic weight must be finite and nonnegative");
    }
  }
  const double t0 = now_s(), c0 = cpu_s();
  r.setup(frame.lefs, frame.input_def);
  const double t1 = now_s(), c1 = cpu_s();
  r.run();
  double route_wall = now_s() - t1, route_cpu = cpu_s() - c1;
  if (agent) {
    const auto status = [&] {
      int                     routed = 0, pending = 0, deferred = 0;
      std::ostringstream      failures;
      std::vector<const Net*> attempted, unattempted;
      for (const auto& n : r.nets) {
        if (n.skip) {
          continue;
        }
        if (n.routed) {
          ++routed;
          continue;
        }
        ++pending;
        const bool waiting  = n.calls == 0 && n.fail_why == "time";
        deferred           += waiting;
        auto& group         = waiting ? unattempted : attempted;
        if (group.size() < 20) {
          group.push_back(&n);
        }
      }
      attempted.insert(attempted.end(), unattempted.begin(), unattempted.end());
      for (size_t i = 0; i < std::min(size_t{20}, attempted.size()); ++i) {
        const auto& n = *attempted[i];
        if (i) {
          failures << ',';
        }
        failures << "{\"net\":" << std::quoted(n.name) << ",\"reason\":" << std::quoted(n.fail_why);
        if (n.fail_pin >= 0) {
          const auto& pin = n.pins[n.fail_pin];
          failures << ",\"pin\":" << std::quoted(pin.name) << ",\"x\":" << pin.cx << ",\"y\":" << pin.cy;
        }
        failures << '}';
      }
      std::cout << "{\"routed\":" << routed << ",\"pending\":" << pending << ",\"deferred\":" << deferred << ",\"failures\":["
                << failures.str() << "]}" << std::endl;
    };
    status();
    for (std::string line; std::getline(std::cin, line);) {
      std::istringstream cmd(line);
      std::string        op, extra;
      cmd >> op;
      if (op == "finish" && !(cmd >> extra)) {
        break;
      }
      if (op == "release") {
        std::string name;
        cmd >> name;
        auto it = std::find_if(r.nets.begin(), r.nets.end(), [&](const Net& n) { return n.name == name && n.routed; });
        if (it == r.nets.end() || (cmd >> extra)) {
          std::cout << "{\"error\":\"expected a routed, mutable net\"}" << std::endl;
          continue;
        }
        r.rip(int(it - r.nets.begin()));
      } else if (op == "retry") {
        int64_t budget;
        int     passes, ripup;
        double  seconds;
        if (!(cmd >> budget >> passes >> ripup >> seconds) || (cmd >> extra) || budget <= 0 || budget > INT32_MAX || passes < 0
            || passes > 100 || ripup < 0 || ripup > 100 || !std::isfinite(seconds) || seconds < 0) {
          std::cout << "{\"error\":\"expected retry expansions passes ripups cpu_seconds\"}" << std::endl;
          continue;
        }
        r.opt.max_expand   = budget;
        r.opt.final_passes = passes;
        r.opt.ripup        = ripup;
        r.opt.time_limit   = seconds;
        for (auto& n : r.nets) {
          n.rips = 0;
        }
        const double before = now_s(), cpu_before = cpu_s();
        r.run(true);
        route_wall += now_s() - before;
        route_cpu  += cpu_s() - cpu_before;
      } else if (op != "status" || (cmd >> extra)) {
        std::cout << "{\"error\":\"expected status, release, retry or finish\"}" << std::endl;
        continue;
      }
      status();
    }
  }
  const double t2 = now_s();
  r.write_def(frame.input_def, frame.output_def);
  if (r.opt.verify) {
    r.verify(frame.output_def);
  }
  const double       t3 = now_s();
  std::ostringstream o;
  o << "{\n  \"setup_s\": " << t1 - t0 << ", \"route_s\": " << route_wall << ", \"write_s\": " << t3 - t2
    << ", \"setup_cpu_s\": " << c1 - c0 << ", \"route_cpu_s\": " << route_cpu << ",\n  \"states\": " << r.nstates
    << ", \"expansions\": " << r.expansions;
  for (const auto& [k, v] : r.stat) {
    o << ",\n  \"" << k << "\": " << v;
  }
  o << "\n}\n";
  if (!report.empty()) {
    std::ofstream f(report);
    f.exceptions(std::ios::badbit | std::ios::failbit);
    f << "net\tpins\thpwl\tstatus\tfail_pin\twhy\texpansions\tcalls\tfail_x_dbu\tfail_y_dbu\n";
    for (const auto& n : r.nets) {
      if (!n.skip) {
        f << n.name << '\t' << n.pins.size() << '\t' << n.bbox_hp << '\t'
          << (n.routed   ? "routed"
              : n.failed ? "failed"
                         : "unattempted")
          << '\t' << (n.fail_pin >= 0 ? n.pins[n.fail_pin].name : "") << '\t' << n.fail_why << '\t' << n.expanded << '\t' << n.calls
          << '\t';
        if (n.fail_pin >= 0) {
          const auto& pin = n.pins[n.fail_pin];
          f << pin.cx << '\t' << pin.cy;
        } else {
          f << '\t';
        }
        f << '\n';
      }
    }
    f.close();
  }
  summary << o.str();
}
}  // namespace

namespace livehd::routing {

Settings parse_settings(const std::vector<std::string>& args) {
  Settings                 settings;
  std::vector<std::string> positional;
  parse_options(settings, args, positional);
  if (!positional.empty()) {
    throw std::invalid_argument("unexpected routing argument: " + positional.front());
  }
  return settings;
}

static Outcome outcome(const Router& router, std::string stats) {
  Outcome result{std::move(stats), {}};
  for (const auto& [name, value] : router.stat) {
    if (name.starts_with("verify_") && value) {
      result.findings.emplace(name, value);
    }
  }
  return result;
}

Outcome route_frame(const Frame& frame, const Settings& settings) {
  Router router;
  router.opt = settings;
  std::ostringstream summary;
  run(router, frame, settings.report, settings.agent, summary);
  return outcome(router, summary.str());
}

Outcome verify_frame(const Frame& frame, const Settings& settings) {
  Router router;
  router.opt = settings;
  router.setup(frame.lefs, frame.input_def);
  router.verify(frame.input_def);
  std::ostringstream counts;
  for (const auto& [key, value] : router.stat) {
    if (key.starts_with("verify_")) {
      counts << key << ' ' << value << '\n';
    }
  }
  return outcome(router, counts.str());
}

int worker(int argc, char** argv) try {
  if (argc == 2 && std::string_view(argv[1]) == "--identity") {
    std::cout << LHD_ROUTE_ID << '\n';
    return 0;
  }
  Settings                 settings;
  std::vector<std::string> positional;
  parse_options(settings, {argv + 1, argv + argc}, positional);
  if (positional.size() < 3) {
    throw std::invalid_argument("routing worker expects input DEF, output DEF and ordered LEFs");
  }
  Frame frame{
      positional[0],
      positional[1],
      {positional.begin() + 2, positional.end()}
  };
  const auto result = frame.output_def == "-" ? verify_frame(frame, settings) : route_frame(frame, settings);
  std::cout << result.stats;
  return 0;
} catch (const std::exception& e) {
  std::cerr << e.what() << '\n';
  return 1;
}
}  // namespace livehd::routing

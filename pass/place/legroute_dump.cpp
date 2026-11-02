// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
// legroute.dump_regions: a region as a chip of its own (2l-legroute.html), so an external router can be asked about
// the region alone: its cells at the tiles legroute gave them, every crossing net cut at the region boundary into a
// die pin on the track the hop took (a track of the tile row or column when the region has no answer), the power
// grid and the macros that reach into it. Routing the small DEF answers whether the region's tracks carry its
// nets, and compares with the SAT's verdict, in minutes instead of a whole-chip run.
#include <cmath>
#include <fstream>
#include <set>

#include "core/json_util.hpp"
#include "legroute_internal.hpp"
#include "row_occupancy.hpp"

namespace livehd::placement::lr {
namespace ph = physical;

void dump_regions(const Design& d, const Grid& g, const Boxes& boxes, const Routing& R, const Legroute_regions& regions,
                  const Legroute_report& rep, const std::string& dir, const std::string& which) {
  const auto&      fp = d.fp;
  std::vector<int> chosen;
  if (which == "all") {
    for (int r = 0; r < g.regions; ++r) {
      chosen.push_back(r);
    }
  } else if (which == "failed") {
    for (size_t r = 0; r < rep.region_stats.size(); ++r) {
      if (rep.region_stats[r].status == 1 || rep.region_stats[r].status == 2) {
        chosen.push_back(int(r));
      }
    }
  } else {
    for (size_t pos = 0; pos < which.size();) {
      const auto comma = which.find(',', pos);
      const auto item  = which.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
      if (!item.empty()) {
        chosen.push_back(std::stoi(item));
      }
      pos = comma == std::string::npos ? which.size() : comma + 1;
    }
  }
  const auto macro_rect = [&](size_t i) {
    const auto& c = fp.components[i];
    return ph::Rect{double(c.place.x),
                    double(c.place.y),
                    double(c.place.x + std::llround(d.macro[i]->width * fp.dbu)),
                    double(c.place.y + std::llround(d.macro[i]->height * fp.dbu))};
  };
  const auto intersects = [](const ph::Rect& a, const ph::Rect& b) {
    return a.xl < b.xh && b.xl < a.xh && a.yl < b.yh && b.yl < a.yh;
  };
  for (const int r : chosen) {
    if (r < 0 || r >= g.regions || g.region_tiles[size_t(r)].empty()) {
      continue;
    }
    int c0 = g.cols, c1 = -1, r0 = g.rows, r1 = -1;
    for (int t : g.region_tiles[size_t(r)]) {
      c0 = std::min(c0, g.col(t));
      c1 = std::max(c1, g.col(t));
      r0 = std::min(r0, g.row(t));
      r1 = std::max(r1, g.row(t));
    }
    const ph::Rect box{double(g.left(c0)), double(g.bottom(r0)), double(g.left(c1 + 1)), double(g.bottom(r1 + 1))};
    const auto     inside_tile = [&](int t) { return g.col(t) >= c0 && g.col(t) <= c1 && g.row(t) >= r0 && g.row(t) <= r1; };
    const auto     inside_pt   = [&](double x, double y) { return x >= box.xl && x < box.xh && y >= box.yl && y < box.yh; };
    ph::Floorplan  sub;
    sub.design = fp.design + "_region_" + std::to_string(r);
    sub.dbu    = fp.dbu;
    sub.die    = box;
    sub.tracks = fp.tracks;
    sub.vias   = fp.vias;
    {
      std::set<std::string> have;
      for (const auto& v : sub.vias) {
        have.insert(v.name);
      }
      for (const auto& v : regions.power_vias) {
        if (have.insert(v.name).second) {
          sub.vias.push_back(v);
        }
      }
    }
    sub.nondefault_rules = fp.nondefault_rules;
    // the cells: the region's boxes' cells where legroute put them; the fixed cells and macros reaching into the box
    std::vector<char> included(fp.components.size(), 0);
    size_t            cells = 0, fixed_cells = 0, macros = 0;
    for (size_t i = 0; i < fp.components.size(); ++i) {
      const auto& c    = fp.components[i];
      const auto  role = frame_role(d.tech, c);
      if (role == Frame_role::movable) {
        const int cell = d.cell[i];
        const int b    = cell >= 0 ? boxes.box_of[size_t(cell)] : -1;
        const bool in  = b >= 0 && boxes.boxes[size_t(b)].tile >= 0 ? inside_tile(boxes.boxes[size_t(b)].tile)
                                                                     : inside_pt(double(c.place.x), double(c.place.y));
        if (in) {
          included[i] = 1;
          ++cells;
        }
      } else if (role == Frame_role::fixed_logic || role == Frame_role::obstacle) {
        const auto rect = macro_rect(i);
        if (intersects(rect, box)) {
          included[i] = 1;
          if (!d.macro[i]->klass.starts_with("CORE")) {  // a hard macro: the die grows to hold it whole
            ++macros;
            sub.die.xl = std::min(sub.die.xl, rect.xl);
            sub.die.yl = std::min(sub.die.yl, rect.yl);
            sub.die.xh = std::max(sub.die.xh, rect.xh);
            sub.die.yh = std::max(sub.die.yh, rect.yh);
          } else {
            ++fixed_cells;
          }
        }
      }
    }
    for (size_t i = 0; i < fp.components.size(); ++i) {
      if (included[i]) {
        sub.components.push_back(fp.components[i]);
      }
    }
    // the rows: the region's span of each placement row, on the row's site grid
    for (const auto& row : fp.rows) {
      if (row.y < box.yl || row.y >= box.yh || row.step <= 0) {
        continue;
      }
      const int64_t x0 = row.x + (std::max<int64_t>(0, int64_t(box.xl) - row.x) + row.step - 1) / row.step * row.step;
      const int64_t x1 = std::min<int64_t>(row.x + row.count * row.step, int64_t(box.xh));
      if (x1 <= x0) {
        continue;
      }
      ph::Row cut  = row;
      cut.x        = x0;
      cut.count    = (x1 - x0) / row.step;
      cut.name     = row.name + "_r" + std::to_string(r);
      sub.rows.push_back(cut);
    }
    // the die pins: the chip's own terminals inside the box, and one pin per hop of a crossing net on the hop's
    // layer and track (L5), else a track of the hop tile's row or column, distinct per edge
    std::vector<char> term_in(fp.terminals.size(), 0);
    for (size_t i = 0; i < fp.terminals.size(); ++i) {
      if (inside_pt(double(fp.terminals[i].x), double(fp.terminals[i].y))) {
        term_in[i] = 1;
        sub.terminals.push_back(fp.terminals[i]);
      }
    }
    std::map<int, std::vector<std::string>>    hop_pins;  // net -> pin names
    std::set<std::tuple<int, int, int64_t>>    taken;     // (edge side, layer, track)
    std::map<int, int>                         hop_count;
    size_t                                     hops = 0, hops_dropped = 0;
    const auto driver_inside = [&](int n) {
      const auto& N = d.nets[size_t(n)];
      if (N.driver < 0) {
        return false;
      }
      const auto& p = N.pins[size_t(N.driver)];
      if (p.comp >= 0) {
        return included[size_t(p.comp)] != 0;
      }
      return p.term >= 0 && term_in[size_t(p.term)];
    };
    for (const auto& c : R.conns) {
      if (c.net < 0 || c.a < 0 || c.b < 0) {
        continue;
      }
      const int ta = R.trees[size_t(c.net)].nodes[size_t(c.a)].tile, tb = R.trees[size_t(c.net)].nodes[size_t(c.b)].tile;
      if (ta < 0 || tb < 0 || inside_tile(ta) == inside_tile(tb) || c.poly.size() > 2) {
        continue;  // a hop: the one-edge connection between an inside tile and an outside tile
      }
      const int  p = inside_tile(ta) ? ta : tb, q = inside_tile(ta) ? tb : ta;
      const bool horizontal = g.row(p) == g.row(q);  // the crossing wire runs horizontally: a vertical die edge
      int        layer      = -1;
      int64_t    track      = -1;
      for (int s : c.segs) {
        const auto& seg = R.segments[size_t(s)];
        if (seg.layer >= 0 && seg.track >= 0 && g.layers[size_t(seg.layer)].horizontal == horizontal) {
          layer = seg.layer;
          track = seg.track;
          break;
        }
      }
      const int side = horizontal ? (g.col(q) > g.col(p) ? 1 : 0) : (g.row(q) > g.row(p) ? 3 : 2);  // L, R, B, T
      // the pin's rectangle stays inside the region's die, two pitches clear of the corners: a pin that leaves the die
      // (a track within half a width of a corner) has no access point, and OpenROAD's maze route aborts on it
      const auto fits = [&](int l, int64_t t) {
        const auto&  L            = g.layers[size_t(l)];
        const double w            = double(L.width > 0 ? L.width : L.pitch / 2);
        const double track_center = double(g.track_coord(l, t));
        const double m            = w / 2 + 2.0 * double(L.pitch);
        return horizontal ? (track_center - m >= box.yl && track_center + m <= box.yh)
                          : (track_center - m >= box.xl && track_center + m <= box.xh);
      };
      if (layer < 0 || taken.contains({side, layer, track}) || !fits(layer, track)) {
        // a free track of the crossing's direction, nearest the hop tile: its band first, then the bands beside it
        // (a boundary tile carries more hops than one layer's tracks), every layer of the direction per band
        const int band = horizontal ? g.row(p) : g.col(p), bands = horizontal ? g.rows : g.cols;
        layer          = -1;
        for (int ring = 0; ring < bands && layer < 0; ++ring) {
          for (const int b : {band + ring, band - ring}) {
            if (b < 0 || b >= bands || (ring == 0 && b != band + ring)) {
              continue;
            }
            if (horizontal ? (b < r0 || b > r1) : (b < c0 || b > c1)) {
              continue;  // the pin stays on the region's own edge span
            }
            for (size_t l = 0; l < g.layers.size() && layer < 0; ++l) {
              if (g.layers[l].horizontal != horizontal) {
                continue;
              }
              const auto [f, hi] = g.tracks(int(l), b);
              for (int64_t k = 0; k <= hi - f && layer < 0; ++k) {  // from the band's middle outward
                const int64_t t = (f + hi) / 2 + (k % 2 ? -(k + 1) / 2 : (k + 1) / 2);
                if (t >= f && t <= hi && !taken.contains({side, int(l), t}) && fits(int(l), t)) {
                  layer = int(l);
                  track = t;
                }
              }
            }
          }
        }
        if (layer < 0) {
          ++hops_dropped;
          continue;  // no track left on that edge: the hop stays out
        }
      }
      taken.emplace(side, layer, track);
      const auto& L    = g.layers[size_t(layer)];
      const double w   = double(L.width > 0 ? L.width : L.pitch / 2);
      const double len = 4.0 * double(L.pitch);
      ph::Terminal pin;
      const int    k      = hop_count[c.net]++;
      pin.name            = fp.nets[d.nets[size_t(c.net)].frame].name + "_hop" + std::to_string(k);
      pin.net             = fp.nets[d.nets[size_t(c.net)].frame].name;
      pin.direction       = driver_inside(c.net) ? "OUTPUT" : "INPUT";
      pin.status          = ph::Status::FIXED;
      const int64_t coord = g.track_coord(layer, track);
      if (horizontal) {
        pin.x = int64_t(side == 1 ? box.xh : box.xl);
        pin.y = coord;
        pin.shapes.push_back({L.name, side == 1 ? ph::Rect{-len, -w / 2, 0, w / 2} : ph::Rect{0, -w / 2, len, w / 2}});
      } else {
        pin.x = coord;
        pin.y = int64_t(side == 3 ? box.yh : box.yl);
        pin.shapes.push_back({L.name, side == 3 ? ph::Rect{-w / 2, -len, w / 2, 0} : ph::Rect{-w / 2, 0, w / 2, len}});
      }
      hop_pins[c.net].push_back(pin.name);
      sub.terminals.push_back(std::move(pin));
      ++hops;
    }
    // the nets: every routed signal net with two or more endpoints inside (cells, terminals, hop pins)
    for (size_t n = 0; n < d.nets.size(); ++n) {
      const auto& N = d.nets[n];
      if (!N.routed || N.clock || N.power) {
        continue;
      }
      ph::Net net;
      net.name = fp.nets[N.frame].name;
      net.use  = fp.nets[N.frame].use;
      for (const auto& p : N.pins) {
        if (p.comp >= 0 && included[size_t(p.comp)]) {
          net.connections.push_back({fp.components[size_t(p.comp)].name, std::string(p.name)});
        } else if (p.term >= 0 && term_in[size_t(p.term)]) {
          net.connections.push_back({"PIN", fp.terminals[size_t(p.term)].name});
        }
      }
      if (const auto it = hop_pins.find(int(n)); it != hop_pins.end()) {
        for (const auto& name : it->second) {
          net.connections.push_back({"PIN", name});
        }
      }
      if (net.connections.size() >= 2) {
        sub.nets.push_back(std::move(net));
      }
    }
    // the power grid clipped to the die: each straight run's part inside, the vias inside
    const auto& power = regions.power_nets.empty() ? fp.special_nets : regions.power_nets;
    size_t      pg_paths = 0;
    for (const auto& pn : power) {
      ph::Net sn;
      sn.name = pn.name;
      sn.use  = pn.use;
      std::map<std::string, size_t> comp_index;
      for (size_t i = 0; i < fp.components.size(); ++i) {
        if (included[i]) {
          comp_index.emplace(fp.components[i].name, i);
        }
      }
      for (const auto& c : pn.connections) {  // the chip's supply connections: the region's cells, pins and wildcards
        if (c.instance == "PIN" || c.instance == "*" || comp_index.contains(c.instance)) {
          sn.connections.push_back(c);
        }
      }
      ph::Wire wire;
      for (const auto& w : pn.wires) {
        wire.type = w.type;
        for (const auto& path : w.paths) {
          std::optional<ph::Route_point> cur;
          for (const auto& step : path.steps) {
            if (const auto* pt = std::get_if<ph::Route_point>(&step)) {
              if (cur) {
                ph::Route_path piece;
                piece.layer = path.layer;
                piece.width = path.width;
                piece.shape = path.shape;
                if (cur->y == pt->y && cur->y >= sub.die.yl && cur->y < sub.die.yh) {
                  const auto xa = std::max<int64_t>(std::min(cur->x, pt->x), int64_t(sub.die.xl));
                  const auto xb = std::min<int64_t>(std::max(cur->x, pt->x), int64_t(sub.die.xh));
                  if (xa < xb) {
                    piece.steps = {ph::Route_point{xa, cur->y}, ph::Route_point{xb, cur->y}};
                  }
                } else if (cur->x == pt->x && cur->x >= sub.die.xl && cur->x < sub.die.xh) {
                  const auto ya = std::max<int64_t>(std::min(cur->y, pt->y), int64_t(sub.die.yl));
                  const auto yb = std::min<int64_t>(std::max(cur->y, pt->y), int64_t(sub.die.yh));
                  if (ya < yb) {
                    piece.steps = {ph::Route_point{cur->x, ya}, ph::Route_point{cur->x, yb}};
                  }
                }
                if (!piece.steps.empty()) {
                  wire.paths.push_back(std::move(piece));
                  ++pg_paths;
                }
              }
              cur = ph::Route_point{pt->x, pt->y};
            } else if (const auto* via = std::get_if<ph::Route_via>(&step)) {
              if (cur && cur->x >= sub.die.xl && cur->x < sub.die.xh && cur->y >= sub.die.yl && cur->y < sub.die.yh) {
                ph::Route_path piece;
                piece.layer = path.layer;
                piece.width = path.width;
                piece.shape = path.shape;
                piece.steps = {*cur, *via};
                wire.paths.push_back(std::move(piece));
                ++pg_paths;
              }
            }
          }
        }
      }
      if (!wire.paths.empty()) {
        sn.wires.push_back(std::move(wire));
      }
      if (!sn.wires.empty()) {
        sub.special_nets.push_back(std::move(sn));
      }
    }
    const auto path = dir + "/region_" + std::to_string(r);
    ph::write_def(sub, path + ".def");
    std::ofstream js(path + ".json");
    const int     status = size_t(r) < rep.region_stats.size() ? rep.region_stats[size_t(r)].status : -1;
    js << std::format(R"({{"region":{},"status":{},"box_um":[{:.3f},{:.3f},{:.3f},{:.3f}],"die_um":[{:.3f},{:.3f},{:.3f},{:.3f}],)"
                      R"("tiles":{},"cells":{},"fixed_cells":{},"macros":{},"terminals":{},"hops":{},"nets":{},"power_paths":{}}})"
                      "\n",
                      r,
                      status,
                      box.xl / double(fp.dbu),
                      box.yl / double(fp.dbu),
                      box.xh / double(fp.dbu),
                      box.yh / double(fp.dbu),
                      sub.die.xl / double(fp.dbu),
                      sub.die.yl / double(fp.dbu),
                      sub.die.xh / double(fp.dbu),
                      sub.die.yh / double(fp.dbu),
                      g.region_tiles[size_t(r)].size(),
                      cells,
                      fixed_cells,
                      macros,
                      sub.terminals.size() - hops,
                      hops,
                      sub.nets.size(),
                      pg_paths);
    std::fprintf(stderr,
                 "legroute dump region %d: status %d, %zu cells, %zu fixed, %zu macros, %zu hops (%zu without a track), %zu nets -> "
                 "%s.def\n",
                 r,
                 status,
                 cells,
                 fixed_cells,
                 macros,
                 hops,
                 hops_dropped,
                 sub.nets.size(),
                 path.c_str());
  }
}
}  // namespace livehd::placement::lr

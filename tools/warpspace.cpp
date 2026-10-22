// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
// Experimental geometry-only DEF mesh warp. No synthesis/placement defaults change.
#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <numeric>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "Highs.h"
#include "rapidjson/document.h"
#include "rapidjson/prettywriter.h"
#include "rapidjson/stringbuffer.h"
#include "zlib.h"

namespace warp {
using Clock = std::chrono::steady_clock;
struct Point {
  double x = 0, y = 0;
  Point  operator+(Point p) const { return {x + p.x, y + p.y}; }
  Point  operator-(Point p) const { return {x - p.x, y - p.y}; }
  Point  operator*(double s) const { return {x * s, y * s}; }
};
double cross(Point a, Point b) { return a.x * b.y - a.y * b.x; }
double length(Point p) { return std::hypot(p.x, p.y); }
void   require(bool condition, const std::string& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}
std::string read(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  require(bool(in), "cannot read " + path);
  return {std::istreambuf_iterator<char>(in), {}};
}
void write(const std::string& path, const std::string& text) {
  std::ofstream out(path, std::ios::binary);
  require(bool(out), "cannot write " + path);
  out << text;
  require(bool(out), "write failed: " + path);
}
struct Token {
  std::string text;
  size_t      begin = 0, end = 0;
};
// Token spans let the writer preserve every byte except numeric placement fields.
std::vector<Token> lex(const std::string& s) {
  std::vector<Token> out;
  for (size_t i = 0; i < s.size();) {
    if (std::isspace(static_cast<unsigned char>(s[i]))) {
      ++i;
      continue;
    }
    if (s[i] == '#') {
      while (i < s.size() && s[i] != '\n') {
        ++i;
      }
      continue;
    }
    size_t start = i;
    if (s[i] == '"') {
      ++i;
      while (i < s.size()) {
        if (s[i] == '\\' && i + 1 < s.size()) {
          i += 2;
          continue;
        }
        if (s[i++] == '"') {
          break;
        }
      }
      require(i > start + 1 && s[i - 1] == '"', "unterminated quoted string");
    } else if (s[i] == '(' || s[i] == ')' || s[i] == ';' || s[i] == '+') {
      ++i;
    } else {
      while (i < s.size() && !std::isspace(static_cast<unsigned char>(s[i]))) {
        if (s[i] == '\\' && i + 1 < s.size()) {
          i += 2;
          continue;
        }
        if (s[i] == '(' || s[i] == ')' || s[i] == ';' || s[i] == '+' || s[i] == '#') {
          break;
        }
        ++i;
      }
    }
    require(i > start, "invalid input token");
    out.push_back({s.substr(start, i - start), start, i});
  }
  return out;
}
double number(const std::string& s) {
  size_t used = 0;
  double n    = std::stod(s, &used);
  require(used == s.size() && std::isfinite(n), "invalid number: " + s);
  return n;
}
struct Master {
  double w = 0, h = 0;
  bool   block = false;
  Point  origin;
};
struct Tech {
  std::map<std::string, Master> masters;
  std::map<std::string, double> sites;
};
Tech read_lefs(const std::vector<std::string>& files) {
  Tech tech;
  for (const auto& file : files) {
    auto t = lex(read(file));
    for (size_t i = 0; i + 1 < t.size(); ++i) {
      bool macro = t[i].text == "MACRO", site = t[i].text == "SITE";
      if (!macro && !site) {
        continue;
      }
      // SITE references inside MACROs are skipped by consuming the whole MACRO.
      std::string name = t[++i].text;
      Master      m;
      bool        ended = false;
      while (++i + 1 < t.size()) {
        if (t[i].text == "END" && t[i + 1].text == name) {
          ++i;
          ended = true;
          break;
        }
        if (t[i].text == "SIZE" && i + 3 < t.size() && t[i + 2].text == "BY") {
          m.w = number(t[i + 1].text);
          m.h = number(t[i + 3].text);
        }
        if (t[i].text == "CLASS" && i + 1 < t.size()) {
          m.block = t[i + 1].text == "BLOCK" || t[i + 1].text == "PAD";
        }
        if (t[i].text == "ORIGIN" && i + 2 < t.size()) {
          m.origin = {number(t[i + 1].text), number(t[i + 2].text)};
        }
      }
      require(ended && m.w > 0 && m.h > 0, "LEF needs positive SIZE for " + name);
      if (macro) {
        tech.masters[name] = m;
      } else {
        tech.sites[name] = m.h;
      }
    }
  }
  require(!tech.masters.empty(), "no LEF MACRO geometry found");
  return tech;
}
struct Inst {
  std::string           name, master, orient;
  Point                 p, original;
  double                w = 0, h = 0;
  bool                  macro = false, fixed = false, standard = false, cover = false;
  size_t                xtoken = 0, ytoken = 0;
  int                   face = -1;
  std::array<double, 4> weights{};
};
struct Layout {
  std::string                      source;
  std::vector<Token>               tokens;
  std::vector<Inst>                insts;
  std::vector<std::vector<size_t>> nets;
  double                           dbu = 0, row_height = 0;
  Point                            lo, hi;
  double                           width() const { return hi.x - lo.x; }
  double                           height() const { return hi.y - lo.y; }
  Point                            normalize(Point p) const { return {(p.x - lo.x) / width(), (p.y - lo.y) / height()}; }
  Point                            physical(Point p) const { return {lo.x + p.x * width(), lo.y + p.y * height()}; }
};
Layout read_def(const std::string& path, const Tech& tech, double macro_factor, bool geometry_only = false) {
  Layout l;
  l.source                        = read(path);
  l.tokens                        = lex(l.source);
  const auto&                 t   = l.tokens;
  bool                        die = false, components = false;
  std::vector<std::string>    row_sites;
  const std::set<std::string> other_sections = {"PINS",
                                                "NETS",
                                                "SPECIALNETS",
                                                "VIAS",
                                                "NONDEFAULTRULES",
                                                "PROPERTYDEFINITIONS",
                                                "BLOCKAGES",
                                                "REGIONS",
                                                "GROUPS",
                                                "SCANCHAINS",
                                                "FILLS",
                                                "SLOTS",
                                                "STYLES",
                                                "PINPROPERTIES"};
  for (size_t i = 0; i < t.size(); ++i) {
    if (other_sections.contains(t[i].text) && (!i || t[i - 1].text != "END")) {
      const auto section = t[i].text;
      if (section == "BLOCKAGES" || section == "REGIONS") {
        require(i + 1 < t.size() && number(t[i + 1].text) == 0, "nonempty DEF " + section + " is unsupported by this prototype");
      }
      size_t end = i + 1;
      while (end + 1 < t.size() && !(t[end].text == "END" && t[end + 1].text == section)) {
        ++end;
      }
      require(end + 1 < t.size(), "unterminated DEF section: " + section);
      i = end + 1;
      continue;
    }
    if (t[i].text == "UNITS" && i + 3 < t.size() && t[i + 1].text == "DISTANCE" && t[i + 2].text == "MICRONS") {
      l.dbu = number(t[i + 3].text);
    }
    if (t[i].text == "DIEAREA") {
      size_t end = i + 1;
      while (end < t.size() && t[end].text != ";") {
        ++end;
      }
      require(end == i + 9 && t[i + 1].text == "(" && t[i + 4].text == ")" && t[i + 5].text == "(" && t[i + 8].text == ")",
              "only rectangular DIEAREA is supported");
      l.lo = {number(t[i + 2].text), number(t[i + 3].text)};
      l.hi = {number(t[i + 6].text), number(t[i + 7].text)};
      die  = true;
    }
    if (t[i].text == "ROW" && i + 2 < t.size()) {
      row_sites.push_back(t[i + 2].text);
    }
    if ((t[i].text == "BLOCKAGES" || t[i].text == "REGIONS") && (!i || t[i - 1].text != "END")) {
      require(i + 1 < t.size() && number(t[i + 1].text) == 0, "nonempty DEF " + t[i].text + " is unsupported by this prototype");
    }
    if (t[i].text != "COMPONENTS" || (i && t[i - 1].text == "END")) {
      continue;
    }
    require(!components, "duplicate COMPONENTS section");
    components       = true;
    size_t expected  = static_cast<size_t>(number(t.at(i + 1).text));
    i               += 2;
    require(t.at(i).text == ";", "malformed COMPONENTS header");
    while (++i < t.size() && t[i].text != "END") {
      require(t[i].text == "-" && i + 2 < t.size(), "malformed COMPONENTS entry");
      Inst c;
      c.name      = t[++i].text;
      c.master    = t[++i].text;
      bool placed = false;
      while (++i < t.size() && t[i].text != ";") {
        if (t[i].text != "+" || i + 1 >= t.size()) {
          continue;
        }
        const auto& status = t[i + 1].text;
        if (status != "PLACED" && status != "FIXED" && status != "COVER" && status != "UNPLACED") {
          continue;
        }
        require(!placed && status != "UNPLACED", "component must have one placed position: " + c.name);
        require(i + 7 < t.size() && t[i + 2].text == "(" && t[i + 5].text == ")", "invalid component placement: " + c.name);
        c.xtoken = i + 3;
        c.ytoken = i + 4;
        c.p      = {number(t[c.xtoken].text), number(t[c.ytoken].text)};
        c.orient = t[i + 6].text;
        require(std::set<std::string>{"N", "S", "E", "W", "FN", "FS", "FE", "FW"}.contains(c.orient),
                "unsupported orientation: " + c.orient);
        c.fixed = status != "PLACED";
        c.cover = status == "COVER";
        placed  = true;
      }
      require(placed, "unplaced component: " + c.name);
      l.insts.push_back(std::move(c));
    }
    require(i + 1 < t.size() && t[i + 1].text == "COMPONENTS" && l.insts.size() == expected, "COMPONENTS count/end mismatch");
    ++i;
  }
  require(die && components && l.dbu > 0 && l.width() > 0 && l.height() > 0, "DEF needs UNITS, rectangular DIEAREA and COMPONENTS");
  l.lo = l.lo * (1 / l.dbu);
  l.hi = l.hi * (1 / l.dbu);
  for (const auto& site : row_sites) {
    require(tech.sites.contains(site), "missing LEF SITE: " + site);
    double h     = tech.sites.at(site);
    l.row_height = l.row_height == 0 ? h : std::min(l.row_height, h);
  }
  if (l.row_height == 0) {
    for (const auto& [name, m] : tech.masters) {
      if (!m.block) {
        l.row_height = l.row_height == 0 ? m.h : std::min(l.row_height, m.h);
      }
    }
  }
  require(l.row_height > 0, "cannot infer row height; provide LEF SITE/CORE geometry");
  std::unordered_map<std::string, size_t> index;
  for (size_t i = 0; i < l.insts.size(); ++i) {
    auto& c = l.insts[i];
    require(index.emplace(c.name, i).second, "duplicate instance: " + c.name);
    require(tech.masters.contains(c.master), "missing LEF MACRO: " + c.master);
    const auto& m = tech.masters.at(c.master);
    require(m.origin.x == 0 && m.origin.y == 0, "nonzero LEF ORIGIN unsupported: " + c.master);
    c.w = m.w;
    c.h = m.h;
    if (c.orient == "E" || c.orient == "W" || c.orient == "FE" || c.orient == "FW") {
      std::swap(c.w, c.h);
    }
    c.standard = !m.block && c.h <= macro_factor * l.row_height;
    c.macro    = !c.standard || c.fixed;  // fixed standard cells are rigid obstacles
    c.p        = c.p * (1 / l.dbu);
    c.original = c.p;
    double e   = 1e-8 * std::max(l.width(), l.height());
    require(c.p.x >= l.lo.x - e && c.p.y >= l.lo.y - e && c.p.x + c.w <= l.hi.x + e && c.p.y + c.h <= l.hi.y + e,
            "component outside DIEAREA: " + c.name);
  }
  if (geometry_only) {
    require(!l.insts.empty(), "empty placement");
    l.source.clear();
    l.source.shrink_to_fit();
    l.tokens.clear();
    l.tokens.shrink_to_fit();
    return l;
  }
  // Only connectivity is used. Preserve routing, properties and IO sections verbatim.
  for (size_t i = 0; i < t.size(); ++i) {
    if (t[i].text != "NETS" || i + 2 >= t.size() || t[i + 2].text != ";" || (i && t[i - 1].text == "END")) {
      continue;
    }
    i += 2;
    require(t.at(i).text == ";", "malformed NETS header");
    while (++i < t.size() && t[i].text != "END") {
      require(t[i].text == "-", "malformed NETS entry");
      ++i;
      std::vector<size_t> net;
      bool                attributes = false;
      while (++i < t.size() && t[i].text != ";") {
        if (t[i].text == "+") {
          attributes = true;
        }
        if (!attributes && t[i].text == "(" && i + 3 < t.size() && t[i + 3].text == ")") {
          if (t[i + 1].text != "PIN") {
            require(index.contains(t[i + 1].text), "unknown NETS instance: " + t[i + 1].text);
            net.push_back(index.at(t[i + 1].text));
          }
          i += 3;
        }
      }
      std::sort(net.begin(), net.end());
      net.erase(std::unique(net.begin(), net.end()), net.end());
      if (net.size() > 1) {
        l.nets.push_back(std::move(net));
      }
    }
    break;
  }
  require(!l.insts.empty(), "empty placement");
  return l;
}
void write_def(const Layout& l, const std::string& path) {
  std::map<size_t, std::string> replace;
  for (const auto& c : l.insts) {
    if (!c.fixed) {
      replace[c.xtoken] = std::to_string(std::llround(c.p.x * l.dbu));
      replace[c.ytoken] = std::to_string(std::llround(c.p.y * l.dbu));
    }
  }
  std::ofstream out(path, std::ios::binary);
  require(bool(out), "cannot write " + path);
  size_t cursor = 0;
  for (const auto& [i, value] : replace) {
    const auto& token = l.tokens[i];
    out.write(l.source.data() + cursor, static_cast<std::streamsize>(token.begin - cursor));
    out << value;
    cursor = token.end;
  }
  out.write(l.source.data() + cursor, static_cast<std::streamsize>(l.source.size() - cursor));
  require(bool(out), "write failed: " + path);
}

struct Options {
  std::vector<std::string> lefs;
  std::string              input, output, report = "warp_report.png", metrics;
  int                      bins = 64, iters = 6, pixels = 1600;
  double                   rho = .85, mu = 1, lambda = -1, tol = .02, macro_factor = 2, time_limit = 30;
  double                   balance       = 0;
  std::string              balance_model = "area";
  std::string              replay;
  std::string              density_input;
  bool                     macro_space   = false;
  int                      smooth_window = 32;
  double                   smooth_blend  = .5;
  std::string              label;
  bool                     density_grid        = false;
  std::string              preview_grid        = "macro-edges";
  std::string              density_scale       = "auto";
  bool                     deformation_preview = false;
  bool                     render_only = false, require_cap = false, movable_macros = false;
};
struct Face {
  std::array<int, 4> v;
  int                macro  = -1;
  double             demand = 0, initial_area = 0, reserved = 0, target = -1;
};
struct Mesh {
  bool                             density_only = false;
  int                              nx = 0, ny = 0, reassigned = 0;
  std::vector<double>              xs, ys;
  std::vector<Point>               original, points;
  std::vector<Face>                faces;
  std::vector<std::pair<int, int>> edges;
  std::vector<size_t>              macros;
  std::vector<std::vector<int>>    macro_vertices;
  int                              vertex(int i, int j) const { return j * nx + i; }
  double area(const Face& f) const { return .5 * cross(points[f.v[2]] - points[f.v[0]], points[f.v[3]] - points[f.v[1]]); }
};
std::vector<double> grid_lines(int bins, std::vector<double> macro) {
  macro.push_back(0);
  macro.push_back(1);
  std::sort(macro.begin(), macro.end());
  macro.erase(std::unique(macro.begin(), macro.end()), macro.end());
  auto all = macro;
  for (int i = 1; i < bins; ++i) {
    double x        = double(i) / bins;
    auto   it       = std::lower_bound(macro.begin(), macro.end(), x);
    double distance = it == macro.end() ? 1 : std::abs(*it - x);
    if (it != macro.begin()) {
      distance = std::min(distance, std::abs(*std::prev(it) - x));
    }
    if (distance > .25 / bins) {
      all.push_back(x);
    }
  }
  std::sort(all.begin(), all.end());
  return all;
}
Mesh make_mesh(Layout& l, const Options& o) {
  Mesh m;
  m.density_only = o.deformation_preview;
  std::vector<double> xs, ys;
  for (size_t i = 0; i < l.insts.size(); ++i) {
    if (l.insts[i].macro) {
      const auto& c  = l.insts[i];
      Point       lo = l.normalize(c.p), hi = l.normalize(c.p + Point{c.w, c.h});
      if (!(o.deformation_preview && o.preview_grid == "uniform" && !c.standard)) {
        xs.insert(xs.end(), {lo.x, hi.x});
        ys.insert(ys.end(), {lo.y, hi.y});
      }
      m.macros.push_back(i);
    }
  }
  // Macro count is small: this also catches touching-independent interiors accurately.
  for (size_t i = 0; i < m.macros.size(); ++i) {
    for (size_t j = 0; j < i; ++j) {
      const auto& a = l.insts[m.macros[i]];
      const auto& b = l.insts[m.macros[j]];
      require(std::min(a.p.x + a.w, b.p.x + b.w) <= std::max(a.p.x, b.p.x)
                  || std::min(a.p.y + a.h, b.p.y + b.h) <= std::max(a.p.y, b.p.y),
              "overlapping rigid objects: " + a.name + " and " + b.name);
    }
  }
  m.xs = grid_lines(o.bins, xs);
  m.ys = grid_lines(o.bins, ys);
  m.nx = static_cast<int>(m.xs.size());
  m.ny = static_cast<int>(m.ys.size());
  require(int64_t(m.nx) * m.ny <= 100000, "mesh exceeds 100000 vertices; reduce --bins or macro count");
  for (int j = 0; j < m.ny; ++j) {
    for (int i = 0; i < m.nx; ++i) {
      m.original.push_back({m.xs[i], m.ys[j]});
      if (i + 1 < m.nx) {
        m.edges.emplace_back(m.vertex(i, j), m.vertex(i + 1, j));
      }
      if (j + 1 < m.ny) {
        m.edges.emplace_back(m.vertex(i, j), m.vertex(i, j + 1));
      }
    }
  }
  m.points = m.original;
  for (int j = 0; j + 1 < m.ny; ++j) {
    for (int i = 0; i + 1 < m.nx; ++i) {
      Face f{
          {m.vertex(i, j), m.vertex(i + 1, j), m.vertex(i + 1, j + 1), m.vertex(i, j + 1)}
      };
      f.initial_area = (m.xs[i + 1] - m.xs[i]) * (m.ys[j + 1] - m.ys[j]);
      Point center{.5 * (m.xs[i] + m.xs[i + 1]), .5 * (m.ys[j] + m.ys[j + 1])};
      for (size_t k = 0; k < m.macros.size(); ++k) {
        const auto& c  = l.insts[m.macros[k]];
        auto        lo = l.normalize(c.p), hi = l.normalize(c.p + Point{c.w, c.h});
        if ((!m.density_only || c.standard) && center.x > lo.x && center.x < hi.x && center.y > lo.y && center.y < hi.y) {
          f.macro = static_cast<int>(k);
        }
      }
      m.faces.push_back(f);
    }
  }
  for (size_t k : m.macros) {
    const auto&      c  = l.insts[k];
    auto             lo = l.normalize(c.p), hi = l.normalize(c.p + Point{c.w, c.h});
    std::vector<int> vertices;
    for (size_t v = 0; v < m.original.size(); ++v) {
      auto p = m.original[v];
      if (p.x >= lo.x && p.x <= hi.x && p.y >= lo.y && p.y <= hi.y) {
        vertices.push_back(static_cast<int>(v));
      }
    }
    require(!vertices.empty() || (m.density_only && !c.standard), "empty macro vertex set");
    m.macro_vertices.push_back(std::move(vertices));
  }
  double cell_area = 0, macro_area = 0, all_cell_area = 0, true_macro_area = 0;
  for (auto& c : l.insts) {
    if (c.standard) {
      all_cell_area += c.w * c.h;
    } else {
      true_macro_area += c.w * c.h;
    }
    if (c.macro) {
      if (!m.density_only || c.standard) {
        macro_area += c.w * c.h;
      }
      continue;
    }
    cell_area += c.w * c.h;
    auto p     = l.normalize(c.p + Point{c.w / 2, c.h / 2});
    int  i     = std::clamp(int(std::upper_bound(m.xs.begin(), m.xs.end(), p.x) - m.xs.begin()) - 1, 0, m.nx - 2);
    int  j     = std::clamp(int(std::upper_bound(m.ys.begin(), m.ys.end(), p.y) - m.ys.begin()) - 1, 0, m.ny - 2);
    c.face     = j * (m.nx - 1) + i;
    if (m.faces[c.face].macro >= 0) {
      double best = std::numeric_limits<double>::infinity();
      int    face = -1;
      for (size_t f = 0; f < m.faces.size(); ++f) {
        if (m.faces[f].macro < 0) {
          auto   a = m.original[m.faces[f].v[0]], b = m.original[m.faces[f].v[2]];
          double dx = (p.x - std::clamp(p.x, a.x, b.x)) * l.width(), dy = (p.y - std::clamp(p.y, a.y, b.y)) * l.height();
          double distance = dx * dx + dy * dy;
          if (distance < best) {
            best = distance;
            face = static_cast<int>(f);
          }
        }
      }
      require(face >= 0, "no free mesh face for a cell inside a macro");
      c.face = face;
      ++m.reassigned;
    }
    const auto& f = m.faces[c.face];
    auto        a = m.original[f.v[0]], b = m.original[f.v[2]];
    double      u = std::clamp((p.x - a.x) / (b.x - a.x), 0., 1.), v = std::clamp((p.y - a.y) / (b.y - a.y), 0., 1.);
    c.weights               = {(1 - u) * (1 - v), u * (1 - v), u * v, (1 - u) * v};
    m.faces[c.face].demand += c.w * c.h / (l.width() * l.height());
  }
  double free            = l.width() * l.height() - macro_area;
  double evaluation_free = l.width() * l.height() - (m.density_only ? 0 : true_macro_area);
  require(evaluation_free > 0 && all_cell_area / evaluation_free <= o.rho * (1 + 1e-9),
          "globally infeasible density cap including fixed standard cells");
  require(free > 0 && cell_area / free <= o.rho * (1 + 1e-9),
          "globally infeasible density cap: movable cell area / free area = " + std::to_string(cell_area / std::max(free, 1e-30)));
  return m;
}
void invariants(const Mesh& m, const Layout& l) {
  double sum = 0;
  for (const auto& f : m.faces) {
    double area = m.area(f);
    require(area > 0, "nonpositive mesh face area");
    require(area + 1e-8 >= f.reserved, "reserved macro area compressed away");
    sum += area;
    for (int k = 0; k < 4; ++k) {
      Point a = m.points[f.v[k]], b = m.points[f.v[(k + 1) % 4]], c = m.points[f.v[(k + 2) % 4]];
      require(cross(b - a, c - b) > 0, "folded/nonconvex mesh face");
    }
  }
  require(std::abs(sum - 1) <= 1e-6, "mesh does not conserve die area");
  for (size_t k = 0; k < m.macros.size(); ++k) {
    if (m.density_only && !l.insts[m.macros[k]].standard) {
      continue;
    }
    int   first = m.macro_vertices[k].front();
    Point delta = m.points[first] - m.original[first];
    for (int v : m.macro_vertices[k]) {
      require(length((m.points[v] - m.original[v]) - delta) < 1e-7, "macro rigidity violated");
    }
    if (l.insts[m.macros[k]].fixed) {
      require(length(delta) < 1e-9, "fixed object moved");
    }
  }
}

struct Pass {
  std::string axis, status;
  double      slack = 0, seconds = 0;
  int         slack_faces = 0;
};
struct Lp {
  HighsLp model;
  Lp() {
    model.a_matrix_.format_ = MatrixFormat::kRowwise;
    model.a_matrix_.start_  = {0};
  }
  int variable(double cost, double lo = 0, double hi = kHighsInf) {
    model.col_cost_.push_back(cost);
    model.col_lower_.push_back(lo);
    model.col_upper_.push_back(hi);
    return model.num_col_++;
  }
  void row(std::vector<std::pair<int, double>> coefficients, double lo, double hi = kHighsInf) {
    // Sparse rows must not contain duplicate columns (degenerate expressions can).
    std::sort(coefficients.begin(), coefficients.end());
    for (size_t i = 0; i < coefficients.size();) {
      auto [column, value] = coefficients[i++];
      while (i < coefficients.size() && coefficients[i].first == column) {
        value += coefficients[i++].second;
      }
      if (std::abs(value) > 1e-14) {
        model.a_matrix_.index_.push_back(column);
        model.a_matrix_.value_.push_back(value);
      }
    }
    model.a_matrix_.start_.push_back(static_cast<HighsInt>(model.a_matrix_.index_.size()));
    model.row_lower_.push_back(lo);
    model.row_upper_.push_back(hi);
    ++model.num_row_;
  }
};
double coordinate(Point p, int axis) { return axis == 0 ? p.x : p.y; }
Pass   solve_axis(Mesh& m, const Layout& l, const Options& o, int axis) {
  auto         start = Clock::now();
  Lp           lp;
  const double scale     = axis == 0 ? l.width() : l.height();
  double       lambda    = o.lambda < 0 ? 1000. * o.bins * o.bins : o.lambda * l.width() * l.height();
  double       free_area = 0, demand = 0;
  for (const auto& f : m.faces) {
    if (f.macro < 0) {
      free_area += f.initial_area;
      demand    += f.demand;
    }
  }
  const double balance_cost = free_area > 0 ? o.balance * scale * m.points.size() / free_area : 0;
  if (o.lambda < 0) {
    // Keep capacity dominant when the optional balancing objective is strong.
    lambda = std::max(lambda, 100 * balance_cost);
  }
  for (size_t v = 0; v < m.points.size(); ++v) {
    double original = coordinate(m.original[v], axis);
    bool   boundary = original == 0 || original == 1;
    lp.variable(0, boundary ? 0 : -original, boundary ? 0 : 1 - original);
  }
  std::vector<int> slack(m.faces.size(), -1);
  for (size_t f = 0; f < m.faces.size(); ++f) {
    if (m.faces[f].macro < 0) {
      slack[f] = lp.variable(lambda);
    }
  }
  for (size_t v = 0; v < m.points.size(); ++v) {
    int a = lp.variable(scale);
    lp.row(
        {
            {                  a,  1},
            {static_cast<int>(v), -1}
    },
        0);
    lp.row(
        {
            {                  a, 1},
            {static_cast<int>(v), 1}
    },
        0);
  }
  for (auto [a, b] : m.edges) {
    int t = lp.variable(o.mu * scale);
    lp.row(
        {
            {t,  1},
            {a, -1},
            {b,  1}
    },
        0);
    lp.row(
        {
            {t,  1},
            {a,  1},
            {b, -1}
    },
        0);
    auto   delta = m.original[b] - m.original[a];
    double gap   = coordinate(delta, axis);
    if (gap > 0) {
      // Never make an originally thin, exact macro-edge face infeasible.
      double minimum = std::min(.01 / o.bins, .1 * gap);
      lp.row(
          {
              {b,  1},
              {a, -1}
      },
          minimum - gap);
    }
  }
  for (size_t f = 0; f < m.faces.size(); ++f) {
    const auto&                         face = m.faces[f];
    std::vector<std::pair<int, double>> coefficients;
    double                              constant = 0;
    for (int k = 0; k < 4; ++k) {
      double next = coordinate(m.points[face.v[(k + 1) % 4]], 1 - axis), prev = coordinate(m.points[face.v[(k + 3) % 4]], 1 - axis);
      double c = .5 * (axis == 0 ? next - prev : prev - next);
      coefficients.emplace_back(face.v[k], c);
      constant += c * coordinate(m.original[face.v[k]], axis);
    }
    if (slack[f] >= 0) {
      if (o.balance > 0 && demand > 0) {
        // Allocate free space to cell demand. A small original-area share keeps
        // empty corridors open; rigid macro faces never enter this objective.
        // Both signs are linear in the active axis, just like capacity.
        double target    = face.target >= 0 ? face.target : .05 * face.initial_area + .95 * free_area * face.demand / demand;
        int    deviation = lp.variable(balance_cost);
        if (o.balance_model == "strain") {
          // Convex piecewise quadratic of fractional area error. An empty
          // uncompressed corridor has more pressure than a compressed one.
          // q >= A0 * (2*t*((A-target)/A0) - t*t), with each axis still an LP.
          for (double t : {-4., -2., -1., -.5, -.25, -.125, -.0625, 0., .0625, .125, .25, .5, 1., 2., 4.}) {
            auto row = coefficients;
            for (auto& term : row) {
              term.second *= 2 * t;
            }
            row.emplace_back(deviation, -1);
            lp.row(std::move(row), -kHighsInf, 2 * t * (target - constant) + t * t * face.initial_area);
          }
        } else {
          auto positive = coefficients, negative = coefficients;
          for (auto& term : negative) {
            term.second = -term.second;
          }
          positive.emplace_back(deviation, -1);
          negative.emplace_back(deviation, -1);
          lp.row(std::move(positive), -kHighsInf, target - constant);
          lp.row(std::move(negative), -kHighsInf, constant - target);
        }
      }
      if (face.reserved > 0) {
        lp.row(coefficients, face.reserved - constant);
      }
      coefficients.emplace_back(slack[f], 1);
      lp.row(std::move(coefficients), face.reserved + face.demand / o.rho - constant);
    }
    for (int k = 0; k < 4; ++k) {
      int    a = face.v[k], b = face.v[(k + 1) % 4], c = face.v[(k + 2) % 4];
      double aa = coordinate(m.points[a], 1 - axis), bb = coordinate(m.points[b], 1 - axis), cc = coordinate(m.points[c], 1 - axis);
      std::array<double, 3> values
          = axis == 0 ? std::array<double, 3>{bb - cc, cc - aa, aa - bb} : std::array<double, 3>{cc - bb, aa - cc, bb - aa};
      double base = values[0] * coordinate(m.original[a], axis) + values[1] * coordinate(m.original[b], axis)
                    + values[2] * coordinate(m.original[c], axis);
      lp.row(
          {
              {a, values[0]},
              {b, values[1]},
              {c, values[2]}
      },
          face.initial_area * 1e-4 - base);
    }
  }
  for (size_t k = 0; k < m.macros.size(); ++k) {
    const auto& macro = l.insts[m.macros[k]];
    if (m.density_only && !macro.standard) {
      continue;
    }
    int t = lp.variable(0, macro.fixed ? 0 : -1, macro.fixed ? 0 : 1);
    for (int v : m.macro_vertices[k]) {
      lp.row(
          {
              {v,  1},
              {t, -1}
      },
          0,
          0);
    }
  }
  // Exact bounds on the bilinearly mapped center keep rigid cell rectangles in
  // the die. A valid center map alone does not guarantee this near the outline.
  for (const auto& c : l.insts) {
    if (!c.macro) {
      const auto&                         f = m.faces[c.face];
      std::vector<std::pair<int, double>> coefficients;
      double                              base = 0;
      for (int k = 0; k < 4; ++k) {
        coefficients.emplace_back(f.v[k], c.weights[k]);
        base += c.weights[k] * coordinate(m.original[f.v[k]], axis);
      }
      double half = (axis == 0 ? c.w / l.width() : c.h / l.height()) / 2;
      lp.row(std::move(coefficients), half - base, 1 - half - base);
    }
  }
  Highs highs;
  highs.setOptionValue("output_flag", false);
  highs.setOptionValue("threads", 1);
  highs.setOptionValue("solver", "ipm");
  highs.setOptionValue("run_crossover", "off");
  highs.setOptionValue("time_limit", o.time_limit);
  highs.setOptionValue("small_matrix_value", 1e-12);
  highs.setOptionValue("primal_feasibility_tolerance", 1e-9);
  highs.setOptionValue("ipm_optimality_tolerance", 1e-10);
  require(highs.passModel(lp.model) != HighsStatus::kError, "HiGHS rejected LP");
  highs.run();
  auto status           = highs.getModelStatus();
  bool simplex_fallback = false;
  if (status == HighsModelStatus::kUnknown || status == HighsModelStatus::kSolveError) {
    double remaining = o.time_limit - std::chrono::duration<double>(Clock::now() - start).count();
    if (remaining > 0) {
      std::cout << "IPM returned " << highs.modelStatusToString(status) << "; retrying the same LP with simplex\n" << std::flush;
      highs.clearSolver();
      highs.setOptionValue("solver", "simplex");
      highs.setOptionValue("time_limit", remaining);
      highs.run();
      status           = highs.getModelStatus();
      simplex_fallback = true;
    }
  }
  const auto& solution = highs.getSolution();
  require(status == HighsModelStatus::kOptimal && solution.value_valid,
          "LP did not reach an optimum: " + highs.modelStatusToString(status) + "; try a smaller --bins or larger --time-limit");
  auto previous = m.points;
  for (size_t v = 0; v < m.points.size(); ++v) {
    double value = coordinate(m.original[v], axis) + solution.col_value[v];
    if (axis == 0) {
      m.points[v].x = value;
    } else {
      m.points[v].y = value;
    }
  }
  try {
    invariants(m, l);
  } catch (...) {
    m.points = std::move(previous);
    throw;
  }
  Pass report{axis == 0 ? "x" : "y", highs.modelStatusToString(status)};
  if (simplex_fallback) {
    report.status += " (simplex fallback)";
  }
  for (size_t f = 0; f < m.faces.size(); ++f) {
    if (slack[f] >= 0) {
      double s      = std::max(0., m.faces[f].reserved + m.faces[f].demand / o.rho - m.area(m.faces[f]));
      report.slack += s * l.width() * l.height();
      if (s > 1e-9 * m.faces[f].initial_area) {
        ++report.slack_faces;
      }
    }
  }
  report.seconds = std::chrono::duration<double>(Clock::now() - start).count();
  std::cout << report.axis << " LP: " << lp.model.num_col_ << " variables, " << lp.model.num_row_
            << " constraints, slack=" << report.slack << " um^2 (" << report.slack_faces << " faces), " << report.seconds << " s\n"
            << std::flush;
  return report;
}
double face_density(const Mesh& m) {
  double peak = 0;
  for (const auto& f : m.faces) {
    if (f.macro < 0) {
      peak = std::max(peak, f.demand / std::max(1e-30, m.area(f) - f.reserved));
    }
  }
  return peak;
}
void map_cells(Layout& l, const Mesh& m) {
  for (auto& c : l.insts) {
    if (c.fixed) {
      continue;
    }
    if (c.macro) {
      if (m.density_only && !c.standard) {
        continue;  // Display the warped outline, never export it as a rigid DEF macro.
      }
      auto   it    = std::find(m.macros.begin(), m.macros.end(), static_cast<size_t>(&c - l.insts.data()));
      size_t k     = static_cast<size_t>(it - m.macros.begin());
      int    v     = m.macro_vertices[k].front();
      Point  delta = m.points[v] - m.original[v];
      c.p          = c.original + Point{delta.x * l.width(), delta.y * l.height()};
    } else {
      Point center;
      for (int k = 0; k < 4; ++k) {
        center = center + m.points[m.faces[c.face].v[k]] * c.weights[k];
      }
      c.p = l.physical(center) - Point{c.w / 2, c.h / 2};
    }
    // DEF integer DBU quantization; inward rounding at the outline only.
    double xmin = std::ceil(l.lo.x * l.dbu), xmax = std::floor((l.hi.x - c.w) * l.dbu + 1e-7);
    double ymin = std::ceil(l.lo.y * l.dbu), ymax = std::floor((l.hi.y - c.h) * l.dbu + 1e-7);
    require(xmin <= xmax && ymin <= ymax, "component cannot fit in die: " + c.name);
    c.p.x = std::clamp(std::round(c.p.x * l.dbu), xmin, xmax) / l.dbu;
    c.p.y = std::clamp(std::round(c.p.y * l.dbu), ymin, ymax) / l.dbu;
  }
}
struct Metrics {
  std::vector<double> density, displacement;
  double              peak = 0, p99 = 0, overflow = 0, hpwl = 0, mean = 0, p95 = 0, maximum = 0;
};
double quantile(std::vector<double> values, double q) {
  if (values.empty()) {
    return 0;
  }
  std::sort(values.begin(), values.end());
  return values[static_cast<size_t>(q * double(values.size() - 1))];
}
Metrics measure(const Layout& l, const Options& o) {
  Metrics m;
  m.density.resize(static_cast<size_t>(o.bins) * o.bins);
  double dx = l.width() / o.bins, dy = l.height() / o.bins, area = 0;
  for (const auto& c : l.insts) {
    if (c.standard) {
      area   += c.w * c.h;
      int x0  = std::clamp(int(std::floor((c.p.x - l.lo.x) / dx)), 0, o.bins - 1),
          x1  = std::clamp(int(std::floor((c.p.x + c.w - l.lo.x) / dx)), 0, o.bins - 1);
      int y0  = std::clamp(int(std::floor((c.p.y - l.lo.y) / dy)), 0, o.bins - 1),
          y1  = std::clamp(int(std::floor((c.p.y + c.h - l.lo.y) / dy)), 0, o.bins - 1);
      for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
          double overlap_x = std::max(0., std::min(c.p.x + c.w, l.lo.x + (x + 1) * dx) - std::max(c.p.x, l.lo.x + x * dx));
          double overlap_y = std::max(0., std::min(c.p.y + c.h, l.lo.y + (y + 1) * dy) - std::max(c.p.y, l.lo.y + y * dy));
          m.density[y * o.bins + x] += overlap_x * overlap_y / (dx * dy);
        }
      }
      m.displacement.push_back(length(c.p - c.original));
    }
  }
  m.peak = *std::max_element(m.density.begin(), m.density.end());
  m.p99  = quantile(m.density, .99);
  for (double d : m.density) {
    m.overflow += std::max(0., d - o.rho) * dx * dy;
  }
  if (area > 0) {
    m.overflow /= area;
  }
  for (const auto& net : l.nets) {
    double xl = std::numeric_limits<double>::infinity(), yl = xl, xh = -xl, yh = -xl;
    for (size_t i : net) {
      const auto& c = l.insts[i];
      Point       p = c.p + Point{c.w / 2, c.h / 2};
      xl            = std::min(xl, p.x);
      xh            = std::max(xh, p.x);
      yl            = std::min(yl, p.y);
      yh            = std::max(yh, p.y);
    }
    m.hpwl += xh - xl + yh - yl;
  }
  m.mean    = m.displacement.empty()
                  ? 0
                  : std::accumulate(m.displacement.begin(), m.displacement.end(), 0.) / double(m.displacement.size());
  m.p95     = quantile(m.displacement, .95);
  m.maximum = quantile(m.displacement, 1);
  return m;
}

// Small CPU rasterizer: PNG output is available without Python, Qt or OpenROAD.
using Color = std::array<unsigned char, 3>;
struct Image {
  int                        w, h;
  std::vector<unsigned char> rgb;
  Image(int width, int height) : w(width), h(height), rgb(static_cast<size_t>(width) * height * 3, 255) {}
  void pixel(int x, int y, Color c) {
    if (x < 0 || y < 0 || x >= w || y >= h) {
      return;
    }
    size_t p = (static_cast<size_t>(y) * w + x) * 3;
    for (int k = 0; k < 3; ++k) {
      rgb[p + k] = c[k];
    }
  }
  void rect(int x0, int y0, int x1, int y1, Color c) {
    for (int y = std::max(0, y0); y < std::min(h, y1); ++y) {
      for (int x = std::max(0, x0); x < std::min(w, x1); ++x) {
        pixel(x, y, c);
      }
    }
  }
  void line(int x0, int y0, int x1, int y1, Color c) {
    int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1, dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1, error = dx + dy;
    for (;;) {
      pixel(x0, y0, c);
      if (x0 == x1 && y0 == y1) {
        break;
      }
      int twice = 2 * error;
      if (twice >= dy) {
        error += dy;
        x0    += sx;
      }
      if (twice <= dx) {
        error += dx;
        y0    += sy;
      }
    }
  }
  void arrow(int x0, int y0, int x1, int y1, Color c) {
    line(x0, y0, x1, y1, c);
    if (std::hypot(x1 - x0, y1 - y0) < 3) {
      return;
    }
    double angle = std::atan2(y1 - y0, x1 - x0);
    for (double side : {-0.5, 0.5}) {
      line(x1,
           y1,
           x1 - static_cast<int>(std::round(4 * std::cos(angle + side))),
           y1 - static_cast<int>(std::round(4 * std::sin(angle + side))),
           c);
    }
  }
  void hatch(const std::array<std::pair<int, int>, 4>& polygon, Color color, bool solid = false) {
    int xl = w, xh = 0, yl = h, yh = 0;
    for (auto [x, y] : polygon) {
      xl = std::min(xl, x);
      xh = std::max(xh, x);
      yl = std::min(yl, y);
      yh = std::max(yh, y);
    }
    xl = std::max(0, xl);
    xh = std::min(w - 1, xh);
    yl = std::max(0, yl);
    yh = std::min(h - 1, yh);
    for (int y = yl; y <= yh; ++y) {
      for (int x = solid ? xl : xl + (8 - (xl + y) % 8) % 8; x <= xh; x += solid ? 1 : 8) {
        bool positive = true, negative = true;
        for (int k = 0; k < 4; ++k) {
          auto [ax, ay] = polygon[k];
          auto [bx, by] = polygon[(k + 1) % 4];
          int64_t side  = int64_t(bx - ax) * (y - ay) - int64_t(by - ay) * (x - ax);
          positive      = positive && side >= 0;
          negative      = negative && side <= 0;
        }
        if (positive || negative) {
          pixel(x, y, color);
        }
      }
    }
  }
  void text(int x, int y, std::string text, Color c = {30, 30, 30}, int scale = 2) {
    static const std::string                                  chars  = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 .:-/()%=_+";
    static const std::array<std::array<unsigned char, 5>, 46> glyphs = {
        {{{126, 9, 9, 9, 126}},   {{127, 73, 73, 73, 54}}, {{62, 65, 65, 65, 34}}, {{127, 65, 65, 34, 28}}, {{127, 73, 73, 73, 65}},
         {{127, 9, 9, 9, 1}},     {{62, 65, 73, 73, 122}}, {{127, 8, 8, 8, 127}},  {{0, 65, 127, 65, 0}},   {{32, 64, 65, 63, 1}},
         {{127, 8, 20, 34, 65}},  {{127, 64, 64, 64, 64}}, {{127, 2, 12, 2, 127}}, {{127, 4, 8, 16, 127}},  {{62, 65, 65, 65, 62}},
         {{127, 9, 9, 9, 6}},     {{62, 65, 81, 33, 94}},  {{127, 9, 25, 41, 70}}, {{70, 73, 73, 73, 49}},  {{1, 1, 127, 1, 1}},
         {{63, 64, 64, 64, 63}},  {{31, 32, 64, 32, 31}},  {{63, 64, 56, 64, 63}}, {{99, 20, 8, 20, 99}},   {{7, 8, 112, 8, 7}},
         {{97, 81, 73, 69, 67}},  {{62, 81, 73, 69, 62}},  {{0, 66, 127, 64, 0}},  {{98, 81, 73, 73, 70}},  {{34, 65, 73, 73, 54}},
         {{24, 20, 18, 127, 16}}, {{39, 69, 69, 69, 57}},  {{60, 74, 73, 73, 48}}, {{1, 113, 9, 5, 3}},     {{54, 73, 73, 73, 54}},
         {{6, 73, 73, 41, 30}},   {{0, 0, 0, 0, 0}},       {{0, 96, 96, 0, 0}},    {{0, 54, 54, 0, 0}},     {{8, 8, 8, 8, 8}},
         {{32, 16, 8, 4, 2}},     {{0, 28, 34, 65, 0}},    {{0, 65, 34, 28, 0}},   {{99, 19, 8, 100, 99}},  {{20, 20, 20, 20, 20}},
         {{64, 64, 64, 64, 64}}}
    };
    for (char ch : text) {
      size_t g = chars.find(static_cast<char>(std::toupper(static_cast<unsigned char>(ch))));
      if (g < glyphs.size()) {
        for (int a = 0; a < 5; ++a) {
          for (int b = 0; b < 7; ++b) {
            if (glyphs[g][a] & (1 << b)) {
              rect(x + a * scale, y + b * scale, x + (a + 1) * scale, y + (b + 1) * scale, c);
            }
          }
        }
      }
      x += 6 * scale;
    }
  }
  static void integer(std::string& s, uint32_t n) {
    for (int i = 3; i >= 0; --i) {
      s.push_back(static_cast<char>((n >> (i * 8)) & 255));
    }
  }
  void png(const std::string& path) const {
    std::string raw;
    raw.reserve(static_cast<size_t>(w * 3 + 1) * h);
    for (int y = 0; y < h; ++y) {
      raw.push_back(0);
      raw.append(reinterpret_cast<const char*>(rgb.data() + static_cast<size_t>(y) * w * 3), static_cast<size_t>(w) * 3);
    }
    uLongf      size = compressBound(raw.size());
    std::string compressed(size, '\0');
    require(compress2(reinterpret_cast<Bytef*>(compressed.data()), &size, reinterpret_cast<const Bytef*>(raw.data()), raw.size(), 6)
                == Z_OK,
            "PNG compression failed");
    compressed.resize(size);
    std::string result = "\x89PNG\r\n\x1a\n";
    auto        chunk  = [&](const std::string& type, const std::string& data) {
      integer(result, static_cast<uint32_t>(data.size()));
      size_t begin  = result.size();
      result       += type;
      result       += data;
      integer(result,
              static_cast<uint32_t>(
                  crc32(0, reinterpret_cast<const Bytef*>(result.data() + begin), static_cast<uInt>(result.size() - begin))));
    };
    std::string header;
    integer(header, static_cast<uint32_t>(w));
    integer(header, static_cast<uint32_t>(h));
    header.append("\x08\x02\x00\x00\x00", 5);
    chunk("IHDR", header);
    chunk("IDAT", compressed);
    chunk("IEND", "");
    write(path, result);
  }
};
Color heat(double d, double maximum) {
  double t = std::clamp(d / std::max(maximum, 1e-30), 0., 1.);
  return {static_cast<unsigned char>(245 - 25 * t),
          static_cast<unsigned char>(250 - 220 * t),
          static_cast<unsigned char>(255 - 225 * t)};
}
std::string format(double x, int precision = 3) {
  std::ostringstream s;
  s << std::fixed << std::setprecision(precision) << x;
  return s.str();
}
double density_palette_max(const Metrics& a, const Metrics& b, const Options& o, int row) {
  double shared = std::max({a.peak, b.peak, o.rho});
  if (o.density_scale == "independent" || (o.density_scale == "auto" && o.deformation_preview && row == 1)) {
    return std::max(row == 0 ? a.peak : b.peak, 1e-30);
  }
  return shared;
}
void render(const Layout& before, const Layout& after, const Mesh* mesh, const Metrics& a, const Metrics& b, const Options& o) {
  int    width = o.pixels, height = width * 2 / 3;
  Image  im(width, height);
  int    panel_width = (width - 80) / 3, panel_height = (height - 160) / 2;
  double vmax = std::max({a.peak, b.peak, o.rho});
  im.text(20,
          12,
          o.deformation_preview ? "DENSITY-ONLY SPACE WARP / DEFORMED SRAM OUTLINES"
                                : "WARPSPACE - RIGID CELLS / TRANSLATION-ONLY MACROS",
          {25, 25, 25},
          width >= 1300 ? 2 : 1);
  for (int row = 0; row < 2; ++row) {
    for (int col = 0; col < 3; ++col) {
      int         x0 = 20 + col * (panel_width + 20), y0 = 60 + row * (panel_height + 60);
      const auto& l       = row == 0 ? before : after;
      const auto& metrics = row == 0 ? a : b;
      int         pw = panel_width, ph = panel_height;
      if (col < 2 || row == 1) {
        double aspect = l.width() / l.height();
        pw            = std::min(panel_width, static_cast<int>(panel_height * aspect));
        ph            = std::min(panel_height, static_cast<int>(panel_width / aspect));
        pw            = std::max(1, pw);
        ph            = std::max(1, ph);
      }
      int x1 = x0 + pw, y1 = y0 + ph;
      if (col < 2) {
        im.text(x0,
                y0 - 23,
                std::string(row == 0 ? "BEFORE " : "AFTER ")
                    + (col == 0   ? "PLACEMENT"
                       : col == 1 ? "DENSITY"
                                  : ""),
                {25, 25, 25},
                2);
      }
      auto point = [&](Point p) {
        auto n = l.normalize(p);
        return std::pair<int, int>{x0 + int(std::round(n.x * (pw - 1))), y1 - 1 - int(std::round(n.y * (ph - 1)))};
      };
      auto warped_macros = [&]() {
        for (size_t k : mesh->macros) {
          const auto& c = before.insts[k];
          if (c.standard) {
            continue;
          }
          auto lo = before.normalize(c.p), hi = before.normalize(c.p + Point{c.w, c.h});
          for (const auto& f : mesh->faces) {
            auto  face_lo = mesh->original[f.v[0]], face_hi = mesh->original[f.v[2]];
            Point clipped_lo{std::max(lo.x, face_lo.x), std::max(lo.y, face_lo.y)},
                  clipped_hi{std::min(hi.x, face_hi.x), std::min(hi.y, face_hi.y)};
            if (clipped_lo.x >= clipped_hi.x || clipped_lo.y >= clipped_hi.y) {
              continue;
            }
            std::array<Point, 4> source = {
                clipped_lo,
                Point{clipped_hi.x, clipped_lo.y},
                clipped_hi,
                Point{clipped_lo.x, clipped_hi.y}
            };
            std::array<std::pair<int, int>, 4> polygon;
            for (int v = 0; v < 4; ++v) {
              double u      = (source[v].x - face_lo.x) / (face_hi.x - face_lo.x),
                     t      = (source[v].y - face_lo.y) / (face_hi.y - face_lo.y);
              Point  mapped = mesh->points[f.v[0]] * ((1 - u) * (1 - t)) + mesh->points[f.v[1]] * (u * (1 - t))
                              + mesh->points[f.v[2]] * (u * t) + mesh->points[f.v[3]] * ((1 - u) * t);
              polygon[v]    = point(l.physical(mapped));
            }
            if (col == 0) {
              im.hatch(polygon, {190, 155, 110}, true);
            }
            for (int e = 0; e < 4; ++e) {
              auto a0 = source[e], b0 = source[(e + 1) % 4];
              if ((a0.x == b0.x && (a0.x == lo.x || a0.x == hi.x)) || (a0.y == b0.y && (a0.y == lo.y || a0.y == hi.y))) {
                auto a1 = polygon[e], b1 = polygon[(e + 1) % 4];
                im.line(a1.first, a1.second, b1.first, b1.second, {60, 45, 25});
              }
            }
          }
        }
      };
      im.rect(x0, y0, x1, y1, {245, 247, 249});
      if (o.deformation_preview && row == 1 && col == 2 && mesh) {
        for (const auto& f : mesh->faces) {
          double t     = std::clamp(std::log2(mesh->area(f) / f.initial_area) / 4, -1., 1.);
          Color  color = t >= 0 ? Color{static_cast<unsigned char>(245 - 210 * t), static_cast<unsigned char>(245 - 100 * t), 245}
                                : Color{245, static_cast<unsigned char>(245 + 180 * t), static_cast<unsigned char>(245 + 180 * t)};
          std::array<std::pair<int, int>, 4> polygon;
          for (int v = 0; v < 4; ++v) {
            polygon[v] = point(l.physical(mesh->points[f.v[v]]));
          }
          im.hatch(polygon, color, true);
        }
        warped_macros();
        im.text(x0, y0 - 23, "SPACE AREA CHANGE", {25, 25, 25}, 2);
        im.text(x0, y1 + 8, "RED COMPRESS / BLUE EXPAND", {25, 25, 25}, 1);
        im.text(x0, y1 + 20, "LOG2 SCALE -4..4 (1/16..16X)", {25, 25, 25}, 1);
      } else if (col == 0 || (row == 1 && col == 2)) {
        for (const auto& c : l.insts) {
          if (c.macro && !(o.deformation_preview && row == 1 && !c.standard)) {
            auto lo = point(c.p), hi = point(c.p + Point{c.w, c.h});
            im.rect(lo.first, hi.second, hi.first + 1, lo.second + 1, c.fixed ? Color{65, 75, 95} : Color{150, 100, 60});
          }
        }
        if (o.deformation_preview && row == 1 && mesh) {
          warped_macros();
        }
        if (col == 0) {
          for (const auto& c : l.insts) {
            if (!c.macro) {
              auto p = point(c.p + Point{c.w / 2, c.h / 2});
              im.pixel(p.first, p.second, {45, 115, 170});
            }
          }
          if (row == 1 && mesh) {
            int stride = std::max(1, std::max(mesh->nx, mesh->ny) / 24);
            for (auto [u, v] : mesh->edges) {
              if ((u / mesh->nx == v / mesh->nx && (u / mesh->nx) % stride != 0)
                  || (u % mesh->nx == v % mesh->nx && (u % mesh->nx) % stride != 0)) {
                continue;
              }
              auto p = point(l.physical(mesh->points[u])), q = point(l.physical(mesh->points[v]));
              im.line(p.first, p.second, q.first, q.second, {130, 155, 130});
            }
            for (const auto& c : l.insts) {
              if (c.macro && !c.fixed) {
                auto p = point(c.original + Point{c.w / 2, c.h / 2}), q = point(c.p + Point{c.w / 2, c.h / 2});
                im.arrow(p.first, p.second, q.first, q.second, {235, 30, 60});
              }
            }
          }
        } else {
          size_t samples = std::min<size_t>(600, l.insts.size()), step = std::max<size_t>(1, l.insts.size() / samples);
          while (std::gcd(step, l.insts.size()) != 1) {
            ++step;
          }
          for (size_t sample = 0; sample < samples; ++sample) {
            size_t      i = (sample * step) % l.insts.size();
            const auto& c = l.insts[i];
            if (c.macro) {
              continue;
            }
            auto  p = point(c.original + Point{c.w / 2, c.h / 2}), q = point(c.p + Point{c.w / 2, c.h / 2});
            Color color = heat(length(c.p - c.original), std::max(metrics.maximum, 1e-20));
            im.arrow(p.first, p.second, q.first, q.second, color);
            im.rect(q.first - 1, q.second - 1, q.first + 2, q.second + 2, color);
          }
          im.text(x0, y0 - 23, "AFTER DISPLACEMENT", {25, 25, 25}, 2);
        }
      } else if (col == 1) {
        double density_scale = density_palette_max(a, b, o, row);
        for (int y = 0; y < o.bins; ++y) {
          for (int x = 0; x < o.bins; ++x) {
            double d  = metrics.density[y * o.bins + x];
            int    bx = x0 + x * pw / o.bins, by = y1 - (y + 1) * ph / o.bins, ex = x0 + (x + 1) * pw / o.bins,
                   ey = y1 - y * ph / o.bins;
            im.rect(bx, by, ex, ey, heat(d, density_scale));
            if (d > o.rho) {
              im.line(bx, by, ex - 1, by, {100, 25, 30});
              im.line(bx, by, bx, ey - 1, {100, 25, 30});
            }
          }
        }
        if (row == 1 && mesh) {
          for (const auto& f : mesh->faces) {
            if (f.macro < 0 && f.demand / o.rho - mesh->area(f) > 1e-9 * f.initial_area) {
              std::array<std::pair<int, int>, 4> polygon;
              for (int k = 0; k < 4; ++k) {
                polygon[k] = point(l.physical(mesh->points[f.v[k]]));
              }
              im.hatch(polygon, {220, 0, 45});
            }
          }
        }
        im.text(x0, y1 + 8, "MAX " + format(metrics.peak) + " P99 " + format(metrics.p99), {25, 25, 25}, 1);
        im.text(x0,
                y1 + 20,
                "SCALE 0.." + format(density_scale)
                    + (o.density_scale == "shared" ? " (SHARED)"
                                                   : (o.deformation_preview && row == 1 ? " (RESCALED)" : " CAP " + format(o.rho))),
                {25, 25, 25},
                1);
      } else {
        std::array<int, 40> bins{};
        for (double d : metrics.density) {
          ++bins[std::min(39, int(39 * d / std::max(vmax, 1e-30)))];
        }
        int peak = *std::max_element(bins.begin(), bins.end());
        for (int x = 0; x < 40; ++x) {
          im.rect(x0 + x * pw / 40, y1 - bins[x] * (ph - 10) / std::max(1, peak), x0 + (x + 1) * pw / 40 - 1, y1, {55, 120, 170});
        }
        int mark = x0 + int(o.rho / vmax * (pw - 1));
        im.line(mark, y0, mark, y1, {230, 40, 40});
        im.text(x0, y0 - 23, "BEFORE DENSITY HISTOGRAM", {25, 25, 25}, 2);
        im.text(x0, y1 + 8, "OVERFLOW " + format(metrics.overflow) + " HPWL " + format(metrics.hpwl, 1), {25, 25, 25}, 1);
      }
      im.line(x0, y0, x1, y0, {30, 30, 30});
      im.line(x0, y1, x1, y1, {30, 30, 30});
      im.line(x0, y0, x0, y1, {30, 30, 30});
      im.line(x1, y0, x1, y1, {30, 30, 30});
    }
  }
  im.text(20,
          height - 22,
          o.deformation_preview ? "SPACE WARP PREVIEW - MACRO SHAPES ARE PASSIVE MARKERS - NO DEF OUTPUT"
                                : "UNIFORM-GRID AREA-OVERLAP DENSITY - UNLEGALIZED OUTPUT - UNITS MICRONS",
          {40, 40, 40},
          1);
  im.png(o.report);
}

void report_json(const Layout& before, const Layout& after, const Mesh* mesh, const Metrics& a, const Metrics& b,
                 const std::vector<Pass>& passes, const Options& o) {
  rapidjson::StringBuffer                          buffer;
  rapidjson::PrettyWriter<rapidjson::StringBuffer> w(buffer);
  w.StartObject();
  w.Key("schema_version");
  w.Int(1);
  w.Key("input_def");
  w.String(std::filesystem::weakly_canonical(o.input).string().c_str());
  w.Key("density_color_scale");
  w.String(o.density_scale.c_str());
  w.Key("preview_grid");
  w.String(o.preview_grid.c_str());
  w.Key("density_palette_before_max");
  w.Double(density_palette_max(a, b, o, 0));
  w.Key("density_palette_after_max");
  w.Double(density_palette_max(a, b, o, 1));
  w.Key("rho_max");
  w.Double(o.rho);
  w.Key("bins");
  w.Int(o.bins);
  w.Key("balance_weight");
  w.Double(o.balance);
  w.Key("balance_model");
  w.String(o.balance_model.c_str());
  w.Key("deformation_preview");
  w.Bool(o.deformation_preview);
  w.Key("macro_rigidity_enforced");
  w.Bool(!o.deformation_preview);
  w.Key("movable_macros");
  w.Bool(o.movable_macros);
  w.Key("units");
  w.String("microns; density uses standard-cell rectangle overlap / full evaluation-bin area");
  w.Key("uniform_grid_cap_satisfied");
  w.Bool(b.peak <= o.rho * (1 + o.tol));
  w.Key("mesh_cap_satisfied");
  if (mesh) {
    w.Bool(face_density(*mesh) <= o.rho * (1 + o.tol));
  } else {
    w.Null();
  }
  w.Key("mesh_invariants_passed");
  if (mesh) {
    w.Bool(true);
  } else {
    w.Null();
  }
  w.Key("reassigned_cell_centers");
  w.Int(mesh ? mesh->reassigned : 0);
  w.Key("mesh_vertices");
  w.Uint64(mesh ? mesh->points.size() : 0);
  w.Key("mesh_faces");
  w.Uint64(mesh ? mesh->faces.size() : 0);
  w.Key("mesh_max_density");
  if (mesh) {
    w.Double(face_density(*mesh));
  } else {
    w.Null();
  }
  w.Key("total_slack_area");
  w.Double(passes.empty() ? 0 : passes.back().slack);
  w.Key("slack_faces");
  w.Int(passes.empty() ? 0 : passes.back().slack_faces);
  auto metrics = [&](const char* name, const Metrics& m) {
    w.Key(name);
    w.StartObject();
    w.Key("max_density");
    w.Double(m.peak);
    w.Key("p99_density");
    w.Double(m.p99);
    w.Key("overflow");
    w.Double(m.overflow);
    w.Key("hpwl_cell_centers");
    if (o.deformation_preview && std::string_view(name) == "after") {
      w.Null();  // Deformed macro outlines do not define a rigid placement HPWL.
    } else {
      w.Double(m.hpwl);
    }
    w.Key("displacement_mean");
    w.Double(m.mean);
    w.Key("displacement_p95");
    w.Double(m.p95);
    w.Key("displacement_max");
    w.Double(m.maximum);
    w.Key("density");
    w.StartArray();
    for (double d : m.density) {
      w.Double(d);
    }
    w.EndArray();
    w.EndObject();
  };
  metrics("before", a);
  metrics("after", b);
  w.Key("passes");
  w.StartArray();
  for (const auto& p : passes) {
    w.StartObject();
    w.Key("axis");
    w.String(p.axis.c_str());
    w.Key("status");
    w.String(p.status.c_str());
    w.Key("slack_area");
    w.Double(p.slack);
    w.Key("slack_faces");
    w.Int(p.slack_faces);
    w.Key("seconds");
    w.Double(p.seconds);
    w.EndObject();
  }
  w.EndArray();
  w.Key("macros_and_fixed_objects");
  w.StartArray();
  for (size_t i = 0; i < after.insts.size(); ++i) {
    if (after.insts[i].macro) {
      const auto& c = after.insts[i];
      Point       d = c.p - before.insts[i].p;
      w.StartObject();
      w.Key("name");
      w.String(c.name.c_str());
      w.Key("fixed");
      w.Bool(c.fixed);
      w.Key("input_fixed");
      w.Bool(before.insts[i].fixed);
      w.Key("dx");
      w.Double(d.x);
      w.Key("dy");
      w.Double(d.y);
      w.Key("displacement");
      w.Double(length(d));
      w.EndObject();
    }
  }
  w.EndArray();
  w.Key("mesh");
  w.StartObject();
  w.Key("nx");
  w.Int(mesh ? mesh->nx : 0);
  w.Key("ny");
  w.Int(mesh ? mesh->ny : 0);
  w.Key("vertices");
  w.StartArray();
  if (mesh) {
    for (size_t i = 0; i < mesh->points.size(); ++i) {
      auto a0 = before.physical(mesh->original[i]), b0 = before.physical(mesh->points[i]);
      w.StartArray();
      w.Double(a0.x);
      w.Double(a0.y);
      w.Double(b0.x);
      w.Double(b0.y);
      w.EndArray();
    }
  }
  w.EndArray();
  w.Key("free_faces");
  w.StartArray();
  if (mesh) {
    double free_area = 0, demand = 0;
    for (const auto& f : mesh->faces) {
      if (f.macro < 0) {
        free_area += f.initial_area;
        demand    += f.demand;
      }
    }
    const double die_area = before.width() * before.height();
    for (const auto& f : mesh->faces) {
      if (f.macro < 0) {
        // [vertex ids (CCW), cell area, initial area, final area, target area].
        w.StartArray();
        for (int v : f.v) {
          w.Int(v);
        }
        w.Double(f.demand * die_area);
        w.Double(f.initial_area * die_area);
        w.Double(mesh->area(f) * die_area);
        w.Double((.05 * f.initial_area + (demand > 0 ? .95 * free_area * f.demand / demand : 0)) * die_area);
        w.EndArray();
      }
    }
  }
  w.EndArray();
  w.EndObject();
  w.Key("limitations");
  w.String(
      "Unlegalized, rigid-cell center warp; frozen topology. Face capacity is a surrogate, not a guarantee on evaluation bins. "
      "Reassigned centers are clamped. IO terms are omitted from center HPWL; routed geometry is preserved but stale after "
      "movement. Fixed standard cells are rigid obstacles.");
  w.EndObject();
  write(o.metrics, std::string(buffer.GetString()) + "\n");
}
// Compact density-grid experiments: transport area demand, then sample the
// field at macro centers to suggest anchors. No cell coordinates or legality.
std::vector<Point> clip_box(std::vector<Point> polygon, Point lo, Point hi) {
  for (int axis = 0; axis < 2; ++axis) {
    for (int side = 0; side < 2; ++side) {
      if (polygon.empty()) {
        return polygon;
      }
      double             bound = coordinate(side == 0 ? lo : hi, axis);
      std::vector<Point> out;
      for (size_t i = 0; i < polygon.size(); ++i) {
        Point a = polygon[i], b = polygon[(i + 1) % polygon.size()];
        bool  ain = side == 0 ? coordinate(a, axis) >= bound : coordinate(a, axis) <= bound;
        bool  bin = side == 0 ? coordinate(b, axis) >= bound : coordinate(b, axis) <= bound;
        if (ain) {
          out.push_back(a);
        }
        if (ain != bin) {
          out.push_back(a + (b - a) * ((bound - coordinate(a, axis)) / (coordinate(b, axis) - coordinate(a, axis))));
        }
      }
      polygon = std::move(out);
    }
  }
  return polygon;
}
Point sample_field(const Mesh& m, Point p) {
  int         i = std::clamp(int(std::upper_bound(m.xs.begin(), m.xs.end(), p.x) - m.xs.begin()) - 1, 0, m.nx - 2);
  int         j = std::clamp(int(std::upper_bound(m.ys.begin(), m.ys.end(), p.y) - m.ys.begin()) - 1, 0, m.ny - 2);
  double      u = std::clamp((p.x - m.xs[i]) / (m.xs[i + 1] - m.xs[i]), 0., 1.);
  double      v = std::clamp((p.y - m.ys[j]) / (m.ys[j + 1] - m.ys[j]), 0., 1.);
  const auto& f = m.faces[j * (m.nx - 1) + i];
  return m.points[f.v[0]] * ((1 - u) * (1 - v)) + m.points[f.v[1]] * (u * (1 - v)) + m.points[f.v[2]] * (u * v)
         + m.points[f.v[3]] * ((1 - u) * v);
}
std::vector<double> macro_occupancy(const Layout& l, int bins) {
  using Rectangle = std::array<double, 4>;
  std::vector<std::vector<Rectangle>> pieces(static_cast<size_t>(bins) * bins);
  double                              dx = l.width() / bins, dy = l.height() / bins;
  for (const auto& c : l.insts) {
    double xl = std::max(l.lo.x, c.p.x), yl = std::max(l.lo.y, c.p.y), xh = std::min(l.hi.x, c.p.x + c.w),
           yh = std::min(l.hi.y, c.p.y + c.h);
    if (xl >= xh || yl >= yh) {
      continue;
    }
    int x0 = std::clamp(int((xl - l.lo.x) / dx), 0, bins - 1), x1 = std::clamp(int((xh - l.lo.x) / dx), 0, bins - 1);
    int y0 = std::clamp(int((yl - l.lo.y) / dy), 0, bins - 1), y1 = std::clamp(int((yh - l.lo.y) / dy), 0, bins - 1);
    for (int y = y0; y <= y1; ++y) {
      for (int x = x0; x <= x1; ++x) {
        Rectangle r{std::max(xl, l.lo.x + x * dx),
                    std::max(yl, l.lo.y + y * dy),
                    std::min(xh, l.lo.x + (x + 1) * dx),
                    std::min(yh, l.lo.y + (y + 1) * dy)};
        if (r[0] < r[2] && r[1] < r[3]) {
          pieces[y * bins + x].push_back(r);
        }
      }
    }
  }
  std::vector<double> out(pieces.size(), 0.);
  for (size_t i = 0; i < pieces.size(); ++i) {
    std::vector<double> xs;
    for (const auto& r : pieces[i]) {
      xs.push_back(r[0]);
      xs.push_back(r[2]);
    }
    std::sort(xs.begin(), xs.end());
    xs.erase(std::unique(xs.begin(), xs.end()), xs.end());
    for (size_t j = 1; j < xs.size(); ++j) {
      double                                 mid = (xs[j - 1] + xs[j]) * .5;
      std::vector<std::pair<double, double>> ys;
      for (const auto& r : pieces[i]) {
        if (r[0] < mid && mid < r[2]) {
          ys.emplace_back(r[1], r[3]);
        }
      }
      std::sort(ys.begin(), ys.end());
      double length = 0, hi = -std::numeric_limits<double>::infinity();
      for (auto [a, b] : ys) {
        if (b > hi) {
          length += b - std::max(a, hi);
          hi      = b;
        }
      }
      out[i] += (xs[j] - xs[j - 1]) * length;
    }
  }
  return out;
}
std::vector<double> smooth_box(const std::vector<double>& input, int bins, int window) {
  if (window == 0) {
    return input;
  }
  // A full-block or oversized filter uses the block average everywhere.
  // In particular, never dilute boundary bins with zero padding.
  if (window >= bins) {
    double mean = std::accumulate(input.begin(), input.end(), 0.) / input.size();
    return std::vector<double>(input.size(), mean);
  }
  // A centered even-width box has half-weight end samples: for width 8,
  // offsets -4..4 carry {.5,1,1,1,1,1,1,1,.5}. No directional half-bin bias.
  std::vector<std::pair<int, double>> kernel;
  double                              radius = window * .5;
  for (int i = -window; i <= window; ++i) {
    double w = std::max(0., std::min(i + .5, radius) - std::max(i - .5, -radius));
    if (w > 0) {
      kernel.emplace_back(i, w / window);
    }
  }
  auto reflect = [&](int k) {
    while (k < 0 || k >= bins) {
      if (k < 0) {
        k = -k - 1;
      } else {
        k = 2 * bins - k - 1;
      }
    }
    return k;
  };
  std::vector<double> a(input.size(), 0.), b(input.size(), 0.);
  for (int y = 0; y < bins; ++y) {
    for (int x = 0; x < bins; ++x) {
      for (auto [k, w] : kernel) {
        a[y * bins + x] += w * input[y * bins + reflect(x + k)];
      }
    }
  }
  for (int y = 0; y < bins; ++y) {
    for (int x = 0; x < bins; ++x) {
      for (auto [k, w] : kernel) {
        b[y * bins + x] += w * a[reflect(y + k) * bins + x];
      }
    }
  }
  return b;
}
int density_grid_run(Options o) {
  auto                start = Clock::now();
  Layout              before;
  std::vector<double> mass;
  if (o.density_input.empty()) {
    before          = read_def(o.input, read_lefs(o.lefs), o.macro_factor, true);
    auto   measured = measure(before, o);
    double bin_area = before.width() * before.height() / (o.bins * o.bins);
    for (double d : measured.density) {
      mass.push_back(d * bin_area);
    }
    std::erase_if(before.insts, [](const Inst& c) { return c.standard; });
    before.insts.shrink_to_fit();
  } else {
    rapidjson::Document d;
    d.Parse(read(o.density_input).c_str());
    require(!d.HasParseError() && d.IsObject(), "invalid density dataset");
    if (o.label.empty() && d.HasMember("label") && d["label"].IsString()) {
      o.label = d["label"].GetString();
    }
    auto num = [](const rapidjson::Value& x) {
      require(x.IsNumber() && std::isfinite(x.GetDouble()), "invalid density dataset number");
      return x.GetDouble();
    };
    require(d.HasMember("die") && d["die"].IsArray() && d["die"].Size() == 4 && d.HasMember("bins") && d["bins"].IsInt(),
            "density dataset needs die[4] and bins");
    before.lo = {num(d["die"][0]), num(d["die"][1])};
    before.hi = {num(d["die"][2]), num(d["die"][3])};
    o.bins    = d["bins"].GetInt();
    require(before.width() > 0 && before.height() > 0 && o.bins >= 2 && o.bins <= 256, "invalid density grid dimensions");
    require(d.HasMember("mass") && d["mass"].IsArray() && d["mass"].Size() == static_cast<size_t>(o.bins * o.bins),
            "density dataset needs bins*bins mass values");
    for (const auto& v : d["mass"].GetArray()) {
      double value = num(v);
      require(value >= 0, "negative bin mass");
      mass.push_back(value);
    }
    require(d.HasMember("macros") && d["macros"].IsArray(), "density dataset needs macros array");
    for (const auto& v : d["macros"].GetArray()) {
      require(v.IsObject() && v.HasMember("name") && v["name"].IsString() && v.HasMember("center") && v["center"].IsArray()
                  && v["center"].Size() == 2 && v.HasMember("size") && v["size"].IsArray() && v["size"].Size() == 2,
              "invalid density macro");
      Inst c;
      c.name = v["name"].GetString();
      c.w    = num(v["size"][0]);
      c.h    = num(v["size"][1]);
      Point center{num(v["center"][0]), num(v["center"][1])};
      require(c.w > 0 && c.h > 0 && center.x >= before.lo.x && center.x <= before.hi.x && center.y >= before.lo.y
                  && center.y <= before.hi.y,
              "invalid macro center/size");
      c.p        = center - Point{c.w / 2, c.h / 2};
      c.original = c.p;
      c.macro    = true;
      before.insts.push_back(c);
    }
  }
  double die_area = before.width() * before.height(), bin_area = die_area / (o.bins * o.bins);
  double total = std::accumulate(mass.begin(), mass.end(), 0.);
  require(total > 0 && total / die_area <= o.rho * (1 + 1e-9), "density grid has zero mass or infeasible global cap");
  std::vector<double> occupied(mass.size(), 0.), free(mass.size(), bin_area), target(mass.size(), -1.),
      target_density(mass.size(), 0.);
  if (o.macro_space) {
    occupied = macro_occupancy(before, o.bins);
  }
  for (size_t i = 0; i < free.size(); ++i) {
    free[i] = std::max(0., bin_area - occupied[i]);
  }
  double free_total = std::accumulate(free.begin(), free.end(), 0.);
  require(free_total > 0 && total / free_total <= o.rho * (1 + 1e-9), "infeasible global free-area density cap");
  if (o.smooth_window > 0) {
    auto smooth_mass = smooth_box(mass, o.bins, o.smooth_window), smooth_free = smooth_box(free, o.bins, o.smooth_window);
    std::vector<double> requested(free.size(), 0.);
    for (size_t i = 0; i < free.size(); ++i) {
      double original   = mass[i] / std::max(free[i], 1e-9 * bin_area);
      double smoothed   = smooth_mass[i] / std::max(smooth_free[i], 1e-9 * bin_area);
      target_density[i] = (1 - o.smooth_blend) * original + o.smooth_blend * smoothed;
      requested[i]      = target_density[i] > 0 ? mass[i] / target_density[i] : free[i];
    }
    double requested_total = std::accumulate(requested.begin(), requested.end(), 0.);
    require(requested_total > 0, "invalid local density target");
    for (size_t i = 0; i < target.size(); ++i) {
      target[i] = occupied[i] + requested[i] * free_total / requested_total;
    }
  } else if (o.macro_space) {
    for (size_t i = 0; i < target.size(); ++i) {
      target[i] = occupied[i] + .05 * free[i] + .95 * free_total * mass[i] / total;
    }
  }
  Mesh m;
  m.density_only = true;
  m.nx = m.ny = o.bins + 1;
  for (int i = 0; i <= o.bins; ++i) {
    m.xs.push_back(double(i) / o.bins);
    m.ys.push_back(double(i) / o.bins);
  }
  for (int j = 0; j < m.ny; ++j) {
    for (int i = 0; i < m.nx; ++i) {
      m.original.push_back({m.xs[i], m.ys[j]});
      if (i + 1 < m.nx) {
        m.edges.emplace_back(m.vertex(i, j), m.vertex(i + 1, j));
      }
      if (j + 1 < m.ny) {
        m.edges.emplace_back(m.vertex(i, j), m.vertex(i, j + 1));
      }
      if (i < o.bins && j < o.bins) {
        Face f{
            {m.vertex(i, j), m.vertex(i + 1, j), m.vertex(i + 1, j + 1), m.vertex(i, j + 1)}
        };
        f.initial_area = 1. / (o.bins * o.bins);
        f.demand       = mass[j * o.bins + i] / die_area;
        f.reserved     = occupied[j * o.bins + i] / die_area;
        f.target       = target[j * o.bins + i] < 0 ? -1 : target[j * o.bins + i] / die_area;
        m.faces.push_back(f);
      }
    }
  }
  m.points = m.original;
  std::vector<Pass> passes;
  invariants(m, before);
  std::cout << "Density grid " << o.bins << " x " << o.bins << ", " << before.insts.size()
            << " macro anchors; no per-cell variables or constraints\n"
            << std::flush;
  for (int i = 0; i < o.iters; ++i) {
    passes.push_back(solve_axis(m, before, o, 0));
    passes.push_back(solve_axis(m, before, o, 1));
    if (o.balance == 0 && face_density(m) <= o.rho * (1 + o.tol)) {
      break;
    }
  }
  Layout after = before;
  for (auto& c : after.insts) {
    Point center = before.normalize(c.p + Point{c.w / 2, c.h / 2});
    c.p          = before.physical(sample_field(m, center)) - Point{c.w / 2, c.h / 2};
  }
  std::vector<double> out_mass(mass.size(), 0.);
  std::vector<double> out_occupied(mass.size(), 0.);
  for (const auto& f : m.faces) {
    if (f.demand == 0 && f.reserved == 0) {
      continue;
    }
    std::vector<Point> polygon;
    Point              lo{1, 1}, hi{0, 0};
    for (int v : f.v) {
      Point p = m.points[v];
      polygon.push_back(p);
      lo.x = std::min(lo.x, p.x);
      lo.y = std::min(lo.y, p.y);
      hi.x = std::max(hi.x, p.x);
      hi.y = std::max(hi.y, p.y);
    }
    int xl = std::clamp(int(lo.x * o.bins), 0, o.bins - 1), xh = std::clamp(int(hi.x * o.bins), 0, o.bins - 1),
        yl = std::clamp(int(lo.y * o.bins), 0, o.bins - 1), yh = std::clamp(int(hi.y * o.bins), 0, o.bins - 1);
    for (int y = yl; y <= yh; ++y) {
      for (int x = xl; x <= xh; ++x) {
        auto   p    = clip_box(polygon, {double(x) / o.bins, double(y) / o.bins}, {double(x + 1) / o.bins, double(y + 1) / o.bins});
        double area = 0;
        for (size_t k = 0; k < p.size(); ++k) {
          area += cross(p[k], p[(k + 1) % p.size()]);
        }
        out_mass[y * o.bins + x]     += f.demand * die_area * std::abs(area) * .5 / m.area(f);
        out_occupied[y * o.bins + x] += f.reserved * die_area * std::abs(area) * .5 / m.area(f);
      }
    }
  }
  double out_total = std::accumulate(out_mass.begin(), out_mass.end(), 0.);
  require(std::abs(total - out_total) <= 1e-7 * total, "density transport did not conserve mass");
  double total_occupied       = std::accumulate(occupied.begin(), occupied.end(), 0.);
  double transported_occupied = std::accumulate(out_occupied.begin(), out_occupied.end(), 0.);
  require(std::abs(total_occupied - transported_occupied) <= 1e-7 * std::max(1., total_occupied),
          "macro reservation area not conserved");
  std::vector<double> a, b;
  for (size_t i = 0; i < mass.size(); ++i) {
    a.push_back(mass[i] / std::max(free[i], 1e-9 * bin_area));
    b.push_back(out_mass[i] / std::max(bin_area - out_occupied[i], 1e-9 * bin_area));
  }
  int    width = o.pixels, height = width * 2 / 5;
  Image  im(width, height);
  int    pw = (width - 100) / 3, ph = height - 120;
  double aspect = before.width() / before.height();
  pw            = std::min(pw, int(ph * aspect));
  ph            = std::min(ph, int(pw / aspect));
  double vmax   = std::max(*std::max_element(a.begin(), a.end()), *std::max_element(b.begin(), b.end()));
  im.text(20,
          12,
          o.label.empty() ? "DENSITY TRANSPORT / SUGGESTED MACRO ANCHORS - NO LEGALITY" : o.label,
          {25, 25, 25},
          width >= 1300 ? 2 : 1);
  for (int col = 0; col < 3; ++col) {
    int  x0 = 20 + col * (width - 40) / 3, y0 = 60;
    auto point = [&](Point p) {
      auto n = before.normalize(p);
      return std::pair<int, int>{x0 + int(std::round(n.x * (pw - 1))), y0 + ph - 1 - int(std::round(n.y * (ph - 1)))};
    };
    im.rect(x0, y0, x0 + pw, y0 + ph, {250, 250, 250});
    if (col < 2) {
      const auto& density = col == 0 ? a : b;
      for (int y = 0; y < o.bins; ++y) {
        for (int x = 0; x < o.bins; ++x) {
          im.rect(x0 + x * pw / o.bins,
                  y0 + ph - (y + 1) * ph / o.bins,
                  x0 + (x + 1) * pw / o.bins,
                  y0 + ph - y * ph / o.bins,
                  heat(density[y * o.bins + x], vmax));
        }
      }
    }
    for (size_t i = 0; i < before.insts.size(); ++i) {
      auto rectangle = [&](const Inst& c, Color color) {
        auto p = point(c.p), q = point(c.p + Point{c.w, c.h});
        int  xl = std::clamp(p.first, x0, x0 + pw - 1), xh = std::clamp(q.first, x0, x0 + pw - 1),
             yl = std::clamp(q.second, y0, y0 + ph - 1), yh = std::clamp(p.second, y0, y0 + ph - 1);
        im.line(xl, yl, xh, yl, color);
        im.line(xh, yl, xh, yh, color);
        im.line(xh, yh, xl, yh, color);
        im.line(xl, yh, xl, yl, color);
      };
      if (col == 0) {
        rectangle(before.insts[i], {70, 80, 100});
      } else if (col == 1) {
        rectangle(after.insts[i], {125, 90, 45});
      } else {
        auto p = point(before.insts[i].p + Point{before.insts[i].w / 2, before.insts[i].h / 2}),
             q = point(after.insts[i].p + Point{after.insts[i].w / 2, after.insts[i].h / 2});
        im.rect(p.first - 1, p.second - 1, p.first + 2, p.second + 2, {80, 90, 100});
        im.arrow(p.first, p.second, q.first, q.second, {200, 45, 40});
      }
    }
    im.text(x0,
            y0 - 23,
            col == 0   ? "BEFORE DENSITY + MACROS"
            : col == 1 ? "AFTER DENSITY + ANCHORS"
                       : "MACRO CENTER MOVEMENTS",
            {25, 25, 25},
            width >= 1300 ? 2 : 1);
    im.text(x0,
            y0 + ph + 8,
            col < 2 ? "SHARED SCALE 0.." + format(vmax) + "  MAX "
                          + format(col == 0 ? *std::max_element(a.begin(), a.end()) : *std::max_element(b.begin(), b.end()))
                    : "GRAY ORIGINAL / RED ARROWS TO ANCHORS",
            {25, 25, 25},
            1);
  }
  im.text(20,
          height - 20,
          o.macro_space
              ? "CELL DENSITY PER FREE AREA; RESERVED MACRO AREA TRANSPORTED WITH MESH; BOXES ARE ANCHOR SUGGESTIONS"
              : "BIN MASS IS SPREAD OVER WARPED FACES; MACRO SIZE UNCHANGED; OVERLAPS ALLOWED; BOXES CLIPPED AT DIE FOR DISPLAY",
          {25, 25, 25},
          1);
  im.png(o.report);
  rapidjson::StringBuffer                          buffer;
  rapidjson::PrettyWriter<rapidjson::StringBuffer> w(buffer);
  auto                                             point_json = [&](Point p) {
    w.StartArray();
    w.Double(p.x);
    w.Double(p.y);
    w.EndArray();
  };
  auto values = [&](const char* key, const std::vector<double>& data) {
    w.Key(key);
    w.StartArray();
    for (double v : data) {
      w.Double(v);
    }
    w.EndArray();
  };
  w.StartObject();
  w.Key("format");
  w.String("warpspace-density-v1");
  w.Key("label");
  w.String(o.label.c_str());
  w.Key("die");
  w.StartArray();
  w.Double(before.lo.x);
  w.Double(before.lo.y);
  w.Double(before.hi.x);
  w.Double(before.hi.y);
  w.EndArray();
  w.Key("bins");
  w.Int(o.bins);
  values("input_mass", mass);
  values("mass", out_mass);
  values("input_occupied_area", occupied);
  values("occupied_area", out_occupied);
  values("requested_free_density", target_density);
  values("target_total_area", target);
  w.Key("macro_space_reserved");
  w.Bool(o.macro_space);
  w.Key("smooth_window");
  w.Int(o.smooth_window);
  w.Key("smooth_blend");
  w.Double(o.smooth_blend);
  w.Key("density_basis");
  w.String(o.macro_space ? "cell area / free area" : "cell area / die-bin area");
  w.Key("input_macro_area");
  w.Double(total_occupied);
  w.Key("transported_macro_area");
  w.Double(transported_occupied);
  w.Key("target_mean_free_density");
  w.Double(total / free_total);
  w.Key("balance_weight");
  w.Double(o.balance);
  w.Key("balance_model");
  w.String(o.balance_model.c_str());
  w.Key("rho_max");
  w.Double(o.rho);
  w.Key("iterations");
  w.Int(static_cast<int>(passes.size() / 2));
  w.Key("mesh_max_density");
  w.Double(face_density(m));
  w.Key("mesh_cap_satisfied");
  w.Bool(face_density(m) <= o.rho * (1 + o.tol));
  w.Key("total_slack_area");
  double capacity_slack = 0;
  for (const auto& f : m.faces) {
    capacity_slack += std::max(0., f.reserved + f.demand / o.rho - m.area(f)) * die_area;
  }
  w.Double(capacity_slack);
  w.Key("passes");
  w.StartArray();
  for (const auto& p : passes) {
    w.StartObject();
    w.Key("axis");
    w.String(p.axis.c_str());
    w.Key("status");
    w.String(p.status.c_str());
    w.Key("seconds");
    w.Double(p.seconds);
    w.EndObject();
  }
  w.EndArray();
  w.Key("mass_unit");
  w.String("square microns of standard-cell area");
  w.Key("total_input_mass");
  w.Double(total);
  w.Key("total_output_mass");
  w.Double(out_total);
  w.Key("before_max_density");
  w.Double(*std::max_element(a.begin(), a.end()));
  w.Key("after_max_density");
  w.Double(*std::max_element(b.begin(), b.end()));
  w.Key("before_p99_density");
  w.Double(quantile(a, .99));
  w.Key("after_p99_density");
  w.Double(quantile(b, .99));
  w.Key("mesh_invariants_passed");
  w.Bool(true);
  w.Key("seconds");
  w.Double(std::chrono::duration<double>(Clock::now() - start).count());
  w.Key("macros");
  w.StartArray();
  int   inward = 0, outward = 0, moved = 0;
  Point die_center = (before.lo + before.hi) * .5;
  for (size_t i = 0; i < before.insts.size(); ++i) {
    const auto& c = before.insts[i];
    Point  p = c.p + Point{c.w / 2, c.h / 2}, q = after.insts[i].p + Point{c.w / 2, c.h / 2}, delta = q - p, r = p - die_center;
    double radial = length(r) > 0 ? (r.x * delta.x + r.y * delta.y) / length(r) : 0;
    if (length(delta) > 1e-3) {
      ++moved;
      if (radial < -.001) {
        ++inward;
      } else if (radial > .001) {
        ++outward;
      }
    }
    w.StartObject();
    w.Key("name");
    w.String(c.name.c_str());
    w.Key("before_center");
    point_json(p);
    w.Key("center");
    point_json(q);
    w.Key("size");
    point_json({c.w, c.h});
    w.Key("delta");
    point_json(delta);
    w.Key("radial_component");
    w.Double(radial);
    w.EndObject();
  }
  w.EndArray();
  w.Key("moved_macros");
  w.Int(moved);
  w.Key("inward_macros");
  w.Int(inward);
  w.Key("outward_macros");
  w.Int(outward);
  w.EndObject();
  write(o.metrics, std::string(buffer.GetString()) + "\n");
  std::cout << "Density peak " << *std::max_element(a.begin(), a.end()) << " -> " << *std::max_element(b.begin(), b.end())
            << "; mass " << total << " -> " << out_total << "; inward/outward anchors " << inward << "/" << outward << "; "
            << o.report << "\n";
  return o.require_cap && *std::max_element(b.begin(), b.end()) > o.rho * (1 + o.tol) ? 2 : 0;
}
Options arguments(int argc, char** argv) {
  Options o;
  bool    smooth_window_explicit = false;
  for (int i = 1; i < argc; ++i) {
    std::string flag  = argv[i];
    auto        value = [&]() {
      require(i + 1 < argc, "missing value for " + flag);
      return std::string(argv[++i]);
    };
    auto integer = [&]() {
      double d = number(value());
      require(d == std::floor(d) && d >= 0 && d <= 1000000, "invalid integer for " + flag);
      return static_cast<int>(d);
    };
    if (flag == "--help" || flag == "-h") {
      std::cout << "Usage: warpspace --lef tech.lef cells.lef [macro.lef ...] --def in.def\n"
                   "  --out-def warped.def --report report.png [--metrics report.json]\n"
                   "  [--rho-max .85 --bins 64 --iters 6 --mu 1 --lambda AREA_PENALTY]\n"
                   "  [--tol .02 --time-limit 30 --pixels 1600 --k-macro 2 --require-cap]\n"
                   "  [--movable-macros --balance WEIGHT] (opt-in density equalization)\n"
                   "  [--balance-model area|strain] (default area; strain varies local pressure)\n"
                   "  --deformation-preview: density-only warp with deformable macro outlines; PNG/JSON only.\n"
                   "  --replay-mesh report.json: replot a saved mesh with the same input/options, without solving.\n"
                   "  --density-scale auto|shared|independent: heatmap comparison scale (default auto).\n"
                   "  --preview-grid macro-edges|uniform: preview mesh lines (default macro-edges).\n"
                   "  --density-grid: fast bin-mass transport and suggested macro anchors (PNG/JSON only).\n"
                   "  --density-input grid.json: use a compact density dataset, without DEF/LEF.\n"
                   "  --density-grid --iters 0 extracts a reusable grid and macro dataset without solving.\n"
                   "  --label TEXT: title for a density-grid experiment.\n"
                   "  --macro-space: reserve macro occupied area in density-grid mode.\n"
                   "  --smooth-window N --smooth-blend .5: density-grid box average (default N=32; 0 disables).\n"
                   "  --render-only: DEF-to-PNG and metrics, without mesh solving or movement.\n"
                   "The output DEF is unlegalized. A reported face cap does not guarantee a\n"
                   "uniform-grid cap. --require-cap returns 2 if the measured cap is unmet.\n";
      std::exit(0);
    } else if (flag == "--lef") {
      size_t n = o.lefs.size();
      while (i + 1 < argc && std::string_view(argv[i + 1]).substr(0, 2) != "--") {
        o.lefs.emplace_back(argv[++i]);
      }
      require(o.lefs.size() > n, "--lef needs files");
    } else if (flag == "--def") {
      o.input = value();
    } else if (flag == "--out-def") {
      o.output = value();
    } else if (flag == "--report" || flag == "--png") {
      o.report = value();
    } else if (flag == "--metrics") {
      o.metrics = value();
    } else if (flag == "--bins") {
      o.bins = integer();
    } else if (flag == "--iters") {
      o.iters = integer();
    } else if (flag == "--pixels") {
      o.pixels = integer();
    } else if (flag == "--rho-max") {
      o.rho = number(value());
    } else if (flag == "--mu") {
      o.mu = number(value());
    } else if (flag == "--balance") {
      o.balance = number(value());
    } else if (flag == "--balance-model") {
      o.balance_model = value();
    } else if (flag == "--deformation-preview") {
      o.deformation_preview = true;
    } else if (flag == "--replay-mesh") {
      o.replay = value();
    } else if (flag == "--density-scale") {
      o.density_scale = value();
    } else if (flag == "--preview-grid") {
      o.preview_grid = value();
    } else if (flag == "--density-grid") {
      o.density_grid = true;
    } else if (flag == "--density-input") {
      o.density_input = value();
      o.density_grid  = true;
    } else if (flag == "--label") {
      o.label = value();
    } else if (flag == "--macro-space") {
      o.macro_space = true;
    } else if (flag == "--smooth-window") {
      o.smooth_window        = integer();
      smooth_window_explicit = true;
    } else if (flag == "--smooth-blend") {
      o.smooth_blend = number(value());
    } else if (flag == "--movable-macros") {
      o.movable_macros = true;
    } else if (flag == "--lambda") {
      o.lambda = number(value());
      require(o.lambda > 0, "--lambda must be positive");
    } else if (flag == "--tol") {
      o.tol = number(value());
    } else if (flag == "--k-macro") {
      o.macro_factor = number(value());
    } else if (flag == "--time-limit") {
      o.time_limit = number(value());
    } else if (flag == "--render-only") {
      o.render_only = true;
    } else if (flag == "--require-cap") {
      o.require_cap = true;
    } else {
      throw std::runtime_error("unknown option: " + flag);
    }
  }
  if (!o.density_grid && !smooth_window_explicit) {
    o.smooth_window = 0;
  }
  if (o.density_grid) {
    require(o.output.empty() && !o.render_only && o.replay.empty(),
            "density-grid mode writes PNG/JSON only; omit --out-def, --render-only and --replay-mesh");
    o.deformation_preview = true;
    o.preview_grid        = "uniform";
  }
  require((!o.lefs.empty() && !o.input.empty()) || !o.density_input.empty(),
          "provide --lef and --def, or --density-input (see --help)");
  require(o.render_only || o.deformation_preview || !o.output.empty(), "provide --out-def, --render-only or --deformation-preview");
  require(!o.deformation_preview || (o.output.empty() && !o.render_only),
          "deformation preview writes PNG/JSON only; omit --out-def and --render-only");
  require(o.bins >= 2 && o.bins <= 256 && o.iters <= 30 && o.pixels >= 900 && o.pixels <= 4000 && o.rho > 0 && o.mu >= 0
              && o.tol >= 0 && o.macro_factor > 0 && o.time_limit > 0 && o.balance >= 0,
          "invalid numeric option (see --help)");
  require(o.render_only || o.iters > 0 || o.density_grid, "--iters must be positive for a warp");
  require(o.smooth_window <= 256 && o.smooth_blend >= 0 && o.smooth_blend <= 1, "invalid smoothing options");
  require(o.density_grid || (!o.macro_space && o.smooth_window == 0),
          "macro-space and smoothing options require density-grid mode");
  require(o.balance_model == "area" || o.balance_model == "strain", "--balance-model must be area or strain");
  require(o.density_scale == "auto" || o.density_scale == "shared" || o.density_scale == "independent", "invalid --density-scale");
  require(o.preview_grid == "macro-edges" || (o.preview_grid == "uniform" && o.deformation_preview),
          "--preview-grid uniform requires --deformation-preview");
  if (o.metrics.empty()) {
    auto p = std::filesystem::path(o.report);
    p.replace_extension(".json");
    o.metrics = p.string();
  }
  std::set<std::filesystem::path> inputs;
  inputs.insert(std::filesystem::weakly_canonical(o.input));
  if (!o.replay.empty()) {
    inputs.insert(std::filesystem::weakly_canonical(o.replay));
  }
  if (!o.density_input.empty()) {
    inputs.insert(std::filesystem::weakly_canonical(o.density_input));
  }
  for (const auto& f : o.lefs) {
    inputs.insert(std::filesystem::weakly_canonical(f));
  }
  std::set<std::filesystem::path> outputs;
  for (const auto& f : {o.output, o.report, o.metrics}) {
    if (!f.empty()) {
      auto p = std::filesystem::weakly_canonical(f);
      require(!inputs.contains(p), "output would overwrite input: " + f);
      require(outputs.insert(p).second, "output paths must differ");
      require(p.parent_path().empty() || std::filesystem::is_directory(p.parent_path()),
              "output directory does not exist: " + p.parent_path().string());
    }
  }
  return o;
}
int run(int argc, char** argv) {
  Options o = arguments(argc, argv);
  if (o.density_grid) {
    return density_grid_run(o);
  }
  auto              tech   = read_lefs(o.lefs);
  Layout            before = read_def(o.input, tech, o.macro_factor), after = before;
  auto              a = measure(before, o);
  Mesh              mesh;
  std::vector<Pass> passes;
  if (!o.render_only) {
    if (o.movable_macros) {
      for (auto& c : after.insts) {
        if (c.macro && !c.standard && !c.cover) {
          c.fixed = false;
        }
      }
    }
    mesh = make_mesh(after, o);
    invariants(mesh, after);
    std::cout << after.insts.size() << " instances; mesh " << mesh.nx << " x " << mesh.ny << "; reassigned " << mesh.reassigned
              << " cell centers\n";
    if (!o.replay.empty()) {
      rapidjson::Document d;
      d.Parse(read(o.replay).c_str());
      require(!d.HasParseError() && d.IsObject() && d.HasMember("mesh") && d["mesh"].IsObject(), "invalid saved mesh JSON");
      require(d.HasMember("deformation_preview") && d["deformation_preview"].IsBool()
                  && d["deformation_preview"].GetBool() == o.deformation_preview,
              "saved mesh preview mode differs");
      if (d.HasMember("input_def")) {
        require(d["input_def"].IsString() && std::filesystem::weakly_canonical(o.input) == d["input_def"].GetString(),
                "saved mesh input DEF differs");
      }
      const auto& saved = d["mesh"];
      require(saved.HasMember("vertices") && saved["vertices"].IsArray() && saved["vertices"].Size() == mesh.points.size(),
              "saved mesh dimensions differ");
      for (size_t v = 0; v < mesh.points.size(); ++v) {
        const auto& p = saved["vertices"][static_cast<rapidjson::SizeType>(v)];
        require(p.IsArray() && p.Size() == 4, "invalid saved vertex");
        for (const auto& x : p.GetArray()) {
          require(x.IsNumber() && std::isfinite(x.GetDouble()), "invalid saved coordinate");
        }
        require(length(before.normalize({p[0].GetDouble(), p[1].GetDouble()}) - mesh.original[v]) < 1e-10,
                "saved mesh original grid differs");
        mesh.points[v] = before.normalize({p[2].GetDouble(), p[3].GetDouble()});
      }
      invariants(mesh, after);
      Pass replay{"replay", "validated saved mesh"};
      for (const auto& f : mesh.faces) {
        if (f.macro < 0) {
          double slack        = std::max(0., f.demand / o.rho - mesh.area(f));
          replay.slack       += slack * before.width() * before.height();
          replay.slack_faces += slack > 1e-9 * f.initial_area;
        }
      }
      passes.push_back(replay);
    }
    for (int iteration = 0; o.replay.empty() && iteration < o.iters; ++iteration) {
      passes.push_back(solve_axis(mesh, after, o, 0));
      passes.push_back(solve_axis(mesh, after, o, 1));
      std::cout << "iteration " << iteration + 1 << ": face max density=" << face_density(mesh) << "\n";
      if (o.balance == 0 && face_density(mesh) <= o.rho * (1 + o.tol)) {
        break;
      }
    }
    map_cells(after, mesh);
  }
  auto b = measure(after, o);
  if (!o.output.empty()) {
    write_def(after, o.output);
  }
  report_json(before, after, o.render_only ? nullptr : &mesh, a, b, passes, o);
  render(before, after, o.render_only ? nullptr : &mesh, a, b, o);
  std::cout << std::setw(24) << "metric" << std::setw(18) << "before" << std::setw(18) << "after" << "\n";
  auto row = [&](const char* label, double x, double y) {
    std::cout << std::setw(24) << label << std::setw(18) << x << std::setw(18) << y << "\n";
  };
  row("max bin density", a.peak, b.peak);
  row("p99 bin density", a.p99, b.p99);
  row("overflow", a.overflow, b.overflow);
  if (!o.deformation_preview) {
    row("center HPWL (um)", a.hpwl, b.hpwl);
  }
  row("mean displacement (um)", 0, b.mean);
  row("p95 displacement (um)", 0, b.p95);
  row("max displacement (um)", 0, b.maximum);
  bool capped = b.peak <= o.rho * (1 + o.tol);
  std::cout << "Uniform-grid density cap: " << (capped ? "MET" : "NOT MET") << "; report " << o.report << "; metrics " << o.metrics
            << "\n";
  return o.require_cap && !capped ? 2 : 0;
}
}  // namespace warp
int main(int argc, char** argv) {
  try {
    return warp::run(argc, argv);
  } catch (const std::exception& e) {
    std::cerr << "warpspace: " << e.what() << "\n";
    return 1;
  }
}

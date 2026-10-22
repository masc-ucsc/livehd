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

#include "pass/place/density_warp.hpp"
#include "rapidjson/document.h"
#include "rapidjson/prettywriter.h"
#include "rapidjson/stringbuffer.h"
#include "zlib.h"

namespace warp {
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
            Point clip_lo{std::max(lo.x, face_lo.x), std::max(lo.y, face_lo.y)},
                clip_hi{std::min(hi.x, face_hi.x), std::min(hi.y, face_hi.y)};
            if (clip_lo.x >= clip_hi.x || clip_lo.y >= clip_hi.y) {
              continue;
            }
            std::array<Point, 4> source = {
                clip_lo,
                Point{clip_hi.x, clip_lo.y},
                clip_hi,
                Point{clip_lo.x, clip_hi.y}
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
  auto   result         = density_warp(before, mass, o);
  auto&  after          = result.after;
  auto&  m              = result.mesh;
  auto&  passes         = result.passes;
  auto&  occupied       = result.occupied;
  double free_total     = std::accumulate(result.free.begin(), result.free.end(), 0.);
  auto&  target         = result.target;
  auto&  target_density = result.target_density;
  double die_area       = before.width() * before.height();
  auto&  out_mass       = result.out_mass;
  auto&  out_occupied   = result.out_occupied;
  auto&  a              = result.a;
  auto&  b              = result.b;
  double total = result.total, out_total = result.out_total;
  double total_occupied = result.total_occupied, transported_occupied = result.transported_occupied;

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

// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "route_tech.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <sstream>
#include <stdexcept>

#include "inou/lefdef/tech_profile.hpp"

namespace ph = livehd::physical;

namespace livehd::routing {
namespace {
std::vector<std::string> words(const std::string& text) {
  std::istringstream       in(text);
  std::vector<std::string> out;
  for (std::string w; in >> w;) {
    out.push_back(w);
  }
  return out;
}
std::optional<double> number(const std::string& w) {
  try {
    size_t     used = 0;
    const auto v    = std::stod(w, &used);
    return used == w.size() ? std::optional<double>(v) : std::nullopt;
  } catch (const std::exception&) {
    return std::nullopt;
  }
}
// The number right after each occurrence of `key` in the texts of one LEF58 property.
std::vector<double> after(const ph::Metal_rules& rules, const std::string& property, const std::string& key, size_t skip = 0) {
  std::vector<double> out;
  const auto          it = rules.properties.find(property);
  if (it == rules.properties.end()) {
    return out;
  }
  for (const auto& text : it->second) {
    const auto w = words(text);
    for (size_t i = 0; i + 1 + skip < w.size(); ++i) {
      if (w[i] == key) {
        if (const auto v = number(w[i + 1 + skip])) {
          out.push_back(*v);
        }
      }
    }
  }
  return out;
}
struct Up {
  int64_t dbu, grid;
  // Microns to database units, rounded up to the manufacturing grid.
  int operator()(double um) const {
    const auto v = int64_t(std::ceil(um * double(dbu) - 1e-6));
    return int((v + grid - 1) / grid * grid);
  }
};
}  // namespace

Tech_stack derive_tech_stack(const ph::Technology& tech, int64_t dbu,
                             const std::function<bool(const std::string&, bool)>& has_tracks) {
  Tech_stack out;
  out.grid = int(ph::manufacturing_grid_dbu(tech, dbu).value_or(1));
  const Up   up{dbu, out.grid};
  const auto at = [&](double um) { return int(std::llround(um * double(dbu))); };
  if (tech.routing_layers.size() < 2) {
    throw std::runtime_error("the technology LEF has no routing layer above the pin layer");
  }
  // Which layers: a registered profile states what the reference flow routes on (ASAP7 stops at M7, below the two
  // thick layers). Without one, the lowest routing layer holds the pins and every layer above it with tracks is used.
  std::vector<std::string> names;
  const ph::Tech_profile*  profile = nullptr;
  try {
    profile = ph::detect_tech_profile(tech, ph::Tech_policy::route);
  } catch (const std::invalid_argument&) {
    profile = nullptr;
  }
  if (profile != nullptr) {
    out.pin_layer = profile->route.access.pin;
    names         = profile->route.signal_layers;
  } else {
    out.pin_layer = tech.routing_layers.front();
    for (size_t i = 1; i < tech.routing_layers.size(); ++i) {
      const auto& name  = tech.routing_layers[i];
      const auto  layer = tech.layers.find(name);
      if (layer == tech.layers.end() || (layer->second.direction != "HORIZONTAL" && layer->second.direction != "VERTICAL")) {
        break;
      }
      if (!has_tracks(name, layer->second.direction == "HORIZONTAL")) {
        if (names.size() < 2) {  // the stack cannot end here: say which tracks are missing
          throw std::runtime_error("the frame has no " + std::string(layer->second.direction == "HORIZONTAL" ? "horizontal" : "vertical")
                                   + " tracks for " + name + ": the router needs two routed layers above pin layer "
                                   + out.pin_layer);
        }
        break;
      }
      names.push_back(name);
    }
  }
  if (names.empty() || !tech.layers.contains(out.pin_layer)) {
    throw std::runtime_error("no routable layer with tracks above pin layer " + out.pin_layer);
  }
  for (const auto& name : names) {
    const auto found = tech.layers.find(name);
    if (found == tech.layers.end()) {
      throw std::runtime_error("routing layer " + name + " is not in the technology LEF");
    }
    const auto& layer = found->second;
    const auto& rules = layer.rules;
    Tech_layer  ly;
    ly.name  = name;
    ly.horiz = layer.direction == "HORIZONTAL";
    ly.w     = at(layer.width);
    if (ly.w <= 0 || ly.w % 2 != 0) {
      throw std::runtime_error("routing layer " + name + " needs an even default width in database units");
    }
    ly.pitch = at(ly.horiz ? layer.pitch_y : layer.pitch_x);
    // Ordinary spacing of default-width wires.
    double spacing = 0;
    for (const auto& s : rules.spacing) {
      if (!s.range && !s.eol && s.distance > 0) {
        spacing = spacing == 0 ? s.distance : std::min(spacing, s.distance);
      }
    }
    if (spacing == 0 && !rules.parallel.empty() && !rules.parallel.front().spacing.empty()
        && !rules.parallel.front().spacing.front().empty()) {
      spacing = rules.parallel.front().spacing.front().front();
    }
    ly.spacing = spacing > 0 ? up(spacing) : ly.w;
    // Ends on one track: the largest of the ordinary spacing, an end-of-line spacing, an end-to-end spacing and the
    // forward reach of an end-of-line keep-out.
    double e2e = spacing;
    for (const auto& s : rules.spacing) {
      if (s.eol) {
        e2e = std::max(e2e, s.distance);
      }
    }
    for (double v : after(rules, "LEF58_SPACING", "ENDTOEND")) {
      e2e = std::max(e2e, v);
    }
    for (double v : after(rules, "LEF58_EOLKEEPOUT", "EXTENSION", 2)) {
      e2e = std::max(e2e, v);
    }
    ly.e2e = std::max(ly.spacing, up(e2e));
    // Facing ends on neighbouring tracks: a corner spacing rule, or what the ordinary spacing leaves to cover when the
    // tracks are closer than a wire plus the spacing.
    double corner = 0;
    {
      const auto it = rules.properties.find("LEF58_CORNERSPACING");
      if (it != rules.properties.end()) {
        for (const auto& text : it->second) {
          const auto w = words(text);
          for (size_t i = 0; i + 3 < w.size(); ++i) {
            if (w[i] == "WIDTH" && w[i + 2] == "SPACING") {
              corner = std::max(corner, number(w[i + 3]).value_or(0));
            }
          }
        }
      }
    }
    if (corner > 0) {
      ly.cs = up(corner);
    } else {
      const int gap = ly.pitch - ly.w;
      ly.cs         = gap >= ly.spacing ? 0 : up(std::sqrt(double(ly.spacing) * ly.spacing - double(gap) * gap) / double(dbu));
    }
    // Shortest piece: minimum area over the width, or the long side of a minimum size.
    double longest = 0;
    for (const auto& size : rules.min_size) {
      longest = std::max(longest, std::max(size[0], size[1]));
    }
    ly.minlen   = std::max({ly.w, up(longest), rules.area > 0 ? up(rules.area / layer.width) : 0});
    ly.min_area = int64_t(std::llround(rules.area * double(dbu) * double(dbu)));
    ly.ext    = longest > 0 ? up(longest / 2) : 0;  // a via landing alone is a rectangle of at least the minimum size
    // Spacing from wider metal: the largest entry of the width tables, and never less than a wire width more.
    double wide = 0;
    for (const auto& table : rules.parallel) {
      for (size_t i = 0; i < table.spacing.size(); ++i) {
        double most = 0;
        for (double v : table.spacing[i]) {
          most = std::max(most, v);
        }
        wide = std::max(wide, most);
        if (i < table.widths.size()) {
          ly.width_spacing.emplace_back(at(table.widths[i]), up(most));
        }
      }
    }
    std::sort(ly.width_spacing.begin(), ly.width_spacing.end());
    for (const auto& s : rules.spacing) {
      if (s.range) {
        wide = std::max(wide, s.distance);
      }
    }
    ly.wide = std::max(2 * ly.w, up(wide));
    out.layers.push_back(std::move(ly));
  }
  // The default via of each pair of neighbouring layers: one cut, with the narrowest pads across the wires.
  const auto direction = [&](const std::string& name) { return tech.layers.at(name).direction == "HORIZONTAL"; };
  for (size_t k = 0; k < out.layers.size(); ++k) {
    const std::string lower = k == 0 ? out.pin_layer : out.layers[k - 1].name;
    const std::string upper = out.layers[k].name;
    const bool        lower_h = direction(lower), upper_h = direction(upper);
    std::optional<Tech_cut> best;
    double                  best_score = 0;
    for (const auto& [name, via] : tech.vias) {
      if (!via.is_default || !via.unsupported.empty()) {
        continue;
      }
      std::optional<ph::Rect> lo, hi, cut;
      std::string             cut_layer;
      bool                    plain = true;
      for (const auto& s : via.shapes) {
        auto& slot = s.layer == lower ? lo : s.layer == upper ? hi : cut;
        if (s.layer != lower && s.layer != upper) {
          if (!tech.cut_layers.contains(s.layer)) {
            plain = false;
            break;
          }
          cut_layer = s.layer;
        }
        if (slot) {
          plain = false;  // more than one rectangle on a layer: not a single-cut via
          break;
        }
        slot = s.rect;
      }
      if (!plain || !lo || !hi || !cut) {
        continue;
      }
      const double score = (lower_h ? std::max(-lo->yl, lo->yh) : std::max(-lo->xl, lo->xh))
                           + (upper_h ? std::max(-hi->yl, hi->yh) : std::max(-hi->xl, hi->xh));
      if (best && score >= best_score - 1e-9) {
        continue;
      }
      Tech_cut c;
      c.name  = cut_layer;
      c.via   = name;
      c.lower = lower;
      c.upper = upper;
      c.hx    = at(cut->xh);
      c.hy    = at(cut->yh);
      if (at(-cut->xl) != c.hx || at(-cut->yl) != c.hy || c.hx <= 0 || c.hy <= 0) {
        continue;  // the model keeps a cut by its centre and half sizes
      }
      const auto scaled = [&](const ph::Rect& r) {
        return ph::Rect{double(at(r.xl)), double(at(r.yl)), double(at(r.xh)), double(at(r.yh))};
      };
      c.lower_pad = scaled(*lo);
      c.upper_pad = scaled(*hi);
      best        = std::move(c);
      best_score  = score;
    }
    if (!best) {
      throw std::runtime_error("the technology LEF has no default single-cut via between " + lower + " and " + upper);
    }
    const auto& rule  = tech.cut_layers.at(best->name);
    double      space = 0;
    for (double v : rule.spacing) {
      space = std::max(space, v);
    }
    if (const auto it = rule.properties.find("LEF58_SPACINGTABLE"); it != rule.properties.end()) {
      for (const auto& text : it->second) {
        const auto w = words(text);
        for (size_t i = 0; i + 1 < w.size(); ++i) {
          if (w[i] == "DEFAULT") {
            space = std::max(space, number(w[i + 1]).value_or(0));
          }
        }
      }
    }
    best->space = space > 0 ? up(space) : 2 * std::max(best->hx, best->hy);
    out.cuts.push_back(std::move(*best));
  }
  // What the vias add to a wire: their metal along it (the wire runs that far past the via centre) and beside it.
  std::vector<int> floor_ext;
  for (const auto& ly : out.layers) {
    floor_ext.push_back(ly.ext);
  }
  const auto pads = [&] {
    for (size_t l = 0; l < out.layers.size(); ++l) {
      out.layers[l].ext  = floor_ext[l];
      out.layers[l].bump = 0;
    }
    for (size_t k = 0; k < out.cuts.size(); ++k) {
      const auto& c    = out.cuts[k];
      const auto  take = [&](Tech_layer& ly, const ph::Rect& pad) {
        const int along  = int(ly.horiz ? std::max(-pad.xl, pad.xh) : std::max(-pad.yl, pad.yh));
        const int across = int(ly.horiz ? std::max(-pad.yl, pad.yh) : std::max(-pad.xl, pad.xh));
        ly.ext           = std::max(ly.ext, (along + out.grid - 1) / out.grid * out.grid);
        ly.bump          = std::max(ly.bump, across - ly.w / 2);
      };
      take(out.layers[k], c.upper_pad);
      if (k > 0) {
        take(out.layers[k - 1], c.lower_pad);
      }
    }
    for (auto& ly : out.layers) {
      ly.bump = std::max(0, (ly.bump + out.grid - 1) / out.grid * out.grid);
    }
  };
  pads();
  // A via pad wider than its wire must still leave the spacing to a wire on the next track. Where the pad of the
  // topmost via does not (sky130: the via to met5 has a 1.18 um pad on met4, whose tracks are 0.92 um apart), the
  // model cannot place that via, and the top layer is left unused.
  const auto fits = [&](const Tech_layer& ly) { return ly.pitch <= 0 || ly.pitch - ly.w - ly.bump >= ly.spacing; };
  while (out.layers.size() > 2 && (!fits(out.layers.back()) || !fits(out.layers[out.layers.size() - 2]))) {
    out.unused.push_back(out.layers.back().name);
    out.layers.pop_back();
    out.cuts.pop_back();
    floor_ext.pop_back();
    pads();
  }
  return out;
}
}  // namespace livehd::routing

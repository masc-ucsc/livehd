// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "chunk_templates.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>
#include <tuple>

namespace livehd::placement {
namespace {
using Json = rapidjson::Value;
using Pin  = std::pair<int, double>;  // left, right, bottom, top; normalized tangential offset
void require(bool ok, const std::string& message) {
  if (!ok) {
    throw std::invalid_argument("chunk implementations: " + message);
  }
}
std::string string_field(const Json& j, const char* key) {
  require(j.IsObject() && j.HasMember(key) && j[key].IsString() && j[key].GetStringLength(), std::string("missing ") + key);
  return j[key].GetString();
}
const Json& array(const Json& j, const char* key) {
  require(j[key].IsArray(), std::string(key) + " must be an array");
  return j[key];
}
double      dim(const Chunk_box& b, int k) { return b[k + 2] - b[k]; }
double      area(const Chunk_box& b) { return dim(b, 0) * dim(b, 1); }
double      aspect(const Chunk_box& b) { return dim(b, 0) / dim(b, 1); }
Chunk_point center(const Chunk_box& b) { return {(b[0] + b[2]) / 2, (b[1] + b[3]) / 2}; }
double      median(std::vector<double> v) {
  std::sort(v.begin(), v.end());
  return (v[(v.size() - 1) / 2] + v[v.size() / 2]) / 2;
}
struct Bounds {
  Chunk_point lo{INFINITY, INFINITY}, hi{-INFINITY, -INFINITY};
  void        add(Chunk_point p) {
    for (int k = 0; k < 2; ++k) {
      lo[k] = std::min(lo[k], p[k]);
      hi[k] = std::max(hi[k], p[k]);
    }
  }
  double length() const { return hi[0] - lo[0] + hi[1] - lo[1]; }
};
double hpwl(const Interface_net& w, const Chunk_plan& p, bool pins) {
  Bounds b;
  if (w.terminal_bounds) {
    const auto& box = *w.terminal_bounds;
    b.add({box[0], box[1]});
    b.add({box[2], box[3]});
  } else if (w.terminal) {
    b.add(*w.terminal);
  }
  for (const auto& t : w.contacts) {
    b.add(pins ? t.xy : center(p.chunks[t.home].box));
  }
  return b.length();
}
Chunk_point boundary(const Chunk_box& b, Pin pin) {
  const auto [s, f] = pin;
  const int   k = s / 2, j = 1 - k;
  Chunk_point p{};
  p[k] = b[k + 2 * (s % 2)];
  p[j] = std::clamp(std::round(b[j] + f * dim(b, j)), b[j], b[j + 2]);
  return p;
}
// Intersect the center-to-neighbor ray with the perimeter. Corners retain a deterministic side.
Pin facing(const Chunk_box& b, Chunk_point dest) {
  auto   c  = center(b);
  double dx = (dest[0] - c[0]) / dim(b, 0), dy = (dest[1] - c[1]) / dim(b, 1);
  if (dx == 0 && dy == 0) {
    return {1, .5};
  }
  if (std::abs(dx) >= std::abs(dy)) {
    return {dx >= 0 ? 1 : 0, std::clamp(.5 + .5 * dy / std::abs(dx), 0., 1.)};
  }
  return {dy >= 0 ? 3 : 2, std::clamp(.5 + .5 * dx / std::abs(dy), 0., 1.)};
}
using Families = std::map<std::string, std::vector<int>>;
using Group    = std::pair<std::string, std::string>;
using Groups   = std::map<Group, std::vector<int>>;
using Tap      = std::pair<size_t, size_t>;
using Roles    = std::map<std::string, Tap>;
double target(const Chunk_plan& p, const std::vector<int>& copies, const Template_form* form) {
  if (form && form->aspect > 0) {
    return form->aspect;
  }
  std::vector<double> v;
  for (int i : copies) {
    v.push_back(std::log(aspect(p.chunks[i].box)));
  }
  return std::exp(median(std::move(v)));
}
const Template_form* form_for(const Template_rules& rules, const Group& g) {
  auto i = rules.find(g.first);
  if (i == rules.end()) {
    return nullptr;
  }
  auto f = i->second.forms.find(g.second);
  return f == i->second.forms.end() ? nullptr : &f->second;
}
// Recut only pairs whose union is a rectangle. Bound movement by initial capacity,
// freeze hard objects/IO, and never buy an aspect improvement by increasing center HPWL.
void shape(Chunk_plan& p, const Groups& groups, const Template_rules& rules, const std::vector<Chunk_object>& objects,
           const std::vector<Interface_net>& wires, Chunk_box core, Chunk_point pitch) {
  const auto                       n = p.chunks.size();
  std::vector<double>              desired(n), base(n), lower(n);
  std::vector<bool>                locked(n);
  std::vector<Chunk_point>         min_size(n);
  std::vector<std::vector<size_t>> incident(n);
  for (size_t i = 0; i < n; ++i) {
    const auto& c = p.chunks[i];
    base[i]       = area(c.box);
    lower[i]      = std::min(base[i], c.reserved);
    for (int j : c.objects) {
      const auto& o  = objects[j];
      locked[i]      = locked[i] || o.fixed.has_value() || o.macro;
      min_size[i][0] = std::max(min_size[i][0], o.width);
      min_size[i][1] = std::max(min_size[i][1], o.height);
    }
  }
  for (size_t w = 0; w < wires.size(); ++w) {
    for (const auto& t : wires[w].contacts) {
      incident[t.home].push_back(w);
      if (wires[w].terminal) {
        locked[t.home] = true;
      }
    }
  }
  for (const auto& [g, copies] : groups) {
    const auto* form = form_for(rules, g);
    if (copies.size() < 2 && !(form && form->aspect > 0)) {
      continue;
    }
    auto a = target(p, copies, form);
    for (int i : copies) {
      desired[i] = a;
    }
  }
  auto loss = [&](int i, const Chunk_box& b) {
    return std::abs(std::log(aspect(b) / desired[i])) + .5 * std::abs(std::log(area(b) / base[i]));
  };
  for (int round = 0; round < 6; ++round) {
    bool changed = false;
    using Edge   = std::tuple<int, double, double, double>;
    std::map<Edge, int> ends;
    for (size_t i = 0; i < n; ++i) {
      if (desired[i] && !locked[i]) {
        const auto& b = p.chunks[i].box;
        for (int k = 0; k < 2; ++k) {
          ends[{k, b[1 - k], b[3 - k], b[k + 2]}] = i;
        }
      }
    }
    for (size_t i = 0; i < n; ++i) {
      if (desired[i] && !locked[i]) {
        for (int k = 0; k < 2; ++k) {
          auto& bi = p.chunks[i].box;
          auto  it = ends.find({k, bi[1 - k], bi[3 - k], bi[k]});
          if (it == ends.end()) {
            continue;
          }
          int   j  = it->second;
          auto& bj = p.chunks[j].box;
          if (bj[k + 2] != bi[k] || bj[1 - k] != bi[1 - k] || bj[3 - k] != bi[3 - k]) {
            continue;
          }
          Chunk_box oi = bi, oj = bj, u = bj;
          u[k + 2] = bi[k + 2];
          std::vector<size_t> touched;
          std::set_union(incident[i].begin(),
                         incident[i].end(),
                         incident[j].begin(),
                         incident[j].end(),
                         std::back_inserter(touched));
          auto length = [&]() {
            double sum = 0;
            for (auto w : touched) {
              sum += hpwl(wires[w], p, false);
            }
            return sum;
          };
          double old_wire = length(), best = loss(i, oi) + loss(j, oj);
          auto   ib = oi, jb = oj;
          for (int axis = 0; axis < 2; ++axis) {
            for (bool inv : {false, true}) {
              for (double shift : {0., -.06, .06}) {
                const double frac = (inv ? area(oi) : area(oj)) / area(u) + shift;
                const double cut
                    = core[axis] + pitch[axis] * std::round((u[axis] - core[axis] + dim(u, axis) * frac) / pitch[axis]);
                if (cut <= u[axis] || cut >= u[axis + 2]) {
                  continue;
                }
                auto left = u, right = u;
                left[axis + 2] = right[axis] = cut;
                auto a = inv ? left : right, b = inv ? right : left;
                auto fits = [&](size_t id, const Chunk_box& box) {
                  return area(box) >= std::max(lower[id], .90 * base[id]) && area(box) <= 1.10 * base[id]
                         && dim(box, 0) >= std::max(pitch[0], min_size[id][0])
                         && dim(box, 1) >= std::max(pitch[1], min_size[id][1]);
                };
                if (!fits(i, a) || !fits(j, b)) {
                  continue;
                }
                double cost = loss(i, a) + loss(j, b);
                if (cost >= best - 1e-8) {
                  continue;
                }
                bi = a;
                bj = b;
                if (length() <= old_wire) {
                  ib   = a;
                  jb   = b;
                  best = cost;
                }
                bi = oi;
                bj = oj;
              }
            }
          }
          if (ib != oi) {
            bi      = ib;
            bj      = jb;
            changed = true;
          }
        }
      }
    }
    if (!changed) {
      break;
    }
  }
}
}  // namespace

Template_rules read_template_rules(const metadata::Bundle* bundle, const std::string& design) {
  Template_rules out;
  if (!bundle) {
    return out;
  }
  for (auto* r : bundle->select(chunk_guidance_class, design)) {
    if (!r->HasMember("implementations")) {
      continue;
    }
    for (const auto& entry : array(*r, "implementations").GetArray()) {
      auto family        = string_field(entry, "partition");
      auto [i, new_rule] = out.try_emplace(family);
      require(new_rule, "duplicate partition " + family);
      auto& rule = i->second;
      if (entry.HasMember("mode")) {
        rule.mode = string_field(entry, "mode");
      }
      require(rule.mode == "auto" || rule.mode == "shared" || rule.mode == "separate", "unknown mode " + rule.mode);
      if (!entry.HasMember("groups")) {
        continue;
      }
      for (const auto& group : array(entry, "groups").GetArray()) {
        auto name = string_field(group, "name");
        require(name != "shared" && !name.starts_with("auto:") && !name.starts_with("instance:"), "reserved group name " + name);
        auto [j, fresh] = rule.forms.try_emplace(name);
        require(fresh, "duplicate group " + name);
        auto& f = j->second;
        f.name  = name;
        require(group.HasMember("instances") && !array(group, "instances").Empty(), "empty group " + name);
        for (const auto& id : group["instances"].GetArray()) {
          require(id.IsString() && id.GetStringLength(), "invalid instance ID");
          require(rule.instances.emplace(id.GetString(), name).second, "instance listed twice: " + std::string(id.GetString()));
        }
        if (group.HasMember("aspect_ratio")) {
          const auto& v = group["aspect_ratio"];
          require(v.IsNumber() && std::isfinite(v.GetDouble()) && v.GetDouble() > 0, "invalid aspect ratio");
          f.aspect = v.GetDouble();
        }
        if (!group.HasMember("pins")) {
          continue;
        }
        for (const auto& pin : array(group, "pins").GetArray()) {
          auto                                  role = string_field(pin, "role"), side = string_field(pin, "side");
          static const std::vector<std::string> sides{"left", "right", "bottom", "top"};
          auto                                  s = std::find(sides.begin(), sides.end(), side);
          require(s != sides.end(), "unknown pin side " + side);
          require(pin.HasMember("offset") && pin["offset"].IsNumber() && pin["offset"].GetDouble() >= 0
                      && pin["offset"].GetDouble() <= 1,
                  "invalid pin offset");
          require(f.pins.emplace(role, Pin{s - sides.begin(), pin["offset"].GetDouble()}).second, "duplicate pin role " + role);
        }
      }
    }
  }
  return out;
}

// Match contacts once, then score an orientation using each affected hypernet once.
// Do not promote an unmatched contact to a pin just because two bus widths agree.
static void copy_interfaces(Chunk_plan& p, std::vector<Interface_net>& wires, const std::vector<Interface_copy_rule>& rules,
                            const Contact_keys& resolver, const Template_rules& templates) {
  if (rules.empty()) {
    return;
  }
  std::map<std::string, int> ids;
  for (size_t h = 0; h < p.chunks.size(); ++h) {
    ids.emplace(p.chunks[h].id, h);
  }
  std::set<int> used, targets, sources;
  for (const auto& r : rules) {
    require(ids.contains(r.source) && ids.contains(r.target), "unknown interface copy home: " + r.source + " -> " + r.target);
    auto s = ids.at(r.source), t = ids.at(r.target);
    require(s != t && targets.insert(t).second, "each interface target needs exactly one other source: " + r.target);
    sources.insert(s);
    used.insert(s);
    used.insert(t);
    auto& c    = p.chunks[t];
    auto  form = form_for(templates, {c.partition, c.implementation});
    require(!form || form->pins.empty(), "target also has explicit implementation pins: " + r.target);
  }
  for (int t : targets) {
    require(!sources.contains(t), "an interface source cannot itself be a copy: " + p.chunks[t].id);
  }
  Contact_taps             refs;
  std::vector<Tap>         taps;
  std::vector<std::string> keys;
  for (size_t i = 0; i < wires.size(); ++i) {
    for (size_t j = 0; j < wires[i].contacts.size(); ++j) {
      const auto& c = wires[i].contacts[j];
      if (!used.contains(c.home)) {
        continue;
      }
      refs.emplace_back(wires[i].index, c.home);
      taps.emplace_back(i, j);
      if (!resolver) {
        keys.push_back(c.role.starts_with("pin:") ? c.role : "");
      }
    }
  }
  if (resolver) {
    keys = resolver(p, refs);
  }
  require(keys.size() == refs.size(), "incomplete endpoint identity data");
  std::map<int, Roles>  maps;
  std::map<int, size_t> totals;
  for (size_t k = 0; k < keys.size(); ++k) {
    int h = refs[k].second;
    ++totals[h];
    if (keys[k].empty()) {
      continue;
    }
    auto [it, fresh] = maps[h].try_emplace(std::move(keys[k]), taps[k]);
    if (!fresh) {
      it->second = {SIZE_MAX, 0};  // an ambiguous role never authorizes a copy
    }
  }
  struct Binding {
    Tap         target;
    Chunk_point uv;
  };
  struct Trial {
    int                  target;
    std::vector<Binding> matches;
    std::set<size_t>     nets;
  };
  std::vector<Trial> trials;
  for (const auto& r : rules) {
    int         h = ids.at(r.source), t = ids.at(r.target);
    const auto& b     = p.chunks[h].box;
    auto&       trial = trials.emplace_back(Trial{t, {}, {}});
    for (const auto& [key, tap] : maps[t]) {
      auto it = maps[h].find(key);
      if (tap.first == SIZE_MAX || it == maps[h].end() || it->second.first == SIZE_MAX) {
        continue;
      }
      auto [n, j]                                           = it->second;
      wires[tap.first].contacts[tap.second].copy_source_net = wires[n].index;
      auto xy                                               = wires[n].contacts[j].xy;
      trial.matches.push_back({
          tap,
          {(xy[0] - b[0]) / dim(b, 0), (xy[1] - b[1]) / dim(b, 1)}
      });
      trial.nets.insert(tap.first);
    }
    require(!trial.matches.empty(), "no unambiguous corresponding boundary pins: " + r.source + " -> " + r.target);
    p.chunks[t].interface_copy = Interface_copy{r.source,
                                                r.orientation,
                                                "N",
                                                resolver ? "local_cell_master_pin" : "named_driver",
                                                trial.matches.size(),
                                                totals[h],
                                                totals[t]};
  }
  auto put = [&](const Trial& trial, std::string_view ori) {
    const auto& b = p.chunks[trial.target].box;
    for (const auto& m : trial.matches) {
      auto uv = m.uv;
      if (ori == "FN" || ori == "S") {
        uv[0] = 1 - uv[0];
      }
      if (ori == "FS" || ori == "S") {
        uv[1] = 1 - uv[1];
      }
      auto& xy = wires[m.target.first].contacts[m.target.second].xy;
      for (int k = 0; k < 2; ++k) {
        xy[k] = std::clamp(std::round(b[k] + dim(b, k) * uv[k]), b[k], b[k + 2]);
      }
    }
  };
  // Start from all authored orientations, then use bounded coordinate descent
  // on coupled auto targets. Stable targets and strict improvements avoid ties oscillating.
  std::sort(trials.begin(), trials.end(), [&](const auto& a, const auto& b) {
    return p.chunks[a.target].id < p.chunks[b.target].id;
  });
  for (auto& t : trials) {
    auto& c = *p.chunks[t.target].interface_copy;
    if (c.requested != "auto") {
      c.orientation = c.requested;
    }
    put(t, c.orientation);
  }
  for (int round = 0; round < 8; ++round) {
    bool changed = false;
    for (auto& t : trials) {
      auto& c = *p.chunks[t.target].interface_copy;
      if (c.requested != "auto") {
        continue;
      }
      const auto cost = [&] {
        double sum = 0;
        for (auto n : t.nets) {
          sum += hpwl(wires[n], p, true);
        }
        return sum;
      };
      auto   chosen = c.orientation;
      double best   = cost();
      for (const char* ori : {"N", "FN", "FS", "S"}) {
        put(t, ori);
        double v = cost();
        if (v < best) {
          chosen = ori;
          best   = v;
        }
      }
      changed       |= chosen != c.orientation;
      c.orientation  = chosen;
      put(t, chosen);
    }
    if (!changed) {
      break;
    }
  }
}
std::vector<Interface_copy_rule> read_interface_copies(const metadata::Bundle* b, const std::string& design) {
  std::vector<Interface_copy_rule> out;
  if (b) {
    for (const auto* r : b->select(chunk_guidance_class, design)) {
      if (r->HasMember("interface_copies")) {
        for (const auto& j : array(*r, "interface_copies").GetArray()) {
          auto s = string_field(j, "source"), t = string_field(j, "target"), o = string_field(j, "orientation");
          require(o == "auto" || o == "N" || o == "FN" || o == "FS" || o == "S",
                  "use auto, N, FN, FS or S for a row-based interface");
          out.push_back({s, t, o});
        }
      }
    }
  }
  return out;
}

void refine_templates(Chunk_plan& p, std::vector<Interface_net>& wires, const std::vector<Chunk_object>& objects,
                      const Template_rules& rules, Chunk_box core, Chunk_point pitch, bool move_boxes, const Chunk_edits& edits,
                      const std::vector<Interface_copy_rule>& copies_requested, const Contact_keys& resolver) {
  Families families;
  for (size_t i = 0; i < p.chunks.size(); ++i) {
    families[p.chunks[i].partition].push_back(i);
  }
  for (const auto& [id, r] : rules) {
    require(families.contains(id), "unknown partition " + id);
    std::set<std::string> ids;
    for (int i : families.at(id)) {
      ids.insert(p.chunks[i].id);
    }
    for (const auto& [instance, _] : r.instances) {
      require(ids.contains(instance), "instance " + instance + " does not belong to " + id);
    }
  }
  Groups broad;
  for (const auto& [family, copies] : families) {
    const Template_rule empty;
    const auto&         rule = rules.contains(family) ? rules.at(family) : empty;
    for (int i : copies) {
      auto& c               = p.chunks[i];
      c.implementation_mode = rule.mode;
      if (rule.instances.contains(c.id)) {
        c.implementation      = rule.instances.at(c.id);
        c.implementation_mode = "named";
      } else if (rule.mode == "separate") {
        c.implementation = "instance:" + c.id;
      } else {
        c.implementation = rule.mode == "auto" ? "auto:1" : "shared";
      }
      broad[{family, c.implementation}].push_back(i);
    }
  }
  if (move_boxes) {
    shape(p, broad, rules, objects, wires, core, pitch);
  }
  edit_chunk_boxes(p, edits, objects, core, pitch);
  std::vector<Roles>                 roles(p.chunks.size());
  std::vector<std::set<std::string>> ambiguous(p.chunks.size());
  std::vector<std::vector<Pin>>      natural(wires.size());
  for (size_t n = 0; n < wires.size(); ++n) {
    auto&       w   = wires[n];
    Chunk_point sum = w.terminal.value_or(Chunk_point{});
    for (const auto& t : w.contacts) {
      auto c = center(p.chunks[t.home].box);
      for (int k = 0; k < 2; ++k) {
        sum[k] += c[k];
      }
    }
    for (size_t j = 0; j < w.contacts.size(); ++j) {
      auto& t    = w.contacts[j];
      auto& b    = p.chunks[t.home].box;
      auto  dest = center(b);
      for (int k = 0; k < 2; ++k) {
        dest[k] = (sum[k] - dest[k]) / (w.contacts.size() + bool(w.terminal) - 1);
      }
      auto pin = facing(b, dest);
      natural[n].push_back(pin);
      t.xy = boundary(b, pin);
      if (!t.role.empty() && !roles[t.home].emplace(t.role, Tap{n, j}).second) {
        ambiguous[t.home].insert(t.role);
      }
    }
  }
  for (auto& w : wires) {
    for (auto& t : w.contacts) {
      if (ambiguous[t.home].contains(t.role)) {
        roles[t.home].erase(t.role);
        t.role.clear();
      }
    }
  }
  // Average normalized pin distance and aspect bound reuse. First-fit in stable instance order;
  // bound representative comparisons for irregular families instead of quadratic all-pairs pairing.
  auto unit = [](Pin pin) {
    const auto [s, f] = pin;
    return s < 2 ? Chunk_point{double(s), f} : Chunk_point{f, double(s - 2)};
  };
  for (const auto& [key, copies] : broad) {
    if (p.chunks[copies.front()].implementation_mode != "auto") {
      continue;
    }
    std::vector<int> reps;
    for (int i : copies) {
      auto&  c     = p.chunks[i];
      size_t match = 0;
      for (; match < reps.size(); ++match) {
        int         j     = reps[match];
        const auto& other = p.chunks[j];
        if (roles[i].size() != roles[j].size() || std::abs(std::log(aspect(c.box) / aspect(other.box))) > std::log(2.25)) {
          continue;
        }
        double distance   = 0;
        bool   compatible = true;
        for (const auto& [role, tap] : roles[i]) {
          auto it = roles[j].find(role);
          if (it == roles[j].end()) {
            compatible = false;
            break;
          }
          auto [n, a] = tap;
          auto [m, b] = it->second;
          auto x = unit(natural[n][a]), y = unit(natural[m][b]);
          distance += std::abs(x[0] - y[0]) + std::abs(x[1] - y[1]);
        }
        if (compatible && distance <= .8 * roles[i].size()) {
          break;
        }
      }
      if (match == reps.size()) {
        if (reps.size() >= 32) {
          c.implementation = "instance:" + c.id;
          continue;
        }
        reps.push_back(i);
      }
      c.implementation = "auto:" + std::to_string(match + 1);
    }
  }
  Groups groups;
  for (size_t i = 0; i < p.chunks.size(); ++i) {
    auto& c = p.chunks[i];
    groups[{c.partition, c.implementation}].push_back(i);
  }
  struct Slot {
    std::vector<Tap>    taps;
    std::vector<size_t> nets;
    std::vector<Pin>    candidates;
    std::optional<Pin>  fixed, current;
  };
  std::vector<Slot> slots;
  for (const auto& [g, copies] : groups) {
    const auto*                             f  = form_for(rules, g);
    double                                  ar = target(p, copies, f);
    std::map<std::string, std::vector<Tap>> same;
    for (int i : copies) {
      p.chunks[i].target_aspect = ar;
      for (const auto& [role, t] : roles[i]) {
        same[role].push_back(t);
      }
    }
    if (f) {
      for (const auto& [role, _] : f->pins) {
        require(same.contains(role) && same.at(role).size() == copies.size(), "unmatched group pin " + role);
      }
    }
    for (const auto& [role, taps] : same) {
      const bool fixed = f && f->pins.contains(role);
      if (!fixed && taps.size() < 2) {
        continue;
      }
      auto& s = slots.emplace_back();
      s.taps  = taps;
      if (fixed) {
        s.fixed = f->pins.at(role);
      }
      std::vector<double> xx, yy;
      for (auto [n, j] : taps) {
        s.nets.push_back(n);
        auto q = unit(natural[n][j]);
        xx.push_back(q[0]);
        yy.push_back(q[1]);
      }
      std::sort(s.nets.begin(), s.nets.end());
      s.nets.erase(std::unique(s.nets.begin(), s.nets.end()), s.nets.end());
      double x = median(std::move(xx)), y = median(std::move(yy));
      for (int side = 0; side < 4; ++side) {
        for (double f0 : {side < 2 ? y : x, .125, .375, .5, .625, .875, 0., 1.}) {
          s.candidates.emplace_back(side, f0);
        }
      }
    }
  }
  auto put = [&](Tap tap, Pin pin) {
    auto [n, j] = tap;
    auto& t     = wires[n].contacts[j];
    t.xy        = boundary(p.chunks[t.home].box, pin);
  };
  // Coordinates for a role move together. Count a touched hypernet once even when
  // both endpoints are in the group. Penalize unnecessary movement from its natural contact.
  for (int round = 0; round < 12; ++round) {
    ++p.pin_sweeps;
    bool changed = false;
    for (auto& s : slots) {
      if (s.fixed) {
        if (!s.current) {
          for (auto t : s.taps) {
            put(t, *s.fixed);
          }
          s.current = s.fixed;
          changed   = true;
        }
        continue;
      }
      auto score = [&](Pin pin) {
        double v = 0;
        for (auto t : s.taps) {
          put(t, pin);
          auto [n, j]    = t;
          auto& contact  = wires[n].contacts[j];
          auto  q        = boundary(p.chunks[contact.home].box, natural[n][j]);
          v             += .12 * (std::abs(q[0] - contact.xy[0]) + std::abs(q[1] - contact.xy[1]));
        }
        for (auto n : s.nets) {
          v += hpwl(wires[n], p, true);
        }
        return v;
      };
      double best   = s.current ? score(*s.current) : std::numeric_limits<double>::infinity();
      Pin    choice = s.current.value_or(Pin{});
      for (auto pin : s.candidates) {
        double v = score(pin);
        if (v < best - 1e-6) {
          best   = v;
          choice = pin;
        }
      }
      for (auto t : s.taps) {
        put(t, choice);
      }
      if (s.current != choice) {
        changed   = true;
        s.current = choice;
      }
    }
    if (!changed) {
      p.pins_converged = true;
      break;
    }
  }
  copy_interfaces(p, wires, copies_requested, resolver, rules);
  measure_chunk_ports(p,wires,core);
}
void measure_chunk_ports(Chunk_plan& p,const std::vector<Interface_net>& wires,Chunk_box core) {
  p.center_hpwl=p.port_hpwl=0;
  p.route={};
  for(auto& c:p.chunks) c.incident_hpwl=c.longest_span=0;
  // Spread each net's horizontal and vertical spans over its pin bounding
  // box. A zero-width line belongs to one bin in that axis. The sums recover
  // HPWL; dividing each direction by the tile span gives track equivalents.
  p.route.frame       = core;
  constexpr auto bins = Chunk_route_grid::bins;
  p.crossing_nets     = wires.size();
  for (const auto& w : wires) {
    p.center_hpwl += hpwl(w, p, false);
    Bounds bounds;
    for (const auto& tap : w.contacts) {
      bounds.add(tap.xy);
    }
    if (w.terminal_bounds) {
      auto b = *w.terminal_bounds;
      bounds.add({b[0], b[1]});
      bounds.add({b[2], b[3]});
    } else if (w.terminal) {
      bounds.add(*w.terminal);
    }
    std::array<std::array<double, bins>, 2> frac{};
    std::array<int, 2>                      first, last;
    Chunk_point                             length{}, step{};
    for (int k = 0; k < 2; ++k) {
      length[k] = bounds.hi[k] - bounds.lo[k];
      step[k]   = (core[k + 2] - core[k]) / bins;
      auto bin  = [&](double x) { return int(std::clamp(std::floor((x - core[k]) / step[k]), 0., double(bins - 1))); };
      first[k]  = bin(bounds.lo[k]);
      last[k]   = bin(bounds.hi[k]);
      for (int i = first[k]; i <= last[k]; ++i) {
        double       lo    = core[k] + step[k] * i;
        // IO may lie beyond the placement core. Charge the outside portion
        // to the edge tile so directional demand still sums to pin HPWL.
        const double start = i == 0 ? bounds.lo[k] : std::max(lo, bounds.lo[k]);
        const double end   = i == bins - 1 ? bounds.hi[k] : std::min(lo + step[k], bounds.hi[k]);
        frac[k][i]         = length[k] > 0 ? std::max(0., end - start) / length[k] : 1.;
      }
    }
    for (int y = first[1]; y <= last[1]; ++y) {
      for (int x = first[0]; x <= last[0]; ++x) {
        auto&  tile   = p.route.load[y * bins + x];
        double weight = frac[0][x] * frac[1][y];
        for (int k = 0; k < 2; ++k) {
          tile[k] += weight * length[k] / step[k];
        }
        p.route.peak_h = std::max(p.route.peak_h, tile[0]);
        p.route.peak_v = std::max(p.route.peak_v, tile[1]);
      }
    }
    const double span  = hpwl(w, p, true);
    p.port_hpwl       += span;
    for (const auto& tap : w.contacts) {
      auto& c          = p.chunks[tap.home];
      c.incident_hpwl += span;
      c.longest_span   = std::max(c.longest_span, span);
    }
    if (w.terminal) {
      auto t = *w.terminal;
      p.fixed_contacts.emplace(w.index, w.terminal_bounds.value_or(Chunk_box{t[0], t[1], t[0], t[1]}));
    }
  }
}
}  // namespace livehd::placement

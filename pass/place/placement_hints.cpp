// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "placement_hints.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <numeric>
#include <stdexcept>

#include "core/diag.hpp"
#include "dreamplace/native/orientation.hpp"
#include "graph/node_util.hpp"
namespace livehd::placement {
namespace dp = DreamPlace::native;
namespace gu = graph_util;
namespace {
using J = rapidjson::Value;
std::string str(const J& v, const char* key, std::string fallback = {}) {
  if (!v.IsObject() || !v.HasMember(key)) {
    return fallback;
  }
  if (!v[key].IsString()) {
    throw std::invalid_argument(std::string(key) + " must be a string");
  }
  return {v[key].GetString(), v[key].GetStringLength()};
}
std::vector<std::string> path(const J& v) {
  std::vector<std::string> out;
  if (v.IsString()) {
    std::string s(v.GetString(), v.GetStringLength());
    size_t      start = 0;
    do {
      auto end = s.find('/', start);
      out.push_back(s.substr(start, end - start));
      if (end == s.npos) {
        break;
      }
      start = end + 1;
    } while (true);
  } else if (v.IsArray()) {
    for (const auto& part : v.GetArray()) {
      if (!part.IsString()) {
        throw std::invalid_argument("path components must be strings");
      }
      out.emplace_back(part.GetString(), part.GetStringLength());
    }
  } else {
    throw std::invalid_argument("path must be a string or component array");
  }
  if (out.empty() || std::any_of(out.begin(), out.end(), [](const auto& p) { return p.empty() || p == "." || p == ".."; })) {
    throw std::invalid_argument("path must be nonempty and scope-relative");
  }
  return out;
}
std::string join(const std::vector<std::string>& parts, char separator = '.') {
  std::string s;
  for (const auto& p : parts) {
    if (!s.empty()) {
      s += separator;
    }
    s += p;
  }
  return s;
}
std::string scoped(const std::vector<std::string>& scope, const std::vector<std::string>& ref) {
  auto p = scope;
  p.insert(p.end(), ref.begin(), ref.end());
  return gu::logical_hier_name(join(p), true);
}
struct Box {
  double xl, yl, xh, yh;
};
Box box(const dp::Design& d, int i, bool halo = true) {
  double hx = halo && !d.node_halo_x.empty() ? d.node_halo_x[i] : 0;
  double hy = halo && !d.node_halo_y.empty() ? d.node_halo_y[i] : 0;
  return {d.node_x[i] - hx, d.node_y[i] - hy, d.node_x[i] + d.node_size_x[i] + hx, d.node_y[i] + d.node_size_y[i] + hy};
}
Box bounds(const dp::Design& d, const std::vector<int>& nodes) {
  auto b = box(d, nodes.front());
  for (int i : nodes) {
    auto c = box(d, i);
    b      = {std::min(b.xl, c.xl), std::min(b.yl, c.yl), std::max(b.xh, c.xh), std::max(b.yh, c.yh)};
  }
  return b;
}
bool overlap(Box a, Box b) { return a.xl < b.xh && b.xl < a.xh && a.yl < b.yh && b.yl < a.yh; }
bool movable_macro(const dp::Design& d, int i) { return i >= 0 && i < d.num_movable && !d.node_macro.empty() && d.node_macro[i]; }
}  // namespace
std::vector<Hint_scope> placement_hint_scopes(hhds::Graph& top, const physical::Technology& tech) {
  std::vector<Hint_scope>                                     out;
  std::set<hhds::Gid>                                         active;
  std::function<void(hhds::Graph&, std::vector<std::string>)> walk = [&](hhds::Graph& g, std::vector<std::string> p) {
    if (!active.insert(g.get_gid()).second) {
      return;
    }
    out.push_back({std::string(g.get_name()), p});
    for (auto n : g.body().nodes()) {
      if (gu::type_op_of(n) != Ntype_op::Sub || n.is_loop_subnode()) {
        continue;
      }
      auto child = n.get_subnode_graph();
      if (!child || tech.macros.contains(std::string(child->get_name()))) {
        continue;
      }
      auto next = p;
      // Unnamed generated color wrappers are transparent, matching flatten.
      if (gu::has_name(n)) {
        auto name = gu::logical_hier_name(gu::default_instance_name(n), true);
        if (!name.empty()) {
          next.push_back(std::move(name));
        }
      }
      walk(*child, std::move(next));
    }
    active.erase(g.get_gid());
  };
  walk(top, {});
  return out;
}
void Placement_hints::reject(Hint& h, const std::string& reason) {
  if (h.status == "discarded") {
    return;
  }
  h.status = "discarded";
  h.reason = reason;
  diag::warn("pass.place", "placement-hint", "placement").msg("placement hint '{}': {}; ignored", h.id, reason).emit();
}
Placement_hints::Placement_hints(std::shared_ptr<const metadata::Bundle> input, std::string design,
                                 const std::vector<Hint_scope>& scopes, std::vector<Hint_object> objects, double dbu)
    : input_(std::move(input)), design_(std::move(design)), objects_(std::move(objects)), dbu_(dbu) {
  if (!input_) {
    return;
  }
  std::map<std::string, std::vector<int>> names;
  for (size_t i = 0; i < objects_.size(); ++i) {
    names[gu::logical_hier_name(objects_[i].name, true)].push_back(int(i));
  }
  struct Template {
    const J*    value;
    std::string id;
    Hint_scope  scope;
  };
  std::vector<Template> templates;
  for (const auto* record : input_->select("livehd.placement.hints", design_)) {
    Hint invalid;
    invalid.id                = "<record>";
    const auto template_start = templates.size();
    try {
      invalid.id = str(*record, "id");
      if (invalid.id.empty()) {
        throw std::invalid_argument("placement record needs a nonempty id");
      }
      if (!record->HasMember("schema_version") || !(*record)["schema_version"].IsInt()
          || (*record)["schema_version"].GetInt() != 1) {
        throw std::invalid_argument("unsupported placement hints schema_version");
      }
      if (!record->HasMember("templates") || !(*record)["templates"].IsArray()) {
        throw std::invalid_argument("templates must be an array");
      }
      for (const auto& t : (*record)["templates"].GetArray()) {
        const auto            module = str(t, "module");
        std::set<std::string> matched;
        for (const auto& s : scopes) {
          if (s.module == module) {
            matched.insert(s.module);
          }
        }
        if (matched.empty()) {
          for (const auto& s : scopes) {
            if (s.module.substr(s.module.rfind('.') == std::string::npos ? 0 : s.module.rfind('.') + 1) == module) {
              matched.insert(s.module);
            }
          }
        }
        if (matched.size() != 1) {
          throw std::invalid_argument("module selector '" + module + "' is missing or ambiguous");
        }
        for (const auto& s : scopes) {
          if (s.module == *matched.begin()) {
            templates.push_back({&t, invalid.id, s});
          }
        }
      }
    } catch (const std::exception& e) {
      templates.resize(template_start);
      reject(invalid, e.what());
      hints_.push_back(std::move(invalid));
    }
  }
  std::map<std::string, std::vector<std::vector<int>>> groups;
  auto                                                 resolve_object = [&](const std::vector<std::string>& scope, const J& ref) {
    auto key = scoped(scope, path(ref));
    auto it  = names.find(key);
    if (it == names.end() || it->second.size() != 1) {
      throw std::invalid_argument("physical instance '" + key + "' is missing or ambiguous");
    }
    return std::vector<int>{it->second.front()};
  };
  for (const auto& t : templates) {
    if (t.value->HasMember("groups")) {
      Hint h;
      h.id = t.id + ":" + join(t.scope.path, '/') + ":groups";
      try {
        const auto& gs = (*t.value)["groups"];
        if (!gs.IsObject()) {
          throw std::invalid_argument("groups must be an object");
        }
        for (const auto& g : gs.GetObject()) {
          if (!g.value.IsArray() || g.value.Empty()) {
            throw std::invalid_argument("group members must be a nonempty array");
          }
          std::vector<int> members;
          for (const auto& ref : g.value.GetArray()) {
            auto ns = resolve_object(t.scope.path, ref);
            members.insert(members.end(), ns.begin(), ns.end());
          }
          std::sort(members.begin(), members.end());
          members.erase(std::unique(members.begin(), members.end()), members.end());
          auto key = scoped(t.scope.path, {std::string(g.name.GetString(), g.name.GetStringLength())});
          groups[key].push_back(std::move(members));
        }
      } catch (const std::exception& e) {
        reject(h, e.what());
        hints_.push_back(std::move(h));
      }
    }
  }
  auto resolve = [&](const Template& t, const J& ref) {
    auto key = scoped(t.scope.path, path(ref));
    auto g   = groups.find(key);
    if (g != groups.end()) {
      if (g->second.size() != 1 || names.contains(key)) {
        throw std::invalid_argument("ambiguous group '" + key + "'");
      }
      return g->second.front();
    }
    return resolve_object(t.scope.path, ref);
  };
  std::set<std::string> ids;
  for (const auto& t : templates) {
    if (!t.value->HasMember("hints") || !(*t.value)["hints"].IsArray()) {
      Hint h;
      h.id = t.id + ":" + join(t.scope.path, '/');
      reject(h, "hints must be an array");
      hints_.push_back(std::move(h));
      continue;
    }
    for (const auto& v : (*t.value)["hints"].GetArray()) {
      Hint h;
      h.id = t.id + ":" + join(t.scope.path, '/') + ":";
      try {
        auto id  = str(v, "id");
        h.id    += id;
        if (id.empty() || !ids.insert(h.id).second) {
          throw std::invalid_argument("missing or duplicate hint id");
        }
        h.kind = str(v, "kind");
        if (v.HasMember("weight")) {
          if (!v["weight"].IsNumber()) {
            throw std::invalid_argument("weight must be in (0,1]");
          }
          h.weight = v["weight"].GetDouble();
          if (!std::isfinite(h.weight) || h.weight <= 0 || h.weight > 1) {
            throw std::invalid_argument("weight must be in (0,1]");
          }
        }
        if (h.kind == "cluster") {
          if (!v.HasMember("group")) {
            throw std::invalid_argument("cluster needs group");
          }
          h.subject = resolve(t, v["group"]);
        } else if (h.kind == "align") {
          if (!v.HasMember("members") || !v["members"].IsArray()) {
            throw std::invalid_argument("align needs members");
          }
          for (const auto& m : v["members"].GetArray()) {
            auto ns = resolve(t, m);
            h.subject.insert(h.subject.end(), ns.begin(), ns.end());
          }
          if (h.subject.size() < 2) {
            throw std::invalid_argument("align needs at least two objects");
          }
          h.axis   = str(v, "axis");
          h.anchor = str(v, "anchor", "center");
          if ((h.axis != "x" && h.axis != "y") || (h.anchor != "center" && h.anchor != "min" && h.anchor != "max")) {
            throw std::invalid_argument("align needs axis x/y and anchor center/min/max");
          }
        } else if (h.kind == "position" || h.kind == "direction" || h.kind == "snap") {
          if (!v.HasMember("subject")) {
            throw std::invalid_argument("hint needs subject");
          }
          h.subject = resolve(t, v["subject"]);
          if (h.kind == "position") {
            if (h.subject.size() != 1) {
              throw std::invalid_argument("position requires one physical object");
            }
            if (str(v, "frame") != "floorplan" || str(v, "anchor") != "lower_left") {
              throw std::invalid_argument("position needs floorplan frame and lower_left anchor");
            }
            if (!v.HasMember("x_um") || !v["x_um"].IsNumber() || !v.HasMember("y_um") || !v["y_um"].IsNumber()) {
              throw std::invalid_argument("position needs finite x_um/y_um");
            }
            h.x           = v["x_um"].GetDouble() * dbu_;
            h.y           = v["y_um"].GetDouble() * dbu_;
            h.orientation = str(v, "orientation");
            if (!std::isfinite(h.x) || !std::isfinite(h.y)) {
              throw std::invalid_argument("nonfinite position");
            }
          } else {
            if (!v.HasMember("relative_to")) {
              throw std::invalid_argument("hint needs relative_to");
            }
            h.reference = resolve(t, v["relative_to"]);
            for (int i : h.subject) {
              if (std::find(h.reference.begin(), h.reference.end(), i) != h.reference.end()) {
                throw std::invalid_argument("relationship contains the same physical object on both sides");
              }
            }
            h.side = str(v, "side");
            if (h.side != "north" && h.side != "south" && h.side != "east" && h.side != "west"
                && !(h.kind == "snap" && h.side.empty())) {
              throw std::invalid_argument("invalid direction");
            }
          }
        } else {
          throw std::invalid_argument("unknown hint kind '" + h.kind + "'");
        }
        std::sort(h.subject.begin(), h.subject.end());
        h.subject.erase(std::unique(h.subject.begin(), h.subject.end()), h.subject.end());
      } catch (const std::exception& e) {
        reject(h, e.what());
      }
      hints_.push_back(std::move(h));
    }
  }
  std::map<std::string, int> counts;
  for (const auto& h : hints_) {
    ++counts[h.id];
  }
  for (auto& h : hints_) {
    if (counts[h.id] > 1) {
      reject(h, "duplicate scoped hint id; no file-order precedence");
    }
  }
}
void Placement_hints::rebind(std::vector<Hint_object> objects) {
  if (objects.size() == objects_.size()
      && std::equal(objects.begin(), objects.end(), objects_.begin(), [](const auto& a, const auto& b) {
           return a.name == b.name && a.master == b.master;
         })) {
    return;
  }
  std::map<std::string, int> index;
  for (size_t i = 0; i < objects.size(); ++i) {
    index.emplace(objects[i].name, int(i));
  }
  for (auto& h : hints_) {
    bool missing = false;
    for (auto* list : {&h.subject, &h.reference}) {
      std::vector<int> mapped;
      for (int i : *list) {
        auto it = index.find(objects_.at(i).name);
        if (it == index.end()) {
          missing = true;
        } else {
          mapped.push_back(it->second);
        }
      }
      *list = std::move(mapped);
    }
    if (missing) {
      reject(h, "physical instance disappeared during placement");
    }
  }
  objects_ = std::move(objects);
}
void Placement_hints::seed(dp::Design& d) {
  struct Candidate {
    Hint*           hint;
    int             node;
    Box             bounds;
    dp::Orientation orientation;
  };
  std::vector<Candidate> candidates;
  std::map<int, int>     coordinate_counts;
  for (const auto& h : hints_) {
    if (h.status != "discarded" && h.kind == "position") {
      ++coordinate_counts[h.subject.front()];
    }
  }
  for (auto& h : hints_) {
    if (h.status == "discarded") {
      continue;
    }
    bool valid = true;
    for (int i : h.subject) {
      if (!movable_macro(d, i)) {
        valid = false;
      }
    }
    for (int i : h.reference) {
      if (i >= int(d.node_x.size()) || d.node_macro.empty() || !d.node_macro[i]) {
        valid = false;
      }
    }
    if (!valid) {
      reject(h, "this stage supports movable hard-macro subjects only");
      continue;
    }
    if (h.kind != "position") {
      continue;
    }
    int i = h.subject.front();
    if (coordinate_counts[i] > 1) {
      reject(h, "more than one coordinate hint addresses this macro");
      continue;
    }
    auto orientation = d.node_orientation.empty() ? dp::Orientation::N : d.node_orientation[i];
    if (!h.orientation.empty()) {
      auto& obj = objects_[i];
      if (h.orientation == "N") {
        orientation = dp::Orientation::N;
      } else if (h.orientation == "FN" && obj.symmetry_y) {
        orientation = dp::Orientation::FN;
      } else if (h.orientation == "FS" && obj.symmetry_x) {
        orientation = dp::Orientation::FS;
      } else if (h.orientation == "S" && obj.symmetry_x && obj.symmetry_y) {
        orientation = dp::Orientation::S;
      } else {
        reject(h, "orientation is unsupported by this master");
        continue;
      }
    }
    auto   b  = box(d, i);
    double dx = h.x - d.node_x[i], dy = h.y - d.node_y[i];
    b = {b.xl + dx, b.yl + dy, b.xh + dx, b.yh + dy};
    if (b.xl < d.layout.xl || b.yl < d.layout.yl || b.xh > d.layout.xh || b.yh > d.layout.yh) {
      reject(h, "coordinate and halo are outside the core");
      continue;
    }
    bool collision = false;
    for (int j = d.num_movable; j < int(d.node_x.size()); ++j) {
      if (d.node_size_x[j] > 0 && d.node_size_y[j] > 0 && overlap(b, box(d, j))) {
        collision = true;
        break;
      }
    }
    if (collision) {
      reject(h, "coordinate overlaps fixed geometry");
      continue;
    }
    candidates.push_back({&h, i, b, orientation});
  }
  // Validate the whole request before moving anything. File order must never
  // choose the winner of two overlapping coordinate hints.
  for (size_t a = 0; a < candidates.size(); ++a) {
    for (size_t b = a + 1; b < candidates.size(); ++b) {
      if (overlap(candidates[a].bounds, candidates[b].bounds)) {
        reject(*candidates[a].hint, "coordinate overlaps another coordinate hint");
        reject(*candidates[b].hint, "coordinate overlaps another coordinate hint");
      }
    }
  }
  for (auto& c : candidates) {
    auto& h = *c.hint;
    if (h.status == "discarded") {
      continue;
    }
    try {
      dp::set_orientation_unchecked(d, c.node, c.orientation);
    } catch (const std::exception& e) {
      reject(h, e.what());
      continue;
    }
    d.node_x[c.node] = h.x;
    d.node_y[c.node] = h.y;
    h.applied        = true;
  }
}
std::vector<dp::MacroPull> Placement_hints::pulls(const dp::Design& d) const {
  struct Sum {
    double x = 0, y = 0, w = 0;
  };
  std::map<int, Sum> sums;
  auto               add = [&](int i, double dx, double dy, double weight) {
    if (!movable_macro(d, i)) {
      return;
    }
    auto   b  = box(d, i);
    double cx = d.node_x[i] + d.node_size_x[i] / 2, cy = d.node_y[i] + d.node_size_y[i] / 2;
    // Clamp displacement to the legal core for the full halo envelope.
    dx       = std::clamp(dx, d.layout.xl - b.xl, d.layout.xh - b.xh);
    dy       = std::clamp(dy, d.layout.yl - b.yl, d.layout.yh - b.yh);
    auto& s  = sums[i];
    s.x     += (cx + dx) * weight;
    s.y     += (cy + dy) * weight;
    s.w     += weight;
  };
  for (const auto& h : hints_) {
    if (h.status == "discarded" || h.kind == "position" || h.subject.empty()) {
      continue;
    }
    if (h.kind == "cluster" || h.kind == "align") {
      double tx = 0, ty = 0;
      for (int i : h.subject) {
        auto b  = box(d, i, false);
        tx     += (b.xl + b.xh) / 2;
        ty     += (b.yl + b.yh) / 2;
      }
      tx /= h.subject.size();
      ty /= h.subject.size();
      if (h.kind == "align" && h.anchor != "center") {
        double total = 0;
        for (int i : h.subject) {
          auto b  = box(d, i, false);
          total  += h.axis == "x" ? (h.anchor == "min" ? b.xl : b.xh) : (h.anchor == "min" ? b.yl : b.yh);
        }
        total /= h.subject.size();
        if (h.axis == "x") {
          tx = total;
        } else {
          ty = total;
        }
      }
      for (int i : h.subject) {
        auto   b = box(d, i, false);
        double x = (b.xl + b.xh) / 2, y = (b.yl + b.yh) / 2;
        if (h.kind == "align" && h.anchor != "center") {
          x = h.anchor == "min" ? b.xl : b.xh;
          y = h.anchor == "min" ? b.yl : b.yh;
        }
        add(i, h.kind == "align" && h.axis == "y" ? 0 : tx - x, h.kind == "align" && h.axis == "x" ? 0 : ty - y, h.weight);
      }
    } else {
      auto a = bounds(d, h.subject), b = bounds(d, h.reference);
      auto side = h.side;
      if (side.empty()) {
        std::vector<std::pair<double, std::string>> choices = {
            {std::abs(b.xh - a.xl),  "east"},
            {std::abs(b.xl - a.xh),  "west"},
            {std::abs(b.yh - a.yl), "north"},
            {std::abs(b.yl - a.yh), "south"}
        };
        side = std::min_element(choices.begin(), choices.end())->second;
      }
      double dx = 0, dy = 0;
      if (side == "east") {
        dx = h.kind == "snap" ? b.xh - a.xl : std::max(0., b.xh - a.xl);
      }
      if (side == "west") {
        dx = h.kind == "snap" ? b.xl - a.xh : std::min(0., b.xl - a.xh);
      }
      if (side == "north") {
        dy = h.kind == "snap" ? b.yh - a.yl : std::max(0., b.yh - a.yl);
      }
      if (side == "south") {
        dy = h.kind == "snap" ? b.yl - a.yh : std::min(0., b.yl - a.yh);
      }
      if (h.kind == "snap") {
        if (side == "east" || side == "west") {
          dy = (b.yl + b.yh - a.yl - a.yh) / 2;
        } else {
          dx = (b.xl + b.xh - a.xl - a.xh) / 2;
        }
      }
      for (int i : h.subject) {
        add(i, dx, dy, h.weight);
      }
    }
  }
  std::vector<dp::MacroPull> out;
  for (const auto& [i, s] : sums) {
    out.push_back({i, s.x / s.w, s.y / s.w, std::min(1., s.w)});
  }
  return out;
}
void Placement_hints::attach(dp::Options& control, std::function<std::vector<Hint_object>()> objects) {
  if (control.magnet.enabled) {
    // Native magnet callbacks address contracted nodes, which are not source
    // instances. Coordinates have already seeded the uncontracted design.
    for (auto& h : hints_) {
      if (h.kind != "position") {
        reject(h, "relative hints are unsupported with rigid macro magnet contraction");
      }
    }
    return;
  }
  if (std::none_of(hints_.begin(), hints_.end(), [](const auto& h) { return h.status != "discarded" && h.kind != "position"; })) {
    return;
  }
  const auto prior            = control.macro_pull_update;
  // Preserve the previous producer's cadence and density gate. The native
  // callback itself is still gated by that producer's density policy.
  const int  interval         = std::max(1, control.macro_pull_interval);
  control.macro_pull_interval = 1;
  control.macro_pull_update   = [this, prior, interval, objects, old = std::vector<dp::MacroPull>{}](
                                    const dp::Design& d,
                                    int               iteration) mutable -> std::optional<std::vector<dp::MacroPull>> {
    bool changed = false;
    if (prior && iteration % interval == 0) {
      if (auto p = prior(d, iteration)) {
        old     = std::move(*p);
        changed = true;
      }
    }
    if (iteration % 25 != 0 && !changed) {
      return std::nullopt;
    }
    if (objects) {
      rebind(objects());
    }
    auto out = pulls(d);
    for (const auto& p : old) {
      auto found = std::find_if(out.begin(), out.end(), [&](const auto& q) { return p.node == q.node; });
      if (found == out.end()) {
        out.push_back(p);
      } else {
        double w = p.weight + found->weight;
        if (w > 0) {
          found->x      = (p.x * p.weight + found->x * found->weight) / w;
          found->y      = (p.y * p.weight + found->y * found->weight) / w;
          found->weight = std::min(1., w);
        }
      }
    }
    return out;
  };
}
void Placement_hints::write(const dp::Design& d, const std::string& filename) {
  if (!input_) {
    return;
  }
  metadata::Bundle    out(input_->json());
  rapidjson::Document report;
  report.SetObject();
  auto& a      = report.GetAllocator();
  auto  string = [&](std::string_view s) { return J(s.data(), s.size(), a); };
  report.AddMember("class", "livehd.placement.hint_results", a);
  report.AddMember("schema_version", 1, a);
  report.AddMember("id", "placement", a);
  report.AddMember("design", string(design_), a);
  J results(rapidjson::kArrayType);
  for (auto& h : hints_) {
    double residual = 0;
    if (h.status != "discarded" && h.kind == "align") {
      double lo = std::numeric_limits<double>::infinity(), hi = -lo;
      for (int i : h.subject) {
        auto   b = box(d, i, false);
        double v = h.axis == "x" ? (h.anchor == "min"   ? b.xl
                                    : h.anchor == "max" ? b.xh
                                                        : (b.xl + b.xh) / 2)
                                 : (h.anchor == "min"   ? b.yl
                                    : h.anchor == "max" ? b.yh
                                                        : (b.yl + b.yh) / 2);
        lo       = std::min(lo, v);
        hi       = std::max(hi, v);
      }
      residual = hi - lo;
    } else if (h.status != "discarded" && (h.kind == "direction" || h.kind == "snap")) {
      const auto x = bounds(d, h.subject), y = bounds(d, h.reference);
      auto       distance = [&](std::string_view side) {
        double gap = side == "east" ? x.xl - y.xh : side == "west" ? y.xl - x.xh : side == "north" ? x.yl - y.yh : y.yl - x.yh;
        if (h.kind == "direction") {
          return std::max(0., -gap);
        }
        double offset = (side == "east" || side == "west") ? (x.yl + x.yh - y.yl - y.yh) / 2 : (x.xl + x.xh - y.xl - y.xh) / 2;
        return std::hypot(gap, offset);
      };
      if (h.side.empty()) {
        residual = std::min({distance("north"), distance("south"), distance("east"), distance("west")});
      } else {
        residual = distance(h.side);
      }
    }
    if (h.status == "accepted" && residual > std::max(d.layout.site_width, d.layout.row_height)) {
      h.status = "relaxed";
      h.reason = "soft relationship not met within one placement pitch after legalization";
      diag::warn("pass.place", "placement-hint-relaxed", "placement")
          .msg("placement hint '{}': {}; residual {} um", h.id, h.reason, residual / dbu_)
          .emit();
    }
    J r(rapidjson::kObjectType);
    r.AddMember("residual_um", residual / dbu_, a);
    r.AddMember("id", string(h.id), a);
    r.AddMember("kind", string(h.kind), a);
    r.AddMember("status", string(h.status), a);
    r.AddMember("reason", string(h.reason), a);
    r.AddMember("seed_applied", h.applied, a);
    J members(rapidjson::kArrayType);
    for (int i : h.subject) {
      members.PushBack(string(objects_[i].name), a);
    }
    r.AddMember("subjects", members, a);
    results.PushBack(r, a);
  }
  report.AddMember("hints", results, a);
  out.replace(report);
  rapidjson::Document state;
  state.SetObject();
  auto& b = state.GetAllocator();
  state.AddMember("class", "livehd.placement.state", b);
  state.AddMember("schema_version", 1, b);
  state.AddMember("id", "macro-positions", b);
  state.AddMember("design", J(design_.c_str(), b), b);
  state.AddMember("automatic_reuse", false, b);  // Existing physical cache owns dependency-checked reuse.
  J templates(rapidjson::kArrayType), t(rapidjson::kObjectType), positions(rapidjson::kArrayType);
  t.AddMember("module", J(design_.c_str(), b), b);
  for (size_t i = 0; i < objects_.size() && i < d.node_x.size(); ++i) {
    if (!d.node_macro.empty() && d.node_macro[i]) {
      J p(rapidjson::kObjectType);
      p.AddMember("id", J(objects_[i].name.c_str(), b), b);
      p.AddMember("kind", "position", b);
      // Exact physical names are a single path component; never invent a
      // different hierarchy by splitting escaped instance identifiers.
      J subject(rapidjson::kArrayType);
      subject.PushBack(J(objects_[i].name.c_str(), b), b);
      p.AddMember("subject", subject, b);
      p.AddMember("frame", "floorplan", b);
      p.AddMember("anchor", "lower_left", b);
      p.AddMember("x_um", d.node_x[i] / dbu_, b);
      p.AddMember("y_um", d.node_y[i] / dbu_, b);
      const auto o = d.node_orientation.empty() ? dp::Orientation::N : d.node_orientation[i];
      p.AddMember("orientation",
                  J(o == dp::Orientation::N    ? "N"
                    : o == dp::Orientation::FN ? "FN"
                    : o == dp::Orientation::FS ? "FS"
                                               : "S",
                    b),
                  b);
      positions.PushBack(p, b);
    }
  }
  t.AddMember("hints", positions, b);
  templates.PushBack(t, b);
  state.AddMember("templates", templates, b);
  out.replace(state);
  out.write(filename);
}
}  // namespace livehd::placement

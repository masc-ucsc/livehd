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
#include "graph/attrs.hpp"
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
std::pair<double, double> group_center(const dp::Design& d, const std::vector<int>& nodes) {
  double x = 0, y = 0;
  for (int i : nodes) {
    const auto b = box(d, i, false);
    x += (b.xl + b.xh) / 2;
    y += (b.yl + b.yh) / 2;
  }
  return {x / nodes.size(), y / nodes.size()};
}

// Automatic snapping chooses the nearest parallel edges, then centers the
// orthogonal axis. Its residual must use the same side as the actual pull.
std::pair<double, double> relative_shift(Box a, Box b, std::string_view kind, std::string_view side, double gap) {
  if (side.empty()) {
    side = std::min({std::pair<double, std::string_view>{std::abs(b.xh - a.xl), "east"},
                     {std::abs(b.xl - a.xh), "west"}, {std::abs(b.yh - a.yl), "north"}, {std::abs(b.yl - a.yh), "south"}}).second;
  }
  const bool horizontal = side == "east" || side == "west";
  const bool positive   = side == "east" || side == "north";
  double delta = side == "east" ? b.xh - a.xl : side == "west" ? b.xl - a.xh : side == "north" ? b.yh - a.yl : b.yl - a.yh;
  double across = 0;
  if (kind == "direction") {
    delta = positive ? std::max(0., delta) : std::min(0., delta);
  } else if (kind == "gap") {
    delta += positive ? gap : -gap;
  } else {
    across = horizontal ? (b.yl + b.yh - a.yl - a.yh) / 2 : (b.xl + b.xh - a.xl - a.xh) / 2;
  }
  return horizontal ? std::pair{delta, across} : std::pair{across, delta};
}
bool overlap(Box a, Box b) { return a.xl < b.xh && b.xl < a.xh && a.yl < b.yh && b.yl < a.yh; }
bool movable_macro(const dp::Design& d, int i) { return i >= 0 && i < d.num_movable && !d.node_macro.empty() && d.node_macro[i]; }
}  // namespace
std::vector<Hint_scope> placement_hint_scopes(hhds::Graph& top, const physical::Technology& tech) {
  std::vector<Hint_scope> out;
  std::set<hhds::Gid> active;
  std::vector<size_t> memories;
  auto walk = [&](auto&& self, hhds::Graph& g, std::vector<std::string> p, bool named_instance) -> void {
    if (!active.insert(g.get_gid()).second) {
      return;
    }
    out.push_back({std::string(g.get_name()), p});
    const bool memory = named_instance && g.get_input_node().attr(attrs::memory_module).has();
    if (memory) {
      memories.push_back(out.size() - 1);
      out.back().memory_banks.emplace();
    }
    for (auto n : g.body().nodes()) {
      if (gu::type_op_of(n) != Ntype_op::Sub || n.is_loop_subnode()) {
        continue;
      }
      auto io = n.get_subnode_io();
      if (!io) {
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
      const bool named = next.size() > p.size();
      if (const auto macro = tech.macros.find(std::string(io->get_name())); macro != tech.macros.end()) {
        if (!memories.empty() && macro->second.klass.starts_with("BLOCK") && physical::hard_macro(macro->second)) {
          for (const auto owner : memories) {
            // An unnamed bank makes its entire logical binding unresolved.
            out[owner].memory_banks->push_back(named ? scoped({}, next) : std::string{});
          }
        }
      } else if (auto child = n.get_subnode_graph()) {
        self(self, *child, std::move(next), named);
      }
    }
    if (memory) {
      memories.pop_back();
    }
    active.erase(g.get_gid());
  };
  walk(walk, top, {}, false);
  return out;
}
void Placement_hints::reject(Hint& h, const std::string& reason) {
  if (h.status == "discarded") {
    return;
  }
  h.status = "discarded";
  h.reason = reason;
  diag::warn("pass.place", "placement-hint", "unsupported").msg("placement hint '{}': {}; ignored", h.id, reason).emit();
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
  std::map<std::string, std::vector<const Hint_scope*>> memories;
  for (const auto& scope : scopes) {
    if (scope.memory_banks) {
      memories[scoped({}, scope.path)].push_back(&scope);
    }
  }
  struct Template {
    const J*    value;
    std::string id;
    Hint_scope  scope;
    int         version;
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
          || ((*record)["schema_version"].GetInt() != 1 && (*record)["schema_version"].GetInt() != 2)) {
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
            templates.push_back({&t, invalid.id, s, (*record)["schema_version"].GetInt()});
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
  auto physical_index = [&](const std::string& key) {
    auto it  = names.find(key);
    if (key.empty() || it == names.end() || it->second.size() != 1) {
      throw std::invalid_argument("physical instance '" + key + "' is missing or ambiguous");
    }
    return it->second.front();
  };
  auto resolve_object = [&](const std::vector<std::string>& scope, const J& ref) {
    auto key = scoped(scope, path(ref));
    auto memory = memories.find(key);
    if (memory == memories.end()) {
      return std::vector<int>{physical_index(key)};
    }
    if (memory->second.size() != 1 || names.contains(key)) {
      throw std::invalid_argument("ambiguous logical memory '" + key + "'");
    }
    std::set<int> members;
    for (const auto& bank : *memory->second.front()->memory_banks) {
      if (!members.insert(physical_index(bank)).second) {
        throw std::invalid_argument("ambiguous bank '" + bank + "' in logical memory '" + key + "'");
      }
    }
    if (members.empty()) {
      throw std::invalid_argument("logical memory '" + key + "' has no bound physical banks");
    }
    return std::vector<int>(members.begin(), members.end());
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
      if (g->second.size() != 1 || names.contains(key) || memories.contains(key)) {
        throw std::invalid_argument("ambiguous group '" + key + "'");
      }
      return g->second.front();
    }
    return resolve_object(t.scope.path, ref);
  };
  std::set<std::string> ids;
  std::map<std::string, Centroid> pending_centroids;
  for (const auto& t : templates) {
    if (!t.value->HasMember("hints") || !(*t.value)["hints"].IsArray()) {
      Hint h;
      h.id = t.id + ":" + join(t.scope.path, '/');
      reject(h, "hints must be an array");
      hints_.push_back(std::move(h));
      continue;
    }
    std::map<std::string, const J*> overrides;
    if (t.version >= 2 && t.value->HasMember("overrides")) {
      Hint invalid;
      invalid.id = t.id + ":" + join(t.scope.path, '/') + ":overrides";
      try {
        const auto& entries = (*t.value)["overrides"];
        if (!entries.IsArray()) {
          throw std::invalid_argument("overrides must be an array");
        }
        for (const auto& entry : entries.GetArray()) {
          if (!entry.IsObject() || !entry.HasMember("instance") || !entry["instance"].IsArray()) {
            throw std::invalid_argument("override requires an exact instance path array");
          }
          std::vector<std::string> instance;
          for (const auto& part : entry["instance"].GetArray()) {
            if (!part.IsString()) {
              throw std::invalid_argument("invalid override path");
            }
            instance.emplace_back(part.GetString());
          }
          if (std::ranges::none_of(scopes, [&](const Hint_scope& scope) {
                return scope.module == t.scope.module && scope.path == instance;
              })) {
            throw std::invalid_argument("unbound override instance " + join(instance, '/'));
          }
          if (instance != t.scope.path) {
            continue;
          }
          const auto id = str(entry, "id");
          if (id.empty() || !overrides.emplace(id, &entry).second) {
            throw std::invalid_argument("duplicate or missing override ID");
          }
          if (entry.HasMember("disabled") && !entry["disabled"].IsBool()) {
            throw std::invalid_argument("disabled must be boolean");
          }
          if (entry.HasMember("set") && !entry["set"].IsObject()) {
            throw std::invalid_argument("override set must be an object");
          }
        }
      } catch (const std::exception& e) {
        reject(invalid, e.what());
        hints_.push_back(std::move(invalid));
        continue;
      }
    }
    for (const auto& authored : (*t.value)["hints"].GetArray()) {
      rapidjson::Document merged;
      const J*            value = &authored;

      Hint h;
      h.id = t.id + ":" + join(t.scope.path, '/') + ":";
      try {
        auto id  = str(authored, "id");
        h.id    += id;
        if (id.empty() || !ids.insert(h.id).second) {
          throw std::invalid_argument("missing or duplicate hint id");
        }
        if (auto found = overrides.find(id); found != overrides.end()) {
          const auto& edit = *found->second;
          overrides.erase(found);
          if (edit.HasMember("disabled") && edit["disabled"].GetBool()) {
            h.kind   = str(authored, "kind");
            h.status = "discarded";
            h.reason = "disabled by instance override";
            hints_.push_back(std::move(h));
            continue;
          }
          if (edit.HasMember("set")) {
            merged.CopyFrom(authored, merged.GetAllocator());
            for (const auto& field : edit["set"].GetObject()) {
              if (std::string_view(field.name.GetString()) == "id") {
                throw std::invalid_argument("override cannot rename a hint");
              }
              merged.RemoveMember(field.name);
              merged.AddMember(J(field.name, merged.GetAllocator()), J(field.value, merged.GetAllocator()), merged.GetAllocator());
            }
            value = &merged;
          }
        }
        const auto& v = *value;
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
        const auto finite = [&](const char* field, double fallback) {
          if (!v.HasMember(field)) {
            return fallback;
          }
          if (!v[field].IsNumber() || !std::isfinite(v[field].GetDouble())) {
            throw std::invalid_argument(std::string("invalid ") + field);
          }
          return v[field].GetDouble();
        };
        if (h.kind == "pin_facing" && t.version >= 2) {
          if (!v.HasMember("subject") || !v.HasMember("pins") || !v["pins"].IsArray() || v["pins"].Empty()) {
            throw std::invalid_argument("pin_facing needs a macro subject and signal pin names");
          }
          h.subject = resolve(t, v["subject"]);
          h.side    = str(v, "side");
          if (h.subject.size() != 1 || (h.side != "north" && h.side != "south" && h.side != "west" && h.side != "east")) {
            throw std::invalid_argument("pin_facing needs one bound macro and a core compass side");
          }
          for (const auto& pin : v["pins"].GetArray()) {
            if (!pin.IsString() || !objects_[h.subject[0]].signal_pin_centers.contains(pin.GetString())) {
              throw std::invalid_argument("unknown signal pin in pin_facing");
            }
            h.pins.emplace_back(pin.GetString());
          }
          std::ranges::sort(h.pins);
          if (std::adjacent_find(h.pins.begin(), h.pins.end()) != h.pins.end()) {
            throw std::invalid_argument("repeated signal pin");
          }
        } else if (h.kind == "anchor" && t.version >= 2) {
          if (!v.HasMember("subject") || str(v, "frame") != "core") {
            throw std::invalid_argument("anchor requires subject and core frame");
          }
          h.subject  = resolve(t, v["subject"]);
          h.anchor_x = finite("anchor_x", .5);
          h.anchor_y = finite("anchor_y", .5);
          h.x        = finite("offset_x_um", 0) * dbu_;
          h.y        = finite("offset_y_um", 0) * dbu_;
          if (h.anchor_x < 0 || h.anchor_x > 1 || h.anchor_y < 0 || h.anchor_y > 1) {
            throw std::invalid_argument("invalid normalized anchor");
          }
        } else if (h.kind == "distribute" && t.version >= 2) {
          h.axis = str(v, "axis");
          if ((h.axis != "x" && h.axis != "y") || !v.HasMember("members") || !v["members"].IsArray()) {
            throw std::invalid_argument("distribution needs ordered members and an axis");
          }
          std::set<int> unique;
          for (const auto& m : v["members"].GetArray()) {
            const auto member = resolve_object(t.scope.path, m);
            if (member.size() != 1) {
              throw std::invalid_argument("each ordered distribution member must select one physical instance");
            }
            int i = member.front();
            if (!unique.insert(i).second) {
              throw std::invalid_argument("duplicate distribution member");
            }
            h.subject.push_back(i);
          }
          if (h.subject.size() < 2) {
            throw std::invalid_argument("distribution needs at least two members");
          }
        } else if (h.kind == "matrix" && t.version >= 2) {
          if (!v.HasMember("members") || !v["members"].IsArray() || !v.HasMember("columns") || !v["columns"].IsInt()) {
            throw std::invalid_argument("matrix requires ordered members and columns");
          }
          std::set<int> unique;
          for (const auto& member : v["members"].GetArray()) {
            auto ids_for_member = resolve_object(t.scope.path, member);
            if (ids_for_member.size() != 1) {
              throw std::invalid_argument("each ordered matrix slot must select one physical instance");
            }
            if (!unique.insert(ids_for_member.front()).second) {
              throw std::invalid_argument("duplicate matrix member");
            }
            h.subject.push_back(ids_for_member.front());
          }
          h.columns = v["columns"].GetInt();
          if (h.columns < 1 || size_t(h.columns) > h.subject.size()) {
            throw std::invalid_argument("invalid matrix columns");
          }
          h.gap_x    = finite("gap_x_um", 0) * dbu_;
          h.gap_y    = finite("gap_y_um", 0) * dbu_;
          h.anchor_x = finite("anchor_x", .5);
          h.anchor_y = finite("anchor_y", .5);
          if (h.anchor_x < 0 || h.anchor_x > 1 || h.anchor_y < 0 || h.anchor_y > 1 || h.gap_x < 0 || h.gap_y < 0) {
            throw std::invalid_argument("invalid normalized matrix anchor or physical clearance");
          }
        } else if (h.kind == "cluster") {
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
        } else if (h.kind == "position" || h.kind == "direction" || h.kind == "snap" || (t.version >= 2 && h.kind == "gap")) {
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
            if (h.kind == "gap") {
              h.gap_x = finite("gap_um", -1) * dbu_;
              if (h.gap_x < 0) {
                throw std::invalid_argument("gap requires a nonnegative physical gap_um");
              }
            }
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
        } else if (h.kind == "centroid") {
          // Members are instances of any kind (flops, cells, memories, macros) or other centroids, named relative to the scope.
          // They bind to placement nodes when a stage compiles the set, not here: only hard macros are hint objects.
          const auto     prefix = t.id + ":" + join(t.scope.path, '/') + ":";
          Centroid_names qualify;
          qualify.member   = [&](const std::vector<std::string>& ref) { return scoped(t.scope.path, ref); };
          qualify.centroid = [&](const std::string& local) { return prefix + local; };
          auto record      = parse_centroid(v, qualify);
          record.origin    = record.origin.empty() ? "file" : record.origin;
          pending_centroids.emplace(h.id, std::move(record));
        } else {
          throw std::invalid_argument("unknown hint kind '" + h.kind + "'");
        }
        if (h.kind != "matrix" && h.kind != "distribute") {
          std::sort(h.subject.begin(), h.subject.end());
          h.subject.erase(std::unique(h.subject.begin(), h.subject.end()), h.subject.end());
        }
      } catch (const std::exception& e) {
        reject(h, e.what());
      }
      hints_.push_back(std::move(h));
    }
    for (const auto& [id, unused] : overrides) {
      Hint missing;
      missing.id = t.id + ":" + join(t.scope.path, '/') + ":" + id;
      reject(missing, "instance override names an unknown hint");
      hints_.push_back(std::move(missing));
    }
  }
  std::map<std::string, int> counts;
  for (const auto& h : hints_) {
    ++counts[h.id];
  }
  for (auto& h : hints_) {
    if (counts[h.id] > 1) {
      reject(h, "duplicate scoped hint id; no file-order precedence");
    } else if (auto it = pending_centroids.find(h.id); it != pending_centroids.end()) {
      std::string why;
      if (!centroids_.add(std::move(it->second), &why)) {
        reject(h, why);
      }
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
double Placement_hints::pin_distance(const Hint& h, const dp::Design& d, dp::Orientation orient) const {
  const int    i    = h.subject.front();
  const auto&  pins = objects_[i].signal_pin_centers;
  const double w = d.node_size_x[i], height = d.node_size_y[i];
  double       sum = 0;
  for (const auto& pin : h.pins) {
    if (!pins.contains(pin)) {
      return std::numeric_limits<double>::infinity();
    }
    auto [x, y] = pins.at(pin);
    if (orient == dp::Orientation::S || orient == dp::Orientation::FN) {
      x = w - x;
    }
    if (orient == dp::Orientation::S || orient == dp::Orientation::FS) {
      y = height - y;
    }
    sum += h.side == "west" ? x : h.side == "east" ? w - x : h.side == "north" ? height - y : y;
  }
  return sum / h.pins.size();
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
  std::map<int, std::vector<Hint*>> facing;
  std::set<int>                     positioned_orientation;
  for (const auto& h : hints_) {
    if (h.status != "discarded" && h.kind == "position") {
      ++coordinate_counts[h.subject.front()];
      if (!h.orientation.empty()) {
        positioned_orientation.insert(h.subject.front());
      }
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
    if (h.kind == "pin_facing") {
      facing[h.subject.front()].push_back(&h);
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
  for (const auto& [i, hints] : facing) {
    if (positioned_orientation.contains(i) || (!d.node_orientation_fixed.empty() && d.node_orientation_fixed[i])) {
      continue;
    }
    const auto current = d.node_orientation.empty() ? dp::Orientation::N : d.node_orientation[i];
    if (current != dp::Orientation::N && current != dp::Orientation::FN && current != dp::Orientation::S
        && current != dp::Orientation::FS) {
      continue;
    }
    const auto& m    = objects_[i];
    auto        cost = [&](auto orient) {
      double total = 0;
      for (const Hint* h : hints) {
        total += h->weight * pin_distance(*h, d, orient);
      }
      return total;
    };
    auto   best    = current;
    double minimum = cost(current);
    for (auto orient : {dp::Orientation::N, dp::Orientation::FN, dp::Orientation::FS, dp::Orientation::S}) {
      if ((orient == dp::Orientation::FN || orient == dp::Orientation::S) && !m.symmetry_y) {
        continue;
      }
      if ((orient == dp::Orientation::FS || orient == dp::Orientation::S) && !m.symmetry_x) {
        continue;
      }
      const auto c = cost(orient);
      if (c < minimum) {
        minimum = c;
        best    = orient;
      }
    }
    if (std::isfinite(minimum)) {
      dp::set_orientation_unchecked(d, i, best);
      for (auto* hint : hints) {
        hint->applied = true;
      }
    }
  }
}
// Row-major membership is authored once. The matrix envelope uses real sizes
// and halos; the normalized anchor locates it in the remaining core space.
std::vector<std::pair<double, double>> Placement_hints::matrix_targets(const Hint& hint, const dp::Design& d, bool* fits_core) const {
  const size_t        cols = hint.columns, rows = (hint.subject.size() + cols - 1) / cols;
  std::vector<double> widths(cols), heights(rows);
  for (size_t k = 0; k < hint.subject.size(); ++k) {
    const auto b      = box(d, hint.subject[k]);
    widths[k % cols]  = std::max(widths[k % cols], b.xh - b.xl);
    heights[k / cols] = std::max(heights[k / cols], b.yh - b.yl);
  }
  const double                           width  = std::accumulate(widths.begin(), widths.end(), hint.gap_x * (cols - 1));
  const double                           height = std::accumulate(heights.begin(), heights.end(), hint.gap_y * (rows - 1));
  if (fits_core) {
    *fits_core = width <= d.layout.xh - d.layout.xl && height <= d.layout.yh - d.layout.yl;
  }
  const double left = d.layout.xl + hint.anchor_x * (d.layout.xh - d.layout.xl - width);
  double                                 y = d.layout.yl + hint.anchor_y * (d.layout.yh - d.layout.yl - height), x = left;
  std::vector<std::pair<double, double>> targets;
  for (size_t k = 0; k < hint.subject.size(); ++k) {
    const size_t row = k / cols, col = k % cols;
    if (col == 0 && row) {
      y += heights[row - 1] + hint.gap_y;
      x  = left;
    }
    targets.emplace_back(x + .5 * widths[col], y + .5 * heights[row]);
    x += widths[col] + hint.gap_x;
  }
  return targets;
}
std::vector<std::pair<double, double>> Placement_hints::relationship_targets(const Hint& hint, const dp::Design& d) const {
  if (hint.kind == "matrix") {
    return matrix_targets(hint, d);
  }
  const auto envelope = bounds(d, hint.subject);
  double     dx = 0, dy = 0;
  const bool horizontal = hint.axis == "x";
  double     cursor = horizontal ? envelope.xl : envelope.yl, gap = 0;
  if (hint.kind == "anchor") {
    dx = d.layout.xl + hint.anchor_x * (d.layout.xh - d.layout.xl - (envelope.xh - envelope.xl)) + hint.x - envelope.xl;
    dy = d.layout.yl + hint.anchor_y * (d.layout.yh - d.layout.yl - (envelope.yh - envelope.yl)) + hint.y - envelope.yl;
  } else {
    double available = horizontal ? envelope.xh - envelope.xl : envelope.yh - envelope.yl;
    for (int node : hint.subject) {
      const auto b  = box(d, node);
      available    -= horizontal ? b.xh - b.xl : b.yh - b.yl;
    }
    gap = std::max(0., available / (hint.subject.size() - 1));
  }
  std::vector<std::pair<double, double>> targets;
  for (int node : hint.subject) {
    const auto b = box(d, node);
    double     x = (b.xl + b.xh) / 2 + dx, y = (b.yl + b.yh) / 2 + dy;
    if (hint.kind == "distribute") {
      const double dimension  = horizontal ? b.xh - b.xl : b.yh - b.yl;
      (horizontal ? x : y)    = cursor + dimension / 2;
      cursor                 += dimension + gap;
    }
    targets.emplace_back(x, y);
  }
  return targets;
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
    if (b.xh - b.xl > d.layout.xh - d.layout.xl || b.yh - b.yl > d.layout.yh - d.layout.yl) {
      return;
    }
    dx       = std::clamp(dx, d.layout.xl - b.xl, d.layout.xh - b.xh);
    dy       = std::clamp(dy, d.layout.yl - b.yl, d.layout.yh - b.yh);
    auto& s  = sums[i];
    s.x     += (cx + dx) * weight;
    s.y     += (cy + dy) * weight;
    s.w     += weight;
  };
  for (const auto& h : hints_) {
    if (h.status == "discarded" || h.kind == "position" || h.kind == "pin_facing" || h.subject.empty()) {
      continue;
    }
    if (h.kind == "matrix" || h.kind == "distribute" || h.kind == "anchor") {
      const auto targets = relationship_targets(h, d);
      for (size_t k = 0; k < h.subject.size(); ++k) {
        const auto i = h.subject[k];
        const auto b = box(d, i, false);
        add(i, targets[k].first - (b.xl + b.xh) / 2, targets[k].second - (b.yl + b.yh) / 2, h.weight);
      }
    } else if (h.kind == "cluster" || h.kind == "align") {
      auto [tx, ty] = group_center(d, h.subject);
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
      const auto [dx, dy] = relative_shift(bounds(d, h.subject), bounds(d, h.reference), h.kind, h.side, h.gap_x);
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
bool Placement_hints::has_pull_hints() const {
  return std::any_of(hints_.begin(), hints_.end(), [](const auto& h) {
    return h.status != "discarded" && !h.subject.empty() && h.kind != "position" && h.kind != "pin_facing";
  });
}

void Placement_hints::attach(dp::Options& control, std::function<std::vector<Hint_object>()> objects, bool dynamic) {
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
  if (!dynamic && !has_pull_hints()) {
    return;
  }
  constexpr int hint_interval = 25;
  const int     interval      = control.macro_pull_interval;
  const auto    prior         = interval > 0 ? control.macro_pull_update : nullptr;
  // DREAMPlace constructs a physical snapshot when the interval expires.
  // Keep both producers' due iterations without requesting every iteration.
  control.macro_pull_interval = prior ? std::gcd(interval, hint_interval) : hint_interval;
  control.macro_pull_update   = [this, prior, interval, objects, old = std::vector<dp::MacroPull>{}, had_hint_pulls = false](
                                    const dp::Design& d,
                                    int               iteration) mutable -> std::optional<std::vector<dp::MacroPull>> {
    bool changed = false;
    for (auto& p : old) {
      p.jump = false;  // A cached anchor persists; its one-shot move has already been delivered.
    }
    if (prior && iteration % interval == 0) {
      if (auto p = prior(d, iteration)) {
        old     = std::move(*p);
        changed = true;
      }
    }
    if (iteration % hint_interval != 0 && !changed) {
      return std::nullopt;
    }
    std::vector<dp::MacroPull> out;
    if (has_pull_hints()) {
      if (objects) {
        rebind(objects());
      }
      out = pulls(d);
    }
    // An empty replacement explicitly clears anchors and restarts momentum.
    // Return it once when a hint disappears, never just because the GUI is open.
    if (out.empty() && !changed && !had_hint_pulls) {
      return std::nullopt;
    }
    had_hint_pulls = !out.empty();
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
rapidjson::Document Placement_hints::preview(const dp::Design& d) const {
  auto report = this->report(d);
  auto& alloc = report.GetAllocator();
  J targets(rapidjson::kArrayType);
  auto target = [&](int i, double x, double y, std::string_view kind) {
    J t(rapidjson::kObjectType), from(rapidjson::kArrayType), to(rapidjson::kArrayType);
    const auto b = box(d, i, false);
    from.PushBack((b.xl + b.xh) / 2, alloc).PushBack((b.yl + b.yh) / 2, alloc);
    to.PushBack(x, alloc).PushBack(y, alloc).PushBack(b.xh - b.xl, alloc).PushBack(b.yh - b.yl, alloc);
    t.AddMember("name", J(objects_.at(i).name.c_str(), alloc), alloc);
    t.AddMember("kind", J(kind.data(), kind.size(), alloc), alloc);
    t.AddMember("from_center", from, alloc);
    t.AddMember("target_center_size", to, alloc);
    targets.PushBack(t, alloc);
  };
  for (const auto& pull : pulls(d)) {
    target(pull.node, pull.x, pull.y, "pull");
  }
  for (size_t k = 0; k < hints_.size(); ++k) {
    const auto& hint = hints_[k];
    if (hint.subject.empty() || hint.status == "discarded") {
      continue;
    }
    const int i = hint.subject.front();
    if (hint.kind == "position") {
      target(i, hint.x + d.node_size_x.at(i) / 2, hint.y + d.node_size_y.at(i) / 2, "position");
    } else if (hint.kind == "pin_facing") {
      auto& row = report["hints"][k];
      row.AddMember("side", J(hint.side.c_str(), alloc), alloc);
      const auto b = box(d, i, false);
      J edge(rapidjson::kArrayType);
      edge.PushBack(hint.side == "east" ? b.xh : b.xl, alloc).PushBack(hint.side == "north" ? b.yh : b.yl, alloc);
      edge.PushBack(hint.side == "west" ? b.xl : b.xh, alloc).PushBack(hint.side == "south" ? b.yl : b.yh, alloc);
      row.AddMember("pin_facing_edge_dbu", edge, alloc);
    }
  }
  report.AddMember("targets_dbu", targets, alloc);
  return report;
}

rapidjson::Document Placement_hints::report(const dp::Design& d, bool warnings) const {
  rapidjson::Document report;
  report.SetObject();
  auto& a      = report.GetAllocator();
  auto  string = [&](std::string_view s) { return J(s.data(), s.size(), a); };
  report.AddMember("class", "livehd.placement.hint_results", a);
  report.AddMember("schema_version", 1, a);
  report.AddMember("id", "placement", a);
  report.AddMember("design", string(design_), a);
  J results(rapidjson::kArrayType);
  for (auto h : hints_) {
    double residual = 0;
    bool fits_core = true;
    if (h.status != "discarded" && h.kind == "pin_facing") {
      int        i      = h.subject.front();
      const auto orient = d.node_orientation.empty() ? dp::Orientation::N : d.node_orientation.at(i);
      if (orient != dp::Orientation::N && orient != dp::Orientation::S && orient != dp::Orientation::FS
          && orient != dp::Orientation::FN) {
        h.status = "discarded";
        h.reason = "rotated macro pin geometry unsupported";
      } else {
        residual = pin_distance(h, d, orient);
        if (!std::isfinite(residual)) {
          residual = 0;
          h.status = "discarded";
          h.reason = "signal pin disappeared";
        }
      }
    } else if (h.status != "discarded" && (h.kind == "matrix" || h.kind == "distribute" || h.kind == "anchor")) {
      const auto targets = h.kind == "matrix" ? matrix_targets(h, d, &fits_core) : relationship_targets(h, d);
      for (size_t k = 0; k < h.subject.size(); ++k) {
        const auto b = box(d, h.subject[k], false);
        residual     = std::max(residual, std::hypot(targets[k].first - (b.xl + b.xh) / 2, targets[k].second - (b.yl + b.yh) / 2));
      }
    } else if (h.status != "discarded" && h.kind == "align") {
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
    } else if (h.status != "discarded" && (h.kind == "direction" || h.kind == "snap" || h.kind == "gap")) {
      const auto [dx, dy] = relative_shift(bounds(d, h.subject), bounds(d, h.reference), h.kind, h.side, h.gap_x);
      residual = std::hypot(dx, dy);
    } else if (h.status != "discarded" && h.kind == "cluster") {
      const auto [cx, cy] = group_center(d, h.subject);
      for (int i : h.subject) {
        const auto b = box(d, i, false);
        residual = std::max(residual, std::hypot((b.xl + b.xh) / 2 - cx, (b.yl + b.yh) / 2 - cy));
      }
    } else if (h.status != "discarded" && h.kind == "position") {
      const int i = h.subject.front();
      residual = std::hypot(d.node_x[i] - h.x, d.node_y[i] - h.y);
    }
    // Clusters request attraction without a maximum radius. An advisory seed
    // is applied only at initialization. Neither requires zero final distance.
    if (h.status == "accepted" && h.kind != "cluster" && h.kind != "position" && h.kind != "centroid"
        && (!fits_core || residual > std::max(d.layout.site_width, d.layout.row_height))) {
      h.status = "relaxed";
      h.reason = fits_core ? "soft relationship exceeds one placement pitch in the measured frame"
                           : "matrix with the requested gaps and macro halos is larger than the core";
      if (warnings) {
        diag::warn("pass.place", "placement-hint-relaxed", "unsupported")
            .msg("placement hint '{}': {}; residual {} um", h.id, h.reason, residual / dbu_)
            .emit();
      }
    }
    J r(rapidjson::kObjectType);
    r.AddMember("weight", h.weight, a);
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
  if (!centroid_report_.results.empty()) {
    J cs(rapidjson::kArrayType);
    for (const auto& c : centroid_report_.results) {
      J r(rapidjson::kObjectType);
      r.AddMember("id", string(c.id), a);
      r.AddMember("members", static_cast<uint64_t>(c.members), a);
      r.AddMember("unresolved", static_cast<uint64_t>(c.unresolved), a);
      r.AddMember("strength", c.strength, a);
      r.AddMember("compiled", c.group >= 0, a);
      r.AddMember("reason", string(c.reason), a);
      if (const auto it = centroid_report_.layout_residual.find(c.id); it != centroid_report_.layout_residual.end()) {
        r.AddMember("layout_residual_um", it->second / dbu_, a);
      }
      cs.PushBack(r, a);
    }
    report.AddMember("centroids", cs, a);
  }
  return report;
}
void Placement_hints::note_centroids(Centroid_compiled compiled) {
  compiled.groups.clear();
  centroid_report_ = std::move(compiled);
}
void Placement_hints::append_results(metadata::Bundle& out, const dp::Design& d) {
  out.replace(report(d, true));
}
}  // namespace livehd::placement

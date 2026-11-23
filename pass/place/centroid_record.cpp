// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "centroid_record.hpp"

#include <algorithm>
#include <limits>
#include <numeric>
#include <stdexcept>

namespace livehd::placement {
namespace dp = DreamPlace::native;
namespace {
using J = rapidjson::Value;

const char* position_name(Centroid_position_mode m) {
  switch (m) {
    case Centroid_position_mode::free    : return "free";
    case Centroid_position_mode::at      : return "at";
    case Centroid_position_mode::relative: return "relative";
  }
  return "free";
}
const char* layout_name(Centroid_layout_kind k) {
  switch (k) {
    case Centroid_layout_kind::floating: return "floating";
    case Centroid_layout_kind::row     : return "row";
    case Centroid_layout_kind::column  : return "column";
    case Centroid_layout_kind::array   : return "array";
  }
  return "floating";
}
std::string str(const J& v, const char* key, std::string fallback = {}) {
  if (!v.IsObject() || !v.HasMember(key)) {
    return fallback;
  }
  if (!v[key].IsString()) {
    throw std::invalid_argument(std::string(key) + " must be a string");
  }
  return {v[key].GetString(), v[key].GetStringLength()};
}
double number(const J& v, const char* key, double fallback) {
  if (!v.HasMember(key)) {
    return fallback;
  }
  if (!v[key].IsNumber() || !std::isfinite(v[key].GetDouble())) {
    throw std::invalid_argument(std::string(key) + " must be a finite number");
  }
  return v[key].GetDouble();
}
std::vector<std::string> split_path(const J& v) {
  std::vector<std::string> out;
  if (v.IsString()) {
    const std::string s(v.GetString(), v.GetStringLength());
    size_t            start = 0;
    do {
      const auto end = s.find('/', start);
      out.push_back(s.substr(start, end - start));
      if (end == std::string::npos) {
        break;
      }
      start = end + 1;
    } while (true);
  } else if (v.IsArray()) {
    for (const auto& part : v.GetArray()) {
      if (!part.IsString()) {
        throw std::invalid_argument("member path components must be strings");
      }
      out.emplace_back(part.GetString(), part.GetStringLength());
    }
  } else {
    throw std::invalid_argument("a member is an instance path or {\"centroid\": id}");
  }
  if (out.empty() || std::ranges::any_of(out, [](const auto& p) { return p.empty() || p == "." || p == ".."; })) {
    throw std::invalid_argument("member path must be nonempty and scope-relative");
  }
  return out;
}
std::string join_slash(const std::vector<std::string>& parts) {
  std::string s;
  for (const auto& p : parts) {
    if (!s.empty()) {
      s += '/';
    }
    s += p;
  }
  return s;
}
}  // namespace

Centroid parse_centroid(const J& v, const Centroid_names& names) {
  if (!v.IsObject()) {
    throw std::invalid_argument("centroid must be an object");
  }
  Centroid   c;
  const auto id = str(v, "id");
  if (id.empty()) {
    throw std::invalid_argument("centroid needs a nonempty id");
  }
  c.id = names.centroid ? names.centroid(id) : id;
  if (!v.HasMember("members") || !v["members"].IsArray() || v["members"].Empty()) {
    throw std::invalid_argument("centroid needs a nonempty members array");
  }
  for (const auto& m : v["members"].GetArray()) {
    if (m.IsObject() && m.HasMember("centroid")) {
      if (!m["centroid"].IsString() || m["centroid"].GetStringLength() == 0) {
        throw std::invalid_argument("a nested member is {\"centroid\": id}");
      }
      const std::string nested(m["centroid"].GetString(), m["centroid"].GetStringLength());
      c.members.push_back({names.centroid ? names.centroid(nested) : nested, true});
    } else {
      // A path, or {"instance": path, "share": s} for a member that counts for a part of its area.
      double share = 1;
      if (m.IsObject()) {
        if (!m.HasMember("instance")) {
          throw std::invalid_argument("a member is an instance path, {\"instance\": path, \"share\": s} or {\"centroid\": id}");
        }
        share = number(m, "share", 1);
        if (share <= 0 || share > 1) {
          throw std::invalid_argument("share must be in (0,1]");
        }
      }
      const auto      p = split_path(m.IsObject() ? m["instance"] : m);
      Centroid_member member{names.member ? names.member(p) : join_slash(p), false};
      member.share = share;
      c.members.push_back(std::move(member));
    }
  }
  if (v.HasMember("position")) {
    const auto& p = v["position"];
    if (!p.IsObject()) {
      throw std::invalid_argument("position must be an object");
    }
    const auto mode = str(p, "mode", "free");
    if (mode == "free") {
      c.position = Centroid_position_mode::free;
    } else if (mode == "at") {
      c.position = Centroid_position_mode::at;
      if (!p.HasMember("x_um") || !p.HasMember("y_um")) {
        throw std::invalid_argument("position at needs x_um and y_um");
      }
      c.x_um = number(p, "x_um", 0);
      c.y_um = number(p, "y_um", 0);
    } else if (mode == "relative") {
      c.position    = Centroid_position_mode::relative;
      const auto to = str(p, "to");
      if (to.empty()) {
        throw std::invalid_argument("position relative needs to");
      }
      c.to    = names.centroid ? names.centroid(to) : to;
      c.dx_um = number(p, "dx_um", 0);
      c.dy_um = number(p, "dy_um", 0);
    } else {
      throw std::invalid_argument("position mode must be free, at or relative");
    }
  }
  if (v.HasMember("layout")) {
    const auto& l = v["layout"];
    if (!l.IsObject()) {
      throw std::invalid_argument("layout must be an object");
    }
    const auto kind = str(l, "kind", "floating");
    if (kind == "floating") {
      c.layout = Centroid_layout_kind::floating;
    } else if (kind == "row") {
      c.layout = Centroid_layout_kind::row;
    } else if (kind == "column") {
      c.layout = Centroid_layout_kind::column;
    } else if (kind == "array") {
      c.layout = Centroid_layout_kind::array;
    } else {
      throw std::invalid_argument("layout kind must be floating, row, column or array");
    }
    c.gap_x_um = number(l, "gap_x_um", 0);
    c.gap_y_um = number(l, "gap_y_um", 0);
    if (l.HasMember("columns")) {
      if (!l["columns"].IsInt()) {
        throw std::invalid_argument("columns must be an integer");
      }
      c.columns = l["columns"].GetInt();
    }
  }
  if (v.HasMember("strength")) {
    c.strength = number(v, "strength", -1);
    if (c.strength < 0) {
      throw std::invalid_argument("strength must be nonnegative");
    }
  }
  if (v.HasMember("clock")) {
    const auto& k = v["clock"];
    if (!k.IsObject()) {
      throw std::invalid_argument("clock must be an object");
    }
    c.clock.drive = str(k, "drive");
    if (k.HasMember("arrival_ps")) {
      c.clock.arrival_ps = number(k, "arrival_ps", 0);
    }
  }
  if (v.HasMember("generated")) {
    if (!v["generated"].IsBool()) {
      throw std::invalid_argument("generated must be boolean");
    }
    c.generated = v["generated"].GetBool();
  }
  c.origin = str(v, "origin");
  return c;
}

void write_centroid(const Centroid& c, J& out, rapidjson::Document::AllocatorType& a,
                    const std::function<std::string(int)>& name_of) {
  auto text = [&](const std::string& s) { return J(s.c_str(), static_cast<rapidjson::SizeType>(s.size()), a); };
  out.SetObject();
  out.AddMember("id", text(c.id), a);
  out.AddMember("kind", "centroid", a);
  J members(rapidjson::kArrayType);
  for (const auto& m : c.members) {
    if (m.centroid) {
      J nested(rapidjson::kObjectType);
      nested.AddMember("centroid", text(m.name), a);
      members.PushBack(nested, a);
    } else {
      const std::string name = m.name.empty() && m.node >= 0 ? (name_of ? name_of(m.node) : "#" + std::to_string(m.node)) : m.name;
      if (m.share == 1) {
        members.PushBack(text(name), a);
      } else {
        J instance(rapidjson::kObjectType);
        instance.AddMember("instance", text(name), a);
        instance.AddMember("share", m.share, a);
        members.PushBack(instance, a);
      }
    }
  }
  out.AddMember("members", members, a);
  if (c.position != Centroid_position_mode::free) {
    J p(rapidjson::kObjectType);
    p.AddMember("mode", J(position_name(c.position), a), a);
    if (c.position == Centroid_position_mode::at) {
      p.AddMember("x_um", c.x_um, a);
      p.AddMember("y_um", c.y_um, a);
    } else {
      p.AddMember("to", text(c.to), a);
      p.AddMember("dx_um", c.dx_um, a);
      p.AddMember("dy_um", c.dy_um, a);
    }
    out.AddMember("position", p, a);
  }
  if (c.layout != Centroid_layout_kind::floating) {
    J l(rapidjson::kObjectType);
    l.AddMember("kind", J(layout_name(c.layout), a), a);
    if (c.layout == Centroid_layout_kind::array) {
      l.AddMember("columns", c.columns, a);
    }
    l.AddMember("gap_x_um", c.gap_x_um, a);
    l.AddMember("gap_y_um", c.gap_y_um, a);
    out.AddMember("layout", l, a);
  }
  if (c.strength >= 0) {
    out.AddMember("strength", c.strength, a);
  }
  if (!c.clock.empty()) {
    J k(rapidjson::kObjectType);
    if (!c.clock.drive.empty()) {
      k.AddMember("drive", text(c.clock.drive), a);
    }
    if (c.clock.arrival_ps) {
      k.AddMember("arrival_ps", *c.clock.arrival_ps, a);
    }
    out.AddMember("clock", k, a);
  }
  if (c.generated) {
    out.AddMember("generated", true, a);
  }
  if (!c.origin.empty()) {
    out.AddMember("origin", text(c.origin), a);
  }
}

namespace {
bool same_member(const Centroid_member& a, const Centroid_member& b) {
  if (a.centroid != b.centroid) {
    return false;
  }
  return !a.centroid && a.node >= 0 && b.node >= 0 ? a.node == b.node : a.name == b.name;
}
std::string malformed(const Centroid& c) {
  if (c.id.empty()) {
    return "centroid needs a nonempty id";
  }
  if (c.members.empty()) {
    return "centroid has no members";
  }
  if (!std::isfinite(c.strength) || (c.strength < 0 && c.strength != -1)) {
    return "invalid strength";
  }
  if (c.layout == Centroid_layout_kind::array && c.columns < 1) {
    return "an array layout needs columns >= 1";
  }
  if (!std::isfinite(c.gap_x_um) || !std::isfinite(c.gap_y_um) || c.gap_x_um < 0 || c.gap_y_um < 0) {
    return "layout gaps must be finite and nonnegative";
  }
  if (c.position == Centroid_position_mode::relative && (c.to.empty() || c.to == c.id)) {
    return "a relative position needs another centroid";
  }
  if (!std::isfinite(c.x_um) || !std::isfinite(c.y_um) || !std::isfinite(c.dx_um) || !std::isfinite(c.dy_um)) {
    return "position must be finite";
  }
  if (c.clock.arrival_ps && !std::isfinite(*c.clock.arrival_ps)) {
    return "clock arrival must be finite";
  }
  for (const auto& m : c.members) {
    if ((m.name.empty() && (m.centroid || m.node < 0)) || (m.centroid && m.name == c.id) || !std::isfinite(m.share) || m.share <= 0
        || m.share > 1) {
      return "invalid member";
    }
  }
  return {};
}
}  // namespace

void Centroid_set::reindex() {
  index_.clear();
  for (size_t i = 0; i < items_.size(); ++i) {
    index_[items_[i].id] = i;
  }
}
const Centroid* Centroid_set::find(const std::string& id) const {
  const auto it = index_.find(id);
  return it == index_.end() ? nullptr : &items_[it->second];
}
bool Centroid_set::add(Centroid c, std::string* reason) {
  auto fail = [&](std::string why) {
    if (reason) {
      *reason = std::move(why);
    }
    return false;
  };
  if (auto why = malformed(c); !why.empty()) {
    return fail(std::move(why));
  }
  if (index_.contains(c.id)) {
    return fail("duplicate centroid id '" + c.id + "'");
  }
  index_[c.id] = items_.size();
  items_.push_back(std::move(c));
  return true;
}
bool Centroid_set::put(Centroid c, std::string* reason) {
  if (auto why = malformed(c); !why.empty()) {
    if (reason) {
      *reason = std::move(why);
    }
    return false;
  }
  const auto it = index_.find(c.id);
  if (it == index_.end()) {
    return add(std::move(c), reason);
  }
  items_[it->second] = std::move(c);
  return true;
}
bool Centroid_set::erase(const std::string& id) {
  if (!index_.contains(id)) {
    return false;
  }
  std::set<std::string> removed{id};
  while (!removed.empty()) {
    std::erase_if(items_, [&](const Centroid& c) { return removed.contains(c.id); });
    std::set<std::string> emptied;
    for (auto& c : items_) {
      std::erase_if(c.members, [&](const Centroid_member& m) { return m.centroid && removed.contains(m.name); });
      if (c.position == Centroid_position_mode::relative && removed.contains(c.to)) {
        c.position = Centroid_position_mode::free;
        c.to.clear();
      }
      if (c.members.empty()) {
        emptied.insert(c.id);
      }
    }
    removed = std::move(emptied);
  }
  reindex();
  return true;
}
bool Centroid_set::add_member(const std::string& id, Centroid_member m, std::string* reason) {
  auto fail = [&](std::string why) {
    if (reason) {
      *reason = std::move(why);
    }
    return false;
  };
  const auto it = index_.find(id);
  if (it == index_.end()) {
    return fail("unknown centroid '" + id + "'");
  }
  if (m.centroid && (m.name == id || !index_.contains(m.name))) {
    return fail("unknown or self nested centroid '" + m.name + "'");
  }
  for (auto& c : items_) {
    if (c.id != id) {
      std::erase_if(c.members, [&](const Centroid_member& other) { return same_member(other, m); });
    }
  }
  auto& members = items_[it->second].members;
  if (std::ranges::none_of(members, [&](const Centroid_member& other) { return same_member(other, m); })) {
    members.push_back(std::move(m));
  }
  adopt(id);
  // A centroid emptied by the move leaves the set rather than becoming invalid.
  std::vector<std::string> emptied;
  for (const auto& c : items_) {
    if (c.members.empty()) {
      emptied.push_back(c.id);
    }
  }
  for (const auto& e : emptied) {
    erase(e);
  }
  return true;
}
bool Centroid_set::remove_member(const std::string& id, const Centroid_member& m, std::string* reason) {
  const auto it  = index_.find(id);
  const auto has = [&](const Centroid_member& other) { return same_member(other, m); };
  if (it == index_.end() || std::ranges::none_of(items_[it->second].members, has)) {
    if (reason) {
      *reason = it == index_.end() ? "unknown centroid '" + id + "'" : "'" + m.name + "' is not a member of '" + id + "'";
    }
    return false;
  }
  std::erase_if(items_[it->second].members, has);
  adopt(id);
  if (items_[it->second].members.empty()) {
    erase(id);
  }
  return true;
}
bool Centroid_set::move_member(const std::string& from, const std::string& to, const Centroid_member& m, std::string* reason) {
  const auto source = find(from);
  if (!source || std::ranges::none_of(source->members, [&](const Centroid_member& other) { return same_member(other, m); })) {
    if (reason) {
      *reason = "'" + m.name + "' is not a member of '" + from + "'";
    }
    return false;
  }
  return add_member(to, m, reason);
}
bool Centroid_set::adopt(const std::string& id) {
  const auto it = index_.find(id);
  if (it == index_.end()) {
    return false;
  }
  items_[it->second].generated = false;
  items_[it->second].origin.clear();
  return true;
}
std::vector<std::string> Centroid_set::replace_generated(const std::string& origin, std::vector<Centroid> fresh,
                                                         const std::function<int(const std::string&)>& resolve) {
  std::vector<std::string> dropped;
  std::erase_if(items_, [&](const Centroid& c) { return c.generated && c.origin == origin; });
  reindex();
  std::set<int>         owned_nodes;  // instances owned by an authored centroid, by node when it can be told
  std::set<std::string> owned_names;
  const auto            node_of
      = [&](const Centroid_member& m) { return m.node >= 0 ? m.node : (resolve && !m.name.empty() ? resolve(m.name) : -1); };
  for (const auto& c : items_) {
    if (c.generated) {
      continue;
    }
    for (const auto& m : c.members) {
      if (m.centroid) {
        continue;
      }
      owned_names.insert(m.name);
      if (const int n = node_of(m); n >= 0) {
        owned_nodes.insert(n);
      }
    }
  }
  for (auto& c : fresh) {
    c.generated = true;
    c.origin    = origin;
    if (index_.contains(c.id)) {
      dropped.push_back(c.id);
      continue;
    }
    std::erase_if(c.members, [&](const Centroid_member& m) {
      if (m.centroid) {
        return false;
      }
      const int n = node_of(m);
      return (n >= 0 && owned_nodes.contains(n)) || (!m.name.empty() && owned_names.contains(m.name));
    });
    const std::string id = c.id;
    if (c.members.empty() || !add(std::move(c))) {
      dropped.push_back(id);
    }
  }
  return dropped;
}
void Centroid_set::reload_authored(const Centroid_set& authored, const std::function<int(const std::string&)>& resolve) {
  std::map<std::string, std::vector<Centroid>> generated;
  for (const auto& c : items_) {
    if (c.generated) {
      generated[c.origin].push_back(c);
    }
  }
  *this = authored;
  for (auto& [origin, groups] : generated) {
    replace_generated(origin, std::move(groups), resolve);
  }
}
std::string Centroid_set::parent_of(const std::string& id) const {
  for (const auto& c : items_) {
    for (const auto& m : c.members) {
      if (m.centroid && m.name == id) {
        return c.id;
      }
    }
  }
  return {};
}
Centroid_clock Centroid_set::effective_clock(const std::string& id) const {
  Centroid_clock        out;
  std::set<std::string> seen;
  std::string           at = id;
  while (!at.empty() && seen.insert(at).second) {
    if (const auto* c = find(at)) {
      if (out.drive.empty()) {
        out.drive = c->clock.drive;
      }
      if (!out.arrival_ps) {
        out.arrival_ps = c->clock.arrival_ps;
      }
    }
    at = parent_of(at);
  }
  return out;
}
std::vector<Centroid_member> Centroid_set::leaves(const std::string& id) const {
  std::vector<Centroid_member>            out;
  std::set<std::string>                   seen;
  std::function<void(const std::string&)> walk = [&](const std::string& at) {
    const auto* c = find(at);
    if (!c || !seen.insert(at).second) {
      return;
    }
    for (const auto& m : c->members) {
      if (m.centroid) {
        walk(m.name);
      } else {
        out.push_back(m);
      }
    }
  };
  walk(id);
  return out;
}
std::vector<Centroid_issue> Centroid_set::validate() const {
  std::vector<Centroid_issue> out;
  std::set<std::string>       bad;
  auto                        flag = [&](const std::string& id, std::string why) {
    if (bad.insert(id).second) {
      out.push_back({id, std::move(why)});
    }
  };
  std::map<std::string, std::string> nested_parent;
  struct Owner {
    std::string id;
    double      share     = 0;
    bool        shareable = false;
  };
  std::map<std::string, Owner> owner;
  std::map<int, Owner>         node_owner;
  for (const auto& c : items_) {
    if (auto why = malformed(c); !why.empty()) {
      flag(c.id, std::move(why));
      continue;
    }
    for (const auto& m : c.members) {
      if (m.centroid) {
        if (!index_.contains(m.name)) {
          flag(c.id, "unknown nested centroid '" + m.name + "'");
        } else if (const auto [it, fresh] = nested_parent.emplace(m.name, c.id); !fresh) {
          flag(c.id, "centroid '" + m.name + "' is already nested in '" + it->second + "'");
        }
      } else {
        // PCRS divides one physical gate's area across its leaf groups. All other membership is exclusive.
        const bool shareable = c.generated && c.origin == "pcrs" && m.share < 1.0;
        const bool bound     = m.node >= 0 && m.name.empty();
        auto&      entry     = bound ? node_owner[m.node] : owner[m.name];
        if (entry.id == c.id || (!entry.id.empty() && (!entry.shareable || !shareable || entry.share + m.share > 1.0 + 1e-12))) {
          const auto name = bound ? "node " + std::to_string(m.node) : "'" + m.name + "'";
          flag(c.id, entry.id == c.id ? name + " is listed twice" : name + " is already a member of '" + entry.id + "'");
        } else {
          entry.id         = c.id;
          entry.share     += m.share;
          entry.shareable  = shareable;
        }
      }
    }
    if (c.position == Centroid_position_mode::relative && !index_.contains(c.to)) {
      flag(c.id, "relative position to unknown centroid '" + c.to + "'");
    }
  }
  // Cycles in nesting and in relative positions, and a position given to a nested centroid.
  for (const auto& c : items_) {
    if (bad.contains(c.id)) {
      continue;
    }
    std::set<std::string> seen{c.id};
    std::string           at = c.id;
    for (auto p = nested_parent.find(at); p != nested_parent.end(); p = nested_parent.find(at)) {
      at = p->second;
      if (!seen.insert(at).second) {
        flag(c.id, "nesting cycle through '" + at + "'");
        break;
      }
    }
    if (nested_parent.contains(c.id) && c.position != Centroid_position_mode::free) {
      flag(c.id, "a nested centroid takes its position from its parent's layout");
    }
    if (c.position == Centroid_position_mode::relative && !bad.contains(c.id)) {
      std::set<std::string> chain{c.id};
      const Centroid*       t = find(c.to);
      if (nested_parent.contains(c.to)) {
        flag(c.id, "a relative position needs a top-level centroid");
      }
      while (t && t->position == Centroid_position_mode::relative) {
        if (!chain.insert(t->id).second) {
          flag(c.id, "relative position cycle");
          break;
        }
        t = find(t->to);
      }
    }
  }
  return out;
}

namespace {
struct Block {
  std::vector<int>                       nodes;
  std::vector<double>                    areas;
  std::vector<std::pair<double, double>> own;    // leaf centers relative to this centroid, for its own group
  std::vector<std::pair<double, double>> shape;  // the same, as a parent sees them (a floating block keeps its present shape)
  double                                 w = 0, h = 0;
  std::pair<double, double>              center{0, 0};  // present area-weighted center
  double                                 area = 0;
};
struct Compiler {
  const Centroid_set&             set;
  const dp::Design&               d;
  const Centroid_compile_options& options;
  std::set<std::string>           bad;
  Centroid_compiled               out;
  std::map<std::string, Block>    blocks;
  std::set<std::string>           active;

  std::map<std::string, size_t> result_index;

  double cx(int i) const { return d.node_x[i] + d.node_size_x[i] / 2; }
  double cy(int i) const { return d.node_y[i] + d.node_size_y[i] / 2; }
  double strength_of(const Centroid& c) const { return c.strength >= 0 ? c.strength : options.default_strength; }

  // Shape of a floating set of leaves: their present centers around the area-weighted center.
  static void snapshot(Block& b, const dp::Design& d) {
    double area = 0, x = 0, y = 0;
    for (size_t k = 0; k < b.nodes.size(); ++k) {
      const int i  = b.nodes[k];
      area        += b.areas[k];
      x           += b.areas[k] * (d.node_x[i] + d.node_size_x[i] / 2);
      y           += b.areas[k] * (d.node_y[i] + d.node_size_y[i] / 2);
    }
    b.area    = area;
    b.center  = {x / area, y / area};
    double xl = 1e300, yl = 1e300, xh = -1e300, yh = -1e300;
    b.shape.clear();
    for (const int i : b.nodes) {
      const double px = d.node_x[i] + d.node_size_x[i] / 2, py = d.node_y[i] + d.node_size_y[i] / 2;
      b.shape.push_back({px - b.center.first, py - b.center.second});
      xl = std::min(xl, d.node_x[i]);
      yl = std::min(yl, d.node_y[i]);
      xh = std::max(xh, d.node_x[i] + d.node_size_x[i]);
      yh = std::max(yh, d.node_y[i] + d.node_size_y[i]);
    }
    b.w = xh - xl;
    b.h = yh - yl;
  }

  std::optional<Block> build(const Centroid& c) {
    if (const auto found = blocks.find(c.id); found != blocks.end()) {
      return found->second;
    }
    if (!active.insert(c.id).second) {
      return std::nullopt;
    }
    auto& res = out.results[result_index.at(c.id)];
    struct Item {
      Block                     block;
      std::pair<double, double> pos{0, 0};
    };
    std::vector<Item> items;
    std::set<int>     seen_nodes;
    for (const auto& m : c.members) {
      ++res.members;
      if (m.centroid) {
        const auto* child = set.find(m.name);
        auto        block = child && !bad.contains(m.name) ? build(*child) : std::nullopt;
        if (!block) {
          ++res.unresolved;
          continue;
        }
        items.push_back({
            std::move(*block),
            {0, 0}
        });
        continue;
      }
      const int i = m.node >= 0 ? m.node : (options.resolve && !m.name.empty() ? options.resolve(m.name) : -1);
      if (i < 0 || i >= static_cast<int>(d.node_x.size()) || d.node_size_x[i] <= 0 || d.node_size_y[i] <= 0
          || !seen_nodes.insert(i).second) {
        ++res.unresolved;
        continue;
      }
      Block leaf;
      leaf.nodes = {i};
      leaf.areas = {m.share * d.node_size_x[i] * d.node_size_y[i]};
      leaf.own   = {
          {0, 0}
      };
      leaf.shape = {
          {0, 0}
      };
      leaf.w      = d.node_size_x[i];
      leaf.h      = d.node_size_y[i];
      leaf.area   = leaf.areas[0];
      leaf.center = {cx(i), cy(i)};
      items.push_back({
          std::move(leaf),
          {0, 0}
      });
    }
    active.erase(c.id);
    if (items.empty()) {
      bad.insert(c.id);
      res.reason = "no member resolved";
      return std::nullopt;
    }
    const double gx = c.gap_x_um * options.dbu, gy = c.gap_y_um * options.dbu;
    switch (c.layout) {
      case Centroid_layout_kind::floating: break;
      case Centroid_layout_kind::row     : {
        double x = 0;
        for (auto& it : items) {
          it.pos  = {x + it.block.w / 2, 0};
          x      += it.block.w + gx;
        }
        break;
      }
      case Centroid_layout_kind::column: {
        double y = 0;
        for (auto& it : items) {
          it.pos  = {0, y + it.block.h / 2};
          y      += it.block.h + gy;
        }
        break;
      }
      case Centroid_layout_kind::array: {
        const size_t        columns = std::min(items.size(), static_cast<size_t>(c.columns));
        const size_t        rows    = (items.size() + columns - 1) / columns;
        std::vector<double> width(columns, 0), height(rows, 0);
        for (size_t k = 0; k < items.size(); ++k) {
          width[k % columns]  = std::max(width[k % columns], items[k].block.w);
          height[k / columns] = std::max(height[k / columns], items[k].block.h);
        }
        std::vector<double> x0(columns, 0), y0(rows, 0);
        for (size_t k = 1; k < columns; ++k) {
          x0[k] = x0[k - 1] + width[k - 1] + gx;
        }
        for (size_t k = 1; k < rows; ++k) {
          y0[k] = y0[k - 1] + height[k - 1] + gy;
        }
        for (size_t k = 0; k < items.size(); ++k) {
          items[k].pos = {x0[k % columns] + width[k % columns] / 2, y0[k / columns] + height[k / columns] / 2};
        }
        break;
      }
    }
    Block b;
    for (const auto& it : items) {
      for (size_t k = 0; k < it.block.nodes.size(); ++k) {
        b.nodes.push_back(it.block.nodes[k]);
        b.areas.push_back(it.block.areas[k]);
        const auto& s = it.block.shape[k];
        b.own.push_back({it.pos.first + s.first, it.pos.second + s.second});
      }
    }
    // Present area-weighted center and shape of everything below.
    Block present = b;
    snapshot(present, d);
    b.area   = present.area;
    b.center = present.center;
    if (c.layout == Centroid_layout_kind::floating) {
      // Leaves are pulled to the point itself; nested blocks keep their own shape around theirs.
      size_t at = 0;
      for (const auto& it : items) {
        const bool single = it.block.nodes.size() == 1 && it.block.own.size() == 1;
        for (size_t k = 0; k < it.block.nodes.size(); ++k, ++at) {
          b.own[at] = single ? std::pair<double, double>{0, 0} : it.block.shape[k];
        }
      }
      b.shape = present.shape;
      b.w     = present.w;
      b.h     = present.h;
    } else {
      // Center the layout so that the area-weighted mean of the slots is the point.
      double mx = 0, my = 0;
      for (size_t k = 0; k < b.own.size(); ++k) {
        mx += b.areas[k] * b.own[k].first;
        my += b.areas[k] * b.own[k].second;
      }
      mx /= b.area;
      my /= b.area;
      for (auto& p : b.own) {
        p.first  -= mx;
        p.second -= my;
      }
      b.shape   = b.own;
      double xl = 1e300, yl = 1e300, xh = -1e300, yh = -1e300;
      for (const auto& it : items) {
        xl = std::min(xl, it.pos.first - it.block.w / 2);
        xh = std::max(xh, it.pos.first + it.block.w / 2);
        yl = std::min(yl, it.pos.second - it.block.h / 2);
        yh = std::max(yh, it.pos.second + it.block.h / 2);
      }
      b.w             = xh - xl;
      b.h             = yh - yl;
      double residual = 0;
      for (size_t k = 0; k < b.nodes.size(); ++k) {
        const int i = b.nodes[k];
        residual    = std::max(residual,
                            std::hypot(cx(i) - (b.center.first + b.own[k].first), cy(i) - (b.center.second + b.own[k].second)));
      }
      out.layout_residual[c.id] = residual;
    }
    blocks.emplace(c.id, b);
    const double s = strength_of(c);
    res.strength   = s;
    if (s > 0) {
      dp::CohesionGroup g;
      g.nodes  = b.nodes;
      g.areas  = b.areas;
      g.weight = s;
      for (const auto& p : b.own) {
        g.offsets.push_back({-p.first, -p.second});
      }
      if (c.position == Centroid_position_mode::at) {
        g.has_target = true;
        g.target_x   = c.x_um * options.dbu;
        g.target_y   = c.y_um * options.dbu;
      }
      res.group = static_cast<int>(out.groups.size());
      out.groups.push_back(std::move(g));
    } else {
      res.reason = "zero strength";
    }
    return b;
  }
};
}  // namespace

Centroid_compiled compile_centroids(const Centroid_set& set, const dp::Design& d, const Centroid_compile_options& options) {
  Compiler cc{set, d, options, {}, {}, {}, {}, {}};
  for (const auto& c : set.items()) {
    cc.result_index.emplace(c.id, cc.out.results.size());
    cc.out.results.push_back({c.id, 0, 0, -1, 0, {}});
  }
  for (const auto& issue : set.validate()) {
    cc.bad.insert(issue.id);
    cc.out.results[cc.result_index.at(issue.id)].reason = issue.reason;
  }
  // Children are built (and emit their group) before the parent that lays them out.
  for (const auto& c : set.items()) {
    if (!cc.bad.contains(c.id)) {
      cc.build(c);
    }
  }
  // A centroid placed relative to another keeps their blocks apart by the offset: one more group over both.
  for (const auto& c : set.items()) {
    if (cc.bad.contains(c.id) || c.position != Centroid_position_mode::relative) {
      continue;
    }
    const auto   a = cc.blocks.find(c.id), b = cc.blocks.find(c.to);
    const double s = cc.strength_of(c);
    if (a == cc.blocks.end() || b == cc.blocks.end() || s <= 0) {
      continue;
    }
    const double      dx = c.dx_um * options.dbu, dy = c.dy_um * options.dbu;
    const double      wa = a->second.area, wb = b->second.area;
    dp::CohesionGroup g;
    g.weight = s;
    // This block sits at V + D*wb/(wa+wb), the other at V - D*wa/(wa+wb); V is their area-weighted center.
    for (const auto& [block, fraction_x, fraction_y] : {
             std::tuple{&a->second,  dx * wb / (wa + wb),  dy * wb / (wa + wb)},
             std::tuple{&b->second, -dx * wa / (wa + wb), -dy * wa / (wa + wb)}
    }) {
      for (size_t k = 0; k < block->nodes.size(); ++k) {
        g.nodes.push_back(block->nodes[k]);
        g.areas.push_back(block->areas[k]);
        g.offsets.push_back({-(fraction_x + block->shape[k].first), -(fraction_y + block->shape[k].second)});
      }
    }
    cc.out.groups.push_back(std::move(g));
  }
  return std::move(cc.out);
}
}  // namespace livehd::placement

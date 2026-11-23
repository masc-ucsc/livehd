// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "gui_centroid.hpp"

#include <algorithm>
#include <set>
#include <stdexcept>

#include "core/metadata.hpp"
#include "pass/place/centroid_record.hpp"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"

namespace livehd::placement::gui {
namespace {
using J = rapidjson::Value;
using A = rapidjson::Document::AllocatorType;

std::string text(const J& v) {
  rapidjson::StringBuffer                    buffer;
  rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
  v.Accept(writer);
  return buffer.GetString();
}
std::string str(const J& v, const char* key) {
  if (!v.IsObject() || !v.HasMember(key) || !v[key].IsString()) {
    return {};
  }
  return {v[key].GetString(), v[key].GetStringLength()};
}
// The members of a request as record members: names, nested centroids, or the page selection.
std::vector<Centroid_member> members_of(const J& request, const char* key, const std::vector<std::string>& selection) {
  std::vector<Centroid_member> out;
  if (!request.HasMember(key)) {
    return out;
  }
  const auto& m = request[key];
  if (m.IsString() && std::string_view(m.GetString()) == "selected") {
    if (selection.empty()) {
      throw std::invalid_argument("select one or more instances in the page first");
    }
    for (const auto& name : selection) {
      out.push_back({name, false});
    }
    return out;
  }
  if (!m.IsArray()) {
    throw std::invalid_argument("members must be an array of instance names, {\"centroid\": id} or the string \"selected\"");
  }
  for (const auto& e : m.GetArray()) {
    if (e.IsString()) {
      out.push_back({std::string(e.GetString(), e.GetStringLength()), false});
    } else if (e.IsObject() && e.HasMember("centroid") && e["centroid"].IsString()) {
      out.push_back({std::string(e["centroid"].GetString(), e["centroid"].GetStringLength()), true});
    } else {
      throw std::invalid_argument("a member is an instance name or {\"centroid\": id}");
    }
  }
  return out;
}
void put(J& object, const char* key, J&& value, A& a) {
  object.RemoveMember(key);
  object.AddMember(J(key, a), value, a);
}
// Overlay the fields of a request on a centroid record (as JSON), so the one parser validates every edit.
void overlay(J& record, const J& request, A& a, double fallback_strength) {
  if (request.HasMember("position")) {
    const auto& p = request["position"];
    J           position(rapidjson::kObjectType);
    if (p.IsString()) {
      position.AddMember("mode", J(p.GetString(), p.GetStringLength(), a), a);
    } else if (p.IsArray() && p.Size() == 2 && p[0].IsNumber() && p[1].IsNumber()) {
      position.AddMember("mode", "at", a).AddMember("x_um", p[0].GetDouble(), a).AddMember("y_um", p[1].GetDouble(), a);
    } else if (p.IsObject()) {
      position.CopyFrom(p, a);
    } else {
      throw std::invalid_argument("position is free, [x_um, y_um] or {mode: free|at|relative, ...}");
    }
    put(record, "position", std::move(position), a);
  }
  if (request.HasMember("layout")) {
    const auto& l = request["layout"];
    J           layout(rapidjson::kObjectType);
    if (l.IsString()) {
      layout.AddMember("kind", J(l.GetString(), l.GetStringLength(), a), a);
    } else if (l.IsObject()) {
      layout.CopyFrom(l, a);
    } else {
      throw std::invalid_argument("layout is a kind name or {kind: floating|row|column|array, columns?, gap_x_um?, gap_y_um?}");
    }
    put(record, "layout", std::move(layout), a);
  }
  if (request.HasMember("strength")) {
    if (!request["strength"].IsNumber()) {
      throw std::invalid_argument("strength must be a number");
    }
    put(record, "strength", J(request["strength"].GetDouble()), a);
  }
  if (request.HasMember("strength_scale")) {
    if (!request["strength_scale"].IsNumber() || request["strength_scale"].GetDouble() <= 0) {
      throw std::invalid_argument("strength_scale must be a positive number");
    }
    const double base
        = record.HasMember("strength") && record["strength"].IsNumber() ? record["strength"].GetDouble() : fallback_strength;
    put(record, "strength", J(base * request["strength_scale"].GetDouble()), a);
  }
  if (request.HasMember("clock")) {
    if (!request["clock"].IsObject()) {
      throw std::invalid_argument("clock must be an object {drive?, arrival_ps?}");
    }
    J clock(rapidjson::kObjectType);
    if (record.HasMember("clock") && record["clock"].IsObject()) {
      clock.CopyFrom(record["clock"], a);
    }
    for (const auto& field : request["clock"].GetObject()) {
      clock.RemoveMember(field.name);
      clock.AddMember(J(field.name, a), J(field.value, a), a);
    }
    put(record, "clock", std::move(clock), a);
  }
}
}  // namespace

Centroid_edit edit_centroids(const std::string& intent, const std::string& design, const std::string& module, const J& request,
                             const std::vector<std::string>& selection, double fallback_strength,
                             const std::function<std::vector<std::string>(const std::string&)>& generated_members) {
  const auto op = str(request, "op");
  if (op.empty()) {
    throw std::invalid_argument("centroid_edit needs op: create|delete|add|remove|move|set|adopt");
  }
  metadata::Bundle    bundle(intent.empty() ? std::string("[]") : intent);
  rapidjson::Document record(rapidjson::kObjectType);
  auto&               a     = record.GetAllocator();
  bool                found = false;
  for (const auto* r : bundle.select("livehd.placement.hints", design)) {
    if (str(*r, "id") == kCentroidRecord) {
      record.CopyFrom(*r, a);
      found = true;
    }
  }
  if (!found) {
    record.AddMember("class", "livehd.placement.hints", a)
        .AddMember("schema_version", 2, a)
        .AddMember("id", J(kCentroidRecord, a), a)
        .AddMember("design", J(design.c_str(), a), a)
        .AddMember("templates", J(rapidjson::kArrayType), a);
  }
  if (!record.HasMember("templates") || !record["templates"].IsArray()) {
    throw std::invalid_argument(std::string("record '") + kCentroidRecord + "' has no templates array");
  }
  J* tmpl = nullptr;
  for (auto& t : record["templates"].GetArray()) {
    if (str(t, "module") == module) {
      tmpl = &t;
    }
  }
  if (!tmpl) {
    J t(rapidjson::kObjectType);
    t.AddMember("module", J(module.c_str(), a), a).AddMember("hints", J(rapidjson::kArrayType), a);
    record["templates"].PushBack(t, a);
    tmpl = &record["templates"][record["templates"].Size() - 1];
  }
  if (!tmpl->HasMember("hints") || !(*tmpl)["hints"].IsArray()) {
    throw std::invalid_argument("template hints must be an array");
  }
  Centroid_set set;
  J            others(rapidjson::kArrayType);
  for (const auto& h : (*tmpl)["hints"].GetArray()) {
    if (str(h, "kind") == "centroid") {
      std::string why;
      if (!set.add(parse_centroid(h), &why)) {
        throw std::invalid_argument(why);
      }
    } else {
      others.PushBack(J(h, a), a);
    }
  }
  const auto id            = str(request, "id");
  const auto require_known = [&](const std::string& which) {
    if (!set.find(which)) {
      throw std::invalid_argument("no centroid '" + which + "' in the GUI's centroid record (centroids from other records or the "
                                  "flow are read-only; adopt a generated one first)");
    }
  };
  const auto fail = [](const std::string& why) {
    if (!why.empty()) {
      throw std::invalid_argument(why);
    }
  };
  std::string touched = id;
  if (op == "create" || op == "adopt") {
    std::vector<Centroid_member> members;
    std::string                  made = id;
    J                            fields(rapidjson::kObjectType);
    if (op == "adopt") {
      if (id.empty()) {
        throw std::invalid_argument("adopt needs the id of a generated centroid");
      }
      const auto names = generated_members ? generated_members(id) : std::vector<std::string>{};
      if (names.empty()) {
        throw std::invalid_argument("no generated centroid '" + id + "' (see the centroids command)");
      }
      for (const auto& name : names) {
        members.push_back({name, false});
      }
      made = str(request, "as");
      if (made.empty()) {
        made = id;
        std::ranges::replace(made, ':', '_');
      }
    } else {
      members = members_of(request, "members", selection);
      if (members.empty()) {
        throw std::invalid_argument("create needs members");
      }
      if (made.empty()) {
        for (size_t n = set.size() + 1;; ++n) {
          made = "c" + std::to_string(n);
          if (!set.find(made)) {
            break;
          }
        }
      }
    }
    if (set.find(made)) {
      throw std::invalid_argument("centroid '" + made + "' exists; use set");
    }
    J rec(rapidjson::kObjectType);
    rec.AddMember("id", J(made.c_str(), a), a).AddMember("kind", "centroid", a);
    J list(rapidjson::kArrayType);
    for (const auto& m : members) {
      if (m.centroid) {
        J nested(rapidjson::kObjectType);
        nested.AddMember("centroid", J(m.name.c_str(), a), a);
        list.PushBack(nested, a);
      } else {
        list.PushBack(J(m.name.c_str(), a), a);
      }
    }
    rec.AddMember("members", list, a);
    overlay(rec, request, a, fallback_strength);
    auto c   = parse_centroid(rec);
    c.origin = "gui";
    for (const auto& m : c.members) {
      // A member lives in one centroid: it leaves the previous one.
      if (!m.centroid) {
        for (const auto& other : std::vector<Centroid>(set.items())) {
          set.remove_member(other.id, m);
        }
      }
    }
    std::string why;
    if (!set.add(std::move(c), &why)) {
      throw std::invalid_argument(why);
    }
    touched = made;
  } else if (op == "delete") {
    require_known(id);
    set.erase(id);
  } else if (op == "add" || op == "remove") {
    require_known(id);
    const auto members = members_of(request, "members", selection);
    if (members.empty()) {
      throw std::invalid_argument(op + " needs members");
    }
    for (const auto& m : members) {
      std::string why;
      if (op == "add" ? !set.add_member(id, m, &why) : !set.remove_member(id, m, &why)) {
        throw std::invalid_argument(why);
      }
    }
  } else if (op == "move") {
    touched = str(request, "to");
    require_known(touched);
    const auto members = members_of(request, "members", selection);
    if (members.empty()) {
      throw std::invalid_argument("move needs members");
    }
    for (const auto& m : members) {
      std::string why;
      if (!set.add_member(touched, m, &why)) {
        throw std::invalid_argument(why);
      }
    }
  } else if (op == "set") {
    require_known(id);
    const auto* existing = set.find(id);
    J           rec(rapidjson::kObjectType);
    write_centroid(*existing, rec, a);
    if (request.HasMember("members")) {
      const auto members = members_of(request, "members", selection);
      J          list(rapidjson::kArrayType);
      for (const auto& m : members) {
        if (m.centroid) {
          J nested(rapidjson::kObjectType);
          nested.AddMember("centroid", J(m.name.c_str(), a), a);
          list.PushBack(nested, a);
        } else {
          list.PushBack(J(m.name.c_str(), a), a);
        }
      }
      put(rec, "members", std::move(list), a);
    }
    overlay(rec, request, a, fallback_strength);
    auto c   = parse_centroid(rec);
    c.origin = "gui";
    std::string why;
    if (!set.put(std::move(c), &why)) {
      throw std::invalid_argument(why);
    }
  } else {
    throw std::invalid_argument("unknown centroid_edit op '" + op + "'");
  }
  for (const auto& issue : set.validate()) {
    fail("centroid '" + issue.id + "': " + issue.reason);
  }
  // Rebuild the template's hints: whatever was not a centroid, then the centroids in set order.
  J hints(rapidjson::kArrayType);
  for (auto& o : others.GetArray()) {
    hints.PushBack(o, a);
  }
  J written(rapidjson::kArrayType);
  for (const auto& c : set.items()) {
    J rec;
    write_centroid(c, rec, a);
    hints.PushBack(J(rec, a), a);
    written.PushBack(rec, a);
  }
  tmpl->RemoveMember("hints");
  tmpl->AddMember("hints", hints, a);
  bundle.replace(record);
  rapidjson::Document reply(rapidjson::kObjectType);
  auto&               b = reply.GetAllocator();
  reply.AddMember("ok", true, b)
      .AddMember("op", J(op.c_str(), b), b)
      .AddMember("id", J(touched.c_str(), b), b)
      .AddMember("centroids", J(written, b), b);
  return {bundle.json(), text(reply)};
}

std::string list_centroid_records(const std::string& intent, const std::string& design) {
  metadata::Bundle    bundle(intent.empty() ? std::string("[]") : intent);
  rapidjson::Document out(rapidjson::kObjectType);
  auto&               a = out.GetAllocator();
  J                   list(rapidjson::kArrayType);
  for (const auto* r : bundle.select("livehd.placement.hints", design)) {
    if (!r->HasMember("templates") || !(*r)["templates"].IsArray()) {
      continue;
    }
    for (const auto& t : (*r)["templates"].GetArray()) {
      if (!t.HasMember("hints") || !t["hints"].IsArray()) {
        continue;
      }
      for (const auto& h : t["hints"].GetArray()) {
        if (str(h, "kind") == "centroid") {
          J c(h, a);
          c.AddMember("record", J(str(*r, "id").c_str(), a), a);
          c.AddMember("module", J(str(t, "module").c_str(), a), a);
          list.PushBack(c, a);
        }
      }
    }
  }
  out.AddMember("centroids", list, a);
  return text(out);
}
}  // namespace livehd::placement::gui

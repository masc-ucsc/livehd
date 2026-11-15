// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <cmath>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include "core/metadata.hpp"

namespace livehd::placement {
struct Macro_pose {
  std::string name, master, orientation;
  double      x_um, y_um;
};

// Absolute physical names are single path components: hierarchy separators can be literal characters in a DEF name.
inline rapidjson::Document macro_state(const std::string& design, const std::vector<Macro_pose>& poses) {
  rapidjson::Document record(rapidjson::kObjectType);
  auto& a = record.GetAllocator();
  using V = rapidjson::Value;
  auto string = [&](const std::string& s) { return V(s.c_str(), s.size(), a); };
  record.AddMember("class", "livehd.placement.state", a);
  record.AddMember("schema_version", 1, a);
  record.AddMember("id", "macro-positions", a);
  record.AddMember("design", string(design), a);
  record.AddMember("automatic_reuse", false, a);
  V templates(rapidjson::kArrayType), t(rapidjson::kObjectType), positions(rapidjson::kArrayType);
  t.AddMember("module", string(design), a);
  for (const auto& pose : poses) {
    V p(rapidjson::kObjectType), subject(rapidjson::kArrayType);
    p.AddMember("id", string(pose.name), a);
    p.AddMember("kind", "position", a);
    subject.PushBack(string(pose.name), a);
    p.AddMember("subject", subject, a);
    p.AddMember("master", string(pose.master), a);
    p.AddMember("frame", "floorplan", a);
    p.AddMember("anchor", "lower_left", a);
    p.AddMember("x_um", pose.x_um, a);
    p.AddMember("y_um", pose.y_um, a);
    p.AddMember("orientation", string(pose.orientation), a);
    positions.PushBack(p, a);
  }
  t.AddMember("hints", positions, a);
  templates.PushBack(t, a);
  record.AddMember("templates", templates, a);
  return record;
}

inline std::vector<Macro_pose> read_macro_state(const metadata::Bundle& input, const std::string& design) {
  std::vector<Macro_pose> result;
  std::set<std::string> names;
  const auto string = [](const rapidjson::Value& obj, const char* key) -> std::string {
    if (!obj.IsObject() || !obj.HasMember(key) || !obj[key].IsString() || !obj[key].GetStringLength()) {
      throw std::invalid_argument(std::string("placement state needs a string: ") + key);
    }
    return {obj[key].GetString(), obj[key].GetStringLength()};
  };
  for (const auto* r : input.select("livehd.placement.state", design)) {
    if (!r->HasMember("schema_version") || !(*r)["schema_version"].IsUint() || (*r)["schema_version"].GetUint() != 1
        || string(*r, "design") != design || !r->HasMember("templates") || !(*r)["templates"].IsArray()) {
      throw std::invalid_argument("invalid placement state for " + design);
    }
    for (const auto& t : (*r)["templates"].GetArray()) {
      if (string(t, "module") != design || !t.HasMember("hints") || !t["hints"].IsArray()) {
        throw std::invalid_argument("placement state needs the top module and its macro positions");
      }
      for (const auto& p : t["hints"].GetArray()) {
        if (string(p, "kind") != "position" || string(p, "frame") != "floorplan" || string(p, "anchor") != "lower_left"
            || !p.HasMember("subject") || !p["subject"].IsArray() || p["subject"].Size() != 1 || !p["subject"][0].IsString()
            || !p["subject"][0].GetStringLength()) {
          throw std::invalid_argument("placement state needs one exact macro name and a floorplan lower-left position");
        }
        std::string name(p["subject"][0].GetString(), p["subject"][0].GetStringLength());
        if (!names.insert(name).second) {
          throw std::invalid_argument("duplicate placement state for macro " + name);
        }
        const auto coordinate = [&](const char* key) {
          if (!p.HasMember(key) || !p[key].IsNumber() || !std::isfinite(p[key].GetDouble())) {
            throw std::invalid_argument("invalid placement state coordinate for " + name);
          }
          return p[key].GetDouble();
        };
        result.push_back({name, p.HasMember("master") ? string(p, "master") : "", string(p, "orientation"),
                          coordinate("x_um"), coordinate("y_um")});
      }
    }
  }
  return result;
}
}  // namespace livehd::placement

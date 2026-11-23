// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <string>
#include <string_view>

#include "attrs.hpp"
#include "hhds/graph.hpp"
#include "rapidjson/document.h"

namespace livehd::coloring_info {

inline rapidjson::Document read(const hhds::Graph& graph) {
  rapidjson::Document doc;
  if (auto attr = graph.get_input_node().attr(attrs::coloring_info); attr.has()) {
    const std::string_view json{attr.get()};
    doc.Parse(json.data(), json.size());
  }
  return doc;
}

inline const rapidjson::Value* member(const rapidjson::Value& object, const char* key) {
  if (object.IsObject()) {
    const auto it = object.FindMember(key);
    if (it != object.MemberEnd()) {
      return &it->value;
    }
  }
  return nullptr;
}

inline bool flag(const rapidjson::Value& object, const char* key) {
  const auto* value = member(object, key);
  return value && value->IsTrue();
}

// Older producers stored parameters at the top level. Current algorithms
// put them under params; a packed source group can still set the root flag.
inline bool parameter(const rapidjson::Value& doc, const char* key) {
  const auto* params = member(doc, "params");
  return flag(doc, key) || (params && flag(*params, key));
}

inline bool algorithm(const rapidjson::Value& doc, const char* name) {
  const auto* value = member(doc, "algorithm");
  return value && *value == name;
}

inline bool seeded(const rapidjson::Value& doc) { return algorithm(doc, "block-attr") || flag(doc, "seeded"); }

inline std::string group_name(const rapidjson::Value& doc, int color) {
  const auto* groups = member(doc, "synth_groups");
  const auto* group  = groups ? member(*groups, std::to_string(color).c_str()) : nullptr;
  const auto* name   = group ? member(*group, "name") : nullptr;
  return name && name->IsString() ? std::string{name->GetString(), name->GetStringLength()} : std::string{};
}

}  // namespace livehd::coloring_info

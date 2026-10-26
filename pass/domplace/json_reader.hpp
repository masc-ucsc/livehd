// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <optional>
#include <string>

#include "rapidjson/document.h"

namespace livehd::domplace::json {

// Keyed readers of the bundle sidecar. A missing or mistyped value sets
// `malformed` instead of throwing, so a reader can refuse once with a reason.
// A labeled estimate is a number or null (unmeasured); null is never zero.
inline std::optional<double> read_estimate(const rapidjson::Value& object, const char* key, bool& malformed) {
  if (!object.IsObject() || !object.HasMember(key) || !(object[key].IsNumber() || object[key].IsNull())) {
    malformed = true;
    return std::nullopt;
  }
  return object[key].IsNumber() ? std::optional<double>{object[key].GetDouble()} : std::nullopt;
}
inline std::string read_text(const rapidjson::Value& object, const char* key, bool& malformed) {
  if (!object.IsObject() || !object.HasMember(key) || !object[key].IsString()) {
    malformed = true;
    return {};
  }
  return {object[key].GetString(), object[key].GetStringLength()};
}

}  // namespace livehd::domplace::json

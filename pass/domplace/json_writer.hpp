// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"

namespace livehd::domplace::json {

// Keyed writers shared by the report block and the bundle sidecar.
using Writer = rapidjson::Writer<rapidjson::StringBuffer>;
inline void string(Writer& w, std::string_view value) { w.String(value.data(), static_cast<rapidjson::SizeType>(value.size())); }
inline void text(Writer& w, const char* key, std::string_view value) {
  w.Key(key);
  string(w, value);
}
inline void number(Writer& w, const char* key, uint64_t value) {
  w.Key(key);
  w.Uint64(value);
}
// Missing data is written as null, never as zero.
inline void real(Writer& w, const char* key, std::optional<double> value) {
  w.Key(key);
  if (value) {
    w.Double(*value);
  } else {
    w.Null();
  }
}
inline void flag(Writer& w, const char* key, bool value) {
  w.Key(key);
  w.Bool(value);
}

}  // namespace livehd::domplace::json

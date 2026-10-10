// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <charconv>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"

namespace livehd::synth_attr {
// Values remain typed JSON scalars. In particular 1 and "1" are distinct IDs.
struct Hint {
  std::string value;
  int         rank = 0;
};
using Policy = std::map<std::string, Hint>;
inline std::string json(const rapidjson::Value& v) {
  rapidjson::StringBuffer                    b;
  rapidjson::Writer<rapidjson::StringBuffer> w(b);
  v.Accept(w);
  return {b.GetString(), b.GetSize()};
}
inline std::string quote(std::string_view s) {
  rapidjson::StringBuffer                    b;
  rapidjson::Writer<rapidjson::StringBuffer> w(b);
  w.String(s.data(), s.size());
  return {b.GetString(), b.GetSize()};
}
inline std::string text(std::string_view v) {
  rapidjson::Document d;
  d.Parse(v.data(), v.size());
  return !d.HasParseError() && d.IsString() ? std::string(d.GetString(), d.GetStringLength()) : std::string(v);
}
inline std::string literal(std::string_view s) {
  if (s.size() >= 2 && s.front() == '\'' && s.back() == '\'') {
    return quote(s.substr(1, s.size() - 2));
  }
  rapidjson::Document d;
  d.Parse(s.data(), s.size());
  if (d.HasParseError()) {
    throw std::invalid_argument("synthesis attributes require a literal value");
  }
  return json(d);
}
inline void validate(std::string_view key, std::string_view value) {
  rapidjson::Document d;
  d.Parse(value.data(), value.size());
  const auto bad = [&] { throw std::invalid_argument("invalid synth." + std::string(key) + "=" + std::string(value)); };
  if (d.HasParseError()) {
    bad();
    return;
  }
  if (key == "color") {
    if (!d.IsInt64() && !(d.IsString() && d.GetStringLength())) {
      bad();
    }
    return;
  }
  if (key == "grow" || key == "ware") {
    if (!d.IsBool()) {
      bad();
    }
    return;
  }
  if (key == "block_size" || key == "delay") {
    if (!d.IsUint()) {
      bad();
    }
    return;
  }
  if (key == "abc") {
    if (!d.IsString()) {
      bad();
    }
    return;
  }
  if (!d.IsString()) {
    bad();
    return;
  }
  const std::string_view s{d.GetString(), d.GetStringLength()};
  if (key == "adder") {
    if (s != "auto" && s != "rca" && s != "cska" && s != "cla" && s != "prefix" && s != "brent") {
      bad();
    }
  } else if (key == "multiplier") {
    if (s != "auto" && s != "array" && s != "tree" && s != "csa" && s != "sn") {
      bad();
    }
  } else if (key == "barrel") {
    if (s != "auto" && s != "log" && s != "reverse") {
      bad();
    }
  } else {
    throw std::invalid_argument("unknown synthesis attribute synth." + std::string(key));
  }
}
inline Policy read(std::string_view s) {
  Policy p;
  if (s.empty()) {
    return p;
  }
  rapidjson::Document d;
  d.Parse(s.data(), s.size());
  if (d.HasParseError() || !d.IsObject()) {
    throw std::invalid_argument("malformed synthesis policy");
  }
  for (const auto& m : d.GetObject()) {
    if (!m.value.IsObject() || !m.value.HasMember("value") || !m.value.HasMember("rank")) {
      throw std::invalid_argument("malformed synthesis hint");
    }
    p.emplace(m.name.GetString(), Hint{json(m.value["value"]), m.value["rank"].GetInt()});
  }
  return p;
}
inline std::string write(const Policy& p) {
  std::string s = "{";
  for (const auto& [key, h] : p) {
    if (s.size() > 1) {
      s += ",";
    }
    s += quote(key) + ":{\"value\":" + h.value + ",\"rank\":" + std::to_string(h.rank) + "}";
  }
  return s + "}";
}
inline void overlay(Policy& dst, const Policy& src) {
  for (const auto& [key, h] : src) {
    auto it = dst.find(key);
    if (it == dst.end() || h.rank >= it->second.rank) {
      dst[key] = h;
    }
  }
}
inline std::string get(const Policy& p, std::string_view key, std::string_view fallback = "") {
  auto it = p.find(std::string(key));
  return it == p.end() ? std::string(fallback) : text(it->second.value);
}
inline std::string path(const Policy& p) {
  if (auto it = p.find("_path"); it != p.end()) {
    return it->second.value;
  }
  if (auto it = p.find("color"); it != p.end()) {
    return "[" + it->second.value + "]";
  }
  return {};
}
inline void rescope(Policy& body, const Policy& call) {
  const auto outer = path(call), inner = path(body);
  overlay(body, call);
  if (!outer.empty()) {
    body["_path"] = {inner.empty() ? outer : outer.substr(0, outer.size() - 1) + "," + inner.substr(1), 3};
  }
}
// Hex strings survive LNAST constant folding without quote/interpolation ambiguity.
inline std::string encode(const Policy& p) {
  constexpr char digits[] = "0123456789abcdef";
  std::string    out;
  for (unsigned char c : write(p)) {
    out += digits[c >> 4];
    out += digits[c & 15];
  }
  return out;
}
inline Policy decode(std::string_view s) {
  if (s.size() >= 2 && (s.front() == '\'' || s.front() == '"')) {
    s = s.substr(1, s.size() - 2);
  }
  const auto nibble = [](char c) {
    if (c >= '0' && c <= '9') {
      return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
      return c - 'a' + 10;
    }
    throw std::invalid_argument("malformed synthesis marker");
  };
  if (s.size() % 2) {
    throw std::invalid_argument("malformed synthesis marker");
  }
  std::string out;
  for (size_t i = 0; i < s.size(); i += 2) {
    out += static_cast<char>((nibble(s[i]) << 4) | nibble(s[i + 1]));
  }
  return read(out);
}
}  // namespace livehd::synth_attr

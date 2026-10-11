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

// Every source attribute `::[synth.<key>=<value>]`, with a valid sample value.
// ONE rule ties the two vocabularies together: each key here has a same-named
// `synth.<key>` command option (kSynthSetOptions in lhd/lhd.hpp), so
// `lhd describe synth.<key>` documents the attribute and `--set synth.<key>=`
// sets the run-wide default the attribute overrides. Options with no global
// meaning (color, grow, ware, abc) exist as source-only entries. lhd_options_test
// pins the correspondence; validate() below rejects any key not listed here.
struct Attr_spec {
  std::string_view key;
  std::string_view sample;  // JSON, accepted by validate()
  std::string_view scope;   // which cells the key applies to
};
inline constexpr Attr_spec kAttributes[] = {
    {     "color",             "\"g\"", "any cell: group membership (a name or an integer)"},
    {      "grow",              "false", "any cell: false keeps an explicit group from absorbing neighbouring cones"},
    {     "adder",              "\"cla\"", "Sum, LT, GT, Mult, Div"},
    {"multiplier",              "\"tdm\"", "Mult"},
    {    "barrel",           "\"reverse\"", "SHL, SRA"},
    {"block_size",                "8", "Sum, LT, GT, Mult, Div"},
    {     "delay",              "1000", "a region: its timing budget in ps"},
    {      "ware",               "true", "a region: whether its arithmetic/shift modules are explored"},
    {       "abc",     "\"strash;map\"", "a region: the ABC flow script"},
    {  "stop_mux",              "false", "Mux, Hotmux: cut colors at muxes"},
    {"stop_arith",              "false", "Sum, Mult, Div: cut colors at wide arithmetic"},
    {  "stop_cmp",              "false", "LT, GT: cut colors at wide comparisons"},
    {"stop_shift",              "false", "SHL, SRA: cut colors at runtime shifters"},
    {    "memory",           "\"flops\"", "Memory: auto|flops|macro realization of one memory"},
};
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
  // The registry is the vocabulary: a key outside it has no `synth.<key>` option
  // either, so it is refused here before any value is looked at.
  bool known = false;
  for (const auto& a : kAttributes) {
    known |= a.key == key;
  }
  if (!known) {
    throw std::invalid_argument("unknown synthesis attribute synth." + std::string(key) + " (see `lhd list options 'synth\\..*'`)");
  }
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
  if (key == "grow" || key == "ware" || key == "stop_mux" || key == "stop_arith" || key == "stop_cmp" || key == "stop_shift") {
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
    if (s != "auto" && s != "array" && s != "tree" && s != "csa" && s != "tdm" && s != "sn") {
      bad();
    }
  } else if (key == "barrel") {
    if (s != "auto" && s != "log" && s != "reverse") {
      bad();
    }
  } else if (key == "memory") {
    // auto = the pass.abc.memory heuristic; flops = bit-blast to flops + decode;
    // macro = keep the native memory instance.
    if (s != "auto" && s != "flops" && s != "macro") {
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
// Does the policy key say anything about an operation of this kind? Module and
// block defaults are written once and apply to every cell they cover, but an
// arithmetic pin has no meaning on a mux or an AND. Stamping it anyway makes the
// cell look source-anchored to every later pass (it stops being merged or
// extracted with its neighbours), so an unrelated attribute changed the result of
// a design full of 1-bit mux gates. Membership keys (color, grow, ware, abc,
// delay) apply everywhere; instances only take membership (see below). `Op` is Ntype_op; a template so core stays graph-independent.
template <class Op>
[[nodiscard]] inline bool key_applies(std::string_view key, Op op) {
  if (op == Op::Sub) {
    // An ambient default (module or block) covers the operations defined in its
    // own body, not the callees: only group membership rides on an instance. An
    // explicit call-site attribute is stamped on the instance separately and
    // keeps every key.
    return key == "color" || key == "grow";
  }
  if (key == "adder" || key == "block_size") {
    return op == Op::Sum || op == Op::LT || op == Op::GT || op == Op::Mult || op == Op::Div;
  }
  if (key == "multiplier") {
    return op == Op::Mult;
  }
  // Cut conditions: each governs one operation family in the colorer.
  if (key == "stop_arith") {
    return op == Op::Sum || op == Op::Mult || op == Op::Div;
  }
  if (key == "stop_cmp") {
    return op == Op::LT || op == Op::GT;
  }
  if (key == "stop_shift") {
    return op == Op::SHL || op == Op::SRA;
  }
  if (key == "stop_mux") {
    return op == Op::Mux || op == Op::Hotmux;
  }
  if (key == "barrel") {
    return op == Op::SHL || op == Op::SRA;
  }
  if (key == "memory") {
    return op == Op::Memory;
  }
  return true;
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

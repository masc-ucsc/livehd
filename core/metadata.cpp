// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "metadata.hpp"

#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <stdexcept>

#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"
namespace livehd::metadata {
namespace {
using Writer = rapidjson::Writer<rapidjson::StringBuffer>;
void encode(Writer& w, const rapidjson::Value& v) {
  if (v.IsObject()) {
    std::map<std::string_view, const rapidjson::Value*> fields;
    for (const auto& m : v.GetObject()) {
      if (!fields.emplace(std::string_view(m.name.GetString(), m.name.GetStringLength()), &m.value).second) {
        throw std::invalid_argument("duplicate JSON object key");
      }
    }
    w.StartObject();
    for (const auto& [key, value] : fields) {
      w.Key(key.data(), key.size());
      encode(w, *value);
    }
    w.EndObject();
  } else if (v.IsArray()) {
    w.StartArray();
    for (const auto& item : v.GetArray()) {
      encode(w, item);
    }
    w.EndArray();
  } else {
    if (v.IsDouble() && !std::isfinite(v.GetDouble())) {
      throw std::invalid_argument("nonfinite JSON number");
    }
    v.Accept(w);
  }
}
std::string text(const rapidjson::Value& v, const char* key) {
  const auto m = v.FindMember(key);
  return m != v.MemberEnd() && m->value.IsString() ? std::string(m->value.GetString(), m->value.GetStringLength()) : std::string{};
}
std::string key(const rapidjson::Value& v) {
  // Length-delimited canonical tuple; separator characters in names are safe.
  rapidjson::Document d;
  d.SetArray();
  auto& a = d.GetAllocator();
  for (const auto* field : {"class", "design", "id"}) {
    auto m = v.FindMember(field);
    if (m == v.MemberEnd()) {
      d.PushBack(rapidjson::Value(), a);
    } else {
      d.PushBack(rapidjson::Value(m->value, a), a);
    }
  }
  return canonical(d);
}
}  // namespace
std::string canonical(const rapidjson::Value& value) {
  rapidjson::StringBuffer b;
  Writer                  w(b);
  encode(w, value);
  return {b.GetString(), b.GetSize()};
}
Bundle::Bundle() { entries_.SetArray(); }
Bundle::Bundle(std::string_view json) {
  entries_.Parse(json.data(), json.size());
  if (entries_.HasParseError() || !entries_.IsArray()) {
    throw std::invalid_argument("metadata must be an array of class-tagged records");
  }
  // Validate duplicates recursively before any consumer can observe a record.
  (void)canonical(entries_);
  std::map<std::string, std::string> seen;
  for (const auto& entry : entries_.GetArray()) {
    if (!entry.IsObject() || text(entry, "class").empty()) {
      throw std::invalid_argument("metadata record needs a class string");
    }
    if (!text(entry, "id").empty()) {
      const auto [it, added] = seen.emplace(key(entry), canonical(entry));
      if (!added && it->second != canonical(entry)) {
        throw std::invalid_argument("conflicting metadata record identity: " + key(entry));
      }
    }
  }
}
Bundle Bundle::read(const std::string& path) {
  std::ifstream f(path);
  if (!f) {
    throw std::runtime_error("cannot read metadata: " + path);
  }
  const std::string s{std::istreambuf_iterator<char>(f), {}};
  return Bundle(s);
}
std::vector<const rapidjson::Value*> Bundle::select(std::string_view klass, std::string_view design) const {
  std::vector<const rapidjson::Value*> out;
  std::vector<std::string>             seen;
  for (const auto& e : entries_.GetArray()) {
    if (text(e, "class") != klass) {
      continue;
    }
    if (e.HasMember("design") && !e["design"].IsNull() && text(e, "design") != design) {
      continue;
    }
    const auto encoded = canonical(e);
    if (std::find(seen.begin(), seen.end(), encoded) != seen.end()) {
      continue;
    }
    seen.push_back(encoded);
    out.push_back(&e);
  }
  std::sort(out.begin(), out.end(), [](const auto* a, const auto* b) { return canonical(*a) < canonical(*b); });
  return out;
}
std::string Bundle::identity(std::span<const std::string_view> classes, std::string_view design) const {
  std::vector<std::string> rows;
  for (auto c : classes) {
    for (auto* r : select(c, design)) {
      rows.push_back(canonical(*r));
    }
  }
  std::sort(rows.begin(), rows.end());
  rows.erase(std::unique(rows.begin(), rows.end()), rows.end());
  std::string out = "[";
  for (const auto& row : rows) {
    if (out.size() > 1) {
      out += ",";
    }
    out += row;
  }
  return out + "]";
}
std::string Bundle::json() const { return canonical(entries_); }
void        Bundle::merge(const Bundle& other) {
  rapidjson::Document combined;
  combined.CopyFrom(entries_, combined.GetAllocator());
  for (const auto& entry : other.entries_.GetArray()) {
    combined.PushBack(rapidjson::Value(entry, combined.GetAllocator()), combined.GetAllocator());
  }
  // Validate the whole transaction before replacing the original bundle.
  Bundle checked(canonical(combined));
  entries_.Swap(checked.entries_);
}
void Bundle::erase_selected(std::string_view klass, std::string_view design) {
  for (auto i = entries_.Begin(); i != entries_.End();) {
    if (text(*i, "class") == klass && (!i->HasMember("design") || (*i)["design"].IsNull() || text(*i, "design") == design)) {
      i = entries_.Erase(i);
    } else {
      ++i;
    }
  }
}
void Bundle::replace(const rapidjson::Value& record) {
  if (!record.IsObject() || text(record, "class").empty() || text(record, "id").empty()) {
    throw std::invalid_argument("generated metadata needs class and id");
  }
  const auto identity = key(record);
  auto&      a        = entries_.GetAllocator();
  for (auto i = entries_.Begin(); i != entries_.End();) {
    if (key(*i) == identity) {
      i = entries_.Erase(i);
    } else {
      ++i;
    }
  }
  entries_.PushBack(rapidjson::Value(record, a), a);
}
void Bundle::write(const std::string& path) const {
  namespace fs                        = std::filesystem;
  static std::atomic<unsigned> serial = 0;
  const fs::path               destination(path);
  if (!destination.parent_path().empty()) {
    fs::create_directories(destination.parent_path());
  }
  const auto tmp = path + ".tmp." + std::to_string(getpid()) + "." + std::to_string(serial++);
  try {
    std::ofstream f(tmp);
    f << json() << '\n';
    f.close();
    if (!f) {
      throw std::runtime_error("cannot write metadata: " + path);
    }
    fs::rename(tmp, path);
  } catch (...) {
    std::error_code ec;
    fs::remove(tmp, ec);
    throw;
  }
}
}  // namespace livehd::metadata

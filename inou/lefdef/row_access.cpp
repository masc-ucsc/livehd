// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "row_access.hpp"

#include <fstream>
#include <iterator>
#include <stdexcept>

#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"

namespace livehd::physical {
namespace {
constexpr int kRowAccessSchema = 1;
Row_access    parse_access(std::string_view text, const std::string& where) {
  for (auto access : {Row_access::horizontal, Row_access::vertical}) {
    if (text == row_access_name(access)) {
      return access;
    }
  }
  throw std::invalid_argument(where + ": row access must be \"horizontal\" or \"vertical\"");
}
}  // namespace
std::string_view row_access_name(Row_access access) { return access == Row_access::horizontal ? "horizontal" : "vertical"; }
std::string      Row_access_convention::identity() const {
  rapidjson::StringBuffer                    buffer;
  rapidjson::Writer<rapidjson::StringBuffer> json(buffer);
  const auto                                 text = [&](std::string_view value) { json.String(value.data(), value.size()); };
  json.StartObject();
  json.Key("schema");
  json.Int(kRowAccessSchema);
  json.Key("name");
  text(name);
  json.Key("version");
  text(version);
  json.Key("origin");
  text("first_core_row");
  json.Key("index_base");
  json.Uint(index_base);
  json.Key("even");
  text(row_access_name(even));
  json.Key("odd");
  text(row_access_name(odd));
  json.EndObject();
  return buffer.GetString();
}
Row_access_convention read_row_access(const std::string& path) {
  const auto    where = "row access " + path;
  std::ifstream file(path);
  if (!file) {
    throw std::invalid_argument(where + ": cannot open");
  }
  const std::string   text{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
  rapidjson::Document doc;
  doc.Parse(text.c_str(), text.size());
  if (doc.HasParseError() || !doc.IsObject()) {
    throw std::invalid_argument(where + ": expected a JSON object");
  }
  const auto string = [&](const char* key) -> std::string_view {
    if (!doc.HasMember(key)) {
      throw std::invalid_argument(where + ": missing \"" + key + "\"");
    }
    const auto& value = doc[key];
    if (!value.IsString() || !value.GetStringLength()) {
      throw std::invalid_argument(where + ": \"" + key + "\" must be a nonempty string");
    }
    return {value.GetString(), value.GetStringLength()};
  };
  for (const auto& member : doc.GetObject()) {
    const std::string_view key(member.name.GetString(), member.name.GetStringLength());
    if (key != "schema" && key != "name" && key != "version" && key != "origin" && key != "index_base" && key != "even"
        && key != "odd") {
      throw std::invalid_argument(where + ": unknown key \"" + std::string(key) + "\"");
    }
  }
  if (!doc.HasMember("schema") || !doc["schema"].IsInt() || doc["schema"].GetInt() != kRowAccessSchema) {
    throw std::invalid_argument(where + ": \"schema\" must be 1");
  }
  Row_access_convention convention;
  convention.name    = string("name");
  convention.version = string("version");
  if (doc.HasMember("origin") && string("origin") != "first_core_row") {
    throw std::invalid_argument(where + ": \"origin\" must be \"first_core_row\"");
  }
  if (doc.HasMember("index_base")) {
    if (!doc["index_base"].IsUint()) {
      throw std::invalid_argument(where + ": \"index_base\" must be a nonnegative integer");
    }
    convention.index_base = doc["index_base"].GetUint();
  }
  convention.even = parse_access(string("even"), where);
  convention.odd  = parse_access(string("odd"), where);
  return convention;
}
Row_access_convention resolve_row_access(std::string_view option) {
  return option.empty() || option == "default" ? Row_access_convention{} : read_row_access(std::string(option));
}
size_t placed_row_of_half(size_t start_row, size_t rows, size_t half, Orientation o) {
  if (half >= rows) {
    throw std::invalid_argument("row half outside the master");
  }
  switch (o) {
    case Orientation::N:
    case Orientation::FN: return start_row + half;
    case Orientation::FS:
    case Orientation::S : return start_row + rows - 1 - half;
    default             : throw std::invalid_argument("multi-row masters support only N, FN, FS and S orientations");
  }
}
bool row_access_matches(const Row_access_convention& convention, size_t start_row, Orientation o,
                        std::span<const Row_access> halves) {
  for (size_t half = 0; half < halves.size(); ++half) {
    if (convention.at(placed_row_of_half(start_row, halves.size(), half, o)) != halves[half]) {
      return false;
    }
  }
  return true;
}
}  // namespace livehd::physical

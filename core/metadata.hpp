// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "rapidjson/document.h"

namespace livehd::metadata {
// Deterministic compact encoding. Objects have sorted unique keys; arrays keep
// their semantic order. Integer values never take a floating-point round trip.
std::string canonical(const rapidjson::Value& value);
class Bundle {
  rapidjson::Document entries_;

public:
  Bundle();
  explicit Bundle(std::string_view json);
  static Bundle                        read(const std::string& path);
  std::vector<const rapidjson::Value*> select(std::string_view klass, std::string_view design) const;
  // Only selected classes enter a consumer's identity. Report-only changes do
  // not invalidate its cache. Record ordering and object whitespace are ignored.
  std::string                          identity(std::span<const std::string_view> classes, std::string_view design) const;
  std::string                          json() const;
  void                                 merge(const Bundle& other);
  void                                 erase_selected(std::string_view klass, std::string_view design);
  void                                 replace(const rapidjson::Value& record);
  void                                 write(const std::string& path) const;
};
}  // namespace livehd::metadata

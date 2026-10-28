// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>

#include "node_util.hpp"

namespace livehd::placement {
// DEF readers retain backslash escapes, while graph names can be backtick
// identifiers. Compare semantic names only at the external-floorplan boundary;
// retain the original graph spelling everywhere else (including timing).
class Def_name_binding {
  std::map<std::string, std::string, std::less<>> exact_, semantic_;

public:
  static std::string decode_def(std::string_view text) {
    std::string out;
    for (size_t i = 0; i < text.size(); ++i) {
      if (text[i] == '\\' && i + 1 < text.size()) {
        ++i;
      }
      out += text[i];
    }
    return out;
  }
  void add(std::string_view graph_name) {
    exact_.emplace(graph_name, graph_name);
    const auto name        = graph_util::logical_hier_name(graph_name, true);
    const auto [it, added] = semantic_.emplace(name, graph_name);
    if (!added && it->second != graph_name) {
      it->second.clear();
    }
  }
  std::string resolve(std::string_view def_name) const {
    if (const auto it = exact_.find(def_name); it != exact_.end()) {
      return it->second;
    }
    const auto decoded = decode_def(def_name);
    if (const auto it = semantic_.find(decoded); it != semantic_.end()) {
      if (it->second.empty()) {
        throw std::invalid_argument("ambiguous DEF name binding: " + std::string(def_name));
      }
      return it->second;
    }
    return std::string(def_name);  // caller diagnoses unmatched fixed objects
  }
};
}  // namespace livehd::placement

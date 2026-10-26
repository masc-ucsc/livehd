// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "macro_snap.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <sstream>
#include <stdexcept>

#include "node_util.hpp"

namespace livehd::placement {
std::string original_hierarchy_path(const std::string& name) {
  std::istringstream input(name);
  std::string        path, part;
  while (std::getline(input, part, '.')) {
    // A region wrapper flattened with physical names is `__flat___sub_<nid>`
    // (pass/partition/flatten.cpp); older netlists spell it `sub_<nid>`.
    const bool generated = livehd::graph_util::is_transparent_instance_name(part)
                           || (part.starts_with("sub_") && part.size() > 4
                               && std::all_of(part.begin() + 4, part.end(), [](unsigned char c) { return c >= '0' && c <= '9'; }));
    if (!generated) {
      if (!path.empty()) {
        path += '.';
      }
      path += part;
    }
  }
  return path;
}
std::vector<int> hierarchy_macro_classes(const std::vector<std::string>& names, const std::vector<std::pair<double, double>>& sizes,
                                         const std::vector<std::uint8_t>& macros, int movable) {
  if (names.size() != sizes.size() || names.size() != macros.size() || movable < 0 || static_cast<size_t>(movable) > names.size()) {
    throw std::invalid_argument("invalid macro hierarchy metadata");
  }
  const auto parent = [](const std::string& path) {
    auto end = path.rfind('.');
    return end == std::string::npos ? std::string{} : path.substr(0, end);
  };
  std::vector<std::string>      scopes(names.size());
  std::map<std::string, size_t> counts;
  for (size_t i = 0; i < names.size(); ++i) {
    if (!macros[i]) {
      continue;
    }
    if (names[i].empty() || !std::isfinite(sizes[i].first) || !std::isfinite(sizes[i].second) || sizes[i].first <= 0
        || sizes[i].second <= 0) {
      throw std::invalid_argument("macro hierarchy members need names and finite positive dimensions");
    }
    scopes[i] = parent(original_hierarchy_path(names[i]));
    for (auto scope = scopes[i];; scope = parent(scope)) {
      ++counts[scope];
      if (scope.empty()) {
        break;
      }
    }
  }
  std::vector<int>                                                 result(names.size(), -1);
  std::map<std::pair<std::string, std::pair<double, double>>, int> classes;
  for (int i = 0; i < movable; ++i) {
    if (!macros[i]) {
      continue;
    }
    auto scope = scopes[i];
    while (!scope.empty() && counts.at(scope) == 1) {
      scope = parent(scope);
    }
    const auto [it, inserted] = classes.try_emplace({scope, sizes[i]}, classes.size());
    (void)inserted;
    result[i] = it->second;
  }
  return result;
}
}  // namespace livehd::placement

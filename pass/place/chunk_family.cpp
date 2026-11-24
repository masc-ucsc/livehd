// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "chunk_family.hpp"

#include <algorithm>
#include <map>
#include <numeric>

#include "fp_repetition.hpp"

namespace livehd::placement {
namespace {
using Color = std::pair<std::string, std::string>;
struct Scope {
  std::map<std::string, size_t> children;
  std::map<Color, size_t>       colors;
  size_t                        cells = 0, uses = 0, macros = 0, parent = 0;
  std::string                   path;
};

bool similar(const Scope& a, const Scope& b) {
  if (std::min(a.cells, b.cells) < .70 * std::max(a.cells, b.cells)) {
    return false;
  }
  size_t common = 0;
  for (const auto& [key, n] : a.colors) {
    auto it = b.colors.find(key);
    if (it != b.colors.end()) {
      common += std::min(n, it->second);
    }
  }
  return common >= .45 * (a.uses + b.uses - common);
}
}  // namespace

std::vector<std::string> repeated_chunk_scopes(const std::vector<Chunk_object>& objects, Chunk_family_summary& summary) {
  summary = {};
  std::vector<Scope>  scopes(1);
  std::vector<size_t> owners;
  for (const auto& o : objects) {
    size_t scope = 0;
    for (const auto& part : fp_rep_split(o.instance)) {
      const auto [it, added] = scopes[scope].children.try_emplace(part, scopes.size());
      const size_t next      = it->second;
      if (added) {
        Scope child;
        child.parent = scope;
        child.path   = scopes[scope].path.empty() ? part : scopes[scope].path + "/" + part;
        scopes.push_back(std::move(child));
      }
      scope = next;
    }
    owners.push_back(scope);
    for (;;) {
      scopes[scope].cells  += o.cells;
      scopes[scope].macros += o.macro;
      if (!o.macro && !o.module.empty()) {
        ++scopes[scope].colors[{o.module, o.local}];
        ++scopes[scope].uses;
      }
      if (!scope) {
        break;
      }
      scope = scopes[scope].parent;
    }
  }
  size_t              best_cells = 0;
  std::vector<size_t> best;
  for (const auto& parent : scopes) {
    std::vector<size_t> candidates;
    for (const auto& [name, id] : parent.children) {
      if (scopes[id].uses && scopes[id].cells >= .01 * scopes.front().cells) {
        candidates.push_back(id);
      }
    }
    std::stable_sort(candidates.begin(), candidates.end(), [&](auto a, auto b) { return scopes[a].cells > scopes[b].cells; });
    std::vector<std::vector<size_t>> families;
    for (auto id : candidates) {
      auto it = std::find_if(families.begin(), families.end(), [&](const auto& family) {
        return std::all_of(family.begin(), family.end(), [&](auto peer) { return similar(scopes[id], scopes[peer]); });
      });
      if (it == families.end()) {
        families.push_back({id});
      } else {
        it->push_back(id);
      }
    }
    for (const auto& family : families) {
      const size_t cells
          = std::accumulate(family.begin(), family.end(), size_t{}, [&](size_t n, auto id) { return n + scopes[id].cells; });
      if (family.size() > 1 && cells >= .30 * scopes.front().cells && cells > best_cells) {
        best       = family;
        best_cells = cells;
      }
    }
  }
  if (best.empty()) {
    return {};
  }
  summary.glue_cells = scopes.front().cells - best_cells;
  auto common        = scopes[best.front()].colors;
  for (auto id : best) {
    const auto& scope = scopes[id];
    summary.members.push_back({scope.path, scope.cells, scope.macros, scope.colors.size()});
    for (auto& [key, count] : common) {
      const auto it = scope.colors.find(key);
      count         = it == scope.colors.end() ? 0 : std::min(count, it->second);
    }
  }
  for (const auto& [key, count] : common) {
    summary.shared_color_uses += count;
  }
  std::vector<std::string> group(scopes.size());
  for (auto id : best) {
    group[id] = scopes[id].path;
  }
  for (size_t id = 1; id < scopes.size(); ++id) {
    if (group[id].empty()) {
      group[id] = group[scopes[id].parent];
    }
  }
  std::vector<std::string> result;
  for (auto id : owners) {
    result.push_back(group[id]);
  }
  return result;
}
}  // namespace livehd::placement

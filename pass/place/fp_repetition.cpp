// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "fp_repetition.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <functional>
#include <map>
#include <stdexcept>
#include <tuple>
#include <unordered_map>

#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"

namespace livehd::placement {
namespace {
struct Scope {
  std::string         path;
  size_t              parent = 0;
  int                 module = -1;  // module instance; -1 for a name-only scope
  size_t              flops = 0, memories = 0;
  double              flop_area = 0, memory_area = 0;
  std::vector<size_t> children;
  double              area() const { return flop_area + memory_area; }
};
}  // namespace

void Fp_rep_options::validate() const {
  const auto unit = [](double v) { return std::isfinite(v) && v >= 0 && v <= 1; };
  if (!unit(share) || !unit(min_copy_share) || !unit(tolerance) || min_copies < 2 || fp_min_macros < 0 || fp_min_copies < 2) {
    throw std::invalid_argument(
        "synth.place.fp.auto_* shares/tolerance must be in [0,1], auto_min_copies at least 2, fp_min_macros "
        "nonnegative and fp_min_copies at least 2");
  }
}

bool fp_rep_memory_class(std::string_view lef_class) { return lef_class.starts_with("BLOCK") && lef_class != "BLOCK SOFT"; }

std::vector<std::string> fp_rep_split(std::string_view name) {
  std::vector<std::string> out;
  std::string              part;
  int                      bracket = 0;
  for (char c : name) {
    if (c == '[') {
      ++bracket;
    } else if (c == ']' && bracket > 0) {
      --bracket;
    }
    if (bracket == 0 && (c == '.' || c == '/')) {
      if (!part.empty()) {
        out.push_back(std::move(part));
      }
      part.clear();
      continue;
    }
    if (c == '\\' && part.empty()) {
      continue;  // Verilog escape
    }
    if (c == ' ' || c == '\t') {
      continue;
    }
    part += c;
  }
  if (!part.empty()) {
    out.push_back(std::move(part));
  }
  return out;
}

std::string fp_rep_array_base(std::string_view leaf) {
  // `mem_reg[12][3]$_DFFE_PP_`, `storage_8_3_`: drop the synthesis suffix, then
  // bracket indices and trailing `_<digits>` groups.
  auto end = std::min(leaf.find('['), leaf.find('$'));
  auto out = std::string(leaf.substr(0, end == std::string_view::npos ? leaf.size() : end));
  while (!out.empty()) {
    auto cut = out.size();
    while (cut > 0 && out[cut - 1] == '_') {
      --cut;
    }
    auto digits = cut;
    while (digits > 0 && std::isdigit(static_cast<unsigned char>(out[digits - 1]))) {
      --digits;
    }
    if (digits == cut || digits < 2 || out[digits - 1] != '_') {
      break;
    }
    out.resize(digits - 1);
  }
  return out.empty() ? std::string(leaf) : out;
}

Fp_rep_result detect_fp_repetition(const Fp_rep_design& design, const Fp_rep_options& options) {
  options.validate();
  if (design.top >= design.modules.size()) {
    throw std::invalid_argument("repetition design has no top module");
  }
  std::vector<Scope>                      scopes(1);
  std::unordered_map<std::string, size_t> by_path{
      {"", 0}
  };
  std::map<std::pair<size_t, std::string>, Fp_rep_array> arrays;
  Fp_rep_result                                          result;
  const auto                                             child_scope = [&](size_t parent, const std::string& part) {
    auto path = scopes[parent].path.empty() ? part : scopes[parent].path + "/" + part;
    const auto [it, add] = by_path.try_emplace(path, scopes.size());
    if (add) {
      Scope s;
      s.path   = std::move(path);
      s.parent = parent;
      scopes.push_back(std::move(s));
      scopes[parent].children.push_back(it->second);
    }
    return it->second;
  };
  std::vector<bool>                   active(design.modules.size(), false);
  std::function<void(size_t, size_t)> walk = [&](size_t m, size_t at) {
    if (active[m]) {
      throw std::invalid_argument("recursive module instance: " + design.modules[m].name);
    }
    active[m]          = true;
    const auto& module = design.modules[m];
    for (const auto& leaf : module.leaves) {
      if (!std::isfinite(leaf.area) || leaf.area < 0) {
        throw std::invalid_argument("invalid repetition leaf area: " + leaf.name);
      }
      const auto parts = fp_rep_split(leaf.name);
      auto       scope = at;
      for (size_t i = 0; i + 1 < parts.size(); ++i) {
        scope = child_scope(scope, parts[i]);
      }
      auto& s = scopes[scope];
      if (leaf.memory) {
        ++s.memories;
        s.memory_area += leaf.area;
      } else {
        ++s.flops;
        s.flop_area += leaf.area;
        auto& a      = arrays[{scope, fp_rep_array_base(parts.empty() ? leaf.name : parts.back())}];
        ++a.flops;
        a.area += leaf.area;
      }
      if (m != design.top) {
        result.hierarchy = true;
      }
    }
    for (const auto& child : module.children) {
      if (child.module >= design.modules.size()) {
        throw std::invalid_argument("invalid repetition child module: " + child.name);
      }
      auto       scope = at;
      const auto parts = fp_rep_split(child.name);
      for (const auto& part : parts) {
        scope = child_scope(scope, part);
      }
      if (scope != at && scopes[scope].module < 0) {
        scopes[scope].module = static_cast<int>(child.module);
      }
      walk(child.module, scope);
    }
    active[m] = false;
  };
  walk(design.top, 0);
  for (const auto& s : scopes) {
    result.flops       += s.flops;
    result.memories    += s.memories;
    result.flop_area   += s.flop_area;
    result.memory_area += s.memory_area;
  }
  // Children always follow their parent: accumulate bottom-up.
  for (size_t i = scopes.size(); i-- > 1;) {
    auto& p        = scopes[scopes[i].parent];
    p.flops       += scopes[i].flops;
    p.memories    += scopes[i].memories;
    p.flop_area   += scopes[i].flop_area;
    p.memory_area += scopes[i].memory_area;
  }
  result.total_area = result.flop_area + result.memory_area;
  result.scopes     = scopes.size() - 1;
  if (!(result.total_area > 0)) {
    return result;
  }
  const double total = result.total_area;
  for (const auto& s : scopes) {
    std::vector<size_t> items;
    for (auto c : s.children) {
      if (scopes[c].area() > 0) {
        items.push_back(c);
      }
    }
    if (items.size() < static_cast<size_t>(options.min_copies)) {
      continue;
    }
    std::sort(items.begin(), items.end(), [&](size_t a, size_t b) {
      const auto& x = scopes[a];
      const auto& y = scopes[b];
      return std::tie(x.memories, x.memory_area, x.flops, x.path) < std::tie(y.memories, y.memory_area, y.flops, y.path);
    });
    // Copies match the seed (the smallest unassigned item) on memory count and
    // within `tolerance` on both memory area and flop count, in either
    // direction; the sort orders by memory area, so a flop mismatch inside
    // the memory-area band skips the item instead of ending the group.
    std::vector<bool> used(items.size(), false);
    for (size_t i = 0; i < items.size(); ++i) {
      if (used[i]) {
        continue;
      }
      const auto&         first = scopes[items[i]];
      std::vector<size_t> members{items[i]};
      used[i] = true;
      for (size_t k = i + 1; k < items.size(); ++k) {
        const auto& next = scopes[items[k]];
        if (next.memories != first.memories || next.memory_area > first.memory_area * (1 + options.tolerance) + 1e-9) {
          break;
        }
        const auto lo = static_cast<double>(first.flops) / (1 + options.tolerance) - 1e-9;
        const auto hi = static_cast<double>(first.flops) * (1 + options.tolerance) + 1e-9;
        if (!used[k] && static_cast<double>(next.flops) >= lo && static_cast<double>(next.flops) <= hi) {
          members.push_back(items[k]);
          used[k] = true;
        }
      }
      const size_t copies = members.size();
      Fp_rep_group group;
      group.parent    = s.path;
      group.flops     = first.flops;
      group.memories  = first.memories;
      bool one_module = first.module >= 0;
      for (auto m : members) {
        const auto& copy = scopes[m];
        group.copies.push_back(copy.path);
        group.area += copy.area();
        one_module  = one_module && copy.module == first.module;
      }
      if (one_module) {
        group.module = design.modules[first.module].name;
      }
      group.copy_area = group.area / static_cast<double>(copies);
      group.share     = group.area / total;
      group.fp_candidate
          = group.memories > static_cast<size_t>(options.fp_min_macros) && copies >= static_cast<size_t>(options.fp_min_copies);
      if (copies >= static_cast<size_t>(options.min_copies) && group.copy_area / total >= options.min_copy_share) {
        result.groups.push_back(std::move(group));
      }
    }
  }
  std::sort(result.groups.begin(), result.groups.end(), [](const auto& a, const auto& b) {
    // copies are unique scope paths: the first one breaks ties between families of one parent
    return std::tuple(-a.share, -static_cast<double>(a.copies.size()), a.parent, a.copies.front())
           < std::tuple(-b.share, -static_cast<double>(b.copies.size()), b.parent, b.copies.front());
  });
  for (auto& [key, a] : arrays) {
    a.scope = scopes[key.first].path;
    a.name  = key.second;
    a.share = a.area / total;
    result.arrays.push_back(a);
  }
  std::sort(result.arrays.begin(), result.arrays.end(), [](const auto& a, const auto& b) {
    return std::tie(b.area, a.scope, a.name) < std::tie(a.area, b.scope, b.name);
  });
  result.share       = result.groups.empty() ? 0 : result.groups.front().share;
  result.array_share = result.arrays.empty() ? 0 : result.arrays.front().share;
  for (const auto& g : result.groups) {
    if (g.fp_candidate) {
      result.candidate_share = std::max(result.candidate_share, g.share);
    }
  }
  // With `candidates`, only a floorplan candidate family can turn fp on (even at auto_share 0).
  const bool eligible = options.candidates
                            ? std::any_of(result.groups.begin(), result.groups.end(), [](const auto& g) { return g.fp_candidate; })
                            : !result.groups.empty();
  if (eligible && (options.candidates ? result.candidate_share : result.share) >= options.share) {
    result.enabled = true;
    result.trigger = "repetition";
  }
  return result;
}

std::string fp_rep_json(const Fp_rep_result& r, const Fp_rep_options& o, size_t limit) {
  rapidjson::StringBuffer                    buffer;
  rapidjson::Writer<rapidjson::StringBuffer> json(buffer);
  json.StartObject();
  json.Key("proxy");
  json.String("flop+memory area (um^2)");
  json.Key("auto_share");
  json.Double(o.share);
  json.Key("auto_min_copies");
  json.Int(o.min_copies);
  json.Key("auto_min_copy_share");
  json.Double(o.min_copy_share);
  json.Key("auto_tolerance");
  json.Double(o.tolerance);
  json.Key("auto_candidates");
  json.Bool(o.candidates);
  json.Key("fp_min_macros");
  json.Int(o.fp_min_macros);
  json.Key("fp_min_copies");
  json.Int(o.fp_min_copies);
  json.Key("source");
  json.String(r.hierarchy ? "hierarchy" : "names");
  json.Key("flops");
  json.Uint64(r.flops);
  json.Key("memories");
  json.Uint64(r.memories);
  json.Key("flop_area");
  json.Double(r.flop_area);
  json.Key("memory_area");
  json.Double(r.memory_area);
  json.Key("total_area");
  json.Double(r.total_area);
  json.Key("scopes");
  json.Uint64(r.scopes);
  json.Key("share");
  json.Double(r.share);
  json.Key("candidate_share");
  json.Double(r.candidate_share);
  json.Key("array_share");
  json.Double(r.array_share);
  json.Key("enabled");
  json.Bool(r.enabled);
  json.Key("trigger");
  json.String(r.trigger.c_str());
  json.Key("groups");
  json.StartArray();
  for (size_t i = 0; i < r.groups.size() && i < limit; ++i) {
    const auto& g = r.groups[i];
    json.StartObject();
    json.Key("parent");
    json.String(g.parent.c_str());
    json.Key("module");
    json.String(g.module.c_str());
    json.Key("count");
    json.Uint64(g.copies.size());
    json.Key("flops_per_copy");
    json.Uint64(g.flops);
    json.Key("memories_per_copy");
    json.Uint64(g.memories);
    json.Key("copy_area");
    json.Double(g.copy_area);
    json.Key("share");
    json.Double(g.share);
    json.Key("fp_candidate");
    json.Bool(g.fp_candidate);
    json.Key("copies");
    json.StartArray();
    for (size_t k = 0; k < g.copies.size() && k < 8; ++k) {
      json.String(g.copies[k].c_str());
    }
    json.EndArray();
    json.EndObject();
  }
  json.EndArray();
  json.Key("arrays");
  json.StartArray();
  for (size_t i = 0; i < r.arrays.size() && i < limit; ++i) {
    const auto& a = r.arrays[i];
    json.StartObject();
    json.Key("scope");
    json.String(a.scope.c_str());
    json.Key("name");
    json.String(a.name.c_str());
    json.Key("flops");
    json.Uint64(a.flops);
    json.Key("area");
    json.Double(a.area);
    json.Key("share");
    json.Double(a.share);
    json.EndObject();
  }
  json.EndArray();
  json.EndObject();
  return buffer.GetString();
}
}  // namespace livehd::placement

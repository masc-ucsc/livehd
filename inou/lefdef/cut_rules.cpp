// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "cut_rules.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <sstream>
#include <stdexcept>
namespace livehd::physical {
int64_t exact_units(double um, int64_t dbu) {
  const double v       = um * dbu;
  const auto   rounded = rounded_dbu(v, 1e-7);
  if (!rounded || v < 0) {
    throw std::invalid_argument("via dimension is not representable in integer DBU");
  }
  return *rounded;
}
std::vector<std::string> lef58_tokens(const std::string& text) {
  std::string spaced;
  for (auto c : text) {
    if (c == ';') {
      spaced += " ; ";
    } else {
      spaced += c;
    }
  }
  std::istringstream       input(spaced);
  std::vector<std::string> out;
  for (std::string s; input >> s;) {
    out.push_back(s);
  }
  return out;
}
namespace {
double number(const std::string& s) {
  size_t     n = 0;
  const auto v = std::stod(s, &n);
  if (n != s.size() || !std::isfinite(v) || v < 0) {
    throw std::invalid_argument("invalid cut-rule numeric value");
  }
  return v;
}
}  // namespace
std::map<std::string, std::array<int64_t, 2>> cut_classes(const Cut_layer& cut, int64_t dbu, std::set<std::string>* single_cut) {
  std::map<std::string, std::array<int64_t, 2>> result;
  if (!cut.properties.contains("LEF58_CUTCLASS")) {
    return result;
  }
  for (const auto& text : cut.properties.at("LEF58_CUTCLASS")) {
    const auto words = lef58_tokens(text);
    size_t     i     = 0;
    while (i < words.size()) {
      if (i + 4 >= words.size() || words[i++] != "CUTCLASS") {
        throw std::invalid_argument("unsupported CUTCLASS syntax");
      }
      const auto name = words[i++];
      if (words[i++] != "WIDTH") {
        throw std::invalid_argument("unsupported CUTCLASS width");
      }
      const auto width  = exact_units(number(words[i++]), dbu);
      auto       height = width;
      if (i + 1 < words.size() && words[i] == "LENGTH") {
        ++i;
        height = exact_units(number(words[i++]), dbu);
      }
      double count = 1;
      if (i + 1 < words.size() && words[i] == "CUTS") {
        ++i;
        count = number(words[i++]);
        if (count < 1 || count != std::floor(count)) {
          throw std::invalid_argument("invalid cut class count");
        }
      }
      if (i == words.size() || words[i++] != ";" || width == 0 || height == 0
          || !result.emplace(name, std::array{width, height}).second) {
        throw std::invalid_argument("unsupported or duplicate cut class");
      }
      if (single_cut && count == 1) {
        single_cut->insert(name);
      }
    }
  }
  return result;
}
// The ASAP7 tables use DEFAULT for every class pair. Parse and verify that
// subset, including row sizes, rather than treating all LEF58 tables as scalar.
std::optional<int64_t> cut_table_spacing(const Cut_layer& cut, int64_t dbu,
                                         const std::map<std::string, std::array<int64_t, 2>>& classes) {
  if (!cut.properties.contains("LEF58_SPACINGTABLE")) {
    return {};
  }
  int64_t maximum = 0;
  for (const auto& text : cut.properties.at("LEF58_SPACINGTABLE")) {
    const auto w = lef58_tokens(text);
    if (w.size() < 8 || w[0] != "SPACINGTABLE" || w[1] != "DEFAULT" || w[3] != "CUTCLASS") {
      throw std::invalid_argument("unsupported cut spacing table header");
    }
    const auto               spacing = exact_units(number(w[2]), dbu);
    size_t                   i       = 4;
    std::vector<std::string> names;
    while (i < w.size() && classes.contains(w[i]) && std::find(names.begin(), names.end(), w[i]) == names.end()) {
      names.push_back(w[i++]);
    }
    if (names.empty()) {
      throw std::invalid_argument("empty cut spacing table");
    }
    for (const auto& name : names) {
      if (i == w.size() || w[i++] != name) {
        throw std::invalid_argument("unsupported cut spacing table rows");
      }
      for (size_t col = 0; col < 2 * names.size(); ++col) {
        if (i == w.size() || w[i++] != "-") {
          throw std::invalid_argument("non-default cut spacing table entry is unsupported");
        }
      }
    }
    if (i + 1 != w.size() || w[i] != ";") {
      throw std::invalid_argument("unsupported cut spacing table tail");
    }
    maximum = std::max(maximum, spacing);
  }
  return maximum;
}
std::vector<std::array<int64_t, 2>> cut_enclosures(const Cut_layer& cut, int64_t dbu,
                                                   const std::map<std::string, std::array<int64_t, 2>>& classes,
                                                   const std::array<int64_t, 2>& cut_size, bool above, const Layer& metal,
                                                   const std::array<int64_t, 2>& extent, Cut_enclosure_policy policy) {
  std::vector<std::array<int64_t, 2>> result;
  if (!cut.properties.contains("LEF58_ENCLOSURE")) {
    return {{0, 0}};
  }
  for (const auto& text : cut.properties.at("LEF58_ENCLOSURE")) {
    const auto w = lef58_tokens(text);
    size_t     i = 0;
    while (i < w.size()) {
      const auto begin = i;
      while (i < w.size() && w[i] != ";") {
        ++i;
      }
      if (i == w.size()) {
        throw std::invalid_argument("unterminated enclosure rule");
      }
      const auto end = i++;
      size_t     j   = begin;
      if (end - j < 5 || w[j++] != "ENCLOSURE" || w[j++] != "CUTCLASS") {
        throw std::invalid_argument("unsupported enclosure syntax");
      }
      const auto cls = classes.find(w[j++]);
      if (cls == classes.end()) {
        throw std::invalid_argument("unknown enclosure cut class");
      }
      bool applicable = cls->second == cut_size;
      if (w[j] == "ABOVE" || w[j] == "BELOW") {
        const bool rule_above  = w[j++] == "ABOVE";
        applicable            &= rule_above == above;
      }
      int64_t x = 0, y = 0;
      if (w[j] == "EOL") {
        if (end - j != 4) {
          throw std::invalid_argument("unsupported EOL enclosure");
        }
        // LEF EOL enclosure applies only to an edge shorter than eolWidth.
        // Thresholds need not lie on the manufacturing/DBU grid (ASAP7 uses
        // 24.25 nm); rounding one would change the strict boundary test.
        const auto eol_width = number(w[++j]) * dbu;
        ++j;
        x = exact_units(number(w[j++]), dbu);
        y = exact_units(number(w[j++]), dbu);
        if (policy == Cut_enclosure_policy::lef && std::min(extent[0], extent[1]) >= eol_width) {
          continue;
        }
        if (extent[0] < extent[1]) {
          std::swap(x, y);
        } else if (extent[0] == extent[1]) {
          x = y = std::max(x, y);
        }
      } else if (w[j] == "END") {
        if (end - j != 4 || w[j + 2] != "SIDE") {
          throw std::invalid_argument("unsupported END/SIDE enclosure");
        }
        x = exact_units(number(w[j + 1]), dbu);
        y = exact_units(number(w[j + 3]), dbu);
        j = end;
        if (cut_size[0] < cut_size[1]) {
          std::swap(x, y);
        }
      } else {
        if (end - j != 2) {
          throw std::invalid_argument("unsupported enclosure modifiers");
        }
        x = exact_units(number(w[j++]), dbu);
        y = exact_units(number(w[j++]), dbu);
        if ((metal.direction == "VERTICAL" && x > y) || (metal.direction == "HORIZONTAL" && x < y)) {
          std::swap(x, y);
        }
      }
      if (applicable) {
        result.push_back({x, y});
      }
    }
  }
  if (result.empty()) {
    result.push_back({0, 0});
  }
  return result;
}
int64_t signal_cut_spacing(const Cut_layer& cut, int64_t dbu) {
  const auto supported = [](const std::string& property) {
    return property == "LEF58_CUTCLASS" || property == "LEF58_SPACINGTABLE" || property == "LEF58_ENCLOSURE";
  };
  for (const auto& feature : cut.unsupported) {
    if (!feature.starts_with("PROPERTY ") || !supported(feature.substr(9))) {
      throw std::invalid_argument("unsupported signal cut rule");
    }
  }
  for (const auto& [property, values] : cut.properties) {
    if (!supported(property)) {
      throw std::invalid_argument("unsupported signal cut property");
    }
  }
  int64_t spacing = 0;
  for (auto value : cut.spacing) {
    spacing = std::max(spacing, exact_units(value, dbu));
  }
  const auto classes = cut_classes(cut, dbu);
  if (const auto table = cut_table_spacing(cut, dbu, classes)) {
    spacing = std::max(spacing, *table);
  }
  return spacing;
}
}  // namespace livehd::physical

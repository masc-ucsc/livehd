// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "lnast.hpp"

namespace upass {
// Exact registered bodies with IO include already-specialized calls. An empty-IO file
// unit can share a name with a callable nested in the caller's lexical scope.
template <class Find, class UniqueSuffix>
std::shared_ptr<Lnast> resolve_lexical_callee(std::string_view name, std::string_view caller, Find find,
                                              UniqueSuffix unique_suffix) {
  const auto resolve = [&](std::string_view spelling) {
    auto exact = find(spelling);
    if (exact && !exact->io_meta().empty()) {
      return exact;
    }
    const std::string suffix = "." + std::string(spelling);
    for (auto unit = caller; !unit.empty();) {
      if (auto local = find(std::string(unit) + suffix); local && (!exact || !local->io_meta().empty())) {
        return local;
      }
      const auto dot = unit.rfind('.');
      if (dot == std::string_view::npos) {
        break;
      }
      unit = unit.substr(0, dot);
    }
    auto match = unique_suffix(suffix);
    return match && (!exact || !match->io_meta().empty()) ? match : exact;
  };
  auto match = resolve(name);
  if (!match && name.size() >= 2 && name.front() == '`' && name.back() == '`') {
    // Verilog module keys store the literal identifier. Pyrope can register a
    // scoped, quoted reserved name (e.g. `U8`); exhaust that spelling first.
    match = resolve(name.substr(1, name.size() - 2));
  }
  return match;
}
}  // namespace upass

//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

// The registry of cost profiles (pass/aegraph/cost/<name>.cost). rulegen embeds each file's text in a generated
// source that registers it here; the profile is parsed on first use (the parser is rulegen/cost_model.cpp, the
// same one rulegen lints with), so the build that generated the text has already proven it parses.

#include <string>
#include <string_view>
#include <vector>

#include "rulegen/cost_model.hpp"

namespace livehd::aegraph::rt {

class Cost_registrar {
public:
  Cost_registrar(const char* name, const char* path, const char* text);
};

// The parsed profile `name`, or null when none is registered. Thread safe.
[[nodiscard]] const cost::Profile* find_cost_profile(std::string_view name);

// The registered names, sorted.
[[nodiscard]] std::vector<std::string> cost_profile_names();

}  // namespace livehd::aegraph::rt

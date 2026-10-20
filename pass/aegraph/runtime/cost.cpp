//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "runtime/cost.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <mutex>

namespace livehd::aegraph::rt {

namespace {

struct Slot {
  std::string                    name;
  std::string                    path;
  std::string                    text;
  std::once_flag                 once;
  std::unique_ptr<cost::Profile> profile;
};

std::vector<std::unique_ptr<Slot>>& slots() {
  static std::vector<std::unique_ptr<Slot>> all;  // function-local: the registrars run during static init
  return all;
}

}  // namespace

Cost_registrar::Cost_registrar(const char* name, const char* path, const char* text) {
  auto slot  = std::make_unique<Slot>();
  slot->name = name;
  slot->path = path;
  slot->text = text;
  slots().push_back(std::move(slot));
}

const cost::Profile* find_cost_profile(std::string_view name) {
  for (const auto& slot : slots()) {
    if (slot->name != name) {
      continue;
    }
    std::call_once(slot->once, [&] {
      cost::Diags diags;
      auto        profile = cost::parse_profile(slot->text, slot->path, diags);
      cost::lint_profile(profile, diags);
      if (!diags.empty()) {
        // rulegen linted this text at build time: reaching here is a build-system bug.
        for (const auto& d : diags) {
          std::fprintf(stderr, "aegraph cost profile: %s\n", d.c_str());
        }
        std::abort();
      }
      slot->profile = std::make_unique<cost::Profile>(std::move(profile));
    });
    return slot->profile.get();
  }
  return nullptr;
}

std::vector<std::string> cost_profile_names() {
  std::vector<std::string> names;
  for (const auto& slot : slots()) {
    names.push_back(slot->name);
  }
  std::ranges::sort(names);
  return names;
}

}  // namespace livehd::aegraph::rt

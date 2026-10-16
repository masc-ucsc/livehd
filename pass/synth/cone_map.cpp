// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "cone_map.hpp"

#include <map>
#include <mutex>
namespace livehd::synth {
namespace {
auto& providers() {
  static std::map<std::string, Cone_provider> p;
  return p;
}
auto& lock() {
  static std::mutex m;
  return m;
}
}  // namespace
bool register_cone_provider(std::string name, Cone_provider provider) {
  std::lock_guard guard(lock());
  return providers().emplace(std::move(name), std::move(provider)).second;
}
bool has_cone_provider(std::string_view name) {
  std::lock_guard guard(lock());
  return providers().contains(std::string(name));
}
std::optional<Cone_mapping> map_cone(std::string_view name, const Lnet& logic, const Cone_boundary& boundary,
                                     const std::string& liberty) {
  Cone_provider provider;
  {
    std::lock_guard guard(lock());
    const auto      it = providers().find(std::string(name));
    if (it == providers().end()) {
      return {};
    }
    provider = it->second;
  }
  return provider(logic, boundary, liberty);
}
}  // namespace livehd::synth

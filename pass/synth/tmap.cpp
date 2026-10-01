// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "tmap.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <mutex>
#include <tuple>

namespace livehd::synth {
namespace {
struct Providers {
  std::mutex                                        mutex;
  std::map<std::string, Tmap_provider, std::less<>> entries;
};
Providers& providers() {
  static Providers result;
  return result;
}
bool same_ports(const std::vector<hhds::GraphIO::DeclaredIoPin>& left, const std::vector<hhds::GraphIO::DeclaredIoPin>& right) {
  const auto keys = [](const auto& ports) {
    std::vector<std::tuple<std::string, hhds::Port_id, uint32_t, bool, bool>> result;
    result.reserve(ports.size());
    for (const auto& port : ports) {
      result.emplace_back(port.name, port.port_id, port.bits, port.unsign, port.loop_break);
    }
    std::sort(result.begin(), result.end());
    return result;
  };
  return left.size() == right.size() && keys(left) == keys(right);
}
}  // namespace

bool register_tmap_provider(std::string name, Tmap_provider provider) {
  if (name.empty() || !provider) {
    return false;
  }
  auto&           registry = providers();
  std::lock_guard lock{registry.mutex};
  return registry.entries.emplace(std::move(name), std::move(provider)).second;
}

bool has_tmap_provider(std::string_view name) {
  auto&           registry = providers();
  std::lock_guard lock{registry.mutex};
  return registry.entries.contains(name);
}

Tmap_result technology_map(std::string_view name, const std::shared_ptr<hhds::Graph>& top, const Tmap_options& options) {
  Tmap_provider provider;
  {
    auto&           registry = providers();
    std::lock_guard lock{registry.mutex};
    const auto      found = registry.entries.find(name);
    if (found == registry.entries.end()) {
      return {Tmap_status::unavailable, {}, "technology-mapping provider is not available: " + std::string{name}};
    }
    provider = found->second;
  }
  if (!top || !top->get_io() || !top->get_io()->get_library() || options.library.empty() || !std::isfinite(options.delay_ps)
      || options.delay_ps < 0 || options.delay_ps > std::numeric_limits<int>::max() || options.memory_budget_mb <= 0) {
    return {Tmap_status::invalid, {}, "invalid technology-mapping source, library or limits"};
  }
  auto result = provider(top, options);
  if (result.status == Tmap_status::mapped) {
    if (!result.design || !result.design->top || !result.design->top->get_io()
        || result.design->top->get_io()->get_library() != &result.design->library) {
      return {Tmap_status::invalid, {}, "technology-mapping provider returned no owned top definition"};
    }
    const auto source = top->get_io(), mapped = result.design->top->get_io();
    if (source->get_name() != mapped->get_name() || !same_ports(source->get_input_pin_decls(), mapped->get_input_pin_decls())
        || !same_ports(source->get_output_pin_decls(), mapped->get_output_pin_decls())) {
      return {Tmap_status::invalid, {}, "technology-mapping provider changed the top port interface"};
    }
  } else {
    result.design.reset();
  }
  return result;
}

}  // namespace livehd::synth

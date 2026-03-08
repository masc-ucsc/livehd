//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cstddef>
#include <format>
#include <functional>
#include <print>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace upass {

// resolve_order_impl — DFS topological sort shared by both LNAST and LGraph
// runners.  Works with any plugin registry whose value type exposes a
// `depends_on` field (std::vector<std::string>).
//
// Intentionally does NOT call upass::error() (which throws) because dependency
// resolution happens at construction time; errors are reported gracefully
// through the *error_msg out-parameter so the caller can set
// configuration_error = true and defer the report to run().
template <typename Registry>
inline std::vector<std::string> resolve_order_impl(const Registry            &registry,
                                                   const std::vector<std::string> &requested_names,
                                                   std::string_view               tag,
                                                   std::string                   *error_msg) {
  enum class Mark { kUnseen, kVisiting, kDone };
  std::unordered_map<std::string, Mark> marks;
  std::vector<std::string>              ordered;

  std::function<bool(const std::string &)> dfs = [&](const std::string &name) -> bool {
    const auto it = registry.find(name);
    if (it == registry.end()) {
      std::print("{} is not defined.\n", name);
      if (error_msg && error_msg->empty()) {
        *error_msg = std::format("unknown pass '{}'", name);
      }
      return false;
    }

    const auto mit = marks.find(name);
    if (mit != marks.end()) {
      if (mit->second == Mark::kVisiting) {
        std::print(stderr, "{} dependency cycle detected at {}\n", tag, name);
        if (error_msg && error_msg->empty()) {
          *error_msg = std::format("dependency cycle detected at '{}'", name);
        }
        return false;
      }
      return mit->second == Mark::kDone;
    }

    marks.emplace(name, Mark::kVisiting);
    for (const auto &dep : it->second.depends_on) {
      if (!dfs(dep)) {
        std::print(stderr, "{} dependency chain for {} is invalid\n", tag, name);
        if (error_msg && error_msg->empty()) {
          *error_msg = std::format("dependency chain for '{}' is invalid", name);
        }
        marks[name] = Mark::kDone;
        return false;
      }
    }
    marks[name] = Mark::kDone;
    ordered.emplace_back(name);
    return true;
  };

  for (const auto &name : requested_names) {
    dfs(name);
  }

  return ordered;
}

class Runner_fixed_point {
public:
  static std::size_t normalize_max_iters(std::string_view tag, std::size_t max_iters) {
    if (max_iters == 0) {
      std::print("{} - max_iters=0 is invalid, forcing 1\n", tag);
      return 1;
    }
    return max_iters;
  }

  template <typename BeginIterFn, typename ProcessFn, typename ChangedNamesFn>
  static void run(std::string_view tag, std::size_t max_iters, BeginIterFn begin_iter, ProcessFn process_once,
                  ChangedNamesFn changed_names) {
    max_iters = normalize_max_iters(tag, max_iters);

    for (std::size_t iter = 1; iter <= max_iters; ++iter) {
      begin_iter();
      process_once();

      std::vector<std::string> changed = changed_names();
      if (!changed.empty()) {
        std::print("{} - iteration {} changed:", tag, iter);
        for (const auto &name : changed) {
          std::print(" {}", name);
        }
        std::print("\n");
      } else {
        std::print("{} - converged at iteration {}\n", tag, iter);
        return;
      }
    }

    std::print("{} - reached max iterations ({})\n", tag, max_iters);
  }
};

}  // namespace upass

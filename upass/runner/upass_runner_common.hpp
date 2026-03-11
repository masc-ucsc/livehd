//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cstddef>
#include <print>
#include <string>
#include <string_view>
#include <vector>

namespace upass {

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
        for (const auto& name : changed) {
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

//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <format>
#include <iostream>
#include <print>
#include <stdexcept>

namespace upass {

// Prints a formatted diagnostic to stderr and throws std::runtime_error so
// that callers (e.g. the verifier) surface failures rather than silently
// continuing after detecting a malformed IR node.
template <typename... Args>
[[noreturn]] void error(std::format_string<Args...> fmt, Args &&...args) {
  auto msg = std::format(fmt, std::forward<Args>(args)...);
  std::print(stderr, "upass error: {}", msg);
  throw std::runtime_error(msg);
}

}  // namespace upass

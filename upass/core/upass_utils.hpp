//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <format>
#include <print>
#include <stdexcept>

namespace upass {

template <typename... Args>
[[noreturn]] void error(std::format_string<Args...> format, Args&&... args) {
  std::print(stderr, format, std::forward<Args>(args)...);
  throw std::runtime_error(std::format(format, std::forward<Args>(args)...));
}

}  // namespace upass

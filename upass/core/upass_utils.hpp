//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <iostream>
#include <format>

namespace upass {

template <typename... Args>
void error(std::format_string<Args...> format, Args &&...args) {
  std::cout << std::format(format, std::forward<Args>(args)...);
}

}  // namespace upass

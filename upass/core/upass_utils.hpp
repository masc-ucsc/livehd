//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "fmt/format.h"

namespace upass {

template <typename... Args>
void error(fmt::format_string<Args...> format, Args &&...args) {
  fmt::print(format, std::forward<Args>(args)...);
}

}  // namespace upass

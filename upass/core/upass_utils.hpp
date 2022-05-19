//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "fmt/format.h"

namespace upass {

template <typename... Args>
void error(const char *format, const Args &...args) {
  fmt::print(format, args...);
}

}

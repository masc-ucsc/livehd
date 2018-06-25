//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#ifndef LGLOG_H
#define LGLOG_H

#include "spdlog/spdlog.h"

#include <iostream>

// FIXME: is it possible to remove the dependency on ostr.h?
//#include "spdlog/fmt/ostr.h"
#ifndef console
#define console spdlog::get("console")
#endif

class Console_init {
  static int _static_initializer;
  static int initialize_logger();
};

#endif

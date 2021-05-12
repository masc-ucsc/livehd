 //  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <iostream>
#include <filesystem>
#include <fmt/format.h>

class err_tracker {
public:

  static void logger(const std::string &text);
  static void sot_logger(const std::string &text);

  template<typename S, typename... Args >
  static void err_logger(const S &format, Args &&...args) {
    logger(fmt::format(format, args...));
  }


  template<typename S, typename... Args >
  static void sot_log(const S &format, Args &&...args) {
    sot_logger(fmt::format(format, args...));
  }
};

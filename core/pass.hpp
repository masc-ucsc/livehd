//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <string>

#include "fmt/format.h"

#include "eprp.hpp"

class Pass {
private:
  const std::string name;

protected:
  void register_pass(Eprp_method &method);
  void register_inou(Eprp_method &method);

  Pass(const std::string &name_);

  bool setup_directory(const std::string &dir) const;
public:
  static Eprp eprp;

  virtual void setup() = 0;

  static void error(const std::string &msg) {
    eprp.parser_error(msg);
  }
  static void warn(const std::string &msg) {
    eprp.parser_warn(msg);
  }
  static void info(const std::string &msg) {
#ifndef NDEBUG
    eprp.parser_info(msg);
#endif
  }

  static void error(const char *msg) {
    eprp.parser_error(std::string(msg));
  }
  static void warn(const char *msg) {
    eprp.parser_warn(std::string(msg));
  }
  static void info(const char *msg) {
#ifndef NDEBUG
    eprp.parser_info(std::string(msg));
#endif
  }

  template <typename... Args>
    static void error(const char *format, const Args & ... args) {
      eprp.parser_error(fmt::vformat(format, fmt::make_format_args(args...)));
    }

  template <typename... Args>
    static void warn(const char *format, const Args & ... args) {
      eprp.parser_warn(fmt::vformat(format, fmt::make_format_args(args...)));
    }

  template <typename... Args>
    static void info(const char *format, const Args & ... args) {
      eprp.parser_info(fmt::vformat(format, fmt::make_format_args(args...)));
    }

};


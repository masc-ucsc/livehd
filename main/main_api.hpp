//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#pragma once

#include <functional>

#include "pass.hpp"

class Main_api {
protected:
public:
  static void error(const std::string &msg) { Pass::eprp.parser_error(msg); }
  static void warn(const std::string &msg) { Pass::eprp.parser_warn(msg); }

  template <typename... Args>
  static void error(std::string_view format, const Args &...args) {
    Pass::eprp.parser_error(format, args...);
  }
  template <typename... Args>
  static void warn(std::string_view format, const Args &...args) {
    Pass::eprp.parser_warn(format, args...);
  }

  static void setup(std::function<void(Eprp &)> fn) { fn(Pass::eprp); }

  static void parse_inline(std::string_view line) { Pass::eprp.parse_inline(line); }

  static void get_commands(std::function<void(const std::string &, const std::string &)> fn) { Pass::eprp.get_commands(fn); };

  static const std::string &get_command_help(const std::string &cmd) { return Pass::eprp.get_command_help(cmd); }

  static void get_labels(const std::string &cmd, std::function<void(const std::string &, const std::string &, bool required)> fn) {
    Pass::eprp.get_labels(cmd, fn);
  }

  static bool has_errors() { return Pass::eprp.has_errors(); }

  static void init();
};

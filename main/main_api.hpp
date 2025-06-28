//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#pragma once

#include <format>
#include <functional>

#include "pass.hpp"

class Main_api {
protected:
public:
  static void error(const std::string &msg) { throw Eprp::parser_error(Pass::eprp, msg); }
  static void warn(const std::string &msg) { Pass::eprp.parser_warn(msg); }

  template <typename... Args>
  static void error(std::format_string<Args...> fmt, Args &&...args) {
    throw Eprp::parser_error(Pass::eprp, fmt, std::forward<Args>(args)...);
  }
  template <typename... Args>
  static void warn(std::format_string<Args...> fmt, Args &&...args) {
    Pass::eprp.parser_warn(fmt, std::forward<Args>(args)...);
  }

  static void setup(const std::function<void(Eprp &)> &fn) { fn(Pass::eprp); }

  static void parse_inline(std::string_view line);

  static void get_commands(std::function<void(std::string_view, std::string_view)> fn) { Pass::eprp.get_commands(fn); };

  static std::string_view get_command_help(std::string_view cmd) { return Pass::eprp.get_command_help(cmd); }

  static void get_labels(std::string_view cmd, std::function<void(std::string_view, std::string_view, bool required)> fn) {
    Pass::eprp.get_labels(cmd, fn);
  }

  static bool has_errors() { return Pass::eprp.has_errors(); }

  static void init();
};

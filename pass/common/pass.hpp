//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <functional>
#include <string>
#include <map>

#include "absl/strings/substitute.h"
#include "eprp.hpp"
#include "fmt/format.h"
#include "iassert.hpp"


class Pass {
protected:
  const std::string pass_name;

  // Common names used by many passes
  const std::string files;
  const std::string path;
  const std::string odir;

  const std::string get_files(const Eprp_var &var) const;
  const std::string get_path(const Eprp_var &var) const;
  const std::string get_odir(const Eprp_var &var) const;

  static void register_pass(Eprp_method &method);
  static void register_inou(std::string_view pname, Eprp_method &method);

  bool setup_directory(std::string_view dir) const;

  Pass(std::string_view _pass_name, const Eprp_var &var);

public:
  static Eprp eprp;

  static void error(std::string_view msg) { eprp.parser_error(msg); }
  static void warn(std::string_view msg) { eprp.parser_warn(msg); }
  static void info(std::string_view msg) {
#ifndef NDEBUG
    eprp.parser_info(msg);
#endif
  }

  template <typename... Args>
  static void error(const char *format, const Args &... args) {
    fmt::format_args   fargs = fmt::make_format_args(args...);
    fmt::memory_buffer tmp;
    fmt::vformat_to(tmp, format, fargs);
    eprp.parser_error(std::string_view(tmp.data(), tmp.size()));
  }

  template <typename... Args>
  static void warn(std::string_view format, const Args &... args) {
    fmt::format_args   fargs = fmt::make_format_args(args...);
    fmt::memory_buffer tmp;
    fmt::vformat_to(tmp, format, fargs);
    eprp.parser_warn(std::string_view(tmp.data(), tmp.size()));
  }

  template <typename... Args>
  static void info(std::string_view format, const Args &... args) {
    fmt::format_args   fargs = fmt::make_format_args(args...);
    fmt::memory_buffer tmp;
    fmt::vformat_to(tmp, format, fargs);
    eprp.parser_info(std::string_view(tmp.data(), tmp.size()));
  }
};

class Pass_plugin {
public:
  using Setup_fn = std::function<void()>;
  using Map_setup = std::map<std::string, Setup_fn>;
protected:

  static Map_setup registry;

public:
  Pass_plugin(const std::string &name, Setup_fn setup_fn) {
    if (registry.find(name) != registry.end()) {
      Pass::error("Pass_plugin: {} is already registered", name);
      return;
    }
    registry[name] = setup_fn;
  }

  static const Map_setup &get_registry() { return registry; }
};

//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <format>
#include <functional>
#include <map>
#include <string>

#include "absl/strings/str_split.h"
#include "diag.hpp"
#include "eprp.hpp"
#include "err_tracker.hpp"
#include "iassert.hpp"

static_assert(__cplusplus >= 201703L, "C++ 17 support is required. Please upgrade your compiler.");

class Pass {
protected:
  std::string pass_name;

  // Common names used by many passes
  const std::string files;
  const std::string path;
  const std::string odir;

  [[nodiscard]] std::string get_files(const Eprp_var& var) const;
  [[nodiscard]] std::string get_path(const Eprp_var& var) const;
  [[nodiscard]] std::string get_odir(const Eprp_var& var) const;

  static void register_pass(Eprp_method& method);
  static void register_inou(std::string_view pname, Eprp_method& method);

  [[nodiscard]] bool setup_directory(std::string_view dir) const;

  Pass(std::string_view _pass_name, const Eprp_var& var);

public:
  static inline Eprp eprp;

  // Errors/warnings go through livehd::diag (the Builder or a pass-local
  // located helper) — the old Pass::error/Pass::warn shims are deleted so
  // generic, unlocated, code-less records cannot creep back in. `info`
  // remains: it is debug-build progress logging, not a diagnostic, and stays
  // out of the machine-readable stream.
  static void info(std::string_view msg) {
#ifndef NDEBUG
    eprp.parser_info(msg);
#else
    (void)msg;
#endif
  }

  template <typename... Args>
  static void info(std::format_string<Args...> format, Args&&... args) {
#ifndef NDEBUG
    auto tmp = std::format(format, std::forward<Args>(args)...);
    err_tracker::logger(tmp);
    eprp.parser_info(tmp);
#else
    (void)format;
    ((void)args, ...);
#endif
  }
};

class Pass_plugin {
public:
  using Setup_fn  = std::function<void()>;
  using Map_setup = std::map<std::string, Setup_fn>;

protected:
  static inline Map_setup registry;

public:
  Pass_plugin(const std::string& name, const Setup_fn& setup_fn) {
    if (registry.find(name) != registry.end()) {
      livehd::diag::err("pass", "plugin-duplicate", "internal").msg("Pass_plugin: {} is already registered", name).fatal();
    }
    registry[name] = setup_fn;
  }

  static const Map_setup& get_registry() { return registry; }
};

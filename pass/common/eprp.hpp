//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cassert>
#include <format>
#include <map>
#include <stdexcept>
#include <string_view>

#include "eprp_method.hpp"
#include "eprp_var.hpp"

// Method registry + the compile-diagnostic exception types. The interactive
// shell that used to parse `cmd label:path |> cmd2 …` text is gone; the lhd
// CLI drives the registered methods programmatically via run_method_now.
class Eprp {
protected:
  std::map<std::string, Eprp_method, eprp_casecmp_str> methods;

  void parser_error_int(std::string_view text) const;
  void parser_warn_int(std::string_view text) const;
  void parser_info_int(std::string_view text) const;

public:
  Eprp();

  // Compile-error exception: the constructor flushes the staged diag record
  // (or a generic one) through the diag sink, then the exception propagates
  // to the lhd kernel, which reports + exits non-zero.
  class parser_error : public std::runtime_error {
  public:
    template <typename... Args>
    parser_error(const Eprp& eprp, std::format_string<Args...> format, Args&&... args)
        : std::runtime_error(std::format(format, args...)) {
      eprp.parser_error_int(what());
    };
    parser_error(const Eprp& eprp, std::string_view txt) : std::runtime_error(std::string(txt)) {
      eprp.parser_error_int(what());
    };
  };

  template <typename... Args>
  void parser_warn(std::format_string<Args...> format, Args&&... args) const {
    parser_warn_int(std::format(format, args...));
  }
  void parser_warn(std::string_view txt) const { parser_warn_int(txt); }

  template <typename... Args>
  void parser_info(std::format_string<Args...> format, Args&&... args) const {
    parser_info_int(std::format(format, args...));
  }
  // NOTE: historical behavior — the string_view info overload routes through
  // the WARNING path (diag sink severity warning), and callers depend on it.
  void parser_info(std::string_view txt) const { parser_warn_int(txt); }

  void register_method(const Eprp_method& method) {
    assert(methods.find(std::string(method.get_name())) == methods.end());
    methods.insert({std::string(method.get_name()), method});
  }

  // Registered method by name (nullptr when absent). Read-only access to a
  // method's label registry (name/help/default/required) — the lhd CLI uses
  // it to validate --set/--config flags and to render `lhd list options`.
  const Eprp_method* get_method(std::string_view cmd) const {
    const auto it = methods.find(std::string(cmd));
    return it == methods.end() ? nullptr : &it->second;
  }

  // Run one registered method synchronously on `var` (no pipe, no text
  // parsing): set the per-stage labels, merge them into the var, fill in
  // label defaults, check required labels, call the method. Used by the lhd
  // CLI to drive passes programmatically.
  void run_method_now(std::string_view cmd, Eprp_var& var, const Eprp_var::Eprp_dict& step_labels);
};

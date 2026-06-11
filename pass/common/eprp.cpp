//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "eprp.hpp"

#include <print>

#include "diag.hpp"
#include "err_tracker.hpp"

void Eprp::parser_error_int(std::string_view text) const {
  err_tracker::logger(text);
  // Human + JSONL output go through the diag sink (livehd: error: …). A rich
  // diagnostic staged at the call site is emitted here; otherwise a generic
  // one. The exception (Eprp::parser_error) propagates to the EPRP command
  // handler (the lhd kernel), which reports + exits non-zero.
  livehd::diag::sink().flush(livehd::diag::Severity::error, text);
}

void Eprp::parser_warn_int(std::string_view text) const {
  err_tracker::logger(text);
  livehd::diag::sink().flush(livehd::diag::Severity::warning, text);
}

void Eprp::parser_info_int(std::string_view text) const {
  err_tracker::logger(text);
  std::print("info: {}\n", text);
}

void Eprp::run_method_now(std::string_view cmd, Eprp_var& var, const Eprp_var::Eprp_dict& step_labels) {
  const auto& it = methods.find(std::string(cmd));
  if (it == methods.end()) {
    throw parser_error(*this, "method {} not registered", cmd);
  }
  const auto& m = it->second;

  var.set_stage_labels(step_labels);
  for (const auto& label : step_labels) {
    var.add(label.first, label.second);
  }

  for (const auto& label : m.labels) {
    if (!label.second.default_value.empty() && !var.has_label(label.first)) {
      var.add(label.first, label.second.default_value);
    }
  }

  auto [err, err_msg] = m.check_labels(var);
  if (err) {
    throw parser_error(*this, "{}", err_msg);
  }

  m.method(var);
}

Eprp::Eprp() {}

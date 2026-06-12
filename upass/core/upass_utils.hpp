//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <format>
#include <print>
#include <stdexcept>

#include "diag.hpp"

namespace upass {

template <typename... Args>
[[noreturn]] void error(std::format_string<Args...> format, Args&&... args) {
  auto msg = std::format(format, std::forward<Args>(args)...);
  // Emit directly (this path throws std::runtime_error, not Eprp::parser_error,
  // so it does not pass through parser_error_int's flush). The sink prints the
  // `livehd: error:` line when the human channel is on.
  livehd::diag::sink().emit(livehd::diag::Diagnostic{.severity = livehd::diag::Severity::error,
                                                     .code     = "upass-error",
                                                     .category = "internal",
                                                     .pass     = "upass",
                                                     .message  = msg});
  throw std::runtime_error(msg);
}

// Located variant: the caller resolves the node's SourceId through
// its Lnast's locator (`ln->span_of(nid)`) and passes the resolved Span here —
// core/diag stays a leaf library with no locator dependency.
template <typename... Args>
[[noreturn]] void error(livehd::diag::Span span, std::format_string<Args...> format, Args&&... args) {
  auto msg = std::format(format, std::forward<Args>(args)...);
  livehd::diag::sink().emit(livehd::diag::Diagnostic{.severity = livehd::diag::Severity::error,
                                                     .code     = "upass-error",
                                                     .category = "internal",
                                                     .pass     = "upass",
                                                     .message  = msg,
                                                     .span     = std::move(span)});
  throw std::runtime_error(msg);
}

}  // namespace upass

//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

// slang DiagnosticClient that routes slang's lexer/parser/elaboration
// diagnostics into LiveHD's diag::Sink (todo/ 1s subtask C) instead of the
// TextDiagnosticClient-to-stderr path. Each slang ReportedDiagnostic becomes
// one livehd::diag::Diagnostic: severity mapped, code = slang's stable
// diagnostic name, pass = "inou.slang", span minted through the shared
// slang_loc helper (same conversion the LNAST importer uses, subtask D).

#include <string>
#include <utility>

#include "diag.hpp"
#include "slang/diagnostics/DiagnosticClient.h"
#include "slang/diagnostics/DiagnosticEngine.h"
#include "slang/diagnostics/Diagnostics.h"
#include "slang_location.hpp"

namespace livehd::slang_diag {

class Sink_client : public slang::DiagnosticClient {
public:
  void report(const slang::ReportedDiagnostic& diag) override {
    using slang::DiagnosticSeverity;

    livehd::diag::Severity sev;
    switch (diag.severity) {
      case DiagnosticSeverity::Note: sev = livehd::diag::Severity::note; break;
      case DiagnosticSeverity::Warning: sev = livehd::diag::Severity::warning; break;
      case DiagnosticSeverity::Error:
      case DiagnosticSeverity::Fatal: sev = livehd::diag::Severity::error; break;
      case DiagnosticSeverity::Ignored:
      default: return;  // the engine already decided to drop it
    }

    livehd::diag::Diagnostic d;
    d.severity = sev;
    d.code     = std::string(slang::toString(diag.originalDiagnostic.code));
    d.category = "syntax";  // slang front-end (lex/parse/elaborate); refined in 2s
    d.pass     = "inou.slang";
    d.message  = std::string(diag.formattedMessage);
    if (sourceManager != nullptr) {
      const slang::SourceRange range
          = diag.ranges.empty() ? slang::SourceRange(diag.location, diag.location) : diag.ranges.front();
      d.span = livehd::slang_loc::span_of(*sourceManager, range);
    }
    livehd::diag::sink().emit(std::move(d));
  }
};

}  // namespace livehd::slang_diag

//------------------------------------------------------------------------------
// slang_driver.cpp
//
// Minimal embedded slang driver for the direct --reader slang front-end
// (todo/ 1s subtask B). Ported to slang v11: drives only the options
// inou_slang.cpp feeds (-I/-D/-U, --quiet, --ignore-unknown-modules) and routes
// every slang diagnostic into LiveHD's diag::Sink (subtask C) instead of the
// stderr TextDiagnosticClient. The -E/--macros-only/--ast-json/--time-trace
// passthroughs of the upstream tools/driver/driver.cpp copy are gone.
//
// SPDX-FileCopyrightText: Michael Popoloski (upstream driver this derives from)
// SPDX-License-Identifier: MIT
//------------------------------------------------------------------------------
#include <memory>
#include <optional>

#include "slang/analysis/AnalysisManager.h"  // complete type for runAnalysis's unique_ptr
#include "slang/ast/Compilation.h"
#include "slang/driver/Driver.h"
#include "slang_diag.hpp"
#include "slang_tree.hpp"

using namespace slang;
using namespace slang::driver;

template <typename TArgs>
static int driverMain(int argc, TArgs argv, Slang_tree& slang_tree) {
  SLANG_TRY {
    Driver driver;
    driver.addStandardArgs();

    // --quiet is a driver-frontend flag (not part of addStandardArgs); keep it
    // because inou_slang.cpp passes it. Everything else inou_slang feeds
    // (-I/-D/-U, --ignore-unknown-modules) is a standard arg.
    std::optional<bool> quiet;
    driver.cmdLine.add("-q,--quiet", quiet, "Suppress non-essential output");

    if (!driver.parseCommandLine(argc, argv)) {
      return 1;
    }
    if (!driver.processOptions()) {
      return 2;
    }

    // Route slang's diagnostics through LiveHD's sink instead of stderr text.
    driver.diagEngine.clearClients();
    driver.diagEngine.addClient(std::make_shared<livehd::slang_diag::Sink_client>());

    const bool quietMode = quiet == true;

    bool ok = driver.parseAllSources();

    auto compilation = driver.createCompilation();
    driver.reportCompilation(*compilation, quietMode);
    driver.runAnalysis(*compilation);
    ok &= driver.reportDiagnostics(quietMode);

    if (ok) {
      slang_tree.set_source_manager(&driver.sourceManager);
      slang_tree.process_root(compilation->getRoot());
    }

    return ok ? 0 : 5;
  }
  SLANG_CATCH(const std::exception& e) {
#if __cpp_exceptions
    livehd::diag::sink().emit(livehd::diag::Diagnostic{.severity = livehd::diag::Severity::error,
                                                       .code     = "slang-internal-error",
                                                       .category = "internal",
                                                       .pass     = "inou.slang",
                                                       .message  = std::string(e.what())});
#endif
    return 6;
  }
}

#ifndef FUZZ_TARGET
int slang_main(int argc, char** argv, Slang_tree& tree) { return driverMain(argc, argv, tree); }
#endif

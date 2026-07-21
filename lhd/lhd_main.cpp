//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

// lhd — the stateless, hermetic LiveHD CLI kernel.
// See lhd.hpp and the LiveHD docs.

#include <cstdio>
#include <cstdlib>
#include <exception>

#include "diag.hpp"
#include "host_mem.hpp"
#include "iassert.hpp"
#include "lhd.hpp"
#include "lhd_pyrope.hpp"
#include "perf_tracing.hpp"

namespace {

void mark_failed(lhd::Result& res, const lhd::Lhd_error& e) {
  res.status        = "fail";
  res.exit_code     = lhd::exit_code_for(e.cls);
  res.error_class   = e.cls;
  res.error_message = e.msg;
  res.error_hint    = e.hint;
}

// Perfetto tracing lifetime (LIVEHD_PROFILING builds only; both calls compile
// to no-ops otherwise). Used to be anchored to the global Thread_pool's
// constructor/destructor; that pool was removed, so the process scope lives
// here as an RAII guard covering every main() exit path.
struct Trace_guard {
  Trace_guard() { start_tracing(); }
  ~Trace_guard() { stop_tracing(); }
};

}  // namespace

int main(int argc, char** argv) {
  I_setup();

  // Hard memory backstop (see pass/cost/host_mem.hpp). Armed FIRST, before any
  // large allocation, so a runaway pass (a whole-design ABC/LEC run that would
  // otherwise grow to hundreds of GB and take the machine down through swap/
  // jetsam) hits an address-space ceiling and dies alone instead. Inherited
  // across fork+exec, so this one call also bounds yosys and any re-invoked
  // `lhd`. Silent by default; LIVEHD_MEMORY_DEBUG reports the armed ceiling.
  // This is a backstop, not a clean error -- pass/abc's sampled admission and the
  // node-count gate produce the diagnosable refusal before it fires.
  if (const uint64_t limit = livehd::cost::install_memory_backstop();
      limit != 0 && std::getenv("LIVEHD_MEMORY_DEBUG") != nullptr) {
    std::fprintf(stderr, "lhd: memory backstop armed (RLIMIT_AS = %llu MiB)\n",
                 static_cast<unsigned long long>(limit >> 20));
  }

  Trace_guard trace_guard;

  lhd::Options opts;
  lhd::Result  res;

  try {
    opts = lhd::parse_args(argc, argv);
  } catch (const lhd::Lhd_error& e) {
    res.command = "usage";
    mark_failed(res, e);
    lhd::write_result(opts, res);
    return res.exit_code;
  } catch (const std::exception& e) {  // backstop: argv parsing must never abort
    res.command = "usage";
    mark_failed(res, lhd::Lhd_error{"usage", e.what(), ""});
    lhd::write_result(opts, res);
    return res.exit_code;
  }

  // `lhd pyrope <lsp|fmt>` — the Pyrope developer tools. `pyrope lsp` is the
  // Pyrope LSP server (JSON-RPC owns stdio, so run_stdio() reassigns fd 1
  // internally and drives the front-end passes directly); `pyrope fmt` is the
  // prpfmt source formatter. Both run before the pass/inou engine is
  // initialized and write no result envelope (fmt's output is the formatted
  // source on stdout / the rewritten files).
  if (opts.command == "pyrope") {
    return livehd::pyrope::run(opts);
  }

  if (lhd::is_meta_command(opts)) {
    return lhd::run_meta_command(opts);
  }

  res.command = opts.command;
  if (!opts.language.empty()) {
    res.command += ' ';
    res.command += opts.language;
  }

  try {
    res.run_id = lhd::compute_run_id(opts);
    lhd::init_engine();
    lhd::run_engine_command(opts, res);
  } catch (const lhd::Lhd_error& e) {
    mark_failed(res, e);
  } catch (const std::exception& e) {
    mark_failed(res, lhd::classify_engine_failure(e.what()));
  } catch (...) {
    mark_failed(res, lhd::Lhd_error{"internal", "unknown exception", ""});
  }

  auto& sink     = livehd::diag::sink();
  res.n_errors   = sink.count(livehd::diag::Severity::error);
  res.n_warnings = sink.count(livehd::diag::Severity::warning);
  if (res.status == "pass" && sink.has_errors()) {
    mark_failed(res, lhd::classify_engine_failure("diagnostics reported errors"));
  }

  lhd::write_result(opts, res);
  return res.exit_code;
}

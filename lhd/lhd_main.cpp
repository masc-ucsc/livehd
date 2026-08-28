//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

// lhd — the stateless, hermetic LiveHD CLI kernel.
// See lhd.hpp and the LiveHD docs.

#include <chrono>
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
  // AFTER I_setup: on platforms where iassert's SIGSEGV handler renders nothing
  // this takes the signal over and reports which pass died and where its log is
  // (a crash in ABC/yosys/a solver otherwise exits 1 with no output at all).
  lhd::install_crash_reporter();

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

  // argv parsing runs BEFORE the diagnostics sink is configured from opts, so a
  // failure here is counted straight into the envelope rather than emitted: the
  // header must never say "0 errors" for a run that failed (see the sink
  // reconciliation after run_engine_command below).
  try {
    opts = lhd::parse_args(argc, argv);
  } catch (const lhd::Lhd_error& e) {
    res.command  = "usage";
    res.n_errors = 1;
    mark_failed(res, e);
    lhd::write_result(opts, res);
    return res.exit_code;
  } catch (const std::exception& e) {  // backstop: argv parsing must never abort
    res.command  = "usage";
    res.n_errors = 1;
    mark_failed(res, lhd::Lhd_error{"usage", e.what(), ""});
    lhd::write_result(opts, res);
    return res.exit_code;
  }

  // Configure the diagnostics sink from `opts` ONCE, here, before anything can
  // fail. Several failures are raised BEFORE any per-step setup_diag runs
  // (--emit/--dump validation, an unknown `--set`, `lhd sim --list-tests`, and
  // every `lhd pyrope fmt` failure below), and an unconfigured sink falls back
  // to its env default: it would ignore --quiet and write its records to a
  // stray ./diag.jsonl in the user's cwd while the declared
  // `--emit diagnostics:` file stays empty. The per-step setup_diag calls
  // inside the kernel re-apply these same three settings.
  {
    auto&            sink = livehd::diag::sink();
    std::string_view diag_path{"off"};
    for (const auto& e : opts.emits) {
      if (e.kind == "diagnostics") {
        diag_path = e.path;
        break;
      }
    }
    sink.set_human_stderr(!opts.quiet);
    sink.set_stderr_jsonl(opts.diag_fmt == lhd::Diag_fmt::jsonl);
    sink.set_jsonl_path(diag_path);
  }

  // `lhd pyrope <lsp|fmt>` — the Pyrope developer tools. `pyrope lsp` is the
  // Pyrope LSP server (JSON-RPC owns stdio, so run_stdio() reassigns fd 1
  // internally and drives the front-end passes directly); `pyrope fmt` is the
  // prpfmt source formatter. Both run before the pass/inou engine is
  // initialized and write no result envelope (fmt's output is the formatted
  // source on stdout / the rewritten files) -- but their failures still go
  // through the sink configured above, so --diag-fmt / -q / --emit diagnostics:
  // work there too.
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
    // The run_id content hash is size-dependent WORK, not a constant preamble:
    // it reads every input's bytes, and for `lhd lec` with two lg: sides that is
    // the per-side slice of both libraries — measured 0.4 s of a 2.6 s proof on
    // two 32 MB minion libraries, which is where the lg-to-lg gap actually was
    // (the graph library's own load() is lazy: see "lec.load", ~0.1 ms). Timed
    // inline rather than with Phase_timer, which lives in the kernel-internal
    // header; init_engine() is pass registration only and stays in the residual.
    {
      const auto t0                                      = std::chrono::steady_clock::now();
      res.run_id                                         = lhd::compute_run_id(opts);
      const std::chrono::duration<double, std::milli> dt = std::chrono::steady_clock::now() - t0;
      res.phase_ms.emplace_back("lhd.run_id", dt.count());
    }
    lhd::init_engine();
    lhd::run_engine_command(opts, res);
  } catch (const lhd::Lhd_error& e) {
    mark_failed(res, e);
  } catch (const std::exception& e) {
    mark_failed(res, lhd::classify_engine_failure(e.what()));
  } catch (...) {
    mark_failed(res, lhd::Lhd_error{"internal", "unknown exception", ""});
  }

  auto& sink = livehd::diag::sink();
  if (res.status == "pass" && sink.has_errors()) {
    mark_failed(res, lhd::classify_engine_failure("diagnostics reported errors"));
  }
  // A kernel-level failure (a thrown Lhd_error, or any std::exception the
  // catches above classified) never went through the diagnostics sink, so the
  // envelope and the pretty header would report "0 errors" for a run that
  // failed -- a reader cannot tell that from a miscounted success, and no
  // `--emit diagnostics:` consumer sees the reason at all. Record it as the
  // error it is, exactly once (an error already in the sink IS the reason, and
  // re-reporting it would double the count).
  if (res.status != "pass" && !sink.has_errors()) {
    // Machine channel only: write_result already prints this same text to the
    // human channel (`error[<class>]: …` in pretty mode, `error.message` in the
    // envelope), so leaving the human copy on would say it twice.
    sink.set_human_stderr(false);
    // lhd's error CLASS (the exit-code vocabulary, see exit_code_for) folded
    // onto the pinned diagnostic categories, the way `missing_file` folds onto
    // `io` (core/tests/diag_test.cpp). "io" covers options and paths; anything
    // that is not a user-facing input problem stays `internal`.
    const std::string_view category = res.error_class == "syntax"        ? "syntax"
                                      : res.error_class == "unsupported" ? "unsupported"
                                      : (res.error_class == "usage" || res.error_class == "missing_file"
                                         || res.error_class == "config" || res.error_class == "dependency")
                                          ? "io"
                                          : "internal";
    auto                   b        = livehd::diag::err("lhd", "run-failed", category).msg("{}", res.error_message);
    if (!res.error_hint.empty()) {
      b.hint(res.error_hint);
    }
    b.emit();
    sink.set_human_stderr(!opts.quiet);
  }
  res.n_errors   = sink.count(livehd::diag::Severity::error);
  res.n_warnings = sink.count(livehd::diag::Severity::warning);

  lhd::write_result(opts, res);
  return res.exit_code;
}

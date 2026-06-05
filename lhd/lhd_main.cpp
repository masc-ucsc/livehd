//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

// lhd — the stateless, hermetic LiveHD CLI kernel (task 1y-bazel).
// See lhd.hpp and docs/contracts/future_cli.md.

#include <exception>

#include "diag.hpp"
#include "iassert.hpp"
#include "lhd.hpp"
#include "livehd_lsp.hpp"

namespace {

void mark_failed(lhd::Result& res, const lhd::Lhd_error& e) {
  res.status        = "fail";
  res.exit_code     = 1;
  res.error_class   = e.cls;
  res.error_message = e.msg;
  res.error_hint    = e.hint;
}

}  // namespace

int main(int argc, char** argv) {
  I_setup();

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

  // `lhd lsp` — the Pyrope LSP server (task 1n). JSON-RPC owns stdio, so no
  // result envelope is written; run_stdio() reassigns fd 1 internally and
  // drives the front-end passes directly (no engine/registry init needed).
  if (opts.command == "lsp") {
    return livehd::lsp::run_stdio();
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

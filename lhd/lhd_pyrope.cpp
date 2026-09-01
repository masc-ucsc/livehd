//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "lhd_pyrope.hpp"

#include <cstdlib>  // std::free
#include <format>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "diag.hpp"
#include "file_utils.hpp"
#include "lhd.hpp"
#include "livehd_lsp.hpp"
#include "prpfmt_api.h"

namespace livehd::pyrope {

namespace {

// Slurp a file into a string. Returns false (and leaves `out` untouched) if the
// file cannot be opened.
bool read_file(const std::string& path, std::string& out) {
  auto content = livehd::file_utils::read_file(path);
  if (!content) {
    return false;
  }
  out = std::move(*content);
  return true;
}

bool write_file(const std::string& path, std::string_view bytes) {
  std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
  if (!ofs.is_open()) {
    return false;
  }
  ofs.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  return static_cast<bool>(ofs);
}

// `lhd pyrope fmt` — format Pyrope sources. inputs are opts.files[1..] (files[0]
// is the `fmt` sub-command word). Mirrors clang-format: prints the formatted
// source to stdout by default; -i/--inplace rewrites each file; -o/--output
// writes to a file (one input only).
int run_fmt(const lhd::Options& opts) {
  std::vector<std::string> inputs(opts.files.begin() + 1, opts.files.end());
  if (inputs.empty()) {
    livehd::diag::err("lhd.pyrope.fmt", "no-input", "io")
        .msg("no input files")
        .hint("usage: lhd pyrope fmt FILE… [-i] [-o OUT] [--indent N] [--width N] [--verify]")
        .emit();
    return 1;
  }
  if (opts.fmt_inplace && !opts.fmt_output.empty()) {
    livehd::diag::err("lhd.pyrope.fmt", "conflicting-options", "io")
        .msg("-i/--inplace and -o/--output are mutually exclusive")
        .emit();
    return 1;
  }
  if (!opts.fmt_output.empty() && inputs.size() > 1) {
    livehd::diag::err("lhd.pyrope.fmt", "too-many-inputs", "io")
        .msg("-o/--output takes a single input file (got {})", inputs.size())
        .hint("use -i/--inplace to rewrite several files")
        .emit();
    return 1;
  }

  int exit_code = 0;
  for (const auto& path : inputs) {
    std::string src;
    if (!read_file(path, src)) {
      livehd::diag::err("lhd.pyrope.fmt", "missing-file", "io").msg("cannot open '{}'", path).emit();
      exit_code = 1;
      continue;
    }

    char*  out = nullptr;
    size_t out_len = 0;
    int    rc = prpfmt_format_string(src.data(), src.size(), opts.fmt_indent, opts.fmt_width,
                                     opts.fmt_verify ? 1 : 0, &out, &out_len);
    if (rc == 2) {
      livehd::diag::err("lhd.pyrope.fmt", "parse-failed", "syntax")
          .msg("'{}' did not parse", path)
          .hint(std::format("run `lhd compile {}` to locate the error", path))
          .emit();
      exit_code = 1;
      continue;
    }
    if (rc == 1 || out == nullptr) {
      livehd::diag::err("lhd.pyrope.fmt", "format-failed", "internal").msg("'{}' could not be formatted", path).emit();
      exit_code = 1;
      std::free(out);
      continue;
    }
    if (rc == 3) {
      livehd::diag::warn("lhd.pyrope.fmt", "verify-failed", "syntax")
          .msg("'{}' formatted output failed to re-parse (--verify); emitting anyway", path)
          .emit();
      exit_code = 1;
    }

    std::string_view formatted{out, out_len};
    if (opts.fmt_inplace) {
      if (formatted != src) {  // skip rewriting an already-formatted file (preserve mtime)
        if (!write_file(path, formatted)) {
          livehd::diag::err("lhd.pyrope.fmt", "write-failed", "io").msg("cannot write '{}'", path).emit();
          exit_code = 1;
        }
      }
    } else if (!opts.fmt_output.empty()) {
      if (!write_file(opts.fmt_output, formatted)) {
        livehd::diag::err("lhd.pyrope.fmt", "write-failed", "io").msg("cannot write '{}'", opts.fmt_output).emit();
        exit_code = 1;
      }
    } else {
      std::fwrite(formatted.data(), 1, formatted.size(), stdout);
    }
    std::free(out);
  }
  return exit_code;
}

}  // namespace

int run(const lhd::Options& opts) {
  const std::string sub = opts.files.empty() ? "" : opts.files.front();

  if (sub == "lsp") {
    return livehd::lsp::run_stdio();
  }
  if (sub == "fmt") {
    return run_fmt(opts);
  }
  if (sub.empty()) {
    livehd::diag::err("lhd.pyrope", "missing-subcommand", "io").msg("a sub-command is required").hint("lsp | fmt").emit();
  } else {
    livehd::diag::err("lhd.pyrope", "unknown-subcommand", "io")
        .msg("unknown sub-command '{}'", sub)
        .hint("lsp | fmt")
        .emit();
  }
  return 1;
}

}  // namespace livehd::pyrope

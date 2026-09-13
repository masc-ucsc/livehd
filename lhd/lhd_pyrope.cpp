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
#include "pyrope_style.hpp"

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

    char*  out     = nullptr;
    size_t out_len = 0;
    int rc = prpfmt_format_string(src.data(), src.size(), opts.fmt_indent, opts.fmt_width, opts.fmt_verify ? 1 : 0, &out, &out_len);
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

livehd::diag::Span style_span(const std::string& path, const style::Range& r) {
  livehd::diag::Span span;
  span.file       = path;
  span.start_byte = r.start_byte;
  span.end_byte   = r.end_byte;
  span.start_line = r.start_line;
  span.start_col  = r.start_column;
  span.end_line   = r.end_line;
  span.end_col    = r.end_column;
  return span;
}

int run_style(const lhd::Options& opts) {
  if (opts.files.size() < 2) {
    livehd::diag::err("lhd.pyrope.style", "no-input", "io")
        .msg("no input files")
        .hint("usage: lhd pyrope style FILE… [--min-repeats N] [--max-block-statements N] [--max-findings N]")
        .emit();
    return 1;
  }
  const style::Options config{opts.style_min_repeats, opts.style_max_block_statements, opts.style_max_findings};
  int                  status = 0;
  for (size_t i = 1; i < opts.files.size(); ++i) {
    const auto& path = opts.files[i];
    std::string src;
    if (!read_file(path, src)) {
      livehd::diag::err("lhd.pyrope.style", "missing-file", "io").msg("cannot open '{}'", path).emit();
      status = 1;
      continue;
    }
    try {
      const auto report = style::analyze(src, config);
      if (report.partial) {
        auto b = livehd::diag::warn("lhd.pyrope.style", "partial-analysis", "syntax");
        b.msg("'{}' contains syntax errors; reporting patterns only in intact statement sequences", path);
        for (const auto& r : report.parse_errors) {
          b.note("Tree-sitter error or missing syntax; this region was skipped", style_span(path, r));
        }
        b.emit();
      }
      for (const auto& f : report.findings) {
        auto b = livehd::diag::Builder(livehd::diag::Severity::info,
                                       "lhd.pyrope.style",
                                       f.progressing ? "likely-unrolled-loop" : "repeated-code",
                                       "syntax");
        b.at(style_span(path, f.range))
            .msg("{}: {} statements per copy, repeated {} times across lines {}-{}",
                 f.progressing ? "likely unrolled loop" : "repeated code",
                 f.statements,
                 f.repetitions,
                 f.range.start_line,
                 f.range.end_line)
            .hint(f.progressing ? "consider a loop; numbered scalar names may first need an indexed collection"
                                : "consider a loop or a shared helper")
            .attr("statements_per_copy", std::to_string(f.statements))
            .attr("repetitions", std::to_string(f.repetitions))
            .attr("score", std::to_string(f.score))
            .attr("template", f.pattern)
            .attr("progression", f.progression)
            .note("first copy; template placeholders are descriptive, not executable Pyrope", style_span(path, f.first_copy))
            .note(std::format("template:\n{}", f.pattern));
        if (!f.progression.empty()) {
          b.note(std::format("{}; i = 0..{}", f.progression, f.repetitions - 1));
        }
        b.emit();
      }
      livehd::diag::Builder(livehd::diag::Severity::info, "lhd.pyrope.style", "style-summary", "syntax")
          .msg("'{}': {} suggestions, {} shown{}",
               path,
               report.total_findings,
               report.findings.size(),
               report.partial ? " (partial parse)" : "")
          .attr("file", path)
          .attr("partial", report.partial ? "true" : "false")
          .attr("total_findings", std::to_string(report.total_findings))
          .attr("shown_findings", std::to_string(report.findings.size()))
          .emit();
    } catch (const std::exception& e) {
      livehd::diag::err("lhd.pyrope.style", "analysis-failed", "internal").msg("'{}': {}", path, e.what()).emit();
      status = 1;
    }
  }
  return status;
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
  if (sub == "style") {
    return run_style(opts);
  }
  if (sub.empty()) {
    livehd::diag::err("lhd.pyrope", "missing-subcommand", "io").msg("a sub-command is required").hint("lsp | fmt | style").emit();
  } else {
    livehd::diag::err("lhd.pyrope", "unknown-subcommand", "io")
        .msg("unknown sub-command '{}'", sub)
        .hint("lsp | fmt | style")
        .emit();
  }
  return 1;
}

}  // namespace livehd::pyrope

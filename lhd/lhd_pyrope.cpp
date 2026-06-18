//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "lhd_pyrope.hpp"

#include <cstdlib>  // std::free
#include <fstream>
#include <print>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "lhd.hpp"
#include "livehd_lsp.hpp"
#include "prpfmt_api.h"

namespace livehd::pyrope {

namespace {

// Slurp a file into a string. Returns false (and leaves `out` untouched) if the
// file cannot be opened.
bool read_file(const std::string& path, std::string& out) {
  std::ifstream ifs(path, std::ios::binary);
  if (!ifs.is_open()) {
    return false;
  }
  std::ostringstream ss;
  ss << ifs.rdbuf();
  out = ss.str();
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
    std::print(stderr,
               "lhd pyrope fmt: no input files\n"
               "usage: lhd pyrope fmt FILE… [-i] [-o OUT] [--indent N] [--width N] [--verify]\n");
    return 1;
  }
  if (opts.fmt_inplace && !opts.fmt_output.empty()) {
    std::print(stderr, "lhd pyrope fmt: -i/--inplace and -o/--output are mutually exclusive\n");
    return 1;
  }
  if (!opts.fmt_output.empty() && inputs.size() > 1) {
    std::print(stderr, "lhd pyrope fmt: -o/--output takes a single input file (got {})\n", inputs.size());
    return 1;
  }

  int exit_code = 0;
  for (const auto& path : inputs) {
    std::string src;
    if (!read_file(path, src)) {
      std::print(stderr, "lhd pyrope fmt: cannot open '{}'\n", path);
      exit_code = 1;
      continue;
    }

    char*  out = nullptr;
    size_t out_len = 0;
    int    rc = prpfmt_format_string(src.data(), src.size(), opts.fmt_indent, opts.fmt_width,
                                     opts.fmt_verify ? 1 : 0, &out, &out_len);
    if (rc == 2) {
      std::print(stderr, "lhd pyrope fmt: '{}' did not parse — run `lhd compile {}` to locate the error\n", path, path);
      exit_code = 1;
      continue;
    }
    if (rc == 1 || out == nullptr) {
      std::print(stderr, "lhd pyrope fmt: '{}' could not be formatted (internal error)\n", path);
      exit_code = 1;
      std::free(out);
      continue;
    }
    if (rc == 3) {
      std::print(stderr, "lhd pyrope fmt: WARNING '{}' formatted output failed to re-parse (--verify); emitting anyway\n",
                 path);
      exit_code = 1;
    }

    std::string_view formatted{out, out_len};
    if (opts.fmt_inplace) {
      if (formatted != src) {  // skip rewriting an already-formatted file (preserve mtime)
        if (!write_file(path, formatted)) {
          std::print(stderr, "lhd pyrope fmt: cannot write '{}'\n", path);
          exit_code = 1;
        }
      }
    } else if (!opts.fmt_output.empty()) {
      if (!write_file(opts.fmt_output, formatted)) {
        std::print(stderr, "lhd pyrope fmt: cannot write '{}'\n", opts.fmt_output);
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
    std::print(stderr, "lhd pyrope: a sub-command is required (lsp | fmt)\n");
  } else {
    std::print(stderr, "lhd pyrope: unknown sub-command '{}' (lsp | fmt)\n", sub);
  }
  return 1;
}

}  // namespace livehd::pyrope

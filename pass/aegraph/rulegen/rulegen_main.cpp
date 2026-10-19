//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

// rulegen: the host tool behind tools/aegraph.bzl's aeg_rules().
//
//   rulegen --out-dir DIR --analyses rules/analyses.decl
//           --group arith=rules/arith.rules --group shift=rules/shift.rules
//           --native rules/arith.cpp ...
//
// Reads the rule files, runs the lints (any error fails the run, so a bad rule
// fails the build that generates it), and writes, per group G:
//   G_rules.gen.cpp            the rule code (#line back into the .rules file)
//   aeg_fuzz_G_smoke.gen.cpp   LHS/RHS equality fuzz + "example fires"
//   aeg_prove_G_smoke.gen.cpp  the bv proof tier
// and RULES.generated.md for the whole set. The group order is the --group order.

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "rules.hpp"

namespace rg = livehd::aegraph::rulegen;

namespace {

bool read_file(const std::string& path, std::string& text) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return false;
  }
  std::ostringstream ss;
  ss << in.rdbuf();
  text = ss.str();
  return true;
}

bool write_file(const std::filesystem::path& path, const std::string& text) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out << text;
  return static_cast<bool>(out);
}

int usage() {
  std::fputs("usage: rulegen --out-dir DIR [--analyses FILE] --group NAME=FILE... [--native FILE...] [--lint-only]\n", stderr);
  return 2;
}

}  // namespace

int main(int argc, char** argv) {
  std::string                                       out_dir, analyses_path;
  std::vector<std::pair<std::string, std::string>> groups;
  std::vector<std::string>                          natives;
  bool                                              lint_only = false;
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    const auto        value = [&]() -> std::string { return i + 1 < argc ? argv[++i] : std::string{}; };
    if (a == "--out-dir") {
      out_dir = value();
    } else if (a == "--analyses") {
      analyses_path = value();
    } else if (a == "--group") {
      const auto v  = value();
      const auto eq = v.find('=');
      if (eq == std::string::npos) {
        return usage();
      }
      groups.emplace_back(v.substr(0, eq), v.substr(eq + 1));
    } else if (a == "--native") {
      natives.push_back(value());
    } else if (a == "--lint-only") {
      lint_only = true;
    } else {
      return usage();
    }
  }
  if ((out_dir.empty() && !lint_only) || groups.empty()) {
    return usage();
  }

  rg::Diags diags;
  std::string text;

  rg::Analyses analyses;
  if (!analyses_path.empty()) {
    if (!read_file(analyses_path, text)) {
      std::fprintf(stderr, "rulegen: cannot read %s\n", analyses_path.c_str());
      return 1;
    }
    analyses = rg::parse_analyses(text, analyses_path, diags);
  }
  std::vector<rg::Rules_file> files;
  for (const auto& [group, path] : groups) {
    if (!read_file(path, text)) {
      std::fprintf(stderr, "rulegen: cannot read %s\n", path.c_str());
      return 1;
    }
    files.push_back(rg::parse_rules(text, path, group, diags));
  }
  std::vector<rg::Native_rule> native_rules;
  for (const auto& path : natives) {
    if (!read_file(path, text)) {
      std::fprintf(stderr, "rulegen: cannot read %s\n", path.c_str());
      return 1;
    }
    auto found = rg::scan_native(text, path);
    native_rules.insert(native_rules.end(), found.begin(), found.end());
  }

  // Lint only what parsed: a syntax error leaves half a tree behind.
  if (diags.empty()) {
    rg::lint(files, analyses, native_rules, diags);
  }
  bool failed = false;
  for (const auto& d : diags) {
    std::fprintf(stderr, "%s\n", d.c_str());
    failed = failed || d.find(": error: ") != std::string::npos;
  }
  if (failed) {
    std::fputs("rulegen: the rule files have errors; nothing generated\n", stderr);
    return 1;
  }
  if (lint_only) {
    return 0;
  }

  const std::filesystem::path dir(out_dir);
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  if (ec) {
    std::fprintf(stderr, "rulegen: cannot create %s: %s\n", out_dir.c_str(), ec.message().c_str());
    return 1;
  }
  bool ok = true;
  for (const auto& f : files) {
    ok = write_file(dir / (f.group + "_rules.gen.cpp"), rg::emit_rules_cpp(f, analyses)) && ok;
    ok = write_file(dir / ("aeg_fuzz_" + f.group + "_smoke.gen.cpp"), rg::emit_fuzz_test(f, analyses)) && ok;
    ok = write_file(dir / ("aeg_prove_" + f.group + "_smoke.gen.cpp"), rg::emit_prove_test(f, analyses)) && ok;
  }
  ok = write_file(dir / "RULES.generated.md", rg::emit_rules_md(files, analyses, native_rules)) && ok;
  if (!ok) {
    std::fprintf(stderr, "rulegen: cannot write into %s\n", out_dir.c_str());
    return 1;
  }
  return 0;
}

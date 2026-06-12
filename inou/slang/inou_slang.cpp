//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

// clang-format off

#include "absl/strings/str_split.h"

#include "slang_tree.hpp"
#include "inou_slang.hpp"

#include "perf_tracing.hpp"

// clang-format on

extern int slang_main(int argc, char** argv, Slang_tree& tree);  // in slang_driver.cpp

static Pass_plugin sample("inou.verilog", Inou_slang::setup);

void Inou_slang::setup() {
  Eprp_method m1("inou.verilog", "System verilog to LNAST using slang", &Inou_slang::work);

  m1.add_label_required("files", "input verilog files");
  m1.add_label_optional("includes", "comma separated include paths (otherwise, verilog paths)");
  m1.add_label_optional("defines", "comma separated defines. E.g: defines:foo=1,XXX,LALA=1");
  m1.add_label_optional("undefines", "comma separated undefines");
  m1.add_label_optional("timecheck", "true to keep timechecks on generated mods (default: suppressed for slang input)");

  register_pass(m1);

  Eprp_method m2("inou.slang", "alias for inou.verilog (System verilog to LNAST using slang)", &Inou_slang::work);
  m2.add_label_required("files", "input verilog files");
  m2.add_label_optional("includes", "comma separated include paths (otherwise, verilog paths)");
  m2.add_label_optional("defines", "comma separated defines. E.g: defines:foo=1,XXX,LALA=1");
  m2.add_label_optional("undefines", "comma separated undefines");
  m2.add_label_optional("timecheck", "true to keep timechecks on generated mods (default: suppressed for slang input)");

  register_pass(m2);
}

Inou_slang::Inou_slang(const Eprp_var& var) : Pass("inou.slang", var) {}

void Inou_slang::work(Eprp_var& var) {
  TRACE_EVENT("verilog", "verilog_tolnast");
  Inou_slang p(var);

  std::vector<char*> argv;

  argv.push_back(strdup("lhd"));  // argv[0] placeholder for the slang driver

  argv.push_back(strdup("--quiet"));
  argv.push_back(strdup("--ignore-unknown-modules"));
  // argv.push_back(strdup("--single-unit"));

  if (var.has_label("includes")) {
    auto txt = var.get("includes");
    for (const auto f : absl::StrSplit(txt, ',')) {
      argv.push_back(strdup("-I"));
      argv.push_back(strdup(std::string(f).c_str()));
    }
  }

  if (var.has_label("defines")) {
    auto txt = var.get("defines");
    for (const auto f : absl::StrSplit(txt, ',')) {
      argv.push_back(strdup("-D"));
      argv.push_back(strdup(std::string(f).c_str()));
    }
  }

  if (var.has_label("undefines")) {
    auto txt = var.get("undefines");
    for (const auto f : absl::StrSplit(txt, ',')) {
      argv.push_back(strdup("-U"));
      argv.push_back(strdup(std::string(f).c_str()));
    }
  }

  // Timechecks on generated `mod`s are suppressed by default for slang input
  // (the direct reader predates the io/timing conventions, todo/ 1s subtask E),
  // overridable via --set inou.verilog.timecheck=true.
  const bool keep_timecheck = var.has_label("timecheck") && var.get("timecheck") == "true";

  // One isolated slang Driver/Compilation per file, processed sequentially. The
  // old in-process thread_pool fan-out was never sound (two FIXME: slang
  // multithread fails comments); the build system exposes parallelism instead by
  // running independent `lhd elaborate --reader slang` invocations concurrently.
  std::vector<std::string> file_list = absl::StrSplit(p.files, ',');

  for (const auto& fname : file_list) {
    TRACE_EVENT("verilog", nullptr, [&fname](perfetto::EventContext ctx) { ctx.event()->set_name(fname); });

    Slang_tree tree;

    std::vector<char*> argv_final{argv};

    char* ptr_fname = strdup(fname.c_str());

    argv_final.emplace_back(ptr_fname);
    argv_final.emplace_back(nullptr);

    slang_main(argv_final.size() - 1, argv_final.data(), tree);  // compile to lnasts

    for (auto& ln : tree.pick_lnast()) {
      ln->set_skip_timecheck(!keep_timecheck);
      var.add(ln);
    }

    free(ptr_fname);
  }

  for (char* ptr : argv) {
    if (ptr) {
      free(ptr);
    }
  }
}

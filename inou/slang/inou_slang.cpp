//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

// clang-format off

#include <algorithm>

#include "absl/strings/str_split.h"

#include "diag.hpp"
#include "slang_context.hpp"
#include "inou_slang.hpp"

#include "perf_tracing.hpp"

// clang-format on

extern int slang_main(int argc, char** argv, Slang_context& tree);  // in slang_driver.cpp

static Pass_plugin sample("inou.verilog", Inou_slang::setup);

void Inou_slang::setup() {
  Eprp_method m1("inou.verilog", "System verilog to LNAST using slang", &Inou_slang::work);

  m1.add_label_optional("files", "input verilog files (optional when slang_flags supplies the sources, e.g. -F filelist.f)");
  m1.add_label_optional("top", "elaborate only this top module's hierarchy (forwarded to slang as --top)");
  m1.add_label_optional("includes", "comma separated include paths (otherwise, verilog paths)");
  m1.add_label_optional("defines", "comma separated defines. E.g: defines:foo=1,XXX,LALA=1");
  m1.add_label_optional("undefines", "comma separated undefines");
  m1.add_label_optional("timecheck", "true to keep timechecks on generated mods (default: suppressed for slang input)");
  m1.add_label_optional("unroll_limit", "slang-side loop unroll budget per process (default: 4000)");
  m1.add_label_optional("slang_flags",
                        "raw slang driver args ('\\x1f'-separated, e.g. -F filelist.f); supplied by `lhd --reader slang -- ...`");

  register_pass(m1);

  Eprp_method m2("inou.slang", "alias for inou.verilog (System verilog to LNAST using slang)", &Inou_slang::work);
  m2.add_label_optional("files", "input verilog files (optional when slang_flags supplies the sources, e.g. -F filelist.f)");
  m2.add_label_optional("top", "elaborate only this top module's hierarchy (forwarded to slang as --top)");
  m2.add_label_optional("includes", "comma separated include paths (otherwise, verilog paths)");
  m2.add_label_optional("defines", "comma separated defines. E.g: defines:foo=1,XXX,LALA=1");
  m2.add_label_optional("undefines", "comma separated undefines");
  m2.add_label_optional("timecheck", "true to keep timechecks on generated mods (default: suppressed for slang input)");
  m2.add_label_optional("unroll_limit", "slang-side loop unroll budget per process (default: 4000)");
  m2.add_label_optional("slang_flags",
                        "raw slang driver args ('\\x1f'-separated, e.g. -F filelist.f); supplied by `lhd --reader slang -- ...`");

  register_pass(m2);
}

Inou_slang::Inou_slang(const Eprp_var& var) : Pass("inou.slang", var) {}

void Inou_slang::work(Eprp_var& var) {
  TRACE_EVENT("verilog", "verilog_tolnast");
  Inou_slang p(var);

  std::vector<char*> argv;

  argv.push_back(strdup("lhd"));  // argv[0] placeholder for the slang driver

  // Collect the raw slang_flags first so the convenience defaults below
  // (`--quiet`, `--ignore-unknown-modules`) are only injected when the caller
  // did NOT already pass them. slang declares these as `optional<bool>`
  // options, so supplying one twice is a hard "more than one value provided"
  // error rather than a harmless repeat.
  std::vector<std::string> user_flags;
  const bool               has_slang_flags = var.has_label("slang_flags");
  if (has_slang_flags) {
    auto txt = var.get("slang_flags");
    for (const auto f : absl::StrSplit(txt, '\x1f')) {
      if (!f.empty()) {
        user_flags.emplace_back(f);
      }
    }
  }
  const auto user_has = [&](std::string_view flag) {
    return std::find(user_flags.begin(), user_flags.end(), flag) != user_flags.end();
  };

  if (!user_has("-q") && !user_has("--quiet")) {
    argv.push_back(strdup("--quiet"));
  }
  if (!user_has("--ignore-unknown-modules")) {
    argv.push_back(strdup("--ignore-unknown-modules"));
  }

  // Forward lhd's `--top` so slang elaborates ONLY that module's hierarchy.
  // Without it slang auto-tops EVERY uninstantiated module (e.g. a SimTop with
  // difftest probes), wasting elaboration on -- and emitting reader errors for
  // -- modules outside the design under test. Skip when the caller already
  // passed an explicit `--top` in the raw slang args (slang's `--top` is
  // repeatable, but honor the user's intent).
  if (!user_has("--top") && var.has_label("top")) {
    auto top = var.get("top");
    if (!top.empty() && top != "-auto-top") {
      argv.push_back(strdup("--top"));
      argv.push_back(strdup(std::string(top).c_str()));
    }
  }

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

  // Raw slang driver args (e.g. `-F filelist.f`) passed through verbatim from
  // `lhd --reader slang -- <args>` (already split into `user_flags` above).
  for (const auto& f : user_flags) {
    argv.push_back(strdup(f.c_str()));
  }

  // Timechecks on generated `mod`s are suppressed by default for slang input
  // (the direct reader predates the io/timing conventions, todo/ 1s subtask E),
  // overridable via --set inou.verilog.timecheck=true.
  const bool keep_timecheck = var.has_label("timecheck") && var.get("timecheck") == "true";

  Slang_context::Options opts;
  opts.keep_timecheck = keep_timecheck;
  if (var.has_label("unroll_limit")) {
    if (int v = atoi(std::string(var.get("unroll_limit")).c_str()); v > 0) {
      opts.unroll_limit = v;
    }
  }

  // Run one isolated slang Driver/Compilation over `argv` (plus an optional
  // positional source file), lowering its lnasts into `var`. `fname == nullptr`
  // means the sources come from `slang_flags` alone (e.g. `-F filelist.f`).
  auto compile_unit = [&](const char* fname) {
    Slang_context tree;
    tree.set_options(opts);

    std::vector<char*> argv_final{argv};
    char*              ptr_fname = nullptr;
    if (fname != nullptr) {
      ptr_fname = strdup(fname);
      argv_final.emplace_back(ptr_fname);
    }
    argv_final.emplace_back(nullptr);

    slang_main(argv_final.size() - 1, argv_final.data(), tree);  // compile to lnasts

    for (auto& ln : tree.pick_lnast()) {
      ln->set_skip_timecheck(!keep_timecheck);
      var.add(ln);
    }

    if (ptr_fname != nullptr) {
      free(ptr_fname);
    }
  };

  // `files` is optional: slang_flags (e.g. `-F filelist.f`) may supply the
  // sources instead. The `files` label, when present, is a comma list; when
  // absent the Pass base leaves a "/INVALID" sentinel, so read the label
  // directly rather than `p.files`.
  std::vector<std::string> file_list;
  if (var.has_label("files")) {
    auto txt = var.get("files");
    if (!txt.empty()) {
      file_list = absl::StrSplit(txt, ',');
    }
  }

  if (file_list.empty()) {
    if (!has_slang_flags) {
      livehd::diag::err("inou.slang", "no-input", "io")
          .msg("inou.slang needs `files` or slang_flags supplying the sources (e.g. -F filelist.f)")
          .fatal();
    }
    // One isolated slang Driver/Compilation; sources come from slang_flags.
    compile_unit(nullptr);
  } else {
    // One isolated slang Driver/Compilation per file, processed sequentially.
    // The old in-process thread_pool fan-out was never sound (two FIXME: slang
    // multithread fails comments); the build system exposes parallelism instead
    // by running independent `lhd elaborate --reader slang` invocations
    // concurrently.
    for (const auto& fname : file_list) {
      TRACE_EVENT("verilog", nullptr, [&fname](perfetto::EventContext ctx) { ctx.event()->set_name(fname); });
      compile_unit(fname.c_str());
    }
  }

  for (char* ptr : argv) {
    if (ptr) {
      free(ptr);
    }
  }
}

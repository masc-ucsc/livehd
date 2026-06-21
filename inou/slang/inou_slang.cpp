//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

// clang-format off

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <optional>

#include "absl/strings/str_split.h"

#include "diag.hpp"
#include "file_utils.hpp"
#include "slang_context.hpp"
#include "inou_slang.hpp"

#include "perf_tracing.hpp"

// clang-format on

extern int slang_main(int argc, char** argv, Slang_context& tree);  // in slang_driver.cpp

static Pass_plugin sample("inou.verilog", Inou_slang::setup);

namespace {
namespace fs = std::filesystem;

// Locate LiveHD's built-in `ware/rtl` Verilog library: the cgen_memory_*.v RAM
// wrappers that cgen emits ``\`include "cgen_memory_<R>rd_<W>wr.v"`` lines for.
// cgen-generated Verilog read back through --reader slang (e.g. the `lhd lec`
// re-check path) must find these, so the directory is added to slang's include
// search path by default. Probed across the bazel runfiles layouts (a direct
// ./bazel-bin invocation, `bazel run`, `bazel test`) and the source/install
// tree; returns nullopt when no copy is found (the include then fails as before
// — nothing to add). A directory only counts when it actually holds a wrapper.
std::optional<std::string> find_ware_rtl_dir() {
  auto is_ware_rtl = [](const fs::path& p) -> bool {
    std::error_code ec;
    return fs::is_regular_file(p / "cgen_memory_1rd_1wr.v", ec);
  };
  // A runfiles/exec root nests the workspace under `_main` (bzlmod) or `livehd`
  // (legacy), but a plain source/exec root holds `ware/rtl` directly.
  auto under = [&](const fs::path& base) -> std::optional<std::string> {
    for (const char* ws : {"_main", "livehd"}) {
      if (fs::path cand = base / ws / "ware" / "rtl"; is_ware_rtl(cand)) {
        return cand.string();
      }
    }
    if (fs::path direct = base / "ware" / "rtl"; is_ware_rtl(direct)) {
      return direct.string();
    }
    return std::nullopt;
  };

  // 1. Runfiles dir from the environment (`bazel run` / `bazel test`).
  for (const char* env : {"RUNFILES_DIR", "TEST_SRCDIR"}) {
    if (const char* v = std::getenv(env); v != nullptr && *v != '\0') {
      if (auto r = under(fs::path(v))) {
        return r;
      }
    }
  }

  // 2. `<exe>.runfiles/...` sitting next to the binary (direct ./bazel-bin run).
  const fs::path  exe_dir = file_utils::get_exe_path();
  std::error_code ec;
  for (const auto& e : fs::directory_iterator(exe_dir, ec)) {
    if (e.path().extension() == ".runfiles") {
      if (auto r = under(e.path())) {
        return r;
      }
    }
  }

  // 3. Source / install tree relative to the exe, then the current directory.
  for (const fs::path& base : {exe_dir, exe_dir.parent_path(), exe_dir.parent_path().parent_path(), fs::path{"."}}) {
    if (auto r = under(base)) {
      return r;
    }
  }

  return std::nullopt;
}
}  // namespace

void Inou_slang::setup() {
  Eprp_method m1("inou.verilog", "System verilog to LNAST using slang", &Inou_slang::work);

  m1.add_label_optional("files", "input verilog files (optional when slang_flags supplies the sources, e.g. -F filelist.f)");
  m1.add_label_optional("top", "elaborate only this top module's hierarchy (forwarded to slang as --top)");
  m1.add_label_optional("includes", "extra comma separated include paths (the input file dirs and the built-in ware/rtl library are always searched too)");
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
  m2.add_label_optional("includes", "extra comma separated include paths (the input file dirs and the built-in ware/rtl library are always searched too)");
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

  // `files` is optional: slang_flags (e.g. `-F filelist.f`) may supply the
  // sources instead. The `files` label, when present, is a comma list; when
  // absent the Pass base leaves a "/INVALID" sentinel, so read the label
  // directly rather than `p.files`. Parsed up here so the input directories can
  // seed the default include search path below.
  std::vector<std::string> file_list;
  if (var.has_label("files")) {
    auto txt = var.get("files");
    if (!txt.empty()) {
      file_list = absl::StrSplit(txt, ',');
    }
  }

  // Include search path, in priority order: explicit `includes`, then the
  // directory of every input source file (the documented "verilog paths"
  // default — lets a source ``\`include`` a header sitting next to a sibling
  // input), then LiveHD's built-in `ware/rtl` library (so cgen-generated
  // Verilog, whose memories ``\`include "cgen_memory_*.v"``, reads back without
  // the caller spelling out `-I ware/rtl`). slang searches a quoted include's
  // own directory first, so these are pure fallbacks and never shadow a local
  // file. De-duplicated to keep the argv tidy.
  std::vector<std::string> inc_dirs;
  const auto               add_inc = [&](std::string dir) {
    if (dir.empty()) {
      return;
    }
    if (std::find(inc_dirs.begin(), inc_dirs.end(), dir) == inc_dirs.end()) {
      inc_dirs.emplace_back(std::move(dir));
    }
  };

  if (var.has_label("includes")) {
    for (const auto f : absl::StrSplit(var.get("includes"), ',')) {
      add_inc(std::string(f));
    }
  }
  for (const auto& f : file_list) {
    add_inc(fs::path(f).parent_path().string());
  }
  if (auto ware = find_ware_rtl_dir()) {
    add_inc(*ware);
  }
  for (const auto& dir : inc_dirs) {
    argv.push_back(strdup("-I"));
    argv.push_back(strdup(dir.c_str()));
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

  // Run ONE slang Driver/Compilation over `argv` plus every positional source
  // file in `fnames`, lowering its lnasts into `var`. All files share a single
  // compilation so cross-file references resolve: packages defined in one file
  // are visible to importers, and a module instantiating another finds its
  // definition (the per-file isolated-compilation model could not — it reported
  // `unknown package`/`unknown module` for any multi-file design). `--top`
  // restricts elaboration to that design's hierarchy; `lower_instance` recurses
  // into each instantiated definition (deduped), so one compilation still emits
  // one Lnast per reachable module. An empty `fnames` means the sources come
  // from `slang_flags` alone (e.g. `-F filelist.f`).
  auto compile_unit = [&](const std::vector<std::string>& fnames) {
    Slang_context tree;
    tree.set_options(opts);

    std::vector<char*> argv_final{argv};
    std::vector<char*> owned;
    owned.reserve(fnames.size());
    for (const auto& fname : fnames) {
      char* ptr = strdup(fname.c_str());
      owned.emplace_back(ptr);
      argv_final.emplace_back(ptr);
    }
    argv_final.emplace_back(nullptr);

    slang_main(argv_final.size() - 1, argv_final.data(), tree);  // compile to lnasts

    for (auto& ln : tree.pick_lnast()) {
      ln->set_skip_timecheck(!keep_timecheck);
      var.add(ln);
    }

    for (char* ptr : owned) {
      free(ptr);
    }
  };

  if (file_list.empty()) {
    if (!has_slang_flags) {
      livehd::diag::err("inou.slang", "no-input", "io")
          .msg("inou.slang needs `files` or slang_flags supplying the sources (e.g. -F filelist.f)")
          .fatal();
    }
    // One slang Driver/Compilation; sources come from slang_flags.
    compile_unit({});
  } else {
    // One slang Driver/Compilation over every source file. A multi-file design
    // (package + submodules + top) must share a single compilation so slang can
    // resolve cross-file packages and instantiations; the build system still
    // gets parallelism by running independent `lhd compile --reader slang`
    // invocations (one design each) concurrently.
    compile_unit(file_list);
  }

  for (char* ptr : argv) {
    if (ptr) {
      free(ptr);
    }
  }
}

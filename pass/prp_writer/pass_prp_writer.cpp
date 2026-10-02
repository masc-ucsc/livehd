//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "pass_prp_writer.hpp"

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <unistd.h>  // getpid -- the self-check scratch dir
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "absl/container/flat_hash_set.h"
#include "file_name.hpp"   // livehd::unit_file_stem — the shared long-name policy
#include "file_utils.hpp"  // get_exe_path / read_file — the self-check recompile
#include "lnast_prp_writer.hpp"
#include "perf_tracing.hpp"  // TRACE_EVENT — no-op unless built with --define profiling=1
#include "worker_pool.hpp"   // livehd::run_workers (big-stack workers)

static Pass_plugin sample("pass_prp_writer", Pass_prp_writer::setup);

namespace {
// `units` (one source file's, sorted by name) reordered so every lambda follows
// the same-file lambdas it calls -- a Pyrope call names a lambda declared
// EARLIER, so a sibling instantiated by a body or called by an input default
// (`b:U8 = g(a=a)`, user ruling 2026-09-28 (39)) must be written first. The
// file-level unit stays first; independent lambdas keep their name order.
void order_callees_first(std::vector<std::shared_ptr<Lnast>>& units, std::string_view file_name) {
  std::unordered_map<std::string, size_t> by_name;
  for (size_t i = 0; i < units.size(); ++i) {
    by_name.emplace(std::string(units[i]->get_top_module_name()), i);
  }
  const auto callees_of = [&](const Lnast& ln) {
    std::vector<size_t> out;
    for (const auto& n : ln.depth_preorder(ln.get_root())) {
      const auto nid = Lnast_nid(n);
      if (!Lnast_ntype::is_func_call(ln.get_type(nid))) {
        continue;
      }
      const auto dst = ln.get_first_child(nid);
      const auto cal = dst.is_invalid() ? dst : ln.get_sibling_next(dst);
      if (cal.is_invalid()) {
        continue;
      }
      std::string_view name = ln.get_name(cal);
      if (name.size() >= 2 && name.front() == '\'' && name.back() == '\'') {
        name = name.substr(1, name.size() - 2);
      }
      auto it = by_name.find(std::string(name));
      if (it == by_name.end()) {
        it = by_name.find(std::format("{}.{}", file_name, name));
      }
      if (it != by_name.end()) {
        out.push_back(it->second);
      }
    }
    return out;
  };
  std::vector<std::shared_ptr<Lnast>> ordered;
  std::vector<uint8_t>                seen(units.size(), 0);
  std::function<void(size_t)>         visit = [&](size_t i) {
    if (seen[i] != 0) {
      return;  // emitted, or a call cycle (recursion keeps its name order)
    }
    seen[i] = 1;
    for (const auto c : callees_of(*units[i])) {
      visit(c);
    }
    ordered.push_back(units[i]);
  };
  if (const auto self = by_name.find(std::string(file_name)); self != by_name.end()) {
    seen[self->second] = 1;  // the file scope precedes every lambda
    ordered.push_back(units[self->second]);
  }
  for (size_t i = 0; i < units.size(); ++i) {
    visit(i);
  }
  units = std::move(ordered);
}

std::string shell_quote(std::string_view s) {
  std::string out = "'";
  for (const char c : s) {
    out += c == '\'' ? std::string("'\\''") : std::string(1, c);
  }
  return out + "'";
}

// The string value of `"key":"..."` in one JSONL record, with the simple JSON
// escapes decoded (`\n` becomes a space: the value lands in a one-line message).
std::string json_field(std::string_view rec, std::string_view key) {
  const auto k = std::format("\"{}\":\"", key);
  auto       p = rec.find(k);
  if (p == std::string_view::npos) {
    return {};
  }
  p += k.size();
  std::string out;
  for (; p < rec.size() && rec[p] != '"'; ++p) {
    if (rec[p] == '\\' && p + 1 < rec.size()) {
      ++p;
      out += rec[p] == 'n' || rec[p] == 't' ? ' ' : rec[p];
      continue;
    }
    out += rec[p];
  }
  while (!out.empty() && out.back() == ' ') {
    out.pop_back();
  }
  return out;
}

// Self-check: the Pyrope just written must re-read. Recompile it through the
// same front end (parse + uPass, including the type check; no graph lowering)
// in a CHILD `lhd` -- clean process state, exactly what a user's next
// `lhd compile` sees. Returns the child's first error ("" when it compiles, or
// when no sibling `lhd` binary exists to run, e.g. a unit-test host).
std::string recompile_error(const std::vector<std::string>& files) {
  const auto lhd = livehd::file_utils::get_exe_path() + "/lhd";
  std::error_code ec;
  if (files.empty() || !std::filesystem::exists(lhd, ec)) {
    return {};
  }
  static std::atomic<int> seq{0};
  const auto dir = std::filesystem::temp_directory_path(ec) / std::format("lhd_prp_selfcheck_{}_{}", ::getpid(), seq++);
  std::filesystem::remove_all(dir, ec);
  std::filesystem::create_directories(dir, ec);
  const auto  diag = (dir / "diag.jsonl").string();
  std::string cmd  = shell_quote(lhd) + " compile";
  for (const auto& f : files) {
    cmd += " " + shell_quote(f);
  }
  cmd += " -q --set upass.tolg=false --set lhd.incremental=false --workdir " + shell_quote((dir / "w").string())
         + " --emit " + shell_quote("diagnostics:" + diag) + " > " + shell_quote((dir / "log").string()) + " 2>&1";
  const int   st  = std::system(cmd.c_str());
  std::string err;
  if (st != 0) {
    // An import of a unit the writer did NOT emit is an external (blackbox,
    // `--ignore-unknown-modules`) module: the source compile never had its body
    // either, so the output cannot be blamed for it. An import of an emitted
    // sibling that does not resolve is still a writer gap.
    absl::flat_hash_set<std::string> emitted;
    for (const auto& f : files) {
      emitted.insert(std::filesystem::path(f).stem().string());
    }
    auto only_external_imports = [&](std::string_view rec) {
      if (rec.find("\"code\":\"import-no-progress\"") == std::string_view::npos) {
        return false;
      }
      const auto msg = json_field(rec, "message");
      const auto c   = msg.find(':');
      if (c == std::string::npos) {
        return false;
      }
      bool any = false;
      for (auto q = msg.find('"', c); q != std::string::npos; q = msg.find('"', q + 1)) {
        const auto e = msg.find('"', q + 1);
        if (e == std::string::npos) {
          break;
        }
        const auto unit = msg.substr(q + 1, e - q - 1);
        if (emitted.contains(unit.substr(0, unit.find('.')))) {
          return false;
        }
        any = true;
        q   = e;
      }
      return any;
    };
    bool       blamed = false;
    const auto jsonl  = livehd::file_utils::read_file(diag);
    for (std::string_view rest = jsonl ? std::string_view(*jsonl) : std::string_view{}; !rest.empty();) {
      const auto nl  = rest.find('\n');
      const auto rec = rest.substr(0, nl);
      rest           = nl == std::string_view::npos ? std::string_view{} : rest.substr(nl + 1);
      if (rec.find("\"severity\":\"error\"") == std::string_view::npos) {
        continue;
      }
      if (only_external_imports(rec)) {
        continue;
      }
      blamed = true;
      std::string where = json_field(rec, "file");
      if (const auto lp = rec.find("\"start_line\":"); !where.empty() && lp != std::string_view::npos) {
        where += ":" + std::string(rec.substr(lp + 13, rec.find_first_of(",}", lp + 13) - (lp + 13)));
      }
      err = (where.empty() ? std::string{} : where + ": ") + json_field(rec, "message");
      break;
    }
    // No diagnostics file at all (the child crashed before writing it) is a
    // failure too, never a silent pass.
    if (!blamed && (!jsonl || jsonl->find("\"code\":\"import-no-progress\"") == std::string::npos)) {
      err = "the recompile failed (exit status " + std::to_string(st) + ")";
    }
  }
  std::filesystem::remove_all(dir, ec);
  return err;
}
}  // namespace

void Pass_prp_writer::setup() {
  Eprp_method m1("pass.prp_writer", "emit LNAST as Pyrope 3.0 source files", &Pass_prp_writer::work);
  m1.add_label_optional("odir", "output directory for .prp files", ".");
  m1.add_label_optional("debug", "emit /* TODO */ for unimplemented constructs instead of failing the compile", "false");
  m1.add_label_optional("selfcheck",
                        "recompile the written Pyrope (parse + type check, no graphs) and fail when it does not re-read",
                        "true");
  register_pass(m1);
}

Pass_prp_writer::Pass_prp_writer(const Eprp_var& var) : Pass("pass.prp_writer", var) {}

void Pass_prp_writer::work(Eprp_var& var) {
  Pass_prp_writer p(var);

  if (var.lnasts.empty()) {
    livehd::diag::warn("pass.prp_writer", "no-input", "io").msg("no LNASTs in pipeline — nothing written").emit();
    return;
  }

  // Read odir directly from var (optional labels may not set has_label).
  auto out_dir = std::string(var.get("odir", "."));
  if (out_dir.empty()) {
    out_dir = ".";
  }

  // `prp_writer.debug=true` downgrades an unimplemented construct from a hard
  // error to a `/* TODO */` comment in the output (for inspecting the partial
  // emission).  Default false: any unimplemented construct fails the compile so
  // it cannot silently succeed with a non-reparsable / lossy stub.
  auto debug_opt = std::string(var.get("debug", "false"));
  bool debug_on  = debug_opt == "true" || debug_opt == "1";
  auto self_opt  = std::string(var.get("selfcheck", "true"));
  bool selfcheck = !debug_on && self_opt != "false" && self_opt != "0";

  if (!p.setup_directory(out_dir)) {
    livehd::diag::err("pass.prp_writer", "write-failed", "io").msg("could not create output directory: {}", out_dir).fatal();
    return;
  }

  // Every module emitted in this run, by name.  A unit's file-top `import`s are
  // generated for its instantiated submodules that are themselves emitted here,
  // so the per-file output names its cross-module dependencies.
  std::unordered_set<std::string> emitted_modules;
  for (const auto& ln : var.lnasts) {
    emitted_modules.emplace(std::string(ln->get_top_module_name()));
  }

  // Every emitted module name, keyed by the last `.`-component (the spelling a
  // call site uses).  A func_call to one of these is a real submodule
  // instantiation, so the writer annotates it `Callee::[name=<lhs>]` to keep the
  // bound variable's hierarchical instance name through a re-compile (v2prp name
  // correspondence).  Stateless `comb`s are included: with `upass.inline=false`
  // they stay Sub instances, and the annotation is inert when a comb is inlined.
  std::unordered_set<std::string> instantiated_modules;
  std::unordered_set<std::string> sink_modules;
  for (const auto& ln : var.lnasts) {
    // A file-level import/container unit is not a callable zero-output module.
    // Its basename can equal the tail of a real extracted lambda in that file.
    if (!ln->is_verilog_origin() && ln->get_lambda_kind().empty()) {
      continue;
    }
    std::string_view full = ln->get_top_module_name();
    auto             dot  = full.rfind('.');
    auto             tail = std::string(dot == std::string_view::npos ? full : full.substr(dot + 1));
    instantiated_modules.emplace(tail);
    if (ln->io_meta().outputs.empty()) {
      sink_modules.emplace(std::move(tail));
    }
  }

  // One .prp per SOURCE FILE, not per unit. A Pyrope file contributes a
  // file-level unit named `<file>` plus one `<file>.<entity>` unit per lambda
  // the extractor lifted out; writing each to its own `<name>.prp` split the
  // source in two and made the emission non-idempotent (`import("f.e")` became
  // `import("f.e.e")`, and the next round trip `f.e.e.e`). Group by the file
  // component so `<file>.prp` carries the imports plus every `pub mod` of that
  // file, exactly like the source it came from. A slang-origin unit has no dot
  // and is its own file — the historical 1:1 layout, unchanged.
  // Names a CONCRETE (non-template) unit already owns. A fully-defaulted
  // template whose name is in here has been realized under its own name by an
  // IDENTITY specialization (maybe_specialize_template_call) — emitting both
  // would write two same-named defs into one file, which no longer re-parses.
  // The specialization is the one to keep: it carries the same `<N=default>`
  // signature with the generics already folded.
  absl::flat_hash_set<std::string> concrete_unit_names;
  for (const auto& ln : var.lnasts) {
    if (!ln->is_template()) {
      concrete_unit_names.insert(std::string(ln->get_top_module_name()));
    }
  }

  // A rolled_for prints its retained source loop. Its lifted implementation
  // units have artificial index/activation ports and are not source lambdas.
  std::unordered_set<std::string> loop_implementations;
  for (const auto& ln : var.lnasts) {
    for (const auto& node : ln->depth_preorder()) {
      const auto n = Lnast_nid(node);
      if (!Lnast_ntype::is_rolled_for(ln->get_type(n))) {
        continue;
      }
      size_t index = 0;
      for (auto child : ln->children(n)) {
        if (index++ != lnast_rolled_for::lowering_payload) {
          continue;
        }
        for (auto statement : ln->children(child)) {
          if (!Lnast_ntype::is_func_call(ln->get_type(statement))) {
            continue;
          }
          const auto callee = ln->get_sibling_next(ln->get_first_child(statement));
          loop_implementations.emplace(ln->get_name(callee));
        }
      }
    }
  }

  std::map<std::string, std::vector<std::shared_ptr<Lnast>>> by_file;
  std::unordered_set<std::string>                            unemitted_modules;  // the templates dropped below
  for (const auto& ln : var.lnasts) {
    if (loop_implementations.contains(std::string(ln->get_top_module_name()))) {
      continue;
    }
    // An `ln:` load synthesizes a `<file>.__pub` wrapper (the export table the
    // import machinery reads: `f.[__pub] = "comb"`, `f = "ln:file.f"`). It is
    // loader state, not source -- written out it re-read as assignments to
    // undeclared names.
    if (ln->get_top_module_name().ends_with(".__pub")) {
      continue;
    }
    // A deferred TEMPLATE (`mod f(b)` with an untyped param, `...args`, an
    // unbound `<T>`) is never elaborated — its body still holds the unresolved
    // comptime temps the specialization consumed, so re-emitting it produces a
    // lambda that does not re-parse ("read of undefined variable 't47…_0'").
    // Its concrete twins ARE emitted and every call site names one of them, so
    // dropping the template loses nothing the artifact can use.
    if (ln->is_template()) {
      if (concrete_unit_names.contains(std::string(ln->get_top_module_name()))) {
        continue;  // its identity specialization is emitted under this same name
      }
      // ... EXCEPT a template every one of whose generics carries a DECLARATION
      // DEFAULT. That one is elaboration-COMPLETE as written: the writer renders
      // it with its own `<NAME=default, …>` header and the result re-parses to the
      // same unit. Dropping it silently truncated a v2prp artifact to a ZERO-BYTE
      // .prp on re-emit (exit 0, 0 diagnostics, still listed in manifest.json) --
      // and destroyed the input when the emit dir was the input dir.
      const auto& gens            = ln->get_generics();
      const auto& defs            = ln->get_generic_defaults();
      const bool  fully_defaulted = !gens.empty() && defs.size() >= gens.size()
                                    && std::none_of(defs.begin(), defs.end(), [](const auto& d) { return d.empty(); });
      // An untyped comb can be a compile-time helper called by a retained
      // generic module. It needs no concrete graph: preserve its source body
      // so the caller can specialize/fold it on the next compile.
      const bool  source_comb     = ln->get_lambda_kind() == "comb" && gens.empty();
      if (!fully_defaulted && !source_comb) {
        // Genuinely unelaborated (untyped param, `...args`, an unbound `<T>`).
        // Say so: a skipped unit must never be reported as a successful emit.
        livehd::diag::warn("pass.prp_writer", "template-not-emitted", "io")
            .msg("unit `{}` is an unelaborated template — not written", ln->get_top_module_name())
            .hint("its concrete specializations are emitted instead; give every generic a default to emit the template itself")
            .emit();
        unemitted_modules.emplace(ln->get_top_module_name());
        continue;
      }
    }
    std::string full(ln->get_top_module_name());
    by_file[full.substr(0, full.find('.'))].push_back(ln);
  }

  struct File_job {
    const std::string*                   file_name;
    std::vector<std::shared_ptr<Lnast>>* units;
  };
  struct File_result {
    std::string                                      path;  // the written .prp
    std::string                                      write_error;
    std::vector<std::pair<std::string, std::string>> unimplemented;
    std::vector<std::pair<std::string, std::string>> clock_as_data;
    std::exception_ptr                               error;
  };

  // Clock ports every connected net agrees on (Lnast_prp_writer::clock_data_ports).
  const auto clock_data_ports = Lnast_prp_writer::clock_data_ports(var.lnasts);

  std::vector<File_job> jobs;
  jobs.reserve(by_file.size());
  for (auto& [file_name, units] : by_file) {
    // File-level unit (name == file) first, then the lambdas in name order, so
    // the emission is deterministic and the file scope precedes its users;
    // a lambda follows the same-file lambdas it calls.
    std::sort(units.begin(), units.end(), [](const auto& a, const auto& b) {
      return a->get_top_module_name() < b->get_top_module_name();
    });
    order_callees_first(units, file_name);
    jobs.push_back(File_job{.file_name = &file_name, .units = &units});
  }

  std::vector<File_result> results(jobs.size());
  auto                     emit_file = [&](size_t job_idx) {
    const auto& file_name = *jobs[job_idx].file_name;
    auto&       units     = *jobs[job_idx].units;
    auto&       result    = results[job_idx];

    try {
      TRACE_EVENT("pass", "prp_writer.file", "unit", file_name);
      // The shared long-name policy: a generated parameter specialization
      // can name a module past NAME_MAX, and the emit must not die on it.
      auto fname = std::format("{}/{}.prp", out_dir, livehd::unit_file_stem(file_name));

      // Two phases: collect the (deduped) file-scope import header from every
      // unit, then render the bodies below it.
      std::vector<std::string>                       header;
      std::vector<std::unique_ptr<Lnast_prp_writer>> writers;
      std::vector<std::ostringstream>                bodies(units.size());
      writers.reserve(units.size());
      for (size_t i = 0; i < units.size(); ++i) {
        auto w = std::make_unique<Lnast_prp_writer>(bodies[i], units[i]);
        w->set_debug(debug_on);
        w->set_known_modules(&emitted_modules);
        w->set_unemitted_modules(&unemitted_modules);
        w->set_instantiated_modules(&instantiated_modules);
        w->set_sink_modules(&sink_modules);
        w->set_clock_data_ports(&clock_data_ports);
        w->set_header_sink(&header);
        w->collect_header();
        writers.emplace_back(std::move(w));
      }
      for (size_t i = 0; i < units.size(); ++i) {
        writers[i]->write_all();
      }

      std::ofstream out(fname);
      if (!out.is_open()) {
        result.write_error = fname;
        return;
      }
      for (const auto& l : header) {
        out << l;
      }
      if (!header.empty()) {
        out << "\n";
      }
      bool first = true;
      for (auto& b : bodies) {
        auto text = b.str();
        if (text.empty()) {
          continue;
        }
        if (!first) {
          out << "\n";
        }
        first = false;
        out << text;
        if (text.back() != '\n') {
          out << "\n";
        }
      }
      out.close();
      result.path = fname;

      if (!debug_on) {
        for (size_t i = 0; i < units.size(); ++i) {
          for (const auto& f : writers[i]->clock_as_data()) {
            result.clock_as_data.emplace_back(units[i]->get_top_module_name(), f);
          }
          if (!writers[i]->has_unimplemented()) {
            continue;
          }
          std::string feats;
          for (const auto& f : writers[i]->unimplemented()) {
            feats += feats.empty() ? "" : "; ";
            feats += f;
          }
          result.unimplemented.emplace_back(units[i]->get_top_module_name(), std::move(feats));
        }
      }
    } catch (...) {
      result.error = std::current_exception();
    }
  };

  // Source-file groups share no writer state and target different output
  // paths. Render a bounded batch concurrently; the cap avoids excessive
  // transient analysis maps on machines with very high core counts.
  //
  // livehd::run_workers, not std::thread: render_def_rhs recurses once per
  // folded single-use temp, and a default secondary-thread stack (512 KiB on
  // macOS) held only ~34 such levels at -O0 -- CVA6's unrolled packed-array
  // write chains died there with `Bus error: 10`.
  std::atomic<size_t> next{0};
  const size_t        hw = std::max<size_t>(1, std::thread::hardware_concurrency());
  const size_t        nw = std::min({jobs.size(), hw, size_t{16}});
  livehd::run_workers(nw, [&](size_t) {
    while (true) {
      const size_t i = next.fetch_add(1, std::memory_order_relaxed);
      if (i >= jobs.size()) {
        break;
      }
      emit_file(i);
    }
  });

  // Publish failures in stable file order even though rendering was parallel.
  bool                     failed = false;
  std::vector<std::string> written;
  for (const auto& result : results) {
    failed |= !result.clock_as_data.empty() || !result.unimplemented.empty();
    if (!result.path.empty()) {
      written.push_back(result.path);
    }
    if (result.error) {
      std::rethrow_exception(result.error);
    }
    if (!result.write_error.empty()) {
      livehd::diag::err("pass.prp_writer", "write-failed", "io").msg("could not open output file: {}", result.write_error).fatal();
    }
    // A Verilog clock that is also data (qa.md section 6) has no Pyrope
    // spelling: the written unit types it U1 and still clocks a register with
    // it, which the recompile rejects (clock-bind-not-clock).
    for (const auto& [unit, what] : result.clock_as_data) {
      livehd::diag::err("pass.prp_writer", "prp-writer-clock-as-data", "type")
          .msg("cannot emit Pyrope for '{}': {}", unit, what)
          .hint(
              "a Pyrope Clock is not data and only `Clock(clock_pin=clk, enable=en)` derives one: remove the data read, or "
              "pass --set prp_writer.debug=true to keep the output")
          .emit();
    }
    for (const auto& [unit, feats] : result.unimplemented) {
      livehd::diag::err("pass.prp_writer", "unimplemented", "unsupported")
          .msg("cannot emit Pyrope for '{}': unimplemented construct(s): {}", unit, feats)
          .hint(
              "the .prp was written with /* TODO */ markers; pass --set prp_writer.debug=true to keep the partial "
              "output and let the compile pass")
          .emit();
    }
  }

  // Never exit 0 with Pyrope that does not re-read (suggestions6 1.2: a writer
  // gap -- a reserved word as a name, an integer seed of a Bool, an empty bit
  // range -- used to surface only at the user's next compile). All files are
  // checked together: an emitted file may import its emitted siblings.
  if (selfcheck && !failed) {
    TRACE_EVENT("pass", "prp_writer.selfcheck");
    if (const auto err = recompile_error(written); !err.empty()) {
      livehd::diag::err("pass.prp_writer", "prp-writer-invalid-output", "unsupported")
          .msg("the Pyrope written to {} does not recompile: {}", out_dir, err)
          .hint(
              "a pass.prp_writer gap -- please report it with the source; pass --set prp_writer.selfcheck=false to keep "
              "the output without this check")
          .emit();
    }
  }
}

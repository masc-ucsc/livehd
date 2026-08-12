//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "pass_prp_writer.hpp"

#include <algorithm>
#include <format>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "lnast_prp_writer.hpp"
#include "perf_tracing.hpp"  // TRACE_EVENT — no-op unless built with --define profiling=1

static Pass_plugin sample("pass_prp_writer", Pass_prp_writer::setup);

void Pass_prp_writer::setup() {
  Eprp_method m1("pass.prp_writer", "emit LNAST as Pyrope 3.0 source files", &Pass_prp_writer::work);
  m1.add_label_optional("odir", "output directory for .prp files", ".");
  m1.add_label_optional("debug",
                        "emit /* TODO */ for unimplemented constructs instead of failing the compile",
                        "false");
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
  for (const auto& ln : var.lnasts) {
    std::string_view full = ln->get_top_module_name();
    auto             dot  = full.rfind('.');
    instantiated_modules.emplace(std::string(dot == std::string_view::npos ? full : full.substr(dot + 1)));
  }

  // One .prp per SOURCE FILE, not per unit. A Pyrope file contributes a
  // file-level unit named `<file>` plus one `<file>.<entity>` unit per lambda
  // the extractor lifted out; writing each to its own `<name>.prp` split the
  // source in two and made the emission non-idempotent (`import("f.e")` became
  // `import("f.e.e")`, and the next round trip `f.e.e.e`). Group by the file
  // component so `<file>.prp` carries the imports plus every `pub mod` of that
  // file, exactly like the source it came from. A slang-origin unit has no dot
  // and is its own file — the historical 1:1 layout, unchanged.
  std::map<std::string, std::vector<std::shared_ptr<Lnast>>> by_file;
  for (const auto& ln : var.lnasts) {
    // A deferred TEMPLATE (`mod f(b)` with an untyped param, `...args`, an
    // unbound `<T>`) is never elaborated — its body still holds the unresolved
    // comptime temps the specialization consumed, so re-emitting it produces a
    // lambda that does not re-parse ("read of undefined variable 't47…_0'").
    // Its concrete twins ARE emitted and every call site names one of them, so
    // dropping the template loses nothing the artifact can use.
    if (ln->is_template()) {
      continue;
    }
    std::string full(ln->get_top_module_name());
    by_file[full.substr(0, full.find('.'))].push_back(ln);
  }

  for (auto& [file_name, units] : by_file) {
    // File-level unit (name == file) first, then the lambdas in name order, so
    // the emission is deterministic and the file scope precedes its users.
    std::sort(units.begin(), units.end(), [](const auto& a, const auto& b) {
      return a->get_top_module_name() < b->get_top_module_name();
    });

    TRACE_EVENT("pass", "prp_writer.file", "unit", file_name);
    auto fname = std::format("{}/{}.prp", out_dir, file_name);

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
      w->set_instantiated_modules(&instantiated_modules);
      w->set_header_sink(&header);
      w->collect_header();
      writers.emplace_back(std::move(w));
    }
    for (size_t i = 0; i < units.size(); ++i) {
      writers[i]->write_all();
    }

    std::ofstream out(fname);
    if (!out.is_open()) {
      livehd::diag::err("pass.prp_writer", "write-failed", "io").msg("could not open output file: {}", fname).fatal();
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

    if (!debug_on) {
      for (size_t i = 0; i < units.size(); ++i) {
        if (!writers[i]->has_unimplemented()) {
          continue;
        }
        std::string feats;
        for (const auto& f : writers[i]->unimplemented()) {
          feats += feats.empty() ? "" : "; ";
          feats += f;
        }
        livehd::diag::err("pass.prp_writer", "unimplemented", "unsupported")
            .msg("cannot emit Pyrope for '{}': unimplemented construct(s): {}", units[i]->get_top_module_name(), feats)
            .hint("the .prp was written with /* TODO */ markers; pass --set prp_writer.debug=true to keep the partial "
                  "output and let the compile pass")
            .emit();
      }
    }
  }
}

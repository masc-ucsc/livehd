//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
// Compile pipeline: validation, elaboration, scanning, lowering, and synthesis.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <format>
#include <regex>
#include <set>
#include <sstream>
#include <thread>

#include "absl/container/flat_hash_set.h"
#include "diag.hpp"
#include "graph_library_singleton.hpp"
#include "hhds/tree_edit_distance.hpp"
#include "json_util.hpp"
#include "latch_contract.hpp"
#include "legalize.hpp"
#include "lhd_compile_cache.hpp"
#include "lhd_kernel_internal.hpp"
#include "lhd_prp_import.hpp"
#include "lnast.hpp"
#include "lnast_ntype.hpp"
#include "pass.hpp"
#include "perf_tracing.hpp"
#include "prp2lnast.hpp"
#include "upass_tolg.hpp"
#include "worker_pool.hpp"  // livehd::run_workers (big-stack workers)

namespace lhd {

// ---- emit-kind validation ---------------------------------------------------

void reject_emit_kind(const Options& opts, std::string_view kind, const Lhd_error& err) {
  if (find_slot(opts.emits, kind) != nullptr || find_slot(opts.emit_dirs, kind) != nullptr) {
    throw err;
  }
}

// `lhd pass semdiff …` — the structural diff/match pass, now a `pass`
// subcommand rather than a top-level command. The command-level validations
// that used to key off the former `semdiff` command word (no --emit/--dump
// observables) now key off this.
bool is_pass_semdiff(const Options& opts) {
  return opts.command == "pass" && !opts.files.empty() && opts.files.front() == "semdiff";
}

void validate_emits(const Options& opts) {
  // Kinds with no implementation yet anywhere:
  reject_emit_kind(opts, "graphviz", {"unsupported", "--emit graphviz: is not implemented yet", ""});
  reject_emit_kind(opts, "metadata", {"unsupported", "--emit metadata: is not implemented yet (needs [3c] hashes)", ""});

  // ln:/lg:/lnast-dump:/isabelle:/lean: outputs are directory containers (a Forest dir, a
  // GraphLibrary dir, one file per unit) — directory form only, never a single
  // file. (pyrope: is allowed as a single file for a one-unit design; the
  // multi-unit check lives in emit_pyrope_single_file.)
  for (const auto& e : opts.emits) {
    if (e.kind == "ln" || e.kind == "lg" || e.kind == "lnast-dump" || e.kind == "isabelle" || e.kind == "lean"
        || e.kind == "report") {
      throw Lhd_error{"usage",
                      std::format("--emit {0}:PATH is a directory container; use --emit-dir {0}:DIR/", e.kind),
                      "ln: is a Forest save dir, lg: a GraphLibrary save dir, lnast-dump:/isabelle:/lean: one file per unit, "
                      "report: the synth qor.json + timing.json"};
    }
  }
  // `report:` is the synth flow's QoR/timing sidecar directory; no other
  // command produces one, so anywhere else it is a typo, not a silent no-op.
  if (opts.command != "synth") {
    reject_emit_kind(opts, "report", {"usage", "--emit-dir report: is a `lhd synth` output (qor.json + timing.json)", ""});
  }
  if (opts.command == "synth") {
    // synth emits the MAPPED netlist (lg:/verilog:) and the reports; the
    // LNAST-side observables belong to `lhd compile`.
    for (const char* k : {"ln", "pyrope", "lnast-dump", "isabelle", "lean", "sim"}) {
      reject_emit_kind(opts,
                       k,
                       {"usage",
                        std::format("synth does not emit {}: (its outputs are the mapped netlist as lg:/verilog: and report:)", k),
                        "run `lhd compile` for the pre-synthesis observables"});
    }
  }

  bool has_ln_inputs = false;
  for (const auto& in : opts.in_dirs) {
    has_ln_inputs |= in.kind == "ln";
  }
  // Sources may arrive as positional files or, for the verilog readers, via
  // raw `-- -F filelist.f` args (no positional file then).
  const bool has_sources = !opts.files.empty() || !opts.raw_args.empty();

  // --reader slang emits the current func-style LNAST conventions (io node,
  // declare/store, lambda_kind), so lg:/verilog: emits lower through the same
  // upass+tolg pipeline as pyrope sources (todo/ 2s).
  if (opts.command == "compile" && opts.language == "verilog" && opts.reader != "slang") {
    for (const char* k : {"ln", "lnast-dump"}) {
      reject_emit_kind(opts,
                       k,
                       {"unsupported",
                        "the yosys-* readers elaborate to LGraphs (lg:), not LNAST",
                        "use --emit-dir lg:DIR/, or --reader slang (the direct SV -> LNAST front-end)"});
    }
    reject_emit_kind(opts,
                     "pyrope",
                     {"unsupported",
                      "there is no LGraph -> LNAST decompiler, so the yosys-* readers cannot re-emit pyrope",
                      "--reader slang elaborates SV to LNAST, which can"});
  }
  if (opts.command == "compile" && !has_ln_inputs && !has_sources) {
    // An lg:-only compile (no sources, no ln: inputs) has no LNAST on the pipe;
    // ln/pyrope/lnast-dump outputs need LNAST units, and there is no decompiler.
    for (const char* k : {"ln", "lnast-dump"}) {
      reject_emit_kind(opts, k, {"unsupported", "there is no LGraph -> LNAST decompiler", "ln: outputs need source/ln: inputs"});
    }
    reject_emit_kind(opts,
                     "pyrope",
                     {"unsupported", "there is no LGraph -> LNAST decompiler", "pyrope outputs need source/ln: inputs"});
  }
  if (opts.command == "lec" || is_pass_semdiff(opts)) {
    const std::string_view what = opts.command == "lec" ? "lec" : "pass semdiff";
    for (const char* k : {"lg", "verilog", "ln", "pyrope", "lnast-dump", "isabelle", "lean"}) {
      reject_emit_kind(opts, k, {"usage", std::format("{} has no outputs beyond the result", what), ""});
    }
  }
  if (opts.command == "tool") {
    for (const char* k : {"lg", "verilog", "ln", "pyrope", "lnast-dump", "isabelle", "lean"}) {
      reject_emit_kind(
          opts,
          k,
          {"usage", "tool prints to stdout (redirect with `>`); it has no --emit outputs", "use compile for declared artifacts"});
    }
  }
}

// --dump stage availability mirrors the emit-kind rules above: a dump is an
// observable, so a stage the command/reader cannot produce is an error, not
// a silent no-op.
void validate_dumps(const Options& opts) {
  if (opts.dumps.empty()) {
    return;
  }
  if (opts.command == "lec" || is_pass_semdiff(opts) || opts.command == "scan" || opts.command == "tool") {
    const std::string_view what = is_pass_semdiff(opts) ? "pass semdiff" : std::string_view{opts.command};
    throw Lhd_error{"usage", std::format("{} has no --dump observables", what), "--dump applies to compile"};
  }
  if (opts.language == "verilog" && opts.reader != "slang" && (wants_dump(opts, "parse") || wants_dump(opts, "lnast"))) {
    throw Lhd_error{"unsupported",
                    "the yosys-* readers elaborate to LGraphs, so there is no LNAST to dump",
                    "use --dump lg, or --reader slang (the direct SV -> LNAST front-end)"};
  }
  // compile from IR inputs has no front-end parse, and only ln: inputs carry
  // LNAST: --dump parse needs sources, --dump lnast needs sources or ln:.
  const bool has_sources = !opts.files.empty() || !opts.raw_args.empty();
  bool       has_ln      = false;
  for (const auto& in : opts.in_dirs) {
    has_ln |= in.kind == "ln";
  }
  if (!has_sources && wants_dump(opts, "parse")) {
    throw Lhd_error{"usage", "--dump parse needs source files (an ln:/lg: input is already parsed)", ""};
  }
  if (!has_sources && !has_ln && wants_dump(opts, "lnast")) {
    throw Lhd_error{"usage", "--dump lnast needs source files or ln: inputs (an lg: input carries no LNAST)", ""};
  }
}

// ---- elaborate --------------------------------------------------------------

std::vector<std::shared_ptr<Lnast>> filter_top(const std::vector<std::shared_ptr<Lnast>>& units, const std::string& top) {
  if (top.empty()) {
    return units;
  }
  std::vector<std::string> names;
  names.reserve(units.size());
  for (const auto& ln : units) {
    names.emplace_back(ln->get_top_module_name());
  }
  // Unit names are the internal `file.entity`; accept a bare `--top XXX` when
  // it resolves to the unique XXX entity (resolve_top_name warns on fallback).
  std::string want = top;
  if (const std::string resolved = resolve_top_name(names, top, "lhd.compile"); !resolved.empty()) {
    want = resolved;
  }
  std::vector<std::shared_ptr<Lnast>> out;
  for (size_t i = 0; i < units.size(); ++i) {
    if (names[i] == want) {
      out.push_back(units[i]);
    }
  }
  if (out.empty()) {
    throw Lhd_error{"config",
                    std::format("--top {} not found among elaborated units", top),
                    std::format("available units: {}", join_csv(names))};
  }
  return out;
}

// Synthesize the `<unit>.__pub` wrapper tree: the durable,
// atomically-published pub list of a file unit. Self-describing body shape
// (walkable without evaluation; valid plain LNAST):
//   value leaves: store(ref '<name>[.field]', const <pyrope-text>)
//                 — the folded comptime leaves stamped by uPass_constprop
//   lambdas:      attr_set(ref '<name>', const '__pub', const '<kind>')
//                 store(ref '<name>', const 'ln:<unit>.<name>')
// Returns nullptr when the unit exports nothing.
std::shared_ptr<Lnast> synthesize_pub_wrapper(const std::shared_ptr<Lnast>& ln) {
  const auto& pubs = ln->get_pub_list();
  if (pubs.empty()) {
    return nullptr;
  }
  const std::string unit{ln->get_top_module_name()};
  auto              w            = std::make_shared<Lnast>(unit + ".__pub");
  auto              root         = w->set_root(Lnast_ntype::create_top());
  auto              stmts        = w->add_child(root, Lnast_ntype::create_stmts());
  // Wrapper nodes anchor at their pub declaration — re-mint the pub
  // decl's SourceId (recorded by prp2lnast) into the wrapper's own locator.
  auto              pub_srcid_of = [&](std::string_view name) -> hhds::SourceId {
    for (const auto& p : pubs) {
      if (p.name == name && p.srcid != hhds::SourceId_invalid) {
        return w->source_locator().import_from(ln->source_locator(), p.srcid);
      }
    }
    return hhds::SourceId_invalid;
  };
  for (const auto& [path, text] : ln->get_pub_values()) {
    auto s = w->add_child(stmts, Lnast_ntype::create_store());
    w->set_srcid(s, pub_srcid_of(std::string_view(path).substr(0, path.find('.'))));
    w->add_child(s, Lnast_node::create_ref(path));
    w->add_child(s, Lnast_node::create_const(text));
  }
  for (const auto& p : pubs) {
    if (p.kind == "value") {
      continue;
    }
    const auto wid = pub_srcid_of(p.name);
    auto       a   = w->add_child(stmts, Lnast_ntype::create_attr_set());
    w->set_srcid(a, wid);
    w->add_child(a, Lnast_node::create_ref(p.name));
    w->add_child(a, Lnast_node::create_const("__pub"));
    w->add_child(a, Lnast_node::create_const(std::format("'{}'", p.kind)));
    // A `pub type` needs no special case: it stores the same provenance string
    // as every other non-value pub. Its RANGE is not carried here at all — it
    // is read structurally off the exporting unit's
    // `declare(name, prim_type_int(max,min), 'type')` via Lnast::pub_type_face.
    auto s = w->add_child(stmts, Lnast_ntype::create_store());
    w->set_srcid(s, wid);
    w->add_child(s, Lnast_node::create_ref(p.name));
    w->add_child(s, Lnast_node::create_const(std::format("'ln:{}.{}'", unit, p.name)));
  }
  return w;
}

// The publishable unit set of the given source units: each source
// plus every derived tree named "<src>.<entity>" (func_extract naming —
// extracted lambdas, and the `.__pub` wrapper once synthesized).
std::vector<std::shared_ptr<Lnast>> collect_source_derived(const std::vector<std::shared_ptr<Lnast>>& all,
                                                           const std::vector<std::shared_ptr<Lnast>>& sources) {
  absl::flat_hash_set<std::string> roots;
  for (const auto& ln : sources) {
    roots.emplace(ln->get_top_module_name());
  }
  std::vector<std::shared_ptr<Lnast>> out;
  for (const auto& ln : all) {
    std::string name{ln->get_top_module_name()};
    if (roots.contains(name)) {
      out.push_back(ln);
      continue;
    }
    // "<src>.<entity>" — match against every root (a unit name may itself
    // contain dots, so a single find('.') split would mis-bucket).
    for (const auto& r : roots) {
      if (name.size() > r.size() + 1 && name.compare(0, r.size(), r) == 0 && name[r.size()] == '.') {
        out.push_back(ln);
        break;
      }
    }
  }
  return out;
}

Ir_inputs gather_ir_inputs(const Options& opts, std::string_view cmd) {
  Ir_inputs ir;
  for (const auto& in : opts.in_dirs) {
    if (in.kind == "ln") {
      ir.ln_dirs.push_back(in.path);
    } else {
      throw Lhd_error{"usage", std::format("{} does not accept {}: dir inputs", cmd, in.kind), ""};
    }
  }
  for (const auto& in : opts.ins) {
    if (in.kind == "lg") {
      ir.lg_dirs.push_back(in.path);
    } else if (in.kind == "ln") {
      // `--in ln:DIR` is the SAME input as the positional `ln:DIR` (which
      // route_positional files under in_dirs). Accepting only one of the two
      // spellings was an accident of the two parse paths, not a rule.
      ir.ln_dirs.push_back(in.path);
    } else {
      throw Lhd_error{"usage", std::format("{} does not accept {}: inputs", cmd, in.kind), "IR inputs are ln:DIR or lg:DIR"};
    }
  }

  // An --emit-dir naming one of the IR inputs would overwrite the library it is
  // reading. An emission REPLACES the directory (the declaration file is
  // rewritten in full, and bodies no longer in the artifact are pruned), so the
  // input is destroyed in place while the run still exits 0 — silent data loss.
  // Compare canonically, so `d`, `./d` and `a/../d` are all caught.
  {
    namespace fs   = std::filesystem;
    auto canonical = [](const std::string& p) {
      std::error_code ec;
      const auto      c = fs::weakly_canonical(p, ec);
      return ec ? p : c.lexically_normal().string();
    };
    auto reject_clash = [&](const Typed_path& out, const std::vector<std::string>& ins, std::string_view in_kind) {
      const auto out_c = canonical(out.path);
      for (const auto& in : ins) {
        if (canonical(in) == out_c) {
          throw Lhd_error{"usage",
                          std::format("--emit-dir {}:{} is also a {}: input", out.kind, out.path, in_kind),
                          "an emission replaces the directory, which would destroy the input; emit somewhere else"};
        }
      }
    };
    for (const auto& out : opts.emit_dirs) {
      if (out.kind == "ln" || out.kind == "lg") {
        reject_clash(out, ir.ln_dirs, "ln");
        reject_clash(out, ir.lg_dirs, "lg");
      }
    }
  }
  return ir;
}

// 2i-import S1 — on-disk import discovery. For each source unit's unresolved
// import (a bare logical name, not `lg:`/`ln:`), search the *importing file's
// own directory* for `<name>.prp`, parse it (named by its logical import path),
// and repeat to a fixpoint. So `lhd compile foo/bar.prp` pulls in its siblings
// without the caller listing every dependency, and the LSP (which opens a
// single file) resolves the same way. Resolution is importer-directory relative
// only — never the cwd, never an ancestor crawl. A logical name that resolves
// to two distinct files is an error, never a silent first-hit.
std::vector<std::string> collect_imports(const std::shared_ptr<Lnast>& ln);  // defined below

void discover_imports(Eprp_var& var, Result& res, size_t n_imports, const std::vector<std::string>& seed_files) {
  TRACE_EVENT("pyrope", "discover_imports");
  // The other half of the Pyrope front end, and for the one-seed-file shape
  // (`lhd compile top.prp`, what ../lhdsuite/bench/matrix.sh runs) by far the
  // BIGGER half: the threaded Prp2lnast parse of every transitively imported
  // unit happens here, not in the "inou.prp" run_step that parsed the seeds.
  // Disjoint from it — that run_step's timer closed before this call — so the
  // two never double-count; sum them for the whole parse.
  Phase_timer             phase(res, "inou.prp.imports");
  // Resolution itself lives in lhd_prp_import.hpp and is also used by the
  // incremental closure scanner. Keeping one implementation is a soundness
  // requirement: the cache must never preserve a different closure from the
  // compiler that consumes it.
  import_detail::Resolver resolver;

  absl::flat_hash_map<std::string, std::string> unit_dir;      // unit -> source dir (case-sensitive)
  absl::flat_hash_set<std::string>              parsed_paths;  // abs paths already parsed
  for (const auto& f : seed_files) {
    unit_dir[import_detail::unit_name_of(f)] = import_detail::dir_of(f);
    parsed_paths.insert(import_detail::abspath_of(f));
  }

  // Each unit's imports are examined exactly ONCE, in the round after the unit
  // was parsed (`next_scan` worklist). Re-scanning every loaded unit per round
  // is O(units × rounds) full-LNAST walks — and resolution is deterministic on
  // a static tree, so a re-scan can never produce a different outcome: an
  // import that resolved was parsed on the spot, one that didn't never will
  // (same dir, same stem), and one satisfied by a later round's load needs no
  // action anyway.
  absl::flat_hash_set<std::string> loaded;
  for (const auto& ln : var.lnasts) {
    loaded.insert(std::string(ln->get_top_module_name()));
  }
  absl::flat_hash_map<std::string, std::vector<std::string>> import_cache;
  size_t                                                     next_scan = n_imports;
  while (true) {
    std::map<std::string, std::string>           found;       // logical name -> file to parse
    std::map<std::string, std::set<std::string>> seen_paths;  // logical name -> resolved files

    const size_t scan_end = var.lnasts.size();
    for (size_t i = next_scan; i < scan_end; ++i) {
      const auto& ln  = var.lnasts[i];
      auto        dit = unit_dir.find(std::string(ln->get_top_module_name()));
      if (dit == unit_dir.end()) {
        continue;  // a unit with no known on-disk origin (e.g. a derived tree)
      }
      const std::string dir = dit->second;
      const std::string unit_name(ln->get_top_module_name());
      auto [iit, inserted] = import_cache.try_emplace(unit_name);
      if (inserted) {
        iit->second = collect_imports(ln);
      }
      for (const auto& raw : iit->second) {
        if (raw.starts_with("lg:") || raw.starts_with("ln:")) {
          continue;  // artifact imports resolve elsewhere, not on-disk source
        }
        // A trailing `.entry` after the last '/' selects a pub member, so the
        // file is the stem; otherwise the whole string is the file path.
        const auto names   = import_detail::candidates(raw);
        bool       already = false;
        for (const auto& c : names) {
          if (loaded.contains(c)) {
            already = true;
            break;
          }
        }
        if (already) {
          continue;
        }
        for (const auto& c : names) {
          std::string path = resolver.find(dir, c);
          if (!path.empty()) {
            seen_paths[c].insert(import_detail::abspath_of(path));
            found.try_emplace(c, path);
            break;
          }
        }
      }
    }

    bool ambiguous = false;
    for (const auto& [name, paths] : seen_paths) {
      if (paths.size() <= 1) {
        continue;
      }
      ambiguous = true;
      std::string list;
      for (const auto& p : paths) {
        if (!list.empty()) {
          list += ", ";
        }
        list += p;
      }
      livehd::diag::sink().emit(
          livehd::diag::Diagnostic{.severity = livehd::diag::Severity::error,
                                   .code     = "import-ambiguous",
                                   .category = "name",
                                   .pass     = "lhd.compile",
                                   .message  = std::format("ambiguous import \"{}\": resolves to more than one file", name),
                                   .hint     = std::format("candidates: {}; rename the file or import explicitly", list)});
    }
    if (ambiguous) {
      throw classify_engine_failure("ambiguous import resolution");
    }

    struct Parse_job {
      std::string              name;
      std::string              path;
      std::shared_ptr<Lnast>   lnast;
      std::vector<std::string> imports;
      std::exception_ptr       error;
    };
    std::vector<Parse_job> jobs;
    jobs.reserve(found.size());
    for (const auto& [name, path] : found) {
      if (!parsed_paths.insert(import_detail::abspath_of(path)).second) {
        continue;  // already parsed under some name — don't double-load the file
      }
      jobs.push_back(Parse_job{.name = name, .path = path, .lnast = {}, .imports = {}, .error = {}});
    }

    // Imported source units are independent parse jobs.  Parse a bounded
    // batch in parallel, then publish the results in the deterministic logical
    // name order above.  Sixteen workers avoids oversubscribing large machines
    // and keeps the transient parser/source-buffer footprint small beside the
    // retained LNAST closure.  Diagnostics serialize through diag::Sink while
    // their staged record and source locator remain thread-local.
    // Big-stack workers (livehd::run_workers): Prp2lnast descends the AST
    // recursively, one frame per expression nesting level, and a default
    // secondary-thread stack is 512 KiB on macOS -- a tiny fraction of what
    // the same parse gets on the main thread.
    if (!jobs.empty()) {
      std::atomic<size_t> next{0};
      const size_t        hw = std::max<size_t>(1, std::thread::hardware_concurrency());
      const size_t        nw = std::min({jobs.size(), hw, size_t{16}});
      livehd::run_workers(nw, [&](size_t) {
        while (true) {
          const size_t i = next.fetch_add(1, std::memory_order_relaxed);
          if (i >= jobs.size()) {
            break;
          }
          try {
            Prp2lnast converter(jobs[i].path, jobs[i].name);
            jobs[i].lnast   = converter.get_lnast();
            jobs[i].imports = collect_imports(jobs[i].lnast);
          } catch (...) {
            jobs[i].error = std::current_exception();
          }
        }
      });
      for (const auto& job : jobs) {
        if (job.error) {
          std::rethrow_exception(job.error);
        }
      }
      for (auto& job : jobs) {
        job.lnast->rehome_name_pool(Lnast::active_name_pool());
        var.add(std::move(job.lnast));
        loaded.insert(job.name);
        unit_dir[job.name] = import_detail::dir_of(job.path);
        import_cache.emplace(job.name, std::move(job.imports));
      }
    }
    next_scan = scan_end;
    if (jobs.empty()) {
      break;
    }
  }
  // A discovered file that fails to parse propagates its own diagnostic out of
  // Prp2lnast (caught by the top-level compile handler), so no extra check here.
}

// Pyrope parse phase: load ln: import units (visible to upass/inliner), then
// parse+validate the source files. Returns the number of imported units (the
// source units are var.lnasts[n_imports..]).
size_t pyrope_parse(Options& opts, Result& res, Eprp_var& var, const std::vector<std::string>& ln_import_dirs,
                    bool defer_cache_lnasts = false) {
  check_inputs_exist(opts.files);
  res.inputs = opts.files;

  size_t n_imports = 0;
  for (const auto& d : ln_import_dirs) {
    res.inputs.push_back(d);
    for (auto& ln : load_ln_dir(d)) {
      // A loaded import whose post-upass io_meta was restored is reused as-is:
      // pass.upass skips re-elaborating it (it already holds its final body),
      // resolving callers through the restored interface. Restricted to STATEFUL
      // `mod`/`pipe` entities: those are always Sub instances — never inlined,
      // never comptime-evaluated — so the importer needs only their interface. A
      // `comb` (even when emitted as a Sub) can still be inlined or called at
      // comptime (`cassert(f(3)==4)`), which needs its body, so leave it to
      // re-elaborate. The file/__pub wrappers carry no io_meta. Older ln: dirs
      // without persisted io_meta also fall back to re-elaboration.
      const auto lk = ln->get_lambda_kind();
      if (!ln->io_meta().empty() && (lk == "mod" || lk == "pipe")) {
        ln->set_pre_elaborated(true);
      }
      var.add(ln);
      ++n_imports;
    }
  }

  if (res.compile_cache.enabled) {
    compile_cache_parse_sources(opts, res, var, opts.files, defer_cache_lnasts);
  } else {
    run_step("inou.prp",
             var,
             {
                 {"files", join_csv(opts.files)}
    },
             opts,
             res);
    // 2i-import S1 — transitively pull in imported sibling sources from each
    // importing file's own directory, so a single-file compile needs no
    // dependency list (and the LSP resolves the same way).
    discover_imports(var, res, n_imports, opts.files);
  }
  if (!defer_cache_lnasts && lnastfmt_enabled(opts)) {
    run_step("pass.lnastfmt", var, {}, opts, res);
  }
  if (wants_dump(opts, "parse")) {  // this invocation's source units only (imports are pre-elaborated)
    screen_dump_lnasts(std::vector<std::shared_ptr<Lnast>>(var.lnasts.begin() + static_cast<long>(n_imports), var.lnasts.end()),
                       "post-parse");
  }
  return n_imports;
}

// Frontend half of verilog: source files -> LGraphs in the library at
// `lib_path` (and on var.graphs). Returns the library path used.
std::string verilog_frontend(Options& opts, Result& res, Eprp_var& var) {
  check_inputs_exist(opts.files);
  res.inputs = opts.files;

  const auto* d_out    = find_slot(opts.emit_dirs, "lg");
  std::string lib_path = d_out ? d_out->path : workdir(opts) + "/lgdb";

  // --top rides RAW to yosys: source-level module names may legitimately
  // contain '.' (LiveHD's own cgen emits `file.entity` as an escaped Verilog
  // identifier), so no entity-stripping here — an unknown top fails loudly in
  // yosys itself.
  Eprp_var::Eprp_dict labels{
      {    "path",                                                                       lib_path},
      {     "top",                         opts.top.empty() ? std::string{"-auto-top"} : opts.top},
      {"frontend", opts.reader == "yosys-verilog" ? std::string{"verilog"} : std::string{"slang"}}
  };
  // An empty `files` label is path-validated (and rejected) by Eprp_var::add, so
  // only set it when there are positional sources; with `-- -F filelist.f` the
  // sources ride in slang_flags instead.
  if (!opts.files.empty()) {
    labels["files"] = join_csv(opts.files);
  }
  if (!opts.raw_args.empty()) {
    // Join with '\x1f' (ASCII unit separator) — a comma is lossy for shell
    // argv tokens like +incdir+a,b; inou_yosys_api splits on '\x1f' when seen.
    std::string joined;
    for (const auto& arg : opts.raw_args) {
      if (!joined.empty()) {
        joined += '\x1f';
      }
      joined += arg;
    }
    labels["slang_flags"] = joined;
  }
  merge_sets(opts, "compile.yosys", labels);

  run_step("inou.yosys.tolg", var, labels, opts, res);

  if (var.graphs.empty()) {
    throw Lhd_error{"internal", "verilog elaboration produced no graphs", "check the step log in --workdir"};
  }
  return lib_path;
}

bool emits_need_graphs(const Options& opts);  // fwd (defined below)
bool force_diag_graphs(const Options& opts);  // fwd (defined below)

// --reader slang: the direct inou.slang SV -> LNAST front-end. From here the
// design is LNAST units, so the rest of the flow is the pyrope one (lnastfmt,
// upass, emit-gated tolg). The yosys-* readers above elaborate to LGraphs.
void slang_parse(Options& opts, Result& res, Eprp_var& var) {
  check_inputs_exist(opts.files);
  res.inputs = opts.files;

  Eprp_var::Eprp_dict labels;
  // An empty `files` label is rejected by Eprp_var::add (it validates each
  // comma split path), so only set it when there are positional sources.
  if (!opts.files.empty()) {
    labels["files"] = join_csv(opts.files);
  }
  // Forward --top so inou.slang elaborates only that module's hierarchy (it
  // otherwise auto-tops every uninstantiated module, e.g. a sim/difftest top).
  // Raw: source module names may contain '.' via escaped identifiers (cgen
  // emits `file.entity` that way), so no entity-stripping here.
  if (!opts.top.empty() && opts.top != "-auto-top") {
    labels["top"] = opts.top;
  }
  // Raw `--` args ride straight to the slang driver (e.g. `-F filelist.f` to
  // read a file list). Join with '\x1f' (ASCII unit separator) so shell argv
  // tokens like `+incdir+a,b` survive; inou.slang splits on '\x1f'. With a
  // `-F`/`-f` file list the explicit `files` may be empty.
  if (!opts.raw_args.empty()) {
    std::string joined;
    for (const auto& arg : opts.raw_args) {
      if (!joined.empty()) {
        joined += '\x1f';
      }
      joined += arg;
    }
    labels["slang_flags"] = joined;
  }

  merge_sets(opts, "compile.slang", labels);  // e.g. --set compile.slang.preserve_param_provenance=false
  // Named-constant provenance (symbolic pkg.PARAM refs + pub-comptime-const
  // package units) is ON BY DEFAULT for a pyrope-emitting, no-graphs compile —
  // every pyrope generation runs with the same behavior; the flag remains only
  // as a debug escape (=false folds like before). A graphs flow (tolg) cannot
  // resolve the symbolic refs yet (they would silently nil-wire), so it keeps
  // folding, and an EXPLICIT =true combined with a graphs flow is refused.
  {
    const bool needs_graphs = emits_need_graphs(opts) || force_diag_graphs(opts);
    auto       pit          = labels.find("preserve_param_provenance");
    if (pit == labels.end()) {
      if (!needs_graphs && find_slot(opts.emit_dirs, "pyrope") != nullptr) {
        labels["preserve_param_provenance"] = "true";
      }
    } else if (needs_graphs && (pit->second == "1" || pit->second == "true")) {
      throw Lhd_error{"io",
                      "compile.slang.preserve_param_provenance=true requires a pyrope-only emission",
                      "a graphs flow (lg/verilog/sim emit) folds package params; drop the flag or emit pyrope in a "
                      "separate invocation"};
    }
    // Struct ports are BUNDLES everywhere: reading a Verilog struct yields a
    // bundle, which is LiveHD's representation of one, and both front ends
    // must agree or a comparison between them cannot pair the ports (a trusted
    // sub-instance is keyed by port NAME, so `d` vs `d.f0`/`d.f1` never meet —
    // see lhd/tests/lec_trusted_box_struct_port_test.sh). Flattening is an
    // explicit opt-out; `flat_top_io` is the narrow form that packs ONLY the
    // top interface, for generated-vs-original Verilog equivalence.
    (void)needs_graphs;
  }
  run_step("inou.slang", var, labels, opts, res);
  if (lnastfmt_enabled(opts)) {
    run_step("pass.lnastfmt", var, {}, opts, res);
  }
  if (wants_dump(opts, "parse")) {
    screen_dump_lnasts(var.lnasts, "post-parse");
  }
}

void lower_lnasts(Options& opts, Result& res, Eprp_var& var, const std::string& lib_path, bool need_graphs);  // fwd

// True when any requested observable needs LGraphs (gates the tolg lowering —
// the CLI-level equivalent of pass.upass's tolg:0|1 toggle). A --dump lg is
// an observable like an emit, so it pulls the stage in too.
bool emits_need_graphs(const Options& opts) {
  return find_slot(opts.emit_dirs, "lg") != nullptr || find_slot(opts.emits, "verilog") != nullptr
         || find_slot(opts.emit_dirs, "verilog") != nullptr || find_slot(opts.emit_dirs, "isabelle") != nullptr
         || find_slot(opts.emit_dirs, "lean") != nullptr || find_slot(opts.emit_dirs, "sim") != nullptr || wants_dump(opts, "lg");
}

// True when any requested observable consumes the post-upass LNAST (gates the
// toln materialization — the dual of emits_need_graphs for pass.upass's
// toln:0|1). --dump lnast prints that tree, so it counts; ln.cat/ln.diff
// print/compare it, so the commands count too.
bool emits_need_lnast(const Options& opts) {
  // The single-file `--emit foo.prp` pyrope emit consumes the post-upass forest
  // too (emit_pyrope_single_file runs prp_writer over var.lnasts): without it a
  // warm graph-cache hit would skip upass and emit from raw parse-stage trees.
  return find_slot(opts.emit_dirs, "ln") != nullptr || find_slot(opts.emit_dirs, "pyrope") != nullptr
         || find_slot(opts.emit_dirs, "lnast-dump") != nullptr || find_slot(opts.emits, "pyrope") != nullptr
         || wants_dump(opts, "lnast") || opts.command == "tool";
}

// A *bare* `lhd compile FILE` (no --emit/--emit-dir at all) is the
// maximum-diagnostics action: it should lower all the way to LGraphs (tolg +
// recipe) so the LNAST->LGraph lowering and the graph passes surface every
// warning/error, even though nothing is saved (graph_pipeline_and_emits is a
// no-op without an emit slot: the lg-save guard needs a non-null lg slot and
// every emit_* helper early-returns).
//
// Gated narrowly so it never changes a flow the user explicitly asked for:
//   - When ANY emit/emit-dir is requested, the existing emits_need_graphs()
//     decides — so `--emit-dir pyrope:`/`ln:`/`lnast-dump:` keep their no-tolg
//     (pre-upass / toln:0) re-emit semantics.
//   - The explicit front-end-only tier `--set upass.order=noop` guts the runner
//     so the tree is never rewritten into a tolg-ready shape; lowering it would
//     only produce spurious failures (prplib.py's `:type: parsing`/`lnast`
//     tiers rely on this). Mirrors the user_toln_off scan in lower_lnasts.
bool force_diag_graphs(const Options& opts) {
  if (!opts.emits.empty() || !opts.emit_dirs.empty()) {
    return false;  // a requested emit defines the flow — don't override it
  }
  for (const auto& [key, value] : opts.sets) {
    if (key == "compile.upass.order" && value == "noop") {
      return false;
    }
    // Explicit opt-out: `--set upass.tolg=false` means "front-end + upass only,
    // don't lower to graphs" — the stage-scoped check the prplib comptime/upass
    // test tiers want (a pure comptime program is not synthesizable hardware).
    if (key == "compile.upass.tolg" && (value == "0" || value == "false")) {
      return false;
    }
  }
  return true;
}

// ---- scan (pyrope import/dependency discovery) -------------------------------

// Imports are comptime string literals (see the LiveHD docs),
// so the strings are statically extractable from the parse: every LNAST
// func_call of the form (target, const "import", const "<module>"). The list
// is a conservative over-approximation — an import under `if cond` may be
// constprop-dead, which only elaboration can tell.
std::vector<std::string> collect_imports(const std::shared_ptr<Lnast>& ln) {
  std::vector<std::string> out;
  for (const auto& nid : ln->tree().body().nodes(hhds::Tree_order::preorder)) {
    if (!Lnast_ntype::is_func_call(ln->get_type(nid))) {
      continue;
    }
    auto target = ln->get_first_child(nid);
    if (target.is_invalid()) {
      continue;
    }
    auto fname = ln->get_sibling_next(target);
    if (fname.is_invalid() || ln->get_name(fname) != "import") {
      continue;
    }
    auto mod = ln->get_sibling_next(fname);
    if (mod.is_invalid()) {
      continue;
    }
    std::string text{ln->get_name(mod)};
    // The module argument is the source text of a string literal.
    if (text.size() >= 2 && (text.front() == '"' || text.front() == '\'') && text.back() == text.front()) {
      text = text.substr(1, text.size() - 2);
    }
    if (!text.empty()) {
      out.push_back(text);
    }
  }
  std::sort(out.begin(), out.end());
  out.erase(std::unique(out.begin(), out.end()), out.end());
  return out;
}

std::string json_escape_min(std::string_view text) { return livehd::json_util::escape(text); }

// `lhd scan FILES...` — emit each pyrope file's import strings as written. The payload
// rides the result envelope as the "scan" member, so BUILD generators
// (gazelle-style) and depfile writers can consume one JSON object.
void scan_command(Options& opts, Result& res) {
  setup_diag(opts, "scan");
  if (opts.files.empty()) {
    throw Lhd_error{"usage", "scan requires at least one .prp file", ""};
  }
  if (!opts.emits.empty() || !opts.emit_dirs.empty() || !opts.ins.empty() || !opts.in_dirs.empty()) {
    throw Lhd_error{"usage", "scan takes no --emit/--in slots; its output is the result's \"scan\" member", ""};
  }
  check_inputs_exist(opts.files);
  res.inputs = opts.files;

  std::string payload = "[";
  bool        first   = true;
  for (const auto& f : opts.files) {
    // A directory (e.g. a path with a trailing '/') passes check_inputs_exist
    // but has an empty stem; create_io asserts on an empty unit name. Reject any
    // non-regular-file / empty-stem input with a clean usage error.
    if (!fs::is_regular_file(f)) {
      throw Lhd_error{"usage",
                      std::format("scan expects .prp source files, got '{}'", f),
                      "scan derives a module name from each file's stem"};
    }
    auto base = fs::path(f).stem().string();
    if (base.empty()) {
      throw Lhd_error{"usage", std::format("cannot derive a module name from '{}'", f), ""};
    }

    // Dependency discovery only needs the import strings, so run the prpparse
    // LEXER (Prp2lnast::scan_imports) — NOT a full parse/elaborate. This is the
    // whole point of `lhd scan`: it stays linear/fast (ms even on multi-MB files),
    // where building the LNAST per file took minutes. A malformed file (lex error,
    // e.g. an unterminated string) is reported and skipped with empty imports so
    // one bad file in a multi-file scan does not abort the rest.
    // Lexer-only: a file that LEXES but would not PARSE still yields its imports
    // (scan deliberately does no parse/elaborate). Only a genuine LEX failure
    // (unterminated string/comment) is an error — reported per-file (category
    // syntax) so it exits non-zero with error.class=syntax, while the loop still
    // finishes and res.scan_json is written for every other file (lhd_main turns
    // the error diag into the fail status; write_result emits scan_json regardless).
    std::vector<std::string> imports;
    try {
      imports = Prp2lnast::scan_imports(f);
    } catch (const std::exception& e) {
      livehd::diag::sink().emit(livehd::diag::Diagnostic{.severity = livehd::diag::Severity::error,
                                                         .code     = "scan-lex-error",
                                                         .category = "syntax",
                                                         .pass     = "scan",
                                                         .message  = std::format("scan: {} did not lex: {}", f, e.what())});
    }

    if (!first) {
      payload += ',';
    }
    first        = false;
    payload     += std::format("{{\"file\":\"{}\",\"imports\":[", json_escape_min(f));
    bool ifirst  = true;
    for (const auto& imp : imports) {
      if (!ifirst) {
        payload += ',';
      }
      ifirst   = false;
      payload += std::format("\"{}\"", json_escape_min(imp));
    }
    payload += "]}";
  }
  payload       += "]";
  res.scan_json  = payload;
}

// ---- ln.cat / ln.diff (LNAST debug tools; payload on stdout) -----------------

// One ln.cat/ln.diff input set: .prp or .v/.sv sources and/or ln: Forest dirs.
Ln_inputs classify_ln_inputs(const std::vector<std::string>& tokens, std::string_view cmd) {
  Ln_inputs in;
  for (const auto& t : tokens) {
    if (auto pos = t.find(':'); pos != std::string::npos && pos != 0) {
      auto kind = t.substr(0, pos);
      if (kind == "ln" || kind == "lnast") {
        in.ln_dirs.push_back(t.substr(pos + 1));
        continue;
      }
      if (kind == "lg" || kind == "design" || kind == "lgraph") {
        throw Lhd_error{"usage",
                        std::format("{} reads LNAST, not lg: graph libraries", cmd),
                        "inputs are .prp/.v/.sv sources or ln:DIR forests"};
      }
      // Explicit source schemes (URL-like, like the .prp/.v/.sv shortcuts below):
      // the scheme is authoritative, so strip it and route by language.
      if (kind == "pyrope") {
        in.prp_files.push_back(t.substr(pos + 1));
        continue;
      }
      if (kind == "verilog") {
        in.sv_files.push_back(t.substr(pos + 1));
        continue;
      }
      // any other prefix: treat as a plain path (mirror route_positional)
    }
    std::string_view sv{t};
    if (sv.ends_with(".prp")) {
      in.prp_files.push_back(t);  // pyrope: shortcut
    } else if (sv.ends_with(".v") || sv.ends_with(".sv")) {
      in.sv_files.push_back(t);  // verilog: shortcut
    } else {
      throw Lhd_error{"usage",
                      std::format("{}: cannot classify input '{}'", cmd, t),
                      "inputs are .prp sources, .v/.sv sources (inou.slang), or ln:DIR forests"};
    }
  }
  return in;
}

// Elaborate one input set to LNAST units. Sources run the front-end + upass
// (the post-upass tree, like --dump lnast); ln: dirs load as stored when they
// are the whole set, and join sources as pre-elaborated imports otherwise
// (the elaborate convention — imports are not returned).
std::vector<std::shared_ptr<Lnast>> ln_tool_units(Options& opts, Result& res, const Ln_inputs& in) {
  if (!in.prp_files.empty() && !in.sv_files.empty()) {
    throw Lhd_error{"usage", "cannot mix pyrope and verilog sources in one input set", "split into two ln: elaborations"};
  }

  Eprp_var var;
  size_t   n_imports = 0;
  for (const auto& d : in.ln_dirs) {
    res.inputs.push_back(d);
    for (auto& ln : load_ln_dir(d)) {
      var.add(ln);
      ++n_imports;
    }
  }

  if (in.prp_files.empty() && in.sv_files.empty()) {
    if (in.ln_dirs.empty()) {
      throw Lhd_error{"usage", "no inputs", "pass .prp/.v/.sv sources or ln:DIR forests"};
    }
    return var.lnasts;  // pure ln: load — show/compare what was stored
  }

  const auto& files = in.prp_files.empty() ? in.sv_files : in.prp_files;
  check_inputs_exist(files);
  for (const auto& f : files) {
    res.inputs.push_back(f);
  }
  run_step(in.prp_files.empty() ? "inou.slang" : "inou.prp",
           var,
           {
               {"files", join_csv(files)}
  },
           opts,
           res);
  lower_lnasts(opts, res, var, workdir(opts) + "/lgdb", /*need_graphs=*/false);  // lnastfmt + upass; no tolg

  return std::vector<std::shared_ptr<Lnast>>(var.lnasts.begin() + static_cast<long>(n_imports), var.lnasts.end());
}

std::vector<std::shared_ptr<Lnast>> sorted_by_name(std::vector<std::shared_ptr<Lnast>> units) {
  std::sort(units.begin(), units.end(), [](const auto& a, const auto& b) {
    return a->get_top_module_name() < b->get_top_module_name();
  });
  return units;
}

// Dump text as lines with the trailing " @(...)" attribute suffix stripped:
// loc/fname differ across front-ends and revisions, and the tree-edit cost
// below ignores them too, so the line diff must not flag them. The leading
// module-name line is dropped too (the pair header already names both units,
// and unit names embed the file stem).
std::vector<std::string> dump_lines_no_attrs(const std::shared_ptr<Lnast>& ln) {
  std::ostringstream oss;
  ln->dump(oss);
  std::vector<std::string> lines;
  std::istringstream       iss(oss.str());
  std::string              line;
  bool                     first = true;
  while (std::getline(iss, line)) {
    if (first) {
      first = false;
      continue;
    }
    if (auto pos = line.find(" @("); pos != std::string::npos) {
      line.resize(pos);
    }
    lines.push_back(std::move(line));
  }
  return lines;
}

// Minimal LCS line diff: -/+ lines with 2 lines of kept context per hunk.
void print_line_diff(std::string& out, const std::vector<std::string>& a, const std::vector<std::string>& b, size_t ctx) {
  const size_t n = a.size();
  const size_t m = b.size();
  if (n * m > size_t{16} * 1024 * 1024) {
    out += "  (trees too large for a line diff; see tree-edit-distance below)\n";
    return;
  }
  // lcs[i][j] = LCS length of a[i..] / b[j..]
  std::vector<uint32_t> lcs((n + 1) * (m + 1), 0);
  auto                  at = [&](size_t i, size_t j) -> uint32_t& { return lcs[i * (m + 1) + j]; };
  for (size_t i = n; i-- > 0;) {
    for (size_t j = m; j-- > 0;) {
      at(i, j) = (a[i] == b[j]) ? at(i + 1, j + 1) + 1 : std::max(at(i + 1, j), at(i, j + 1));
    }
  }
  // ops: ' ' keep, '-' only-in-a, '+' only-in-b
  std::vector<std::pair<char, const std::string*>> ops;
  for (size_t i = 0, j = 0; i < n || j < m;) {
    if (i < n && j < m && a[i] == b[j]) {
      ops.emplace_back(' ', &a[i]);
      ++i, ++j;
    } else if (j < m && (i == n || at(i, j + 1) >= at(i + 1, j))) {
      ops.emplace_back('+', &b[j]);
      ++j;
    } else {
      ops.emplace_back('-', &a[i]);
      ++i;
    }
  }
  const size_t      kCtx = ctx;
  std::vector<bool> show(ops.size(), false);
  for (size_t k = 0; k < ops.size(); ++k) {
    if (ops[k].first == ' ') {
      continue;
    }
    size_t lo = k >= kCtx ? k - kCtx : 0;
    size_t hi = std::min(ops.size(), k + kCtx + 1);
    for (size_t x = lo; x < hi; ++x) {
      show[x] = true;
    }
  }
  bool in_gap = false;
  for (size_t k = 0; k < ops.size(); ++k) {
    if (!show[k]) {
      if (!in_gap) {
        out    += "  ...\n";
        in_gap  = true;
      }
      continue;
    }
    in_gap  = false;
    out    += ops[k].first;
    out    += ' ';
    out    += *ops[k].second;
    out    += '\n';
  }
}

// `lhd tool cat ln:…` — the former ln.cat: bare Lnast::dump concatenation of
// every selected unit. `tokens` are the input tokens (the verb stripped).
void tool_cat_ln(Options& opts, Result& res, const std::vector<std::string>& tokens) {
  auto in    = classify_ln_inputs(tokens, "tool cat");
  auto units = sorted_by_name(filter_top(ln_tool_units(opts, res, in), opts.top));
  for (const auto& ln : units) {  // bare Lnast::dump concatenation (true cat)
    std::ostringstream oss;
    ln->dump(oss);
    auto s = oss.str();
    std::fwrite(s.data(), 1, s.size(), stdout);
  }
  std::fflush(stdout);
}

// `lhd tool diff ln:a ln:b` — the former ln.diff: Zhang-Shasha tree-edit
// distance + line diff over the two LNAST forests. `tokens` are the two inputs.
void tool_diff_ln(Options& opts, Result& res, const std::vector<std::string>& tokens) {
  if (tokens.size() != 2) {
    throw Lhd_error{"usage",
                    "tool diff (ln) takes exactly two inputs (each a .prp/.v/.sv source or an ln:DIR forest)",
                    "e.g. `lhd tool diff old.prp new.prp` or `lhd tool diff ln:before/ x.prp`"};
  }
  auto a_units = sorted_by_name(filter_top(ln_tool_units(opts, res, classify_ln_inputs({tokens[0]}, "tool diff")), opts.top));
  auto b_units = sorted_by_name(filter_top(ln_tool_units(opts, res, classify_ln_inputs({tokens[1]}, "tool diff")), opts.top));

  auto names_of = [](const std::vector<std::shared_ptr<Lnast>>& units) {
    std::vector<std::string> names;
    names.reserve(units.size());
    for (const auto& ln : units) {
      names.emplace_back(ln->get_top_module_name());
    }
    return names;
  };
  if (a_units.size() != b_units.size()) {
    throw Lhd_error{"config",
                    std::format("unit count mismatch: {} has [{}], {} has [{}]",
                                tokens[0],
                                join_csv(names_of(a_units)),
                                tokens[1],
                                join_csv(names_of(b_units))),
                    "use --top NAME to select one unit per side"};
  }

  // Pair sorted-by-name positionally (unit names embed the file stem, so
  // exact name matching would never pair two differently-named files of the
  // same design). The header shows which units got compared.
  std::string out;
  double      total = 0.0;
  for (size_t k = 0; k < a_units.size(); ++k) {
    const auto& la  = a_units[k];
    const auto& lb  = b_units[k];
    out            += std::format("//---- tool diff {} vs {}\n", la->get_top_module_name(), lb->get_top_module_name());

    // The hhds tree diff (Zhang-Shasha edit distance) on the live trees:
    // nodes match on (lnast type, name) — loc/fname attrs are ignored.
    auto cost_fn = [&la, &lb](const hhds::Tree::Node_class& x, const hhds::Tree::Node_class& y) -> double {
      if (la->get_type(x) != lb->get_type(y)) {
        return 1.0;
      }
      return la->get_name(x) == lb->get_name(y) ? 0.0 : 1.0;
    };
    auto ted  = hhds::TreeEditDistance::compute(la->tree_ptr(), lb->tree_ptr(), hhds::EditCosts{}, cost_fn);
    total    += ted.distance;

    if (ted.distance == 0.0) {
      out += "identical\n";
    } else {
      print_line_diff(out, dump_lines_no_attrs(la), dump_lines_no_attrs(lb), static_cast<size_t>(opts.tool_context));
    }
    out += std::format("tree-edit-distance: {}\n", ted.distance);
  }
  if (a_units.size() > 1) {
    out += std::format("//---- total tree-edit-distance over {} unit pairs: {}\n", a_units.size(), total);
  }
  std::fwrite(out.data(), 1, out.size(), stdout);
  std::fflush(stdout);
  res.recipe_steps.emplace_back("hhds.tree_edit_distance");
}

// elaborate — build the design database from sources and/or IR inputs:
//   sources (.prp [+ ln: imports] | .v)  -> --emit-dir ln:DIR and/or lg:DIR
//   ln: dirs only (aggregate)            -> one combined ln:/lg: container
//   one lg: dir (pass-through copy)      -> lg:DIR
// ---- synth ------------------------------------------------------------------

// Lower LNAST units: lnastfmt + upass, then (only when `need_graphs`) the
// terminal LNAST->LGraph sub-pass into the library at lib_path — the
// CLI-level tolg:0|1 gate, derived from the requested emits.
void lower_lnasts(Options& opts, Result& res, Eprp_var& var, const std::string& lib_path, bool need_graphs) {
  // Publish parser-streamed lambda siblings before lnastfmt and, critically,
  // before the import-defer loop snapshots pristine trees. If pass.upass were
  // the first owner to take them, a blocked import retry would erase the
  // derived function while the source wrapper's transient handoff had already
  // been consumed. Making them ordinary queue entries here gives every
  // streamed function its own pristine retry body.
  const auto source_count = var.lnasts.size();
  for (std::size_t i = 0; i < source_count; ++i) {
    for (auto& fn : var.lnasts[i]->take_streamed_lambdas()) {
      var.add(std::move(fn));
    }
  }

  const auto redo_begin     = std::chrono::steady_clock::now();
  auto       account_redone = [&] {
    if (res.compile_cache.enabled) {
      const std::chrono::duration<double, std::milli> dt  = std::chrono::steady_clock::now() - redo_begin;
      res.compile_cache.redone_ms                        += dt.count();
    }
  };
  if (lnastfmt_enabled(opts)) {
    run_step("pass.lnastfmt", var, {}, opts, res);
  }

  // The verifier runs by default: it prints `cputs` and, crucially, turns a
  // comptime-false `cassert` into a compile error during elaborate/synth. No
  // verifier_pass / verifier_fail expectation is passed here, so it runs in
  // quiet mode (no count tally) — every cassert that resolves must hold, an
  // unknown / deferred cassert is left for later. A user can still silence it
  // with `--set upass.verifier=false`.
  Eprp_var::Eprp_dict up{
      {"constprop",    "1"},
      { "verifier", "true"}
  };
  // comb inlining is OFF by default (a directly-named, fully-defined comb is
  // emitted as a Sub module instance, preserving the comb boundary for
  // debug/optimization); only the O2 recipe flattens by inlining. Bare `compile`
  // and O0/O1 keep the boundary. A user `--set compile.upass.inline=…` overrides
  // (merge_sets below runs after this).
  up["inline"] = (opts.recipe == "O2") ? "true" : "false";
  // Derived toln gate (the dual of the emit-derived tolg gate): when neither
  // the lnast.tolg stage below (need_graphs) nor any post-upass LNAST emit
  // (ln:/pyrope:/lnast-dump:) consumes the rewritten tree, skip materializing
  // it — pass.upass toln:0 drops the whole staging build, the post-walk DCE,
  // and the coalescer; every pass still dispatches, so diagnostics are
  // unchanged. An explicit `--set upass.toln=…` (merged below) overrides.
  if (!need_graphs && !emits_need_lnast(opts)) {
    up["toln"] = "0";
  }
  // lg-only flows: the rewritten LNAST is consumed ONLY by lnast.tolg and then
  // dropped, so the post-walk DCE marks dead statements for tolg to skip
  // instead of rebuilding a cleaned staging tree (dce:mark — the rebuild is
  // the dominant DCE cost on generated-code-scale units). Any LNAST-keeping
  // flow (pyrope:/ln:/lnast dumps/tool) keeps the rebuild.
  if (need_graphs && !emits_need_lnast(opts)) {
    up["dce"] = "mark";
  }
  // Named-constant provenance, the Pyrope counterpart of the inou.slang flag of
  // the same name (see run_slang): a pyrope-emitting, no-graphs compile keeps a
  // folded `pkg.PARAM` symbolic so the re-emitted source reads
  // `cmd == vpu_defs_pkg.VPU_TRANS_SIN_P2`, not `cmd == 0x78`. tolg cannot wire
  // a symbolic ref, so any graphs flow keeps folding.
  if (!need_graphs && find_slot(opts.emit_dirs, "pyrope") != nullptr) {
    up["preserve_param_provenance"] = "true";
  }
  // Loop representation: `compile.unroll` (kernel gate, default false) is the
  // one user-facing switch; pass.upass's `roll` label is its inverse.
  up["roll"] = compile_unroll_requested(opts) ? "false" : "true";
  merge_sets(opts, "compile.upass", up);

  // A user `--set upass.toln=0` keeps each tree's original (post-lnastfmt,
  // pre-upass-rewrite) body in var.lnasts instead of the rewritten one, so
  // pass.prp_writer re-emits Pyrope straight from the inou.slang / inou.prp
  // LNAST. That pyrope emit is the one supported reason to force toln:0 from
  // the CLI; with no pyrope emit nothing downstream consumes the un-rewritten
  // tree, so it is a diagnostics/debug or unexpected flow — flag it.
  bool user_toln_off = false;
  for (const auto& [key, value] : opts.sets) {
    if (key == "compile.upass.toln" && (value == "0" || value == "false")) {
      user_toln_off = true;
    }
  }
  if (user_toln_off && find_slot(opts.emits, "pyrope") == nullptr && find_slot(opts.emit_dirs, "pyrope") == nullptr) {
    livehd::diag::warn("lhd.elaborate", "toln-debug-flow", "io")
        .msg("--set upass.toln=0 keeps the original pre-upass LNAST, but no pyrope emit (pass.prp_writer) consumes it")
        .hint(
            "toln:0 is meant for `--emit-dir pyrope:DIR/` (re-emit source from the inou.slang/inou.prp LNAST); "
            "without a pyrope emit this is a debugging or unexpected flow")
        .emit();
  }

  // Iterate until converged. Units may import each other in any
  // order (no topological pre-ordering; liveness needs constprop), so:
  // each round runs pass.upass over everything; a file whose walk hit an
  // unresolved LIVE import retries WHOLESALE next round from its pristine
  // post-lnastfmt body — by then the exporter has published (its folded pub
  // values ride its Lnast). A round with no progress fails, listing every
  // blocked file with its unresolved import strings (covers both true
  // cycles and missing units). Import-free invocations take the single-pass
  // fast path (no clones, no defer mode).
  // NOTE: this test must stay origin-AGNOSTIC. Pyrope re-emitted from Verilog
  // carries `::[hdl]` (is_verilog_origin) AND a real file-scope
  // `const X = import("X.X")` header, and it needs the retry loop exactly like
  // hand-written Pyrope does — skipping such trees here made `tmod` fail to
  // resolve `tpkg` on recompile (//lhd/tests:slang_param_provenance_test).
  // Cheapening the `pristine` snapshot for large generated RTL has to happen
  // inside the clone, not by disarming the retry.
  bool imports_present = false;
  for (const auto& ln : var.lnasts) {
    if (!collect_imports(ln).empty()) {
      imports_present = true;
      break;
    }
  }
  if (!imports_present) {
    run_step("pass.upass", var, up, opts, res);
  } else {
    up["import_defer"] = "1";
    // Pristine (post-lnastfmt, pre-upass) bodies for the whole-file retry.
    absl::flat_hash_map<std::string, std::shared_ptr<hhds::Tree>> pristine;
    for (const auto& ln : var.lnasts) {
      pristine.emplace(std::string(ln->get_top_module_name()), ln->tree_ptr()->clone());
    }
    std::map<std::string, std::set<std::string>> prev_blocked;
    while (true) {
      run_step("pass.upass", var, up, opts, res);
      if (var.unresolved_imports.empty()) {
        break;
      }
      // Map each blocked unit (possibly an extracted body `file.fn`) to its
      // retryable file-level unit — the longest pristine name prefixing it.
      std::map<std::string, std::set<std::string>> blocked;
      for (const auto& [unit, text] : var.unresolved_imports) {
        std::string file;
        for (const auto& [pname, _] : pristine) {
          const bool prefix
              = unit == pname
                || (unit.size() > pname.size() + 1 && unit.compare(0, pname.size(), pname) == 0 && unit[pname.size()] == '.');
          if (prefix && pname.size() > file.size()) {
            file = pname;
          }
        }
        blocked[file.empty() ? unit : file].insert(text);
      }
      if (blocked == prev_blocked) {
        std::vector<std::string> available_units;
        available_units.reserve(pristine.size());
        for (const auto& [pname, _] : pristine) {
          available_units.push_back(pname);
        }
        std::sort(available_units.begin(), available_units.end());
        const auto units_avail     = join_csv(available_units);
        const auto available_count = available_units.size();
        for (const auto& [file, texts] : blocked) {
          std::string list;
          for (const auto& t : texts) {
            if (!list.empty()) {
              list += ", ";
            }
            list += '"' + t + '"';
          }
          livehd::diag::sink().emit(livehd::diag::Diagnostic{
              .severity = livehd::diag::Severity::error,
              .code     = "import-no-progress",
              .category = "name",
              .pass     = "lhd.elaborate",
              .message  = std::format("unit `{}` is blocked on unresolved import(s): {}", file, list),
              .hint     = std::format("an import cycle or a missing unit; {} unit{} available (use --diag-fmt "
                                      "json for the full list)",
                                  available_count,
                                  available_count == 1 ? " is" : "s are"),
              .attrs    = {{"available_unit_count", std::to_string(available_count)}, {"available_units", units_avail}}
          });
        }
        throw classify_engine_failure("import resolution made no progress");
      }
      prev_blocked = std::move(blocked);
      // Whole-file retry: restore each blocked file's pristine body and drop
      // its round-derived trees so re-extraction doesn't duplicate units.
      for (const auto& [file, _] : prev_blocked) {
        auto pit = pristine.find(file);
        if (pit == pristine.end()) {
          continue;
        }
        for (const auto& ln : var.lnasts) {
          if (ln->get_top_module_name() == file) {
            ln->replace_body(pit->second->clone());
            ln->set_pub_values({});
            break;
          }
        }
        std::erase_if(var.lnasts, [&](const std::shared_ptr<Lnast>& ln) {
          std::string name{ln->get_top_module_name()};
          return name.size() > file.size() + 1 && name.compare(0, file.size(), file) == 0 && name[file.size()] == '.'
                 && !pristine.contains(name);  // derived this invocation, not a loaded unit
        });
      }
      // Everything still standing walked to completion with every import
      // resolved, so its body is FINAL. Freeze it: the next round exists only
      // for the blocked files above, and re-walking a converged unit corrupts
      // it — SSA demotes its private `___ssa_N` names into the `__wN`
      // namespace the generated source already uses (two variables silently
      // become one, then constprop folds live logic to a constant and DCE
      // deletes the real cone), and uPass_pipe doubles its output flop. The
      // blocked files were just restored to their pristine bodies, so they are
      // NOT frozen and re-elaborate from scratch next round.
      for (const auto& ln : var.lnasts) {
        if (!prev_blocked.contains(std::string(ln->get_top_module_name()))) {
          ln->set_upass_converged(true);
        }
      }
    }
  }

  if (wants_dump(opts, "lnast")) {
    screen_dump_lnasts(var.lnasts, "post-upass");
  }

  if (!need_graphs) {
    account_redone();
    return;  // no lg/verilog emit requested -> skip the tolg lowering
  }
  {
    Phase_timer   phase(res, "lnast.tolg");
    Stdout_to_log redirect(next_log_path(opts, "lnast.tolg"));
    // 2f-lg: reject two units pinned to the same artifact name (lg="…")
    // before any GraphIO is created.
    uPass_tolg::detect_lg_collisions(var.lnasts);
    // Two-phase: register every module's GraphIO first so call
    // sites can bind callee GraphIOs (Sub instances) regardless of order.
    for (const auto& ln : var.lnasts) {
      if (ln->is_graph_restored()) {
        continue;  // graph body restored from the compile cache
      }
      uPass_tolg::register_io(ln, lib_path, var.lnasts);
    }
    // The reset_style elaboration flag rides the upass set
    // (`--set upass.reset_style=async`); tolg is its only consumer.
    std::string reset_style = "sync";
    if (auto it = up.find("reset_style"); it != up.end() && !it->second.empty()) {
      reset_style = it->second;
    }
    std::vector<std::shared_ptr<hhds::Graph>> lowered;
    for (const auto& ln : var.lnasts) {
      if (ln->is_graph_restored()) {
        continue;  // final post-formal graph already lives in lib_path/var
      }
      auto g = uPass_tolg::run(ln, lib_path, var.lnasts, reset_style);
      if (g) {
        var.add(g);
        lowered.push_back(g);
      }
    }
    uPass_tolg::gate_activation_clocks(lowered);
  }
  res.recipe_steps.emplace_back("lnast.tolg");
  if (livehd::diag::sink().has_errors()) {
    throw classify_engine_failure("lnast.tolg reported errors");
  }
  account_redone();
}

// Graph half shared by synth and compile: recipe passes + typed emits.
// `lib_path` is the library the graphs in `var` live in ("" when there are
// no graphs, e.g. a pure-LNAST run).
void graph_pipeline_and_emits(Options& opts, Result& res, Eprp_var& var, const std::string& lib_path, bool already_final) {
  check_known_set_passes(opts);
  const auto                       redo_begin = std::chrono::steady_clock::now();
  Eprp_var                         fresh;
  Eprp_var*                        active = &var;
  absl::flat_hash_set<std::string> restored(res.compile_cache_restored_graphs.begin(), res.compile_cache_restored_graphs.end());
  if (!restored.empty()) {
    for (const auto& graph : var.graphs) {
      if (graph && !restored.contains(std::string(graph->get_name()))) {
        fresh.add(graph);
      }
    }
    active = &fresh;
  }
  if (!already_final) {
    for (const auto& [set_name, method] : recipe_graph_passes(opts, "O1")) {
      if (active->graphs.empty()) {
        break;  // nothing to optimize (validated below if an emit needs graphs)
      }
      Eprp_var::Eprp_dict labels;
      merge_sets(opts, set_name, labels);
      run_step(method, *active, labels, opts, res);
    }

    // THE LATCH CONTRACT CHECK (todo/livehd/2f-latch M3). Modelling a latch as a
    // flop-with-enable that commits at its window's closing edge is an
    // abstraction with a PRECONDITION — no time borrowing — and a precondition
    // must be CHECKED, never assumed. A restored final graph already passed the
    // check under the same code salt and is not checked a second time.
    for (const auto& gref : active->graphs) {
      if (!livehd::latch_contract::check(gref.get())) {
        throw classify_engine_failure("latch contract violation");
      }
    }

    // pass.formal — single-design property checks (assert / assume / Hotmux
    // one-hotness) on the cvc5 prover. It stamps the final cached graphs.
    if (!active->graphs.empty()) {
      Eprp_var::Eprp_dict labels;
      merge_sets(opts, "compile.formal", labels);
      Eprp_var::Eprp_dict formal_labels;
      merge_sets(opts, "formal", formal_labels);
      if (auto it = formal_labels.find("assume_check"); it != formal_labels.end()) {
        labels["assume_check"] = it->second;
      }
      const std::string recipe = opts.recipe.empty() ? "O1" : opts.recipe;
      const std::string mode   = labels.count("mode") ? labels["mode"] : (recipe == "O0" ? "none" : "fast");
      if (mode != "none" && mode != "fast" && mode != "normal") {
        throw Lhd_error{"usage", std::format("--set compile.formal.mode must be none|fast|normal, got '{}'", mode), ""};
      }
      if (mode != "none") {
        labels["mode"] = mode;
        if (!opts.top.empty() && opts.top != "-auto-top" && !labels.count("top")) {
          labels["top"] = opts.top;
        }
        if (active != &var) {
          Eprp_var formal_view;
          for (const auto& graph : var.graphs) {
            formal_view.add(graph);
          }
          std::vector<std::string> active_names;
          active_names.reserve(active->graphs.size());
          for (const auto& graph : active->graphs) {
            if (graph) {
              active_names.emplace_back(graph->get_name());
            }
          }
          labels["active"] = join_csv(active_names);
          run_step("pass.formal", formal_view, labels, opts, res);
        } else {
          run_step("pass.formal", *active, labels, opts, res);
        }
      }
    }

    // pass.legalize -- the one sanctioned structural transform between the
    // optimization passes and every consumer, and the point the design is
    // FROZEN. NOT a recipe step on purpose: `recipe:O0` has no steps, so a
    // recipe-gated legalize would skip exactly the level that needs it most.
    //
    // It runs LAST, after pass.formal, so that every pass that may still
    // reshape the graph has run before the structure is recorded; formal itself
    // only annotates (`proven` / `runtime_check`). Everything downstream of
    // here -- cgen, the emits, and the LEC / synthesis / simulation consumers
    // -- reads a graph nobody may reshape.
    //
    // The split may create defs (loop halves) and delete defs (orphaned loop
    // bodies, stale halves); both var.graphs and the active subset are updated
    // so the emits, the cache and the freeze check see the design as it is.
    if (!active->graphs.empty()) {
      Phase_timer                               phase(res, "pass.legalize");
      std::vector<std::shared_ptr<hhds::Graph>> design(active->graphs.begin(), active->graphs.end());
      const auto                                legalized = livehd::legalize::legalize_design(design, verify_frozen_enabled(opts));
      if (!legalized.removed.empty() || !legalized.added.empty()) {
        absl::flat_hash_set<const hhds::Graph*> gone;
        for (const auto& g : legalized.removed) {
          gone.insert(g.get());
        }
        for (auto* v : {&var, active}) {
          std::erase_if(v->graphs, [&](const std::shared_ptr<hhds::Graph>& g) { return gone.contains(g.get()); });
          if (v == active && active == &var) {
            break;
          }
        }
        for (const auto& g : legalized.added) {
          var.add(g);
          if (active != &var) {
            active->add(g);
          }
        }
      }
    }
  }

  if (res.compile_cache.enabled && !active->graphs.empty()) {
    const std::chrono::duration<double, std::milli> dt  = std::chrono::steady_clock::now() - redo_begin;
    res.compile_cache.redone_ms                        += dt.count();
  }
  // Overlaying validated clean bodies is EXACTNESS, not correctness: the live
  // pipeline above already produced a complete, checked result for every graph.
  // Only a failure that left the library mid-transplant may fail the compile.
  const auto overlay_status = compile_cache_overlay_clean_graphs(res, var, lib_path);
  if (overlay_status == Overlay_status::damaged) {
    throw Lhd_error{"config",
                    "the compile cache left the graph library incomplete while overlaying validated clean bodies",
                    "remove the damaged compile scope or rerun with --set lhd.incremental=false"};
  }
  // Closes the window the compile cache carries (Result::compile_cache_diag_mark).
  // Everything below is EMITS, which a warm restore re-runs — and whose targets
  // are not part of the cache key — so their records must not ride along.
  res.compile_cache_diag_end = livehd::diag::sink().records().size();
  if (overlay_status == Overlay_status::declined) {
    // Deliberately AFTER compile_cache_diag_end: this record describes THIS
    // run's cache scope, not the design. Inside the window it would be stored
    // into the generation and replayed by every later warm restore of a scope
    // that is no longer damaged.
    livehd::diag::warn("lhd.compile", "cache-overlay-declined", "io")
        .msg(
            "compile cache: the validated clean graph bodies could not be reused; unchanged modules are reported as "
            "re-derived rather than cache-exact")
        .hint(
            "the compile result is complete and correct; the scope is republished by this run, or pass --set "
            "lhd.incremental=false to skip the cache entirely")
        .emit();
  }

  if (wants_dump(opts, "lg")) {
    screen_dump_graphs(var, "post-recipe");
  }

  if (const auto* lg_out = find_slot(opts.emit_dirs, "lg"); lg_out != nullptr) {
    if (lib_path.empty() || var.graphs.empty()) {
      // A type/constant-only unit (a `_pkg` file) legitimately has no lgraph.
      // Emitting an EMPTY library is the honest outcome — the request was
      // satisfied, there was simply nothing to put in it — so warn and exit 0
      // rather than fail. Failing here forced every batch caller that emits
      // `ln:`+`lg:` per file to string-match this message to tell a real error
      // from an empty package. It is the LINK step that should notice a module
      // is missing, not the per-file emit.
      livehd::diag::warn("lhd.compile", "empty-lg-emit", "io")
          .msg("--emit-dir lg: produced no graphs — the input has no synthesizable modules")
          .hint("expected for a type/constant-only unit; the library is written empty")
          .emit();
      if (!lib_path.empty()) {
        TRACE_EVENT("pass", "lg.save");
        Phase_timer phase(res, "lg.save");
        livehd::Hhds_graph_library::save(lib_path);
      }
      res.outputs.push_back(lg_out->path);
    } else {
      // By construction lib_path == the lg output dir whenever one was
      // declared (tolg/copy targeted it), so saving the library is the emit.
      TRACE_EVENT("pass", "lg.save");
      {
        Phase_timer phase(res, "lg.save");
        livehd::Hhds_graph_library::save(lib_path);
      }
      res.outputs.push_back(lg_out->path);
    }
  }

  emit_isabelle_outputs(opts, res, var);
  emit_lean_outputs(opts, res, var);
  emit_verilog_outputs(opts, res, var);
  emit_sim_outputs(opts, res, var);  // --emit-dir sim:DIR/ (inou.cgen.sim)
  // ln: emit is handled per-path by the caller (source publish vs plain forest),
  // so it is NOT done here.
  emit_pyrope_outputs(opts, res, var);             // --emit-dir pyrope:DIR/ (one .prp per unit)
  emit_pyrope_single_file(opts, res, var);         // --emit foo.prp (single-unit design)
  emit_lnast_dump_outputs(var.lnasts, opts, res);  // post-upass textual dump (debug/test observable)

  // Did anything reshape the design after pass.legalize froze it? Emits are
  // supposed to READ the graph; a structural change here means a consumer is
  // still rewriting the artifact that LEC, synthesis and simulation all share.
  // A graph legalize did not freeze in THIS run (a cache overlay swapped the
  // object, a restored body) is not claimed. Gated like the freeze itself.
  if (verify_frozen_enabled(opts)) {
    std::vector<std::shared_ptr<hhds::Graph>> design(var.graphs.begin(), var.graphs.end());
    (void)livehd::legalize::verify_design_frozen(design, "compile emits");
  }
}

// First-elaboration `ln:` publish from pyrope sources: the source-derived
// units (each source plus its extracted lambdas — the `ln:<unit>.<entity>` url
// targets) plus a synthesized `<unit>.__pub` wrapper per pub-exporting file.
// Imports are pre-elaborated and keep their own `ln:` dir, so they are
// excluded (var.lnasts[n_imports..] is this invocation's source set).
void publish_source_ln(Options& opts, Result& res, Eprp_var& var, size_t n_imports, const std::string& dir) {
  std::vector<std::shared_ptr<Lnast>> source_units(var.lnasts.begin() + static_cast<long>(n_imports), var.lnasts.end());
  auto                                publish = collect_source_derived(var.lnasts, filter_top(source_units, opts.top));
  // Publication happens at completion (never a partial pub list): the upass
  // above either finished every unit or threw.
  std::vector<std::shared_ptr<Lnast>> wrappers;
  for (const auto& ln : publish) {
    if (ln->get_lambda_kind().empty() && !ln->get_top_module_name().ends_with(".__pub")) {
      if (auto w = synthesize_pub_wrapper(ln)) {
        wrappers.push_back(std::move(w));
      }
    }
  }
  publish.insert(publish.end(), wrappers.begin(), wrappers.end());
  save_ln_dir(opts, res, publish, dir);
}

// IR-only compile: ln:DIR forests and/or lg:DIR libraries, no sources. ln:
// dirs are re-lowered (lnastfmt + upass + tolg); a single lg: dir is loaded.
// Then the shared graph pipeline (recipe + emits) runs. (The ln: + lg: linker
// has its own path — see compile_link_ir.)
void compile_ir(Options& opts, Result& res, const Ir_inputs& ir) {
  if (ir.lg_dirs.size() > 1) {
    throw Lhd_error{"unsupported",
                    "multiple lg: inputs are not supported yet (gids are library-scoped)",
                    "aggregate upstream from ln: units, or merge into one library"};
  }

  const auto* lg_out = find_slot(opts.emit_dirs, "lg");
  const auto* ln_out = find_slot(opts.emit_dirs, "ln");
  Eprp_var    var;
  std::string lib_path;

  if (!ir.ln_dirs.empty()) {
    setup_diag(opts, "compile.ln");
    for (const auto& d : ir.ln_dirs) {
      res.inputs.push_back(d);
      for (auto& ln : load_ln_dir(d)) {
        var.add(ln);
      }
    }
    lib_path = lg_out ? lg_out->path : workdir(opts) + "/lgdb";
    // Force the tolg lowering for diagnostics even with no lg: emit (bare
    // `lhd compile ln:DIR`) — see force_diag_graphs.
    lower_lnasts(opts, res, var, lib_path, emits_need_graphs(opts) || force_diag_graphs(opts));
    if (ln_out != nullptr) {  // re-publish the post-upass forest (the loaded units are the design)
      emit_ln_outputs(var.lnasts, opts, res);
    }
  } else {
    setup_diag(opts, "compile.lg");
    const auto& lg_in = ir.lg_dirs.front();
    if (!fs::is_directory(lg_in)) {
      throw Lhd_error{"missing_file", std::format("lg: input not found: {}", lg_in), "an lg: input is a GraphLibrary directory"};
    }
    check_ir_body_magic(lg_in, "graph_", kHhdsGraphBodyMagic, "lg:");
    res.inputs.push_back(lg_in);
    lib_path = lg_in;
    if (lg_out != nullptr && fs::weakly_canonical(lg_out->path) != fs::weakly_canonical(lg_in)) {
      // Saving only rewrites dirty graphs, so emit-to-a-new-path starts from
      // a filesystem copy of the input library and mutates that copy. Purge
      // the destination first: fs::copy never removes destination-only
      // entries, so a reused output dir would keep stale graph bodies.
      std::error_code ec;
      fs::remove_all(lg_out->path, ec);
      ensure_dir(lg_out->path);
      fs::copy(lg_in, lg_out->path, fs::copy_options::recursive | fs::copy_options::overwrite_existing);
      lib_path = lg_out->path;
    }
    auto& lib = livehd::Hhds_graph_library::instance(lib_path);
    for (const hhds::Gid id : lib.all_gids()) {  // gids are sparse name-hashes now
      auto g = lib.get_graph(id);
      if (g) {
        var.add(g);
      }
    }
    if (var.graphs.empty()) {
      throw Lhd_error{"config", std::format("lg: input {} holds no graphs", lg_in), ""};
    }
  }

  graph_pipeline_and_emits(opts, res, var, lib_path);
}

// Linker: merge lg: libraries + lower the ln: source units that reference them
// (`import("lg:foo")` -> a black-box Sub) into one new lg: library, then run
// the shared graph pipeline (recipe + emits).
void compile_link_ir(Options& opts, Result& res, const Ir_inputs& ir) {
  setup_diag(opts, "compile.link");
  const auto* lg_out = find_slot(opts.emit_dirs, "lg");
  if (lg_out == nullptr) {
    throw Lhd_error{"usage", "linking ln: + lg: inputs needs --emit-dir lg:DIR/", ""};
  }
  const std::string lib_path = lg_out->path;
  // Absorb each lg: library into the output library. Name-hash gids make a
  // shared graph name keep the same gid across libraries, so load_merge is
  // conflict-free for matching names (and dedups them).
  auto&             lib      = livehd::Hhds_graph_library::instance(lib_path);
  for (const auto& d : ir.lg_dirs) {
    check_lg_input_dir(d);
    res.inputs.push_back(d);
    lib.load_merge(d);
  }
  // Lower the ln: source units into the SAME library; their `import("lg:…")`
  // calls resolve to the absorbed graphs by name at tolg.
  Eprp_var var;
  for (const auto& d : ir.ln_dirs) {
    res.inputs.push_back(d);
    for (auto& ln : load_ln_dir(d)) {
      var.add(ln);
    }
  }
  lower_lnasts(opts, res, var, lib_path, /*need_graphs=*/true);
  // Same rule as the source+lg: path in compile_sources: the absorbed graphs are
  // part of the linked design, so every emit sees them (the lg: emit already did
  // — it IS the library — but `--emit verilog:` would have dropped the black-box
  // bodies). Eprp_var::add dedups by shared_ptr against what tolg just created.
  load_lg_into_var(lib_path, var);
  graph_pipeline_and_emits(opts, res, var, lib_path);
}

// ---- compile (the single source->IR->netlist action; front-end + elaborate +
// synth fused into one) ------------------------------------------------------

// Source-driven compile (pyrope or verilog): front-end parse -> pass.upass ->
// (recipe + emits). The pyrope path also publishes a first-elaboration `ln:`
// dir (pub wrappers) when one is requested; the slang path emits the plain
// post-upass forest.
void compile_sources(Options& opts, Result& res, const Ir_inputs& ir) {
  Eprp_var var;
  if (opts.language == "pyrope") {
    // workdir() lazily creates ephemeral scratch, so capture the user's intent
    // before the first timed pass asks for a log path. Incremental persistence
    // is never activated merely because the kernel needed temporary files.
    const bool user_workdir   = !opts.workdir.empty() && !opts.workdir_scratch;
    res.compile_cache.present = user_workdir;
    res.compile_cache.enabled = user_workdir && compile_cache_enabled(opts);
    setup_diag(opts, "compile.pyrope");
    const auto* lg_out             = find_slot(opts.emit_dirs, "lg");
    const auto* ln_out             = find_slot(opts.emit_dirs, "ln");
    std::string lib_path           = lg_out ? lg_out->path : workdir(opts) + "/lgdb";
    const bool  need_graphs        = emits_need_graphs(opts) || force_diag_graphs(opts) || !ir.lg_dirs.empty();
    const bool  defer_cache_lnasts = res.compile_cache.enabled && need_graphs && !emits_need_lnast(opts) && ir.ln_dirs.empty()
                                    && ir.lg_dirs.empty() && !wants_dump(opts, "parse");
    auto n_imports            = pyrope_parse(opts, res, var, ir.ln_dirs, defer_cache_lnasts);
    auto materialize_deferred = [&] {
      if (!defer_cache_lnasts) {
        return;
      }
      compile_cache_materialize_sources(res, var);
      if (lnastfmt_enabled(opts)) {
        run_step("pass.lnastfmt", var, {}, opts, res);
      }
    };
    // 2f-lgimport — absorb any lg: input libraries into the working library
    // BEFORE lowering, so a source unit's `import("lg:name")` resolves to the
    // pre-compiled graph by name at tolg (the same linker mechanism the
    // ln:+lg: path uses). Name-hash gids make matching names dedup cleanly.
    if (!ir.lg_dirs.empty()) {
      auto& lib = livehd::Hhds_graph_library::instance(lib_path);
      for (const auto& d : ir.lg_dirs) {
        check_lg_input_dir(d);
        res.inputs.push_back(d);
        lib.load_merge(d);
      }
    }
    // Tier B restores the all-clean closure as a fast path and transplants
    // eligible units into a mixed restored+fresh dirty-cone rebuild. LNAST-
    // observing outputs still need a complete post-upass forest, and mixed
    // source+lg linking remains on the normal path.
    const bool all_sources_clean
        = res.compile_cache_clean_units.size() == res.compile_cache_unit_keys.size() && !res.compile_cache_unit_keys.empty();
    const bool lg_artifact_only = lg_out != nullptr && opts.emit_dirs.size() == 1 && opts.emits.empty() && opts.dumps.empty();
    if (all_sources_clean && lg_artifact_only && compile_cache_restore_lg_artifact(opts, res, lib_path)) {
      res.outputs.push_back(lg_out->path);
      return;
    }
    // A mixed dirty rebuild still needs the complete hermetic Tier-A forest.
    // Source-level type/pub/elaboration dependencies are broader than graph
    // ownership alone; selecting only Merkle-dirty roots produced a structurally
    // different Minion top. The all-clean lg-only path above remains fully lazy.
    materialize_deferred();
    // Opens the diagnostic window the graph pipeline owns. It starts HERE, not
    // after the parse: the front end and materialize_deferred (lnastfmt) both
    // run on a warm restore too, so their records must not be carried. The
    // window is closed by graph_pipeline_and_emits, before the emits.
    res.compile_cache_diag_mark = livehd::diag::sink().records().size();
    const bool graph_cache_hit  = res.compile_cache.enabled && need_graphs && !emits_need_lnast(opts) && ir.ln_dirs.empty()
                                 && ir.lg_dirs.empty() && compile_cache_restore_graphs(opts, res, var, lib_path);
    // Bare `lhd compile FILE.prp` (no emit) still lowers to LGraphs for max
    // diagnostics; the graphs are built and discarded (force_diag_graphs).
    if (!graph_cache_hit) {
      lower_lnasts(opts, res, var, lib_path, need_graphs);
    }
    // An absorbed lg: library is part of the DESIGN, not just a name table for
    // `import("lg:…")` to bind against. Put its graphs on `var` so every emit
    // sees them: without this the source-side modules were emitted alone and a
    // `--emit verilog:`/`--emit-dir sim:` netlist referenced instances whose
    // bodies were nowhere (`lhd sim lg:DIR tb.prp` failed outright with "no
    // synthesizable modules", the black box being the whole DUT). Adding them
    // after the lowering keeps tolg's freshly created graphs first, and
    // Eprp_var::add dedups by shared_ptr — the library hands back the same
    // handle tolg created, so a name defined on both sides is added once.
    if (!ir.lg_dirs.empty()) {
      load_lg_into_var(lib_path, var);
    }
    if (need_graphs) {
      // F6: an lg: destination is the live closure, not an accumulating bag of
      // definitions from prior runs. Keep real bodies and referenced blackbox
      // declarations; remove renamed/deleted ghosts before any emit/save.
      compile_cache_prune_graphs(var, res, lib_path);
    }
    if (ln_out != nullptr) {
      publish_source_ln(opts, res, var, n_imports, ln_out->path);
    }
    graph_pipeline_and_emits(opts, res, var, lib_path, graph_cache_hit);
    if (res.compile_cache.enabled && need_graphs && !graph_cache_hit && ir.ln_dirs.empty() && ir.lg_dirs.empty()) {
      compile_cache_store_graphs(opts, res, var, lib_path);
    }
  } else {
    setup_diag(opts, "compile.verilog");
    if (!ir.ln_dirs.empty() || !ir.lg_dirs.empty()) {
      throw Lhd_error{"usage", "verilog sources take no ln:/lg: inputs", ""};
    }
    if (opts.reader == "slang") {  // direct SV -> LNAST: the pyrope flow from here
      slang_parse(opts, res, var);
      const auto* lg_out   = find_slot(opts.emit_dirs, "lg");
      const auto* ln_out   = find_slot(opts.emit_dirs, "ln");
      std::string lib_path = lg_out ? lg_out->path : workdir(opts) + "/lgdb";
      // Bare `lhd compile FILE.sv` (no emit) still lowers for max diagnostics.
      lower_lnasts(opts, res, var, lib_path, emits_need_graphs(opts) || force_diag_graphs(opts));
      if (ln_out != nullptr) {
        // The slang units are the design (no imports) — but a provenance
        // PACKAGE unit exports pub values/types, so persist its `__pub`
        // wrapper alongside the forest (a later `import("pkg")` from this
        // ln: dir would otherwise bind a value-less namespace).
        //
        // The WHOLE forest is published, never `filter_top`: --top already rode
        // into slang, so what came back IS the top's elaborated cone. Filtering
        // again here kept the single unit whose name equals --top and dropped
        // every module it instantiates — unlike the lg:/pyrope: emits, which
        // never filter — so `--top X --emit-dir ln:` wrote a one-unit dir that
        // could not be linked ("call to undefined function '<child>'").
        auto                                units = var.lnasts;
        std::vector<std::shared_ptr<Lnast>> wrappers;
        for (const auto& ln : units) {
          if (ln->get_lambda_kind().empty() && !ln->get_top_module_name().ends_with(".__pub")) {
            if (auto w = synthesize_pub_wrapper(ln)) {
              wrappers.push_back(std::move(w));
            }
          }
        }
        units.insert(units.end(), wrappers.begin(), wrappers.end());
        save_ln_dir(opts, res, units, ln_out->path);
      }
      graph_pipeline_and_emits(opts, res, var, lib_path);
    } else {
      auto lib_path = verilog_frontend(opts, res, var);
      graph_pipeline_and_emits(opts, res, var, lib_path);
    }
  }
  const auto closure = harvest_source_files(res, var.lnasts);
  write_depfile(opts, res);
  write_unused_inputs(opts, res, closure);
}

void compile_command(Options& opts, Result& res) {
  auto ir = gather_ir_inputs(opts, "compile");

  // `--reader <slang|yosys-slang|yosys-verilog> -- -F filelist.f` supplies the
  // verilog sources through the raw slang args, so a source compile is valid
  // with no positional file.
  const bool has_sources = !opts.files.empty() || (opts.language == "verilog" && !opts.raw_args.empty());

  if (has_sources) {
    compile_sources(opts, res, ir);
    return;
  }

  // No sources: IR-only inputs (aggregate / link / optimize).
  if (ir.ln_dirs.empty() && ir.lg_dirs.empty()) {
    throw Lhd_error{"usage",
                    "compile requires source files (.prp/.v/.sv) or ln:/lg: inputs",
                    "e.g. `lhd compile foo.prp`, `lhd compile lg:dir --emit verilog:net.v`"};
  }
  if (!ir.ln_dirs.empty() && !ir.lg_dirs.empty()) {
    compile_link_ir(opts, res, ir);  // ln: + lg: linker
  } else {
    compile_ir(opts, res, ir);  // ln:-only or lg:-only
  }
  // IR-only compiles declare no source-file positionals; the list is still
  // created (empty) so a Bazel action can rely on its existence.
  write_unused_inputs(opts, res, {});
}

}  // namespace lhd

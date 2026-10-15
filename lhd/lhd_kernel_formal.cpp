//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
// Formal verification, LEC, witness reproduction, and formal-block handling.

#include <fnmatch.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <map>
#include <mutex>
#include <regex>
#include <set>
#include <sstream>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/strings/str_join.h"
#include "cprop.hpp"
#include "diag.hpp"
#include "encode.hpp"
#include "file_utils.hpp"
#include "formal_blocks.hpp"
#include "formal_cache.hpp"
#include "formal_salt.hpp"
#include "graph_library_singleton.hpp"
#include "hhds/graph.hpp"
#include "inline_sub.hpp"
#include "job_manager.hpp"
#include "latch_contract.hpp"
#include "lhd_kernel_internal.hpp"
#include "lib_cell_prep.hpp"
#include "lnast.hpp"
#include "node_util.hpp"
#include "occurrence_materialize.hpp"
#include "pass.hpp"
#include "pass_single_edge.hpp"
#include "proof_prep.hpp"
#include "query.hpp"
#include "semdiff.hpp"
#include "solve_stats.hpp"
#include "split_selfref.hpp"  // //graph — repair a self-ref exposed by flattening a comb instance
#include "str_tools.hpp"
#include "taskflow/taskflow.hpp"

// Proof-preparation helpers (pass/single_edge/proof_prep.hpp).
using livehd::single_edge::inline_clock_gates_and_fold;
using livehd::single_edge::inline_instances_missing_from_other_side;
using livehd::single_edge::materialize_clock_cells_all;

namespace lhd {

namespace {
bool setting_enabled(std::string_view value) { return value != "false" && value != "0" && value != "off"; }
}  // namespace

// ---- lec (in-process relational equivalence via pass.lec / Pono) ------------

// Legacy exact no-solver fallback for mixed loop representation. New flat
// additive loops are handled first by semdiff's read-only occurrence fold. This
// private-scratch materialize+inline path remains for older supported shapes
// (notably lifted bodies containing calls); replacing it is tracked as the
// highest-priority representation debt in todo_loop_cond_sub.md.
bool mixed_loop_structural_identity(hhds::Graph* compact, hhds::Graph* unrolled, const livehd::semdiff::Semdiff_options& options) {
  if (compact == nullptr || unrolled == nullptr) {
    return false;
  }
  std::vector<hhds::Gid> lifted_gids;
  for (const auto node : compact->body().nodes()) {
    if (!node.is_loop_subnode()) {
      continue;
    }
    const auto desc = node.subnode_loop();
    const auto body = node.get_subnode_graph();
    if (!desc || !body || desc->count > (1u << 20)) {
      return false;
    }
    // inline_sub_instance diagnoses a direct IO feed-through as an unsupported
    // boundary cycle. Since this path is optional, decline it silently before
    // calling the mutator and leave the ordinary solver fallback pristine.
    for (auto sink : body->get_output_node().inp_sorted_pins()) {  // read-only
      if (livehd::graph_util::is_graph_input_pin(sink.get_driver_pin())) {
        return false;
      }
    }
    lifted_gids.push_back(node.get_subnode_gid());
  }
  if (lifted_gids.empty()) {
    return false;
  }

  const auto compact_io  = compact->get_io();
  auto*      compact_lib = compact_io ? compact_io->get_library() : nullptr;
  if (compact_lib == nullptr) {
    return false;
  }
  hhds::GraphLibrary scratch;
  for (const auto& graph : compact->definitions().graphs()) {
    if (!scratch.copy_from(*compact_lib, graph->get_name())) {
      return false;
    }
  }
  const auto top_io = scratch.find_io(compact->get_name());
  auto       top    = top_io ? top_io->get_graph() : std::shared_ptr<hhds::Graph>{};
  if (!top) {
    return false;
  }
  const int materialized = livehd::graph_util::materialize_occurrences(top.get(), "pass.semdiff");
  if (materialized < 0) {
    return false;
  }

  absl::flat_hash_set<hhds::Gid> lifted(lifted_gids.begin(), lifted_gids.end());
  std::vector<hhds::Node_class>  instances;
  for (const auto node : top->body().nodes()) {
    if (livehd::graph_util::type_op_of(node) == Ntype_op::Sub && lifted.contains(node.get_subnode_gid())) {
      instances.push_back(node);
    }
  }
  if (instances.empty()) {
    return false;
  }
  for (const auto& instance : instances) {
    if (!livehd::graph_util::inline_sub_instance(top.get(), instance, "pass.semdiff")) {
      return false;
    }
  }
  Cprop cprop;
  cprop.do_trans(top);
  const bool identical = livehd::semdiff::structural_identical(top.get(), unrolled, options);
  if (identical) {
    return true;
  }
  return livehd::semdiff::structural_equivalent_traversal(top.get(), unrolled, options);
}

// ── TOP-LEVEL OUTCOME of `lhd lec` and `lhd formal verify` ──────────────────
// Owner rulings 2026-10-14 (pass/lec/property_types.hpp has the table). The
// two commands print the SAME words and exit with the SAME codes; every
// verdict carries a modifier and "unknown" names nothing, so it is never
// printed. These helpers are the ONLY place that turns a final result into an
// outcome word, its JSON value and its exit status: the headline, the
// --result-json `lec.verdict`, formal_report.json's run/obligation verdicts,
// the warning diagnostics and the exit ladder all read one Top_outcome.
struct Top_outcome {
  livehd::lec::Outcome kind     = livehd::lec::Outcome::Refuted_unexplained;
  int64_t              n        = -1;  // PROVEN-bounded: design cycles; TESTED-sampled: vectors; else -1
  std::string          word;           // "PROVEN-bounded(8)"
  std::string          json;           // "proven-bounded"
  int                  exit_code = livehd::lec::kExitNoAnswer;
  bool                 warn      = false;  // exit 0, but worth a warning diagnostic
};

[[nodiscard]] static Top_outcome make_top_outcome(livehd::lec::Outcome kind, int64_t n = -1) {
  const auto  info = livehd::lec::outcome_info(kind);
  Top_outcome t;
  t.kind      = kind;
  t.n         = (kind == livehd::lec::Outcome::Proven_bounded || kind == livehd::lec::Outcome::Tested_sampled) ? n : -1;
  t.word      = livehd::lec::outcome_word(kind, t.n);
  t.json      = std::string{info.json};
  t.exit_code = info.exit_code;
  t.warn      = info.warn;
  return t;
}

// Design cycles covered by `steps` BMC steps: an edge-normalized design runs
// `slots` sub-steps per clock period, and `formal.bound` was scaled by the
// same factor, so the user's number comes back un-scaled.
[[nodiscard]] static int64_t design_cycles(int64_t steps, int slots) {
  if (steps <= 0) {
    return 0;
  }
  return slots > 1 ? steps / slots : steps;
}

// `lhd lec`: the outcome of the selected top's FINAL result (after every
// escalation the drivers run). `engine_forced` = the options pinned an
// engine/portfolio (anything but engine=auto), passed explicitly by the caller.
// `fallback_steps` is the requested (scaled) bound, used only when a bounded
// result carries no checked depth of its own (a hierarchy composition).
[[nodiscard]] static Top_outcome lec_top_outcome(const livehd::lec::Query_result& r, bool engine_forced, int slots,
                                                 int fallback_steps) {
  auto kind = livehd::lec::query_outcome(r, engine_forced);
  if (kind == livehd::lec::Outcome::Refuted_unexplained && engine_forced && r.verdict == livehd::lec::Verdict::Undecided) {
    // Unpaired INTERNAL state / cut points are never an issue by themselves:
    // engine=auto falls back to an engine that does not need the pairing (bmc,
    // bit level) and reports THAT outcome. When the options pinned the engine,
    // the fallback was forbidden, so the pinned engine not handling this logic
    // is REFUTED-unchecked (an induction give-up the classifier does not name
    // -- a step counterexample from an arbitrary state, an incomplete state
    // correspondence -- is the same case). "NO reason" stays a bug.
    const std::string_view d   = r.cause_detail.empty() ? r.detail : r.cause_detail;
    const auto             has = [&](std::string_view needle) { return d.find(needle) != std::string_view::npos; };
    // "no inductive base" is the same give-up by name: two memories with
    // different power-on contents leave the shared memory cut no base, and the
    // BMC leg that decides it is exactly the escalation the options forbade.
    if (!has("NO reason")
        && (livehd::lec::internal_state_only(r.unmatched_ref, r.unmatched_impl) || has("single-step CEX")
            || has("correspondence") || has("cut point") || has("no inductive base"))) {
      kind = livehd::lec::Outcome::Refuted_unchecked;
    }
  }
  int64_t n = -1;
  if (kind == livehd::lec::Outcome::Proven_bounded) {
    n = design_cycles(r.checked_steps > 0 ? r.checked_steps : fallback_steps, slots);
  } else if (kind == livehd::lec::Outcome::Tested_sampled) {
    n = static_cast<int64_t>(r.sampling.vectors);
  }
  return make_top_outcome(kind, n);
}

// The last cycle a verify run was ASKED to check (the reset prologue plus the
// requested bound), and whether a bounded-proven obligation stopped short of
// it: the total-budget freeze keeps an obligation at the depth it reached once
// formal.timeout is spent. That is not PROVEN-bounded(n) at a smaller n -- the
// check was cut before the REQUESTED bound, which is TIMEOUT-time ("proven to
// cycle k", owner ruling 2026-10-14).
[[nodiscard]] static int verify_last_cycle(const livehd::lec::Verify_result& r) { return r.reset_hold + r.checked_steps - 1; }
[[nodiscard]] static bool prop_cut_short(const livehd::lec::Prop_result& p, const livehd::lec::Verify_result& r) {
  return p.verdict == livehd::lec::Verdict::Proven && !p.unbounded && !p.vacuous_guard && p.proven_to >= 0
         && p.proven_to < verify_last_cycle(r);
}

// `lhd formal verify`: one obligation row (an assert, or a checked assume).
[[nodiscard]] static Top_outcome prop_top_outcome(const livehd::lec::Prop_result& p, const livehd::lec::Verify_result& r,
                                                  bool engine_forced, int slots) {
  if (prop_cut_short(p, r)) {
    return make_top_outcome(livehd::lec::Outcome::Timeout_time);
  }
  const auto kind = livehd::lec::prop_outcome(p, engine_forced);
  return make_top_outcome(kind,
                          kind == livehd::lec::Outcome::Proven_bounded
                              ? design_cycles(livehd::lec::prop_bounded_depth(p, r.reset_hold), slots)
                              : int64_t{-1});
}

// `lhd formal verify`: the whole run, its rows folded by severity (the
// engine's verify_outcome), plus the rows the budget freeze cut short.
[[nodiscard]] static Top_outcome verify_top_outcome(const livehd::lec::Verify_result& r, bool engine_forced, int slots) {
  auto kind = livehd::lec::verify_outcome(r, engine_forced);
  if (r.verdict == livehd::lec::Verdict::Proven || r.verdict == livehd::lec::Verdict::Undecided) {
    for (const auto& p : r.props) {
      if (p.kind != "assume" && prop_cut_short(p, r)) {
        kind = livehd::lec::worse(kind, livehd::lec::Outcome::Timeout_time);
      }
    }
  }
  const auto n = livehd::lec::verify_outcome_depth(r, kind);
  return make_top_outcome(kind, kind == livehd::lec::Outcome::Proven_bounded ? design_cycles(n, slots) : n);
}

// The exit ladder shared by both commands: the ONLY place an outcome becomes
// an exit status. PROVEN-* / TESTED-* exit 0, TIMEOUT-* exit 0 with a warning,
// REFUTED-cex exits 10 (class equiv_fail) and every other REFUTED-<why>
// (REFUTED-vacuous included: a check that proved nothing) exits 7 (class
// unsupported). The message of a
// failure starts with the outcome word.
static void conclude_outcome(const Top_outcome& t, std::string_view diag_pass, std::string_view cmd, const std::string& msg,
                             const std::string& hint) {
  if (t.exit_code == livehd::lec::kExitPass) {
    if (t.warn) {
      const std::string code = std::string{cmd} + "-" + t.json;
      livehd::diag::warn(diag_pass, code, "unsupported").msg("{}: {}", t.word, msg).hint(hint).emit();
    }
    return;
  }
  throw Lhd_error{t.kind == livehd::lec::Outcome::Refuted_cex ? "equiv_fail" : "unsupported",
                  std::format("{}: {}", t.word, msg),
                  hint};
}

// `lhd formal verify`'s machine-readable run verdict (--result-json `formal`).
static void record_formal_verdict(Result& res, const Top_outcome& t) {
  res.formal.present = true;
  res.formal.verdict = t.json;
  res.formal.bounded = t.kind == livehd::lec::Outcome::Proven_bounded;
  res.formal.bound   = res.formal.bounded ? t.n : int64_t{0};
}

// A result whose `detail` concatenates several attempts is classified from
// the DECIDING attempt's own detail (Query_result::cause_detail), so another
// attempt's keyword cannot win by substring precedence.
static void adopt_cause(livehd::lec::Query_result& dst, const livehd::lec::Query_result& src) {
  dst.cause_detail = src.cause_detail.empty() ? src.detail : src.cause_detail;
}

// A TIMEOUT-time result, and nothing else, goes into the known-timeout ledger.
[[nodiscard]] static bool is_time_limit(const livehd::lec::Query_result& r) {
  if (r.verdict != livehd::lec::Verdict::Undecided || !r.witness.empty() || r.unsupported || r.nothing_compared || r.tested) {
    return false;
  }
  return livehd::lec::undecided_outcome(r.cause_detail.empty() ? r.detail : r.cause_detail, false, false)
         == livehd::lec::Outcome::Timeout_time;
}

// REFUTED-interface: the selected tops' port NAMES (owner ruling 2026-10-14).
// A top OUTPUT name on one side only, or a one-sided top INPUT that has fanout
// on the side that declares it, is an interface mismatch; widths never matter
// (the miter sign/zero extends). A one-sided input with no fanout is reported
// in the detail only. Clocks and resets pair by role, exactly as the engine
// pairs them (clock inputs are the time base, resets are pinned by the reset
// phase), so they never count. A tuple-leaf <-> flat-bus split (`req.a`,`req.b`
// on one side, `req` on the other) is a MATCH: the engine bridges it with
// bundle compare points (detect_port_bundles), and this check accepts any
// leaf/base relation by name so it is never stricter than the engine.
struct Lec_interface_report {
  std::vector<std::string> mismatched;  // "output 'x' only in ref", ...
  std::vector<std::string> unused;      // one-sided inputs without fanout (noted, not an error)
  [[nodiscard]] bool       ok() const { return mismatched.empty(); }
};

static Lec_interface_report lec_top_interface(hhds::Graph* ref, hhds::Graph* impl) {
  Lec_interface_report rep;
  if (ref == nullptr || impl == nullptr || ref->get_io() == nullptr || impl->get_io() == nullptr) {
    return rep;
  }
  auto names = [](hhds::Graph* g, bool inputs) {
    absl::flat_hash_set<std::string> s;
    auto                             gio = g->get_io();
    for (const auto& d : inputs ? gio->get_input_pin_decls() : gio->get_output_pin_decls()) {
      s.insert(d.name);
    }
    return s;
  };
  // `name` pairs with the other side's port set by name, or as a leaf/base of a
  // tuple-leaf <-> flat-bus bundle.
  auto bundle_matched = [](const std::string& name, const absl::flat_hash_set<std::string>& other) {
    const std::string prefix = name + ".";
    for (const auto& o : other) {
      if (o.size() > prefix.size() && o.compare(0, prefix.size(), prefix) == 0) {
        return true;  // `name` is the flat bus, `other` holds its leaves
      }
    }
    for (size_t dot = name.find('.'); dot != std::string::npos; dot = name.find('.', dot + 1)) {
      if (other.contains(name.substr(0, dot))) {
        return true;  // `name` is a leaf of a bus the other side declares flat
      }
    }
    return false;
  };
  const auto ref_in   = names(ref, true);
  const auto impl_in  = names(impl, true);
  const auto ref_out  = names(ref, false);
  const auto impl_out = names(impl, false);
  const auto ref_ck   = livehd::lec::design_clock_inputs(ref, nullptr);
  const auto impl_ck  = livehd::lec::design_clock_inputs(impl, nullptr);
  auto       is_reset = [](const std::string& n) {
    bool neg = false;
    return str_tools::reset_name_polarity(n, neg);
  };
  auto sorted = [](const absl::flat_hash_set<std::string>& s) {
    std::vector<std::string> v(s.begin(), s.end());
    std::sort(v.begin(), v.end());
    return v;
  };
  for (const auto& [mine, other, side] : {std::tuple{&ref_out, &impl_out, "ref"}, std::tuple{&impl_out, &ref_out, "impl"}}) {
    for (const auto& n : sorted(*mine)) {
      if (!other->contains(n) && !bundle_matched(n, *other)) {
        rep.mismatched.push_back(std::format("output '{}' only in {}", n, side));
      }
    }
  }
  for (const auto& [g, mine, other, ck, side] :
       {std::tuple{ref, &ref_in, &impl_in, &ref_ck, "ref"}, std::tuple{impl, &impl_in, &ref_in, &impl_ck, "impl"}}) {
    for (const auto& n : sorted(*mine)) {
      if (other->contains(n) || bundle_matched(n, *other) || ck->contains(n) || is_reset(n)) {
        continue;
      }
      bool fanout = false;
      if (auto pin = g->get_input_pin(n); !pin.is_invalid()) {
        for ([[maybe_unused]] const auto& e : pin.out_edges()) {
          fanout = true;
          break;
        }
      }
      if (fanout) {
        rep.mismatched.push_back(std::format("input '{}' only in {} (and it drives logic there)", n, side));
      } else {
        rep.unused.push_back(std::format("input '{}' only in {} (no fanout: ignored)", n, side));
      }
    }
  }
  return rep;
}

static void dedup_inputs(Result& res) {
  std::vector<std::string> dedup;
  for (const auto& p : res.inputs) {
    if (std::find(dedup.begin(), dedup.end(), p) == dedup.end()) {
      dedup.push_back(p);
    }
  }
  res.inputs = std::move(dedup);
}

// Load one --impl/--ref side into `var.graphs` WITHOUT cgen. lg: libraries load
// directly; pyrope:/ln: parse/load then lower (upass + tolg + recipe) to
// graphs; verilog: always elaborates through native Slang (SV -> LNAST).
// A Yosys debug comparison must compile each source to lg: explicitly first. The in-process lec engine consumes the graphs
// directly; the lgyosys backend re-emits them through cgen (materialize_verilog).
void load_side_graphs(Options& opts, Result& res, const std::string& kind, const std::string& path, std::string_view side,
                      Eprp_var& var) {
  res.inputs.push_back(path);
  if (kind == "lg") {
    // "lec.load" times ONLY the lg: deserialization, never the file-typed sides
    // below: those already report their front-end as inou.prp / inou.slang /
    // inou.yosys.tolg / lnast.tolg run_steps, so a timer spanning the whole
    // function would double-count them (the consumer SUMS the array). lg-to-lg
    // is the only shape the lec ledger row runs and it emitted no phase at all
    // for the loads. Expect a SMALL number here: GraphLibrary::load is lazy, so
    // two 32 MB minion libraries measure ~0.1 ms and the bodies materialize
    // inside pass.lec. The unattributed 15% on that shape was NOT this — it is
    // the run_id content hash in main(), now timed as "lhd.run_id".
    if (!fs::is_directory(path)) {
      throw Lhd_error{"missing_file", std::format("lg: input not found: {}", path), ""};
    }
    Phase_timer load_phase(res, "lec.load");
    auto&       lib = livehd::Hhds_graph_library::instance(path);
    for (const hhds::Gid id : lib.all_gids()) {
      auto g = lib.get_graph(id);
      if (g) {
        var.add(g);
      }
    }
  } else if (kind == "pyrope" || kind == "ln" || kind == "verilog") {
    auto lib_path = std::format("{}/lec_{}_lgdb", workdir(opts), side);
    if (kind == "pyrope") {
      // A pyrope: input can be a single .prp OR an emit DIRECTORY holding one
      // .prp per module (the slang->pyrope multi-module emission). inou.prp
      // splits `files` on comma and loads each as its own LNAST; the runner
      // then resolves the top's import() of its sibling modules. Enumerate the
      // dir's *.prp so a multi-file library recompiles (a lone top file would
      // fail import-no-progress with its callees absent).
      std::string files = path;
      if (fs::is_directory(path)) {
        std::vector<std::string> prps;
        for (const auto& de : fs::directory_iterator(path)) {
          if (de.is_regular_file() && de.path().extension() == ".prp") {
            prps.push_back(de.path().string());
          }
        }
        if (prps.empty()) {
          throw Lhd_error{"missing_file", std::format("pyrope: directory has no .prp files: {}", path), ""};
        }
        std::sort(prps.begin(), prps.end());
        files.clear();
        for (const auto& p : prps) {
          files += (files.empty() ? "" : ",") + p;
        }
      } else {
        check_inputs_exist({path});
      }
      run_step("inou.prp",
               var,
               {
                   {"files", files}
      },
               opts,
               res);
      // A Pyrope side resolves its own import() dependencies (sibling .prp in
      // the importing file's directory, to a fixpoint) — no pre-compile to lg:
      // is ever needed just to satisfy imports (Verilog still needs its own
      // elaboration; this is the same discovery `lhd compile` runs).
      {
        std::vector<std::string> seeds;
        for (size_t b = 0; b <= files.size();) {
          auto e = files.find(',', b);
          if (e == std::string::npos) {
            e = files.size();
          }
          if (e > b) {
            seeds.emplace_back(files.substr(b, e - b));
          }
          b = e + 1;
        }
        discover_imports(var, res, /*n_imports=*/0, seeds);
      }
    } else if (kind == "verilog") {  // slang: the direct SV -> LNAST front-end
      check_inputs_exist({path});
      // Slang needs definitions while elaborating a mapped Verilog side;
      // resolving --lib only later in the encoder is too late. Keep the
      // original source as the only positional input (relative includes belong
      // to it) and hand each graph library's emitted models to slang as a
      // LIBRARY file (`-v`): a model is elaborated only when the design
      // instantiates it, so unused cells never become auto-top roots and an
      // RTL side keeps the sole-module top fallback. Do not recurse through
      // --lib again while materializing those graph-only inputs.
      auto        model_opts = library_model_opts(opts);
      std::string lib_flags;
      for (size_t i = 0; i < opts.libs.size(); ++i) {
        const auto& lp = opts.libs[i];
        if (lp.kind != "lg") {
          const char* cmd = opts.command == "lec" ? "lec" : "formal verify";
          throw Lhd_error{"usage", std::format("{} --lib expects lg:DIR, got '{}:'", cmd, lp.kind), ""};
        }
        auto mv    = materialize_verilog(model_opts, res, lp.kind, lp.path, std::format("{}_models{}", side, i));
        lib_flags += std::format("{}-v\x1f{}", lib_flags.empty() ? "" : "\x1f", mv);
      }
      Eprp_var::Eprp_dict reader_labels{
          {"files", path}
      };
      merge_sets(opts, "compile.slang", reader_labels);
      if (!lib_flags.empty()) {  // append: a user compile.slang.slang_flags stays in force
        auto& flags = reader_labels["slang_flags"];
        flags       = flags.empty() ? lib_flags : flags + "\x1f" + lib_flags;
      }
      run_step("inou.slang", var, std::move(reader_labels), opts, res);
    } else {  // ln:
      if (!fs::is_directory(path)) {
        throw Lhd_error{"missing_file", std::format("ln: input not found: {}", path), "an ln: input is a Forest save directory"};
      }
      for (auto& ln : load_ln_dir(path)) {
        var.add(ln);
      }
    }
    // Each side's selected top also controls ELABORATION: a generic entry
    // needs its defaults instantiated before there is a graph to select.
    // Scope that to lower_lnasts only -- pass.formal reads `opts.top` as the
    // design's COMMITTED top boundary (is_top => an IO assume becomes an
    // unchecked hypothesis, an assert must hold unconditionally), so letting a
    // per-side `--ref-top`/`--impl-top` submodule reach the graph pipeline
    // fails the run on contracts the real parent discharges.
    auto        side_opts    = opts;
    const auto& selected_top = side == "ref" ? opts.ref_top : opts.impl_top;
    if (!selected_top.empty()) {
      side_opts.top = selected_top;
    }
    lower_lnasts(side_opts, res, var, lib_path, /*need_graphs=*/true);
    graph_pipeline_and_emits(opts, res, var, lib_path);
  } else {
    throw Lhd_error{"usage",
                    std::format("lec accepts verilog:, lg:, pyrope:, or ln: inputs, got {}:", kind),
                    "a bare .v/.sv/.prp path infers its kind"};
  }
  if (var.graphs.empty()) {
    throw Lhd_error{"config", std::format("lec {} input {} holds no graphs", side, path), ""};
  }
  if (kind == "lg" && satopt_setting(opts).value_or(false)) {
    // A loaded side is taken as compiled; an explicit --set pass.satopt=true
    // optimizes it too, in memory only: never save back to either input.
    const auto selected = pick_top_graph(var, side == "ref" ? opts.ref_top : opts.impl_top, opts.top, side, "lec", "pass.lec");
    run_satopt_step(var,
                    {
                        {"top", std::string{selected->get_name()}}
    },
                    opts,
                    res);
  }
}

// Emit the machine-parseable per-block progress line (info severity: never an
// error or exit-code change) the moment a block resolves, so an agent driving a
// long bottom-up run stream-parses pass/fail/<cause> instead of waiting for
// the end. Reuses the diag jsonl/pretty rendering; the record carries the block
// name, the verdict, the engine that reached it (the portfolio winner r.engine
// when the auto engine set one, else the requested engine), and the elapsed ms.
static void emit_lec_block_progress(std::string_view block, const livehd::lec::Query_result& r, const livehd::lec::Lec_options& o,
                                    long long elapsed_ms) {
  std::string code;
  std::string verdict;
  switch (r.verdict) {
    case livehd::lec::Verdict::Proven:
      code    = "lec-block-proven";
      verdict = "pass";
      break;
    case livehd::lec::Verdict::Refuted:
      code    = "lec-block-refuted";
      verdict = "fail";
      break;
    default: {
      // Named by its cause (timeout, unsupported, ...), never "inconclusive"/"unknown".
      const auto cause = livehd::lec::undecided_cause(r.detail, r.unsupported);
      code             = "lec-block-" + std::string{cause.json};
      verdict          = std::string{cause.json};
      break;
    }
  }
  const std::string eng = r.engine.empty() ? o.engine : r.engine;
  const long long   ms  = r.elapsed_ms >= 0 ? r.elapsed_ms : elapsed_ms;
  auto              b   = livehd::diag::info("pass.lec", code.c_str(), "progress")
                              .msg("lec block '{}' {}", block, verdict)
                              .verdict(verdict)
                              .engine(eng)
                              .duration_ms(ms);
  if (!r.detail.empty()) {
    b.attr("detail", r.detail);
  }
  if (!r.loop_certificates.empty()) {
    std::string certificates;
    for (const auto& certificate : r.loop_certificates) {
      certificates += (certificates.empty() ? "" : " | ") + certificate;
    }
    b.attr("loop_certificates", certificates);
  }
  if (!r.witness.empty()) {
    b.attr("witness", r.witness);
  }
  if (o.engine == "bmc" || o.engine == "auto") {
    b.attr("bound", std::to_string(o.bound));
  }
  b.emit();
}

// Cache key for one def-pair proof: the two hierarchical (Merkle) digests +
// every verdict-relevant option. Deliberately EXCLUDED: timeout / partitions /
// split / semdiff (effort and strategy — they change how fast, never what is
// claimed) and witness (reporting). The engine-identity salt is applied
// cache-wide by Verdict_cache, not per key.
static std::string lec_pair_cache_key(const livehd::semdiff::Canonical_digest& dref, const livehd::semdiff::Canonical_digest& dimpl,
                                      const livehd::lec::Lec_options& o) {
  auto sorted_join = [](std::vector<std::string> v) {
    std::sort(v.begin(), v.end());
    std::string s;
    for (auto& e : v) {
      s += e;
      s += ',';
    }
    return s;
  };
  std::vector<std::string> match_pairs;
  match_pairs.reserve(o.match.size());
  for (const auto& [mk, mv] : o.match) {
    match_pairs.push_back(mk + "=" + mv);
  }
  // Uncertain (tier-2) pairs are a DISTINCT key segment: they alter the
  // obligation set like match pairs (same pair set => same key, so the Unknown
  // ledger and a pair-assisted PROVEN replay coherently), but must never alias
  // a run where the same pairs were supplied as certain formal.lec.match (a
  // certain-pair bounded-Proven is a legal PASS; an uncertain-pair one is not).
  std::vector<std::string> um_pairs;
  um_pairs.reserve(o.uncertain_match.size());
  for (const auto& [mk, mv] : o.uncertain_match) {
    um_pairs.push_back(mk + "=" + mv);
  }
  std::vector<std::string> mem_pairs;
  mem_pairs.reserve(o.mem_match.size());
  for (const auto& [mk, mv] : o.mem_match) {
    mem_pairs.push_back(mk + "=" + mv);
  }
  // ignore_memory / trust / mem_match / ignore_assumes change WHAT is claimed
  // (a boxed memory, an assumed-equal leaf, a memory pairing, dropped design
  // assumptions), so a verdict obtained under one setting must never replay
  // under another.
  return std::format(
      "{:016x}{:016x}:{:016x}{:016x}|e={};gx={};b={};dc={};ph={};rc={};r={};m=[{}];um=[{}];c=[{}];ac={};da={};a={};sv={};"
      "im=[{}];t=[{}];mm=[{}];ia={};profile={};portfolio={}",
      dref.h0,
      dref.h1,
      dimpl.h0,
      dimpl.h1,
      o.engine,
      o.gold_x,
      o.bound,
      o.decompose,
      o.phase,
      o.reset_cycles,
      o.reset,
      sorted_join(match_pairs),
      sorted_join(um_pairs),
      sorted_join(o.collapse),
      o.assume_check ? 1 : 0,
      o.design_assumes ? 1 : 0,
      o.assumption_key,
      o.solver,
      sorted_join(o.ignore_memory),
      sorted_join(o.trust),
      sorted_join(mem_pairs),
      o.ignore_assumes ? 1 : 0,
      o.profile,
      o.portfolio);
}

// The detail of a verdict-cache hit. Names the matched digests (ref:impl,
// before the option segment of `ckey`) so a hit is auditable: it can only
// replay a verdict for these exact graphs.
static std::string lec_cache_hit_detail(const std::string& ckey, const livehd::formal::Cached_verdict& hit) {
  return std::format("verdict cache hit on ref:impl digests {} with identical options (was {} in {}ms: {})",
                     ckey.substr(0, ckey.find('|')),
                     hit.engine,
                     hit.elapsed_ms,
                     hit.detail);
}

// Entity tail of a full graph name ("file.entity" -> "entity"): the pair-hint
// key basis on the non-hier path, aligned with the hier driver's entity-canon
// def keys so a design proven either way shares its pair hints.
static std::string lec_entity_of(std::string_view n) { return str_tools::canonical_entity_name(n); }

// If a refute's FIRST diverging signal is a TRUSTED box's input
// ("bbin:<def>#inst:sig"), return that trusted def; else "". A trust box asserts
// every leaf input equal, INCLUDING functional don't-cares (a write-data bit
// while the write is disabled), and the trusted leaf cannot be flattened to tell
// a don't-care from a real difference — so such a refute is NOT a sound disproof
// (the same discipline as "refuted under uncertain pairs is never final"). The
// caller degrades it to Unknown, keeping the witness. A genuine refute diverges
// at a normal signal first, so its "(ref=" precedes any "bbin:" and this returns
// "" — bug detection outside the trusted cones is intact.
static std::string lec_refute_trusted_box(const std::string& w, const absl::flat_hash_set<std::string>& trust_set) {
  if (trust_set.empty()) {
    return "";
  }
  auto b = w.find("bbin:");
  if (b == std::string::npos) {
    return "";
  }
  if (auto r0 = w.find("(ref="); r0 != std::string::npos && r0 < b) {
    return "";  // the first divergence is a normal signal, not this box input
  }
  auto start = b + 5;
  auto end   = w.find('#', start);
  if (end == std::string::npos) {
    end = w.find_first_of(":( ", start);
  }
  std::string full = w.substr(start, end == std::string::npos ? std::string::npos : end - start);
  if (trust_set.count(full) > 0) {
    return full;
  }
  auto        d   = full.rfind('.');
  std::string ent = d == std::string::npos ? full : full.substr(d + 1);
  return trust_set.count(ent) > 0 ? ent : "";
}

// A get_hier_name() debug-nid fallback ("n<id>") — never persisted in a pair
// hint: nids shift across recompiles, and hint re-validation could then bind
// the wrong flop.
static bool lec_dbg_nid_name(std::string_view s) {
  return s.size() >= 2 && s.front() == 'n'
         && std::all_of(s.begin() + 1, s.end(), [](unsigned char c) { return std::isdigit(c) != 0; });
}

// Persist the uncertain pairs a PASS validated as the entity-keyed pair hint
// (cache record kind 5). Warm runs re-inject them without the signature pass;
// replay re-validates and keeps the uncertain discipline. ALL-OR-NOTHING: if
// any pair carries a debug-nid name it cannot persist, and a PARTIAL hint is
// worse than none — its warm replay would suppress the fresh signature pass
// while leaving the unnamed flops unmatched, permanently degrading a cold-run
// PASS to Unknown. Store no hint; the fresh pass re-derives the full set
// every run (near-free) and keeps passing.
// Persist the cone obligations this solve proved. Deliberately NOT gated on the
// def's verdict: a cone proof is per-OBLIGATION, so a def that ends Unknown
// because ONE cone is hard still proved the others, and those stay valid
// forever (the digest is the whole claim). The next run then re-attacks only
// the residue -- the cache accumulates partial progress across runs, which the
// def-pair verdict cache cannot do.
static void lec_store_cones(livehd::formal::Verdict_cache* vcache, const livehd::lec::Query_result& r) {
  if (vcache == nullptr) {
    return;
  }
  for (const auto& d : r.cone_proven) {
    vcache->note_cone_proven(d);
  }
}

static void lec_store_pair_hint(livehd::formal::Verdict_cache* vcache, const std::string& entity,
                                const std::vector<std::pair<std::string, std::string>>& pairs) {
  if (vcache == nullptr || pairs.empty()) {
    return;
  }
  livehd::formal::Pair_hint ph;
  for (const auto& p : pairs) {
    if (lec_dbg_nid_name(p.first) || lec_dbg_nid_name(p.second)) {
      return;  // unnameable pair in the set -> no (partial) hint
    }
    ph.pairs.push_back(p);
  }
  vcache->set_pair_hint(entity, std::move(ph));
}

// The state one def's proof could not correspond, for the result JSON (before
// this it was only in a lec.log the size of the design). Built from what the
// tier-2 pairing already computed: semdiff's unpaired cells ("name (reason)")
// plus every injected uncertain pair the verdict did NOT use (the uncertain
// discipline dropped it). Each cell carries its declared width, and the
// suggestion re-offers the dropped pairs, then pairs same-width cells by name
// similarity -- a hint for the user to try as formal.lec.match, never assumed
// by the prover.
struct Lec_state_report {
  std::vector<Result::Lec_verdict::Unpaired_state> cells;
  std::vector<std::pair<std::string, std::string>> suggested;  // {ref, impl}
};

static std::vector<std::string> lec_state_name_tokens(std::string_view n) {
  std::vector<std::string> out;
  std::string              cur;
  auto                     flush = [&]() {
    // Register-naming noise carries no correspondence evidence.
    if (!cur.empty() && cur != "q" && cur != "d" && cur != "r" && cur != "reg" && cur != "ff") {
      out.push_back(cur);
    }
    cur.clear();
  };
  for (char c : n) {
    if (c == '_' || c == '.' || c == '[' || c == ']') {
      flush();
    } else {
      cur.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
  }
  flush();
  return out;
}

// A state name's token sets (leaf name, full path), built once per cell: the
// pairing below scores every ref x impl pair.
struct Lec_state_name_tokens {
  std::set<std::string> leaf, full;
  explicit Lec_state_name_tokens(std::string_view n) {
    const auto d  = n.rfind('.');
    const auto lt = lec_state_name_tokens(d == std::string_view::npos ? n : n.substr(d + 1));
    const auto ft = lec_state_name_tokens(n);
    leaf.insert(lt.begin(), lt.end());
    full.insert(ft.begin(), ft.end());
  }
};

static double lec_state_name_score(const Lec_state_name_tokens& a, const Lec_state_name_tokens& b) {
  auto jaccard = [](const std::set<std::string>& sx, const std::set<std::string>& sy) {
    if (sx.empty() || sy.empty()) {
      return 0.0;
    }
    size_t inter = 0;
    for (const auto& t : sx) {
      inter += sy.count(t);
    }
    return static_cast<double>(inter) / static_cast<double>(sx.size() + sy.size() - inter);
  };
  // The leaf name decides; the full path only breaks ties.
  return jaccard(a.leaf, b.leaf) + 0.01 * jaccard(a.full, b.full);
}

static Lec_state_report lec_unpaired_state_report(const std::string& def, hhds::Graph* ref, hhds::Graph* impl,
                                                  const std::vector<std::string>&                         ref_unpaired,
                                                  const std::vector<std::string>&                         impl_unpaired,
                                                  const std::vector<std::pair<std::string, std::string>>& injected,
                                                  const std::vector<std::pair<std::string, std::string>>& used) {
  Lec_state_report rep;
  auto             widths = [](hhds::Graph* g) {
    absl::flat_hash_map<std::string, int64_t> w;
    if (g == nullptr) {
      return w;
    }
    for (auto node : g->grouped_hierarchy().nodes()) {
      const auto op = livehd::graph_util::type_op_of(node);
      if (op == Ntype_op::Flop || op == Ntype_op::Fflop || op == Ntype_op::Latch || op == Ntype_op::Memory) {
        // Keyed like the engine keys state: an inlined instance's flop spells
        // its path with escaped dots, semdiff's report without.
        w.emplace(livehd::lec::canon_flop_name(node.get_hier_name()),
                  static_cast<int64_t>(livehd::graph_util::bits_of(node.get_driver_pin(0))));
      }
    }
    return w;
  };
  const auto ref_w  = widths(ref);
  const auto impl_w = widths(impl);
  auto       add    = [&](const char* side, const std::string& name, const std::string& reason) {
    const auto& wm = std::string_view{side} == "ref" ? ref_w : impl_w;
    auto        it = wm.find(livehd::lec::canon_flop_name(name));
    rep.cells.push_back({def, side, name, reason, it == wm.end() ? 0 : it->second});
  };
  // semdiff spells a cell "name (reason)".
  auto split_reason = [](const std::string& s) -> std::pair<std::string, std::string> {
    if (s.size() > 3 && s.back() == ')') {
      if (const auto p = s.rfind(" ("); p != std::string::npos) {
        return {s.substr(0, p), s.substr(p + 2, s.size() - p - 3)};
      }
    }
    return {s, "no full match"};
  };
  for (const auto& s : ref_unpaired) {
    auto [n, why] = split_reason(s);
    add("ref", n, why);
  }
  for (const auto& s : impl_unpaired) {
    auto [n, why] = split_reason(s);
    add("impl", n, why);
  }
  std::set<std::pair<std::string, std::string>> dropped;
  for (const auto& p : injected) {
    if (std::find(used.begin(), used.end(), p) == used.end()) {
      dropped.insert(p);
      add("ref", p.first, std::format("uncertain pair with impl '{}' dropped (did not validate)", p.second));
      add("impl", p.second, std::format("uncertain pair with ref '{}' dropped (did not validate)", p.first));
    }
  }
  // Greedy pairing, best evidence first. A dropped uncertain pair ranks above
  // any name guess: semdiff matched its full signature, and the uncertain
  // discipline drops EVERY pair when any one of them fails, so a dropped pair
  // is not known to be wrong. Name guesses need equal widths.
  struct Cand {
    double      score;
    std::string r, i;
  };
  std::vector<Cand> cands;
  for (const auto& [r, i] : dropped) {
    cands.push_back({2.0, r, i});
  }
  std::vector<Lec_state_name_tokens> toks;
  toks.reserve(rep.cells.size());
  for (const auto& c : rep.cells) {
    toks.emplace_back(c.name);
  }
  for (size_t ri = 0; ri < rep.cells.size(); ++ri) {
    const auto& rc = rep.cells[ri];
    if (rc.side != "ref" || rc.bits <= 0) {
      continue;
    }
    for (size_t ii = 0; ii < rep.cells.size(); ++ii) {
      const auto& ic = rep.cells[ii];
      if (ic.side != "impl" || ic.bits != rc.bits || dropped.contains({rc.name, ic.name})) {
        continue;
      }
      if (const double s = lec_state_name_score(toks[ri], toks[ii]); s > 0) {
        cands.push_back({s, rc.name, ic.name});
      }
    }
  }
  std::stable_sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) { return a.score > b.score; });
  absl::flat_hash_set<std::string> taken_r, taken_i;
  for (const auto& c : cands) {
    if (taken_r.contains(c.r) || taken_i.contains(c.i)) {
      continue;
    }
    taken_r.insert(c.r);
    taken_i.insert(c.i);
    rep.suggested.emplace_back(c.r, c.i);
  }
  return rep;
}

static std::string lec_match_string(const std::vector<std::pair<std::string, std::string>>& pairs) {
  std::string s;
  for (const auto& [r, i] : pairs) {
    s += std::format("{}{}={}", s.empty() ? "" : ",", r, i);
  }
  return s;
}

// One human line per def plus the result-JSON fields.
static void lec_publish_state_reports(Result& res, const std::map<std::string, Lec_state_report>& reports) {
  std::vector<std::pair<std::string, std::string>> all;
  for (const auto& [def, rep] : reports) {
    if (rep.cells.empty()) {
      continue;
    }
    std::string ref_s, impl_s;
    for (const auto& c : rep.cells) {
      auto& s  = c.side == "ref" ? ref_s : impl_s;
      s       += std::format("{}{}:{}", s.empty() ? "" : ", ", c.name, c.bits);
    }
    std::print(
        "lec: '{}' unpaired state (limits the proof): ref{{{}}} impl{{{}}}{}\n",
        def,
        ref_s,
        impl_s,
        rep.suggested.empty() ? std::string{} : std::format("; try --set formal.lec.match={}", lec_match_string(rep.suggested)));
    res.lec.unpaired_state.insert(res.lec.unpaired_state.end(), rep.cells.begin(), rep.cells.end());
    all.insert(all.end(), rep.suggested.begin(), rep.suggested.end());
  }
  res.lec.suggested_match = lec_match_string(all);
}

// Disclose helper-conditioned lec verdicts. NOTHING in the driver sets these
// counters today: lec no longer consumes formal-block sidecars (blocks are
// independent `lhd formal verify` tests, and lec's
// single impl==ref obligation could only take their assumes globally). The
// ENGINE-side capability in query.cpp (Lec_options::assumptions + the monitor
// encode) is deliberately retained so re-admitting blocks later is an explicit
// opt-in flag wiring them back up here, not a re-implementation. Every branch
// below is guarded on > 0, so it is inert until then.
static void disclose_lec_helpers(livehd::lec::Query_result& r, const livehd::lec::Lec_options& o) {
  if (o.proven_helpers > 0) {
    r.detail += std::format("; using {} proven impl invariant(s)", o.proven_helpers);
  }
  if (o.input_assumes > 0) {
    r.detail += r.verdict == livehd::lec::Verdict::Proven ? std::format("; PROVEN under {} input assume(s)", o.input_assumes)
                                                          : std::format("; under {} input assume(s)", o.input_assumes);
  }
  if (o.proven_assumes > 0) {
    r.detail += r.verdict == livehd::lec::Verdict::Proven ? std::format("; PROVEN under {} proven assume(s)", o.proven_assumes)
                                                          : std::format("; under {} proven assume(s)", o.proven_assumes);
  }
  if (o.unchecked_assumes > 0) {
    r.detail += r.verdict == livehd::lec::Verdict::Proven
                    ? std::format("; PROVEN under {} unchecked assume(s)", o.unchecked_assumes)
                    : std::format("; under {} unchecked assume(s)", o.unchecked_assumes);
  }
}

// One side's design-authored assumes that the encoder will actually ASSERT as a
// miter hypothesis, mirroring pass/lec/encode.cpp's prop_active_assume rule
// exactly:
//   * `assume_nocheck` is an environment contract BY SPELLING — active whether
//     or not pass.formal ran or stamped anything. This deliberately survives
//     pass.formal RETRACTING a jointly-contradictory set (which clears
//     `proven`): the pair must still gate this count, or o.design_assumes goes
//     false and the hierarchy's no-solver structural-identity shortcut hands
//     out a PROVEN that the UNSAT hypothesis set should have rejected as
//     CONTRADICTORY.
//   * a CHECKED `assume` needs the `proven` stamp pass.formal put on it (a
//     proven one, or every assume under assume_check=false). One that was never
//     proven — refuted or undecided and kept as a runtime check, in a `lg:`
//     library fed straight to `lhd lec`, or on a side built with
//     compile.formal.mode=none — carries no stamp, is not a hypothesis, and
//     must not be disclosed as one either.
//   * a `proven` of kind kFormalAssumeHier was discharged by the HIERARCHY
//     (proven at every occurrence under the parents' bindings). The encoder
//     does not turn that into a hypothesis for the def on its own, so it is not
//     active here.
// The verdict discloses the two classes apart: a checked assume pass.formal
// proved is a PROVEN fact, while an assume_nocheck (or any assume under
// assume_check=false) is an UNCHECKED contract. The class comes from the
// STAMP (kFormalAssume = proved, kFormalAssumeUnchecked = accepted without
// proof), never from this lec run's assume_check: an `lg:` side is not
// recompiled, so it keeps whatever its own compile decided. A graph persisted
// by an older pass.formal stamped every acceptance kFormalAssume, and a plain
// assume carrying `proven` AND `runtime_check` there was an undischarged top IO
// assume promoted to a hypothesis -- it is unchecked too, never proven.
struct Design_assumes {
  int proven    = 0;
  int unchecked = 0;
};
static Design_assumes design_assume_occurrences(hhds::Graph* top) {
  Design_assumes active;
  if (top == nullptr) {
    return active;
  }
  for (auto node : top->occurrences().nodes(hhds::Node_order::forward)) {
    if (livehd::graph_util::type_op_of(node) != Ntype_op::Sub) {
      continue;
    }
    auto sio = node.get_subnode_io();
    if (sio == nullptr || sio->get_name() != livehd::graph_util::fproperty_module_name) {
      continue;
    }
    std::string_view raw             = livehd::graph_util::node_name_of(node);
    const bool       nocheck_by_name = raw.rfind("assume_nocheck\x1f", 0) == 0;
    if (raw.rfind("assume\x1f", 0) != 0 && !nocheck_by_name) {
      continue;
    }
    const auto base = node.base_node();
    const bool proven_hier
        = livehd::graph_util::has_proven(base) && livehd::graph_util::proven_of(base) == livehd::graph_util::kFormalAssumeHier;
    if (!nocheck_by_name && (!livehd::graph_util::has_proven(base) || proven_hier)) {
      continue;
    }
    const bool unchecked = nocheck_by_name || livehd::graph_util::proven_of(base) == livehd::graph_util::kFormalAssumeUnchecked
                           || livehd::graph_util::has_runtime_check(base);  // legacy promoted top IO assume
    ++(unchecked ? active.unchecked : active.proven);
  }
  return active;
}

// Bottom-up hierarchical LEC driver (formal.lec.hier=true). Build the module-def
// dependency DAG over the defs present in both libraries (paired by ENTITY — see
// below), scope it to the picked TOP pair and its transitive descendants (a
// whole-design library may hold many defs unrelated to --ref-top; those are NOT
// proven as extra roots), topo-order the subtree
// leaves-first, and LEC each def under the `auto` portfolio. Record the proven
// set; for each parent, force-black-box its PROVEN child instances (--collapse) so
// the parent proof stops re-solving them, while a child NOT provable in isolation
// stays FLATTENED into the parent (descended) — the M5 CEGAR / un-black-box
// fallback, now in v1. Correspondence is name-based (no semdiff needed when the
// call structures match). Each def emits a per-block progress line the instant it
// resolves, so an agent stream-parses the long run. Returns the TOP def's result,
// or — when a descendant REFUTED — that descendant's (see the fail-fast gate in
// run_def and the aggregate below).
// `cvc5_hot` (lhd.stats, optional): filled with the top few (def, conflicts)
// pairs, hardest first — the per-def ranking the run-total report appends. The
// summed Cvc5_stats itself rides the returned Query_result's `cvc5` member.
static livehd::lec::Query_result lec_hierarchical(Result& res, Eprp_var& ref_var, Eprp_var& impl_var, const std::string& top_name,
                                                  hhds::Graph* ref_top_g, hhds::Graph* impl_top_g,
                                                  const livehd::lec::Lec_options&                     base,
                                                  const absl::flat_hash_map<hhds::Gid, hhds::Graph*>* sub_lib,
                                                  livehd::formal::Verdict_cache* vcache, bool retry_all, bool fail_fast_refute,
                                                  bool top_down, int edge_slots,
                                                  std::vector<std::pair<std::string, int64_t>>* cvc5_hot = nullptr) {
  using livehd::lec::Verdict;
  namespace gu = livehd::graph_util;

  // key -> def graph (case-sensitive, LiveHD/Pyrope name policy). A def's FULL
  // graph name embeds its front-end namespace (Pyrope "file.entity" vs slang's
  // flat "entity"), so the same module never shares a full name across a
  // cross-front-end pair. Defs therefore pair by ENTITY (the post-'.' tail)
  // when that entity names exactly ONE graph on its side; an ambiguous entity
  // keeps the full name (such defs simply stay flattened into their parents).
  // pass/lec's box-correspondence builder canonicalizes the same way, so the
  // entity keys pushed into o.collapse resolve on both sides.
  auto                                   entity_of = [](std::string_view n) -> std::string { return lec_entity_of(n); };
  // formal.lec.trust set: def keys ASSUMED equal without a proof. Matched by the
  // canonical (entity) key the DAG uses, or by either side's full spelling.
  const absl::flat_hash_set<std::string> trust_set(base.trust.begin(), base.trust.end());
  auto                                   is_trusted = [&](std::string_view key) -> bool {
    if (trust_set.empty()) {
      return false;
    }
    return trust_set.count(std::string{key}) > 0 || trust_set.count(entity_of(key)) > 0;
  };
  Entity_canonicalizer                           canon_ref(ref_var);
  Entity_canonicalizer                           canon_impl(impl_var);
  absl::flat_hash_map<std::string, hhds::Graph*> ref_by_name, impl_by_name;
  for (auto& g : ref_var.graphs) {
    if (g) {
      ref_by_name[canon_ref(g->get_name())] = g.get();
    }
  }
  for (auto& g : impl_var.graphs) {
    if (g) {
      impl_by_name[canon_impl(g->get_name())] = g.get();
    }
  }

  // Per-side tops may DIFFER (--ref-top vs --impl-top; e.g. v2prp LECs the
  // emitted .v module name against the original Pyrope lambda). The by-name
  // pairing alone would then never LEC the top pair at all — a silent
  // vacuous pass. Force-pair the two explicitly-picked TOP graphs under the
  // ref-top key so the driver always proves/refutes the top itself.
  const std::string top_key = canon_ref(top_name);
  ref_by_name[top_key]      = ref_top_g;
  impl_by_name[top_key]     = impl_top_g;

  // Unlike an outputless CHILD (which is structurally unobservable to its
  // parent), an outputless SELECTED TOP gives the user no comparison points at
  // all: there is nothing to check. The port NAMES already matched (the
  // REFUTED-interface pre-check in lec_command ran first), so this is
  // REFUTED-vacuous (exit 7, owner ruling 2026-10-14: a check that proved
  // nothing is a sign of a problem -- a wrong top or dropped ports) -- an empty
  // miter with no one-sided port, exactly what query_outcome() calls vacuous.
  const auto ref_top_io  = ref_top_g == nullptr ? nullptr : ref_top_g->get_io();
  const auto impl_top_io = impl_top_g == nullptr ? nullptr : impl_top_g->get_io();
  if (ref_top_io != nullptr && impl_top_io != nullptr && ref_top_io->get_output_pin_decls().empty()
      && impl_top_io->get_output_pin_decls().empty()) {
    livehd::lec::Query_result r;
    r.verdict          = Verdict::Undecided;
    r.engine           = base.engine;
    r.nothing_compared = true;
    r.detail           = "the selected top has no output port on either side";
    return r;
  }

  // The LEC-able defs are those present on BOTH sides; children[def] = the child
  // def keys it instantiates (taken from the ref-side Subs, canonicalized).
  absl::flat_hash_map<std::string, std::vector<std::string>>         children;
  absl::flat_hash_map<std::string, absl::flat_hash_set<std::string>> box_children;
  absl::flat_hash_map<std::string, std::vector<std::string>>         impl_children;
  for (const auto& [name, g] : impl_by_name) {
    for (auto node : g->body().nodes(hhds::Node_order::forward)) {
      // A Sub with NO subnode binding has no def to pair: a mapped netlist
      // keeps a runtime property marker (`lgassert`/`fproperty`, see
      // gu::is_property_marker) as a body-less Sub whose def is not in the
      // netlist library -- minion's `minion_tlb.sv:372` bit-range guard.
      // Dereferencing its null io here crashed `lhd lec` on the netlist.
      if (gu::type_op_of(node) != Ntype_op::Sub) {
        continue;
      }
      if (auto sio = node.get_subnode_io(); sio != nullptr) {
        impl_children[name].push_back(canon_impl(sio->get_name()));
      }
    }
  }
  for (auto& [name, g] : ref_by_name) {
    if (impl_by_name.find(name) == impl_by_name.end()) {
      continue;
    }
    // A corresponding instance can be below a rolled-loop wrapper. Box
    // matching already handles those occurrences; direct child counts do not.
    auto&                    reachable = box_children[name];
    std::vector<std::string> pending{name};
    while (!pending.empty()) {
      auto parent = std::move(pending.back());
      pending.pop_back();
      if (auto it = impl_children.find(parent); it != impl_children.end()) {
        for (const auto& child : it->second) {
          if (reachable.insert(child).second) {
            pending.push_back(child);
          }
        }
      }
    }
    absl::flat_hash_set<std::string>  seen, wrappers;
    // A ROLLED loop's lifted body (`__loop<n>`, uPass `roll`) exists on THIS
    // side only, so it is never a shared child -- but the shared defs BELOW it
    // (`lane` under `u_loop_0`) must still be enumerated, or this parent is
    // flattened whole and its flat miter has to pair every replica flop by
    // name. Descend through the one-sided wrapper exactly as `reachable` does
    // for the impl side above; the encoder boxes a collapsed def at any depth.
    std::function<void(hhds::Graph*)> collect = [&](hhds::Graph* pg) {
      for (auto node : pg->body().nodes(hhds::Node_order::forward)) {
        if (gu::type_op_of(node) != Ntype_op::Sub) {
          continue;
        }
        auto sio = node.get_subnode_io();
        if (sio == nullptr) {
          continue;  // body-less marker Sub (see impl_children above): no def to pair
        }
        std::string cn  = canon_ref(sio->get_name());
        auto        rit = ref_by_name.find(cn);
        if (rit == ref_by_name.end()) {
          continue;
        }
        if (impl_by_name.find(cn) != impl_by_name.end()) {
          if (seen.insert(cn).second) {
            children[name].push_back(cn);
          }
        } else if (node.is_loop_subnode() && rit->second != pg && wrappers.insert(cn).second) {
          collect(rit->second);
        }
      }
    };
    collect(g);
  }

  // Topo-order leaves-first (DFS post-order; the in-progress mark guards cycles),
  // rooted at the TOP pair only: `order` is exactly the top and its transitive
  // shared descendants. Defs that merely coexist in the two libraries (a
  // whole-design --emit-dir holds every module, not just the --ref-top subtree)
  // are outside the requested proof and must not become extra roots.
  std::vector<std::string>                order;
  absl::flat_hash_map<std::string, int>   mark;  // 0 unvisited, 1 in-progress, 2 done
  std::function<void(const std::string&)> dfs = [&](const std::string& n) {
    if (auto it = mark.find(n); it != mark.end() && it->second != 0) {
      return;  // done, or a cycle back-edge (modules form a DAG)
    }
    mark[n] = 1;
    if (auto it = children.find(n); it != children.end()) {
      for (const auto& c : it->second) {
        dfs(c);
      }
    }
    // Do not retain a reference to mark[n] across the recursive calls above:
    // inserting a descendant can rehash flat_hash_map and invalidate every
    // slot reference. Reacquire the parent slot after the subtree is complete.
    mark[n] = 2;
    order.push_back(n);
  };
  dfs(top_key);

  // An active assumption inside a boxed child would disappear from the
  // parent's cvc5 problem: a box exposes only IO, not the child's fproperty
  // nodes. Until boundary-contract emission is added, keep every
  // assumption-bearing cone transparent. The driver remains hierarchical for
  // unrelated defs, and the normal CEGAR/inlining path carries each assumption
  // at its real occurrence into the selected-top proof.
  auto direct_assume = [](hhds::Graph* g) {
    if (g == nullptr) {
      return false;
    }
    for (auto node : g->body().nodes(hhds::Node_order::forward)) {
      if (livehd::graph_util::type_op_of(node) != Ntype_op::Sub) {
        continue;
      }
      auto sio = node.get_subnode_io();
      if (sio == nullptr || sio->get_name() != livehd::graph_util::fproperty_module_name) {
        continue;
      }
      std::string_view raw = livehd::graph_util::node_name_of(node);
      if (raw.rfind("assume\x1f", 0) == 0 || raw.rfind("assume_nocheck\x1f", 0) == 0) {
        return true;
      }
    }
    return false;
  };
  absl::flat_hash_set<std::string> assume_cones;
  for (const auto& name : order) {  // leaves first: child marks propagate up
    bool active = direct_assume(ref_by_name[name]) || direct_assume(impl_by_name[name]);
    if (auto it = children.find(name); it != children.end()) {
      for (const auto& child : it->second) {
        active = active || assume_cones.contains(child);
      }
    }
    if (active) {
      assume_cones.insert(name);
    }
  }

  // ── DESIGN-WIDE CLOCK FOREST (2f-lec, "Clock-graph propagation") ───────────
  // Resolve clocks ONCE, TOP-DOWN, before any def is proven. Per-endpoint
  // bottom-up resolution cannot do this: a Pyrope `reg x = 0` has NO clock cone
  // at all (it commits on its module's own clock), and a cone that stops at an
  // opaque boundary names the CHILD's port rather than the net the parent
  // drives -- so one physical clock acquires as many "roots" as it has port
  // spellings. minion calls the same net `clk_i` in 85 defs and `clock` in 6,
  // and `core_top` -- which declares exactly ONE clock port in both front-ends
  // -- was reported as having THREE roots, with the two sides disagreeing.
  //
  // Downward instead: the selected TOP's clock inputs are the roots; a child's
  // clock port takes the root of the net its parent drives into it; a module's
  // implicit clock is the root reaching that module. A `Clock_cell` (or an
  // inline gate) is a DERIVATION of a root, never a new one -- gating,
  // inversion and division split the SCHEDULE, not the forest.
  //
  // Built per side (each design has its own graphs and port names) and consumed
  // by plan_phases via Lec_options::clock_forest.
  auto build_forest = [&](const absl::flat_hash_map<std::string, hhds::Graph*>& by_name,
                          const std::function<std::string(std::string_view)>&   canon,
                          hhds::Graph*                                          top_g) {
    livehd::lec::Clock_forest forest;
    const std::string         canonical_top_name = canon(top_g->get_name());

    // Resolve one driver INSIDE parent `pname` to a root name, following the
    // derivations. Returns "" when it does not reach a known root.
    std::function<std::string(const hhds::Pin_class&, const std::string&, int)> root_of
        = [&](const hhds::Pin_class& d, const std::string& pname, int depth) -> std::string {
      if (d.is_invalid() || depth > 12) {
        return "";
      }
      const auto cr = livehd::latch_contract::control_root(d, /*stop_at_clock_cell=*/true);
      if (cr.net.is_invalid()) {
        return "";
      }
      if (gu::is_graph_input_pin(cr.net)) {
        const std::string port{gu::pin_name_of(cr.net)};
        std::string       root_name;
        if (auto* r = forest.find(canon(cr.net.get_graph()->get_name()), port)) {
          root_name = *r;
        } else if (pname == canonical_top_name) {
          root_name = port;  // a top port IS a root
        }
        if (root_name.empty() || gu::bits_of(cr.net) <= 1) {
          return root_name;
        }
        // A BIT of a clock BUS. control_root follows the bit select to the
        // WHOLE input, so without this `.a(clks[0]), .b(clks[1])` gave both
        // ports root `clks`, both sides' phase plans keyed `clks`, and the
        // root-agreement check had nothing to tell apart (a child whose flops
        // moved from `a` to `b` was PROVEN, equiv/lec/clock_phase_root_clash_
        // hier_bus). Key the bit the way encode.cpp's Clk_root and the phase
        // schedule's root_key do: bare name for bit 0, `name[k]` otherwise.
        const auto cb = livehd::lec::clock_input_bit(d);
        if (!cb || cb->first != port) {
          return "";  // the walk cannot tell which bit: leave the port unmapped
        }
        if (cb->second == 0) {
          // Bit 0, or the bus forwarded from its LSB (a multi-bit port): the
          // port's bit k is the root's bit k, which the phase schedule appends.
          // A root that already names a bit cannot absorb another one.
          return gu::bits_of(d) > 1 && root_name.back() == ']' ? std::string{} : root_name;
        }
        if (gu::bits_of(d) > 1 || root_name.back() == ']') {
          return "";  // an offset multi-bit slice, or a bit of a bit: unmapped
        }
        return root_name + "[" + std::to_string(cb->second) + "]";
      }
      if (cr.net.is_const()) {
        return "";
      }
      auto       n  = cr.net.get_master_node();
      const auto op = gu::type_op_of(n);
      if (op == Ntype_op::Clock_cell) {  // a gate DERIVES its reference clock
        return root_of(livehd::latch_contract::sink_driver_hier(n, "clk_ref"), pname, depth + 1);
      }
      if (op == Ntype_op::And || op == Ntype_op::Or) {  // an inline gate, likewise
        for (auto sink : n.inp_sorted_pins()) {
          const auto drv = sink.get_driver_pin();
          if (drv.is_const()) {
            continue;
          }
          if (std::string r = root_of(drv, pname, depth + 1); !r.empty()) {
            return r;
          }
        }
      }
      return "";
    };

    // SEED: the top's own clock input ports.
    //
    // "The top" here is the DEF BEING PROVEN, not the design's top — the
    // leaves-first driver proves each def as its own top, where its ports are
    // unbound. So before calling them roots, union the ports that EVERY
    // instantiation site of this def, ANYWHERE in the loaded library (not just
    // inside `order`), drives from one net. minion's `intpipe_csr_file` declares
    // both `clk_i` and `clock` and its sites tie them together; without this it
    // is two roots and the schedule refuses a single-clock module. Same
    // mechanism that pairs the prim_rf_*_preview family's `preview_clk_i` +
    // `rf_clk_i`.
    absl::flat_hash_map<std::string, std::string> port_uf;  // port -> representative
    // Whether the selected top is instantiated anywhere in the loaded library,
    // and whether any such site drives its `clock` port (THE IMPLICIT-CLOCK
    // PORT rule below needs both answers).
    bool top_has_site     = false;
    bool top_clock_driven = false;
    {
      std::function<std::string(const std::string&)> find = [&](const std::string& x) -> std::string {
        auto it = port_uf.find(x);
        if (it == port_uf.end() || it->second == x) {
          port_uf[x] = x;
          return x;
        }
        std::string r = find(it->second);
        port_uf[x]    = r;
        return r;
      };
      bool seen_site = false;
      for (const auto& [pname, pg] : by_name) {
        (void)pname;
        for (auto node : pg->body().nodes(hhds::Node_order::forward)) {
          if (gu::type_op_of(node) != Ntype_op::Sub) {
            continue;
          }
          auto sio = node.get_subnode_io();
          if (sio == nullptr || canon(sio->get_name()) != canonical_top_name) {
            continue;
          }
          absl::flat_hash_map<std::string, std::vector<std::string>> by_net;
          top_has_site = true;
          for (auto sink : node.inp_sorted_pins()) {
            std::string port;
            for (const auto& d : sio->get_input_pin_decls()) {
              if (sio->get_input_port_id(d.name) == sink.get_port_id()) {
                port = d.name;
                break;
              }
            }
            // PLURAL: a compact loop's carry-in sink holds two drivers
            // (pass/legalize/legalize.cpp:301).
            for (const auto& drv : sink.get_driver_pins()) {
              const auto cr = livehd::latch_contract::control_root(drv);
              if (port.empty() || cr.net.is_invalid()) {
                continue;
              }
              if (port == "clock") {
                top_clock_driven = true;
              }
              // Keyed by (net, BIT): control_root follows a bit select to the
              // whole net, so `.a(clks[0]), .b(clks[1])` would union two
              // distinct clocks into one root. A bus bit the walk cannot name
              // keys on its own driver, so it unions only with itself.
              std::string key = std::to_string(static_cast<uint64_t>(cr.net.get_class_index().value));
              if (gu::bits_of(cr.net) > 1) {
                const auto cb = gu::is_graph_input_pin(cr.net) ? livehd::lec::clock_input_bit(drv) : std::nullopt;
                if (cb && cb->first == gu::pin_name_of(cr.net)) {
                  key += (gu::bits_of(drv) > 1 ? "[*" : "[") + std::to_string(cb->second) + "]";
                } else {
                  key = "\x02" + std::to_string(static_cast<uint64_t>(drv.get_class_index().value));
                }
              }
              by_net[key].push_back(port);
            }
          }
          if (!seen_site) {
            for (auto& [net, ports] : by_net) {
              (void)net;
              for (size_t i = 1; i < ports.size(); ++i) {
                port_uf[find(ports[i])] = find(ports[0]);
              }
            }
            seen_site = true;
            continue;
          }
          // A later site may only NARROW: keep a merge only if this site agrees.
          absl::flat_hash_map<std::string, std::string>  here;
          std::function<std::string(const std::string&)> hfind = [&](const std::string& x) -> std::string {
            auto it = here.find(x);
            if (it == here.end() || it->second == x) {
              here[x] = x;
              return x;
            }
            std::string r = hfind(it->second);
            here[x]       = r;
            return r;
          };
          for (auto& [net, ports] : by_net) {
            (void)net;
            for (size_t i = 1; i < ports.size(); ++i) {
              here[hfind(ports[i])] = hfind(ports[0]);
            }
          }
          absl::flat_hash_map<std::string, std::string>  merged;
          std::function<std::string(const std::string&)> mfind = [&](const std::string& x) -> std::string {
            auto it = merged.find(x);
            if (it == merged.end() || it->second == x) {
              merged[x] = x;
              return x;
            }
            std::string r = mfind(it->second);
            merged[x]     = r;
            return r;
          };
          for (auto& [a, _] : port_uf) {
            (void)_;
            for (auto& [b, __] : port_uf) {
              (void)__;
              if (a < b && find(a) == find(b) && hfind(a) == hfind(b)) {
                merged[mfind(b)] = mfind(a);
              }
            }
          }
          port_uf = std::move(merged);
        }
      }
    }
    auto rep = [&](const std::string& p) -> std::string {
      auto it = port_uf.find(p);
      if (it == port_uf.end()) {
        return p;
      }
      std::string x = p;
      for (int i = 0; i < 8; ++i) {
        auto j = port_uf.find(x);
        if (j == port_uf.end() || j->second == x) {
          break;
        }
        x = j->second;
      }
      return x;
    };
    // Top clock roots that are multi-bit inputs: each BIT is its own clock (see
    // root_of), so such a root is never "the one top clock" the single-root
    // closure below may hand to every unbound clock port.
    absl::flat_hash_set<std::string> bus_roots;
    {
      auto& row = forest.port_root[canonical_top_name];
      for (auto node : top_g->grouped_hierarchy().nodes()) {
        const auto op = gu::type_op_of(node);
        if (op != Ntype_op::Flop && op != Ntype_op::Fflop && op != Ntype_op::Memory && op != Ntype_op::Latch) {
          continue;
        }
        auto seed = [&](const auto& ctrl) {
          const auto cr = livehd::latch_contract::control_root(ctrl);
          if (cr.net.is_invalid() || !gu::is_graph_input_pin(cr.net) || cr.net.get_graph() != top_g) {
            return;
          }
          const std::string port{gu::pin_name_of(cr.net)};
          row[port] = rep(port);
          if (gu::bits_of(cr.net) > 1) {
            bus_roots.insert(row[port]);
          }
        };
        seed(livehd::latch_contract::sink_driver_hier(node, op == Ntype_op::Latch ? "enable" : "clock_pin"));
        // A Memory's PER-PORT clock lanes (`p*Memory_port_stride + clock_pin`,
        // the rule encode.cpp's memory_clocks applies) are roots too. Seeding
        // only sink 0 left a top whose second clock reaches nothing but a memory
        // port with ONE seeded root, and the single-root closure below then
        // mapped that independent clock input onto it (`clk_b -> clk_a`), so the
        // phase plan scheduled the clk_b port on clk_a (false PROVEN,
        // equiv/lec/mem_clock_cell_planroot*).
        if (op == Ntype_op::Memory) {
          constexpr auto stride = static_cast<int>(Ntype::Memory_port_stride);
          const auto     ck_pid = static_cast<int>(Ntype::get_sink_pid(Ntype_op::Memory, "clock_pin"));
          for (const auto& sink : node.inp_sorted_pins()) {
            const auto raw = static_cast<int>(sink.get_port_id());
            if (raw == ck_pid || raw % stride != ck_pid) {
              continue;
            }
            for (const auto& drv : sink.get_driver_pins()) {
              if (!drv.is_invalid() && !drv.is_const()) {
                seed(drv);
              }
            }
          }
        }
      }
      // THE IMPLICIT-CLOCK PORT. tolg gives a module whose body holds a
      // `reg x = 0` an input port named `clock` — the module's own clock — and
      // NO instantiation site drives it explicitly, so no site union can ever
      // tie it to the module's named clock port. Left alone it is a second
      // "root" on a single-clock module, which is what refused minion's
      // `intpipe_csr_file` (declares `clk_i`; its parent wires `clk_i = clock`)
      // and, through it, `intpipe_top` and `core_top`.
      //
      // A module's own clock IS its clock: when exactly one OTHER clock root is
      // present, `clock` is that root. Deliberately narrow — with two other
      // roots there is nothing unique to attach it to and it stays its own,
      // which keeps a genuinely multi-clock module an honest refusal.
      //
      // ONLY for a top the library instantiates without ever driving its
      // `clock`: that port is then the auto-wired implicit clock no site
      // spells. On the DESIGN top (no site at all) `clock` is a primary input
      // like any other, and two top clock inputs are two independent clocks of
      // the miter's environment. Merging them there scheduled both on one
      // microstep sequence -- an undisclosed "the clocks are tied" assumption:
      // `mod derived(clock:Clock, clk_b:Clock)` with a register moved from
      // `clock` to `clk_b` came back PROVEN-full (the sampled run refutes it).
      if (top_has_site && !top_clock_driven) {
        std::string other;
        bool        unique = true;
        for (const auto& [pn, r] : row) {
          if (pn.empty() || pn == "clock") {
            continue;
          }
          if (other.empty()) {
            other = r;
          } else if (other != r) {
            unique = false;
            break;
          }
        }
        if (unique && !other.empty() && row.count("clock") != 0) {
          row["clock"] = other;
        }
      }
      // The top's implicit clock is its single clock ROOT when it has exactly one.
      std::string only;
      bool        one = true;
      for (const auto& [pn, r] : row) {
        if (only.empty()) {
          only = r;
        } else if (only != r) {
          one = false;
          break;
        }
      }
      if (one && !only.empty()) {
        row[""] = only;
      }
    }

    // PROPAGATE downward. `order` is leaves-first, so its reverse visits every
    // parent before its children -- exactly what a top-down binding walk needs.
    for (auto it = order.rbegin(); it != order.rend(); ++it) {
      const std::string& pname = *it;
      auto               pit   = by_name.find(pname);
      if (pit == by_name.end()) {
        continue;
      }
      for (auto node : pit->second->body().nodes(hhds::Node_order::forward)) {
        if (gu::type_op_of(node) != Ntype_op::Sub) {
          continue;
        }
        auto sio = node.get_subnode_io();
        if (sio == nullptr) {
          continue;
        }
        const std::string cn = canon(sio->get_name());
        if (by_name.find(cn) == by_name.end()) {
          continue;
        }
        for (auto sink : node.inp_sorted_pins()) {
          std::string port;
          for (const auto& d : sio->get_input_pin_decls()) {
            if (sio->get_input_port_id(d.name) == sink.get_port_id()) {
              port = d.name;
              break;
            }
          }
          if (port.empty()) {
            continue;
          }
          const std::string r = root_of(sink.get_driver_pins().front(), pname, 0);
          if (r.empty()) {
            continue;  // not a clock (or not resolvable): leave it unmapped
          }
          auto& row = forest.port_root[cn];
          if (auto ex = row.find(port); ex != row.end() && ex->second != r) {
            ex->second = "\x03ambiguous";  // sites disagree: assert nothing
          } else {
            row[port] = r;
          }
        }
      }
      // (No implicit-clock rewrite here. A child's `clock` row entry exists only
      // when a site DRIVES that port, and then the site's root is the fact:
      // overriding it with the def's other clock root merged two independently
      // driven clocks -- and re-merged the design top's own two clock inputs,
      // whose row this loop also visits.)
      // A def's IMPLICIT clock is the root reaching it -- well defined when its
      // clock ports all carry ONE root.
      for (auto& [dname, row] : forest.port_root) {
        if (row.count("") != 0) {
          continue;
        }
        std::string one;
        bool        uniq = true;
        for (const auto& [pn, r] : row) {
          if (pn.empty() || r == "\x03ambiguous") {
            continue;
          }
          if (one.empty()) {
            one = r;
          } else if (one != r) {
            uniq = false;
            break;
          }
        }
        if (uniq && !one.empty()) {
          row[""] = one;
        }
      }
    }
    // SINGLE-ROOT CLOSURE. "The normal case is a single root that propagates and
    // splits": when the selected top has exactly ONE clock input, every
    // clock-spelled port anywhere in its subtree carries that root, including a
    // port no site drives explicitly. Pyrope's implicit clock becomes exactly
    // such a port -- `reg x = 0` lowers to a def port named `clock` that the
    // caller may never wire -- so without this the same net keeps as many roots
    // as it has spellings (minion: `clk_i` in 85 defs, `clock` in 6).
    //
    // Gated on ONE top root: with two independent top clocks there is nothing to
    // close over and an unbound port stays its own root, which is the honest
    // refusal.
    {
      std::string only_root;
      bool        one = true;
      if (auto tit = forest.port_root.find(canonical_top_name); tit != forest.port_root.end()) {
        for (const auto& [pn, r] : tit->second) {
          if (pn.empty()) {
            continue;
          }
          if (only_root.empty()) {
            only_root = r;
          } else if (only_root != r) {
            one = false;
            break;
          }
        }
      }
      if (one && !only_root.empty() && !bus_roots.contains(only_root)) {
        for (const auto& dname : order) {
          auto dit = by_name.find(dname);
          if (dit == by_name.end() || dit->second == nullptr) {
            continue;
          }
          auto gio = dit->second->get_io();
          if (!gio) {
            continue;
          }
          auto& row = forest.port_root[dname];
          for (const auto& d : gio->get_input_pin_decls()) {
            if (livehd::latch_contract::Design_clocks::name_looks_like_clock(d.name)) {
              row.emplace(d.name, only_root);
            }
          }
          row.emplace("", only_root);  // and its implicit clock
        }
      }
    }

    // Drop the ambiguous entries: no mapping is better than a wrong one.
    for (auto& [dname, row] : forest.port_root) {
      absl::erase_if(row, [](const auto& kv) { return kv.second == "\x03ambiguous"; });
    }
    absl::erase_if(forest.port_root, [](const auto& kv) { return kv.second.empty(); });
    // The forest is built under CANONICAL def keys (the post-'.' entity tail, so
    // the two front-ends' spellings pair), but plan_phases looks a row up by the
    // graph's FULL name -- it only has the graph in hand. Publish both, or every
    // lookup misses and the propagation is silently inert.
    std::vector<std::pair<std::string, absl::flat_hash_map<std::string, std::string>>> full_rows;
    for (const auto& [cn, g] : by_name) {
      if (g == nullptr) {
        continue;
      }
      const std::string full{g->get_name()};
      if (full == cn) {
        continue;
      }
      if (auto it = forest.port_root.find(cn); it != forest.port_root.end()) {
        full_rows.emplace_back(full, it->second);
      }
    }
    for (auto& [full, row] : full_rows) {
      forest.port_root[full] = std::move(row);
    }
    return forest;
  };
  auto dump_forest = [](const char* tag, const livehd::lec::Clock_forest& f) {
    if (std::getenv("LEC_PHASE_PLAN") == nullptr) {
      return;
    }
    for (const auto& [d, row] : f.port_root) {
      for (const auto& [p, r] : row) {
        std::print(stderr, "[LEC_FOREST {}] {} port '{}' -> root '{}'\n", tag, d, p, r);
      }
    }
  };
  // A verdict-cache hit returns before phase planning, so it never consumes a
  // clock forest. Building both forests eagerly made an all-hit incremental run
  // rescan both complete libraries for data that was then discarded. Tasks may
  // miss concurrently; call_once keeps the first miss responsible for the
  // shared build while every later miss reads the completed immutable maps.
  livehd::lec::Clock_forest ref_forest;
  livehd::lec::Clock_forest impl_forest;
  std::once_flag            forest_once;
  auto                      ensure_forests = [&]() {
    std::call_once(forest_once, [&]() {
      ref_forest  = build_forest(ref_by_name, canon_ref, ref_top_g);
      impl_forest = build_forest(impl_by_name, canon_impl, impl_top_g);
      // Forest roots are TOP input names: record the ones both tops declare so
      // the per-def root-agreement check can tell two top clocks apart
      // (Clock_forest::top_inputs_both).
      if (ref_top_io && impl_top_io) {
        absl::flat_hash_set<std::string> ref_in;
        for (const auto& d : ref_top_io->get_input_pin_decls()) {
          ref_in.insert(std::string(d.name));
        }
        for (const auto& d : impl_top_io->get_input_pin_decls()) {
          if (ref_in.contains(std::string(d.name))) {
            ref_forest.top_inputs_both.insert(std::string(d.name));
          }
        }
        impl_forest.top_inputs_both = ref_forest.top_inputs_both;
      }
      dump_forest("ref", ref_forest);
      dump_forest("impl", impl_forest);
    });
  };

  // Per-side digest resolvers for the hierarchical (Merkle) canonical digest: a
  // Sub's body resolves within its OWN side first (gids are name-hash stable,
  // and the two sides may hold different bodies under one name), then the
  // shared --lib models.
  absl::flat_hash_map<hhds::Gid, hhds::Graph*> ref_gid2g, impl_gid2g;
  if (vcache != nullptr) {
    for (auto& g : ref_var.graphs) {
      if (g) {
        ref_gid2g[g->get_gid()] = g.get();
      }
    }
    for (auto& g : impl_var.graphs) {
      if (g) {
        impl_gid2g[g->get_gid()] = g.get();
      }
    }
    if (sub_lib != nullptr) {
      for (const auto& [gid, gp] : *sub_lib) {
        ref_gid2g.try_emplace(gid, gp);
        impl_gid2g.try_emplace(gid, gp);
      }
    }
  }
  livehd::semdiff::Digest_resolver ref_dres = [&ref_gid2g](hhds::Gid gid) -> hhds::Graph* {
    auto it = ref_gid2g.find(gid);
    return it == ref_gid2g.end() ? nullptr : it->second;
  };
  livehd::semdiff::Digest_resolver impl_dres = [&impl_gid2g](hhds::Gid gid) -> hhds::Graph* {
    auto it = impl_gid2g.find(gid);
    return it == impl_gid2g.end() ? nullptr : it->second;
  };
  // Precompute digests before the parallel proof DAG. The memoized walk is fast,
  // and keeping it single-threaded avoids racing its shared Merkle memo maps.
  absl::flat_hash_map<hhds::Gid, livehd::semdiff::Canonical_digest> ref_dmemo, impl_dmemo;
  std::vector<livehd::semdiff::Canonical_digest>                    ref_digest(order.size()), impl_digest(order.size());
  if (vcache != nullptr) {
    for (size_t i = 0; i < order.size(); ++i) {
      ref_digest[i]  = livehd::semdiff::canonical_digest(ref_by_name[order[i]], ref_dres, ref_dmemo);
      impl_digest[i] = livehd::semdiff::canonical_digest(impl_by_name[order[i]], impl_dres, impl_dmemo);
    }
  }

  // One task per def. Child->parent edges preserve the exact leaves-first
  // collapse semantics while independent leaves run concurrently.
  absl::flat_hash_map<std::string, size_t> order_ix;
  for (size_t i = 0; i < order.size(); ++i) {
    order_ix.emplace(order[i], i);
  }
  std::vector<uint8_t>                         proven(order.size(), 0);  // each slot written by its owning task
  // Budget scheduler: `settled` marks a def whose verdict is DEFINITIVE (Proven or
  // Refuted), for the straggler diagnosis. When `budget_on` (timeout>0,
  // no compiler resource cap, >1 def), `base.timeout` is a soft TOTAL wall-clock budget for
  // the DAG. Concurrent workers share elapsed time; their solve times are not
  // added together. The TOP def is exempt and keeps the full cap. A 1s floor once
  // spent gives a straggler a quick attempt. It is a HINT/target — a bit over
  // or under is fine. Off ⇒ each def
  // keeps the full base.timeout per-query cap (pre-scheduler behavior).
  std::vector<uint8_t>                         settled(order.size(), 0);
  bool                                         budget_on = false;
  const auto                                   budget_start = std::chrono::steady_clock::now();
  std::atomic<int>                             defs_floored{0};  // defs dispatched past the soft total, on the min_timeout floor
  std::atomic<int>                             defs_solved{0};   // defs actually handed to the solver (the "units" of the report)
  livehd::lec::Query_result                    top_result;
  // lhd.stats: the RUN total. Every def gets its own cvc5::Solver (several,
  // under the portfolio), so no single Query_result holds the run's effort —
  // fold each def's accounting in as it resolves, under report_mutex.
  livehd::lec::Cvc5_stats                      run_cvc5;
  std::vector<std::pair<std::string, int64_t>> cvc5_by_def;  // (def, conflicts), for the `hot` ranking
  bool                                         have_top = false;
  std::atomic<int>          semdiff_count{0};                        // defs dropped structurally (no solver)
  std::atomic<int>          cache_count{0};                          // defs settled by the verdict cache (no analysis at all)
  std::vector<uint8_t>      by_cache(order.size(), 0);               // which def, for the closure-intersected summary
  std::vector<uint8_t>      by_semdiff(order.size(), 0);
  std::atomic<int>          trusted_count{0};  // defs ASSUMED equal (formal.lec.trust; never solved)
  std::mutex                report_mutex;
  // Refuted-descendant bookkeeping (fail-fast). `refuted` marks a def the solver
  // itself refuted; `tainted` marks one skipped because a child (transitively) was
  // — kept apart so a skip is never mistaken for a verdict this def earned.
  // first_refuted is the run's headline: the DAG is leaves-first, so the earliest
  // refute is the deepest, and its counterexample is the most actionable one.
  std::vector<uint8_t>      refuted(order.size(), 0);
  std::vector<uint8_t>      tainted(order.size(), 0);
  livehd::lec::Query_result refuted_result;
  std::string               refuted_def;
  bool                      have_refuted = false;
  // Per-def refutations, so the escalation rounds can RE-PICK the run verdict:
  // a block CEX that its parent absorbed (the parent re-proved with the block
  // INLINED) must never surface as the design's answer.
  absl::flat_hash_map<size_t, livehd::lec::Query_result> refuted_by_ix;
  // The cause (TIMEOUT, UNSUPPORTED, ...) of each def that ended undecided, so a top that
  // proved only with boxed children can name WHY its premises were never discharged.
  absl::flat_hash_map<size_t, std::string> undecided_cause_by_ix;

  // ── TOP-DOWN bookkeeping (formal.lec.hier_order=top_down, the default) ─────
  // Bottom-up boxes only a child ALREADY proven; top-down boxes EVERY child and
  // discharges the assumption from the same pass's other entries. See the
  // closure below and pass/lec/README.md §1.
  //
  //   assumed[i]   the child keys def i boxed WITHOUT a proof in hand. Empty in
  //                bottom-up (a box there is always a discharged premise).
  //   force_flat[i] children this def must DESCEND rather than box. Seeded by
  //                the escalation rounds: when child C is refuted OR
  //                inconclusive, its parent re-proves with C inlined. This is
  //                the ONLY step needed when the parent proves, because the
  //                parent's conditional proof already established that
  //                everything else about it matches.
  //   force_flat_refuted[i] the subset of force_flat caused by a concrete child
  //                refutation. A parent PROVEN while absorbing one of these
  //                still gets the mandatory flat confirmation; expanding an
  //                Unknown child has no counterexample to confirm.
  // A def whose Proven verdict is only BOUNDED. It may NOT discharge a parent's
  // box premise: the sequence-transducer contract a box stands for is
  // explicitly unbounded ("from reset, identical input sequences produce
  // identical output sequences"), and a k-cycle claim does not establish it. A
  // child that diverges at cycle bound+1 would otherwise compose into an
  // unbounded top proof with no caveat on the run's verdict line.
  std::vector<uint8_t>                                       bounded_proof(order.size(), 0);
  // The checked depth (BMC steps) of a def's OWN bounded proof, and the depth
  // its composition reaches (the minimum over itself and every bounded premise
  // below it): a composed bounded proof is only as deep as its shallowest leg,
  // and that is the n of the top's PROVEN-bounded(n). 0 = not bounded.
  std::vector<int>                                           own_bounded_steps(order.size(), 0);
  std::vector<int>                                           composed_bounded_steps(order.size(), 0);
  std::vector<std::vector<std::string>>                      assumed(order.size());
  //   descended[i] children def i DESCENDED because the implementation parent has
  //                no occurrence of them (asymmetric inlining), or because a
  //                successful flat confirmation expanded every child. Such a child was
  //                already flat on both sides inside def i's own proof, so an
  //                escalation round has nothing left to inline there: re-solving
  //                it is a bit-identical query, and the force_flat_refuted it
  //                would record flips the def into the proven-absorbing flat
  //                confirmation (a second, fully flat solve of the whole subtree
  //                whose Unknown DEMOTES a settled PROVEN). Written in-task and
  //                read only with no task in flight, like `assumed`.
  std::vector<absl::flat_hash_set<std::string>>              descended(order.size());
  std::vector<absl::flat_hash_set<std::string>>              force_flat(order.size());
  std::vector<absl::flat_hash_set<std::string>>              force_flat_refuted(order.size());
  // `refuted` for OTHER defs, snapshotted between rounds. run_def reads a
  // grandchild's status when deciding what to keep boxed during an escalation,
  // and two parents in the same retry round can run concurrently — reading the
  // live array would be a race on another task's slot (and would make the round
  // depend on scheduling). Written only while no task is in flight.
  std::vector<uint8_t>                                       refuted_snapshot(order.size(), 0);
  // Parents of a def key (reverse of `children`), for the escalation rounds.
  absl::flat_hash_map<std::string, std::vector<std::string>> parents;
  for (const auto& [p, kids] : children) {
    for (const auto& c : kids) {
      parents[c].push_back(p);
    }
  }
  // Per def, under report_mutex: the state each def's proof could not pair.
  std::map<std::string, Lec_state_report> state_reports;
  auto                                    run_def = [&](size_t def_ix) {
    if (settled[def_ix]) {
      return;  // definitively decided in an earlier round — do not re-solve
    }
    const auto& name = order[def_ix];

    // A non-top definition with no output ports has no hardware observation at
    // its instance boundary. Its inputs may feed simulation-only DPI calls or
    // dead internal logic, but no value can flow back into the parent, so the
    // two definitions are relationally equivalent regardless of their bodies.
    // Treat that as a structural proof and discharge the parent's box premise.
    // Keep the selected top excluded: `lhd lec --top empty` has no observable
    // contract and must retain the normal "nothing compared" refusal instead
    // of becoming a vacuous whole-design PASS.
    const auto ref_io  = ref_by_name[name]->get_io();
    const auto impl_io = impl_by_name[name]->get_io();
    if (name != top_key && ref_io != nullptr && impl_io != nullptr && ref_io->get_output_pin_decls().empty()
        && impl_io->get_output_pin_decls().empty()) {
      livehd::lec::Query_result r;
      r.verdict          = Verdict::Proven;
      r.engine           = "structural";
      r.elapsed_ms       = 0;
      r.detail           = "no observable output ports; the definition cannot affect its parent";
      proven[def_ix]     = 1;
      settled[def_ix]    = 1;
      by_semdiff[def_ix] = 1;
      semdiff_count.fetch_add(1, std::memory_order_relaxed);
      std::lock_guard report_lock(report_mutex);
      emit_lec_block_progress(name, r, base, 0);
      std::print("lec[hier]: '{}' PROVEN (no observable output ports)\n", name);
      return;
    }

    // formal.lec.trust: a def ASSUMED equal WITHOUT a proof — the escape hatch for
    // a cell the encoder cannot model yet (a Latch). Skip solving it entirely and
    // mark it proven-by-assumption so parents black-box it via the proven-child
    // path below; it is NOT written to the verdict cache (an assumption, not a
    // verdict). The top is never trusted (refused at the CLI). A latch inside an
    // UNtrusted def still refuses — only listed defs are excused.
    if (name != top_key && is_trusted(name)) {
      proven[def_ix]  = 1;
      settled[def_ix] = 1;
      trusted_count.fetch_add(1, std::memory_order_relaxed);
      std::lock_guard report_lock(report_mutex);
      livehd::diag::info("pass.lec", "lec-block-trusted", "progress")
          .msg("lec block '{}' trusted", name)
          .verdict("trusted")
          .attr("detail", "assumed equal WITHOUT proof (formal.lec.trust): the encoder does not model a cell in it")
          .emit();
      std::print("lec[hier]: '{}' TRUSTED (assumed equal, NOT proven)\n", name);
      return;
    }

    // Fail-fast on a refuted descendant (formal.lec.hier_refute=fail, a DEBUG
    // mode; the default is escalate, and the run verdict of this mode is
    // REFUTED-unchecked: it forbade the escalation that would decide the top).
    // Only a PROVEN child black-boxes (see the collapse set below), so a def over
    // a REFUTED child would descend into logic already known to differ and grind
    // out a whole-design flat miter — minutes of cvc5 for a verdict the child
    // settled in milliseconds. Skip it, and taint it so its own parents skip too.
    // The child's counterexample stands as the run verdict (aggregate below).
    //   formal.lec.hier_refute=escalate restores the full top-level confirmation: a
    // block-boundary CEX can be UNREACHABLE in context, so only that mode can
    // prove a top equivalent over a differing child — at the cost of the flat solve.
    if (fail_fast_refute) {
      std::string bad_kid;
      if (auto it = children.find(name); it != children.end()) {
        for (const auto& c : it->second) {
          if (auto ci = order_ix.find(c); ci != order_ix.end() && (refuted[ci->second] != 0 || tainted[ci->second] != 0)) {
            bad_kid = c;
            break;
          }
        }
      }
      if (!bad_kid.empty()) {
        tainted[def_ix] = 1;
        std::lock_guard report_lock(report_mutex);
        std::print("lec[hier]: '{}' SKIPPED (child '{}' REFUTED; --set formal.lec.hier_refute=escalate proves this level anyway)\n",
                   name,
                   bad_kid);
        return;
      }
    }
    livehd::lec::Lec_options o = base;
    o.design_assumes           = base.design_assumes && assume_cones.contains(name);
    if (budget_on && name != top_key) {
      // Each def gets the elapsed wall allowance left when dispatched.
      // Parallel workers do not consume one another's time allowance.
      //   The TOP def is exempt: it is the proof the user actually asked for,
      // and it is scheduled LAST (leaves-first DAG), exactly where a soft
      // total budget has nothing left. Starving it turns the whole run
      // UNKNOWN after every block proved. One full per-query cap for one def
      // keeps the run bounded (~2x timeout worst case), matching the
      // pre-scheduler behavior a standalone --top run gives it.
      const long long spent_s
          = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - budget_start).count();
      const long long remaining_s = static_cast<long long>(base.timeout) - spent_s;
      const long long floor_s     = std::max<long long>(1, base.min_timeout);
      if (remaining_s < floor_s) {
        defs_floored.fetch_add(1);  // this def is running on the floor, past the soft total
      }
      o.timeout = static_cast<int>(std::max<long long>(floor_s, remaining_s));
    }
    if (name != top_key && base._deadline != std::chrono::steady_clock::time_point{}) {
      // A hard child must leave time to check its caller in context. This is a
      // sub-deadline within the shared run allowance, never additional time.
      const auto now = std::chrono::steady_clock::now();
      if (now < base._deadline) {
        o._deadline = now + (base._deadline - now) / 2;
      }
    }
    if (name != top_key) {
      // Sidecar paths are resolved against the selected impl top. Descendant
      // defs prove their unconditional contracts; only the top miter consumes
      // the accepted impl-side helper facts.
      o.assumptions = nullptr;
      o.assumption_key.clear();
      o.proven_helpers = o.input_assumes = o.unchecked_assumes = o.proven_assumes = 0;
    }
    // Each def is LEC'd under the requested engine (formal.engine, default `auto` =
    // the ind+bmc portfolio). Honor an explicit engine so `--set formal.engine=bmc`
    // (e.g. a reset-phase proof) is not silently overridden by the hierarchical driver.

    // Effective collapse set FIRST — it is part of the cache key: a proven
    // child black-boxes; a non-proven child is left OUT of the collapse set ->
    // flattened (descended).
    o.collapse.clear();
    // TRUSTED defs are always boxed, at ANY depth in this cone: the encoder
    // matches a collapse name hierarchically, so seeding the whole trust list
    // also covers a trusted grandchild reached through a flattened (non-proven)
    // intermediate. These boxes are NOT confirmed away in the flat retry below —
    // they cover the cell the encoder cannot model.
    o.collapse.insert(o.collapse.end(), base.trust.begin(), base.trust.end());
    std::vector<std::string> coll;  // SPECULATIVE child boxes (the retry confirms these; trust is excluded)
    bool                     kids_proven = true;
    assumed[def_ix].clear();
    descended[def_ix].clear();
    if (auto it = children.find(name); it != children.end()) {
      for (const auto& c : it->second) {
        if (is_trusted(c)) {
          continue;  // already force-boxed via the trust seed above; an assumption, so it never flips kids_proven
        }
        auto       ci                  = order_ix.find(c);
        const bool is_proven           = ci != order_ix.end() && proven[ci->second] != 0;
        // A definition can survive in both libraries even after its instances
        // were inlined on one side. Boxing that one-sided boundary creates
        // unmatched UF cut points and sends an easy combinational comparison
        // into BMC. Descend when the implementation parent has no occurrence,
        // including below any hierarchy/rolled-loop wrappers.
        const auto compatible_children = box_children.find(name);
        if (compatible_children == box_children.end() || !compatible_children->second.contains(c)) {
          kids_proven = kids_proven && is_proven;
          descended[def_ix].insert(c);
          continue;
        }
        // TOP-DOWN: box the child whether or not it is proven YET. The premise
        // "this child pair is equivalent" is discharged by that child's OWN
        // entry in this same pass — the module DAG is well-founded, so the
        // composition is an induction, not circular reasoning. Bottom-up boxes
        // only what it has already discharged.
        //   `force_flat` overrides: an escalation round descends the ONE child
        // whose refutation this def has to absorb or whose inconclusive proof it
        // has to discharge in context.
        const bool want_box = (top_down || is_proven) && force_flat[def_ix].count(c) == 0 && !assume_cones.contains(c);
        if (want_box) {
          // A child must NOT collapse when its ref/impl port sets diverge the
          // tuple-leaf <-> flat-bus way (Pyrope `req.a`/`req.b` leaves vs one
          // packed `req` bus): box correspondence is keyed on port NAMES, so the
          // box would gate this parent to incomplete (Unknown). Leaving it out
          // of the collapse set descends (flattens) it into the parent, where
          // prove_equal's bundle compare points at the parent's own boundary
          // take over. (Read-only probe of two maps populated before the task
          // DAG runs — safe under the parallel run.)
          auto rg = ref_by_name.find(c);
          auto ig = impl_by_name.find(c);
          if (rg != ref_by_name.end() && ig != impl_by_name.end() && livehd::lec::io_bundle_split(rg->second, ig->second)) {
            kids_proven = kids_proven && is_proven;  // descended: it is not a discharged box here
            continue;                                // box-incompatible port shapes -> descend it
          }
          o.collapse.push_back(c);
          coll.push_back(c);
          if (top_down) {
            // Every box is a PREMISE here, discharged by the closure below.
            // Recording only the not-yet-proven ones would make the set depend
            // on which worker finished first (lhd.jobs), and the parallel
            // determinism gate compares per-def output verbatim.
            assumed[def_ix].push_back(c);
          }
        } else {
          kids_proven = false;
          // ESCALATION: `c` is being INLINED into this def to absorb its
          // refutation or discharge its Unknown boundary in context. Only `c`
          // itself has to be expanded — its own children stay BOXED, so the
          // miter grows by one def's logic and not by a whole subtree. (The
          // encoder matches a collapse name hierarchically, so a grandchild
          // named here is boxed even though it is reached through the flattened
          // `c`.) They are premises like any other box, so they join `assumed`
          // and the closure has to discharge them too.
          if (top_down && force_flat[def_ix].count(c) > 0) {
            if (auto gk = children.find(c); gk != children.end()) {
              for (const auto& g : gk->second) {
                auto       gi               = order_ix.find(g);
                const auto grandchild_boxes = box_children.find(c);
                if (grandchild_boxes == box_children.end() || !grandchild_boxes->second.contains(g)) {
                  continue;  // asymmetric inlining also applies during escalation
                }
                if (gi == order_ix.end() || force_flat[def_ix].count(g) > 0 || refuted_snapshot[gi->second] != 0
                    || assume_cones.contains(g)) {
                  continue;  // itself being absorbed, or known-different: descend it too
                }
                auto rg = ref_by_name.find(g);
                auto ig = impl_by_name.find(g);
                if (rg != ref_by_name.end() && ig != impl_by_name.end() && livehd::lec::io_bundle_split(rg->second, ig->second)) {
                  continue;
                }
                if (std::find(coll.begin(), coll.end(), g) != coll.end()) {
                  continue;
                }
                o.collapse.push_back(g);
                coll.push_back(g);
                assumed[def_ix].push_back(g);
              }
            }
          }
        }
      }
    }
    // The structural-identity no-solver skip needs every child ACCOUNTED FOR,
    // not necessarily already proven: under top-down a boxed child is a premise
    // the closure discharges, so "identical modulo boxed children" is a real
    // (conditional) proof. A DESCENDED child still blocks it — its internals are
    // inside this miter and semdiff compared them structurally, which is only a
    // proof if the child itself was settled.
    if (top_down) {
      kids_proven = true;
      if (auto it = children.find(name); it != children.end()) {
        for (const auto& c : it->second) {
          auto       ci    = order_ix.find(c);
          const bool boxed = is_trusted(c) || std::find(coll.begin(), coll.end(), c) != coll.end();
          if (!boxed && !(ci != order_ix.end() && proven[ci->second] != 0)) {
            kids_proven = false;
          }
        }
      }
    }

    // Tier-2 pair-hint replay (cache record kind 5) — BEFORE the cache key:
    // replayed pairs alter the obligation set, so they are part of the key
    // (um=[...]); a warm run that re-injects the same validated pair set both
    // hits the stored verdict AND skips the signature pass entirely. Any
    // dropped pair marks the hint stale — discard it all and fall through to a
    // fresh signature pass (the fresh pairs re-key below).
    bool pairs_from_hint = false;
    if (o.state_pairing && vcache != nullptr) {
      if (auto ph = vcache->pair_hint(name); ph.has_value()) {
        std::vector<std::string> dropped;
        auto valid = livehd::lec::validate_uncertain_pairs(ref_by_name[name], impl_by_name[name], o, ph->pairs, &dropped);
        if (dropped.empty() && !valid.empty()) {
          o.uncertain_match = std::move(valid);
          pairs_from_hint   = true;
        }
      }
    }

    auto record_flat_proof = [&] {
      assumed[def_ix].clear();
      if (auto it = children.find(name); it != children.end()) {
        descended[def_ix].insert(it->second.begin(), it->second.end());
      }
    };

    // 2f-fcore verdict cache: digest-equal def-pair (hierarchical Merkle
    // digest) + identical verdict-relevant options => the stored PROVEN
    // verdict transfers. A hit needs no encode and no solver — an unchanged
    // submodule is instantaneous. Undigestable graphs (anonymous state cell)
    // simply skip the cache. Re-checked after fresh tier-2 pairs change the
    // key (the pair set is inside it).
    std::string ckey;
    auto        cache_settles = [&]() -> bool {
      if (vcache == nullptr) {
        return false;
      }
      const auto& dr = ref_digest[def_ix];
      const auto& di = impl_digest[def_ix];
      if (!dr.valid || !di.valid) {
        return false;
      }
      ckey = lec_pair_cache_key(dr, di, o);
      // Prefer a proof with no speculative boxes over a conditional one.
      auto flat_options = o;
      flat_options.collapse.assign(base.trust.begin(), base.trust.end());
      const auto flat_key = lec_pair_cache_key(dr, di, flat_options);
      auto       hit      = vcache->lookup(flat_key);
      if (hit.has_value()) {
        ckey = flat_key;
        record_flat_proof();
      } else if (flat_key != ckey) {
        hit = vcache->lookup(ckey);
      }
      if (hit.has_value()) {
        livehd::lec::Query_result cr;
        cr.verdict                = Verdict::Proven;
        cr.bounded                = hit->bounded;
        cr.checked_steps          = hit->bounded ? hit->depth : 0;
        bounded_proof[def_ix]     = hit->bounded ? 1 : 0;
        own_bounded_steps[def_ix] = hit->bounded ? hit->depth : 0;
        cr.engine                 = "cache";
        cr.elapsed_ms             = 0;
        cr.detail                 = lec_cache_hit_detail(ckey, *hit);
        proven[def_ix]            = 1;
        by_cache[def_ix]          = 1;
        ++cache_count;
        std::lock_guard report_lock(report_mutex);
        emit_lec_block_progress(name, cr, o, 0);
        std::print("lec[hier]: '{}' {} (cache)\n",
                   name,
                   hit->bounded ? livehd::lec::outcome_word(livehd::lec::Outcome::Proven_bounded,
                                                            design_cycles(hit->depth, edge_slots))
                                : livehd::lec::outcome_word(livehd::lec::Outcome::Proven_full));
        if ((name == top_key)) {
          top_result = cr;
          have_top   = true;
        }
        return true;  // no analysis at all for this def
      }
      // Known-timeout ledger: an unchanged def that already ran out of TIME at
      // this (or a larger) budget skips the re-grind — it still REPORTS
      // TIMEOUT-time, exactly as a re-run would; no verdict is transferred, and
      // only a TIMEOUT-time result is ever ledgered. A digest/option change, a larger
      // formal.timeout, a prover change (salt), or --set formal.retry=all
      // re-attempts.
      // base.timeout, NOT o.timeout: the ledger asks "did a run with THIS MUCH
      // budget already fail here?", and the user's budget is base.timeout. The
      // per-def o.timeout is `base.timeout - wall spent so far`, a SCHEDULING
      // artifact -- so a def that dispatched 1s into the cold run recorded 119
      // while its neighbour recorded 120, and on a warm run (everything ahead of
      // it now a cache hit, spent~0) both ask for 120: the neighbour skips and it
      // does not. Self-defeating, too -- the better the cache works, the more
      // budget is left, the less the ledger fires. Measured on minion: 169/169
      // verdicts hit, and one 127s def re-ground anyway on sub-second jitter.
      if (!retry_all && vcache->skip_timeout(ckey, base.timeout)) {
        livehd::lec::Query_result ur;
        ur.verdict    = Verdict::Undecided;
        ur.engine     = "cache-skip";
        ur.elapsed_ms = 0;
        ur.detail
            = std::format("known timeout at formal.timeout<={}s with unchanged digest/options; --set formal.retry=all re-attempts",
                          o.timeout);
        std::lock_guard report_lock(report_mutex);
        emit_lec_block_progress(name, ur, o, 0);
        std::print("lec[hier]: '{}' {} (skipped: known timeout; formal.retry=all re-attempts)\n",
                   name,
                   livehd::lec::outcome_word(livehd::lec::Outcome::Timeout_time));
        if ((name == top_key)) {
          top_result = ur;
          have_top   = true;
        }
        return true;
      }
      return false;
    };
    if (cache_settles()) {
      return;
    }

    uint64_t preparation_units = 0;
    for (auto* graph : {ref_by_name[name], impl_by_name[name]}) {
      for ([[maybe_unused]] const auto node : graph->body().nodes()) {
        ++preparation_units;
      }
    }
    auto preparation_slot = livehd::Job_manager::global().acquire(livehd::Job_class::formal_arm, preparation_units, 0, o._deadline);
    if (!preparation_slot) {
      livehd::lec::Query_result queued;
      queued.verdict     = Verdict::Undecided;
      queued.engine      = o.engine;
      queued.wall_killed = true;
      queued.detail      = "hit formal.timeout while queued for definition preparation";
      std::lock_guard report_lock(report_mutex);
      emit_lec_block_progress(name, queued, o, 0);
      std::print("lec[hier]: '{}' {} (queued past formal.timeout)\n", name, livehd::lec::outcome_word(livehd::lec::Outcome::Timeout_time));
      if (name == top_key) {
        top_result = queued;
        have_top   = true;
      }
      return;
    }
    // Fork dispatch yields this preparation slot while its managed arms run.

    // M3 structural def-diff reduction: a def whose ref/impl are structurally
    // IDENTICAL (no unmatched node on either side) and whose children are ALL
    // proven is equivalent with NO solver call. A parent's own-structure match
    // does NOT cover a child's internals (the child Sub matches by name regardless),
    // so require the children proven first — leaves-first guarantees they are settled.
    // The same structural_match call doubles as the tier-2 producer
    // (state_pairing): its full-match signature pass pairs the state cells
    // tier-1 names left unmatched, and the surviving pairs are injected as
    // UNCERTAIN correspondence (2f-lec discipline enforced inside prove_equal).
    const bool               want_pairing = o.state_pairing && !pairs_from_hint;
    // semdiff's still-unpaired state, kept past its scope for the result report.
    std::vector<std::string> state_unpaired_ref, state_unpaired_impl;
    auto                     has_direct_loop = [](hhds::Graph* graph) {
      if (graph == nullptr) {
        return false;
      }
      for (const auto node : graph->body().nodes()) {
        if (node.is_loop_subnode()) {
          return true;
        }
      }
      return false;
    };
    const bool ref_loop        = has_direct_loop(ref_by_name[name]);
    const bool impl_loop       = has_direct_loop(impl_by_name[name]);
    const bool mixed_loop_repr = ref_loop != impl_loop;
    // The lifted loop body lives on ONE side only, so it never appears in
    // `children` (built from the defs present on BOTH sides) and therefore never
    // forces `kids_proven` false by itself: the normalization below can keep the
    // ordinary hierarchical-child gate.
    if ((o.semdiff != "none" && kids_proven) || want_pairing) {
      auto                             t0 = std::chrono::steady_clock::now();
      livehd::semdiff::Semdiff_options so;
      so.matching_names             = true;  // anchor flops/mems by hier name (lec's correspondence basis)
      so.state_pairing              = want_pairing;
      so.project_state_bits         = want_pairing;
      so.seed_pairs                 = o.match;  // explicit formal.lec.match pairs are tier-1 anchors for the signatures
      // Defs run CONCURRENTLY (dispatch_dag below: top_down emits no edges at
      // all, and bottom_up still runs siblings in parallel) over graphs they all
      // share, and a parent's mixed-loop normalization deep-copies its children's
      // bodies out of the same library. semdiff must therefore not stamp: a
      // `match` write grows the shared graph's attr_stores_ under another
      // worker's clone_attr_stores_from. Nothing in this driver reads the mark.
      so.stamp_matches              = false;
      // `kids_proven` is as load-bearing here as it is for the plain structural
      // skip below: the normalization inlines ONLY the lifted loop bodies, so
      // every other child Sub stays an opaque node matched by name/def identity
      // alone. Without it a def holding a rolled loop next to an UNKNOWN (or
      // REFUTED) child would be cached Proven on a child proof never made.
      bool virtual_loop_fold        = false;
      bool normalized_loop_identity = false;
      if (o.semdiff != "none" && kids_proven && !o.design_assumes && mixed_loop_repr) {
        auto* compact            = ref_loop ? ref_by_name[name] : impl_by_name[name];
        auto* unrolled           = ref_loop ? impl_by_name[name] : ref_by_name[name];
        virtual_loop_fold        = livehd::semdiff::folded_loop_identical(compact, unrolled, /*stamp_matches=*/false);
        normalized_loop_identity = virtual_loop_fold || mixed_loop_structural_identity(compact, unrolled, so);
      }
      if (normalized_loop_identity) {
        const long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
        livehd::lec::Query_result sr;
        sr.verdict         = Verdict::Proven;
        sr.engine          = "semdiff";
        sr.elapsed_ms      = ms;
        sr.detail          = virtual_loop_fold
                                 ? "structurally identical by compact-loop virtual-occurrence fold (no solver call)"
                                 : "structurally identical after compact-loop materialize+inline normalization (no solver call)";
        proven[def_ix]     = 1;
        by_semdiff[def_ix] = 1;
        ++semdiff_count;
        {
          std::lock_guard report_lock(report_mutex);
          emit_lec_block_progress(name, sr, o, ms);
          if (virtual_loop_fold) {
            std::print("lec[hier]: '{}' MATCHED (semdiff compact-vs-unrolled virtual-occurrence fold, no solver)\n", name);
          } else {
            std::print("lec[hier]: '{}' MATCHED (semdiff compact-vs-unrolled normalization, no solver)\n", name);
          }
        }
        if (vcache != nullptr && !ckey.empty()) {
          vcache->insert(ckey, {sr.engine, sr.detail, ms, sr.bounded});
        }
        if (name == top_key) {
          top_result = sr;
          have_top   = true;
        }
        return;
      }
      auto            m  = livehd::semdiff::structural_match(ref_by_name[name], impl_by_name[name], so);
      const long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
      if (want_pairing) {
        state_unpaired_ref  = m.a_state_unpaired;
        state_unpaired_impl = m.b_state_unpaired;
      }
      // 2f-lec diverged-use guard: memories semdiff flagged as genuinely diverged
      // (kind/init mismatch or no counterpart) must not be force-collapsed.
      o.mem_diverged.clear();
      o.mem_diverged.insert(o.mem_diverged.end(), m.a_mem_diverged.begin(), m.a_mem_diverged.end());
      o.mem_diverged.insert(o.mem_diverged.end(), m.b_mem_diverged.begin(), m.b_mem_diverged.end());
      // The no-solver skip stays anchored to CERTAIN correspondence only: a
      // "structurally identical" verdict that leans on a speculative tier-2
      // pair or an explicit seed is not taken (the spec self-certifies only
      // the unbounded inductive proof) — those defs go to the solver.
      // cut_violated/cut_unknown are NOT optional: a_unmatched==0 is a node-set
      // BIJECTION, not an isomorphism. A cut point's fsig is its name seed and never
      // folds its din, and class_of is forward-authoritative, so swapping two
      // same-named flops' dins (or two graph outputs) leaves the node set identical
      // and would be claimed Proven here — with no solver, and cached below as
      // definitive. The obligations close exactly that hole. The predicate is
      // is_structural_identity (semdiff.hpp) -- the SAME one structural_identical()
      // and abc's reuse gate read, so this soundness-critical skip cannot drift.
      // A bus_name-reconstructed pair (ref `g` vs the impl's `DFF g` / `\g.l` /
      // `\g_cgen1.l`, state.name_reconstructed) may take the skip: the rename is
      // only a hint, and semdiff keys each state obligation by the seed the pair
      // matched through, so its din/enable/reset/initial are compared like any
      // name pair's. Keyed by the raw names they were both one-sided and never
      // checked, and swapped dins or a changed reset constant PROVED here
      // (pass/semdiff/tests/lec_name_hint_test.sh).
      // A node-set bijection says nothing about the two INTERFACES. Two defs can
      // match structurally while declaring a top port at different widths or
      // signedness (`input signed [1:0] a` vs `input a`): the solver path
      // reconciles that to the domain BOTH declarations can hold and DISCLOSES
      // it in the verdict, because a proof there does not cover values only the
      // wider side admits. A bare "structurally identical" would drop that
      // qualifier, so hand such a pair to the solver instead. Only the skip is
      // declined -- the def is still proven, just with the disclosure attached.
      const bool same_interface = [&] {
        auto* a = ref_by_name[name];
        auto* b = impl_by_name[name];
        if (a == nullptr || b == nullptr || a->get_io() == nullptr || b->get_io() == nullptr) {
          return true;  // nothing to compare; the existing guards decide
        }
        auto same = [](const auto& da, const auto& db) {
          if (da.size() != db.size()) {
            return false;
          }
          for (const auto& pa : da) {
            bool found = false;
            for (const auto& pb : db) {
              if (pa.name != pb.name) {
                continue;
              }
              found = pa.bits == pb.bits && pa.unsign == pb.unsign;
              break;
            }
            if (!found) {
              return false;
            }
          }
          return true;
        };
        return same(a->get_io()->get_input_pin_decls(), b->get_io()->get_input_pin_decls())
               && same(a->get_io()->get_output_pin_decls(), b->get_io()->get_output_pin_decls());
      }();
      if (o.semdiff != "none" && kids_proven && !o.design_assumes && same_interface && livehd::semdiff::is_structural_identity(m)) {
        livehd::lec::Query_result sr;
        sr.verdict         = Verdict::Proven;
        sr.engine          = "semdiff";
        sr.elapsed_ms      = ms;
        sr.detail          = std::format("structurally identical (structural: {} matched node(s), no solver call)", m.a_matched);
        proven[def_ix]     = 1;
        by_semdiff[def_ix] = 1;
        ++semdiff_count;
        {
          std::lock_guard report_lock(report_mutex);
          emit_lec_block_progress(name, sr, o, ms);
          std::print("lec[hier]: '{}' MATCHED (semdiff structural, no solver)\n", name);
        }
        if (vcache != nullptr && !ckey.empty()) {
          vcache->insert(ckey, {sr.engine, sr.detail, ms, sr.bounded});  // a structural match is a definitive Proven
        }
        if ((name == top_key)) {
          top_result = sr;
          have_top   = true;
        }
        return;  // skip the solver for this def
      }
      o._state_projections  = m.state_projections;
      o._memory_projections = m.memory_projections;
      if (want_pairing && !m.state_pairs.empty()) {
        std::vector<std::pair<std::string, std::string>> fresh;
        fresh.reserve(m.state_pairs.size());
        for (auto& p : m.state_pairs) {
          if (p.is_mem) {
            // Memories are never name-aliased, but a confident mem pair lets the
            // diverged-use collapse guard (build_shared_mems) trust an ambiguous
            // shape bucket's occurrence pairing instead of leaving it uncollapsed.
            o.mem_match.emplace_back(std::move(p.a_name), std::move(p.b_name));
          } else {
            fresh.emplace_back(std::move(p.a_name), std::move(p.b_name));
          }
        }
        o.uncertain_match = livehd::lec::validate_uncertain_pairs(ref_by_name[name], impl_by_name[name], o, fresh, nullptr);
      }
      if (want_pairing && (!o.uncertain_match.empty() || !m.a_state_unpaired.empty() || !m.b_state_unpaired.empty())) {
        auto capped = [](const std::vector<std::string>& v, size_t cap = 8) {
          std::string s;
          for (size_t i = 0; i < v.size() && i < cap; ++i) {
            s += (i ? ", " : "") + v[i];
          }
          if (v.size() > cap) {
            s += std::format(", (+{} more)", v.size() - cap);
          }
          return s;
        };
        std::lock_guard report_lock(report_mutex);
        if (!o.uncertain_match.empty()) {
          std::print("lec[hier]: '{}' tier-2 state pairing: {} uncertain pair(s) injected ({} round(s))\n",
                     name,
                     o.uncertain_match.size(),
                     m.state.rounds);
        }
        if (!m.a_state_unpaired.empty() || !m.b_state_unpaired.empty()) {
          std::print("lec[hier]: '{}' tier-2 unpaired state: ref{{{}}} impl{{{}}}\n",
                     name,
                     capped(m.a_state_unpaired),
                     capped(m.b_state_unpaired));
        }
      }
      // Freshly injected pairs are part of the obligation set — re-key and
      // re-check the cache: a prior run's Unknown-ledger entry (or a PROVEN
      // whose pair hint was since lost) under the SAME pair set must still
      // settle this def without re-solving.
      if (!o.uncertain_match.empty() && !pairs_from_hint && cache_settles()) {
        return;
      }
    }

    // Strategy hint (cache record kind 3): replay the case-split selector that
    // WON for this entity last run — heuristic-only ordering (pick_split falls
    // back to auto scoring when the hinted input no longer qualifies), so it
    // can speed the proof but never change a verdict.
    if (vcache != nullptr) {
      if (auto h = vcache->hint(name); h.has_value()) {
        if (o.split == "auto" && !h->split.empty()) {
          o.split = h->split;
        }
        if (o.engine == "auto" && (h->engine == "ind" || h->engine == "bmc")) {
          o._preferred_engine = h->engine;
        }
      }
    }

    // The design-wide clock forest. Carried by VALUE so it survives the isolated
    // worker fork; both sides' rows go in, since plan_phases runs over each
    // design's own graphs and looks each up by that design's def name.
    // One forest PER SIDE: the two designs share def names (the same top on
    // each side), so a merged map let one side's row decide the other side's
    // roots (see Lec_options::clock_forest_impl).
    ensure_forests();
    o.clock_forest      = ref_forest;
    o.clock_forest_impl = impl_forest;
    auto              t0               = std::chrono::steady_clock::now();
    // A Liberty-model (`--lib`) comparison pits a gate-level netlist against
    // RTL. Under proven-child collapse the boxes are an abstraction the cone
    // pass refutes on cuts the flat miter proves (dino PipelinedDualIssueCPU:
    // `nxt:pc`, `io_dmem_address`, `bbin:ALU#n:…:io_inputx` come back abc DIFF
    // collapsed and PROVEN flat), so the collapsed attempt can only ever settle
    // by induction. Its BMC leg can at best produce an abstract refute that
    // needs flat confirmation anyway, while burning exactly the wall clock the
    // `cheap_unknown` retry gate below reads: on dino the collapsed attempt
    // sat under the gate by luck (7.5 s of a 12 s line with an optimized lhd)
    // and a debug build blew through it and lost the def with no retry. So:
    // collapsed attempt ind-only, and its Unknown is an unconditional ground
    // for the flat retry (which keeps the requested engine).
    const bool        netlist_cmp      = sub_lib != nullptr && !sub_lib->empty();
    const std::string requested_engine = o.engine;
    const int         requested_timeout = o.timeout;
    const bool reserve_flat = netlist_cmp && !coll.empty() && o.engine == "auto" && o.timeout > 0 && o.compile_resource_limit == 0;
    long long         recovery_wall_ms  = 0;
    if (netlist_cmp && !coll.empty() && o.engine == "auto") {
      o.engine = "ind";
      if (reserve_flat) {
        recovery_wall_ms = livehd::lec::race_deadline_ms(o);
        if (recovery_wall_ms > 0) {
          recovery_wall_ms   = std::min(recovery_wall_ms, static_cast<long long>(o.timeout) * 1000 * o.hard_timeout_mult);
          o._wall_timeout_ms = std::max(1LL, recovery_wall_ms / 2);
        }
        o.timeout = std::max(1, o.timeout / 2);
      }
    }
    // Single-definition queries also need the parent deadline: a native SAT
    // backend may not return to cvc5's cooperative timeout checks.
    auto r                                 = livehd::lec::prove_equal_isolated(ref_by_name[name], impl_by_name[name], o, sub_lib);
    o.engine                               = requested_engine;
    o.timeout                              = requested_timeout;
    o._wall_timeout_ms                     = 0;
    // Both a REFUTE and an UNKNOWN under proven-child collapse get ONE flat re-solve
    // (collapse cleared, children descended) — for opposite reasons:
    //
    //  REFUTE  — an ABSTRACTION verdict: the box over-approximates the child (free/UF
    //    values the real leaf can never emit, and — for unnamed interchangeable
    //    instances — an occurrence-paired correspondence that may associate different
    //    physical instances), so the counterexample can be spurious. Confirm FLAT
    //    before reporting a fail: flat-Proven is adopted, flat-Unknown stays
    //    inconclusive. A FAIL is then only ever reported from a counterexample free of
    //    proven-child collapse boxes (true blackboxes for UNRESOLVED defs may remain in
    //    the flat run — those correspond explicitly and gate to inconclusive when
    //    one-sided).
    //
    //  UNKNOWN — the collapse can make the parent HARDER, not easier: a UF box widens
    //    the logic to QF_AUFBV and thereby DISABLES cvc5's eager bit-blaster (see
    //    pass/lec/query.cpp — both setLogic and the `bv-solver=bitblast-internal` gate
    //    key off state_boxes/comb_boxes being empty), leaving the slow lazy solver on a
    //    whole-design miter. Measured on dino PipelinedDualIssueCPU: the collapsed BMC
    //    stays UNKNOWN even at a 1500s budget, while the SAME miter flat REFUTES in
    //    ~83s. An Unknown is a non-result, so re-spending the remaining budget flat can
    //    only add information — but adopt the flat run only if it actually SETTLES,
    //    else keep the collapsed detail so the report still names the boxes.
    //
    //    ...but NOT unconditionally, because it is the one retry that can cost more
    //    than the whole rest of the run. Fire it only when it is WORTH it, on
    //    either of two independent grounds:
    //
    //    (a) CHASING A CEX (`force_flat_refuted` non-empty = an escalation round
    //        absorbing a child's known refutation). Here the flat miter is how
    //        the counterexample is found at all: dino PipelinedDualIssueCPU
    //        stays UNKNOWN collapsed and REFUTES flat. force_flat may also be
    //        non-empty for an Unknown child; that still earns the modelling
    //        retry below, but there is no child CEX to flat-confirm.
    //
    //    (b) The collapsed attempt BARELY SPENT ITS BUDGET, so it did not fail for
    //        want of solver time and the retry is cheap. That is the modelling
    //        case, not the capacity one: a boxed child holding a LATCH makes
    //        phase_sched decline ("normalizing across a module boundary is not
    //        supported yet"), both engines return in ~0ms, and flattening the leaf
    //        into the parent is exactly what makes it schedulable
    //        (tests/lec_trust_test.sh: untrusted latch leaf must PROVE).
    //
    //    Neither holds for a def that burned its whole budget with every box a
    //    discharged premise: there the retry is a pure gamble on solver luck.
    //    minion `intpipe_csr_file` (16 boxed children, ALL proven inside 1.1s,
    //    nothing refuted anywhere in the design) spent 45 MINUTES and 13.7 GB there
    //    and still came back UNKNOWN, turning a 6.5s hierarchy into a 47-minute run.
    const bool      refuted_under_collapse = r.verdict == Verdict::Refuted && !coll.empty();
    // "Barely spent it": under a tenth of the per-query cap. A structural refusal
    // returns in ~0ms, so this separates it from a genuine give-up without naming
    // any single refusal reason -- and it scales with whatever formal.timeout is.
    const long long cheap_ms               = o.timeout > 0 ? static_cast<long long>(o.timeout) * 100 : 1000;
    // Use solver time for this retry heuristic, independently of the wall
    // budget. r.elapsed_ms also carries graph
    // transforms, encoding and cvc5 term construction -- LiveHD code, ~9x
    // slower in an unoptimized build while cvc5 itself (prebuilt -O2 in every
    // mode) is unchanged -- so keying off it made the SAME def take the retry
    // under `-c opt` and skip it under `-c dbg`: matched_filter_4's collapsed
    // leg costs 1.5s optimized and 13.1s debug against the same 12s threshold,
    // and the debug build shipped the collapsed UNKNOWN (exit 7) for a def the
    // flat retry proves.
    const bool      cheap_unknown          = r.solve_ms < cheap_ms;
    // A wall kill reports no solver accounting and is not a cheap give-up.
    // Only a deliberately shortened collapsed attempt has reserved recovery
    // time: its kill must not suppress the remaining flat/BMC attempt.
    const bool      unknown_under_collapse = r.verdict == Verdict::Undecided && !coll.empty()
                                        && (!r.wall_killed || reserve_flat)
                                        && (!force_flat[def_ix].empty() || cheap_unknown || netlist_cmp);
    //    (c) ABSORBING a known refutation and coming back PROVEN. This is the one
    //        place a wrong PROVEN silently converts a DEFINITE counterexample into
    //        a run-level pass, so it gets the same flat confirmation (a) already
    //        gives the refute. Case (a) above says dino's PipelinedDualIssueCPU
    //        "stays UNKNOWN collapsed and REFUTES flat" — but the abc cone
    //        pre-pass can discharge every cut of that same collapsed miter and
    //        report PROVEN (it does, on the bug1 variant: 186/186 cones in 33 ms
    //        while the flat run refutes with a concrete CEX). Accepting it skips
    //        the very retry (a) exists for. A collapsed PROVEN with nothing
    //        refuted anywhere is untouched — the common case pays nothing.
    // A parent may have inlined its ONLY child, leaving `coll` empty.  It is
    // still absorbing that child's concrete counterexample, and a merely
    // bounded parent pass must go through the same deep confirmation as the
    // partially-collapsed case.  Keying this on `coll` let the all-inlined
    // matched-filter parent accept PASS(6) even though the tap divergence
    // reaches the top after that window.
    const bool      proven_absorbing       = r.verdict == Verdict::Proven && !force_flat_refuted[def_ix].empty();
    bool            absorb_demoted         = false;  // set when that PROVEN is rejected: nothing may restore it
    if (refuted_under_collapse || unknown_under_collapse || proven_absorbing) {
      {
        std::lock_guard report_lock(report_mutex);
        std::print("lec[hier]: '{}' {} under collapse ({} box def(s)) -> flat {}\n",
                   name,
                   refuted_under_collapse ? "REFUTED"
                   : proven_absorbing   ? "PROVEN while absorbing a refutation"
                                        : livehd::lec::undecided_cause(r.detail, r.unsupported).word,
                   coll.size(),
                   refuted_under_collapse ? "confirmation"
                   : proven_absorbing     ? "confirmation (a collapsed proof may not overrule a child's counterexample)"
                                          : "retry (UF boxes disable the eager bit-blaster)");
      }
      livehd::lec::Lec_options oflat = o;  // o.engine is the requested engine again (the ind-only leg is over)
      // Drop the SPECULATIVE proven-child boxes being confirmed, but KEEP the
      // TRUSTED boxes: they cover a cell the encoder cannot model (a Latch), so
      // re-flattening them would refuse the whole miter (exit 7) and destroy the
      // real counterexample this confirm is meant to validate. Only when the
      // trust list is empty is this the original full clear.
      oflat.collapse.assign(base.trust.begin(), base.trust.end());
      if (reserve_flat) {
        const int       induction_share = std::max(1, requested_timeout / 2);
        const long long spent_s         = r.wall_killed ? induction_share : (r.solve_ms + 999) / 1000;
        oflat._bmc_reserved_timeout     = std::max(1, requested_timeout - induction_share);
        oflat.timeout                   = static_cast<int>(std::max<long long>(std::max(1, requested_timeout - induction_share),
                                                             static_cast<long long>(requested_timeout) - spent_s));
        if (recovery_wall_ms > 0) {
          const auto elapsed_ms
              = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
          oflat._wall_timeout_ms = std::max(1LL, recovery_wall_ms - elapsed_ms);
        }
      }
      // When escalation already inlined every ordinary child, `r` is the flat
      // query we are about to request.  Reuse it instead of spending the same
      // solver budget twice; deepen_if_bounded below is the additional check
      // that matters.
      const bool already_flat = proven_absorbing && coll.empty();
      auto       rf = already_flat ? r : livehd::lec::prove_equal_isolated(ref_by_name[name], impl_by_name[name], oflat, sub_lib);
      // The collapsed run really ran cvc5, so its effort is part of what this def
      // cost: carry it into the survivor BEFORE the move discards `r` (lhd.stats).
      // A BOUNDED flat pass ("no CEX up to bound k") cannot by itself overrule
      // a collapsed REFUTE: it cannot tell "the collapsed counterexample was an
      // artifact of the boxes" (instance_state_anon: mispaired lanes, the
      // flat pass is the rescue) from "the counterexample needs more than k
      // cycles to reach an output through the real children". MEASURED on the
      // lhdsuite matched_filter core with one tap's product broken: `tap`
      // REFUTES, the parent REFUTES under collapse, and the flat 6-cycle BMC
      // after the 70-cycle reset hold sees nothing — the broken product needs
      // LOG2N+2 = 8 cycles to reach `y` — so the run shipped as PASS(6), exit
      // 0, on a design with a concrete disproof. Deepen instead: re-run the
      // flat miter with the bound extended by the design's flush depth (the
      // most cycles a divergence can be delayed by flat). A REFUTE there is
      // real (REFUTED-cex); a pass there is the spurious-box case and stands
      // under the ordinary bounded-proof policy. A deepened run that does not
      // settle changes nothing (owner ruling 2026-10-14): the shallow flat leg
      // already checked the requested bound from reset with no counterexample,
      // which is PROVEN-bounded(n) -- a proof to that depth, and the deepening
      // was only an attempt to look further.
      auto deepen_if_bounded = [&](livehd::lec::Query_result& cand) {
        if (!(cand.verdict == Verdict::Proven && cand.bounded)) {
          return;
        }
        const int flush = std::max(cand.reset_hold, r.reset_hold);
        if (flush <= 0) {
          // No reset-hold / pipeline-flush prologue -- reset_hold is 0 for every
          // phase but after_reset. The deepened options would then be BYTE
          // IDENTICAL to the flat run that just finished (same graphs, same
          // bound, same options), so the re-solve buys no coverage.
          return;
        }
        livehd::lec::Lec_options odeep = oflat;
        odeep.bound                    = std::max(oflat.bound > 0 ? oflat.bound : 6, cand.checked_steps) + flush;
        {
          std::lock_guard report_lock(report_mutex);
          std::print("lec[hier]: '{}' flat confirmation only BOUNDED ({} step(s)) -> deepening to bound {} (+ flush depth {})\n",
                     name,
                     cand.checked_steps,
                     odeep.bound,
                     flush);
        }
        auto rd      = livehd::lec::prove_equal_isolated(ref_by_name[name], impl_by_name[name], odeep, sub_lib);
        rd.cvc5     += cand.cvc5;
        rd.solve_ms += cand.solve_ms;  // the shallow flat leg really ran: the budget must see it
        if (rd.verdict == Verdict::Undecided) {
          // Keep the shallow PROVEN-bounded result; only the effort and a note
          // carry over. The deepened leg is usually the one that burned the
          // whole per-query cap; dropping its solve_ms here would let every
          // remaining def draw a fresh full timeout.
          cand.detail   += std::format("; deepened flat run (bound {} = {} + flush {}) did not settle ({}), so the "
                                       "checked depth stands",
                                     odeep.bound,
                                     odeep.bound - flush,
                                     flush,
                                     livehd::lec::undecided_cause(rd.detail, rd.unsupported).word);
          cand.cvc5      = rd.cvc5;
          cand.solve_ms  = rd.solve_ms;
          return;
        }
        rd.detail = std::format("deepened flat confirmation (bound {} = {} + flush {}); ", odeep.bound, odeep.bound - flush, flush)
                    + rd.detail;
        cand      = std::move(rd);
      };
      // An undecided flat leg is the escalation's last word for this def: the
      // def reports the FLAT leg's own cause. Exception: a flat miter the
      // encoder refuses (a boxed child's unmodeled cell, now flattened) does
      // not overrule the collapsed attempt's own cause -- a refusal of the
      // fallback says nothing about the attempt it fell back from.
      auto take_flat_cause = [&](livehd::lec::Query_result& dst, const livehd::lec::Query_result& flat,
                                 const livehd::lec::Query_result& collapsed) {
        const bool flat_refused = flat.unsupported
                                  || livehd::lec::undecided_outcome(flat.cause_detail.empty() ? flat.detail : flat.cause_detail,
                                                                    flat.unsupported,
                                                                    false)
                                         == livehd::lec::Outcome::Refuted_unsupported;
        const auto& src = flat_refused && collapsed.verdict == Verdict::Undecided && !collapsed.unsupported ? collapsed : flat;
        adopt_cause(dst, src);
        dst.unsupported = src.unsupported;
      };
      if (refuted_under_collapse) {
        // An unsettled confirmation (a timeout or an encoder refusal of the flat
        // miter) leaves the def undecided by the FLAT leg's own cause; the
        // int-blast retry below re-solves the COLLAPSED miter and must not
        // decide it.
        deepen_if_bounded(rf);
        if (rf.verdict == Verdict::Undecided) {
          absorb_demoted = true;  // see the int_blast_retry guard below
        }
        const auto flat_leg  = rf;  // its own detail decides an undecided def
        rf.detail            = "flat-confirm after collapsed-box REFUTE" + std::string(rf.detail.empty() ? "" : "; ") + rf.detail
                               + (r.detail.empty() ? "" : " (collapsed run: " + r.detail + ")");
        if (rf.verdict == Verdict::Undecided) {
          take_flat_cause(rf, flat_leg, flat_leg);
        }
        rf.elapsed_ms  = -1;  // the progress record carries the combined wall-clock below
        rf.cvc5       += r.cvc5;
        r              = std::move(rf);
      } else if (proven_absorbing) {
        // The flat run is the authority here. If it settles, adopt it either way;
        // if it cannot, the collapsed PROVEN must NOT stand — a child's
        // counterexample is on the table and nothing discharged it, so the def
        // is undecided by the flat leg's own cause (a TIMEOUT, ...). A merely
        // bounded flat pass is deepened first, exactly as above: no
        // counterexample through the requested bound is PROVEN-bounded(n).
        deepen_if_bounded(rf);
        if (rf.verdict != Verdict::Undecided) {
          rf.detail = "flat-confirm after collapsed-box PROVEN absorbing a refutation" + std::string(rf.detail.empty() ? "" : "; ")
                      + rf.detail + (already_flat || r.detail.empty() ? "" : " (collapsed run: " + r.detail + ")");
          rf.elapsed_ms = -1;
          if (!already_flat) {
            rf.cvc5 += r.cvc5;
          }
          r = std::move(rf);
        } else {
          r.verdict = Verdict::Undecided;
          r.detail  = "flat confirmation of a collapsed proof absorbing a child's REFUTED block did not settle"
                      + std::string(rf.detail.empty() ? "" : "; ") + rf.detail
                      + std::string(already_flat || r.detail.empty() ? "" : "; collapsed run: ") + r.detail;
          adopt_cause(r, rf);
          r.unsupported = rf.unsupported;
          // When `rf` STARTED as a copy of `r` (already_flat), deepen_if_bounded
          // already folded r's own effort into it -- accumulating again would
          // double-charge the shared solver budget and starve the remaining defs.
          if (already_flat) {
            r.cvc5     = rf.cvc5;
            r.solve_ms = rf.solve_ms;
          } else {
            r.cvc5     += rf.cvc5;
            r.solve_ms += rf.solve_ms;  // same reason as the arm below: the flat leg ran
          }
          absorb_demoted = true;  // see the int_blast_retry guard below
        }
      } else {
        // A child's counterexample on the table (force_flat_refuted non-empty):
        // a merely BOUNDED flat pass is deepened first, exactly as the two arms
        // above -- the deepened run is what can still reach a divergence a
        // shallow 6-cycle retry cannot. An ordinary undecided -> flat retry
        // with nothing to absorb does not pay for the second solve.
        if (!force_flat_refuted[def_ix].empty()) {
          deepen_if_bounded(rf);
        }
        if (rf.verdict != Verdict::Undecided) {
          rf.detail      = "flat-retry after collapsed-box undecided result" + std::string(rf.detail.empty() ? "" : "; ") + rf.detail
                           + (r.detail.empty() ? "" : " (collapsed run did not settle: " + r.detail + ")");
          rf.elapsed_ms  = -1;
          rf.cvc5       += r.cvc5;
          r              = std::move(rf);
        } else {
          const auto collapsed  = r;
          r.detail             += "; flat retry (collapse cleared) did not settle either: " + rf.detail;
          take_flat_cause(r, rf, collapsed);
          r.cvc5               += rf.cvc5;  // here `rf` is the discarded side; the effort was still spent
        }
      }
      if (r.verdict == Verdict::Proven) {
        // Every successful branch above adopted the flat query. Its only
        // remaining boxes are explicitly trusted, so the speculative child
        // premises no longer belong to this proof. Otherwise escalation can
        // discard a settled flat proof and time out repeating the same miter.
        record_flat_proof();
        if (!ckey.empty()) {
          // A warm hit must recover the same absence of speculative premises.
          ckey = lec_pair_cache_key(ref_digest[def_ix], impl_digest[def_ix], oflat);
        }
      }
    }
    // int_blast=auto second leg: a solver-give-up Unknown (after any flat retry)
    // earns ONE int-blasted re-solve at the min_timeout floor. Before the
    // trusted-box demotion: a retry REFUTE must pass through the same discipline
    // below as any other refute.
    //
    // NOT after an absorb demotion. `o` still carries o.collapse, so this would
    // re-solve the very COLLAPSED miter whose PROVEN was just rejected for being
    // unconfirmable — and hand back that same Proven, absorbing the child's
    // counterexample and exiting 0 on a design with a concrete disproof. The
    // demotion is the verdict; nothing may quietly undo it.
    if (!absorb_demoted) {
      r = livehd::lec::int_blast_retry(ref_by_name[name], impl_by_name[name], o, std::move(r), sub_lib, order.size() != 1);
    }
    // A refute that turns on a TRUSTED box input stays a refute (owner ruling
    // 2026-10-14): the boxed model IS the contract -- a trust box asserts that
    // equal leaf inputs give equal leaf outputs, so differing inputs are a real
    // difference under the model the user asked for (REFUTED-cex). Disclose it:
    // the trusted leaf may treat that input as a don't-care.
    if (r.verdict == Verdict::Refuted) {
      if (std::string tb = lec_refute_trusted_box(r.witness, trust_set); !tb.empty()) {
        r.detail = std::format(
            "refuted at trusted-box input (bbin:{}) — the boxed model is the contract (a trusted leaf may still treat "
            "that input as a don't-care); {}",
            tb,
            r.detail);
      }
    }
    disclose_lec_helpers(r, o);
    const long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    if (budget_on) {
      defs_solved.fetch_add(1);
    }
    // lhd.stats run total. OUTSIDE the budget_on gate (accounting is on iff
    // timeout>0 && rlimit==0, which has nothing to do with stats), and under
    // report_mutex: the taskflow executor runs run_def concurrently under
    // lhd.jobs, so both the sum and the by-def vector would otherwise race.
    // Empty is the common no-solver case (cache / semdiff / abc settled it).
    if (base.stats && !r.cvc5.empty()) {
      std::lock_guard report_lock(report_mutex);
      run_cvc5 += r.cvc5;
      cvc5_by_def.emplace_back(name, r.cvc5.conflicts);
    }
    lec_store_cones(vcache, r);
    if (r.verdict == Verdict::Proven) {
      proven[def_ix]            = 1;
      bounded_proof[def_ix]     = r.bounded ? 1 : 0;
      own_bounded_steps[def_ix] = r.bounded ? std::max(1, r.checked_steps > 0 ? r.checked_steps : o.bound) : 0;
      if (vcache != nullptr) {
        if (!ckey.empty()) {
          // definitive Proven only (rule F; v1 skips Refuted); a bounded one keeps its depth
          vcache->insert(ckey, {r.engine, r.detail, ms, r.bounded, own_bounded_steps[def_ix]});
        }
        // Strategy hint keyed by entity NAME so it survives the design edit
        // that misses the digest-keyed verdict cache.
        // A bounded pass is not a winning strategy (it is what auto falls back
        // to when induction does not close), so never record it as one.
        if (!r.bounded) {
          vcache->set_hint(name, {r.engine, r.split_used, ms});
        }
        // A PASS obtained WITH uncertain pairs validates them: persist as the
        // entity-keyed pair hint so warm runs inject them without re-running
        // the signature pass.
        lec_store_pair_hint(vcache, name, r.uncertain_pairs_used);
      }
    } else {
      if (is_time_limit(r) && vcache != nullptr && !ckey.empty()) {
        // Ledger the attempt (NOT a verdict): an unchanged re-run at no more
        // budget skips this def instead of re-burning the full solver timeout.
        // Only a TIMEOUT-time result: any other cause (a refusal, a memory
        // limit, a witness-carrying partial-miter diff) must re-surface on
        // every run, never replay as "known timeout".
        vcache->note_timeout(ckey, {base.timeout, ms});  // the RUN's budget; see skip_timeout above
      }
      if (pairs_from_hint && vcache != nullptr) {
        // Self-heal: the replayed pair hint validated but its solve did NOT
        // end Proven — it is stale (a crossed/outdated pairing that would
        // otherwise suppress the fresh signature pass forever). Drop it so
        // the next run re-derives the pairing fresh.
        vcache->clear_pair_hint(name);
      }
    }
    // What failed to pair, whenever the def ended weaker than an unbounded
    // PROVEN (a re-tried def overwrites its earlier round's report).
    {
      Lec_state_report rep;
      if (r.verdict == Verdict::Undecided || (r.verdict == Verdict::Proven && r.bounded)) {
        rep = lec_unpaired_state_report(name,
                                        ref_by_name[name],
                                        impl_by_name[name],
                                        state_unpaired_ref,
                                        state_unpaired_impl,
                                        o.uncertain_match,
                                        r.uncertain_pairs_used);
      }
      std::lock_guard report_lock(report_mutex);
      state_reports[name] = std::move(rep);
    }
    // Scheduler bookkeeping: a Proven or Refuted verdict is DEFINITIVE, so later
    // rounds skip this def; an Unknown stays unsettled and is re-tried with a
    // bigger slice next round (the verdict-cache Unknown ledger, keyed by budget,
    // makes that a genuine re-attempt rather than a cache skip).
    if (r.verdict == Verdict::Proven || r.verdict == Verdict::Refuted) {
      settled[def_ix] = 1;
    }
    {
      std::lock_guard report_lock(report_mutex);
      // Keep the FIRST refute (leaves-first => the deepest one): it is both the
      // fail-fast trigger for this def's parents and the run's reported verdict.
      if (r.verdict == Verdict::Refuted) {
        refuted[def_ix]       = 1;
        refuted_by_ix[def_ix] = r;
        if (!have_refuted) {
          refuted_result = r;
          refuted_def    = name;
          have_refuted   = true;
        }
      } else {
        refuted_by_ix.erase(def_ix);  // an escalation round re-decided this def
      }
      if (r.verdict == Verdict::Undecided) {
        undecided_cause_by_ix[def_ix] = std::string{livehd::lec::undecided_cause(r.detail, r.unsupported).word};
      } else {
        undecided_cause_by_ix.erase(def_ix);
      }
      emit_lec_block_progress(name, r, o, ms);
      if (base.stats) {
        for (const auto& arm : r.arms) {
          std::print("lec[arm]: {} {}:{} {} {}ms won={} killed={} queued={}\n",
                     name,
                     arm.engine,
                     arm.profile,
                     arm.verdict,
                     arm.elapsed_ms,
                     arm.won,
                     arm.killed,
                     arm.queued);
        }
      }
      std::print("lec[hier]: '{}' {} ({} child collapse{})\n",
                 name,
                 r.verdict == Verdict::Proven    ? "PROVEN"
                 : r.verdict == Verdict::Refuted ? "REFUTED"
                                                 : livehd::lec::undecided_cause(r.detail, r.unsupported).word,
                 coll.size(),
                 coll.size() == 1 ? "" : "s");
    }
    if ((name == top_key)) {
      top_result = r;
      have_top   = true;
    }
  };

  // Dispatch the proof DAG once. run_def skips already-`settled` defs, so
  // RE-dispatching is exactly how an escalating round re-tries only the
  // survivors.
  //
  // BOTTOM-UP: child->parent edges, so a parent waits for its children to settle
  // and boxes the proven ones. The depth of the hierarchy is a serial chain.
  //
  // TOP-DOWN: NO edges at all. Every def boxes every child unconditionally, so
  // no def's obligation depends on any other def's VERDICT — only on the DAG
  // being well-founded, which it is. The whole hierarchy is therefore
  // embarrassingly parallel, and each def's miter is at its MINIMUM size (a
  // bottom-up parent has to flatten any child that did not prove, which is
  // exactly the case where the miter is hardest).
  auto dispatch_dag = [&](const std::vector<size_t>* subset = nullptr) {
    if (order.size() == 1) {
      run_def(0);  // preserve the normal portfolio; no hierarchy parallelism needed
      return;
    }
    std::vector<size_t> all;
    if (subset == nullptr) {
      all.resize(order.size());
      for (size_t i = 0; i < order.size(); ++i) {
        all[i] = i;
      }
      subset = &all;
    }
    tf::Taskflow                          proof_dag;
    absl::flat_hash_map<size_t, tf::Task> tasks;
    for (size_t i : *subset) {
      tasks.emplace(i, proof_dag.emplace([&, i] { run_def(i); }).name(order[i]));
    }
    if (!top_down) {
      for (size_t i : *subset) {
        if (auto it = children.find(order[i]); it != children.end()) {
          for (const auto& child : it->second) {
            auto ci = order_ix.find(child);
            if (ci == order_ix.end()) {
              continue;
            }
            auto ct = tasks.find(ci->second);
            if (ct != tasks.end()) {
              ct->second.precede(tasks.at(i));
            }
          }
        }
      }
    }
    tf::Executor executor(static_cast<size_t>(std::max(1, base.jobs)));
    executor.run(proof_dag).wait();
  };

  // ── Budget scheduler ──────────────────────────────────────────────────────
  // With a finite timeout and no rlimit, on a multi-def hierarchy,
  // `base.timeout` is a soft TOTAL wall-clock budget for the DAG: each def's
  // per-query cap becomes the elapsed wall time remaining when dispatched
  // (run_def, above), without adding together concurrent workers. The TOP def keeps the full per-query cap (it is
  // the requested proof and runs last — see run_def). Fast defs (the common
  // case) finish well under budget and still see the full cap, so a design
  // that fits is never regressed; only a run that would out-solve `timeout`
  // is reined in. Off — the full per-query cap, byte-identical to before —
  // for the deterministic rlimit tier, an unbounded budget (timeout==0), or a
  // lone def. A single DAG pass: the verdict cache / Unknown-ledger and per-def
  // progress stay exactly as before.
  budget_on = base.timeout > 0 && base.compile_resource_limit == 0 && order.size() > 1;
  dispatch_dag();

  // ── DISCHARGE the top-down premises, then ESCALATE only where one failed ───
  //
  // Pass A proved every def with ALL children boxed, so each verdict reads
  // "D_ref == D_impl PROVIDED each boxed child pair is equivalent". Those
  // premises are the other entries of the same pass, and the module DAG is
  // well-founded, so the closure below is an induction and not circular
  // reasoning: D is UNCONDITIONALLY proven iff D proved and every child it
  // boxed is unconditionally proven. A TRUSTED child is a disclosed assumption
  // by user request (`formal.lec.trust`) and is treated as discharged — exactly
  // as bottom-up already does.
  //
  // A child that did NOT discharge leaves its ancestors conditional. That is a
  // strictly better first pass than bottom-up's, which flattens the unresolved
  // child into every ancestor and grows the miter on the way up. Here the ONE
  // def that has to absorb or contextualize the failure is the unresolved
  // child's immediate PARENT: re-prove the parent with that child INLINED and
  // everything else still boxed. If the parent proves, its own boundary
  // behaviour is intact, and since every ancestor already proved with the
  // parent BOXED, the whole chain closes — there is nothing to check higher
  // up. Only if the parent is still unresolved does the
  // escalation move up a level.
  std::vector<uint8_t> unconditional(order.size(), 0);
  // True when def `i` has NO undischarged premise -- i.e. it is non-unconditional
  // only because its OWN proof is bounded. The conditional-degradation message
  // below must not fire then: it would print "0 premise(s) never discharged ()"
  // and hide the real reason, which the bounded-pass policy states precisely.
  auto                 open_premise_free = [&](size_t i) {
    for (const auto& c : assumed[i]) {
      auto ci = order_ix.find(c);
      if (ci == order_ix.end() || unconditional[ci->second] == 0) {
        return false;
      }
    }
    return true;
  };
  auto compute_closure = [&]() {
    std::fill(unconditional.begin(), unconditional.end(), 0);
    // `order` is leaves-first, so one forward sweep reaches a fixpoint.
    for (size_t i = 0; i < order.size(); ++i) {
      composed_bounded_steps[i] = 0;
      if (proven[i] == 0) {
        continue;
      }
      composed_bounded_steps[i] = own_bounded_steps[i];
      bool all_kids             = true;
      for (const auto& c : assumed[i]) {
        auto ci = order_ix.find(c);
        if (ci == order_ix.end() || unconditional[ci->second] == 0) {
          all_kids = false;
          break;
        }
        // BOUNDEDNESS PROPAGATES UP. A bounded child still DISCHARGES the
        // premise -- blocking it would strand every design whose leaves prove by
        // BMC, which is the common case (measured: 170 suite failures) -- but the
        // parent's claim is then no stronger than the child's. Marking the parent
        // bounded keeps the composition working AND keeps the final verdict
        // honest: the top ends up PROVEN-bounded(n), n the shallowest bounded
        // leg of the composition. The alternative, silently treating a k-cycle
        // child as an unbounded sequence-transducer contract, is the hole.
        if (bounded_proof[ci->second] != 0) {
          bounded_proof[i]   = 1;
          const int child    = composed_bounded_steps[ci->second];
          int&      composed = composed_bounded_steps[i];
          if (child > 0 && (composed == 0 || child < composed)) {
            composed = child;
          }
        }
      }
      unconditional[i] = all_kids ? 1 : 0;
    }
  };
  if (top_down) {
    compute_closure();
    const size_t top_ix = order_ix.at(top_key);
    // Escalation rounds. Each round picks defs whose OWN proof is unresolved
    // (Refuted or Unknown), inlines exactly those into their parents, and
    // re-proves ONLY those parents. Picking only own-proof failures avoids
    // flattening their already-conditional ancestors in the same round: once
    // the immediate parent proves, the existing boxed theorem closes the whole
    // chain. Bounded by the hierarchy depth: a round that changes nothing
    // stops. The top itself has no parent to escalate into — its own flat
    // confirm (inside run_def) is already the last word.
    for (size_t round = 0; round < order.size() && unconditional[top_ix] == 0; ++round) {
      std::vector<size_t>         retry;
      absl::flat_hash_set<size_t> retry_set;
      for (size_t i = 0; i < order.size(); ++i) {
        if (proven[i] != 0 || order[i] == top_key) {
          continue;
        }
        for (const auto& p : parents[order[i]]) {
          auto pi = order_ix.find(p);
          if (pi == order_ix.end()) {
            continue;
          }
          // Already absorbed in an earlier round, or the parent declined to box it
          // because force_flat already said so.
          if (force_flat[pi->second].count(order[i]) > 0) {
            continue;
          }
          force_flat[pi->second].insert(order[i]);
          if (refuted[i] != 0) {
            force_flat_refuted[pi->second].insert(order[i]);
          }
          // Record the absorption (so the ABSORBED accounting stays right and
          // later rounds stay idempotent) but DO NOT schedule a retry for a parent
          // that never boxed this child in the first place: the asymmetric-inlining
          // guard already descended it, so the parent's standing proof ALREADY
          // contains the child flat on both sides. Re-solving is the identical
          // query, and the force_flat_refuted just recorded would drag a settled
          // PROVEN through the proven-absorbing flat confirmation -- on a 170-module
          // design those parents are 25-210 s defs, and the retry runs on the
          // REMAINING soft budget, so the redundant round can time out and demote a
          // verdict that was already final.
          if (descended[pi->second].count(order[i]) > 0) {
            continue;
          }
          if (retry_set.insert(pi->second).second) {
            retry.push_back(pi->second);
          }
        }
      }
      if (retry.empty()) {
        break;
      }
      refuted_snapshot = refuted;  // no task in flight: safe, and fixes the round
      for (size_t i : retry) {
        std::print(
            "lec[hier]: ESCALATE '{}' — re-proving with {} unresolved child(ren) INLINED "
            "({} refuted, {} undecided), every other child still boxed\n",
            order[i],
            force_flat[i].size(),
            force_flat_refuted[i].size(),
            force_flat[i].size() - force_flat_refuted[i].size());
        settled[i] = 0;  // re-solve this one def
        proven[i]  = 0;
        refuted[i] = 0;
      }
      dispatch_dag(&retry);
      compute_closure();
    }
    // Escalation EXHAUSTED with the top proven only CONDITIONALLY: some premise
    // the top boxed (a child, or a grandchild boxed inside an inlined child)
    // never discharged, and no round is left to inline it -- the loop above
    // only ever re-proves a PARENT of an unresolved def, never the top for a
    // grandchild. UNDISCHARGED is an iteration trigger, never a verdict (owner
    // ruling 2026-10-14): re-prove the TOP with every undischarged premise
    // INLINED (flat), and its outcome is the answer. Each round inlines at
    // least one more def, so this is bounded by the hierarchy size.
    for (size_t round = 0; round < order.size() && have_top && top_result.verdict == Verdict::Proven
                           && unconditional[top_ix] == 0 && !open_premise_free(top_ix);
         ++round) {
      std::vector<std::string> inline_now;
      for (const auto& c : assumed[top_ix]) {
        auto ci = order_ix.find(c);
        if ((ci == order_ix.end() || unconditional[ci->second] == 0) && force_flat[top_ix].insert(c).second) {
          inline_now.push_back(c);
        }
      }
      if (inline_now.empty()) {
        break;
      }
      std::print("lec[hier]: RE-PROVE '{}' — {} undischarged premise(s) INLINED ({}), every discharged child still boxed\n",
                 top_key,
                 inline_now.size(),
                 absl::StrJoin(inline_now, ", "));
      refuted_snapshot   = refuted;  // no task in flight
      settled[top_ix]    = 0;
      proven[top_ix]     = 0;
      refuted[top_ix]    = 0;
      std::vector<size_t> only_top{top_ix};
      dispatch_dag(&only_top);
      compute_closure();
    }
    const int cond_only = static_cast<int>(std::count(proven.begin(), proven.end(), uint8_t{1}))
                          - static_cast<int>(std::count(unconditional.begin(), unconditional.end(), uint8_t{1}));
    if (cond_only > 0) {
      std::print("lec[hier]: {} def(s) proven only CONDITIONALLY (a boxed child never discharged)\n", cond_only);
    }
    // RE-PICK the run's fallback verdict. A block refutation is ABSORBED once
    // some parent re-proved with that block INLINED: the block boundary differs
    // but the design does not, which is the whole reason a module boundary is
    // not part of the specification. An absorbed CEX is a diagnostic, never the
    // answer, so drop it before the aggregate below can adopt it.
    have_refuted = false;
    refuted_def.clear();
    int absorbed = 0;
    for (size_t i = 0; i < order.size(); ++i) {
      auto it = refuted_by_ix.find(i);
      if (it == refuted_by_ix.end()) {
        continue;
      }
      bool is_absorbed = false;
      for (const auto& p : parents[order[i]]) {
        auto pi = order_ix.find(p);
        if (pi != order_ix.end() && proven[pi->second] != 0
            && (force_flat_refuted[pi->second].count(order[i]) > 0 || descended[pi->second].count(order[i]) > 0)) {
          is_absorbed = true;  // the parent re-proved with this block inlined
          break;
        }
      }
      if (is_absorbed) {
        ++absorbed;
        continue;
      }
      if (!have_refuted) {
        refuted_result = it->second;
        refuted_def    = order[i];
        have_refuted   = true;
      }
    }
    if (absorbed > 0) {
      std::print(
          "lec[hier]: {} block refutation(s) ABSORBED by a parent that re-proved with the block inlined "
          "(a module boundary is not part of the specification)\n",
          absorbed);
    }
    // A top STILL proven only CONDITIONALLY after the flat re-prove above is not
    // a proof of the top -- and it is not a verdict either: the re-prove should
    // have inlined every undischarged premise. An internal cause that reaches
    // the top is a LiveHD bug (REFUTED-unexplained, owner ruling 2026-10-14),
    // never silently accepted as PROVEN. The premise list (with each premise's
    // own internal cause) says where to look.
    if (have_top && top_result.verdict == Verdict::Proven && unconditional[top_ix] == 0 && !open_premise_free(top_ix)) {
      std::string open_premises;
      int         n = 0;
      for (size_t i = 0; i < order.size(); ++i) {
        if (unconditional[i] == 0 && order[i] != top_key && !parents[order[i]].empty()) {
          const auto cause = undecided_cause_by_ix.find(i);
          open_premises += (n++ ? ", " : "") + order[i]
                           + (cause == undecided_cause_by_ix.end() ? std::string{} : " [" + cause->second + "]");
          if (n >= 8) {
            open_premises += ", ...";
            break;
          }
        }
      }
      top_result.verdict      = Verdict::Undecided;
      top_result.cause_detail = std::format(
          "UNDISCHARGED premise(s) reached the top after the flat re-prove ({}): the hierarchical driver failed to "
          "inline them, a LiveHD bug to report",
          open_premises);
      top_result.detail = std::format("{}; '{}' proved with every child BOXED, but {} premise(s) never discharged; {}",
                                      top_result.cause_detail,
                                      top_key,
                                      n,
                                      top_result.detail);
    }
  }

  // Soft-budget accounting: `timeout` is a TARGET, so report what it actually
  // cost. `defs_floored` is the overrun's cause — those defs were dispatched
  // after the total was already spent and each drew a full min_timeout floor —
  // so target/actual/units/floored together say whether to raise the target or
  // lower the floor. Printed whenever the target was missed, not only on a
  // failure, because a run that quietly took 3x its budget is the thing an agent
  // loop needs to see.
  if (budget_on) {
    const long long spent_s
        = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - budget_start).count();
    const int       floored = defs_floored.load();
    if (spent_s > base.timeout || floored > 0) {
      std::print("lec[hier]: budget {}s target / {}s actual over {} def(s) solved, {} on the {}s floor\n",
                 base.timeout,
                 spent_s,
                 defs_solved.load(),
                 floored,
                 std::max(1, base.min_timeout));
    }
  }

  // Diagnosis phase (formal.spec_mining_timeout): name the still-unproven defs so a
  // budget-limited run's OUTPUT is actionable. The toxic-core / mining iteration —
  // itself potentially many cvc5 rounds — gets its OWN budget (formal.spec_mining_timeout),
  // never drawing from formal.timeout; the straggler list is today's signal.
  if (base.spec_mining_timeout > 0) {
    std::string stragglers;
    int         n = 0;
    for (size_t i = 0; i < order.size(); ++i) {
      if (!settled[i]) {
        stragglers += (stragglers.empty() ? "" : ", ") + order[i];
        ++n;
      }
    }
    if (n > 0) {
      std::print("lec[hier]: {} def(s) unproven within the {}s budget: {}\n", n, base.timeout, stragglers);
    }
  }

  // proven[] marks a TRUSTED def proven-by-assumption too (so parents box it), so
  // separate the two: report only defs actually PROVEN, and disclose the trusted
  // (assumed-equal) count apart — a trusted def was never solved, cached, or
  // semdiff-matched, so it must not inflate any of those figures.
  // Under top-down, only an UNCONDITIONALLY proven def counts: a conditional
  // proof is a premise waiting on a child, not a settled definition.
  const int  proven_count  = top_down ? static_cast<int>(std::count(unconditional.begin(), unconditional.end(), uint8_t{1}))
                                      : static_cast<int>(std::count(proven.begin(), proven.end(), uint8_t{1}));
  const int  trusted_total = trusted_count.load();
  const int  really_proven = proven_count - trusted_total;
  // Under top-down these are intersected with the closure, so the three
  // provenance figures always sum to `really_proven` instead of going negative
  // when a cache/semdiff hit stayed conditional.
  const auto counted       = [&](const std::vector<uint8_t>& how) {
    int n = 0;
    for (size_t i = 0; i < order.size(); ++i) {
      if (how[i] != 0 && (!top_down || unconditional[i] != 0)) {
        ++n;
      }
    }
    return n;
  };
  const int cache_proven   = counted(by_cache);
  const int semdiff_proven = counted(by_semdiff);

  std::print("lec[hier]: {}/{} def(s) proven {} ({} via cache, {} via semdiff, {} via solver){}\n",
             really_proven,
             static_cast<int>(order.size()) - trusted_total,
             top_down ? "top-down" : "leaves-first",
             cache_proven,
             semdiff_proven,
             really_proven - semdiff_proven - cache_proven,
             trusted_total > 0 ? std::format("; {} def(s) TRUSTED (assumed equal, NOT proven)", trusted_total) : std::string{});
  res.recipe_steps.emplace_back(std::format("pass.lec hierarchical order:{} defs:{} proven:{} trusted:{} cache:{} semdiff:{}",
                                            top_down ? "top_down" : "bottom_up",
                                            order.size(),
                                            really_proven,
                                            trusted_total,
                                            cache_proven,
                                            semdiff_proven));

  // A child counterexample is a boundary obligation, not a top-level trace: a
  // refuted hierarchy BLOCK is not a refuted design. In escalate mode the
  // parent confirms it in context; if the top stays undecided, the top's OWN
  // cause is the answer (the child may require an input combination its caller
  // never supplies) and the block CEX is only a diagnostic. The fail-fast debug
  // mode (formal.lec.hier_refute=fail) adopts the block CEX instead -- but that
  // option forbade the escalation that would confirm or absorb it, so the run
  // is REFUTED-unchecked (exit 7), never REFUTED-cex.
  if (have_refuted && (!have_top || (top_result.verdict != Verdict::Proven && top_result.verdict != Verdict::Refuted))) {
    if (fail_fast_refute) {
      const bool skipped      = !have_top;
      top_result              = refuted_result;
      top_result.verdict      = Verdict::Undecided;
      top_result.cause_detail = std::format(
          "block '{}' REFUTED at its boundary; escalation disabled (formal.lec.hier_refute=fail) so the top never confirmed it",
          refuted_def);
      top_result.detail = std::format("hierarchical: {}{}; block result: {}",
                                      top_result.cause_detail,
                                      skipped ? "" : " (the top itself did not settle)",
                                      top_result.detail);
      have_top          = true;
    } else if (have_top) {
      adopt_cause(top_result, top_result);  // the top's own cause decides; the note below is a diagnostic
      top_result.detail += std::format("; child '{}' differs at its boundary (a block diagnostic, not the top's answer)", refuted_def);
    }
  }
  if (have_top && top_result.verdict == Verdict::Proven && top_down) {
    if (auto ti = order_ix.find(top_key); ti != order_ix.end() && bounded_proof[ti->second] != 0) {
      top_result.bounded = true;  // some premise in the tree was only bounded
      // PROVEN-bounded(n): n is the shallowest bounded leg of the composition.
      if (composed_bounded_steps[ti->second] > 0
          && (top_result.checked_steps <= 0 || composed_bounded_steps[ti->second] < top_result.checked_steps)) {
        top_result.checked_steps = composed_bounded_steps[ti->second];
      }
    }
  }
  if (!have_top) {
    // top_key is force-paired above, so run_def always reaches the top: no top
    // result is a driver bug (REFUTED-unexplained), never an empty miter --
    // nothing_compared would read as REFUTED-vacuous.
    top_result.verdict      = Verdict::Undecided;
    top_result.cause_detail = std::format("hierarchical driver produced no result for top '{}': a LiveHD bug to report", top_name);
    top_result.detail       = top_result.cause_detail;
  }
  // lhd.stats: the run total, ASSIGNED not accumulated — top_result is a copy
  // of some def's Query_result (the top's, or a refuted descendant's), so its
  // `cvc5` already counts that def and a += would double it.
  if (base.stats) {
    top_result.cvc5 = run_cvc5;
    if (cvc5_hot != nullptr) {
      std::sort(cvc5_by_def.begin(), cvc5_by_def.end(), [](const auto& a, const auto& b) {
        return a.second != b.second ? a.second > b.second : a.first < b.first;  // hardest first, name-stable
      });
      if (cvc5_by_def.size() > 3) {
        cvc5_by_def.resize(3);
      }
      *cvc5_hot = std::move(cvc5_by_def);
    }
  }
  lec_publish_state_reports(res, state_reports);
  return top_result;
}

// ===== simfail witness reproduction (`lhd lec` + --workdir) ==================
// On a REFUTED verdict with a reproducible BMC trace, write a self-contained
// Pyrope testbench (formal.simfail, default simfail_<top>.prp) that instantiates BOTH
// designs inside one wrapper module, drives the counterexample input sequence,
// and (with formal.simfail_run) runs `lhd sim` to dump ONE same-basename VCD so the
// impl-vs-ref divergence is visualized / re-runnable. Every step is best-effort:
// a side that cannot re-emit as Pyrope (lg:/yosys netlists have no LNAST), a
// name clash, or a sim build error is a WARNING, never a hard failure — the LEC
// verdict already stands on its own.

// One re-emitted Pyrope module: name + parsed header IO + full source text (the
// emitted `::[lg="..", hdl]` attribute is kept verbatim; a fresh sim compile
// ignores the stale `lg=` reference — validated).
// One SCALAR input the wrapper has to drive. A STRUCT port (`io:(a:u1, b:(c:u2))`)
// is ONE `inputs` entry but SEVERAL leaves, and an instantiation binds such a port
// per LEAF (`io.b.c = expr` — the spelling pass.prp_writer itself emits, see any
// xiangshan sub-module call), never as one aggregate. So the wrapper declares a
// flat scalar per leaf and rebuilds the struct at each call site.
struct Lecfail_leaf {
  std::string dotted;  // `io.b.c` — the actual's name at a call site
  std::string type;    // declared ":u2" suffix, or "" when untyped
};

// Source paths retain backticks for emission, but graph witness keys contain
// the decoded port names. In particular, an explicit `reset` must match the
// trace's reset input rather than being mistaken for a minted reset.
std::string lecfail_trace_name(std::string_view source) {
  std::string name;
  for (size_t i = 0; i < source.size();) {
    if (source[i] != '`') {
      name += source[i++];
      continue;
    }
    const size_t start = i++;
    while (i < source.size() && source[i] != '`') {
      if (source[i] == '\\' && i + 1 < source.size()) {
        ++i;
      }
      ++i;
    }
    if (i == source.size()) {
      name.append(source.substr(start));
      break;
    }
    ++i;
    auto decoded = str_tools::decode_backtick_ident(source.substr(start, i - start));
    if (decoded) {
      name.append(*decoded, 1, decoded->size() - 2);
    }
  }
  return name;
}

// Flattened wrapper ports derived from escaped names still need escaping.
// Keep the bare flat name separately for safe prefixed stimulus-array names.
std::string lecfail_flat_ident(std::string_view dotted, std::string_view flat) {
  return dotted.find('`') == std::string_view::npos ? std::string(flat) : std::format("`{}`", flat);
}

// The leading type word of a declared `:T` suffix is `word` (`:Bool`,
// `:Bool@[0]`, `:Clock`).
bool lecfail_suffix_is(std::string_view suffix, std::string_view word) {
  size_t p = (!suffix.empty() && suffix[0] == ':') ? 1 : 0;
  while (p < suffix.size() && (suffix[p] == ' ' || suffix[p] == '\t')) {
    ++p;
  }
  if (suffix.compare(p, word.size(), word) != 0) {
    return false;
  }
  p += word.size();
  return p == suffix.size() || (std::isalnum(static_cast<unsigned char>(suffix[p])) == 0 && suffix[p] != '_');
}

// A declared `:Bool` suffix. Bool ports are strict: the testbench declares such
// a port `Bool`, drives it with a comparison, and casts at a binding whose other
// side is an integer (`U1(x)` / `Bool(x)`).
bool lecfail_is_bool_type(std::string_view suffix) { return lecfail_suffix_is(suffix, "Bool"); }

// A declared `:Clock` / `:Reset` suffix (docs 07-typesystem "Clock and Reset"):
// the wrapper keeps the type, so the port stays the implicit clock/reset it
// binds by. A Clock is never driven from the trace (the sim clock ticks it); a
// Reset is Bool-like, poked with a comparison.
bool lecfail_is_clock_type(std::string_view suffix) { return lecfail_suffix_is(suffix, "Clock"); }
bool lecfail_is_reset_type(std::string_view suffix) { return lecfail_suffix_is(suffix, "Reset"); }

// The width N of a declared signed `:S<N>` suffix, else 0.
int lecfail_signed_width(std::string_view suffix) {
  size_t p = (!suffix.empty() && suffix[0] == ':') ? 1 : 0;
  while (p < suffix.size() && (suffix[p] == ' ' || suffix[p] == '\t')) {
    ++p;
  }
  if (p >= suffix.size() || suffix[p] != 'S') {
    return 0;
  }
  int    n = 0;
  size_t q = p + 1;
  for (; q < suffix.size() && std::isdigit(static_cast<unsigned char>(suffix[q])) != 0; ++q) {
    n = n * 10 + (suffix[q] - '0');
  }
  if (q == p + 1 || (q < suffix.size() && (std::isalnum(static_cast<unsigned char>(suffix[q])) != 0 || suffix[q] == '_'))) {
    return 0;  // not a number, or a longer name such as a type alias `S4x`
  }
  return n;
}

struct Lecfail_mod {
  std::string                                      name;
  std::string                                      text;       // full module source
  std::vector<std::pair<std::string, std::string>> inputs;     // {name, ":type" suffix or ""}
  std::vector<std::pair<std::string, std::string>> outputs;    // {name, ":type@[..]" suffix or ""}
  std::vector<Lecfail_leaf>                        in_leaves;  // `inputs` flattened to scalars
};

// Simple (unqualified) module name: the tail after the last '.' (a graph named
// "impl.dut" re-emits as `mod dut`).
std::string lecfail_simple_name(std::string_view n) {
  auto dot = n.rfind('.');
  return std::string(dot == std::string_view::npos ? n : n.substr(dot + 1));
}

// One stable filesystem/Pyrope identifier shared by the LEC and verify replay
// paths. Formal blocks retain their full dotted name; graph tops use the simple
// entity name selected by the driver.
std::string simfail_filename(std::string_view subject) {
  std::string safe;
  for (char c : subject) {
    safe += (std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_') ? c : '_';
  }
  if (safe.empty()) {
    safe = "top";
  }
  return "simfail_" + safe + ".prp";
}

// Index of the bracket matching the `(`/`[` at `open`, or npos. A backtick-quoted
// identifier is stepped over so a bracket inside one never counts.
size_t lecfail_match_bracket(std::string_view text, size_t open) {
  if (open >= text.size()) {
    return std::string_view::npos;
  }
  const char oc = text[open];
  const char cc = oc == '(' ? ')' : (oc == '[' ? ']' : '\0');
  if (cc == '\0') {
    return std::string_view::npos;
  }
  int depth = 0;
  for (size_t i = open; i < text.size(); ++i) {
    const char c = text[i];
    if (c == '`') {
      const size_t q = text.find('`', i + 1);
      if (q == std::string_view::npos) {
        break;
      }
      i = q;
      continue;
    }
    if (c == oc) {
      ++depth;
    } else if (c == cc && --depth == 0) {
      return i;
    }
  }
  return std::string_view::npos;
}

// Split a comma-separated IO list ("en, din:u8") into {name, ":type" suffix}.
// The split is DEPTH-AWARE: a STRUCT port's type carries commas of its own
// (`io:(valid:u1, bits:(x:u4))`), and so does a cycle/attribute bracket
// (`o:u8@[0, 1]`) — only a comma at nesting depth 0 separates two ports. The
// flat split this replaces turned every struct port into syntactic garbage
// (`bits:(ftqIdx:(flag:u1`), so a struct-ported design never got a testbench.
void lecfail_parse_io(std::string_view list, std::vector<std::pair<std::string, std::string>>& out) {
  auto emit = [&out](std::string_view item) {
    size_t b = item.find_first_not_of(" \t\r\n");
    size_t e = item.find_last_not_of(" \t\r\n");
    if (b == std::string_view::npos) {
      return;
    }
    item         = item.substr(b, e - b + 1);
    // The name/type separator is the first ':' outside a quoted name AND outside
    // any nesting (a struct field's own ':' must not split the port).
    size_t colon = std::string_view::npos;
    int    depth = 0;
    for (size_t i = 0; i < item.size(); ++i) {
      const char c = item[i];
      if (c == '`') {
        const size_t q = item.find('`', i + 1);
        if (q == std::string_view::npos) {
          break;
        }
        i = q;
      } else if (c == '(' || c == '[') {
        ++depth;
      } else if (c == ')' || c == ']') {
        --depth;
      } else if (c == ':' && depth == 0) {
        colon = i;
        break;
      }
    }
    if (colon == std::string_view::npos) {
      out.emplace_back(std::string(item), std::string{});
    } else {
      out.emplace_back(std::string(item.substr(0, colon)), std::string(item.substr(colon)));
    }
  };
  int    depth = 0;
  size_t start = 0;
  for (size_t i = 0; i <= list.size(); ++i) {
    if (i == list.size()) {
      emit(list.substr(start));
      break;
    }
    const char c = list[i];
    if (c == '`') {
      const size_t q = list.find('`', i + 1);
      if (q == std::string_view::npos) {
        emit(list.substr(start));
        break;
      }
      i = q;
    } else if (c == '(' || c == '[') {
      ++depth;
    } else if (c == ')' || c == ']') {
      --depth;
    } else if (c == ',' && depth == 0) {
      emit(list.substr(start, i - start));
      start = i + 1;
    }
  }
}

// Flatten one declared port into the SCALAR leaves an instantiation binds:
// `io:(valid:u1, bits:(x:u4))` -> {`io.valid`:u1, `io.bits.x`:u4}; a scalar port
// is its own single leaf. The recursion is driven by the TYPE, so a
// backtick-quoted name that merely CONTAINS a dot (`` `io.a` ``, a flat port
// prp_writer emits for a severed struct net) correctly stays one leaf.
void lecfail_expand_leaves(const std::string& name, const std::string& suffix, std::vector<Lecfail_leaf>& out) {
  std::string_view s(suffix);
  size_t           p = (!s.empty() && s[0] == ':') ? 1 : 0;
  while (p < s.size() && (s[p] == ' ' || s[p] == '\t')) {
    ++p;
  }
  const size_t close = (p < s.size() && s[p] == '(') ? lecfail_match_bracket(s, p) : std::string_view::npos;
  std::vector<std::pair<std::string, std::string>> fields;
  if (close != std::string_view::npos) {
    lecfail_parse_io(s.substr(p + 1, close - p - 1), fields);
  }
  if (fields.empty()) {
    out.push_back({name, suffix});
    return;
  }
  for (const auto& [fn, ft] : fields) {
    lecfail_expand_leaves(name + "." + fn, ft, out);
  }
}

// The Pyrope lambda keywords a re-emitted module header can open with. The
// writer emits exactly `pub mod ` (stateful body) or `pub comb ` (stateless) —
// upass/prp_writer/lnast_prp_writer.cpp picks between them with
// body_has_state() — so a COMBINATIONAL design re-emits with NO `mod` keyword
// anywhere. Matching only `mod` silently lost the whole combinational class
// (the witness generator then reported "no Pyrope modules were re-emitted").
// The other two keywords cannot come out of the writer today; they are accepted
// so a hand-written or future-writer file parses too.
constexpr std::string_view lecfail_lambda_kws[] = {"mod", "comb", "pipe", "fluid"};

// Offset of the module NAME in a lambda header, or npos. The keyword must OPEN
// a line (leading whitespace and an optional `pub ` are skipped) and be followed
// by whitespace then an identifier — so a signal named `mod_x`, the word "comb"
// in a comment, and a `lg="a.mod"` string never match. `pipe`'s optional
// `[latency]` is stepped over.
size_t lecfail_header_name_pos(std::string_view text) {
  auto is_blank = [](char c) { return c == ' ' || c == '\t'; };
  for (size_t ls = 0; ls <= text.size();) {
    const size_t nl  = text.find('\n', ls);
    const size_t end = nl == std::string_view::npos ? text.size() : nl;
    size_t       p   = ls;
    while (p < end && is_blank(text[p])) {
      ++p;
    }
    if (text.compare(p, 4, "pub ") == 0) {
      p += 4;
      while (p < end && is_blank(text[p])) {
        ++p;
      }
    }
    for (const auto kw : lecfail_lambda_kws) {
      if (text.compare(p, kw.size(), kw) != 0) {
        continue;
      }
      size_t q = p + kw.size();
      if (q < end && text[q] == '[') {  // pipe[3] / pipe[1..=2]
        const size_t rb = text.find(']', q);
        if (rb == std::string_view::npos || rb >= end) {
          continue;
        }
        q = rb + 1;
      }
      size_t r = q;
      while (r < end && is_blank(text[r])) {
        ++r;
      }
      if (r == q || r >= end) {
        continue;  // the keyword must be followed by whitespace, then the name
      }
      const char c = text[r];
      if ((std::isalpha(static_cast<unsigned char>(c)) == 0) && c != '_' && c != '`') {
        continue;
      }
      return r;
    }
    if (nl == std::string_view::npos) {
      break;
    }
    ls = nl + 1;
  }
  return std::string_view::npos;
}

// Parse `... mod NAME[::[..]](in..) -> (out..) {` (or `comb`/`pipe`/`fluid`)
// from a module's source. Both lists end at the MATCHING bracket, not the first
// one: a struct port nests (`io:(valid:u1, bits:(x:u4))`) and an attribute block
// can hold a paren of its own (`::[name=a(b)]`). Returns false if no lambda
// header is present (e.g. an empty file-level unit).
bool lecfail_parse_header(std::string_view text, Lecfail_mod& m) {
  size_t ns = lecfail_header_name_pos(text);
  if (ns == std::string_view::npos) {
    return false;
  }
  size_t p = ns;
  while (p < text.size() && text[p] != ':' && text[p] != '(' && text[p] != ' ' && text[p] != '\t' && text[p] != '\n') {
    ++p;
  }
  m.name.assign(text.substr(ns, p - ns));
  if (m.name.empty()) {
    return false;
  }
  if (text.compare(p, 3, "::[") == 0) {  // step over the `::[..]` attribute block
    const size_t rb = lecfail_match_bracket(text, p + 2);
    if (rb == std::string_view::npos) {
      return false;
    }
    p = rb + 1;
  }
  size_t io = text.find('(', p);
  if (io == std::string_view::npos) {
    return false;
  }
  size_t ic = lecfail_match_bracket(text, io);
  if (ic == std::string_view::npos) {
    return false;
  }
  size_t body  = text.find('{', ic);
  size_t arrow = text.find("->", ic);
  if (arrow != std::string_view::npos && (body == std::string_view::npos || arrow < body)) {
    size_t oo = text.find('(', arrow);
    size_t oc = oo == std::string_view::npos ? std::string_view::npos : lecfail_match_bracket(text, oo);
    if (oo != std::string_view::npos && oc != std::string_view::npos && (body == std::string_view::npos || oc < body)) {
      lecfail_parse_io(text.substr(oo + 1, oc - oo - 1), m.outputs);
    }
  }
  lecfail_parse_io(text.substr(io + 1, ic - io - 1), m.inputs);
  for (const auto& [n, t] : m.inputs) {
    lecfail_expand_leaves(n, t, m.in_leaves);
  }
  return true;
}

// Parse every lambda header in one .prp's text. pass.prp_writer emits
// one file per SOURCE FILE, so a file can hold SEVERAL lambdas (`<file>.prp`
// carries the file scope plus every `pub mod` lifted out of it) — parse them
// all, or a top that is not the file's first lambda goes missing and the
// simfail testbench is silently skipped. Each module's `text` is its own slice
// plus the file prologue (the imports it may need).
void lecfail_parse_text(const std::string& text, std::vector<Lecfail_mod>& mods) {
  {
    // Start of every lambda header LINE, in order. (kept in a block so the
    // per-file locals below stay scoped exactly as when this was inlined)
    std::vector<size_t> starts;
    for (size_t at = 0; at < text.size();) {
      const size_t np = lecfail_header_name_pos(std::string_view(text).substr(at));
      if (np == std::string_view::npos) {
        break;
      }
      const size_t abs = at + np;
      const size_t bol = text.rfind('\n', abs);
      starts.push_back(bol == std::string::npos ? 0 : bol + 1);
      // Continue past this header's line.
      const size_t nl = text.find('\n', abs);
      if (nl == std::string::npos) {
        break;
      }
      at = nl + 1;
    }
    if (starts.empty()) {
      return;  // a file-level unit with no lambda (e.g. a package)
    }
    // The file prologue (its `const X = import(…)` header) belongs to the FILE,
    // not to each lambda: attaching it to every slice duplicates those bindings
    // once per lambda in the concatenated testbench, which then fails to compile
    // on the redeclaration. Give it to the FIRST slice only — the fallback
    // concatenates whole modules of one file in order, so one copy still leads.
    const std::string prologue = text.substr(0, starts.front());
    bool              first    = true;
    for (size_t i = 0; i < starts.size(); ++i) {
      const size_t      end  = (i + 1 < starts.size()) ? starts[i + 1] : text.size();
      const std::string body = text.substr(starts[i], end - starts[i]);
      Lecfail_mod       m;
      if (!lecfail_parse_header(body, m)) {
        continue;
      }
      m.text = first ? prologue + body : body;
      first  = false;
      mods.push_back(std::move(m));
    }
  }
}

std::vector<Lecfail_mod> lecfail_parse_dir(const std::string& dir) {
  std::vector<Lecfail_mod> mods;
  std::error_code          ec;
  for (auto& de : fs::directory_iterator(dir, ec)) {
    if (!de.is_regular_file() || de.path().extension() != ".prp") {
      continue;
    }
    std::ifstream     ifs(de.path());
    std::stringstream ss;
    ss << ifs.rdbuf();
    lecfail_parse_text(ss.str(), mods);
  }
  std::sort(mods.begin(), mods.end(), [](const Lecfail_mod& a, const Lecfail_mod& b) { return a.name < b.name; });
  return mods;
}

// The same parse over ONE .prp, used for the import form: the testbench then
// references the original source verbatim, so its HEADERS are all that is
// needed and there is nothing to re-emit.
std::vector<Lecfail_mod> lecfail_parse_file(const std::string& path) {
  std::ifstream ifs(path);
  if (!ifs.is_open()) {
    return {};
  }
  std::stringstream ss;
  ss << ifs.rdbuf();
  std::vector<Lecfail_mod> mods;
  lecfail_parse_text(ss.str(), mods);
  std::sort(mods.begin(), mods.end(), [](const Lecfail_mod& a, const Lecfail_mod& b) { return a.name < b.name; });
  return mods;
}

// pass.prp_writer emits one file per SOURCE FILE, so an emitted sibling
// dependency is imported as `<file>.<entity>`. The simfail fallback below
// concatenates both re-emitted hierarchies into one self-contained file; those
// per-file imports must therefore be removed, or they both duplicate the
// now-local definitions and point at side scratch directories that are not
// inputs to the replay. A PACKAGE import is the whole-namespace form
// (`import("pkg")`, no dot) and remains untouched.
std::string lecfail_strip_sibling_imports(std::string_view text) {
  std::string out;
  for (size_t pos = 0; pos < text.size();) {
    const size_t nl   = text.find('\n', pos);
    const size_t end  = nl == std::string_view::npos ? text.size() : nl;
    const auto   line = text.substr(pos, end - pos);
    bool         drop = false;
    if (line.starts_with("const ")) {
      const size_t imp = line.find(" = import(\"");
      const size_t q2  = imp == std::string_view::npos ? std::string_view::npos : line.find("\")", imp + 11);
      if (q2 != std::string_view::npos) {
        const auto path = line.substr(imp + 11, q2 - (imp + 11));
        drop            = path.find('.') != std::string_view::npos;  // `file.entity` — a sibling lambda
      }
    }
    if (!drop) {
      out.append(line);
      if (nl != std::string_view::npos) {
        out += '\n';
      }
    }
    if (nl == std::string_view::npos) {
      break;
    }
    pos = nl + 1;
  }
  return out;
}

// True when the name starting at `i` is preceded by a lambda keyword — i.e. it
// is a DEFINITION site (`mod from`, `comb from`, `pipe[2] from`, ...) rather
// than a use. Walks back over the separating whitespace, an optional `pipe`
// latency bracket, and then the keyword token.
bool lecfail_is_def_site(const std::string& s, size_t i) {
  size_t k = i;
  while (k > 0 && (s[k - 1] == ' ' || s[k - 1] == '\t')) {
    --k;
  }
  if (k == i) {
    return false;  // a keyword is always separated from the name by whitespace
  }
  if (k > 0 && s[k - 1] == ']') {
    const size_t lb = s.rfind('[', k - 1);
    if (lb == std::string::npos) {
      return false;
    }
    k = lb;
  }
  const size_t e = k;
  while (k > 0 && ((std::isalnum(static_cast<unsigned char>(s[k - 1])) != 0) || s[k - 1] == '_')) {
    --k;
  }
  const std::string_view tok(s.data() + k, e - k);
  for (const auto kw : lecfail_lambda_kws) {
    if (tok == kw) {
      return true;
    }
  }
  return false;
}

// Rename module `from` -> `to` ONLY at a definition (`mod from` / `comb from`)
// or an instantiation (`from(`) site — never inside a string/type/signal (so a
// stale `lg="side.from"` reference and any signal named like a module are
// untouched).
std::string lecfail_rename(const std::string& s, const std::string& from, const std::string& to) {
  auto        is_ident = [](char c) { return (std::isalnum(static_cast<unsigned char>(c)) != 0) || c == '_'; };
  std::string out;
  out.reserve(s.size());
  size_t i = 0;
  while (i < s.size()) {
    if (s.compare(i, from.size(), from) == 0) {
      bool   lb = i == 0 || !is_ident(s[i - 1]);
      size_t j  = i + from.size();
      bool   rb = j >= s.size() || !is_ident(s[j]);
      if (lb && rb) {
        bool def  = lecfail_is_def_site(s, i);
        bool inst = j < s.size() && s[j] == '(';
        if (def || inst) {
          out += to;
          i    = j;
          continue;
        }
      }
    }
    out += s[i];
    ++i;
  }
  return out;
}

// Give each UNTYPED module parameter an explicit `:u<width>` when its name is a
// known primary input (from the witness trace). prp_writer re-emits inputs
// untyped (`mod adder(en)`), but Pyrope needs an explicit width at an internal
// `mod` INSTANTIATION boundary — so a hierarchical DUT (a top whose sub-module
// takes a threaded top input) otherwise fails to re-compile. The top's own
// inputs are typed by the wrapper regardless; this fixes the internal boundaries.
std::string lecfail_type_params(const std::string& text, const absl::flat_hash_map<std::string, int>& width_of) {
  size_t mp = lecfail_header_name_pos(text);
  if (mp == std::string::npos) {
    return text;
  }
  // Step over a `::[..]` attribute block, which can hold a '(' of its own.
  size_t after = text.find_first_of("(: \t\n", mp);
  if (after == std::string::npos) {
    return text;
  }
  if (text.compare(after, 3, "::[") == 0) {
    const size_t rb = lecfail_match_bracket(text, after + 2);
    if (rb == std::string::npos) {
      return text;
    }
    after = rb + 1;
  }
  size_t io = text.find('(', after);
  size_t ic = io == std::string::npos ? std::string::npos : lecfail_match_bracket(text, io);
  if (io == std::string::npos || ic == std::string::npos || ic <= io + 1) {
    return text;  // no header params
  }
  // The same depth-aware split the header parser uses: a struct param carries
  // commas and colons of its own, and both would corrupt a flat rebuild.
  std::vector<std::pair<std::string, std::string>> decls;
  lecfail_parse_io(std::string_view(text).substr(io + 1, ic - io - 1), decls);
  std::string rebuilt;
  bool        changed = false;
  for (const auto& [name, suffix] : decls) {
    std::string raw = name + suffix;
    if (suffix.empty()) {  // untyped
      if (auto it = width_of.find(name); it != width_of.end()) {
        raw     = name + std::format(":U{}", it->second);
        changed = true;
      }
    }
    rebuilt += (rebuilt.empty() ? "" : ", ") + raw;
  }
  if (!changed) {
    return text;
  }
  return text.substr(0, io + 1) + rebuilt + text.substr(ic);
}

// Re-emit one --impl/--ref side as Pyrope (LNAST -> .prp) by shelling to a fresh
// `lhd compile ... --emit-dir pyrope:` (clean process isolation; reuses the
// tested front-end + prp_writer flow). Returns false when the side has no LNAST
// path (lg:/yosys-verilog) or the compile fails.
bool lecfail_emit_side(const std::string& lhd_bin, const Options& opts, const std::string& kind, const std::string& path,
                       const std::string& outdir, const std::string& scratch, const std::string& log) {
  ensure_dir(outdir);
  ensure_dir(scratch);
  std::string sidearg = kind == "lg" ? "lg:" + path : (kind == "ln" ? "ln:" + path : path);
  std::string cmd     = shell_quote(lhd_bin) + " compile " + shell_quote(sidearg) + " --emit-dir " + shell_quote("pyrope:" + outdir)
                        + " --workdir " + shell_quote(scratch);
  if (kind == "verilog") {
    cmd += " --reader " + shell_quote(opts.reader);
  }
  cmd    += " >> " + shell_quote(log) + " 2>&1";
  int st  = std::system(cmd.c_str());
  return WIFEXITED(st) && WEXITSTATUS(st) == 0;
}

// True when the .prp at `path` declares `top` as a `pub` lambda — the precondition
// for `import("<stem>.<top>")` to resolve it. (A lec top compiled from a bare
// `mod dut` is not importable; the generator then falls back to inlining.)
// Text scan: a `pub <kind> <top>` where <top> ends the token (`(`, `:`, or space).
bool lecfail_prp_top_is_pub(const std::string& path, const std::string& top) {
  std::ifstream ifs(path);
  if (!ifs.is_open()) {
    return false;
  }
  std::stringstream ss;
  ss << ifs.rdbuf();
  const std::string text = ss.str();
  for (const char* kw : {"mod", "comb", "pipe", "fluid"}) {
    const std::string needle = std::string("pub ") + kw + " " + top;
    for (size_t p = text.find(needle); p != std::string::npos; p = text.find(needle, p + 1)) {
      const size_t e     = p + needle.size();
      const char   after = e < text.size() ? text[e] : ' ';
      if (after == '(' || after == ':' || after == ' ' || after == '\t' || after == '\n' || after == '\r') {
        return true;
      }
    }
  }
  return false;
}

// F7 machine-readable witness artifact (mirrors `lhd sim --result-json`): the full
// driven trace + the source-mapped root cut, written next to the .prp testbench, so
// CI / auto-triage tools consume a LEC/verify failure without scraping the human
// witness string. The input sequence here is dumped from the SAME `Witness_trace`
// that drives the .prp `_drv_*` arrays, so the two match by construction.
static void emit_witness_json(const std::string& path, std::string_view kind, std::string_view impl, std::string_view ref,
                              const livehd::lec::Witness_trace& t) {
  auto esc = [](std::string_view s) {
    std::string o;
    for (char c : s) {
      switch (c) {
        case '"' : o += "\\\""; break;
        case '\\': o += "\\\\"; break;
        case '\n': o += "\\n"; break;
        case '\t': o += "\\t"; break;
        case '\r': o += "\\r"; break;
        default:
          if (static_cast<unsigned char>(c) < 0x20) {
            o += std::format("\\u{:04x}", static_cast<int>(static_cast<unsigned char>(c)));
          } else {
            o += c;
          }
      }
    }
    return o;
  };
  // Split the root cut's "file:line" (start_line is 1-based; 0 = unmapped).
  std::string root_file = t.root_src, root_line = "0";
  if (auto p = t.root_src.rfind(':'); p != std::string::npos) {
    root_file = t.root_src.substr(0, p);
    root_line = t.root_src.substr(p + 1);
  }
  std::string j  = "{\n";
  j             += std::format("  \"schema_version\": 1,\n  \"kind\": \"{}\",\n", esc(kind));
  j             += std::format("  \"impl\": \"{}\",\n  \"ref\": \"{}\",\n", esc(impl), esc(ref));
  j             += std::format("  \"reset_cycles\": {},\n  \"diverge_cycle\": {},\n", t.reset_cycles, t.diverge_cycle);
  j             += "  \"diverge_outputs\": [";
  for (size_t i = 0; i < t.diverge_outputs.size(); ++i) {
    j += std::format("{}\"{}\"", i ? ", " : "", esc(t.diverge_outputs[i]));
  }
  j += "],\n";
  if (t.root_cycle >= 0) {
    j += std::format(
        "  \"root_cut\": {{\"key\": \"{}\", \"cycle\": {}, \"ref\": \"{}\", \"impl\": \"{}\", \"file\": \"{}\", \"line\": {}}},\n",
        esc(t.root_key),
        t.root_cycle,
        esc(t.root_ref),
        esc(t.root_impl),
        esc(root_file),
        root_line.empty() ? "0" : root_line);
  } else {
    j += "  \"root_cut\": null,\n";
  }
  j += "  \"trace\": {\n    \"cycles\": [\n";
  for (size_t c = 0; c < t.cycles.size(); ++c) {
    const auto& cy  = t.cycles[c];
    j              += std::format("      {{\"reset_asserted\": {}, \"inputs\": [", cy.reset_asserted ? "true" : "false");
    for (size_t i = 0; i < cy.inputs.size(); ++i) {
      const auto& in  = cy.inputs[i];
      j              += std::format("{}{{\"name\": \"{}\", \"value\": \"{}\", \"width\": {}}}",
                                    i ? ", " : "",
                                    esc(in.name),
                                    esc(in.value),
                                    in.width);
    }
    j += std::format("]}}{}\n", c + 1 < t.cycles.size() ? "," : "");
  }
  j += "    ]\n  }\n}\n";
  std::ofstream ofs(path);
  if (ofs.is_open()) {
    ofs << j;
  }
}

// The generator proper. `impl_top`/`ref_top` are the two designs' TOP graph names
// (unqualified names are matched against the re-emitted modules).
void emit_lecfail_witness(Options& opts, Result& res, const livehd::lec::Query_result& r, const std::string& impl_top_full,
                          const std::string& ref_top_full, const std::string& simfail, bool run_sim) {
  auto skip = [&](std::string_view why) {
    livehd::diag::info("pass.lec", "simfail-skip", "io").msg("formal.simfail testbench not generated: {}", why).emit();
  };
  if (r.trace.empty()) {
    skip("the verdict carries no reproducible input trace (inductive single-step CEX, or witnesses disabled)");
    return;
  }

  const std::string simfail_path = opts.workdir + "/" + simfail;
  // Preserve the full counterexample even when a graph-only side has no LNAST
  // and cannot be re-emitted as a Pyrope simulation testbench.
  const std::string json_path    = simfail_path.substr(0, simfail_path.size() - 4) + ".json";
  emit_witness_json(json_path, "simfail", opts.impl_path, opts.ref_path, r.trace);
  if (fs::exists(json_path)) {
    res.outputs.push_back(json_path);
  }
  // Test name = the .prp basename stem, sanitized to a Pyrope identifier; it is
  // also the sole sim instance's VCD stem (`<workdir>/<stem>.vcd`).
  std::string stem = fs::path(simfail_path).stem().string();
  std::string test_name;
  for (char c : stem) {
    test_name += (std::isalnum(static_cast<unsigned char>(c)) != 0) ? c : '_';
  }
  if (test_name.empty() || (std::isdigit(static_cast<unsigned char>(test_name[0])) != 0)) {
    test_name = "simfail_" + test_name;
  }

  // Phase 2/3 of the lec-on-failure flow: re-emitting both sides as Pyrope shells
  // out to `lhd` twice, so announce the target up front (the write itself is quick;
  // the side re-emit is the slow part).
  livehd::diag::info("pass.lec", "simfail-creating-prp", "progress")
      .msg("lec: creating counterexample simulation test {}", simfail_path)
      .emit();

  const std::string lhd_bin = livehd::file_utils::get_exe_path() + "/lhd";

  std::string       impl_top = lecfail_simple_name(impl_top_full);
  std::string       ref_top  = lecfail_simple_name(ref_top_full);
  const std::string wrapper  = "__simfail_dut_pair";

  // Prefer IMPORTING the original sources: the testbench then references the two
  // designs by `import("<file>.<top>")` instead of inlining renamed copies, so
  // fixing a bug in the original .prp and re-running the SAME simfail test picks
  // up the fix. Requires both sides to be Pyrope files (an lg:/verilog side has
  // no editable .prp to iterate on) with DISTINCT file stems — the stem is the
  // import unit name, and two same-named units would collide. Otherwise fall
  // back to the self-contained inline form (renamed copies) built below.
  const std::string impl_stem = fs::path(opts.impl_path).stem().string();
  const std::string ref_stem  = fs::path(opts.ref_path).stem().string();
  const bool        prp_pair
      = opts.impl_kind == "pyrope" && opts.ref_kind == "pyrope" && !impl_stem.empty() && !ref_stem.empty() && impl_stem != ref_stem;
  const bool impl_pub   = prp_pair && lecfail_prp_top_is_pub(opts.impl_path, impl_top);
  const bool ref_pub    = prp_pair && lecfail_prp_top_is_pub(opts.ref_path, ref_top);
  const bool can_import = prp_pair && impl_pub && ref_pub;

  // A Pyrope pair that qualifies EXCEPT for a non-`pub` top gets the inline copy
  // (which cannot iterate on the original). Nudge the user to opt into the import
  // form — `import("<file>.<top>")` needs the top to be `pub`. Emitted BEFORE the
  // re-emit below, because that re-emit is the step marking the top `pub` avoids,
  // and it is also the step most likely to fail.
  if (prp_pair && !can_import) {
    std::string which;
    if (!impl_pub) {
      which = std::format("the impl top `{}` in {}", impl_top, opts.impl_path);
    }
    if (!ref_pub) {
      which += (which.empty() ? "" : " and ") + std::format("the ref top `{}` in {}", ref_top, opts.ref_path);
    }
    livehd::diag::warn("pass.lec", "simfail-top-not-pub", "io")
        .msg(
            "the simfail test inlines a COPY of each design because a LEC top is not `pub` ({}) — mark the LEC top `pub` "
            "and the testbench will `import` the original instead, so a fix to the .prp flows into a re-run",
            which)
        .hint(std::format("e.g. `pub mod {}(...)` / `pub comb {}(...)`", impl_top, ref_top))
        .emit();
  }

  // Where the two hierarchies come from. The IMPORT form references the original
  // .prp verbatim, so only the two HEADERS are needed and the sources are parsed
  // straight from disk. Round-tripping a Pyrope side back out through
  // pass.prp_writer would be pure cost there — and it is the one step that can
  // fail for a reason the LEC verdict does not care about (a construct the WRITER
  // cannot emit, e.g. `popcount`), which used to cost the user the whole
  // counterexample testbench on a refutation that was otherwise fully reproduced.
  // Only the INLINE form, which needs each side's module TEXT, re-emits.
  std::vector<Lecfail_mod> impl_mods;
  std::vector<Lecfail_mod> ref_mods;
  if (can_import) {
    impl_mods = lecfail_parse_file(opts.impl_path);
    ref_mods  = lecfail_parse_file(opts.ref_path);
    if (impl_mods.empty() || ref_mods.empty()) {
      skip("no lambda header could be parsed from a side's .prp");
      return;
    }
  } else {
    const std::string impl_dir = opts.workdir + "/lecfail_impl_prp";
    const std::string ref_dir  = opts.workdir + "/lecfail_ref_prp";
    const std::string log      = next_log_path(opts, "formal.simfail");
    if (!lecfail_emit_side(lhd_bin, opts, opts.impl_kind, opts.impl_path, impl_dir, opts.workdir + "/lecfail_impl_w", log)
        || !lecfail_emit_side(lhd_bin, opts, opts.ref_kind, opts.ref_path, ref_dir, opts.workdir + "/lecfail_ref_w", log)) {
      skip(std::format("a side could not be re-emitted as Pyrope (lg:/yosys-verilog sides have no LNAST); see {}", log));
      return;
    }
    impl_mods = lecfail_parse_dir(impl_dir);
    ref_mods  = lecfail_parse_dir(ref_dir);
    if (impl_mods.empty() || ref_mods.empty()) {
      skip("no Pyrope modules were re-emitted for a side");
      return;
    }
  }

  // Rename any ref-side module whose name clashes with an impl-side module (or
  // the wrapper) so both hierarchies coexist in one Pyrope namespace. Only the
  // inline (non-import) form shares a namespace; an import keeps each side's
  // modules under its own unit, so the two `<file>.<top>` graphs never collide.
  absl::flat_hash_map<std::string, std::string> ref_rename;
  if (!can_import) {
    absl::flat_hash_set<std::string> impl_names;
    for (const auto& m : impl_mods) {
      impl_names.insert(m.name);
    }
    for (const auto& m : ref_mods) {
      if (impl_names.count(m.name) != 0 || m.name == wrapper) {
        ref_rename[m.name] = "lecref_" + m.name;
      }
    }
    for (auto& m : ref_mods) {
      for (const auto& [from, to] : ref_rename) {
        m.text = lecfail_rename(m.text, from, to);
      }
    }
    for (auto& m : ref_mods) {
      if (auto it = ref_rename.find(m.name); it != ref_rename.end()) {
        m.name = it->second;
      }
    }
    if (auto it = ref_rename.find(ref_top); it != ref_rename.end()) {
      ref_top = it->second;
    }
  }

  auto find_mod = [](const std::vector<Lecfail_mod>& mods, const std::string& name) -> const Lecfail_mod* {
    for (const auto& m : mods) {
      if (m.name == name) {
        return &m;
      }
    }
    return nullptr;
  };
  const Lecfail_mod* impl_m = find_mod(impl_mods, impl_top);
  const Lecfail_mod* ref_m  = find_mod(ref_mods, ref_top);
  if (impl_m == nullptr || ref_m == nullptr || impl_m->outputs.empty() || ref_m->outputs.empty()) {
    skip("could not locate both TOP modules (or a side exposes no outputs) in the re-emitted Pyrope");
    return;
  }

  // Per-input bit width (any cycle carrying the input), for unsigned wrapper-input
  // typing — driving the unsigned magnitude then reproduces the exact bit pattern.
  absl::flat_hash_map<std::string, int> width_of;
  for (const auto& cyc : r.trace.cycles) {
    for (const auto& in : cyc.inputs) {
      width_of[in.name] = in.width < 1 ? 1 : in.width;
    }
  }
  // One wrapper port. A STRUCT input is declared here as one flat scalar per
  // LEAF (`io.bits.x` -> `io__bits__x`), because a `test` block pokes a named
  // top-level port; the struct is rebuilt at the two call sites, where the
  // per-leaf actual (`io.bits.x = io__bits__x`) is the spelling prp_writer
  // itself emits. A scalar port keeps its own name, so the flat case is byte
  // identical to what this generated before leaves existed.
  struct Wport {
    std::string dotted;  // trace / call-site name
    std::string flat;    // wrapper port + `_drv_` array name
    std::string decl;    // declared ":u4" suffix, "" when untyped
  };
  auto wtype = [&](const Wport& w) {
    if (lecfail_is_bool_type(w.decl)) {
      return std::string{":Bool"};  // the declaring side's kind; the trace carries it as 0/1
    }
    if (lecfail_is_clock_type(w.decl)) {
      return std::string{":Clock"};
    }
    if (lecfail_is_reset_type(w.decl)) {
      return std::string{":Reset"};
    }
    if (auto it = width_of.find(lecfail_trace_name(w.dotted)); it != width_of.end()) {
      return std::format(":U{}", it->second);
    }
    // Not in the trace (the solver never constrained it): keep the DECLARED
    // type rather than defaulting to :u1, which would silently truncate.
    if (!w.decl.empty()) {
      const size_t at = w.decl.find("@[");
      return at == std::string::npos ? w.decl : w.decl.substr(0, at);
    }
    return std::string{":U1"};
  };

  // Union of the two tops' input LEAVES (order: impl first, then ref extras).
  std::vector<Wport>                            win;
  absl::flat_hash_map<std::string, std::string> flat_of;  // dotted -> wrapper port
  absl::flat_hash_set<std::string>              flat_used;
  auto                                          add_leaves = [&](const std::vector<Lecfail_leaf>& leaves) {
    for (const auto& lf : leaves) {
      if (flat_of.contains(lf.dotted)) {
        continue;
      }
      std::string flat;
      for (char c : lf.dotted) {
        if (c == '.') {
          flat += "__";
        } else if ((std::isalnum(static_cast<unsigned char>(c)) != 0) || c == '_') {
          flat += c;
        }  // a backtick or other punctuation in a quoted name is dropped
      }
      if (flat.empty() || (std::isdigit(static_cast<unsigned char>(flat[0])) != 0)) {
        flat = "p_" + flat;
      }
      while (!flat_used.insert(flat).second) {  // a real port could already spell it
        flat += "_";
      }
      flat_of[lf.dotted] = flat;
      win.push_back({lf.dotted, flat, lf.type});
    }
  };
  add_leaves(impl_m->in_leaves);
  add_leaves(ref_m->in_leaves);

  // ---- build the wrapper module -------------------------------------------
  std::string sig_in;
  for (const auto& w : win) {
    sig_in += (sig_in.empty() ? "" : ", ") + lecfail_flat_ident(w.dotted, w.flat) + wtype(w);
  }
  // Every `mod` output MUST declare a landing cycle, and a re-emitted `comb`
  // side carries only a type (`:u64`) — so a wrapper over a combinational DUT
  // needs an explicit `@[]` (the unconstrained opt-out) or it does not compile.
  auto ocycle
      = [](std::string_view suf) { return suf.find("@[") == std::string_view::npos ? std::string{suf} + "@[]" : std::string{suf}; };
  std::string sig_out;
  for (const auto& [n, suf] : impl_m->outputs) {
    sig_out += (sig_out.empty() ? "" : ", ") + std::format("impl_{}{}", n, ocycle(suf));
  }
  for (const auto& [n, suf] : ref_m->outputs) {
    sig_out += (sig_out.empty() ? "" : ", ") + std::format("ref_{}{}", n, ocycle(suf));
  }
  // Per-LEAF actuals: a struct port is bound field by field, and a leaf the
  // wrapper does not carry (a field only the OTHER side declares) is skipped.
  // The wrapper port takes the kind of the side that declared it first, so a
  // side declaring the other kind gets an explicit cast (bool ports are strict).
  absl::flat_hash_set<std::string> bool_port;   // wrapper ports declared `Bool`
  absl::flat_hash_set<std::string> clock_port;  // wrapper ports declared `Clock`
  absl::flat_hash_set<std::string> reset_port;  // wrapper ports declared `Reset`
  for (const auto& w : win) {
    if (lecfail_is_bool_type(w.decl)) {
      bool_port.insert(w.flat);
    } else if (lecfail_is_clock_type(w.decl)) {
      clock_port.insert(w.flat);
    } else if (lecfail_is_reset_type(w.decl)) {
      reset_port.insert(w.flat);
    }
  }
  auto call_args = [&](const Lecfail_mod* m) {
    std::string a;
    for (const auto& lf : m->in_leaves) {
      auto it = flat_of.find(lf.dotted);
      if (it == flat_of.end()) {
        continue;
      }
      std::string actual = lecfail_flat_ident(lf.dotted, it->second);
      const bool  sig    = lecfail_is_clock_type(lf.type) || lecfail_is_reset_type(lf.type) || clock_port.contains(it->second)
                           || reset_port.contains(it->second);
      if (const bool want = lecfail_is_bool_type(lf.type); !sig && want != bool_port.contains(it->second)) {
        actual = std::format("{}({})", want ? "Bool" : "U1", actual);
      } else if (const int sw = lecfail_signed_width(lf.type); !sig && !want && sw > 0) {
        // The wrapper drives an integer port as the unsigned bits the trace
        // carries; a signed port reads those bits with its own sign (LEC pairs
        // `input [3:0] b` with `input signed [3:0] b` as one bus), so pass it
        // the sign-extended low bits rather than an out-of-range magnitude.
        actual = std::format("{}#sext[0..={}]", actual, sw - 1);
      }
      a += (a.empty() ? "" : ", ") + std::format("{} = {}", lf.dotted, actual);
    }
    return a;
  };
  auto side_body = [&](const std::string& top, const Lecfail_mod* m, const std::string& prefix, const std::string& tmp) {
    std::string b;
    if (m->outputs.size() == 1) {
      b += std::format("  {}{} = {}({})\n", prefix, m->outputs[0].first, top, call_args(m));
    } else {
      // A stateful multi-output instance is bound to a fresh local (needs `const`,
      // like prp_writer's own emission), then each output read as `inst.port`.
      b += std::format("  const {} = {}({})\n", tmp, top, call_args(m));
      for (const auto& [n, suf] : m->outputs) {
        b += std::format("  {}{} = {}.{}\n", prefix, n, tmp, n);
      }
    }
    return b;
  };
  // The wrapper calls each side by the imported const name (`implmod`/`refmod`)
  // when importing, else by the (possibly renamed) inlined module name.
  const std::string impl_callee  = can_import ? std::string{"implmod"} : impl_top;
  const std::string ref_callee   = can_import ? std::string{"refmod"} : ref_top;
  std::string       wrap_text    = std::format("mod {}({}) -> ({}) {{\n", wrapper, sig_in, sig_out);
  wrap_text                     += side_body(impl_callee, impl_m, "impl_", "_lec_impl");
  wrap_text                     += side_body(ref_callee, ref_m, "ref_", "_lec_ref");
  wrap_text                     += "}\n";

  // ---- build the test: per-cycle stimulus arrays indexed by `clock` -------
  const int ncyc   = static_cast<int>(r.trace.cycles.size());
  auto      val_at = [&](const std::string& name, int c) -> std::string {
    for (const auto& in : r.trace.cycles[static_cast<size_t>(c)].inputs) {
      if (in.name == name) {
        return in.value;
      }
    }
    return "0";
  };
  // The implicit reset: a trace input named `reset` that is NOT a declared port
  // (Pyrope-origin designs drive their registers off it). An explicit reset PORT
  // is instead driven by name like any other input.
  const bool reset_is_port
      = std::any_of(win.begin(), win.end(), [](const Wport& w) { return lecfail_trace_name(w.dotted) == "reset"; });
  const bool implicit_reset = width_of.count("reset") != 0 && !reset_is_port;

  std::string test_text = std::format("test {} {{\n  mut _lec_dut = {}\n", test_name, wrapper);
  for (const auto& w : win) {
    std::string arr;
    for (int c = 0; c < ncyc; ++c) {
      arr += (arr.empty() ? "" : ", ") + val_at(lecfail_trace_name(w.dotted), c);
    }
    test_text += std::format("  const _drv_{} = [{}]\n", w.flat, arr);
  }
  if (implicit_reset) {
    std::string arr;
    for (int c = 0; c < ncyc; ++c) {
      arr += (arr.empty() ? "" : ", ") + val_at("reset", c);
    }
    test_text += std::format("  const _drv_reset = [{}]\n", arr);
  }
  test_text += std::format("  tick {} {{\n", ncyc);
  // Reset drive: an explicit `reset` PORT is driven by the input loop below (and
  // the sim unifies it with the implicit reset). An IMPLICIT reset (the trace
  // carries `reset` values but the decl list does not) is driven from the trace.
  // A design with NO reset at all (e.g. slang-imported, reset-less flops) gets
  // no drive — `_dut.reset` would be an unknown field and fail the replay.
  if (implicit_reset) {
    // A minted reset is a Reset like a declared one: drive it a Bool (`!= 0`),
    // exactly as the declared-Reset ports below, never a bare integer.
    test_text += "    _lec_dut.`reset` = _drv_reset[`clock`] != 0\n";
  }
  for (const auto& w : win) {
    if (clock_port.contains(w.flat)) {
      continue;  // a Clock is the sim clock, never poked
    }
    test_text += std::format("    _lec_dut.{} = _drv_{}[`clock`]{}\n",
                             lecfail_flat_ident(w.dotted, w.flat),
                             w.flat,
                             bool_port.contains(w.flat) || reset_port.contains(w.flat) ? " != 0" : "");
  }
  test_text += "    step\n  }\n}\n";

  // ---- assemble the simfail test ------------------------------------------
  std::string divtxt;
  for (const auto& d : r.trace.diverge_outputs) {
    divtxt += (divtxt.empty() ? "" : ", ") + d;
  }
  // The reproduce/iterate command. The import form passes BOTH original sources
  // positionally so their `import("<stem>.<top>")` resolve to the co-loaded units
  // (edit either .prp, re-run, the fix flows through); the inline form is a single
  // self-contained file.
  const std::string rerun
      = can_import ? std::format("lhd sim {} {} {} --set sim.vcd=true --workdir <dir>", opts.impl_path, opts.ref_path, simfail_path)
                   : std::format("lhd sim {} --set sim.vcd=true --workdir <dir>", simfail_path);
  // F7: the source-mapped root cut — the first diverging STATE cut the output
  // inherits, stamped with the impl-side `file:line` of the flop's declaration.
  std::string rootcut;
  if (r.trace.root_cycle >= 0) {
    rootcut = std::format("// Root cut: {} (ref={} impl={}){}\n",
                          r.trace.root_key,
                          r.trace.root_ref,
                          r.trace.root_impl,
                          r.trace.root_src.empty() ? std::string{} : std::format(" at {}", r.trace.root_src));
  }
  std::string out = std::format(
      "/*\n:name: {}\n:type: simulation\n*/\n"
      "// AUTO-GENERATED by `lhd lec` from a REFUTED counterexample.\n"
      "// impl='{}'  ref='{}'\n"
      "// Drives BOTH designs with the failing input sequence ({} cycle(s), {} reset-hold).\n"
      "// Divergence at cycle {}: {}\n"
      "{}"
      "// Re-run:  {}   (dumps {}.vcd)\n\n",
      test_name,
      opts.impl_path,
      opts.ref_path,
      ncyc,
      r.trace.reset_cycles,
      r.trace.diverge_cycle,
      divtxt.empty() ? "(see verdict)" : divtxt,
      rootcut,
      rerun,
      test_name);
  if (can_import) {
    // Reference the ORIGINAL sources by their `<file-stem>.<top>` import key.
    out += std::format("const implmod = import(\"{}.{}\")\n", impl_stem, impl_top);
    out += std::format("const refmod  = import(\"{}.{}\")\n\n", ref_stem, ref_top);
  } else {
    // Inline both re-emitted hierarchies (ref-side clashes already renamed).
    for (const auto& m : impl_mods) {
      out += lecfail_type_params(lecfail_strip_sibling_imports(m.text), width_of);
      if (!out.empty() && out.back() != '\n') {
        out += '\n';
      }
      out += '\n';
    }
    for (const auto& m : ref_mods) {
      out += lecfail_type_params(lecfail_strip_sibling_imports(m.text), width_of);
      if (!out.empty() && out.back() != '\n') {
        out += '\n';
      }
      out += '\n';
    }
  }
  out += wrap_text + "\n" + test_text;

  std::ofstream ofs(simfail_path);
  if (!ofs.is_open()) {
    skip(std::format("could not write {}", simfail_path));
    return;
  }
  ofs << out;
  ofs.close();
  res.outputs.push_back(simfail_path);
  res.recipe_steps.push_back(std::format("formal.simfail simulation test -> {}", simfail_path));
  std::print("lec: wrote counterexample simulation test {}\n", simfail_path);

  if (!run_sim) {
    return;
  }
  // Phase 3/3 of the lec-on-failure flow: `lhd sim` on the testbench dumps the
  // waveform; announce the target up front (the sim run is the slow part).
  livehd::diag::info("pass.lec", "simfail-creating-vcd", "progress")
      .msg("lec: creating counterexample waveform {}/{}.vcd", opts.workdir, test_name)
      .emit();
  // Run it: one instance -> one VCD at <workdir>/<test_name>.vcd.
  const std::string sim_log = next_log_path(opts, "formal.simfail_run");
  std::string       cmd     = shell_quote(lhd_bin) + " sim ";
  // Import form: pass both original sources positionally so the testbench's
  // `import("<stem>.<top>")` resolve to the co-loaded units.
  if (can_import) {
    cmd += shell_quote(opts.impl_path) + " " + shell_quote(opts.ref_path) + " ";
  }
  // A counterexample replay is an observation run: it must never create or
  // consult a sim tune store inside the lec/formal workdir.
  cmd += shell_quote(simfail_path) + " --set sim.vcd=true --set sim.tune.profile=off --workdir " + shell_quote(opts.workdir);
  // Forward any explicit sim-runtime header locations (sim.hlop_dir /
  // sim.iassert_dir) to the child sim host-compile — needed when `../hlop`
  // isn't beside the cwd (e.g. under `bazel test`, where the caller passes
  // them) — and the VCD style knob, so `lhd lec --set sim.vcd_fake_delay=false`
  // shapes the counterexample waveform too.
  for (const auto& [k, v] : opts.sets) {
    if ((k == "sim.hlop_dir" || k == "sim.iassert_dir" || k == "sim.vcd_fake_delay") && !v.empty()) {
      cmd += " --set " + shell_quote(k + "=" + v);
    }
  }
  cmd             += " >> " + shell_quote(sim_log) + " 2>&1";
  int         st   = std::system(cmd.c_str());
  std::string vcd  = std::format("{}/{}.vcd", opts.workdir, test_name);
  if (WIFEXITED(st) && WEXITSTATUS(st) == 0 && fs::exists(vcd)) {
    res.outputs.push_back(vcd);
    res.recipe_steps.push_back(std::format("formal.simfail_run VCD -> {}", vcd));
    std::print("lec: wrote counterexample waveform {}\n", vcd);
  } else {
    livehd::diag::warn("pass.lec", "simfail-sim", "io")
        .msg("formal.simfail_run: `lhd sim {}` did not produce {} (see {})", simfail_path, vcd, sim_log)
        .emit();
  }
}

void lec_command(Options& opts, Result& res) {
  // Whether the USER passed --workdir (captured before load_side_graphs' first
  // workdir() call fabricates a scratch temp dir): the lecfail witness testbench
  // + VCD are on-by-default only for a persistent, user-named --workdir.
  const bool workdir_set = !opts.workdir.empty() && !opts.workdir_scratch;
  setup_diag(opts, "lec");
#ifndef NDEBUG
  // NDEBUG is only defined under `-c opt`; a dbg/fastbuild binary runs the SMT
  // discharge far slower, so nudge the user toward an optimized build first.
  livehd::diag::info("pass.lec", "lec-debug-build-slow", "progress")
      .msg("lec is slow and you compile without optimizations. Maybe `bazel build -c opt //...`")
      .emit();
#endif
  if (opts.impl_path.empty() || opts.ref_path.empty()) {
    throw Lhd_error{"usage",
                    "lec requires --impl KIND:PATH and --ref KIND:PATH",
                    "sides: verilog:/pyrope:/ln:/lg: or a bare .v/.sv/.prp path"};
  }

  // lgyosys is a debug comparison request: native Slang/default LEC always
  // runs first, followed by the independent lgcheck oracle.
  Eprp_var::Eprp_dict labels;
  merge_sets(opts, "formal", labels);      // the shared formal.* vocabulary
  merge_sets(opts, "formal.lec", labels);  // lec-specific canonical spelling wins
  auto label = [&](std::string_view k, std::string_view def) -> std::string {
    auto it = labels.find(std::string{k});
    return it == labels.end() ? std::string{def} : it->second;
  };
  const std::string requested_solver = label("solver", "cvc5");
  if (requested_solver != "cvc5" && requested_solver != "bitwuzla" && requested_solver != "lgyosys") {
    throw Lhd_error{"usage",
                    std::format("--set formal.solver expects cvc5|bitwuzla|lgyosys, got '{}'", requested_solver),
                    "cvc5 (default) | bitwuzla | lgyosys (default native LEC plus lgcheck comparison)"};
  }
  const bool        yosys_comparison = requested_solver == "lgyosys";
  const std::string solver           = yosys_comparison ? "cvc5" : requested_solver;
  livehd::lec::Effort effort;
  // DEFAULT CONTRACT (owner-approved 2026-10-13): formal.effort=test. An
  // explicit `proof` restores the proof-only defaults. An UNSET effort whose
  // other options a sampled preset cannot serve falls back to `proof` so those
  // flows keep working; an EXPLICIT sampled preset with them is a usage error.
  const std::string   effort_label    = label("effort", "");
  const bool          effort_explicit = !effort_label.empty();
  try {
    effort = livehd::lec::parse_effort(effort_explicit ? effort_label : std::string{livehd::lec::kDefaultLecEffort});
  } catch (const std::invalid_argument& error) {
    throw Lhd_error{"usage", error.what(), "choose quick-check, test, long-check, verify, or proof"};
  }
  if (!effort_explicit
      && (requested_solver != "cvc5" || (label("engine", "auto") != "auto" && label("engine", "auto") != "bmc")
          || (label("cross", "false") != "false" && label("cross", "false") != "0")
          || label("phase", "after_reset") != "after_reset")) {
    effort = livehd::lec::Effort::Legacy;
  }
  const bool sampled_effort = livehd::lec::is_sampled(effort);
  if (sampled_effort && (requested_solver != "cvc5" || (label("engine", "auto") != "auto" && label("engine", "auto") != "bmc"))) {
    throw Lhd_error{"usage",
                    "sampled effort presets require cvc5 and engine=auto or bmc",
                    "use formal.effort=verify for proof engines"};
  }
  if (sampled_effort && label("cross", "false") != "false" && label("cross", "false") != "0") {
    throw Lhd_error{"usage", "sampled effort presets do not produce a verdict for proof cross-checking", "omit formal.cross"};
  }

  // Formal BLOCKS are a `lhd formal verify` construct, not a lec one: a block
  // is an independent test, while lec has a single
  // obligation (impl == ref) that a block's assumes could only condition
  // globally — which is precisely the cross-block poisoning this separation
  // removes.
  // lec still honors the design's OWN assumes (fproperty Subs in the graph, see
  // query.cpp's graph_has_assume), so an environment constraint written in the
  // design tier reaches lec exactly as before. A sidecar is refused loudly
  // rather than silently ignored; re-admitting it later would be an explicit
  // opt-in flag, never a bare positional.
  if (!opts.files.empty()) {
    throw Lhd_error{"usage",
                    std::format("lec: unexpected positional input '{}'", opts.files.front()),
                    "lec takes no formal-block sidecar: blocks are independent tests, proved by `lhd formal verify "
                    "<design> <sidecar>`. An environment constraint for lec belongs in the design itself, where it is "
                    "in force for every check"};
  }
  if (!opts.formal_filter.empty()) {
    throw Lhd_error{"usage",
                    "lec: --formal selects formal blocks, which lec does not consume",
                    "use `lhd formal verify <design> <sidecar> --formal <glob>`"};
  }

  const bool   assume_check = label("assume_check", "true") != "false" && label("assume_check", "true") != "0";
  Eprp_var     ref_var;
  Eprp_var     impl_var;
  // pass.formal is still a compile pipeline stage with compile.formal.* labels.
  // Bridge the ONE canonical public option into that load gate; no second user
  // option is registered or documented.
  const size_t assume_sets = opts.sets.size();
  if (!assume_check) {
    opts.sets.emplace_back("compile.formal.assume_check", "false");
  }
  load_side_graphs(opts, res, opts.ref_kind, opts.ref_path, "ref", ref_var);
  load_side_graphs(opts, res, opts.impl_kind, opts.impl_path, "impl", impl_var);
  opts.sets.resize(assume_sets);
  dedup_inputs(res);  // each verilog: side re-records the --lib directories it materialized

  // "pass.lec" = the proof itself, everything after both sides are loaded. It
  // is NOT a run_step (lec drives the engine in-kernel rather than through
  // EPRP), so without this the `lec` phase would report only the reader steps
  // that load_side_graphs above already timed. Runs to the end of the function
  // and is recorded during unwinding too, so a refuted/timed-out proof still
  // reports the time it spent.
  Phase_timer lec_phase(res, "pass.lec");

  // Pick the top module on each side: explicit --{ref,impl}-top, else --top,
  // else the sole module (pick_top_graph: exact name or unambiguous entity
  // fallback with a diag warning).
  auto ref_g  = pick_top_graph(ref_var, opts.ref_top, opts.top, "ref", "lec", "pass.lec");
  auto impl_g = pick_top_graph(impl_var, opts.impl_top, opts.top, "impl", "lec", "pass.lec");

  // The options PINNED an engine (anything but engine=auto, or an explicit
  // portfolio): an engine that cannot handle the logic is then REFUTED-unchecked
  // (the options' choice), not REFUTED-unsupported (nothing can check it).
  // Read from the user's labels, never inferred from a detail string: the
  // drivers below switch engines internally (a sampled preset runs bmc, a
  // collapsed netlist leg runs ind) without the user having pinned anything.
  const bool engine_forced = label("engine", "auto") != "auto" || label("portfolio", "auto") != "auto";

  // A refusal BEFORE any solve still prints the verdict line and records the
  // machine-readable verdict, exactly like a solved run: `lec: '<top>' WORD (...)`.
  auto refuse_presolve = [&](livehd::lec::Outcome refusal, const std::string& why, const std::string& hint) {
    const auto t = make_top_outcome(refusal);
    std::print("lec: '{}' {} ({})\n", impl_g->get_name(), t.word, why);
    res.lec.present = true;
    res.lec.verdict = t.json;
    res.lec.effort  = std::string{livehd::lec::effort_name(effort)};
    res.lec.solver  = solver;
    conclude_outcome(t, "pass.lec", "lec", std::format("'{}' {}", impl_g->get_name(), why), hint);
  };

  // REFUTED-interface (owner ruling 2026-10-14): the selected tops must expose
  // the same port NAMES -- every output, and every input that drives logic.
  // Checked before any solve or cache lookup; widths never matter.
  {
    const auto iface = lec_top_interface(ref_g.get(), impl_g.get());
    if (!iface.ok()) {
      std::string why = absl::StrJoin(iface.mismatched, "; ");
      if (!iface.unused.empty()) {
        why += "; also " + absl::StrJoin(iface.unused, "; ");
      }
      refuse_presolve(livehd::lec::Outcome::Refuted_interface,
                      why,
                      "the two tops must declare the same port names (widths may differ; an input that drives nothing "
                      "may be one-sided). Rename the port, add it to the other side, or pick the matching tops with "
                      "--ref-top/--impl-top");
      return;
    }
    if (!iface.unused.empty()) {
      std::print("lec: '{}' interface note: {}\n", impl_g->get_name(), absl::StrJoin(iface.unused, "; "));
    }
  }

  const bool cross = yosys_comparison || (label("cross", "false") != "false" && label("cross", "false") != "0");

  // Discharge in-process via pass/lec (L1). The engine is the authority on the
  // non-cross path; in cross mode we additionally run lgcheck and assert
  // agreement (the strongest encoder check).
  livehd::lec::Lec_options o;
  o.assume_check          = assume_check;
  // pass.formal has already proved every checked obligation it could. What is
  // active is a proven assume or a contract (explicit nocheck, or all
  // assumptions when checking is disabled), and must constrain both the flat
  // and hierarchical cvc5 translations. A plain assume pass.formal did not
  // prove -- a selected-top IO one included (docs 05-assert: every plain
  // assume is an obligation) -- is no hypothesis: it would narrow the compared
  // input space of a miter whose other side never made the claim.
  const auto ref_assumes  = design_assume_occurrences(ref_g.get());
  const auto impl_assumes = design_assume_occurrences(impl_g.get());
  o.proven_assumes        = ref_assumes.proven + impl_assumes.proven;
  o.unchecked_assumes     = ref_assumes.unchecked + impl_assumes.unchecked;
  o.design_assumes        = o.proven_assumes + o.unchecked_assumes > 0;
  o.assumption_key        = std::format("design:{}:{}", o.proven_assumes + o.unchecked_assumes, assume_check ? 1 : 0);
  o.engine                = label("engine", "auto");
  o.solver                = solver;  // cvc5 | bitwuzla
  o.gold_x                = label("gold_x", "ignore");
  o.bound                 = std::atoi(label("bound", "6").c_str());
  o.timeout
      = std::atoi(label("timeout", "120").c_str());  // bound the CLI: hard miters degrade to TIMEOUT, never freeze (0 = unbounded)
  o.witness     = label("witness", "true") != "false" && label("witness", "true") != "0";
  o.decompose   = label("decompose", "auto");
  o.cones       = label("cones", "auto");
  o.conelimit   = std::atoi(label("conelimit", "10000").c_str());
  o.phase_sched = label("phase_sched", "true") != "false" && label("phase_sched", "true") != "0";
  o.box_seq     = label("box_model", "seq") != "uf";
  o.int_blast   = label("int_blast", "auto");

  o.semdiff             = livehd::lec::lec_canon_semdiff(label("semdiff", "structural"));
  o.state_pairing       = label("state_pairing", "true") != "false" && label("state_pairing", "true") != "0";
  o.partitions          = std::atoi(label("partitions", "4").c_str());
  o.jobs                = static_cast<int>(livehd::Job_manager::global().ceiling());
  o.split               = label("split", "auto");
  o.profile             = label("profile", "internal");
  o.portfolio           = label("portfolio", "auto");
  o.effort              = effort;
  if (effort == livehd::lec::Effort::Verify) {
    o.timeout = std::atoi(label("timeout", "0").c_str());
  }
  // Proof scheduling retains the per-definition minimum allowance. Sampled
  // runs use timeout as a wall-clock cap without extending it to this floor.
  o.min_timeout         = std::atoi(label("min_timeout", "1").c_str());
  // Hard wall backstop on a forked proof worker, as a multiple of `timeout` (0 = off).
  // `timeout` is cvc5 tlimit-per, which cannot preempt ONE long CaDiCaL solve -- the
  // shape a flat box-free miter takes. See Lec_options::hard_timeout_mult.
  o.hard_timeout_mult   = std::atoi(label("hard_timeout_mult", "3").c_str());
  o.spec_mining_timeout = std::atoi(label("spec_mining_timeout", "0").c_str());
  o.phase               = label("phase", "after_reset");
  o.reset_cycles        = std::atoi(label("reset_cycles", "2").c_str());
  o.reset               = label("reset", "");
  // Statistics are selected once by --stats / lhd.stats.
  o.stats               = opts.stats;
  if (label("effort", "") == "proof" && o.timeout > 0) {
    o._deadline = std::chrono::steady_clock::now() + std::chrono::seconds(o.timeout);
  }

  // formal.lec.match: explicit register correspondence, inline or @FILE.
  if (std::string match_spec = label("match", ""); !match_spec.empty()) {
    std::string text = match_spec;
    if (match_spec.front() == '@') {
      std::string path = match_spec.substr(1);
      if (!fs::is_regular_file(path)) {
        throw Lhd_error{"missing_file", std::format("formal.lec.match file not found: {}", path), ""};
      }
      std::ifstream     f(path);
      std::stringstream ss;
      ss << f.rdbuf();
      text = ss.str();
    }
    o.match = livehd::lec::parse_match_pairs(text);
  }

  // formal.lec.collapse: proven module defs to force-blackbox. Union of the --collapse
  // flags and a comma-separated `--set formal.lec.collapse=a,b,c`.
  o.collapse = opts.collapse;
  if (std::string cs = label("collapse", ""); !cs.empty()) {
    size_t pos = 0;
    while (pos < cs.size()) {
      size_t c   = cs.find(',', pos);
      size_t end = c == std::string::npos ? cs.size() : c;
      if (end > pos) {
        o.collapse.emplace_back(cs.substr(pos, end - pos));
      }
      pos = end + 1;
    }
  }

  // formal.lec.trust: def names ASSUMED equal WITHOUT a proof (the latch escape
  // hatch). Union of the --trust flags and `--set formal.lec.trust=a,b,c`. The
  // bottom-up driver (lec_hierarchical) skips proving these defs and keeps them
  // boxed even through a refute's flat-confirm; seed them into o.collapse now so
  // the flat (non-hier) path black-boxes them too — the hier driver rebuilds its
  // own per-def collapse set and re-seeds trust there.
  o.trust = opts.trust;
  // formal.ignore_memory: memories the user EXCLUDED from the comparison.
  // `label` merges formal.* and formal.lec.*, so either spelling works.
  if (std::string ms = label("ignore_memory", ""); !ms.empty()) {
    size_t pos = 0;
    while (pos < ms.size()) {
      size_t c   = ms.find(',', pos);
      size_t end = c == std::string::npos ? ms.size() : c;
      if (end > pos) {
        o.ignore_memory.emplace_back(ms.substr(pos, end - pos));
      }
      pos = end + 1;
    }
  }
  if (std::string ts = label("trust", ""); !ts.empty()) {
    size_t pos = 0;
    while (pos < ts.size()) {
      size_t c   = ts.find(',', pos);
      size_t end = c == std::string::npos ? ts.size() : c;
      if (end > pos) {
        o.trust.emplace_back(ts.substr(pos, end - pos));
      }
      pos = end + 1;
    }
  }
  if (!o.trust.empty()) {
    // Trusting the TOP module would assume the WHOLE design equivalent — a
    // vacuous pass. Refuse it: the trust list may only name INTERNAL defs.
    const std::string top_entity = lec_entity_of(ref_g->get_name());
    for (const auto& t : o.trust) {
      if (t == top_entity || t == ref_g->get_name() || t == impl_g->get_name() || t == opts.top) {
        throw Lhd_error{"usage",
                        std::format("formal.lec.trust names the top module '{}': that assumes the whole design "
                                    "equivalent (a vacuous pass)",
                                    t),
                        "trust only INTERNAL defs the encoder cannot model yet (e.g. latch modules)"};
      }
    }
    o.collapse.insert(o.collapse.end(), o.trust.begin(), o.trust.end());
  }

  if (auto e = livehd::lec::lec_options_range_error(o); !e.empty()) {
    throw Lhd_error{"usage", e, "the BMC engine unrolls one SMT copy of the design per cycle"};
  }

  if (auto error = livehd::lec::match_names_error(ref_g.get(), impl_g.get(), o.match); !error.empty()) {
    throw Lhd_error{"usage", error, "use actual state names as reference=implementation; ref. and impl. are not side qualifiers"};
  }

  // --lib lg:DIR libraries resolve Sub instances during encoding (e.g. the
  // gensim cell models behind an ABC standard-cell netlist), so lec can flatten
  // a hierarchical/mapped impl. Gids are name-hash stable, so an instance's
  // subnode gid matches its def's gid across libraries.
  absl::flat_hash_map<hhds::Gid, hhds::Graph*> sub_lib;
  std::vector<std::shared_ptr<hhds::Graph>>    sub_lib_keep;
  for (const auto& lp : opts.libs) {
    if (lp.kind != "lg") {
      throw Lhd_error{"usage",
                      std::format("lec --lib expects lg:DIR, got '{}:'", lp.kind),
                      "the cell-model library, e.g. --lib lg:models"};
    }
    if (!fs::is_directory(lp.path)) {
      throw Lhd_error{"missing_file", std::format("lec --lib not found: {}", lp.path), ""};
    }
    auto& lib = livehd::Hhds_graph_library::instance(lp.path);
    for (const hhds::Gid id : lib.all_gids()) {
      auto g = lib.get_graph(id);
      if (!g) {
        continue;
      }
      sub_lib_keep.push_back(g);
      sub_lib[id] = g.get();  // later --lib wins on a gid clash
    }
  }
  const auto* sub_lib_ptr = sub_lib.empty() ? nullptr : &sub_lib;

  // ── 2f-latch M8: EDGE NORMALIZATION, MITER-WIDE ───────────────────────────
  // The trigger is evaluated over the MITER, not per side. This is the single
  // most dangerous failure mode of a conditional pass: if one side holds a
  // latch and the other is its already-lowered round-trip, a per-side trigger
  // fires on ONE side and the two are compared in different time bases — a
  // guaranteed false REFUTED, and exactly the shape of the prp-v2prp2v-latch_*
  // corpus. So: if EITHER side needs it, BOTH are normalized (a side with
  // nothing to lower still runs the pass; at P=1 that is a no-op for it).
  //
  // Runs after the --lib cell models load and BEFORE canonical_digest: the
  // digest does not capture this pass's timing/naming decisions (its code is
  // salted into //lhd:formal_salt), so the verdict cache must key on the
  // normalized graphs.
  int lec_single_edge_slots = 1;
  {
    // The design's OWN defs, not just the --lib cell models. Scanning only
    // sub_lib meant the trigger saw the TOP BODY alone: a negedge flop inside a
    // submodule was invisible, the pass never ran, and lec[hier] proved the
    // child bottom-up with the edge-blind encoder -- a FALSE PROVEN against the
    // same design with a posedge flop there. (verify_command already included
    // its var.graphs, so only lec had the hole.) The pass fails closed on a
    // stateful Sub, so including them turns that wrong answer into a refusal.
    // A def the user COLLAPSED or TRUSTED is a blackbox: its internals are
    // assumed equal and never encoded, so a latch or a negedge flop inside it
    // is irrelevant here. `formal.lec.trust` exists precisely as the escape
    // hatch for "the encoder cannot model this leaf", and refusing on its
    // contents would pre-empt the mechanism that makes such a design provable.
    auto is_boxed = [&](const hhds::Graph* d) {
      const std::string full{d->get_name()};
      const std::string ent = lec_entity_of(full);
      return std::find(o.collapse.begin(), o.collapse.end(), full) != o.collapse.end()
             || std::find(o.collapse.begin(), o.collapse.end(), ent) != o.collapse.end();
    };
    // No loop preparation here: pass.legalize, the one owner of combinational
    // loops, ran on every graph-producing path -- every compile, including the
    // yosys reader and `lhd compile lg:` -- so each side is acyclic at arc
    // level already (todo/livehd/legalize_acyclic.md).

    std::vector<hhds::Graph*> ref_defs, impl_defs;
    for (const auto& sp : ref_var.graphs) {
      if (sp && sp.get() != ref_g.get() && !is_boxed(sp.get())) {
        ref_defs.push_back(sp.get());
      }
    }
    for (const auto& sp : impl_var.graphs) {
      if (sp && sp.get() != impl_g.get() && !is_boxed(sp.get())) {
        impl_defs.push_back(sp.get());
      }
    }
    for (const auto& [gid, gp] : sub_lib) {
      if (gp != nullptr && gp != ref_g.get() && gp != impl_g.get()) {
        ref_defs.push_back(gp);
        impl_defs.push_back(gp);
      }
    }
    // A STATEFUL `--lib` cell (a mapped DFF) is a modelling gap, not a hard
    // problem: pass/lec/encode.cpp inlines a cell model only when it is
    // COMBINATIONAL, so a register mapped to a library DFF becomes a stateless
    // blackbox with no state to correspond against the ref's native flop. The
    // run then reports "INCONCLUSIVE ... the solver ran out of budget", which
    // sends the reader after solver time when no budget would ever help (both
    // engines give up in MILLISECONDS). Say what is actually wrong, once.
    // The per-def half is load-bearing exactly as it is for the clock gates
    // above: the hierarchical driver encodes each def on its own, so a cell
    // instance inside a CHILD (cva6's `tag_cmp` under `tag_cmp_wrap`) is
    // untouched by inlining the top alone and that def stays inconclusive.
    // A `--lib` model itself never instantiates one, so skip those (they are
    // shared with ref_defs — mutating one would be a cross-side edit).
    const auto ref_flat  = inline_instances_missing_from_other_side(sub_lib, ref_var.graphs, impl_var.graphs, ref_g.get());
    const auto impl_flat = inline_instances_missing_from_other_side(sub_lib, impl_var.graphs, ref_var.graphs, impl_g.get());
    if (ref_flat + impl_flat > 0) {
      std::print("lec: inlined {} ref and {} impl instance(s) absent from the other hierarchy\n", ref_flat, impl_flat);
      // Those definitions are inline now wherever the other side dropped them;
      // definitions present on both sides stay comparable def by def. Do not
      // leave an orphan definition in edge-normalization's scan: it is outside
      // the actual post-flattening miter and could otherwise manufacture a
      // clock/latch refusal of its own.
      auto names_of = [](const std::vector<std::shared_ptr<hhds::Graph>>& graphs) {
        absl::flat_hash_set<std::string> names;
        for (const auto& sp : graphs) {
          if (sp) {
            names.insert(std::string{sp->get_name()});
            names.insert(lec_entity_of(sp->get_name()));
          }
        }
        return names;
      };
      const auto ref_names  = names_of(ref_var.graphs);
      const auto impl_names = names_of(impl_var.graphs);
      auto prune = [&](std::vector<hhds::Graph*>& defs, hhds::Graph* top, const absl::flat_hash_set<std::string>& other_names) {
        std::erase_if(defs, [&](hhds::Graph* d) {
          return d != top && sub_lib.find(d->get_gid()) == sub_lib.end() && !other_names.contains(std::string{d->get_name()})
                 && !other_names.contains(lec_entity_of(d->get_name()));
        });
      };
      prune(ref_defs, ref_g.get(), impl_names);
      prune(impl_defs, impl_g.get(), ref_names);

      // Inlining a definition is not only a hierarchy change: it connects the
      // caller's packing/unpacking cells directly to the callee's body.  A
      // constant slice which used to stop at a Sub boundary can now see the
      // Concat/Set_mask lane that really drives it.  Run the ordinary,
      // value-preserving scalar folds again so those newly visible lane reads
      // are rebased before the word-level encoder schedules the cone.
      //
      // This is load-bearing for ready/valid networks such as
      // br_amba_axi_demux.  Each valid[i] excludes ready[i], hence the circuit
      // is a DAG per bit, but after asymmetric hierarchy collapse an unsimplified
      // packed ready -> packed valid -> ready[i] ring looks cyclic per WORD and
      // cvc5 refuses without ever solving.  Cprop's Get_mask(Concat(...)) and
      // Set_mask slice rules delete exactly those false cross-lane edges.  Run
      // it on every design graph because an absorbed grandchild may have been
      // spliced into a retained common definition, not only into the top.
      // Never touch --lib models: they are shared vocabulary, and mutating one
      // side's cell definition would mutate the other side too.
      absl::flat_hash_set<hhds::Graph*> simplified;
      auto                              simplify_after_inline = [&](const std::vector<std::shared_ptr<hhds::Graph>>& graphs) {
        for (const auto& sp : graphs) {
          auto* g = sp.get();
          if (g == nullptr || sub_lib.find(g->get_gid()) != sub_lib.end() || !simplified.insert(g).second) {
            continue;
          }
          Cprop cprop;
          // This is not a raw front-end graph: the hierarchy transform above
          // may expose an intentionally truncated Concat lane whose internal
          // carrier has no standalone width stamp.  The normal cprop entry
          // assertion is specifically a tolg/upass contract, so skip that
          // assertion here while retaining every value-preserving fold.
          cprop.do_trans(sp);
        }
      };
      simplify_after_inline(ref_var.graphs);
      simplify_after_inline(impl_var.graphs);
    }
    if (std::getenv("LEC_DUMP_COLLAPSE") != nullptr) {
      for (auto* side : {ref_g.get(), impl_g.get()}) {
        // The same instances through the hierarchy view the encoder walks.
        for (auto hn : side->grouped_hierarchy().nodes()) {
          if (livehd::graph_util::type_op_of(hn) != Ntype_op::Sub || sub_lib.find(hn.get_subnode_gid()) != sub_lib.end()) {
            continue;
          }
          auto sio = hn.get_subnode_io();
          std::fprintf(stderr,
                       "[LEC_COLLAPSE] after-prep %s HIER-VIEW sub '%s' def '%s'\n",
                       side == ref_g.get() ? "ref " : "impl",
                       std::string(hn.get_hier_name()).c_str(),
                       sio == nullptr ? "?" : std::string(sio->get_name()).c_str());
        }
        for (auto n : side->body().nodes()) {
          if (livehd::graph_util::type_op_of(n) != Ntype_op::Sub || sub_lib.find(n.get_subnode_gid()) != sub_lib.end()) {
            continue;
          }
          auto sio = n.get_subnode_io();
          std::fprintf(stderr,
                       "[LEC_COLLAPSE] after-prep %s top sub '%s' def '%s' out_edges=%d body=%d\n",
                       side == ref_g.get() ? "ref " : "impl",
                       std::string(n.get_name()).c_str(),
                       sio == nullptr ? "?" : std::string(sio->get_name()).c_str(),
                       n.has_out_edges() ? 1 : 0,
                       n.get_subnode_graph() != nullptr ? 1 : 0);
        }
      }
    }
    // Put both sides into ONE single-edge time base before anything is queried:
    // inline stateful/clock-cone --lib cells, recognize clock gates as
    // Clock_cell, fold gated clocks into flop enables, then dry-run edge
    // normalization on both sides and apply the max P to both
    // (pass/single_edge/proof_prep.hpp, so any other proof client sees the same
    // normalized designs as `lhd lec`).
    //
    // M10: a normalization DECLINE is no longer mandatory-fatal. Under
    // `formal.phase_sched` (the default) the encoder's READ-ONLY four-microstep
    // schedule takes the design instead, which composes across hierarchy
    // because it rewrites nothing; only without it is a decline a refusal.
    const auto tb
        = livehd::single_edge::prepare_time_base(sub_lib, ref_g.get(), ref_defs, impl_g.get(), impl_defs, o.phase_sched, is_boxed);
    for (const auto& step : tb.recipe_steps) {
      res.recipe_steps.push_back(step);
    }
    if (tb.declined && !o.phase_sched) {
      // Re-run loudly so the user gets the specific diagnostic, then fail. The
      // read-only phase schedule (formal.lec.phase_sched, on by default) is what
      // takes a declined design, and the options turned it off: REFUTED-unchecked.
      const char* which = tb.declined_ref ? "ref" : "impl";
      auto*       g_bad = tb.declined_ref ? ref_g.get() : impl_g.get();
      livehd::single_edge::normalize(g_bad, tb.declined_ref ? ref_defs : impl_defs, {});
      refuse_presolve(livehd::lec::Outcome::Refuted_unchecked,
                      std::format("edge normalization declined on the {} side '{}' ({}) and formal.lec.phase_sched=false "
                                  "disabled the phase schedule that models it",
                                  which,
                                  g_bad->get_name(),
                                  tb.decline_reason),
                      "drop formal.lec.phase_sched=false: a partial or one-sided lowering would compare the two designs in "
                      "different time bases");
      return;
    }
    if (!tb.error.empty()) {
      refuse_presolve(livehd::lec::Outcome::Refuted_unsupported, tb.error, tb.error_hint);
      return;
    }
    if (o.phase_sched && !tb.applied) {
      // The read-only phase schedule classifies every remaining latch from its
      // enable's STRUCTURE (`clk & en_l` = a clock-role latch). A mapped gate
      // (`AND2x1(clk, en_l)`) is opaque to it, so one hardware latch would be a
      // clock-role latch on a side that spells the gate natively (the RTL, a
      // re-read Verilog netlist) and a DATA latch on a side that keeps the cell
      // (an lg: netlist) -- two time bases, and a false REFUTED. Expose the
      // clock-dependent single-product cells (AND/NAND/NOR/INV/...) on latch
      // enables on both sides, symmetrically (pass/lec/lib_cell_prep.hpp).
      // A cell whose model does not bind to it by port name refuses the run:
      // leaving it opaque would schedule the two sides differently again.
      size_t                    n_cells = 0;
      std::vector<hhds::Graph*> sides{ref_g.get()};
      if (impl_g.get() != ref_g.get()) {
        sides.push_back(impl_g.get());
      }
      for (auto* side : sides) {
        const auto cr = livehd::lec::inline_clock_latch_enable_cells(sub_lib, side, is_boxed);
        if (!cr.error.empty()) {
          refuse_presolve(livehd::lec::Outcome::Refuted_unsupported,
                          cr.error,
                          "a --lib model must declare every port of the instance by name and width; nothing was compared");
          return;
        }
        n_cells += cr.spliced;
      }
      if (n_cells > 0) {
        res.recipe_steps.emplace_back(
            std::format("pass.lec phase_sched: inlined {} clock-dependent --lib cell(s) on latch enables", n_cells));
      }
    }
    if (tb.applied) {
      // BMC `bound` counts STEPS, and a step is now a sub-step: at P=2 the same
      // bound buys half the design cycles. Scale it so a design keeps the depth
      // coverage its options asked for.
      if (tb.slots > 1) {
        o.bound *= tb.slots;
      }
      lec_single_edge_slots = tb.slots;
      res.recipe_steps.emplace_back(std::format("pass.single_edge slots:{} ref_latches:{} impl_latches:{} bound:{}",
                                                tb.slots,
                                                tb.ref_latches,
                                                tb.impl_latches,
                                                o.bound));
    }
  }

  // A mixed-edge marker has already lost per-port polarity in the IR.
  // Even structural identity or a cache hit cannot establish its behavior.
  // Apply the encoder's refusal before either shortcut can bypass it.
  for (auto* side : {ref_g.get(), impl_g.get()}) {
    for (auto node : side->grouped_hierarchy().nodes()) {
      if (auto error = livehd::lec::mixed_memory_edge_error(node, o.ignore_memory); !error.empty()) {
        refuse_presolve(livehd::lec::Outcome::Refuted_unsupported,
                        std::format("lec cannot model a mixed-edge memory: {}", error),
                        "exclude that memory from the comparison with --set formal.ignore_memory=<name> (disclosed), or "
                        "give each memory port a single clock edge");
        return;
      }
    }
  }

  // Phase 1/3 of the lec-on-failure flow (detect -> testbench -> waveform):
  // announce the (possibly long, quiet) SMT detection up front so a slow solve is
  // legible instead of looking like a hang.
  livehd::diag::info("pass.lec", "lec-detecting", "progress")
      .msg("lec: detecting equivalence of '{}' vs '{}' (engine={}, solver={}, {})",
           impl_g->get_name(),
           ref_g->get_name(),
           o.engine,
           o.solver,
           o.timeout > 0 ? std::format("timeout={}s", o.timeout) : std::string{"no timeout"})
      .emit();

  // 2f-fcore verdict cache: persistent only under a user-named --workdir
  // (formal_cache.json; opt out with --set lhd.incremental=false). Keyed cache-wide
  // by kFormalSrcSalt — the build-time content hash of the prover sources — so
  // a prover change invalidates every stored verdict automatically. v1 wires
  // it into the hierarchical driver (the default path).
  if (o.timeout > 0) {
    o._deadline = std::chrono::steady_clock::now() + std::chrono::seconds(o.timeout);
  }
  std::unique_ptr<livehd::formal::Verdict_cache> vcache;
  if (workdir_set && opts.incremental && !sampled_effort) {
    // MATERIALIZE the workdir before the cache opens it. Verdict_cache::save()
    // treats an unopenable path as "the cache is only ever a speedup" and
    // returns silently -- so without this, every run stored to memory, wrote
    // NOTHING, printed its in-memory `N stored` tally, and came back 0 hit(s)
    // forever. The whole incremental tier (verdict cache, cone cache, Unknown
    // ledger, strategy hint) was dead for exactly this reason: no command on
    // the formal path creates --workdir, and only `save()` ever needed it to
    // already exist.
    ensure_dir(opts.workdir);
    vcache        = std::make_unique<livehd::formal::Verdict_cache>(opts.workdir, livehd::kFormalSrcSalt);
    // Read side of the cone cache: hand the engine the whole PROVEN digest set
    // ONCE, by value. It rides the Lec_options copy into every forked worker,
    // so no worker ever opens this file -- it just checks membership and skips
    // abc for a cone whose obligation is already settled.
    o._cone_cache = vcache->cone_digests();
  }

  livehd::lec::Query_result                    r;
  // lhd.stats: per-def (name, conflicts) ranking, filled by the hierarchical
  // driver only (the flat path is one def, so the totals already say it all).
  std::vector<std::pair<std::string, int64_t>> cvc5_hot;
  bool                                         sampled_verdict = false;
  if (sampled_effort) {
    // Sample the requested top with concrete children. A tested child is never
    // a premise, and cached proofs must not replace the requested fresh work.
    auto sample_opts       = o;
    sample_opts.engine     = "bmc";
    sample_opts.partitions = 1;
    sample_opts.split      = "none";
    res.recipe_steps.emplace_back(std::format("pass.lec effort:{} engine:bmc", livehd::lec::effort_name(effort)));
    r               = livehd::lec::prove_equal_isolated(ref_g.get(), impl_g.get(), sample_opts, &sub_lib);
    sampled_verdict = true;
    // The sampler is ONE engine (concrete-hierarchy BMC). When it cannot model
    // the design -- a collapsed/trusted/ignored box, a phase schedule only the
    // inductive engine composes -- that is not "no engine can model it"
    // (REFUTED-unsupported, owner ruling 1): escalate to the proof engines,
    // which either decide it or name the feature none of them can model.
    if (r.verdict == livehd::lec::Verdict::Undecided && r.unsupported) {
      const std::string why = r.detail;
      std::print("lec: sampled testing ({}) cannot run on this design; escalating to the proof engines (formal.effort=proof): {}\n",
                 livehd::lec::effort_name(effort),
                 why);
      res.recipe_steps.emplace_back(
          std::format("pass.lec effort:{} -> proof (sampling cannot model the design)", livehd::lec::effort_name(effort)));
      effort   = livehd::lec::Effort::Legacy;
      o.effort = effort;
      if (o.timeout > 0) {
        o._deadline = std::chrono::steady_clock::now() + std::chrono::seconds(o.timeout);
      }
      r               = livehd::lec::Query_result{};
      sampled_verdict = false;
    }
  }
  if (sampled_verdict) {
    // The sampled verdict stands.
  } else if (label("hier", "true") != "false" && label("hier", "true") != "0") {
    // Bottom-up: LEC every def leaves-first under `auto`, collapsing proven
    // children. The driver emits a per-def progress line itself; the TOP def's
    // verdict drives the exit policy below (like the single-design path).
    const bool        retry_all   = label("retry", "changed") == "all";
    // DEFAULT: escalate. A module boundary is NOT part of the specification --
    // functionality legitimately moves ACROSS it between the two front-ends --
    // so an intermediate def that refutes is not a disproof of the design. The
    // driver inlines just that def into its caller (its PROVEN siblings stay
    // boxed) and re-proves there; only the requested TOP's boundary is
    // contractual. `fail` is the old fail-fast behaviour and is a DEBUG aid:
    // it stops at the first differing block, which localizes fast but reports a
    // block-boundary counterexample as the run's verdict.
    const std::string hier_refute = label("hier_refute", "escalate");
    if (hier_refute == "fail") {
      livehd::diag::warn("pass.lec", "hier-refute-fail-mode", "unsupported")
          .msg(
              "formal.lec.hier_refute=fail is a DEBUG mode: a REFUTED intermediate def taints its ancestors and its "
              "block-boundary counterexample becomes the run verdict")
          .hint(
              "a module boundary is not part of the specification — functionality can move across it, so an "
              "intermediate refutation is not a disproof of the design. Use the default (escalate), which inlines "
              "the refuting def into its caller and keeps every proven sibling boxed")
          .emit();
    }
    if (hier_refute != "fail" && hier_refute != "escalate") {
      throw Lhd_error{"usage",
                      std::format("--set formal.lec.hier_refute expects fail|escalate, got '{}'", hier_refute),
                      "fail (debug; a refuted block fails the run, its parents are skipped) | escalate (default; prove the "
                      "parents anyway, to confirm the block-boundary counterexample is reachable at the top)"};
    }
    // DEFAULT: top_down. Prove every def with EVERY child BOXED, then discharge
    // each premise from the same pass's other entries (the module DAG is
    // well-founded, so this is an induction, not circular reasoning). Three
    // things fall out, and the third is the one that matters on a real failure:
    //   * no def's obligation depends on another def's VERDICT, so the whole
    //     hierarchy is dispatched in parallel instead of depth-serialized;
    //   * every miter is at its MINIMUM size — bottom-up has to FLATTEN a child
    //     that did not prove, which is exactly when the parent is hardest;
    //   * a refuting block is absorbed by re-proving its immediate PARENT with
    //     that block inlined. If the parent proves, every ancestor already
    //     proved with the parent BOXED, so the chain closes and nothing higher
    //     is re-solved. Bottom-up instead grows the flattened cone one level at
    //     a time, all the way to the top in the worst case.
    // `bottom_up` is the legacy leaves-first order, kept for A/B and because
    // `hier_refute=fail` (a debug mode) is defined in terms of it.
    const std::string hier_order = label("hier_order", "top_down");
    if (hier_order != "top_down" && hier_order != "bottom_up") {
      throw Lhd_error{"usage",
                      std::format("--set formal.lec.hier_order expects top_down|bottom_up, got '{}'", hier_order),
                      "top_down (default; every def proved with all children boxed, premises discharged afterwards, "
                      "a refuting block absorbed by its parent) | bottom_up (legacy leaves-first: only an "
                      "already-proven child is boxed)"};
    }
    // fail-fast is a leaves-first notion (skip a def whose child already
    // refuted); under top-down no def waits on a child's verdict, so the two
    // cannot be combined.
    const bool top_down_hier = hier_order == "top_down" && hier_refute != "fail";
    if (hier_order == "top_down" && hier_refute == "fail") {
      std::print("lec[hier]: formal.lec.hier_refute=fail forces the legacy bottom_up order\n");
    }
    r = lec_hierarchical(res,
                         ref_var,
                         impl_var,
                         std::string(ref_g->get_name()),
                         ref_g.get(),
                         impl_g.get(),
                         o,
                         sub_lib_ptr,
                         vcache.get(),
                         retry_all,
                         /*fail_fast_refute=*/hier_refute == "fail",
                         /*top_down=*/top_down_hier,
                         lec_single_edge_slots,
                         o.stats ? &cvc5_hot : nullptr);
    if (vcache) {
      vcache->save();
      if (vcache->hits() > 0 || vcache->stores() > 0 || vcache->skips() > 0) {
        std::print("lec[cache]: {} hit(s), {} stored, {} skipped-timeout ({}/formal_cache.json)\n",
                   vcache->hits(),
                   vcache->stores(),
                   vcache->skips(),
                   opts.workdir);
      }
    }
  } else {
    res.recipe_steps.emplace_back(std::format("pass.lec engine:{} solver:{} phase:{}", o.engine, o.solver, o.phase));
    if (vcache != nullptr) {
      if (auto h = vcache->hint(std::string{impl_g->get_name()}); h.has_value()) {
        if (o.split == "auto" && !h->split.empty()) {
          o.split = h->split;
        }
        if (o.engine == "auto" && (h->engine == "ind" || h->engine == "bmc")) {
          o._preferred_engine = h->engine;
        }
      }
    }
    // Tier-2 uncertain state pairing (flat path): replay a validated pair
    // hint, else run semdiff's signature pass fresh on the top pair (per-def
    // scope — flattened child flops are 3a-synth territory). The uncertain
    // discipline lives inside prove_equal; this path has no digest cache, so
    // the pairs only shape the solve.
    bool                     pairs_from_hint = false;
    std::vector<std::string> flat_unpaired_ref, flat_unpaired_impl;  // for the result's state report
    if (o.state_pairing) {
      if (vcache != nullptr) {
        if (auto ph = vcache->pair_hint(lec_entity_of(impl_g->get_name())); ph.has_value()) {
          std::vector<std::string> dropped;
          auto                     valid = livehd::lec::validate_uncertain_pairs(ref_g.get(), impl_g.get(), o, ph->pairs, &dropped);
          if (dropped.empty() && !valid.empty()) {
            o.uncertain_match = std::move(valid);
            pairs_from_hint   = true;
          }
        }
      }
      if (!pairs_from_hint) {
        livehd::semdiff::Semdiff_options so;
        so.matching_names  = true;
        so.state_pairing   = true;
        so.project_state_bits = true;
        so.seed_pairs      = o.match;
        auto m             = livehd::semdiff::structural_match(ref_g.get(), impl_g.get(), so);
        o._state_projections  = m.state_projections;
        o._memory_projections = m.memory_projections;
        flat_unpaired_ref  = m.a_state_unpaired;
        flat_unpaired_impl = m.b_state_unpaired;
        // 2f-lec diverged-use guard: keep genuinely-diverged memories uncollapsed.
        o.mem_diverged.clear();
        o.mem_diverged.insert(o.mem_diverged.end(), m.a_mem_diverged.begin(), m.a_mem_diverged.end());
        o.mem_diverged.insert(o.mem_diverged.end(), m.b_mem_diverged.begin(), m.b_mem_diverged.end());
        if (!m.state_pairs.empty()) {
          std::vector<std::pair<std::string, std::string>> fresh;
          fresh.reserve(m.state_pairs.size());
          for (auto& p : m.state_pairs) {
            if (p.is_mem) {
              // Memories are never name-aliased, but a confident mem pair lets the
              // diverged-use collapse guard trust an ambiguous shape bucket's pairing.
              o.mem_match.emplace_back(std::move(p.a_name), std::move(p.b_name));
            } else {
              fresh.emplace_back(std::move(p.a_name), std::move(p.b_name));
            }
          }
          o.uncertain_match = livehd::lec::validate_uncertain_pairs(ref_g.get(), impl_g.get(), o, fresh, nullptr);
        }
        if (!o.uncertain_match.empty()) {
          std::print("lec: '{}' tier-2 state pairing: {} uncertain pair(s) injected ({} round(s))\n",
                     impl_g->get_name(),
                     o.uncertain_match.size(),
                     m.state.rounds);
        }
        if (!m.a_state_unpaired.empty() || !m.b_state_unpaired.empty()) {
          auto capped = [](const std::vector<std::string>& v, size_t cap = 8) {
            std::string s;
            for (size_t i = 0; i < v.size() && i < cap; ++i) {
              s += (i ? ", " : "") + v[i];
            }
            if (v.size() > cap) {
              s += std::format(", (+{} more)", v.size() - cap);
            }
            return s;
          };
          std::print("lec: '{}' tier-2 unpaired state: ref{{{}}} impl{{{}}}\n",
                     impl_g->get_name(),
                     capped(m.a_state_unpaired),
                     capped(m.b_state_unpaired));
        }
      }
    }
    // 2f-fcore verdict cache on the FLAT path (F2 tail): the same store the
    // hierarchical driver uses, applied to the single top-pair miter. Keyed by the
    // canonical digest of each side + the verdict-relevant options (via
    // lec_pair_cache_key, incl. the um=[…] uncertain-pair segment settled above and
    // the manual c=[…] collapse list), salt-gated cache-wide. A PROVEN hit skips
    // encode+solve entirely; an Unknown-ledger hit skips a re-grind that cannot
    // outspend the prior budget. Anonymous-state (undigestable) defs stay
    // uncacheable, exactly as in the hier path.
    const bool                        flat_retry_all = label("retry", "changed") == "all";
    livehd::semdiff::Canonical_digest dref_flat, dimpl_flat;
    std::string                       flat_ckey;
    bool                              flat_cacheable   = false;
    bool                              settled_by_cache = false;
    if (vcache != nullptr) {
      absl::flat_hash_map<hhds::Gid, hhds::Graph*> ref_gid2g, impl_gid2g;
      for (auto& g : ref_var.graphs) {
        if (g) {
          ref_gid2g[g->get_gid()] = g.get();
        }
      }
      for (auto& g : impl_var.graphs) {
        if (g) {
          impl_gid2g[g->get_gid()] = g.get();
        }
      }
      if (sub_lib_ptr != nullptr) {
        for (const auto& [gid, gp] : *sub_lib_ptr) {
          ref_gid2g.try_emplace(gid, gp);
          impl_gid2g.try_emplace(gid, gp);
        }
      }
      livehd::semdiff::Digest_resolver rdres = [&ref_gid2g](hhds::Gid gid) -> hhds::Graph* {
        auto it = ref_gid2g.find(gid);
        return it == ref_gid2g.end() ? nullptr : it->second;
      };
      livehd::semdiff::Digest_resolver idres = [&impl_gid2g](hhds::Gid gid) -> hhds::Graph* {
        auto it = impl_gid2g.find(gid);
        return it == impl_gid2g.end() ? nullptr : it->second;
      };
      dref_flat  = livehd::semdiff::canonical_digest(ref_g.get(), rdres);
      dimpl_flat = livehd::semdiff::canonical_digest(impl_g.get(), idres);
      if (dref_flat.valid && dimpl_flat.valid) {
        flat_cacheable = true;
        flat_ckey      = lec_pair_cache_key(dref_flat, dimpl_flat, o);
        if (auto hit = vcache->lookup(flat_ckey); hit.has_value()) {
          r.verdict       = livehd::lec::Verdict::Proven;
          r.bounded       = hit->bounded;
          r.checked_steps = hit->bounded ? hit->depth : 0;
          r.engine        = "cache";
          r.detail        = lec_cache_hit_detail(flat_ckey, *hit);
          r.elapsed_ms    = 0;
          std::print("lec: '{}' {} (cache)\n",
                     impl_g->get_name(),
                     lec_top_outcome(r, engine_forced, lec_single_edge_slots, o.bound).word);
          settled_by_cache = true;
        } else if (!flat_retry_all && vcache->skip_timeout(flat_ckey, o.timeout)) {
          r.verdict    = livehd::lec::Verdict::Undecided;
          r.engine     = "cache-skip";
          // The ledger records TIMEOUT-time results only (an attempt that hit
          // its budget), so a skip reports the same outcome the attempt did.
          r.detail     = std::format("skipped: known timeout at >= this budget (formal.timeout={}s; --set formal.retry=all to re-attempt)",
                                 o.timeout);
          r.elapsed_ms = 0;
          std::print("lec: '{}' {} (skipped: known timeout)\n",
                     impl_g->get_name(),
                     livehd::lec::outcome_word(livehd::lec::Outcome::Timeout_time));
          settled_by_cache = true;
        }
      }
    }
    auto t0              = std::chrono::steady_clock::now();
    bool confirm_demoted = false;  // a collapsed REFUTE whose flat confirmation could not settle even deepened
    if (!settled_by_cache) {
      r = livehd::lec::prove_equal_isolated(ref_g.get(), impl_g.get(), o, sub_lib_ptr);
      if (r.verdict == livehd::lec::Verdict::Refuted && !o.collapse.empty()) {
        // Same abstraction rule as the hierarchical driver: a REFUTE under a
        // manual --collapse can be an artifact of the box over-approximation, so
        // confirm FLAT before letting the exit policy report a fail.
        std::print("lec: '{}' REFUTED under collapse ({} box def(s)) -> flat confirmation\n",
                   impl_g->get_name(),
                   o.collapse.size());
        livehd::lec::Lec_options oflat = o;
        // Keep TRUSTED boxes (unmodeled cells); drop only the manual --collapse
        // boxes being confirmed. Clearing trust would re-flatten a latch and turn
        // this real counterexample into an encoder refusal (exit 7).
        oflat.collapse.assign(o.trust.begin(), o.trust.end());
        auto      rf    = livehd::lec::prove_equal_isolated(ref_g.get(), impl_g.get(), oflat, sub_lib_ptr);
        // Same rule as the hierarchical driver, INCLUDING its `flush > 0` guard:
        // a bounded "no CEX up to k" cannot by itself overrule a collapsed
        // REFUTE whose witness may need up to the flush depth more cycles to
        // reach an output flat -- but with no flush depth the "deepened" options
        // are byte-identical to the run that just finished, so the re-solve buys
        // nothing and can only flake a settled bounded PROVEN into a hard
        // failure. reset_hold is 0 for every phase but after_reset.
        const int flush = std::max(rf.reset_hold, r.reset_hold);
        if (rf.verdict == livehd::lec::Verdict::Proven && rf.bounded && flush > 0) {
          livehd::lec::Lec_options odeep = oflat;
          odeep.bound                    = std::max(oflat.bound > 0 ? oflat.bound : 6, rf.checked_steps) + flush;
          std::print("lec: '{}' flat confirmation only BOUNDED ({} step(s)) -> deepening to bound {} (+ flush depth {})\n",
                     impl_g->get_name(),
                     rf.checked_steps,
                     odeep.bound,
                     flush);
          auto rd      = livehd::lec::prove_equal_isolated(ref_g.get(), impl_g.get(), odeep, sub_lib_ptr);
          rd.cvc5     += rf.cvc5;
          rd.solve_ms += rf.solve_ms;  // the shallow flat leg really ran
          if (rd.verdict == livehd::lec::Verdict::Undecided) {
            // The shallow flat leg checked the requested bound from reset with
            // no counterexample: PROVEN-bounded(n) stands (owner ruling
            // 2026-10-14). The deepened run only tried to look further.
            rf.detail += std::format("; deepened flat run (bound {}) did not settle ({}), so the checked depth stands",
                                     odeep.bound,
                                     livehd::lec::undecided_cause(rd.detail, rd.unsupported).word);
            rf.cvc5     = rd.cvc5;
            rf.solve_ms = rd.solve_ms;  // the deepened leg is usually the expensive one
          } else {
            rd.detail
                = std::format("deepened flat confirmation (bound {} = {} + flush {}); ", odeep.bound, odeep.bound - flush, flush)
                  + rd.detail;
            rf = std::move(rd);
          }
        }
        // An unsettled flat confirmation leaves the run undecided by the FLAT
        // leg's own cause (a timeout, ...): the collapsed REFUTE may be a box
        // artifact, so it never stands on its own.
        confirm_demoted = rf.verdict == livehd::lec::Verdict::Undecided;
        if (confirm_demoted) {
          adopt_cause(rf, rf);
        }
        rf.detail      = "flat-confirm after collapsed-box REFUTE" + std::string(rf.detail.empty() ? "" : "; ") + rf.detail
                         + (r.detail.empty() ? "" : " (collapsed run: " + r.detail + ")");
        rf.elapsed_ms  = -1;          // the progress record carries the combined wall-clock below
        rf.cvc5       += r.cvc5;      // the collapsed run's cvc5 effort was still spent (lhd.stats)
        rf.solve_ms   += r.solve_ms;  // and so was its solve time
        r              = std::move(rf);
      }
      // int_blast=auto second leg (same rule as the hierarchical driver): a
      // solver-give-up undecided result earns one int-blasted re-solve at
      // min_timeout. NOT after an unsettled confirmation: `o` still carries the
      // collapse, so this would re-solve the very collapsed miter whose REFUTE is
      // the thing left unconfirmed, and report that spurious-or-not refute as
      // the verdict.
      if (!confirm_demoted) {
        r = livehd::lec::int_blast_retry(ref_g.get(), impl_g.get(), o, std::move(r), sub_lib_ptr);
      }
      // Same trusted-box rule as the hierarchical driver: a refute that turns on
      // a trusted box input is REFUTED-cex (the boxed model is the contract),
      // disclosed because the trusted leaf may treat that input as a don't-care.
      if (r.verdict == livehd::lec::Verdict::Refuted) {
        absl::flat_hash_set<std::string> trust_set(o.trust.begin(), o.trust.end());
        if (std::string tb = lec_refute_trusted_box(r.witness, trust_set); !tb.empty()) {
          r.detail = std::format(
              "refuted at trusted-box input (bbin:{}) — the boxed model is the contract (a trusted leaf may still treat "
              "that input as a don't-care); {}",
              tb,
              r.detail);
        }
      }
    }
    disclose_lec_helpers(r, o);
    const long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    // Store — only for a freshly-solved verdict (a cache/skip hit is already recorded).
    if (!settled_by_cache && vcache != nullptr) {
      if (r.verdict == livehd::lec::Verdict::Proven) {
        if (flat_cacheable && !flat_ckey.empty()) {
          // definitive Proven only (rule F); a bounded one keeps its checked depth
          vcache->insert(flat_ckey,
                         {r.engine, r.detail, ms, r.bounded, r.bounded ? std::max(1, r.checked_steps > 0 ? r.checked_steps : o.bound) : 0});
        }
        if (!r.bounded) {  // a bounded pass is a fallback, not a winning strategy
          vcache->set_hint(std::string{impl_g->get_name()}, {r.engine, r.split_used, ms});
        }
        lec_store_pair_hint(vcache.get(), lec_entity_of(impl_g->get_name()), r.uncertain_pairs_used);
      } else if (is_time_limit(r) && flat_cacheable && !flat_ckey.empty()) {
        // A TIMEOUT-time result, and only that: ledger the attempt. Any other
        // undecided cause (a refusal, a memory limit, a witness-carrying
        // partial-miter diff) must re-surface every run.
        vcache->note_timeout(flat_ckey, {o.timeout, ms});
      } else if (pairs_from_hint) {
        // Self-heal (mirror of the hier driver): a replayed pair hint that did
        // not end Proven is stale — drop it so the next run pairs fresh.
        vcache->clear_pair_hint(lec_entity_of(impl_g->get_name()));
      }
    }
    if (r.verdict == livehd::lec::Verdict::Undecided || (r.verdict == livehd::lec::Verdict::Proven && r.bounded)) {
      std::map<std::string, Lec_state_report> reports;
      reports[std::string{impl_g->get_name()}] = lec_unpaired_state_report(std::string{impl_g->get_name()},
                                                                           ref_g.get(),
                                                                           impl_g.get(),
                                                                           flat_unpaired_ref,
                                                                           flat_unpaired_impl,
                                                                           o.uncertain_match,
                                                                           r.uncertain_pairs_used);
      lec_publish_state_reports(res, reports);
    }
    lec_store_cones(vcache.get(), r);
    if (vcache != nullptr) {
      vcache->save();
      if (vcache->hits() > 0 || vcache->stores() > 0 || vcache->skips() > 0) {
        std::print("lec[cache]: {} hit(s), {} stored, {} skipped-timeout ({}/formal_cache.json)\n",
                   vcache->hits(),
                   vcache->stores(),
                   vcache->skips(),
                   opts.workdir);
      }
    }
    // Per-block progress (info severity): stream the verdict the moment it resolves.
    emit_lec_block_progress(impl_g->get_name(), r, o, ms);
  }

  // lhd.stats: the cvc5 solve-insight report, one run total. Both paths funnel
  // into `r` — the hierarchical driver already summed every def into r.cvc5 (and
  // filled cvc5_hot); the flat path carries its single solver's numbers.
  //
  // BEFORE every throwing arm below (pass_lec.cpp:293 does the same for the same
  // reason): the encoder refusal and the undecided
  // failure all exit the process, and a report printed after them is lost in
  // exactly the runs where knowing how hard the solver worked matters most —
  // after the user already paid the ~8x plugin cost for it.
  if (o.stats) {
    livehd::lec::report_cvc5_stats("lec", r.cvc5, cvc5_hot);
    for (const auto& arm : r.arms) {
      std::print("lec[arm]: {}:{} {} {}ms won={} killed={} queued={}\n",
                 arm.engine,
                 arm.profile,
                 arm.verdict,
                 arm.elapsed_ms,
                 arm.won,
                 arm.killed,
                 arm.queued);
    }
  }


  // VERDICT TAXONOMY (owner rulings 2026-10-14; the table lives in
  // pass/lec/property_types.hpp, shared with `lhd formal verify`):
  //
  //   PROVEN-full          an unbounded (inductive) proof                      -> exit 0
  //   PROVEN-bounded(n)    no difference for n DESIGN cycles from reset,
  //                        exhaustive over inputs: a proof, to that depth      -> exit 0
  //   TESTED-sampled(N)    a sampled effort preset: N input vectors agreed     -> exit 0
  //   TIMEOUT-time/-memory a clock or memory limit stopped it, nothing claimed -> exit 0 + warning
  //   REFUTED-cex          a real counterexample                               -> exit 10
  //   REFUTED-<why>        interface | unsupported | unchecked | vacuous |
  //                        unexplained: no answer, and the word says what to
  //                        fix (a port, an option, a design feature, an
  //                        assume, or a LiveHD bug to report)                  -> exit 7
  //                        REFUTED-vacuous: nothing to compare (no output/state
  //                        on either top; the port names match), or a
  //                        CONTRADICTORY assume set (it can prune away a real
  //                        counterexample): a check that proved nothing
  //
  // lec_top_outcome() is the only place that names the outcome and
  // conclude_outcome() the only place that turns it into an exit status. The
  // bounded depth is carried in the verdict and persisted in the cache, so a
  // warm run cannot promote a bounded result to an unbounded proof.

  // A PROVEN verdict obtained with a non-empty trust list is CONDITIONAL on those
  // assumptions — disclose it on the verdict line and in the machine-readable
  // detail (envelope/JSON), so a trust-assisted pass is never read as an
  // unconditional proof. A refute needs no such caveat (a refute is a real
  // counterexample under the trusted model; an undecided run proves nothing).
  if (r.verdict == livehd::lec::Verdict::Proven && !o.trust.empty()) {
    r.detail += std::format("; PROVEN under {} trusted def(s) (assumed equal, NOT proven — formal.lec.trust)", o.trust.size());
  }
  // Same rule for an IGNORED memory: its reads were a shared free symbol and its
  // contents were never compared, so the pass says nothing about what it stores.
  // Without this line an ignore-assisted pass reads as an unconditional proof —
  // which is exactly the "a check that compares nothing is not a proof" trap.
  if (r.verdict == livehd::lec::Verdict::Proven && !o.ignore_memory.empty()) {
    std::string names;
    for (const auto& m : o.ignore_memory) {
      names += (names.empty() ? "" : ", ") + m;
    }
    r.detail += std::format(
        "; PROVEN with {} memory(ies) IGNORED (blackboxed, contents NOT compared — "
        "formal.ignore_memory={})",
        o.ignore_memory.size(),
        names);
  }

  const auto& sampling = r.sampling;
  const auto  top      = lec_top_outcome(r, engine_forced, lec_single_edge_slots, o.bound);
  // REFUTED-vacuous has two readings: an empty comparison, or a CONTRADICTORY
  // assume set (it can prune away a real counterexample). There is no
  // PROVEN-vacuous (final owner ruling 2026-10-14): both exit 7, and the
  // message names which one and what to fix.
  const std::string_view lec_cause = r.cause_detail.empty() ? std::string_view{r.detail} : std::string_view{r.cause_detail};
  const bool lec_contradictory     = lec_cause.find("CONTRADICTORY") != std::string_view::npos
                                 || lec_cause.find("contradictory") != std::string_view::npos;
  // The short reading of each word on the headline, ahead of the engine detail.
  auto lec_phrase = [&](const Top_outcome& t) -> std::string {
    switch (t.kind) {
      case livehd::lec::Outcome::Proven_full: return {};
      case livehd::lec::Outcome::Proven_bounded:
        return std::format("no difference for {} design cycle(s) from reset, exhaustive over inputs; deeper cycles not checked",
                           t.n);
      case livehd::lec::Outcome::Refuted_vacuous:
        return lec_contradictory ? "the active assume set is CONTRADICTORY, so the comparison proved nothing"
                                 : "nothing to compare; the comparison proved nothing";
      case livehd::lec::Outcome::Tested_sampled:
        return std::format("{} reached; {} input vectors, no mismatch{}; equivalence not proven",
                           livehd::lec::effort_name(sampling.reached),
                           sampling.vectors,
                           sampling.reached != sampling.requested
                               ? std::format("; requested {}", livehd::lec::effort_name(sampling.requested))
                               : std::string{});
      case livehd::lec::Outcome::Timeout_time: return "formal.timeout reached; neither proved nor refuted";
      case livehd::lec::Outcome::Timeout_memory: return "out of memory; neither proved nor refuted";
      case livehd::lec::Outcome::Refuted_cex:
        if (livehd::lec::one_sided_box_counterexample(r)) {
          return std::format(
              "not equivalent: the counterexample needs only free outputs of blackbox cell(s) that exist on the {} side alone; "
              "the boxed model is the contract (give their definitions, e.g. --lib, to check the cells)",
              r.unmatched_ref.empty() ? "impl" : "ref");
        }
        return "not equivalent";
      case livehd::lec::Outcome::Refuted_interface: return "the top port names differ";
      case livehd::lec::Outcome::Refuted_unsupported: return "no engine can model or check this design";
      case livehd::lec::Outcome::Refuted_unchecked: return "the selected options cannot decide it";
      case livehd::lec::Outcome::Refuted_unexplained: return "no recognized cause: a LiveHD bug to report";
    }
    return {};
  };
  auto print_headline = [&](const Top_outcome& t, const std::string& detail) {
    const std::string phrase = lec_phrase(t);
    std::print("lec: '{}' {} ({}{}{})\n",
               impl_g->get_name(),
               t.word,
               phrase,
               phrase.empty() || detail.empty() ? "" : "; ",
               detail);
  };
  // Machine-readable verdicts: `status`/exit code alone cannot tell a proof
  // from a timeout (see Result::Lec_verdict).
  auto record_verdict = [&](const Top_outcome& t) {
    res.lec.present = true;
    res.lec.verdict = t.json;
    res.lec.bounded = t.kind == livehd::lec::Outcome::Proven_bounded;
    res.lec.bound   = res.lec.bounded ? t.n : int64_t{0};
    res.lec.vectors = t.kind == livehd::lec::Outcome::Tested_sampled ? static_cast<uint64_t>(t.n) : uint64_t{0};
  };
  print_headline(top, r.detail);
  record_verdict(top);
  res.lec.effort = std::string{livehd::lec::effort_name(effort)};
  if (sampling.ran()) {
    res.lec.sampled         = true;
    res.lec.effort_reached  = std::string{livehd::lec::effort_name(sampling.reached)};
    res.lec.sample_vectors  = sampling.vectors;
    res.lec.sample_traces   = sampling.traces;
    res.lec.sample_partial  = sampling.partial;
    res.lec.sample_queries  = sampling.queries;
    res.lec.sample_capped   = sampling.unknown;
    res.lec.sample_solve_us = sampling.solve_us;
    std::print(
        "lec[effort]: {} input vectors over {} traces ({} partially pinned), {} queries ({} capped), "
        "{} register lead(s) over {} watched, {}us solving\n",
        sampling.vectors,
        sampling.traces,
        sampling.partial,
        sampling.queries,
        sampling.unknown,
        sampling.leads,
        sampling.watched,
        sampling.solve_us);
  }
  res.lec.solver = o.solver;
  if (top.kind == livehd::lec::Outcome::Refuted_cex || top.kind == livehd::lec::Outcome::Proven_full) {
    // The state report explains a WEAK verdict; a definitive one needs none
    // (a child def's report can survive into a top that still proved).
    res.lec.unpaired_state.clear();
    res.lec.suggested_match.clear();
  } else if (!r.step_cex.empty()) {
    // Why the proof stopped short of unbounded: the induction step diverges from
    // this ARBITRARY state. Print it apart from the detail so the state reads at
    // a glance (suggestions6 1.6).
    res.lec.induction_cex = r.step_cex;
    std::print("  induction step fails from an arbitrary state (may be unreachable): {}\n", r.step_cex);
    std::print("  hint: if that state is unreachable, state the invariant with `assume(...)` on the register(s); a proven "
               "assume becomes an induction hypothesis\n");
  }
  // The witness names the diverging COMMON outputs; print it on a refute AND on
  // an undecided run that still carries a matched-portion diff (the actionable
  // iteration signal), not only on REFUTED-cex.
  if (!r.witness.empty()) {
    std::print("  counterexample: {}\n", r.witness);
  }
  // simfail simulation test + VCD (`lhd lec` + --workdir). On REFUTED-cex,
  // write simfail_<resolved-top>.prp and optionally run it. Without a user
  // --workdir both actions are off; formal.witness gates the feature.
  if (top.kind == livehd::lec::Outcome::Refuted_cex) {
    const bool simfail = o.witness && workdir_set && setting_enabled(labels.contains("simfail") ? labels.at("simfail") : "true");
    const bool simfail_run = setting_enabled(labels.contains("simfail_run") ? labels.at("simfail_run") : "true");
    // A counterexample that rests on a BOX (a trusted def's free outputs, or a
    // divergence at a trusted box's inputs) is still REFUTED-cex -- the boxed
    // model is the contract -- but the source replay flattens the real leaf,
    // which can neither drive that free output nor show a box-input difference
    // the leaf ignores. A replay that runs clean would read as "spurious".
    // A one-sided box (an undefined cell) has no source to replay at all.
    const bool on_box = !o.trust.empty() || r.witness.find("bbin:") != std::string::npos
                        || r.witness.find("bbox:") != std::string::npos || livehd::lec::one_sided_box_counterexample(r);
    if (simfail && lec_single_edge_slots > 1) {
      // 2f-latch M8 step 2d, same reason as the verify half: the generator
      // re-emits the un-normalized sides and drives the trace at the reported
      // index, which after normalization counts SUB-steps. A replay that runs
      // clean would read as "the counterexample was spurious", so skip honestly.
      livehd::diag::info("pass.lec", "simfail-skip", "io")
          .msg(
              "formal.simfail testbench not generated: the designs were edge-normalized into {} sub-steps "
              "per clock period, so the witness cycle indices do not line up with the un-normalized sources",
              lec_single_edge_slots)
          .emit();
    } else if (simfail && on_box) {
      livehd::diag::info("pass.lec", "simfail-skip", "io")
          .msg(
              "formal.simfail testbench not generated: the counterexample rests on a boxed model (formal.lec.trust, or "
              "cells with no definition), and a source replay cannot drive a box output or show a box-input difference "
              "the real leaf ignores")
          .emit();
    } else if (simfail) {
      const std::string top_name = lecfail_simple_name(impl_g->get_name());
      emit_lecfail_witness(opts,
                           res,
                           r,
                           std::string(impl_g->get_name()),
                           std::string(ref_g->get_name()),
                           simfail_filename(top_name),
                           simfail_run);
    }
  }

  // The exit ladder: every outcome's message says what happened and the hint
  // what to fix. conclude_outcome() maps the outcome to the exit status.
  auto lec_conclude = [&](const Top_outcome& t, const std::string& detail) {
    const std::string name    = std::string{impl_g->get_name()};
    const std::string witness = r.witness.empty() ? std::string{} : std::format("; candidate trace: {}", r.witness);
    std::string       msg;
    std::string       hint;
    switch (t.kind) {
      case livehd::lec::Outcome::Refuted_vacuous:
        if (lec_contradictory) {
          // Name the contradictory SCOPE: lec's active assume set is the two
          // designs' own assume(...) statements (proven or contract).
          msg  = std::format("'{}' is compared under a CONTRADICTORY assume set (the designs' active assumes: {} in ref '{}', "
                             "{} in impl '{}'): no input satisfies it, so the comparison proved nothing and could hide a real "
                             "counterexample ({})",
                             name,
                             ref_assumes.proven + ref_assumes.unchecked,
                             ref_g->get_name(),
                             impl_assumes.proven + impl_assumes.unchecked,
                             impl_g->get_name(),
                             detail);
          hint = "fix the conflicting assume(...)/assume_nocheck(...) statements so at least one input satisfies them all: "
                 "an unsatisfiable assume set makes any comparison hold";
        } else {
          msg  = std::format("'{}' compared nothing ({}), so this run proves nothing about it", name, detail);
          hint = "nothing to compare: select a top (--top / --ref-top / --impl-top) that drives at least one output or "
                 "state cell on both sides, or restore the dropped output ports";
        }
        break;
      case livehd::lec::Outcome::Timeout_time:
        msg  = std::format("'{}' hit a time limit before a verdict; equivalence is neither proved nor refuted", name);
        hint = "raise --set formal.timeout, or decompose the check (hierarchy, --collapse)";
        break;
      case livehd::lec::Outcome::Timeout_memory:
        msg  = std::format("'{}' ran out of memory before a verdict; equivalence is neither proved nor refuted", name);
        hint = "run on a host with more memory (or a larger LIVEHD_MEMORY_BUDGET_MB), or decompose the check";
        break;
      case livehd::lec::Outcome::Refuted_cex:
        msg  = std::format("'{}' is not equivalent ({} vs {})", name, opts.impl_path, opts.ref_path);
        hint = r.witness.empty() ? std::string{} : std::format("counterexample: {}", r.witness);
        break;
      case livehd::lec::Outcome::Refuted_interface:
        msg  = std::format("'{}' top ports differ by NAME ({}{})", name, detail, witness);
        hint = "the two tops must declare the same port names (widths may differ; an input that drives nothing may be "
               "one-sided): rename the port, add it to the other side, or select matching tops with --ref-top/--impl-top";
        break;
      case livehd::lec::Outcome::Refuted_unsupported:
        msg  = std::format("'{}' cannot be modeled or checked by any engine", name);
        hint = std::format(
            "{}{}. Nothing was decided and raising formal.timeout cannot help: change the design feature named above, "
            "exclude it (formal.ignore_memory / formal.lec.trust, both disclosed), or report a LiveHD bug if it should be "
            "supported",
            detail,
            witness);
        break;
      case livehd::lec::Outcome::Refuted_unchecked:
        msg  = std::format("'{}' cannot be decided under the selected options", name);
        hint = std::format(
            "{}{}. Drop the pinned formal.engine/formal.portfolio (engine=auto escalates to an engine that handles "
            "this logic), raise formal.bound, or re-enable the escalation the options disabled",
            detail,
            witness);
        break;
      case livehd::lec::Outcome::Refuted_unexplained:
        msg  = std::format("'{}' was not decided and no recognized cause explains why", name);
        hint = std::format("{}{}. This is a LiveHD bug: please report it with this command and its inputs", detail, witness);
        break;
      default: break;
    }
    conclude_outcome(t, "pass.lec", "lec", msg, hint);
  };
  if (!cross) {
    lec_conclude(top, r.detail);
    return;
  }

  auto impl_v = fs::absolute(materialize_verilog(opts, res, opts.impl_kind, opts.impl_path, "impl")).string();
  auto ref_v  = fs::absolute(materialize_verilog(opts, res, opts.ref_kind, opts.ref_path, "ref")).string();
  // cross mode re-materializes both sides (and every --lib) through
  // materialize_verilog, which re-records their input paths (load_side_graphs
  // already did above) — collapse res.inputs back to one entry per path.
  dedup_inputs(res);
  auto      lgcheck         = locate_lgcheck();
  auto      yosys           = locate_lgcheck_yosys();
  auto      rundir          = fs::absolute(workdir(opts)).string();
  // lgcheck's bounded miter counts clk2fflogic GLOBAL-clock steps (the clock
  // is a free input sampled every step), and a rising edge becomes visible only
  // every second step: a divergence k edges deep needs 2k steps (measured:
  // count==5 from init 0 refutes at 10 steps, not 9; count==2 at 4, not 3).
  // Native step s observes the state after s-1 edges, so covering the native
  // window of `cycles` steps takes 2*(cycles-1) lgcheck steps. `cycles` is the
  // REQUESTED design-cycle bound (formal.bound, the native default 6 when <= 0),
  // not o.bound: an edge-normalized design scales o.bound into sub-steps, while
  // lgcheck reads the un-normalized Verilog.
  const int requested_bound = std::atoi(label("bound", "6").c_str());
  const int lg_cycles       = requested_bound > 0 ? requested_bound : 6;  // native BMC default (pass/lec/query.cpp)
  const int lg_bmc_steps    = std::max(1, 2 * (lg_cycles - 1));
  auto      cmd
      = std::format("cd {} && LGCHECK_BMC_STEPS={} {} --implementation {} --reference {} --gold_reader slang --gate_reader slang",
                    shell_quote(rundir),
                    lg_bmc_steps,
                    shell_quote(lgcheck),
                    shell_quote(impl_v),
                    shell_quote(ref_v));
  if (!yosys.empty()) {
    cmd += std::format(" --yosys {}", shell_quote(yosys));
  }
  if (!opts.top.empty()) {
    cmd += std::format(" --top {}", shell_quote(opts.top));
  }
  if (!opts.impl_top.empty()) {
    cmd += std::format(" --implementation_top {}", shell_quote(opts.impl_top));
  }
  if (!opts.ref_top.empty()) {
    cmd += std::format(" --reference_top {}", shell_quote(opts.ref_top));
  }
  if (setting_enabled(label("normalize_split_ports", "false"))) {
    cmd += " --normalize_split_ports";
  }
  if (setting_enabled(label("descend_on_inconclusive", "false"))) {
    cmd += " --descend_on_inconclusive";
  }
  res.recipe_steps.emplace_back("pass.lec cross-check:lgcheck");
  auto log              = next_log_path(opts, "lec.lgcheck");
  // Truncate, never append: the log name repeats across runs sharing a
  // --workdir, and the marker scan below must only ever see THIS run's output.
  cmd                  += std::format(" > {} 2>&1", shell_quote(fs::absolute(log).string()));
  const int status      = std::system(cmd.c_str());
  const int code        = status != -1 && WIFEXITED(status) ? WEXITSTATUS(status) : -1;
  // lgcheck reserves exit 0 for unbounded equivalence. Its complete BMC is a
  // bounded result over lgcheck's OWN window: zero-init (-set-init-zero), from
  // t=1 without a reset, or reset held at t=1-2 and checked from t=4. That is
  // not the native after_reset window (native adds reset_hold cycles and uses
  // its own init policy), and the reset-bearing alignment is unverified. So a
  // bounded lgcheck pass may CORROBORATE a native pass, but never contradicts
  // a native REFUTE. The marker is emitted only after every depth succeeds;
  // failures and incomplete runs retain their original verdict.
  bool      lg_bounded  = false;
  if (code == 2) {
    std::ifstream proof_log(log);
    std::string   line;
    const auto    marker = std::format("BMC: found no counterexample within {} steps top:", lg_bmc_steps);
    while (std::getline(proof_log, line)) {
      if (line.starts_with("INCONCLUSIVE:")) {
        break;  // later descent diagnostics concern children, not this miter
      }
      lg_bounded |= line.starts_with(marker);
    }
  }
  res.lec.crosscheck_bounded   = lg_bounded;
  res.lec.crosscheck_bound     = lg_bounded ? lg_cycles : 0;
  // lgcheck exits 0 for an unbounded proof, 1 for a counterexample and 2 when
  // it could not decide (its complete bounded miter is then lg_bounded); any
  // other status means the tool did not run.
  const bool        lg_proven   = code == 0;
  const bool        lg_refuted  = code == 1;
  const bool        lg_failed   = code != 0 && code != 1 && code != 2;
  res.lec.crosscheck_verdict    = lg_proven    ? "proven-full"
                                  : lg_bounded ? "proven-bounded"
                                  : lg_refuted ? "refuted-cex"
                                  : lg_failed  ? "failed"
                                               : "incomplete";
  res.lec.crosscheck_exit_code  = code;
  const std::string lg_verdict  = lg_bounded ? std::format("proven-bounded({})", lg_cycles) : res.lec.crosscheck_verdict;
  // Spell the native leg's depth too: "proven" next to a bounded lgcheck line
  // read as the stronger of the two answers when it was the weaker.
  const std::string native_word = top.n >= 0 && top.kind == livehd::lec::Outcome::Proven_bounded
                                      ? std::format("{}({})", top.json, top.n)
                                      : top.json;
  std::print("lec cross-check: engine={} -> {}; lgcheck -> {}\n", o.engine, native_word, lg_verdict);

  // Cross-check rules (owner rulings 2026-10-14). The native verdict stands
  // unless a rule below names another one.
  //  * a native REFUTED-<why> other than cex (REFUTED-vacuous included) is
  //    decided natively: the cross-check is an extra oracle, never a
  //    replacement;
  //  * lgcheck that failed to RUN is a dependency failure (exit 5);
  //  * native TIMEOUT + lgcheck decides -> adopt lgcheck's decision
  //    (PROVEN-full, or REFUTED-cex);
  //  * native REFUTED-cex + lgcheck undecided -> REFUTED-cex with a warning;
  //  * native and lgcheck decide opposite ways -> the DISAGREE internal error;
  //  * native proven + lgcheck undecided -> REFUTED-unchecked: the cross-check
  //    option requires lgcheck's agreement, and it did not decide.
  const bool native_proven  = top.kind == livehd::lec::Outcome::Proven_full || top.kind == livehd::lec::Outcome::Proven_bounded;
  const bool native_cex     = top.kind == livehd::lec::Outcome::Refuted_cex;
  const bool native_timeout = livehd::lec::is_timeout(top.kind);
  if (!native_proven && !native_cex && !native_timeout) {
    lec_conclude(top, r.detail);
    return;
  }
  if (lg_failed) {
    throw Lhd_error{"dependency",
                    "lgcheck cross-check failed to run",
                    std::format("lgcheck exit status {}; see {}. Fix the lgcheck/yosys installation, or drop formal.solver=lgyosys",
                                code,
                                log)};
  }
  if (native_timeout) {
    if (lg_proven || lg_refuted) {
      const auto adopted = make_top_outcome(lg_proven ? livehd::lec::Outcome::Proven_full : livehd::lec::Outcome::Refuted_cex);
      const auto why     = std::format("adopted from the lgcheck cross-check after the native engine's {}; see {}", top.word, log);
      print_headline(adopted, why);
      record_verdict(adopted);
      lec_conclude(adopted, why);
      return;
    }
    lec_conclude(top, r.detail);
    return;
  }
  if ((native_proven && lg_refuted) || (native_cex && lg_proven)) {
    throw Lhd_error{"internal",
                    std::format("lec engine and lgcheck DISAGREE (engine={}, lgcheck={})", native_word, lg_verdict),
                    std::format("see {}", log)};
  }
  if (native_cex && !lg_refuted) {
    livehd::diag::warn("pass.lec", "lec-crosscheck-undecided", "unsupported")
        .msg("lgcheck cross-check did not decide '{}' ({}); the native REFUTED-cex stands", impl_g->get_name(), lg_verdict)
        .hint(lg_bounded ? std::format("its zero-init {}-step bounded window did not reach the native counterexample; see {}",
                                       lg_bmc_steps,
                                       log)
                         : std::format("see {}", log))
        .emit();
  }
  if (native_proven && !lg_proven && !lg_bounded) {
    const auto unchecked = make_top_outcome(livehd::lec::Outcome::Refuted_unchecked);
    const auto why       = std::format(
        "formal.solver=lgyosys requires lgcheck's agreement and lgcheck did not decide (exit status {}; see {}); the "
        "native engine reported {}",
        code,
        log,
        top.word);
    print_headline(unchecked, why);
    record_verdict(unchecked);
    lec_conclude(unchecked, why);
    return;
  }
  lec_conclude(top, r.detail);
}

// simfail_<formal-test>.prp: on a REFUTED obligation with --workdir, write a
// self-contained Pyrope testbench
// that instantiates the DESIGN, drives the violating per-cycle input trace,
// and (formal.simfail_run) replays it under `lhd sim --set sim.vcd=true`. A design
// assert then FIRES during the replay (sim exiting non-zero is the expected
// reproduction, not a failure); a formal-block obligation has no runtime
// check, but the VCD still shows every signal the block reads.
void emit_formalfail_witness(Options& opts, Result& res, const livehd::lec::Prop_result& prop, const std::string& design_kind,
                             const std::string& design_path, const std::string& top_full, const std::string& simfail, bool run_sim,
                             const std::string& embed_assert) {
  auto skip = [&](std::string_view why) {
    livehd::diag::info("pass.formal", "simfail-skip", "io").msg("formal.simfail testbench not generated: {}", why).emit();
  };
  if (prop.trace.empty()) {
    skip("the refuted obligation carries no reproducible input trace (witnesses disabled?)");
    return;
  }
  const auto& tr = prop.trace;

  const std::string simfail_path = opts.workdir + "/" + simfail;
  std::string       stem         = fs::path(simfail_path).stem().string();
  std::string       test_name;
  for (char c : stem) {
    test_name += (std::isalnum(static_cast<unsigned char>(c)) != 0) ? c : '_';
  }
  if (test_name.empty() || (std::isdigit(static_cast<unsigned char>(test_name[0])) != 0)) {
    test_name = "simfail_" + test_name;
  }

  livehd::diag::info("pass.formal", "simfail-creating-prp", "progress")
      .msg("formal verify: creating counterexample simulation test {}", simfail_path)
      .emit();

  const std::string lhd_bin = livehd::file_utils::get_exe_path() + "/lhd";
  std::string       top     = lecfail_simple_name(top_full);

  // Import the ORIGINAL source when it is a Pyrope file with a `pub` top (a fix
  // to the .prp then flows into a re-run of the SAME simfail test); else
  // inline the re-emitted copy (self-contained).
  const std::string design_stem = fs::path(design_path).stem().string();
  const bool        can_import  = design_kind == "pyrope" && !design_stem.empty() && lecfail_prp_top_is_pub(design_path, top);

  // Same rule as the lec generator: the import form references the original .prp
  // verbatim, so it needs the design's HEADER and nothing else — round-tripping a
  // Pyrope design back out through pass.prp_writer is pure cost, and it is the one
  // step that can fail over a construct the WRITER cannot emit, taking the whole
  // replay with it.
  std::vector<Lecfail_mod> mods;
  if (can_import) {
    mods = lecfail_parse_file(design_path);
    if (mods.empty()) {
      skip("no lambda header could be parsed from the design's .prp");
      return;
    }
  } else {
    const std::string design_dir = opts.workdir + "/formalfail_prp";
    const std::string log        = next_log_path(opts, "formal.simfail");
    if (!lecfail_emit_side(lhd_bin, opts, design_kind, design_path, design_dir, opts.workdir + "/formalfail_w", log)) {
      skip(std::format("the design could not be re-emitted as Pyrope (lg:/yosys-verilog has no LNAST); see {}", log));
      return;
    }
    mods = lecfail_parse_dir(design_dir);
    if (mods.empty()) {
      skip("no Pyrope modules were re-emitted for the design");
      return;
    }
  }
  const Lecfail_mod* m = nullptr;
  for (const auto& mm : mods) {
    if (mm.name == top) {
      m = &mm;
    }
  }
  if (m == nullptr) {
    skip("could not locate the TOP module in the re-emitted Pyrope");
    return;
  }

  absl::flat_hash_map<std::string, int> width_of;
  for (const auto& cyc : tr.cycles) {
    for (const auto& in : cyc.inputs) {
      width_of[in.name] = in.width < 1 ? 1 : in.width;
    }
  }
  // The design's inputs as SCALAR leaves. A struct port is not writable from a
  // test — `_dut.io.bits.x = v` is rejected read-only and `_dut.io = <tuple>` is
  // not a supported test expression — so a struct-ported design gets a thin
  // wrapper whose ports are the flattened leaves, exactly as the lec generator
  // builds. Without it the drive came out as `_dut.io = 0`: a scalar into a
  // struct port, which fails the replay compile and silently produced no VCD.
  std::vector<Lecfail_leaf> leaves = m->in_leaves;
  bool                      wrap   = false;
  for (const auto& lf : leaves) {
    wrap = wrap || lf.dotted.find('.') != std::string::npos;
  }
  struct Fport {
    std::string dotted;
    std::string flat;
    std::string decl;
  };
  std::vector<Fport>               win;
  absl::flat_hash_set<std::string> flat_used;
  for (const auto& lf : leaves) {
    std::string flat;
    for (char c : lf.dotted) {
      if (c == '.') {
        flat += "__";
      } else if ((std::isalnum(static_cast<unsigned char>(c)) != 0) || c == '_') {
        flat += c;
      }
    }
    if (flat.empty() || (std::isdigit(static_cast<unsigned char>(flat[0])) != 0)) {
      flat = "p_" + flat;
    }
    while (!flat_used.insert(flat).second) {
      flat += "_";
    }
    win.push_back({lf.dotted, flat, lf.type});
  }
  const int ncyc   = static_cast<int>(tr.cycles.size());
  auto      val_at = [&](const std::string& name, int c) -> std::string {
    for (const auto& in : tr.cycles[static_cast<size_t>(c)].inputs) {
      if (in.name == name) {
        return in.value;
      }
    }
    return "0";
  };
  const bool reset_is_port
      = std::any_of(win.begin(), win.end(), [](const Fport& w) { return lecfail_trace_name(w.dotted) == "reset"; });
  const bool implicit_reset = width_of.count("reset") != 0 && !reset_is_port;
  if (wrap && implicit_reset) {
    // `_dut.reset` would land on the WRAPPER's implicit reset, never reaching
    // the design's. Rather than emit a testbench that drives the wrong flop,
    // say why nothing was written.
    skip("the design has struct port(s) AND an implicit reset — the flattening wrapper cannot thread the reset");
    return;
  }

  const std::string dut_mod = can_import ? std::string{"dutmod"} : top;
  const std::string wrapper = "__simfail_dut_wrap";
  const std::string inst    = "_simfail_dut";  // named so the embedded check can read into it
  auto              wtype   = [&](const Fport& w) {
    if (lecfail_is_bool_type(w.decl)) {
      return std::string{":Bool"};  // Bool ports are strict; the trace carries it as 0/1
    }
    if (lecfail_is_clock_type(w.decl)) {
      return std::string{":Clock"};
    }
    if (lecfail_is_reset_type(w.decl)) {
      return std::string{":Reset"};
    }
    if (auto it = width_of.find(lecfail_trace_name(w.dotted)); it != width_of.end()) {
      return std::format(":U{}", it->second);
    }
    if (!w.decl.empty()) {
      const size_t at = w.decl.find("@[");
      return at == std::string::npos ? w.decl : w.decl.substr(0, at);
    }
    return std::string{":U1"};
  };
  std::string wrap_text;
  if (wrap) {
    std::string sig_in;
    for (const auto& w : win) {
      sig_in += (sig_in.empty() ? "" : ", ") + lecfail_flat_ident(w.dotted, w.flat) + wtype(w);
    }
    std::string sig_out;
    std::string args;
    for (const auto& [n, suf] : m->outputs) {
      sig_out += (sig_out.empty() ? "" : ", ") + n + (suf.find("@[") == std::string::npos ? suf + "@[]" : suf);
    }
    for (const auto& w : win) {
      args += (args.empty() ? "" : ", ") + std::format("{} = {}", w.dotted, lecfail_flat_ident(w.dotted, w.flat));
    }
    wrap_text = std::format("mod {}({}) -> ({}) {{\n  const {} = {}({})\n", wrapper, sig_in, sig_out, inst, dut_mod, args);
    for (const auto& [n, suf] : m->outputs) {
      wrap_text += std::format("  {} = {}.{}\n", n, inst, n);
    }
    wrap_text += "}\n\n";
  }

  const std::string callee    = wrap ? wrapper : dut_mod;
  std::string       test_text = std::format("test {} {{\n  mut _dut = {}\n", test_name, callee);
  for (const auto& w : win) {
    std::string arr;
    for (int c = 0; c < ncyc; ++c) {
      arr += (arr.empty() ? "" : ", ") + val_at(lecfail_trace_name(w.dotted), c);
    }
    test_text += std::format("  const _drv_{} = [{}]\n", w.flat, arr);
  }
  if (implicit_reset) {
    std::string arr;
    for (int c = 0; c < ncyc; ++c) {
      arr += (arr.empty() ? "" : ", ") + val_at("reset", c);
    }
    test_text += std::format("  const _drv_reset = [{}]\n", arr);
  }
  test_text += std::format("  tick {} {{\n", ncyc);
  // Same reset rule as the lecfail generator: drive only a reset that exists
  // (explicit port via the input loop; implicit via the trace; NONE for a
  // reset-less design — `_dut.reset` would be an unknown field).
  if (implicit_reset) {
    test_text += "    _dut.`reset` = _drv_reset[`clock`] != 0\n";  // a Bool, like a declared Reset
  }
  for (const auto& w : win) {
    if (lecfail_is_clock_type(w.decl)) {
      continue;  // a Clock is the sim clock, never poked
    }
    test_text += std::format("    _dut.{} = _drv_{}[`clock`]{}\n",
                             lecfail_flat_ident(w.dotted, w.flat),
                             w.flat,
                             lecfail_is_bool_type(w.decl) || lecfail_is_reset_type(w.decl) ? " != 0" : "");
  }
  if (!embed_assert.empty()) {
    // The violated formal-block assertion, re-targeted at the instance
    // (`__p_*` idents -> `_dut.<path>` reads): the replay TRIGGERS it through
    // the test-assert machinery at exactly the violating cycle, so the run
    // fails with a located `assert fail: clock=N` line plus the VCD. Behind the
    // flattening wrapper the design sits one level down, so every `_dut.` read
    // has to grow that level.
    std::string chk = embed_assert;
    if (wrap) {
      // Three cases for the path after `_dut.`, and only the last one grows a
      // level: an INPUT leaf becomes the wrapper's flat port (`io.bits.x` ->
      // `io__bits__x`); an OUTPUT is already re-exposed at the wrapper, and
      // reading it through the instance fails ("unknown state 'o'"); anything
      // else is design-internal state, which lives one level down.
      absl::flat_hash_set<std::string> outs;
      for (const auto& [n, suf] : m->outputs) {
        outs.insert(n);
      }
      std::string       rebuilt;
      const std::string from = "_dut.";
      size_t            i    = 0;
      while (i < chk.size()) {
        if (chk.compare(i, from.size(), from) != 0) {
          rebuilt += chk[i++];
          continue;
        }
        size_t e = i + from.size();  // the dotted path that follows
        while (e < chk.size() && ((std::isalnum(static_cast<unsigned char>(chk[e])) != 0) || chk[e] == '_' || chk[e] == '.')) {
          ++e;
        }
        const std::string path = chk.substr(i + from.size(), e - i - from.size());
        std::string       flat;
        for (const auto& w : win) {  // LONGEST input leaf first (`io` vs `io.bits`)
          if ((path == w.dotted || path.starts_with(w.dotted + ".")) && w.dotted.size() > flat.size()) {
            flat = w.dotted;
          }
        }
        if (!flat.empty()) {
          for (const auto& w : win) {
            if (w.dotted == flat) {
              rebuilt += from + w.flat + path.substr(flat.size());
              break;
            }
          }
        } else if (outs.contains(path.substr(0, path.find('.')))) {
          rebuilt += from + path;
        } else {
          rebuilt += from + inst + "." + path;
        }
        i = e;
      }
      chk = rebuilt;
    }
    test_text += std::format("    if `clock` == {} {{\n      {}\n    }}\n", tr.diverge_cycle, chk);
  }
  test_text += "    step\n  }\n}\n";

  std::string what = prop.kind + (prop.loc.empty() ? "" : " at " + prop.loc) + (prop.block.empty() ? "" : " [" + prop.block + "]")
                     + (prop.msg.empty() ? "" : " \"" + prop.msg + "\"");
  const std::string rerun = can_import ? std::format("lhd sim {} {} --set sim.vcd=true --workdir <dir>", design_path, simfail_path)
                                       : std::format("lhd sim {} --set sim.vcd=true --workdir <dir>", simfail_path);
  // Only an obligation that was pinned to one statement is re-checked in the
  // body; the header must say which of the two files this is, because "the
  // replay FAILS on it" printed over a check-less testbench reads as "the
  // counterexample was spurious".
  std::string       out   = std::format(
      "/*\n:name: {}\n:type: simulation\n*/\n"
      "// AUTO-GENERATED by `lhd formal verify` from a REFUTED obligation.\n"
      "// design='{}'  violated: {}\n"
      "// Drives the design with the violating input sequence ({} cycle(s), {} reset-hold);\n"
      "// the violation lands at cycle {}.\n"
      "{}"
      "// Re-run:  {}   (dumps {}.vcd)\n\n",
      test_name,
      design_path,
      what,
      ncyc,
      tr.reset_cycles,
      tr.diverge_cycle,
      embed_assert.empty() ? "// NO runtime check is embedded (a design-body assert is not executed by sim, and a\n"
                             "// formal-block obligation could not be pinned to one statement) — read the violation\n"
                             "// off the VCD against the sibling simfail JSON.\n"
                           : "// The formal-block obligation is re-checked in the test body below, so the replay\n"
                             "// FAILS on it; a design-body assert is not yet executed by sim — read those off the VCD.\n",
      rerun,
      test_name);
  if (can_import) {
    out += std::format("const dutmod = import(\"{}.{}\")\n\n", design_stem, top);
  } else {
    for (const auto& mm : mods) {
      out += lecfail_type_params(mm.text, width_of);
      if (!out.empty() && out.back() != '\n') {
        out += '\n';
      }
      out += '\n';
    }
  }
  out += wrap_text + test_text;

  std::ofstream ofs(simfail_path);
  if (!ofs.is_open()) {
    skip(std::format("could not write {}", simfail_path));
    return;
  }
  ofs << out;
  ofs.close();
  res.outputs.push_back(simfail_path);
  res.recipe_steps.push_back(std::format("formal.simfail simulation test -> {}", simfail_path));
  std::print("formal verify: wrote counterexample simulation test {}\n", simfail_path);

  // F7: machine-readable sibling artifact. For a verify obligation the "root" is the
  // failing assert itself, so stamp its kind/loc/violation-cycle into the trace copy
  // — the simfail JSON then carries the same signal shape on both formal paths.
  livehd::lec::Witness_trace jtr = prop.trace;
  if (jtr.root_cycle < 0) {
    jtr.root_key   = prop.kind;
    jtr.root_cycle = prop.refuted_at;
    jtr.root_src   = prop.loc;
  }
  std::string json_path = simfail_path.substr(0, simfail_path.size() - 4) + ".json";
  emit_witness_json(json_path, "simfail", design_path, design_path, jtr);
  res.outputs.push_back(json_path);

  if (!run_sim) {
    return;
  }
  livehd::diag::info("pass.formal", "simfail-creating-vcd", "progress")
      .msg("formal verify: creating counterexample waveform {}/{}.vcd", opts.workdir, test_name)
      .emit();
  const std::string sim_log = next_log_path(opts, "formal.simfail_run");
  std::string       cmd     = shell_quote(lhd_bin) + " sim ";
  if (can_import) {
    cmd += shell_quote(design_path) + " ";
  }
  // A counterexample replay is an observation run: it must never create or
  // consult a sim tune store inside the lec/formal workdir.
  cmd += shell_quote(simfail_path) + " --set sim.vcd=true --set sim.tune.profile=off --workdir " + shell_quote(opts.workdir);
  for (const auto& [k, v] : opts.sets) {
    if ((k == "sim.hlop_dir" || k == "sim.iassert_dir" || k == "sim.vcd_fake_delay") && !v.empty()) {
      cmd += " --set " + shell_quote(k + "=" + v);
    }
  }
  cmd             += " >> " + shell_quote(sim_log) + " 2>&1";
  int         st   = std::system(cmd.c_str());
  std::string vcd  = std::format("{}/{}.vcd", opts.workdir, test_name);
  // A design assert firing makes the replay exit non-zero — that IS the
  // reproduction; the artifact that matters is the waveform.
  if (fs::exists(vcd)) {
    res.outputs.push_back(vcd);
    res.recipe_steps.push_back(std::format("formal.simfail_run VCD -> {}", vcd));
    const bool fired = !(WIFEXITED(st) && WEXITSTATUS(st) == 0);
    std::print("formal verify: wrote counterexample waveform {}{}\n",
               vcd,
               fired ? " (the replay reproduced the violation: the runtime assert fired)" : "");
    if (!fired) {
      // Silence here would read as "reproduced". With NO embedded check there
      // is nothing that could have fired — do not blame free initial state for
      // that. Otherwise the usual cause is exactly that: the witness depends on
      // FREE INITIAL STATE (an init-less register / memory, or a reset-less
      // design) which the BMC may choose but a sim replay cannot set (sim
      // powers up at the declared init / zero).
      if (embed_assert.empty()) {
        livehd::diag::warn("pass.formal", "simfail-replay-no-refire", "io")
            .msg(
                "the simfail replay ran clean ({}): NO runtime check is embedded (the obligation is a "
                "design-body assert, which sim does not execute, or it could not be pinned to one statement) — "
                "read the violation off the VCD against the sibling simfail JSON",
                vcd)
            .emit();
      } else {
        livehd::diag::warn("pass.formal", "simfail-replay-no-refire", "io")
            .msg(
                "the simfail replay ran but did NOT re-fire the assert ({}): the witness likely depends on "
                "free initial state (init-less registers/memories or no reset input) that the sim cannot "
                "reproduce; inspect the VCD against the sibling simfail JSON",
                vcd)
            .emit();
      }
    }
  } else {
    livehd::diag::warn("pass.formal", "simfail-sim", "io")
        .msg("formal.simfail_run: `lhd sim {}` did not produce {} (see {})", simfail_path, vcd, sim_log)
        .emit();
  }
}

// ---- formal_report.json (P2 agent feedback) ---------------------------------

static std::string json_esc(std::string_view s) {
  std::string o;
  for (char c : s) {
    switch (c) {
      case '"' : o += "\\\""; break;
      case '\\': o += "\\\\"; break;
      case '\n': o += "\\n"; break;
      case '\t': o += "\\t"; break;
      case '\r': o += "\\r"; break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          o += std::format("\\u{:04x}", static_cast<int>(static_cast<unsigned char>(c)));
        } else {
          o += c;
        }
    }
  }
  return o;
}

// ---- `formal verify --list-tests` --------------------------------------------
// A formal block is an INDEPENDENT test (its assumes scope to itself), which is
// why it enumerates through the same `--list-tests` face `lhd sim` gives its
// `test` blocks. The JSON keeps sim's envelope — {"file":…,"tests":[{"name":…,
// "params":[]}]} — so one enumerator reads both commands; `params` is always
// empty because the grammar gives a formal block no parameter list (parse_formal:
// "No parameter list: a formal block takes no runtime arguments"). The extra
// per-test fields (file / target / line / assert+assume counts) are additive.
struct Formal_test_info {
  std::string file;    // the .prp the block was read from (design or sidecar)
  std::string name;    // dotted block name — the selector handle
  std::string target;  // module the alias chain binds to ("" = the verified top)
  int         line    = 0;
  int         asserts = 0;  // assert / assert_always statements
  int         assumes = 0;  // assume / assume_nocheck* statements
};

static Formal_test_info formal_test_info(const std::string& file, const livehd::formal_blocks::Block& blk) {
  Formal_test_info t;
  t.file   = file;
  t.name   = blk.name;
  t.target = blk.target;
  t.line   = blk.line;
  for (const auto& st : blk.stmts) {
    // The extractor already rejected anything that is not one of these forms, so
    // the leading keyword classifies the statement.
    if (st.text.compare(0, 6, "assume") == 0) {
      ++t.assumes;
    } else {
      ++t.asserts;
    }
  }
  return t;
}

static std::string formal_tests_to_json(const std::vector<std::string>& block_files, const std::vector<Formal_test_info>& tests) {
  // "file" mirrors sim's single-source envelope: the LAST block source, which is
  // the sidecar in the canonical `verify <design> <sidecar>` call. Every entry
  // also carries its own "file", so a multi-sidecar run stays unambiguous.
  std::string j  = "{\"file\":\"";
  j             += json_esc(block_files.empty() ? std::string{} : block_files.back());
  j             += "\",\"tests\":[";
  for (size_t i = 0; i < tests.size(); ++i) {
    const auto& t  = tests[i];
    j             += (i != 0 ? ",{\"name\":\"" : "{\"name\":\"");
    j             += json_esc(t.name);
    j             += "\",\"params\":[],\"file\":\"";
    j             += json_esc(t.file);
    j             += "\",\"line\":" + std::to_string(t.line);
    j             += ",\"target\":\"" + json_esc(t.target);
    j             += "\",\"asserts\":" + std::to_string(t.asserts);
    j             += ",\"assumes\":" + std::to_string(t.assumes);
    j             += "}";
  }
  j += "]}";
  return j;
}

// WHY an undecided verify obligation is open (the text behind its
// TIMEOUT-* / REFUTED-<why> word). `human` adds the remediation spelled on the
// stdout row; the report keeps the bare reason.
static std::string prop_undecided_why(const livehd::lec::Prop_result& p, const livehd::lec::Verify_result& r, bool human) {
  const bool scope_vacuous = std::find(r.vacuous_scopes.begin(), r.vacuous_scopes.end(), p.scope) != r.vacuous_scopes.end();
  std::string why = !p.undecided_why.empty() ? p.undecided_why  // carries its own remediation hint
                    : p.undecided_at >= 0
                        ? std::format("solver gave up at cycle {} with no recorded reason{}",
                                      p.undecided_at,
                                      human ? " (a LiveHD bug: please report it)" : "")
                    : scope_vacuous ? (p.scope.empty() ? std::string{"the design's own assume set is contradictory"}
                                                       : std::format("assume set of block '{}' is contradictory", p.scope))
                                    : std::string{"not checked, and no reason was recorded (a LiveHD bug)"};
  if (p.kind == "assume") {
    why += human ? "; unproven assume — NOT used (make it provable, or spell assume_nocheck to impose it UNCHECKED)"
                 : "; unproven assume — NOT used";
  }
  return why;
}

// The machine-readable run report an agent parses each loop iteration: written
// on EVERY run (whatever the outcome -- a TIMEOUT or a REFUTED-<why> is exactly
// the case the agent must act on), covering every obligation with its verdict,
// cycles, classification, cumulative solve time, timeout-core membership, and
// the witness artifact paths. The stdout table stays the human view; this file
// is the contract (ids are stable: kind@file:line[block]). `run_outcome` is the
// run's Top_outcome, the same one the headline and the exit ladder use.
static void emit_formal_report(const std::string& path, const std::string& design_path, std::string_view top,
                               const livehd::lec::Verify_result& r, const livehd::lec::Lec_options& o,
                               const absl::flat_hash_map<std::string, std::string>& artifacts, const Top_outcome& run_outcome,
                               bool engine_forced, int slots) {
  auto prop_id = [](const livehd::lec::Prop_result& p) {
    std::string id = p.kind + "@" + (p.loc.empty() ? std::string{"?"} : p.loc);
    if (!p.block.empty()) {
      id += "[" + p.block + "]";
    } else if (!p.instance.empty()) {
      id += "[" + p.instance + "]";
    }
    return id;
  };
  absl::flat_hash_set<size_t> in_core;
  for (int ix : r.timeout_core) {
    if (ix >= 0 && static_cast<size_t>(ix) < r.props.size()) {
      in_core.insert(static_cast<size_t>(ix));
    }
  }
  int n_unch = 0, n_cp = 0, n_cu = 0, n_cr = 0;
  for (const auto& p : r.props) {
    if (p.kind != "assume") {
      continue;
    }
    if (livehd::lec::is_unchecked_assume_class(p.aclass)) {
      ++n_unch;
    } else if (p.verdict == livehd::lec::Verdict::Proven) {
      ++n_cp;
    } else if (p.verdict == livehd::lec::Verdict::Refuted) {
      ++n_cr;
    } else {
      ++n_cu;
    }
  }
  std::string j    = "{\n";
  j               += "  \"schema_version\": 1,\n  \"kind\": \"formal_report\",\n";
  j               += std::format("  \"design\": \"{}\",\n  \"top\": \"{}\",\n", json_esc(design_path), json_esc(top));
  j               += "  \"run\": {\n";
  j               += std::format("    \"verdict\": \"{}\",\n{}    \"detail\": \"{}\",\n",
                                 run_outcome.json,
                                 run_outcome.kind == livehd::lec::Outcome::Proven_bounded
                                     ? std::format("    \"depth\": {},\n", run_outcome.n)
                                     : std::string{},
                                 json_esc(r.detail));
  j               += std::format("    \"elapsed_ms\": {},\n    \"checked_steps\": {},\n    \"reset_hold\": {},\n",
                                 r.elapsed_ms,
                                 r.checked_steps,
                                 r.reset_hold);
  j               += std::format("    \"reset_detected\": {},\n    \"vacuous\": {},\n",
                                 r.reset_detected ? "true" : "false",
                                 r.vacuous ? "true" : "false");
  {  // which assume scopes were contradictory ("" = the design tier)
    std::string vs;
    for (const auto& s : r.vacuous_scopes) {
      vs += std::format("{}\"{}\"", vs.empty() ? "" : ", ", json_esc(s));
    }
    j += std::format("    \"vacuous_scopes\": [{}],\n", vs);
  }
  j += std::format("    \"engine\": \"{}\",\n    \"bound\": {},\n", json_esc(o.engine), o.bound);
  // Soft-budget block: the target, the floor beneath it, and what the run
  // actually cost against them (spent/units/floored are 0 when no budget was in
  // force). `floored` is the overrun's cause, so an agent can tell "raise the
  // target" from "lower the floor" without re-running.
  j += std::format(
      "    \"budget\": {{\"timeout_s\": {}, \"min_timeout_s\": {}, "
      "\"spec_mining_timeout_s\": {}, \"spent_ms\": {}, \"units\": {}, \"floored\": {}}},\n",
      o.timeout,
      o.min_timeout,
      o.spec_mining_timeout,
      r.budget_spent_ms,
      r.budget_units,
      r.budget_floored);
  // lhd.stats only: the cvc5 solve-insight object. Carries its OWN trailing
  // comma — assume_counts below is the last member of "run" and deliberately has
  // none, so this must be inserted before it, never after.
  if (o.stats) {
    j += std::format("    \"cvc5\": {},\n", livehd::lec::cvc5_stats_json(r.cvc5));
  }
  // Every assume outside the unchecked classes is a checked obligation
  // (prove-then-use), so the ledger is by-verdict; the exact class stays visible
  // per obligation in its "aclass" field.
  j += std::format(
      "    \"assume_counts\": {{\"unchecked\": {}, \"checked_proven\": {}, \"checked_unproven\": {}, "
      "\"checked_refuted\": {}}}\n",
      n_unch,
      n_cp,
      n_cu,
      n_cr);
  j += "  },\n  \"obligations\": [\n";
  for (size_t i = 0; i < r.props.size(); ++i) {
    const auto& p    = r.props[i];
    std::string file = p.loc;
    std::string line = "0";
    if (auto colon = p.loc.rfind(':'); colon != std::string::npos) {
      file = p.loc.substr(0, colon);
      line = p.loc.substr(colon + 1);
    }
    const bool  env_assume = p.kind == "assume" && livehd::lec::is_unchecked_assume_class(p.aclass);
    // An unchecked assume is a constraint in force, never an obligation, so it
    // carries no outcome; every other row reads the SAME outcome word as its
    // stdout row and the run's fold (prop_top_outcome).
    const auto        po      = prop_top_outcome(p, r, engine_forced, slots);
    const std::string verdict = env_assume ? std::string{"in_force"} : po.json;
    std::string       why;
    if (!env_assume && p.verdict != livehd::lec::Verdict::Proven && p.verdict != livehd::lec::Verdict::Refuted) {
      why = prop_undecided_why(p, r, /*human=*/false);
    } else if (!env_assume && prop_cut_short(p, r)) {
      why = std::format("proven to cycle {}; not checked past it: the formal.timeout budget ran out before the requested cycle {}",
                        p.proven_to,
                        verify_last_cycle(r));
    }
    const std::string depth
        = !env_assume && po.kind == livehd::lec::Outcome::Proven_bounded ? std::format("\"depth\": {}, ", po.n) : std::string{};
    // R1 Phase 2: `guarded` says the obligation is `guard implies cond` (written
    // inside an `if`/`match` arm), `vacuous_guard` says that antecedent is
    // unsatisfiable over the checked window — a PROVEN that checked nothing. An
    // agent should treat vacuous_guard like an unproven obligation even though
    // the verdict is honestly "proven".
    j += std::format(
        "    {{\"id\": \"{}\", \"kind\": \"{}\", \"file\": \"{}\", \"line\": {}, \"msg\": \"{}\", "
        "\"block\": \"{}\", \"instance\": \"{}\", \"aclass\": \"{}\", \"verdict\": \"{}\", {}\"unbounded\": {}, \"proven_to\": {}, "
        "\"refuted_at\": {}, \"undecided_at\": {}, \"undecided_why\": {}, \"solve_ms\": {}, "
        "\"in_timeout_core\": {}, \"guarded\": {}, \"vacuous_guard\": {}, "
        "\"cone_nodes\": {}, \"cone_state\": {}, \"witness\": {}}}{}\n",
        json_esc(prop_id(p)),
        json_esc(p.kind),
        json_esc(file),
        line.empty() ? "0" : line,
        json_esc(p.msg),
        json_esc(p.block),
        json_esc(p.instance),
        json_esc(p.aclass),
        verdict,
        depth,
        p.unbounded ? "true" : "false",
        p.proven_to,
        p.refuted_at,
        p.undecided_at,
        why.empty() ? std::string{"null"} : "\"" + json_esc(why) + "\"",
        p.solve_ms,
        in_core.contains(i) ? "true" : "false",
        p.guarded ? "true" : "false",
        p.vacuous_guard ? "true" : "false",
        p.cone_nodes,
        p.cone_state,
        p.witness.empty() ? std::string{"null"} : "\"" + json_esc(p.witness) + "\"",
        i + 1 < r.props.size() ? "," : "");
  }
  j += "  ],\n  \"timeout_core\": [";
  {
    bool first = true;
    for (int ix : r.timeout_core) {
      if (ix >= 0 && static_cast<size_t>(ix) < r.props.size()) {
        j     += std::format("{}\"{}\"", first ? "" : ", ", json_esc(prop_id(r.props[static_cast<size_t>(ix)])));
        first  = false;
      }
    }
  }
  j += "],\n  \"artifacts\": {";
  {
    bool first = true;
    for (const auto& [k, v] : artifacts) {
      j     += std::format("{}\"{}\": \"{}\"", first ? "" : ", ", json_esc(k), json_esc(v));
      first  = false;
    }
  }
  j += "},\n  \"mined\": [\n";
  for (size_t i = 0; i < r.mined.size(); ++i) {
    const auto& m = r.mined[i];
    std::string tgts;
    for (int t : m.targets) {
      if (t >= 0 && static_cast<size_t>(t) < r.props.size()) {
        tgts += std::format("{}\"{}\"", tgts.empty() ? "" : ", ", json_esc(prop_id(r.props[static_cast<size_t>(t)])));
      }
    }
    std::string keys;
    for (const auto& k : m.keys) {
      keys += std::format("{}\"{}\"", keys.empty() ? "" : ", ", json_esc(k));
    }
    j += std::format(
        "    {{\"pyrope\": {}, \"smt2\": \"{}\", \"provenance\": \"{}\", \"status\": \"{}\", "
        "\"keys\": [{}], \"targets\": [{}]}}{}\n",
        m.pyrope.empty() ? std::string{"null"} : "\"" + json_esc(m.pyrope) + "\"",
        json_esc(m.smt2),
        json_esc(m.provenance),
        m.inductive ? "inductive" : "speculative",
        keys,
        tgts,
        i + 1 < r.mined.size() ? "," : "");
  }
  j += "  ]\n}\n";
  std::ofstream ofs(path);
  if (ofs.is_open()) {
    ofs << j;
  }
}

// P3 sidecar emission: the INDUCTIVE mined invariants as a paste-ready formal
// block. Written fresh each mining run (auto-managed — never hand-edited); an
// agent curates it: pass it as an extra positional on the next run, or copy
// statements into its own sidecar. Every statement is an `assume` over design
// state, so the P1 discipline re-proves it on use — a stale invariant after a
// design edit REFUTES instead of corrupting the run.
static void emit_mined_block(const std::string& path, const std::string& design_path, std::string_view top_full,
                             const livehd::lec::Verify_result& r) {
  std::vector<const livehd::lec::Verify_result::Mined_invariant*> emit;
  for (const auto& m : r.mined) {
    if (m.inductive && !m.pyrope.empty()) {
      emit.push_back(&m);
    }
  }
  if (emit.empty()) {
    return;
  }
  auto entity = [](std::string_view n) {
    auto d = n.rfind('.');
    return d == std::string_view::npos ? n : n.substr(d + 1);
  };
  const std::string ent  = std::string(entity(top_full));
  const std::string stem = fs::path(design_path).stem().string();
  std::string       s;
  s += "// AUTO-GENERATED by `lhd formal verify` (mined invariants) — regenerated each mining run.\n";
  s += "// Each fact was proven at every checked cycle AND survived the induction step of the run\n";
  s += "// that mined it. As formal-block assumes they are RE-PROVEN on use (P1 prove-then-use),\n";
  s += "// so a stale invariant after a design edit refutes loudly instead of corrupting a proof.\n";
  s += std::format("const top = import(\"{}.{}\")\n\n", stem, ent);
  s += std::format("formal {}.mined {{\n  mut acc = top\n", ent);
  for (const auto* m : emit) {
    s += std::format("  // {}\n  assume({})\n", m->provenance, m->pyrope);
  }
  s += "}\n";
  std::ofstream ofs(path);
  if (ofs.is_open()) {
    ofs << s;
  }
}

// ---- formal verify (2f-verify V1: single-design assert/assume BMC) ----------

// `lhd formal verify <design> [--top m] [--set formal.bound=N ...]`: prove the
// design's fproperty obligations (user assert / assert_always / assume) by BMC
// from reset on the pass/lec engine (lec::prove_properties): per-obligation
// checkSatAssuming with frontier assumes, a per-assert/per-cycle verdict table,
// and per-obligation timeout isolation. Exit policy is lec's, word for word
// (owner rulings 2026-10-14): REFUTED-cex (a reachable violation, or a refuted
// checked assume) exits 10, any other REFUTED-<why> exits 7 (REFUTED-vacuous:
// no assert, a dead guard, or a contradictory assume set), PROVEN-* exits 0,
// and TIMEOUT-* exit 0 with a warning. Knobs: formal.* (shared engine), formal.lec.*
// (lec-only), formal.verify.* (verify-only), with lec.* accepted as aliases.
void formal_verify_command(Options& opts, Result& res) {
  // Captured before any workdir() call fabricates a scratch dir: the simfail
  // testbench + VCD default ON only for a persistent, user-named --workdir.
  const bool workdir_set = !opts.workdir.empty() && !opts.workdir_scratch;
  setup_diag(opts, "formal");
#ifndef NDEBUG
  livehd::diag::info("pass.formal", "formal-debug-build-slow", "progress")
      .msg("formal verify is slow and you compile without optimizations. Maybe `bazel build -c opt //...`")
      .emit();
#endif

  // The design: --impl KIND:PATH wins; else the first positional after the
  // `verify` subcommand word (kind by extension — verilog:/pyrope: prefixed
  // positionals were stripped to plain paths by route_positional); else a
  // routed lg:DIR. V1 takes ONE design source (sidecar formal-block files: V2).
  std::string              kind = opts.impl_kind;
  std::string              path = opts.impl_path;
  std::vector<std::string> extras(opts.files.begin() + (opts.files.empty() ? 0 : 1), opts.files.end());

  // A LONE NON-PATH positional is the block selector — the same shape `lhd sim`
  // gives its `test` blocks (`lhd sim tb.prp my.test`), since a formal block IS
  // an independent test. Split it off BEFORE the design pick so the selector may
  // sit anywhere among the positionals (the design is then the first remaining
  // path, exactly as before). It feeds the same fnmatch filter as --formal, so a
  // plain dotted name selects one block and a glob still selects a family.
  std::string block_sel;
  {
    std::vector<std::string> paths;
    for (const auto& f : extras) {
      auto ends = [&](std::string_view s) { return f.size() > s.size() && f.compare(f.size() - s.size(), s.size(), s) == 0; };
      if (ends(".prp") || ends(".v") || ends(".sv")) {
        paths.push_back(f);
      } else if (block_sel.empty()) {
        block_sel = f;
      } else {
        throw Lhd_error{"usage",
                        std::format("formal verify: '{}' and '{}' both look like a formal-block selector", block_sel, f),
                        "pass at most ONE selector; use --formal '<glob>' to select a family of blocks"};
      }
    }
    extras = std::move(paths);
  }
  if (!block_sel.empty()) {
    block_sel = str_tools::canonical_escaped_path(block_sel);  // `cnt`.`bounded` selects cnt.bounded
    if (!opts.formal_filter.empty() && opts.formal_filter != block_sel) {
      throw Lhd_error{
          "usage",
          std::format("formal verify: the block selector '{}' conflicts with --formal '{}'", block_sel, opts.formal_filter),
          "pass the selector as a positional OR as --formal, not both"};
    }
    opts.formal_filter = block_sel;
  }
  if (path.empty() && !opts.ins.empty()) {
    // An explicitly routed lg:DIR is the design; every .prp positional stays a
    // formal-block sidecar (the pre-compiled flow for import-heavy designs).
    kind = "lg";
    path = opts.ins.front().path;
  }
  if (path.empty() && !extras.empty()) {
    // extras now holds only .prp/.v/.sv paths (the selector split above), so the
    // extension decides the kind outright.
    const std::string& f = extras.front();
    kind                 = (f.size() > 4 && f.compare(f.size() - 4, 4, ".prp") == 0) ? "pyrope" : "verilog";
    path                 = f;
    extras.erase(extras.begin());
  }
  if (path.empty() && !opts.ins.empty()) {
    kind = "lg";
    path = opts.ins.front().path;
  }
  if (path.empty()) {
    throw Lhd_error{"usage",
                    "formal verify needs a design (a .prp/.v/.sv path, --impl KIND:PATH, or lg:DIR)",
                    "e.g. `lhd formal verify foo.prp --top foo.top`"};
  }
  // Extra .prp positionals are formal-block sources (the sidecar files): they
  // are parsed for `formal name.dotted { ... }` blocks, never compiled as
  // design. The design file itself (when Pyrope) is also a block source, so a
  // design and its blocks can share one file.
  std::vector<std::string> block_files;
  if (kind == "pyrope") {
    block_files.push_back(path);
  }
  for (const auto& f : extras) {
    if (f.size() > 4 && f.compare(f.size() - 4, 4, ".prp") == 0) {
      block_files.push_back(f);
    } else {
      throw Lhd_error{"usage",
                      std::format("formal verify: unexpected extra input '{}'", f),
                      "extra inputs must be .prp formal-block (sidecar) files"};
    }
  }
  std::string block_src;  // the block sources, for the listing + selector diagnostics
  for (const auto& bf : block_files) {
    block_src += (block_src.empty() ? "" : ", ") + bf;
  }

  // ---- --list-tests: a pure parse of the `formal` blocks -> their dotted names
  // (the SAME contract `lhd sim --list-tests` has for `test` blocks). No design
  // load and no solver, so tooling can enumerate the tests cheaply — and even
  // when the design does not compile. Output honors --diag-fmt: JSON by default
  // when piped, a human listing in pretty mode.
  if (opts.list_tests) {
    if (block_files.empty()) {
      throw Lhd_error{"usage",
                      std::format("formal verify --list-tests: '{}' carries no formal blocks (not a Pyrope source)", path),
                      "pass the sidecar holding the `formal` blocks: `lhd formal verify <design> <sidecar.prp> --list-tests`"};
    }
    std::vector<Formal_test_info> listed;
    for (const auto& bf : block_files) {
      for (auto& blk : livehd::formal_blocks::extract(bf, /*allow_nocheck=*/true)) {
        if (!blk.error.empty()) {
          throw Lhd_error{"usage", std::format("formal block error: {}", blk.error), ""};
        }
        if (!opts.formal_filter.empty() && fnmatch(opts.formal_filter.c_str(), blk.name.c_str(), 0) != 0) {
          continue;
        }
        listed.push_back(formal_test_info(bf, blk));
      }
    }
    if (listed.empty()) {
      throw Lhd_error{"usage",
                      opts.formal_filter.empty() ? std::format("no formal blocks found in {}", block_src)
                                                 : std::format("no formal block named '{}' in {}", opts.formal_filter, block_src),
                      "run `lhd formal verify <design> <sidecar.prp> --list-tests` to see the block names"};
    }
    if (opts.diag_fmt == Diag_fmt::pretty) {
      std::print("{} formal block(s) in {}:\n", listed.size(), block_src);
      for (const auto& t : listed) {
        std::print("  {}{} [{} assert(s), {} assume(s)] at {}:{}\n",
                   t.name,
                   t.target.empty() ? std::string{} : std::format(" -> {}", t.target),
                   t.asserts,
                   t.assumes,
                   t.file,
                   t.line);
      }
    } else {
      std::print("{}\n", formal_tests_to_json(block_files, listed));
    }
    std::fflush(stdout);
    return;  // status stays pass (a pure query — nothing was proved)
  }

  // Knobs: lec.* (legacy aliases) < formal.* (the one shared namespace).
  Eprp_var::Eprp_dict labels;
  merge_sets(opts, "formal", labels);      // the shared formal.* vocabulary
  merge_sets(opts, "formal.lec", labels);  // lec-specific canonical spelling wins
  auto label = [&](std::string_view k, std::string_view def) -> std::string {
    auto it = labels.find(std::string{k});
    return it == labels.end() ? std::string{def} : it->second;
  };

  Eprp_var var;
  {
    // formal verify is the authoritative top-rooted property proof. Keep the
    // established compile formal pass preparation, but skip its new hierarchy
    // assumption preflight: otherwise the same assumption is proved twice and
    // a shallow compile budget could reject it before verify reaches the user's
    // requested bound. The local pass still emits the required top-IO warning.
    const size_t saved_sets = opts.sets.size();
    opts.sets.emplace_back("compile.formal.warn_vacuous", "false");
    const bool saved_preflight    = opts.compile_formal_preflight;
    opts.compile_formal_preflight = false;
    if (label("assume_check", "true") == "false" || label("assume_check", "true") == "0") {
      opts.sets.emplace_back("compile.formal.assume_check", "false");
    }
    load_side_graphs(opts, res, kind, path, "impl", var);
    opts.compile_formal_preflight = saved_preflight;
    opts.sets.resize(saved_sets);
  }

  // Top pick: --impl-top / --top, else the sole module; entity fallback like
  // lec (pick_top_graph warns when the fallback substitutes the full name).
  auto g = pick_top_graph(var, opts.impl_top, opts.top, "", "formal verify", "pass.formal");

  // The options PINNED an engine (anything but engine=auto): an engine that
  // cannot handle an obligation is then REFUTED-unchecked, not -unsupported.
  const bool engine_forced = label("engine", "auto") != "auto" || label("portfolio", "auto") != "auto";
  // A refusal BEFORE the proof still prints the run's verdict line, in the
  // same words as a solved run: `formal verify: '<top>' WORD (...)`.
  auto refuse_presolve = [&](livehd::lec::Outcome refusal, const std::string& why, const std::string& hint) {
    const auto t = make_top_outcome(refusal);
    std::print("formal verify: '{}' {} ({})\n", g->get_name(), t.word, why);
    record_formal_verdict(res, t);
    conclude_outcome(t, "pass.formal", "formal", std::format("'{}' {}", g->get_name(), why), hint);
  };

  livehd::lec::Lec_options o;
  // F3: verify gets the shared portfolio — engine=auto races two whole-run
  // strategies (bmc-first at the full bound | ind-first at a shallow base case
  // whose induction rung promotes deep-state invariants to unbounded) and merges
  // per-obligation firsts. bmc / ind still select a single strategy directly.
  o.engine       = label("engine", "auto");
  o.solver       = label("solver", "cvc5");
  o.assume_check = label("assume_check", "true") != "false" && label("assume_check", "true") != "0";
  // Same escape hatch as lec: both drivers instantiate the same Encoder, so a
  // memory it refuses to model (per-port clock edges) has to be excludable here
  // too, or `lhd formal verify` on that design has no way forward.
  if (std::string ms = label("ignore_memory", ""); !ms.empty()) {
    size_t pos = 0;
    while (pos < ms.size()) {
      size_t c   = ms.find(',', pos);
      size_t end = c == std::string::npos ? ms.size() : c;
      if (end > pos) {
        o.ignore_memory.emplace_back(ms.substr(pos, end - pos));
      }
      pos = end + 1;
    }
  }
  o.bound               = std::atoi(label("bound", "6").c_str());
  o.timeout             = std::atoi(label("timeout", "120").c_str());
  o.witness             = label("witness", "true") != "false" && label("witness", "true") != "0";
  o.phase               = label("phase", "after_reset");
  o.reset_cycles        = std::atoi(label("reset_cycles", "2").c_str());
  o.reset               = label("reset", "");
  o.partitions          = std::atoi(label("partitions", "4").c_str());
  o.jobs                = static_cast<int>(livehd::Job_manager::global().ceiling());
  o.split               = label("split", "auto");
  try {
    o.effort = livehd::lec::parse_effort(label("effort", ""));
  } catch (const std::invalid_argument& error) {
    throw Lhd_error{"usage", error.what(), "choose quick-check, test, long-check, verify, or proof"};
  }
  if (livehd::lec::is_sampled(o.effort)) {
    throw Lhd_error{"usage",
                    "sampled effort presets apply to lhd lec comparisons",
                    "omit formal.effort for property verification, or use verify for an uncapped attempt"};
  }
  if (o.effort == livehd::lec::Effort::Verify) {
    o.timeout = std::atoi(label("timeout", "0").c_str());
  }
  // Soft total (on iff timeout>0 && rlimit==0): `timeout` is a TOTAL cvc5-time budget spent
  // across every obligation-check, not `timeout` per check (the O×C hazard) —
  // the verify analogue of the hier-lec scheduler. rlimit>0 (deterministic tier)
  // disables it inside prove_properties. spec_mining_timeout (0=off): an INDEPENDENT
  // diagnosis budget that names the toxic obligation core of a timed-out run.
  o.min_timeout         = std::atoi(label("min_timeout", "1").c_str());
  // Hard wall backstop on a forked proof worker, as a multiple of `timeout` (0 = off).
  // `timeout` is cvc5 tlimit-per, which cannot preempt ONE long CaDiCaL solve -- the
  // shape a flat box-free miter takes. See Lec_options::hard_timeout_mult.
  o.hard_timeout_mult   = std::atoi(label("hard_timeout_mult", "3").c_str());
  o.spec_mining_timeout = std::atoi(label("spec_mining_timeout", "0").c_str());
  o.mine                = label("mine", "");  // P3 mining tier ("" = inductive only | speculative)
  // Statistics are selected once by --stats / lhd.stats.
  o.stats               = opts.stats;
  if (label("effort", "") == "proof" && o.timeout > 0) {
    o._deadline = std::chrono::steady_clock::now() + std::chrono::seconds(o.timeout);
  }

  std::unique_ptr<livehd::formal::Verdict_cache> vcache;
  if (workdir_set && opts.incremental) {
    // MATERIALIZE the workdir before the cache opens it. Verdict_cache::save()
    // treats an unopenable path as "the cache is only ever a speedup" and
    // returns silently -- so without this, every run stored to memory, wrote
    // NOTHING, printed its in-memory `N stored` tally, and came back 0 hit(s)
    // forever. The whole incremental tier (verdict cache, cone cache, Unknown
    // ledger, strategy hint) was dead for exactly this reason: no command on
    // the formal path creates --workdir, and only `save()` ever needed it to
    // already exist.
    ensure_dir(opts.workdir);
    vcache                = std::make_unique<livehd::formal::Verdict_cache>(opts.workdir, livehd::kFormalSrcSalt);
    o.verify_cache_lookup = [&vcache](std::string_view key) { return vcache->lookup(std::string{key}).has_value(); };
    o.verify_cache_store  = [&vcache](std::string key) { vcache->insert(key, {"bmc", "serialized verify obligation UNSAT", 0}); };
  }
  if (auto e = livehd::lec::lec_options_range_error(o); !e.empty()) {
    throw Lhd_error{"usage", e, "the BMC engine unrolls one SMT copy of the design per cycle"};
  }

  // --lib lg:DIR cell-model libraries, exactly as in lec.
  absl::flat_hash_map<hhds::Gid, hhds::Graph*> sub_lib;
  std::vector<std::shared_ptr<hhds::Graph>>    sub_lib_keep;
  for (const auto& lp : opts.libs) {
    if (lp.kind != "lg") {
      throw Lhd_error{"usage", std::format("formal verify --lib expects lg:DIR, got '{}:'", lp.kind), ""};
    }
    if (!fs::is_directory(lp.path)) {
      throw Lhd_error{"missing_file", std::format("formal verify --lib not found: {}", lp.path), ""};
    }
    auto& lib = livehd::Hhds_graph_library::instance(lp.path);
    for (const hhds::Gid id : lib.all_gids()) {
      auto lg = lib.get_graph(id);
      if (!lg) {
        continue;
      }
      sub_lib_keep.push_back(lg);
      sub_lib[id] = lg.get();
    }
  }
  const auto* sub_lib_ptr = sub_lib.empty() ? nullptr : &sub_lib;

  // ── 2f-latch M8: EDGE NORMALIZATION ───────────────────────────────────────
  // Rewrite latches and negedge state into posedge flops before the encoder
  // ever sees them. Placed HERE — after the --lib cell-model graphs load, so a
  // latch inside a cell model is not left as an opaque blackbox, and before the
  // monitors compile and the verdict cache keys anything.
  //
  // Deliberately NOT in graph_pipeline_and_emits: that is the shared graph half
  // of `lhd compile` and `lhd sim`, and this must never touch the synthesis
  // path (the netlist handed to ABC has to keep its real always_latch, and
  // keeping `lhd sim` on the SOURCE graph is what makes it an INDEPENDENT
  // oracle for this very transformation).
  const int single_edge_slots = [&] {
    std::vector<hhds::Graph*> defs;
    for (const auto& sp : var.graphs) {
      if (sp && sp.get() != g.get()) {
        defs.push_back(sp.get());
      }
    }
    for (const auto& [gid, gp] : sub_lib) {
      if (gp != nullptr && gp != g.get()) {
        defs.push_back(gp);
      }
    }
    // Same treatment `lhd lec` gets: the top AND every def, since a design that
    // instantiates its clock gate below the top is the normal case, not the
    // exception. Needs `defs` in hand, so it happens here rather than above.
    absl::flat_hash_set<hhds::Graph*> unfolded;
    if (const int mr = materialize_clock_cells_all(g.get(), defs); mr > 0) {
      res.recipe_steps.emplace_back(std::format("pass.single_edge recognized {} clock gate(s) as Clock_cell", mr));
    }
    if (const auto [cells, folded] = inline_clock_gates_and_fold(g.get(), defs, &unfolded); cells > 0) {
      res.recipe_steps.emplace_back(std::format("pass.single_edge inlined {} clock-gate cell(s), folded {} def(s)", cells, folded));
    }
    std::erase_if(defs, [&unfolded](hhds::Graph* d) { return unfolded.contains(d); });
    auto sn = livehd::single_edge::normalize(g.get(), defs);
    if (sn.error) {
      refuse_presolve(livehd::lec::Outcome::Refuted_unsupported,
                      std::format("edge normalization declined ({})", sn.reason),
                      "the design mixes clock edges or holds latches in a shape the normalizer does not model; a "
                      "partial lowering would be a silent full-cycle error, so it declines instead. Give the design one "
                      "clock edge per register, or report a LiveHD bug with the design");
      return 1;  // unreachable: a REFUTED outcome always throws
    }
    if (sn.applied) {
      res.recipe_steps.emplace_back(std::format("pass.single_edge slots:{} latches:{}", sn.slots, sn.latches_retyped));
    }
    return sn.applied ? sn.slots : 1;
  }();
  if (single_edge_slots > 1) {
    // `formal.bound` counts STEPS and a step is now a SUB-step, so the same
    // bound buys 1/P of the design cycles it used to. Scale it (and the report's
    // own view of it) so a fixture's `:verify_bound:` still means design cycles.
    o.bound *= single_edge_slots;
  }

  // ── Formal-block monitors (2f-verify V2) ──────────────────────────────────
  // Extract every `formal name.dotted { ... }` block from the block sources,
  // filter by --formal <glob>, resolve each referenced dotted path against the
  // design (top input/output ports; registers by canonical hierarchical name),
  // and compile the block's statements into a tiny comb MONITOR module through
  // the normal Pyrope pipeline — exact expression semantics, no re-implemented
  // evaluator. The engine binds the monitor inputs per cycle.
  std::vector<livehd::lec::Monitor>             mons;
  std::vector<Eprp_var>                         mon_keep;  // owns the monitor graphs' lifetime
  // "block\x1floc" -> the block statement re-targeted at `_dut.<path>` reads,
  // so a refuted obligation can be re-checked inside the simfail testbench.
  absl::flat_hash_map<std::string, std::string> fb_embed;
  {
    // Design signal tables (setup-time mirror of the engine's own collection).
    // `boolean`: a `bool` port or `reg`. The graph carries it as a 1-bit pin,
    // so the KIND comes from the unit's LNAST (the io_meta for a port, the
    // `declare` for a register); the monitor must declare it `bool` for a
    // property to read it as one (`acc.en == true`, `not acc.en` -- bools are
    // strict, no implicit u1<->bool).
    struct Sig {
      int  w;
      bool sgn;
      bool boolean = false;
    };
    auto entity = [](std::string_view n) {
      auto d = n.rfind('.');
      return d == std::string_view::npos ? n : n.substr(d + 1);
    };
    // The compiled Pyrope unit of a graph, by graph name (exact, else the
    // unique entity match: a graph keeps its `<file>.<entity>` name). A
    // Verilog or lg: design never declares a `bool` port or register, so all
    // its signals stay integers.
    auto unit_lnast = [&var, &entity](std::string_view unit) -> const Lnast* {
      const Lnast* found = nullptr;
      int          hits  = 0;
      for (const auto& ln : var.lnasts) {
        if (ln == nullptr) {
          continue;
        }
        const auto name = ln->get_top_module_name();
        if (name == unit) {
          return ln.get();
        }
        if (entity(name) == entity(unit)) {
          found = ln.get();
          ++hits;
        }
      }
      return hits == 1 ? found : nullptr;
    };
    auto is_bool_port = [](const Lnast* ln, std::string_view port) {
      const auto* e = ln == nullptr ? nullptr : ln->io_meta().find(port);
      return e != nullptr && e->kind == Io_kind::boolean;
    };
    // The `reg x:bool` names of a def (`declare(x, prim_type_bool, 'reg')`),
    // collected once per def graph.
    absl::flat_hash_map<const hhds::Graph*, absl::flat_hash_set<std::string>> bool_regs_of;
    auto is_bool_reg = [&](const hhds::Graph* def, std::string_view reg) {
      auto [it, fresh] = bool_regs_of.try_emplace(def);
      if (const Lnast* ln = fresh ? unit_lnast(def->get_name()) : nullptr; ln != nullptr) {
        for (const auto& nid : ln->depth_preorder()) {
          if (!Lnast_ntype::is_declare(ln->get_type(nid))) {
            continue;
          }
          const auto var_nid  = ln->get_first_child(nid);
          const auto type_nid = var_nid.is_invalid() ? var_nid : ln->get_sibling_next(var_nid);
          const auto mode_nid = type_nid.is_invalid() ? type_nid : ln->get_sibling_next(type_nid);
          if (!mode_nid.is_invalid() && Lnast_ntype::is_prim_type_bool(ln->get_type(type_nid)) && ln->get_name(mode_nid) == "reg") {
            it->second.emplace(ln->get_name(var_nid));
          }
        }
      }
      if (const auto p = reg.find("___ssa_"); p != std::string_view::npos) {
        reg = reg.substr(0, p);
      }
      return it->second.contains(reg);
    };
    absl::flat_hash_map<std::string, Sig> in_tbl, out_tbl, flop_tbl;
    {
      auto        gio      = g->get_io();
      const auto* top_unit = unit_lnast(g->get_name());
      for (const auto& d : gio->get_input_pin_decls()) {
        auto pin       = g->get_input_pin(d.name);
        int  w         = livehd::graph_util::real_width(pin, *gio, d.name);
        // Sign comes from the IO declaration because it is authoritative at the
        // module boundary. Typing an unsigned monitor input as signed would flip
        // ordered comparisons in user properties (assume(x <= 15) could hold
        // vacuously for large x).
        in_tbl[d.name] = Sig{w == 0 ? 1 : w, !gio->is_unsign(d.name), is_bool_port(top_unit, d.name)};
      }
      for (const auto& d : gio->get_output_pin_decls()) {
        auto pin        = g->get_output_pin(d.name);
        int  w          = livehd::graph_util::real_width(pin, *gio, d.name);
        out_tbl[d.name] = Sig{w == 0 ? 1 : w, !gio->is_unsign(d.name), is_bool_port(top_unit, d.name)};
      }
      for (auto node : g->occurrences().nodes(hhds::Node_order::forward)) {
        if (livehd::graph_util::type_op_of(node) != Ntype_op::Flop) {
          continue;
        }
        auto q = node.get_driver_pin(0);
        if (q.is_invalid()) {
          continue;
        }
        int w = livehd::graph_util::real_width(q);
        flop_tbl[livehd::lec::canon_flop_name(node.get_hier_name())]
            = Sig{w == 0 ? 1 : w,
                  !livehd::graph_util::is_unsign(q),
                  is_bool_reg(node.get_graph(), livehd::graph_util::pin_name_of(q))};
      }
    }
    int         gen_ix   = 0;
    int         sel_hits = 0;  // blocks the selector kept (only meaningful when one was given)
    std::string all_names;     // every block name seen, for the "no such block" diagnostic
    for (const auto& bf : block_files) {
      for (auto& blk : livehd::formal_blocks::extract(bf, /*allow_nocheck=*/true)) {
        if (!blk.error.empty()) {
          throw Lhd_error{"usage",
                          std::format("formal block error: {}", blk.error),
                          "formal blocks: alias bindings + assert/assume/assert_always/assume_nocheck over dotted signal paths"};
        }
        all_names += (all_names.empty() ? "" : ", ") + blk.name;
        if (!opts.formal_filter.empty() && fnmatch(opts.formal_filter.c_str(), blk.name.c_str(), 0) != 0) {
          continue;
        }
        ++sel_hits;
        if (blk.stmts.empty()) {
          continue;  // nothing to prove (aliases only)
        }
        // Where the block binds: the verified top
        // itself when the target IS the top (or unnamed), else EVERY instance
        // of the target module inside the top — the property must hold for
        // each one (reported as block@instance).
        std::vector<std::string> inst_prefixes;  // "" = the top itself
        if (blk.target.empty() || entity(blk.target) == entity(g->get_name())) {
          inst_prefixes.emplace_back("");
        } else {
          for (auto node : g->occurrences().nodes(hhds::Node_order::forward)) {
            if (livehd::graph_util::type_op_of(node) != Ntype_op::Sub) {
              continue;
            }
            auto sio = node.get_subnode_io();
            if (sio != nullptr && entity(sio->get_name()) == entity(blk.target)) {
              // Bind by the PHYSICAL occurrence path: unique and non-empty for
              // every non-top instance. A logical name drops transparent
              // (`__flat___*`) components, so it can be "" -- the sentinel for
              // the top itself, which would read the TOP's ports -- or collide
              // across instances. Logical naming applies only to the flop keys
              // (resolve() canonicalizes prefix.sig through canon_flop_name).
              std::string hname{node.get_hier_name()};
              if (hname.empty() || std::find(inst_prefixes.begin(), inst_prefixes.end(), hname) != inst_prefixes.end()) {
                throw Lhd_error{"usage",
                                std::format("formal block '{}': an instance of module '{}' in '{}' has {} hierarchical name",
                                            blk.name,
                                            blk.target,
                                            g->get_name(),
                                            hname.empty() ? "an empty" : std::format("a duplicate ('{}')", hname)),
                                "each bound instance needs a distinct, non-empty instance name"};
              }
              inst_prefixes.emplace_back(std::move(hname));
            }
          }
          if (inst_prefixes.empty()) {
            throw Lhd_error{"usage",
                            std::format("formal block '{}' targets module '{}', which '{}' does not instantiate",
                                        blk.name,
                                        blk.target,
                                        g->get_name()),
                            "bind the block to the verified top or to a module instantiated inside it"};
          }
        }
        // Submodule PORT table (decl width + decl sign, matching the encoder's
        // "\x05tap:" emission): a submodule-bound block reaches the instance's
        // ports as well as its registers. Same def => same decls per instance.
        absl::flat_hash_map<std::string, Sig> sub_port_tbl;
        if (!inst_prefixes.front().empty()) {
          // fast_hier: looks up ONE instance by hier name and breaks. Lazy, so
          // the break now ends the walk instead of paying a full materialize+sort
          // of the flattened design first.
          for (auto node : g->grouped_hierarchy().nodes()) {
            if (livehd::graph_util::type_op_of(node) != Ntype_op::Sub || node.get_hier_name() != inst_prefixes.front()) {
              continue;
            }
            if (auto sio = node.get_subnode_io(); sio != nullptr) {
              const auto* sub_unit = unit_lnast(sio->get_name());
              for (const auto& d : sio->get_input_pin_decls()) {
                sub_port_tbl[d.name]
                    = Sig{d.bits > 0 ? static_cast<int>(d.bits) : 1, !sio->is_unsign(d.name), is_bool_port(sub_unit, d.name)};
              }
              for (const auto& d : sio->get_output_pin_decls()) {
                sub_port_tbl[d.name]
                    = Sig{d.bits > 0 ? static_cast<int>(d.bits) : 1, !sio->is_unsign(d.name), is_bool_port(sub_unit, d.name)};
              }
            }
            break;
          }
        }
        // Resolve one signal path for one instance context ("" = the top).
        auto resolve = [&](const std::string& sig_path, const std::string& prefix, livehd::lec::Monitor::Bind& b) -> const Sig* {
          if (prefix.empty()) {  // top ports are only visible at the top itself
            if (auto it = in_tbl.find(sig_path); it != in_tbl.end()) {
              b.src = livehd::lec::Monitor::Bind::Src::input;
              b.key = sig_path;
              return &it->second;
            }
            if (auto ot = out_tbl.find(sig_path); ot != out_tbl.end()) {
              b.src = livehd::lec::Monitor::Bind::Src::output;
              b.key = sig_path;
              return &ot->second;
            }
          }
          std::string full = prefix.empty() ? sig_path : prefix + "." + sig_path;
          if (auto ft = flop_tbl.find(livehd::lec::canon_flop_name(full)); ft != flop_tbl.end()) {
            b.src = livehd::lec::Monitor::Bind::Src::state;
            b.key = livehd::lec::canon_flop_name(full);
            return &ft->second;
          }
          if (!prefix.empty()) {
            // Submodule ports bind through an encoder tap ("\x05tap:<inst>.<port>"),
            // read exactly like a top output (Src::output on the tap key).
            if (auto pt = sub_port_tbl.find(sig_path); pt != sub_port_tbl.end()) {
              b.src = livehd::lec::Monitor::Bind::Src::output;
              b.key = std::string("\x05tap:", 5) + prefix + "." + sig_path;
              return &pt->second;
            }
          }
          return nullptr;
        };
        auto is_bool_sig = [&](const std::string& sig_path) {
          livehd::lec::Monitor::Bind probe;
          const Sig*                 s = resolve(sig_path, inst_prefixes.front(), probe);
          return s != nullptr && s->boolean;
        };
        // `past(x, N)` -> a HISTORY port. The property text arrives with signal
        // paths already rewritten to `__p_*` idents, so a use reads
        // `past(__p_req, 2)`; turn each distinct (ident, N) into its own monitor
        // input `__p_req__past2` bound to the SAME design signal with delay=N,
        // and rewrite the text to name that port. The monitor therefore stays a
        // pure combinational function of its ports — the stateless contract
        // below is preserved — and the engine resolves the delay by indexing the
        // unroll. Local copies: the rewritten text feeds both the generated
        // monitor and the witness-replay embedding further down.
        auto blk_inputs = blk.inputs;
        auto blk_stmts  = blk.stmts;
        {
          auto idx_of = [&](std::string_view id) -> const livehd::formal_blocks::Input* {
            for (const auto& in : blk_inputs) {
              if (in.ident == id) {
                return &in;
              }
            }
            return nullptr;
          };
          // Ensure a history port for (base, n) exists; returns its ident.
          auto hist_port = [&](const livehd::formal_blocks::Input& base, int n) -> std::string {
            if (n <= 0) {
              return base.ident;  // depth 0 IS the current value
            }
            std::string hid = std::format("{}__past{}", base.ident, n);
            if (idx_of(hid) == nullptr) {
              livehd::formal_blocks::Input hin;
              hin.ident = hid;
              hin.path  = base.path;
              hin.delay = n;
              blk_inputs.push_back(std::move(hin));
            }
            return hid;
          };
          auto trim = [](std::string s) {
            while (!s.empty() && (std::isspace(static_cast<unsigned char>(s.front())) != 0)) {
              s.erase(s.begin());
            }
            while (!s.empty() && (std::isspace(static_cast<unsigned char>(s.back())) != 0)) {
              s.pop_back();
            }
            return s;
          };
          // The temporal vocabulary. A monitor port is an integer (a 1-bit
          // signal is u1) unless the design declares it `bool`, so a truth test
          // is `!= 0` on an integer and the value itself on a bool -- pyrope
          // keeps bool and int apart, and `and` is boolean-only.
          //
          // WINDOWS. `rose(x, 1..=10)` and friends are SVA's `##[1:10]`: they
          // look FORWARD from the property's anchor cycle. The engine only
          // indexes backward (`Bind::delay` reaches into mon_hist), so a forward
          // window is implemented by RETIMING the whole statement instead of
          // teaching the engine to look ahead: if the largest forward offset in
          // a statement is F, every term is additionally delayed by F, which
          // makes offset `+k` become backward delay `F-k` — all non-negative, so
          // every reference is ordinary history. The property is then evaluated
          // for anchor cycle `c` at unroll cycle `c+F`, and the existing
          // `cyc < max_delay` rule skips (and discloses) the cycles that have no
          // anchor. No engine change, no auxiliary state, and the shift applies
          // to the bare signal reads in the statement too — `rose(req) implies
          // rose(ack, 1..=10)` must read `req` at the anchor, not at `c+F`.
          struct Top {
            std::string_view name;
            int              nargs;   // 2 = takes a second argument
            bool             window;  // second argument is a bounded range, not a count
            bool             wneed;   // ...and it is REQUIRED
          };
          static constexpr std::array<Top, 7> kTemporal{
              {{"past", 2, false, false},
               {"rose", 2, true, false},
               {"fell", 2, true, false},
               {"stable", 2, true, false},
               {"changed", 2, true, false},
               {"eventually", 2, true, true},
               {"always", 2, true, true}}
          };

          // A bounded window literal: `lo..=hi` (inclusive) or `lo..<hi`.
          struct Window {
            int lo = 1;
            int hi = 1;
          };
          auto parse_window = [&](const std::string& s, std::string_view opname) -> Window {
            const auto dots = s.find("..");
            const bool incl = dots != std::string::npos && dots + 2 < s.size() && s[dots + 2] == '=';
            const bool excl = dots != std::string::npos && dots + 2 < s.size() && s[dots + 2] == '<';
            if (dots == std::string::npos || (!incl && !excl)) {
              throw Lhd_error{"usage",
                              std::format("formal block '{}': `{}` window '{}' is not a bounded range", blk.name, opname, s),
                              "write a literal range, e.g. eventually(x, 1..=32) or rose(x, 1..<10)"};
            }
            const std::string lo_s = trim(s.substr(0, dots));
            const std::string hi_s = trim(s.substr(dots + 3));
            auto              num  = [&](const std::string& t) {
              if (t.empty() || t.find_first_not_of("0123456789") != std::string::npos) {
                throw Lhd_error{
                    "usage",
                    std::format("formal block '{}': `{}` window bound '{}' is not a literal cycle", blk.name, opname, t),
                    "both ends must be compile-time numbers: rose(x, 1..=10)"};
              }
              return std::stoi(t);
            };
            Window w;
            w.lo = num(lo_s);
            w.hi = excl ? num(hi_s) - 1 : num(hi_s);
            if (w.hi < w.lo) {
              throw Lhd_error{"usage",
                              std::format("formal block '{}': `{}` window {}..{} is empty", blk.name, opname, w.lo, w.hi),
                              "a window must name at least one cycle; `1..<2` is the single cycle 1"};
            }
            return w;
          };

          // Locate the next bare temporal call at or after `from`, skipping
          // string literals (an obligation's message is part of the statement
          // text, so `assert(..., "past(x, 0) is x")` must not read as a call).
          struct Call {
            size_t      pos   = std::string::npos;
            size_t      close = std::string::npos;
            const Top*  op    = nullptr;
            std::string arg;
            std::string second;  // "" when the call has one argument
          };
          auto next_call = [&](const std::string& text, size_t from) -> Call {
            Call c;
            bool in_str = false;
            for (size_t pos = 0; pos < text.size();) {
              const char ch = text[pos];
              if (in_str && ch == '\\') {
                pos += 2;
                continue;
              }
              if (ch == '"') {
                in_str = !in_str;
                ++pos;
                continue;
              }
              if (in_str || pos < from) {
                ++pos;
                continue;
              }
              const Top* op = nullptr;
              for (const auto& t : kTemporal) {
                if (text.compare(pos, t.name.size(), t.name) == 0 && pos + t.name.size() < text.size()
                    && text[pos + t.name.size()] == '(') {
                  op = &t;
                  break;
                }
              }
              if (op == nullptr
                  || (pos > 0 && (std::isalnum(static_cast<unsigned char>(text[pos - 1])) != 0 || text[pos - 1] == '_'))) {
                ++pos;
                continue;
              }
              // Balanced scan: `past(rose(x), 1)` must not stop at rose's `)`.
              // Such a nesting is refused below (these take a SIGNAL, not an
              // expression), but the diagnosis has to name the real call.
              const size_t open  = pos + op->name.size();
              size_t       close = std::string::npos;
              for (size_t i = open, depth = 0; i < text.size(); ++i) {
                if (text[i] == '(') {
                  ++depth;
                } else if (text[i] == ')' && --depth == 0) {
                  close = i;
                  break;
                }
              }
              if (close == std::string::npos) {
                throw Lhd_error{"usage",
                                std::format("formal block '{}': unterminated `{}(` in a property", blk.name, op->name),
                                "temporal operators take one signal, e.g. rose(x) or past(x, 2)"};
              }
              const std::string inner = text.substr(open + 1, close - open - 1);
              const auto        comma = inner.find(',');
              c.pos                   = pos;
              c.close                 = close;
              c.op                    = op;
              c.arg                   = trim(comma == std::string::npos ? inner : inner.substr(0, comma));
              c.second                = comma == std::string::npos ? std::string{} : trim(inner.substr(comma + 1));
              return c;
            }
            return c;
          };

          // Validate one call and report the forward reach it needs (0 for the
          // backward-only forms). Shared by both passes so the two cannot drift.
          auto call_window = [&](const Call& c) -> Window {
            const std::string_view nm = c.op->name;
            if (c.second.empty()) {
              if (c.op->wneed) {
                throw Lhd_error{"usage",
                                std::format("formal block '{}': `{}` needs a bounded window", blk.name, nm),
                                "eventually(x, 1..=32) / always(x, 1..=10) — an unbounded deadline is out of scope"};
              }
              if (nm == "past") {
                throw Lhd_error{"usage",
                                std::format("formal block '{}': `past` takes 2 argument(s)", blk.name),
                                "past(x, 2) samples 2 cycles back; rose/fell/stable/changed take just the signal"};
              }
              return Window{0, 0};  // the unwindowed edge/stability forms
            }
            if (nm == "past") {
              if (c.second.find("..") != std::string::npos) {
                throw Lhd_error{"usage",
                                std::format("formal block '{}': `past` takes a cycle count, not a window", blk.name),
                                "past(x, 2) is one sample; use eventually/always/rose(x, 1..=10) for a window"};
              }
              if (c.second.find_first_not_of("0123456789") != std::string::npos) {
                throw Lhd_error{
                    "usage",
                    std::format("formal block '{}': past() depth '{}' is not a literal cycle count", blk.name, c.second),
                    "write past(x, 2) — the depth must be a compile-time number"};
              }
              return Window{0, 0};  // backward only
            }
            if (c.second.find("..") == std::string::npos) {
              throw Lhd_error{
                  "usage",
                  std::format("formal block '{}': `{}` second argument must be a window, got '{}'", blk.name, nm, c.second),
                  "a bare count is `past`; a window is a range, e.g. rose(x, 1..=10)"};
            }
            return parse_window(c.second, nm);
          };

          for (auto& st : blk_stmts) {
            // PASS 1: the statement's forward reach F.
            int F = 0;
            for (size_t from = 0;;) {
              const Call c = next_call(st.text, from);
              if (c.op == nullptr) {
                break;
              }
              F    = std::max(F, call_window(c).hi);
              from = c.close + 1;
            }
            // PASS 2: rewrite left to right. Temporal calls expand to history
            // ports; every OTHER read of a block signal is shifted by F so the
            // whole statement speaks about the anchor cycle.
            std::string out;
            bool        in_str = false;
            for (size_t pos = 0; pos < st.text.size();) {
              const char ch = st.text[pos];
              if (in_str) {
                out += ch;
                if (ch == '\\' && pos + 1 < st.text.size()) {
                  out += st.text[pos + 1];
                  pos += 2;
                  continue;
                }
                if (ch == '"') {
                  in_str = false;
                }
                ++pos;
                continue;
              }
              if (ch == '"') {
                in_str  = true;
                out    += ch;
                ++pos;
                continue;
              }
              const Call c = next_call(st.text, pos);
              if (c.op != nullptr && c.pos == pos) {
                const auto* base = idx_of(c.arg);
                if (base == nullptr) {
                  throw Lhd_error{"usage",
                                  std::format("formal block '{}': `{}` argument must be one signal the block names, got '{}'",
                                              blk.name,
                                              c.op->name,
                                              c.arg),
                                  "rose(acc.req) / past(acc.req, 2) are supported; an expression like rose(a and b) is not"};
                }
                // COPY the base first: hist_port may push a new history port
                // into blk_inputs, and that reallocation invalidates `base`.
                const livehd::formal_blocks::Input base_copy = *base;
                const std::string_view             nm        = c.op->name;
                // `port(d)` — the signal `d` cycles before the CURRENT unroll
                // cycle, remembering that every term already carries the shift.
                auto                               port      = [&](int d) {
                  const std::string id = hist_port(base_copy, d);
                  if (id != base_copy.ident) {
                    st.idents.push_back(id);
                  }
                  return id;
                };
                std::string repl;
                if (nm == "past") {
                  repl = port(std::stoi(c.second) + F);
                } else {
                  const Window w        = call_window(c);
                  const bool   windowed = !c.second.empty();
                  // Unwindowed forms are the single anchor cycle: offset 0.
                  const int    klo      = windowed ? w.lo : 0;
                  const int    khi      = windowed ? w.hi : 0;
                  // `stable`/`always` are universal over the window, the rest
                  // existential — the doc's OR/AND split.
                  const bool   univ     = (nm == "stable" || nm == "always");
                  std::string  acc;

                  // One sample's truth: a bool signal is its own value, an
                  // integer one is tested against 0.
                  const bool is_bool = is_bool_sig(base_copy.path);
                  auto       high    = [&](int dd) { return is_bool ? port(dd) : "(" + port(dd) + " != 0)"; };
                  auto       low     = [&](int dd) { return is_bool ? "(not " + port(dd) + ")" : "(" + port(dd) + " == 0)"; };
                  for (int k = klo; k <= khi; ++k) {
                    const int   d = F - k;  // >= 0: F is the largest hi in the statement
                    std::string term;
                    if (nm == "rose") {
                      term = std::format("({} and {})", low(d + 1), high(d));
                    } else if (nm == "fell") {
                      term = std::format("({} and {})", high(d + 1), low(d));
                    } else if (nm == "stable") {
                      term = std::format("({} == {})", port(d), port(d + 1));
                    } else if (nm == "changed") {
                      term = std::format("({} != {})", port(d), port(d + 1));
                    } else {  // eventually / always: the value itself
                      term = is_bool ? std::format("({})", port(d)) : high(d);
                    }
                    acc = acc.empty() ? term : std::format("({} {} {})", acc, univ ? "and" : "or", term);
                  }
                  repl = acc;
                }
                out += repl;
                pos  = c.close + 1;
                continue;
              }
              // A bare read of a block signal: shift it to the anchor cycle.
              if (F > 0 && (std::isalpha(static_cast<unsigned char>(ch)) != 0 || ch == '_')) {
                size_t end = pos;
                while (end < st.text.size()
                       && (std::isalnum(static_cast<unsigned char>(st.text[end])) != 0 || st.text[end] == '_'
                           || st.text[end] == '.')) {
                  ++end;
                }
                const std::string ident = st.text.substr(pos, end - pos);
                if (const auto* base = idx_of(ident); base != nullptr) {
                  const livehd::formal_blocks::Input base_copy = *base;
                  const std::string                  id        = hist_port(base_copy, F);
                  if (id != base_copy.ident) {
                    st.idents.push_back(id);
                  }
                  out += id;
                } else {
                  out += ident;
                }
                pos = end;
                continue;
              }
              out += ch;
              ++pos;
            }
            st.text = std::move(out);
          }
        }

        // Port list + widths from the FIRST context (same module def => same
        // widths in every instance); binds built per instance below.
        livehd::lec::Monitor mon;
        mon.block         = blk.name;
        // Assume scope = the authored block. Every instance context below copies
        // it unchanged (only `block` gains the @instance label), so one block's
        // N instances share one assume set while a sibling block never sees it.
        mon.scope         = blk.name;
        // A tuple-typed port arrives DETUPLED: `io_data:(pc:u64, ..)` is carried as
        // the leaf ports `io_data.pc`, .., and a Bind names exactly ONE signal, so
        // a block that reads the tuple PREFIX cannot bind. The generic message
        // ("internal wires .. come later") reads as if the name were an internal
        // wire, which sent every such block looking for a compile bug instead of
        // at the one-line spelling fix; so name the leaves when they exist.
        auto tuple_leaves = [&](const std::string& sig_path) {
          const std::string        pfx = sig_path + ".";
          std::vector<std::string> leaves;
          for (const auto* tbl : {&in_tbl, &out_tbl}) {
            for (const auto& [k, v] : *tbl) {
              if (k.compare(0, pfx.size(), pfx) == 0) {
                leaves.push_back(k);
              }
            }
          }
          std::sort(leaves.begin(), leaves.end());
          return leaves;
        };
        std::string ports;
        for (const auto& in : blk_inputs) {
          livehd::lec::Monitor::Bind b;
          b.ident      = in.ident;
          b.delay      = in.delay;
          const Sig* s = resolve(in.path, inst_prefixes.front(), b);
          if (s == nullptr) {
            const auto  leaves = tuple_leaves(in.path);
            std::string hint;
            if (leaves.empty()) {
              hint
                  = "blocks reach top input/output ports, registers (dotted through instances), and — for a "
                    "submodule-bound block — the target instance's ports; internal wires and memory "
                    "elements come later";
            } else {
              hint = std::format(
                  "'{}' is a TUPLE port, carried as its leaf ports; a block binds one signal per "
                  "read, so name a leaf: {}",
                  in.path,
                  absl::StrJoin(leaves, ", "));
            }
            throw Lhd_error{
                "usage",
                std::format("formal block '{}': signal path '{}' does not resolve in '{}'{}",
                            blk.name,
                            in.path,
                            g->get_name(),
                            inst_prefixes.front().empty() ? std::string{} : " instance '" + inst_prefixes.front() + "'"),
                hint};
          }
          mon.binds.push_back(std::move(b));
          ports += s->boolean ? std::format("{}{}:Bool", ports.empty() ? "" : ", ", in.ident)
                              : std::format("{}{}:{}{}", ports.empty() ? "" : ", ", in.ident, s->sgn ? "S" : "U", s->w);
        }
        // Generated monitor: one statement per line; line 1 is the header, so
        // emitted statement j sits on generated line 2+j (the loc-remap key).
        // Assume forms: `assume_nocheck_synth` is INVISIBLE to verify (a
        // synthesis-only don't-care by fcore contract); `assume_nocheck` (and
        // the fcore spelling `assume_nocheck_formal`) compiles as a plain
        // `assume` (the nocheck spelling is not a builtin) with its generated
        // line recorded so the engine classifies it "unchecked" — a free
        // constraint by user fiat, disclosed in every verdict row. The fcore
        // spelling additionally warns per encounter (its contract); the plain
        // spelling is the SANCTIONED way to state an environment constraint,
        // so it is disclosed but not warned. A plain `assume` is a proof
        // obligation — checked as an assert before it is ever used.
        std::string src      = std::format("comb __fbmon({}) -> (__fb_ok:Bool) {{\n", ports);
        int         gen_line = 2;
        for (const auto& st : blk_stmts) {
          std::string one = st.text;
          std::replace(one.begin(), one.end(), '\n', ' ');  // keep 1 stmt : 1 line for the remap
          std::string callee;
          for (char ch : one) {
            if (std::isalnum(static_cast<unsigned char>(ch)) != 0 || ch == '_') {
              callee += ch;
            } else {
              break;
            }
          }
          if (callee == "assume_nocheck_synth") {
            continue;
          }
          if (callee == "assume_nocheck" || callee == "assume_nocheck_formal") {
            if (callee == "assume_nocheck_formal") {
              livehd::diag::warn("pass.formal", "formal-unchecked-assume", "comptime")
                  .msg("formal block '{}' uses assume_nocheck_formal ({}:{}); verify verdicts are conditional and UNCHECKED",
                       blk.name,
                       bf,
                       st.line)
                  .emit();
            }
            one.replace(0, callee.size(), "assume");
            mon.nocheck_lines.insert(gen_line);
          }
          src                    += one + "\n";
          mon.line2loc[gen_line]  = std::format("{}:{}", bf, st.line);
          ++gen_line;
        }
        src             += "__fb_ok = true\n}\n";
        const auto genp  = fs::path(workdir(opts)) / std::format("__fbmon_{}.prp", gen_ix++);
        {
          std::ofstream gf(genp);
          gf << src;
        }
        // The monitor is an INTERNAL artifact: its assume/assert conds over
        // free comb inputs would trip the compile gate's root-module FAIL
        // policy, but the deep engine below adjudicates every one of its
        // obligations — skip the gate for the monitor compile only (the user's
        // design load above kept the gate's normal policy).
        const size_t saved_sets = opts.sets.size();
        opts.sets.emplace_back("compile.formal.mode", "none");
        Eprp_var mvar;
        try {
          load_side_graphs(opts, res, "pyrope", genp.string(), "impl", mvar);
        } catch (Lhd_error& e) {
          // The monitor is generated source, so its compile diagnostics point
          // at `__fbmon_N.prp` and its `__p_*` ports. Say which block statement
          // failed (the generated line's source loc), in the signal names the
          // block wrote (`__p_io_valid` -> `io.valid`; longest ident first, as
          // one ident can prefix another).
          std::string where;
          const auto  gen_name = genp.filename().string();
          for (const auto& d : std::views::reverse(livehd::diag::sink().records())) {
            if (d.severity != livehd::diag::Severity::error || !d.span.file.ends_with(gen_name) || !d.span.start_line) {
              continue;
            }
            if (const auto it = mon.line2loc.find(static_cast<int>(*d.span.start_line)); it != mon.line2loc.end()) {
              where = std::format(" statement at {}", it->second);
            }
            break;
          }
          std::vector<const livehd::formal_blocks::Input*> named;
          for (const auto& in : blk_inputs) {
            named.push_back(&in);
          }
          std::ranges::sort(named, std::greater{}, [](const auto* in) { return in->ident.size(); });
          for (const auto* in : named) {
            const std::string user = in->delay > 0 ? std::format("past({}, {})", in->path, in->delay) : in->path;
            for (size_t pos = 0; (pos = e.msg.find(in->ident, pos)) != std::string::npos; pos += user.size()) {
              e.msg.replace(pos, in->ident.size(), user);
            }
          }
          e.msg  = std::format("formal block '{}'{}: {}", blk.name, where, e.msg);
          e.hint = std::format("{} (the block compiles into the generated monitor {})", e.hint, genp.string());
          opts.sets.resize(saved_sets);
          throw;
        }
        opts.sets.resize(saved_sets);
        if (mvar.graphs.size() != 1) {
          throw Lhd_error{"internal",
                          std::format("formal block '{}': monitor compile yielded {} modules", blk.name, mvar.graphs.size()),
                          genp.string()};
        }
        mon.graph = mvar.graphs.front().get();
        // A formal-block monitor must be STATELESS. The engine re-encodes it
        // fresh at every cycle and does not thread its outputs into the next
        // state, so any flop inside it is a NEW free symbol each step — which
        // silently refutes tautologies. `past[N](x)` is the way that happens
        // in practice (it lowers to a pipeline stage), so REFUSE rather than
        // emit an unsound verdict. Temporal properties need engine-resolved
        // history (index the unroll), which is not implemented yet.
        for (auto mn : mon.graph->grouped_hierarchy().nodes()) {
          const auto mop = livehd::graph_util::type_op_of(mn);
          if (mop != Ntype_op::Flop && mop != Ntype_op::Fflop && mop != Ntype_op::Latch && mop != Ntype_op::Memory) {
            continue;
          }
          refuse_presolve(livehd::lec::Outcome::Refuted_unsupported,
                          std::format("formal block '{}': the property holds STATE, which is not implemented", blk.name),
                          "a formal-block property must be a combinational function of the signals it names. The "
                          "pipelining `past[N](x)` is a delay stage, not a history sample, and cannot be used here; "
                          "write the history sample `past(x, N)` (or a window operator) instead");
        }
        mon_keep.push_back(std::move(mvar));
        // One Monitor per instance context: the compiled graph is shared, the
        // binds differ, and non-top contexts carry the @instance label.
        for (const auto& prefix : inst_prefixes) {
          livehd::lec::Monitor im = mon;  // graph + line2loc shared; binds rebuilt
          {
            // Embeddable form of each obligation statement for THIS instance
            // context. A plain `assume` IS an obligation (checked as an assert
            // before it is used), so a refuted one is exactly what the replay
            // must re-fire — embed it AS an assert. The `assume_nocheck*`
            // spellings are free constraints by user fiat: never refuted, and
            // re-checking one in the replay would fail by design.
            for (const auto& st : blk_stmts) {
              std::string callee;
              for (char ch : st.text) {
                if ((std::isalnum(static_cast<unsigned char>(ch)) == 0) && ch != '_') {
                  break;
                }
                callee += ch;
              }
              if (callee.rfind("assume_nocheck", 0) == 0) {
                continue;
              }
              std::string t = st.text;
              if (callee == "assume") {
                t.replace(0, callee.size(), "assert");
              }
              // LONGEST ident first: `sanitize` maps '.' to '_', so the binds of
              // `io` and `io.result` are `__p_io` and `__p_io_result` — and
              // blk.inputs arrives sorted ASCENDING by ident. Substituting the
              // short one first rewrites the head of the long one and silently
              // yields `_dut.io_result` where the design has `_dut.io.result`.
              std::vector<const decltype(blk_inputs)::value_type*> bins;
              bins.reserve(blk_inputs.size());
              for (const auto& bin : blk_inputs) {
                bins.push_back(&bin);
              }
              std::sort(bins.begin(), bins.end(), [](const auto* a, const auto* b) {
                return a->ident.size() != b->ident.size() ? a->ident.size() > b->ident.size() : a->ident < b->ident;
              });
              for (const auto* bin : bins) {
                const std::string dut = "_dut." + (prefix.empty() ? bin->path : prefix + "." + bin->path);
                for (size_t pos = 0; (pos = t.find(bin->ident, pos)) != std::string::npos; pos += dut.size()) {
                  t.replace(pos, bin->ident.size(), dut);
                }
              }
              const std::string blabel = prefix.empty() ? blk.name : blk.name + "@" + prefix;
              // A Prop_result identifies its statement by `block` + `file:line`
              // only, so two statements sharing ONE source line (Pyrope's `;`
              // separator) are indistinguishable at lookup time. Embedding
              // whichever won the race would attach the WRONG check to the
              // refuted obligation — a replay that silently tests something else
              // is worse than one that tests nothing. Mark the key ambiguous
              // (empty text) so the generator falls back to no embedded check.
              auto [it, fresh]         = fb_embed.try_emplace(blabel + "\x1f" + std::format("{}:{}", bf, st.line), t);
              if (!fresh && it->second != t) {
                it->second.clear();
              }
            }
          }
          if (!prefix.empty()) {
            im.block = blk.name + "@" + prefix;
            im.binds.clear();
            for (const auto& in : blk_inputs) {
              livehd::lec::Monitor::Bind b;
              b.ident      = in.ident;
              b.delay      = in.delay;  // a history port (`past`, `rose`, a window) reads an EARLIER cycle here too
              const Sig* s = resolve(in.path, prefix, b);
              if (s == nullptr) {
                throw Lhd_error{
                    "usage",
                    std::format("formal block '{}': signal path '{}' does not resolve in instance '{}'", blk.name, in.path, prefix),
                    "submodule-bound blocks reach the instance's registers and its input/output ports"};
              }
              im.binds.push_back(std::move(b));
            }
          }
          mons.push_back(std::move(im));
        }
      }
    }
    // A selector that matches NOTHING must not quietly degrade into "prove the
    // design's own obligations": that run reports PROVEN while the block the
    // user named was never checked. Same contract as `lhd sim <tb> <name>`,
    // which refuses an unknown test rather than running the others.
    if (!opts.formal_filter.empty() && sel_hits == 0) {
      throw Lhd_error{"usage",
                      std::format("no formal block named '{}' in {}", opts.formal_filter, block_src),
                      all_names.empty() ? std::string{"that source declares no `formal` blocks"}
                                        : std::format("available blocks: {}", all_names)};
    }
  }

  livehd::diag::info("pass.formal", "formal-proving", "progress")
      .msg("formal verify: proving obligations of '{}' (bound={}, phase={}, {} formal block(s), {})",
           g->get_name(),
           o.bound,
           o.phase,
           mons.size(),
           o.timeout > 0 ? std::format("timeout={}s per query", o.timeout) : std::string{"no timeout"})
      .emit();
  res.recipe_steps.emplace_back(std::format("pass.lec prove_properties bound:{} phase:{}", o.bound, o.phase));

  if (single_edge_slots > 1 && !mons.empty()) {
    // A formal-block monitor is a separate comb module bound to the design's
    // ports, so its obligation has no access to the phase register and cannot
    // be gated to the period boundary — it would be checked mid-period, where
    // half the design has committed and half has not. Fail closed rather than
    // report a mid-period refutation as a design bug.
    refuse_presolve(livehd::lec::Outcome::Refuted_unsupported,
                    std::format("needed edge normalization (P={}) and also has {} formal block monitor(s), which cannot be "
                                "gated to the period boundary",
                                single_edge_slots,
                                mons.size()),
                    "state the property as a design-body assert (it is gated automatically), or drop the formal block");
  }

  // The proof itself — the dominant phase of `lhd formal verify` (minutes, next
  // to milliseconds of front-end). Named "pass.lec" to match its own recipe line
  // above and the `lhd lec` path: same engine, same bare step name, so the
  // ledger's `formal` row keys on it exactly as the `lec` row does. stop() right
  // after the call rather than at scope end (the verdict reporting below is not
  // proof time); the destructor still records it if prove_properties throws.
  Phase_timer prove_phase(res, "pass.lec");
  auto        r = livehd::lec::prove_properties(g.get(), o, sub_lib_ptr, mons.empty() ? nullptr : &mons);
  prove_phase.stop();
  if (vcache) {
    vcache->save();
    if (vcache->hits() > 0 || vcache->stores() > 0) {
      std::print("formal[cache]: {} obligation hit(s), {} stored ({}/formal_cache.json)\n",
                 vcache->hits(),
                 vcache->stores(),
                 opts.workdir);
    }
  }

  // The run's outcome: its obligation rows folded by severity
  // (verify_top_outcome). The headline, formal_report.json, the warnings and
  // the exit ladder below all read this one Top_outcome, and each row reads
  // prop_top_outcome -- the same words `lhd lec` prints.
  const auto run_outcome = verify_top_outcome(r, engine_forced, single_edge_slots);
  std::print("formal verify: '{}' {} ({}; {} ms)\n", g->get_name(), run_outcome.word, r.detail, r.elapsed_ms);
  record_formal_verdict(res, run_outcome);
  std::string first_fail;                 // the exit policy's headline: the first refuted obligation
  bool        first_fail_assume = false;  // headline is a refuted assume-check, not a design violation
  for (const auto& p : r.props) {
    std::string where = p.loc.empty() ? std::string{} : " at " + p.loc;
    std::string msg   = p.msg.empty() ? std::string{} : " \"" + p.msg + "\"";
    // COI of this property, when the encoder measured it.
    std::string coi = p.cone_nodes < 0 ? std::string{}
                                       : std::format("  [COI {} node(s), {} state]", p.cone_nodes, p.cone_state);
    if (!p.block.empty()) {
      msg += " [" + p.block + "]";  // block (+@instance) attribution
    } else if (!p.instance.empty()) {
      msg += " [" + p.instance + "]";
    }
    // R1 Phase 2 — an obligation whose GUARD can never hold checked nothing:
    // its row is REFUTED-vacuous (a dead guard, final owner ruling 2026-10-14)
    // and says so on the row itself. An UNCHECKED environment assume with a
    // dead guard constrains nothing; that one keeps a continuation note.
    auto vacuity_note = [&p]() {
      if (p.vacuous_guard) {
        std::print(
            "    VACUOUS: its `if`/`match` guard can never be true, so the property is never exercised — the "
            "branch is dead (fix the guard condition, or drop the branch)\n");
      }
    };
    if (p.kind == "assume" && livehd::lec::is_unchecked_assume_class(p.aclass)) {
      const char* why = p.aclass == "check_disabled" ? "formal.assume_check=false; treated as assume_nocheck" : "assume_nocheck";
      std::print("  assume{}{}: in force (UNCHECKED {}; verdicts are conditional and unchecked){}\n", where, msg, why, coi);
      vacuity_note();  // an env assume whose guard never holds constrains nothing
      continue;
    }
    // Asserts AND checked assumes (prove-then-use): the same verdict rows.
    const auto po = prop_top_outcome(p, r, engine_forced, single_edge_slots);
    switch (p.verdict) {
      case livehd::lec::Verdict::Proven:
        if (p.vacuous_guard) {
          std::print("  {}{}{}: {} (dead guard: its `if`/`match` guard can never be true, so nothing was checked; fix the "
                     "guard condition, or drop the branch){}\n",
                     p.kind,
                     where,
                     msg,
                     po.word,
                     coi);
          continue;
        }
        if (p.unbounded) {
          std::print("  {}{}{}: {} (inductive — every cycle of every bound){}\n", p.kind, where, msg, po.word, coi);
        } else if (prop_cut_short(p, r)) {
          std::print("  {}{}{}: {} (proven to cycle {}; not checked past it: the formal.timeout budget ran out before the "
                     "requested cycle {}; raise --set formal.timeout){}\n",
                     p.kind,
                     where,
                     msg,
                     po.word,
                     p.proven_to,
                     verify_last_cycle(r),
                     coi);
        } else {
          std::print("  {}{}{}: {} (no violation through cycle {}){}\n", p.kind, where, msg, po.word, p.proven_to, coi);
        }
        break;
      case livehd::lec::Verdict::Refuted:
        std::print("  {}{}{}: {} at cycle {}{}\n", p.kind, where, msg, po.word, p.refuted_at, coi);
        if (!p.witness.empty()) {
          std::print("    counterexample inputs: {}\n", p.witness);
        }
        if (r.cex_free_box) {
          std::print(
              "    note: the design holds free blackbox outputs, and this trace may rely on a box output value (the "
              "boxed model is the contract)\n");
        }
        if (p.kind == "assume") {
          // The actionable line: an assume is a proof obligation, and the fix
          // differs by WHY it failed — an input-cone constraint can never hold
          // over free inputs (the env-constraint spelling is assume_nocheck),
          // while an internal claim is genuinely broken by the design.
          if (p.aclass == "input") {
            std::print(
                "    an assume is CHECKED as an assert before it is used, and this one constrains only free "
                "primary inputs — nothing forces it to hold. If it is an intended environment constraint, "
                "spell it assume_nocheck(...)\n");
          } else {
            std::print(
                "    an assume is CHECKED as an assert before it is used, and the design refutes this claim — "
                "fix the design or the assume (assume_nocheck(...) would impose it UNCHECKED)\n");
          }
        }
        if (first_fail.empty()) {
          first_fail        = p.kind + where + msg;
          first_fail_assume = p.kind == "assume";
        }
        break;
      default: {
        // An undecided obligation: its word names the cause (TIMEOUT-time,
        // REFUTED-unchecked, ...), and a contradictory assume set is attributed
        // to the SCOPE that owns it, so the reason names the block to fix.
        std::print("  {}{}{}: {} ({}){}\n", p.kind, where, msg, po.word, prop_undecided_why(p, r, /*human=*/true), coi);
        if (!p.witness.empty()) {
          std::print("    candidate violation inputs: {}\n", p.witness);
        }
        break;
      }
    }
    vacuity_note();
  }

  // One-screen tally of the run, in the shape yosys/Jasper print.
  {
    int asserts = 0, covers = 0;
    int proven_unb = 0, proven_bnd = 0, refuted = 0, undet = 0, vacuous = 0;
    int as_unchecked = 0, as_proven = 0, as_refuted = 0, as_unproven = 0;
    for (const auto& p : r.props) {
      if (p.kind == "cover") {
        ++covers;
        continue;
      }
      if (p.kind == "assume") {
        if (livehd::lec::is_unchecked_assume_class(p.aclass)) {
          ++as_unchecked;
        } else if (p.verdict == livehd::lec::Verdict::Proven) {
          ++as_proven;
        } else if (p.verdict == livehd::lec::Verdict::Refuted) {
          ++as_refuted;
        } else {
          ++as_unproven;
        }
        continue;
      }
      ++asserts;
      if (p.vacuous_guard && p.verdict == livehd::lec::Verdict::Proven) {
        ++vacuous;  // never exercised: REFUTED-vacuous, not a proof
        continue;
      }
      if (p.verdict == livehd::lec::Verdict::Proven) {
        p.unbounded ? ++proven_unb : ++proven_bnd;
      } else if (p.verdict == livehd::lec::Verdict::Refuted) {
        ++refuted;
      } else {
        ++undet;
      }
    }
    const int assumes = as_unchecked + as_proven + as_refuted + as_unproven;
    int coi_max = -1, coi_state_max = 0, coi_sum = 0, coi_n = 0, coi_skipped = 0;
    for (const auto& p : r.props) {
      if (p.kind == "assume") {
        continue;
      }
      if (p.cone_nodes < 0) {
        ++coi_skipped;
        continue;
      }
      ++coi_n;
      coi_sum += p.cone_nodes;
      if (p.cone_nodes > coi_max) {
        coi_max       = p.cone_nodes;
        coi_state_max = p.cone_state;
      }
    }
    std::print("\nformal summary: '{}'\n", g->get_name());
    std::print("  properties    {:4}  ({} assert, {} assume, {} cover)\n", asserts + assumes + covers, asserts, assumes, covers);
    std::print("  proven        {:4}  ({} unbounded, {} bounded)\n", proven_unb + proven_bnd, proven_unb, proven_bnd);
    std::print("  failed (cex)  {:4}\n", refuted);
    std::print("  undetermined  {:4}\n", undet);
    std::print("  vacuous       {:4}  (dead guard: never true, checked nothing -- REFUTED-vacuous)\n", vacuous);
    std::print("  assumes       {:4}  ({} unchecked, {} proven, {} refuted, {} unproven)\n",
               assumes,
               as_unchecked,
               as_proven,
               as_refuted,
               as_unproven);
    if (!r.vacuous_scopes.empty()) {
      std::print("  CONTRADICTORY assume set in {} scope(s) — proofs there proved nothing (REFUTED-vacuous)\n",
                 r.vacuous_scopes.size());
    }
    std::print("  depth         {:4}  checked steps after {} reset cycle(s)\n", r.checked_steps, r.reset_hold);
    // A window no deeper than the history a property reads never exercised it.
    if (r.max_history > 0 && r.checked_steps <= r.max_history) {
      std::print("  SHALLOW: {} checked step(s) but a property reads {} cycle(s) back — it was never\n",
                 r.checked_steps,
                 r.max_history);
      std::print("           exercised. Re-run with --set formal.engine=bmc for the full bound.\n");
    }
    if (covers == 0) {
      std::print("  note: cover is not an obligation in lhd yet, so no reachability was measured\n");
    }
    if (coi_n > 0) {
      std::print("  COI           {:4}  largest cone ({} state), {} avg over {} assert(s)\n",
                 coi_max,
                 coi_state_max,
                 coi_sum / coi_n,
                 coi_n);
    }
    if (coi_skipped > 0) {
      std::print("  note: COI not measured for {} sidecar formal-block assert(s)\n", coi_skipped);
    }
  }

  // lhd.stats: the cvc5 solve-insight report for the whole verify run (one
  // solver per strategy, every obligation), printed under the obligation table.
  if (o.stats) {
    livehd::lec::report_cvc5_stats("formal", r.cvc5);
  }

  // simfail simulation tests + VCDs (`--workdir`, shared with LEC): emit one
  // per refuted formal test that carries a trace (the first failed obligation
  // within each test supplies it). A design-body assertion is one implicit test
  // named after the resolved top.
  std::vector<std::string> simfail_artifact_paths;
  if (run_outcome.kind == livehd::lec::Outcome::Refuted_cex) {
    const bool simfail = o.witness && workdir_set && setting_enabled(labels.contains("simfail") ? labels.at("simfail") : "true");
    const bool simfail_run = setting_enabled(labels.contains("simfail_run") ? labels.at("simfail_run") : "true");
    const bool has_trace
        = std::any_of(r.props.begin(), r.props.end(), [](const auto& p) { return p.refuted_at >= 0 && !p.trace.empty(); });
    if (has_trace && simfail && r.cex_free_box) {
      // REFUTED-cex all the same (the boxed model is the contract), but the
      // replay re-emits the source, where a body-less box has no output to
      // drive: a replay that runs clean would read as "spurious".
      livehd::diag::info("pass.formal", "simfail-skip", "io")
          .msg(
              "formal.simfail testbench not generated: the design holds free blackbox outputs and the "
              "counterexample may rely on a box output value, which a source replay cannot drive")
          .emit();
    } else if (has_trace && simfail && single_edge_slots > 1) {
      // 2f-latch M8 step 2d. The replay generator re-emits the UN-NORMALIZED
      // source and embeds the assert at the raw cycle index the engine reported
      // — but after edge normalization that index counts SUB-STEPS, so the
      // testbench would drive the wrong cycle and simply not reproduce. Emitting
      // it anyway is worse than emitting nothing: a replay that runs clean reads
      // as "the counterexample was spurious". Honest skip until the trace is
      // decimated back into periods.
      livehd::diag::info("pass.formal", "simfail-skip", "io")
          .msg(
              "formal.simfail testbench not generated: the design was edge-normalized into {} sub-steps per "
              "clock period, so the witness cycle indices do not line up with the un-normalized source",
              single_edge_slots)
          .emit();
    } else if (simfail) {
      absl::flat_hash_set<std::string> emitted_tests;
      for (const auto& fp : r.props) {
        if (fp.refuted_at < 0 || fp.trace.empty()) {
          continue;
        }
        const std::string subject = fp.scope.empty() ? lecfail_simple_name(g->get_name()) : fp.scope;
        if (!emitted_tests.insert(subject).second) {
          continue;
        }
        std::string embed;
        if (auto it = fb_embed.find(fp.block + "\x1f" + fp.loc); it != fb_embed.end()) {
          embed = it->second;
          if (embed.empty()) {
            // Marked ambiguous above: the source line holds more than one
            // statement, so the refuted obligation cannot be told from its
            // neighbours. Say so — a testbench with no embedded check otherwise
            // reads as "the counterexample was spurious".
            livehd::diag::info("pass.formal", "simfail-embed-ambiguous", "io")
                .msg(
                    "formal.simfail testbench carries no embedded check: {} holds more than one statement of block "
                    "'{}', so the refuted one cannot be identified; put one statement per line to get it back",
                    fp.loc,
                    fp.block)
                .emit();
          }
        }
        const std::string file = simfail_filename(subject);
        simfail_artifact_paths.push_back(opts.workdir + "/" + file);
        emit_formalfail_witness(opts, res, fp, kind, path, std::string(g->get_name()), file, simfail_run, embed);
      }
    }
  }

  // ── formal_report.json (P2): the agent-loop feedback channel ─────────────
  // Written on EVERY run — a TIMEOUT or REFUTED-<why> included (that is the
  // outcome the agent must act on) — BEFORE the exit-policy throws below, so a
  // refuted run still leaves the report. Pass --workdir to keep it across runs; without one, the file
  // lands in the announced scratch dir (one-shot parsing still works).
  {
    std::string rv = label("report", "formal_report.json");
    if (rv == "true" || rv == "1") {
      rv = "formal_report.json";
    }
    if (!rv.empty() && rv != "false" && rv != "0") {
      const std::string                             rpath = rv.find('/') != std::string::npos ? rv : workdir(opts) + "/" + rv;
      absl::flat_hash_map<std::string, std::string> artifacts;
      // Reference only artifacts that actually exist (the witness emit may skip).
      auto                                          add_artifact = [&](std::string_view key, const std::string& p) {
        if (!p.empty() && fs::exists(p)) {
          artifacts[std::string{key}] = p;
        }
      };
      if (!simfail_artifact_paths.empty()) {
        const auto& first_simfail = simfail_artifact_paths.front();
        add_artifact("simfail", first_simfail);
        add_artifact("simfail_json", first_simfail.substr(0, first_simfail.size() - 4) + ".json");
        std::string stem = fs::path(first_simfail).stem().string(), test_name;
        for (char c : stem) {
          test_name += (std::isalnum(static_cast<unsigned char>(c)) != 0) ? c : '_';
        }
        if (test_name.empty() || (std::isdigit(static_cast<unsigned char>(test_name[0])) != 0)) {
          test_name = "simfail_" + test_name;
        }
        add_artifact("vcd", opts.workdir + "/" + test_name + ".vcd");
      }
      add_artifact("cache", opts.workdir + "/formal_cache.json");
      // Mined inductive invariants also land as a paste-ready sidecar block.
      if (!r.mined.empty()) {
        const std::string mpath = workdir(opts) + "/formal_mined.prp";
        emit_mined_block(mpath, path, g->get_name(), r);
        if (fs::exists(mpath)) {
          add_artifact("mined_block", mpath);
          res.outputs.push_back(mpath);
          std::print("formal verify: wrote mined invariants {}\n", mpath);
        }
      }
      emit_formal_report(rpath, path, g->get_name(), r, o, artifacts, run_outcome, engine_forced, single_edge_slots);
      if (fs::exists(rpath)) {
        res.outputs.push_back(rpath);
        std::print("formal verify: wrote report {}\n", rpath);
      }
    }
  }

  // R1 Phase 2 — ANTECEDENT vacuity. An assert (or an internal assume) whose
  // `if`/`match` guard can never be true checked no reachable behavior: its
  // row is REFUTED-vacuous, and a run whose only issue is a dead guard is
  // REFUTED-vacuous, exit 7 (final owner ruling 2026-10-14: a check that proved
  // nothing is a sign of a problem). The list is named in that error; when a
  // worse outcome claims the run (a counterexample, a one-sided interface) it
  // is warned about here instead, so it stays visible.
  std::string vac_list;
  int         n_vac = 0;
  for (const auto& p : r.props) {
    if (!p.vacuous_guard) {
      continue;
    }
    // A non-internal assume whose guard is dead constrains nothing — worth the
    // row, but not the run-level accounting: the compile tier does not count
    // assumes in its vacuity accounting, and the two tiers must agree on
    // whether the same source is clean.
    if (p.kind == "assume" && p.aclass != "internal") {
      continue;
    }
    ++n_vac;
    vac_list += (vac_list.empty() ? "" : ", ") + p.kind + (p.loc.empty() ? std::string{} : " at " + p.loc)
                + (p.msg.empty() ? std::string{} : " \"" + p.msg + "\"");
  }
  const std::string vac_msg = n_vac == 0 ? std::string{}
                                         : std::format("{} obligation(s) checked nothing ({}): the `if`/`match` guard "
                                                       "can never be true (a dead guard), so the property is never exercised",
                                                       n_vac,
                                                       vac_list);
  if (n_vac > 0 && run_outcome.kind != livehd::lec::Outcome::Refuted_vacuous) {
    livehd::diag::warn("pass.formal", "formal-vacuous-guard", "io")
        .msg("formal verify: {} in '{}'", vac_msg, g->get_name())
        .hint("fix the guard condition, or drop the dead branch")
        .emit();
  }
  // A CONTRADICTORY assume scope whose rows are REFUTED-vacuous while a worse
  // row claims the run: warn about it here so the scope stays named.
  if ((r.vacuous || !r.vacuous_scopes.empty()) && run_outcome.kind != livehd::lec::Outcome::Refuted_vacuous) {
    std::string which;
    for (const auto& s : r.vacuous_scopes) {
      which += (which.empty() ? "" : ", ") + (s.empty() ? std::string{"the design itself"} : "block '" + s + "'");
    }
    livehd::diag::warn("pass.formal", "formal-contradictory-assume", "io")
        .msg("formal verify: contradictory assume set in {} of '{}': every obligation under it is REFUTED-vacuous (it "
             "proved nothing and could hide a real counterexample)",
             which.empty() ? std::string{"the design"} : which,
             g->get_name())
        .hint("fix the conflicting assumes (each block is scoped independently, so only the named one needs it)")
        .emit();
  }

  // The exit ladder: every outcome's message says what happened and the hint
  // what to fix; conclude_outcome() maps the outcome to the exit status (the
  // same mapping `lhd lec` uses).
  const std::string name = std::string{g->get_name()};
  std::string       msg;
  std::string       hint;
  switch (run_outcome.kind) {
    case livehd::lec::Outcome::Refuted_vacuous:
      if (r.vacuous || !r.vacuous_scopes.empty()) {
        // A CONTRADICTORY assume set: everything holds under it, so every proof
        // it governs proved nothing and can hide a real counterexample --
        // REFUTED-vacuous, exit 7 (final owner ruling 2026-10-14). Name the
        // scope(s).
        std::string which;
        for (const auto& s : r.vacuous_scopes) {
          which += (which.empty() ? "" : ", ") + (s.empty() ? std::string{"the design itself"} : "block '" + s + "'");
        }
        msg = std::format("'{}': contradictory assume set in {} -- no input satisfies it, so every obligation under it "
                          "proved nothing and could hide a real counterexample",
                          name,
                          which.empty() ? std::string{"the design"} : which);
        if (!vac_msg.empty()) {
          msg += std::format("; also {}", vac_msg);
        }
        hint = "fix the conflicting assume/assume_nocheck statements so at least one input satisfies them all (each block "
               "is scoped independently, so only the named one needs it)";
      } else if (r.nothing_to_check) {
        msg  = std::format("'{}' has no assert obligation, so nothing was checked", name);
        hint = "nothing to check: add an assert/assert_always to the design or a formal block, or select the block that "
               "holds them";
      } else {
        msg  = std::format("{} in '{}'", vac_msg.empty() ? std::string{"an obligation checked nothing (a dead guard)"} : vac_msg, name);
        hint = "fix the `if`/`match` guard condition so it can be true, or drop the dead branch and its assert";
      }
      break;
    case livehd::lec::Outcome::Timeout_time:
      msg  = std::format("'{}' hit a time limit before every obligation was decided; the open ones are neither proved nor "
                         "refuted",
                         name);
      hint = std::format("{}. Raise --set formal.timeout, or prove fewer obligations per run (--formal <block>)", r.detail);
      break;
    case livehd::lec::Outcome::Timeout_memory:
      msg  = std::format("'{}' ran out of memory before every obligation was decided", name);
      hint = std::format("{}. Run on a host with more memory (or a larger LIVEHD_MEMORY_BUDGET_MB), or lower formal.bound",
                         r.detail);
      break;
    case livehd::lec::Outcome::Refuted_cex:
      if (first_fail_assume) {
        // The headline failure is an assume whose CHECK refuted — the design
        // violated nothing; the verification collateral asks for a constraint
        // it cannot justify. Name that, or the user reads "property violation"
        // and hunts a design bug that does not exist.
        msg  = std::format("'{}' has an assume that fails its check ({})", name, first_fail);
        hint = "every assume is CHECKED as an assert before it is used; if this one is an intended free environment "
               "constraint, spell it assume_nocheck(...) — it is then assumed WITHOUT check and disclosed";
      } else {
        msg  = std::format("'{}' has a reachable property violation ({})", name, first_fail);
        hint = r.cex_free_box ? "the per-cycle input trace above reproduces it from reset; it may rely on a free blackbox "
                                "output value (the boxed model is the contract)"
                              : "the per-cycle input trace above reproduces it from reset";
      }
      break;
    case livehd::lec::Outcome::Refuted_unsupported:
      msg  = std::format("'{}' holds an obligation no engine can model or check", name);
      hint = std::format("{}. Nothing more budget can change: change the design feature named above, exclude it "
                         "(formal.ignore_memory, disclosed), or report a LiveHD bug if it should be supported",
                         r.detail);
      break;
    case livehd::lec::Outcome::Refuted_unchecked:
      msg  = std::format("'{}' holds an obligation the selected options cannot decide", name);
      hint = std::format("{}. Raise --set formal.bound when an obligation's window/past() history is deeper than it, or drop "
                         "the pinned formal.engine (engine=auto runs both strategies)",
                         r.detail);
      break;
    case livehd::lec::Outcome::Refuted_unexplained:
      msg  = std::format("'{}' left an obligation undecided with no recognized cause", name);
      hint = std::format("{}. This is a LiveHD bug: please report it with this command and its inputs", r.detail);
      break;
    default: break;
  }
  conclude_outcome(run_outcome, "pass.formal", "formal", msg, hint);
}

// `lhd formal <sub>`: verify (above) | lec (rewritten to the lec command at
// parse time, so it never reaches here).
void formal_command(Options& opts, Result& res) {
  if (opts.files.empty() || opts.files.front() != "verify") {
    throw Lhd_error{"usage",
                    opts.files.empty() ? std::string{"formal needs a subcommand: verify | lec"}
                                       : std::format("unknown formal subcommand '{}'", opts.files.front()),
                    "`lhd formal verify <design>` proves assert/assume by BMC; `lhd formal lec` is the equivalence check"};
  }
  formal_verify_command(opts, res);
}

}  // namespace lhd

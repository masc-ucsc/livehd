//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
// Formal verification, LEC, witness reproduction, and formal-block handling.

#include <fnmatch.h>

#include <algorithm>
#include <array>
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
#include "split_selfref.hpp"  // //graph — repair a self-ref exposed by flattening a comb instance
#include "latch_contract.hpp"
#include "lhd_kernel_internal.hpp"
#include "lnast.hpp"
#include "node_util.hpp"
#include "occurrence_materialize.hpp"
#include "pass.hpp"
#include "pass_single_edge.hpp"
#include "query.hpp"
#include "semdiff.hpp"
#include "solve_stats.hpp"
#include "taskflow/taskflow.hpp"

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
    for (const auto& edge : body->get_output_node().inp_edges()) {
      if (livehd::graph_util::is_graph_input_pin(edge.driver)) {
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

// Load one --impl/--ref side into `var.graphs` WITHOUT cgen. lg: libraries load
// directly; pyrope:/ln: parse/load then lower (upass + tolg + recipe) to
// graphs; verilog: elaborates through --reader — slang (the default: direct
// SV -> LNAST, the pyrope flow) or yosys-slang/yosys-verilog (yosys ->
// LGraphs). The in-process lec engine consumes the graphs directly; the
// lgyosys backend re-emits them through cgen (materialize_verilog).
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
    // Verilog through a yosys reader elaborates straight to LGraphs; every
    // other path (pyrope, ln:, verilog via slang) yields LNAST that lowers
    // through upass + tolg + the recipe.
    const bool yosys_reader = kind == "verilog" && opts.reader != "slang";
    auto       lib_path     = std::format("{}/lec_{}_lgdb", workdir(opts), side);
    if (yosys_reader) {
      check_inputs_exist({path});
      // --top rides RAW to yosys: source module names may contain '.' via
      // escaped identifiers (cgen emits `file.entity` that way).
      Eprp_var::Eprp_dict labels{
          {    "path",                                                                       lib_path},
          {   "files",                                                                           path},
          {     "top",                         opts.top.empty() ? std::string{"-auto-top"} : opts.top},
          {"frontend", opts.reader == "yosys-verilog" ? std::string{"verilog"} : std::string{"slang"}},
      };
      run_step("inou.yosys.tolg", var, labels, opts, res);
    } else {
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
        run_step("inou.slang",
                 var,
                 {
                     {"files", path}
        },
                 opts,
                 res);
      } else {  // ln:
        if (!fs::is_directory(path)) {
          throw Lhd_error{"missing_file", std::format("ln: input not found: {}", path), "an ln: input is a Forest save directory"};
        }
        for (auto& ln : load_ln_dir(path)) {
          var.add(ln);
        }
      }
      lower_lnasts(opts, res, var, lib_path, /*need_graphs=*/true);
      graph_pipeline_and_emits(opts, res, var, lib_path);
    }
  } else {
    throw Lhd_error{"usage",
                    std::format("lec accepts verilog:, lg:, pyrope:, or ln: inputs, got {}:", kind),
                    "a bare .v/.sv/.prp path infers its kind"};
  }
  if (var.graphs.empty()) {
    throw Lhd_error{"config", std::format("lec {} input {} holds no graphs", side, path), ""};
  }
}

// Emit the machine-parseable per-block progress line (info severity: never an
// error or exit-code change) the moment a block resolves, so an agent driving a
// long bottom-up run stream-parses pass/fail/inconclusive instead of waiting for
// the end. Reuses the diag jsonl/pretty rendering; the record carries the block
// name, the verdict, the engine that reached it (the portfolio winner r.engine
// when the auto engine set one, else the requested engine), and the elapsed ms.
static void emit_lec_block_progress(std::string_view block, const livehd::lec::Query_result& r, const livehd::lec::Lec_options& o,
                                    long long elapsed_ms) {
  const char* code;
  const char* verdict;
  switch (r.verdict) {
    case livehd::lec::Verdict::Proven:
      code    = "lec-block-proven";
      verdict = "pass";
      break;
    case livehd::lec::Verdict::Refuted:
      code    = "lec-block-refuted";
      verdict = "fail";
      break;
    default:
      code    = "lec-block-inconclusive";
      verdict = "inconclusive";
      break;
  }
  const std::string eng = r.engine.empty() ? o.engine : r.engine;
  const long long   ms  = r.elapsed_ms >= 0 ? r.elapsed_ms : elapsed_ms;
  auto              b   = livehd::diag::info("pass.lec", code, "progress")
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
  return std::format(
      "{:016x}{:016x}:{:016x}{:016x}|e={};gx={};b={};dc={};st={};ph={};rc={};r={};m=[{}];um=[{}];c=[{}];ac={};da={};a={};sv={}",
      dref.h0,
      dref.h1,
      dimpl.h0,
      dimpl.h1,
      o.engine,
      o.gold_x,
      o.bound,
      o.decompose,
      o.strict ? 1 : 0,
      o.phase,
      o.reset_cycles,
      o.reset,
      sorted_join(match_pairs),
      sorted_join(um_pairs),
      sorted_join(o.collapse),
      o.assume_check ? 1 : 0,
      o.design_assumes ? 1 : 0,
      o.assumption_key,
      o.solver);
}

// Entity tail of a full graph name ("file.entity" -> "entity"): the pair-hint
// key basis on the non-hier path, aligned with the hier driver's entity-canon
// def keys so a design proven either way shares its pair hints.
static std::string lec_entity_of(std::string_view n) {
  auto d = n.rfind('.');
  return std::string(d == std::string_view::npos ? n : n.substr(d + 1));
}

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

// Disclose helper-conditioned lec verdicts. NOTHING in the driver sets these
// counters today: lec no longer consumes formal-block sidecars (user ruling,
// 2026-07-25 — blocks are independent `lhd formal verify` tests, and lec's
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
  if (o.unchecked_assumes > 0) {
    r.detail += r.verdict == livehd::lec::Verdict::Proven
                    ? std::format("; PROVEN under {} unchecked assume(s)", o.unchecked_assumes)
                    : std::format("; under {} unchecked assume(s)", o.unchecked_assumes);
  }
}

// One side's design-authored assume census. `active` is what the encoder will
// actually ASSERT as a miter hypothesis; `undischarged` is the subset that
// became a hypothesis without ever being PROVED, with `undischarged_loc` naming
// the first one (see the refusal in run_lec).
struct Design_assume_census {
  int         active       = 0;
  int         undischarged = 0;
  std::string undischarged_loc;
};

static Design_assume_census design_assume_occurrences(hhds::Graph* top) {
  Design_assume_census census;
  if (top == nullptr) {
    return census;
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
    // Count only what the encoder will actually ASSERT, mirroring
    // pass/lec/encode.cpp's prop_active_assume rule exactly:
    //   * `assume_nocheck` is an environment contract BY SPELLING — active
    //     whether or not pass.formal ran or stamped anything. This deliberately
    //     survives pass.formal RETRACTING a jointly-contradictory set (which
    //     clears `proven`): the pair must still gate this census, or
    //     o.design_assumes goes false and the hierarchy's no-solver
    //     structural-identity shortcut hands out a PROVEN that the UNSAT
    //     hypothesis set should have rejected as CONTRADICTORY.
    //   * a CHECKED `assume` needs the `proven` stamp pass.formal put on it
    //     (a selected-top IO assume, or every assume under assume_check=false).
    //     One that was never discharged — a `lg:` library fed straight to
    //     `lhd lec`, or a side built at O0 where pass.formal does not run —
    //     carries no attribute, is not a hypothesis, and must not be disclosed
    //     as one either.
    if (!nocheck_by_name && !livehd::graph_util::has_proven(node.base_node())) {
      continue;
    }
    ++census.active;
    // pass.formal stamps an assume BOTH `proven` and `runtime_check` in exactly
    // ONE case: a selected-top IO `assume` it could not discharge (see
    // pass_formal.cpp — `proven` publishes the hypothesis, `runtime_check` keeps
    // the still-unproved obligation in the netlist). Every other stamped assume
    // — assume_nocheck, formal.assume_check=false, a genuinely proven one —
    // carries `proven` ALONE, so the pair is an exact "accepted without proof"
    // discriminator on a persisted graph. An `assume_nocheck` is exempt by
    // spelling: it is a sanctioned free contract, never the "undischarged
    // promotion" this refusal exists for — including a retracted contradictory
    // one, whose `runtime_check` stamp must reach pass.lec's CONTRADICTORY
    // rejection rather than trip the wrong refusal here.
    if (!nocheck_by_name && livehd::graph_util::has_runtime_check(node.base_node())) {
      ++census.undischarged;
      if (census.undischarged_loc.empty()) {
        const auto k            = raw.find('\x1f');  // "kind\x1floc\x1fmsg"
        const auto l            = raw.find('\x1f', k + 1);
        census.undischarged_loc = std::string{raw.substr(k + 1, l == std::string_view::npos ? std::string_view::npos : l - k - 1)};
      }
    }
  }
  return census;
}

// Bottom-up hierarchical LEC driver (formal.lec.hier=true). Build the module-def
// dependency DAG over the defs present in both libraries (paired by ENTITY — see
// below), scope it to the picked TOP pair and its transitive descendants (a
// whole-design library may hold many defs unrelated to --ref-top; those are NOT
// proven as extra roots — ruling 2026-07-10), topo-order the subtree
// leaves-first, and LEC each def under the `auto` portfolio. Record the proven
// set; for each parent, force-black-box its PROVEN child instances (--collapse) so
// the parent proof stops re-solving them, while a child NOT provable in isolation
// stays FLATTENED into the parent (descended) — the M5 CEGAR / un-black-box
// fallback, now in v1. Correspondence is name-based (no semdiff needed when the
// call structures match). Each def emits a per-block progress line the instant it
// resolves, so an agent stream-parses the long run. Returns the TOP def's result,
// or — when a descendant REFUTED — that descendant's (see the fail-fast gate in
// run_def and the aggregate below).
// `cvc5_hot` (formal.stats, optional): filled with the top few (def, conflicts)
// pairs, hardest first — the per-def ranking the run-total report appends. The
// summed Cvc5_stats itself rides the returned Query_result's `cvc5` member.
static livehd::lec::Query_result lec_hierarchical(Result& res, Eprp_var& ref_var, Eprp_var& impl_var, const std::string& top_name,
                                                  hhds::Graph* ref_top_g, hhds::Graph* impl_top_g,
                                                  const livehd::lec::Lec_options&                     base,
                                                  const absl::flat_hash_map<hhds::Gid, hhds::Graph*>* sub_lib,
                                                  livehd::formal::Verdict_cache* vcache, bool retry_all, bool fail_fast_refute,
                                                  bool top_down, std::vector<std::pair<std::string, int64_t>>* cvc5_hot = nullptr) {
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
  auto entity_of = [](std::string_view n) -> std::string {
    auto d = n.rfind('.');
    return std::string(d == std::string_view::npos ? n : n.substr(d + 1));
  };
  // formal.lec.trust set: def keys ASSUMED equal without a proof. Matched by the
  // canonical (entity) key the DAG uses, or by either side's full spelling.
  const absl::flat_hash_set<std::string> trust_set(base.trust.begin(), base.trust.end());
  auto                                   is_trusted = [&](std::string_view key) -> bool {
    if (trust_set.empty()) {
      return false;
    }
    return trust_set.count(std::string{key}) > 0 || trust_set.count(entity_of(key)) > 0;
  };
  absl::flat_hash_map<std::string, int> ref_ent_cnt, impl_ent_cnt;
  for (auto& g : ref_var.graphs) {
    if (g) {
      ref_ent_cnt[entity_of(g->get_name())]++;
    }
  }
  for (auto& g : impl_var.graphs) {
    if (g) {
      impl_ent_cnt[entity_of(g->get_name())]++;
    }
  }
  auto canon_ref = [&](std::string_view full) -> std::string {
    auto e  = entity_of(full);
    auto it = ref_ent_cnt.find(e);
    return it != ref_ent_cnt.end() && it->second == 1 ? e : std::string(full);
  };
  auto canon_impl = [&](std::string_view full) -> std::string {
    auto e  = entity_of(full);
    auto it = impl_ent_cnt.find(e);
    return it != impl_ent_cnt.end() && it->second == 1 ? e : std::string(full);
  };
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
  // pairing alone would then never LEC the top pair at all, and an UNKNOWN
  // top exits 0 under the inconclusive-is-a-warning policy — a silent
  // vacuous pass. Force-pair the two explicitly-picked TOP graphs under the
  // ref-top key so the driver always proves/refutes the top itself.
  const std::string top_key = canon_ref(top_name);
  ref_by_name[top_key]      = ref_top_g;
  impl_by_name[top_key]     = impl_top_g;

  // The LEC-able defs are those present on BOTH sides; children[def] = the child
  // def keys it instantiates (taken from the ref-side Subs, canonicalized).
  absl::flat_hash_map<std::string, std::vector<std::string>> children;
  for (auto& [name, g] : ref_by_name) {
    if (impl_by_name.find(name) == impl_by_name.end()) {
      continue;
    }
    absl::flat_hash_set<std::string> seen;
    for (auto node : g->body().nodes(hhds::Node_order::forward)) {
      if (gu::type_op_of(node) != Ntype_op::Sub) {
        continue;
      }
      auto        sio = node.get_subnode_io();
      std::string cn  = canon_ref(sio->get_name());
      if (ref_by_name.find(cn) != ref_by_name.end() && impl_by_name.find(cn) != impl_by_name.end() && !seen.count(cn)) {
        children[name].push_back(cn);
        seen.insert(cn);
      }
    }
  }

  // Topo-order leaves-first (DFS post-order; the in-progress mark guards cycles),
  // rooted at the TOP pair only: `order` is exactly the top and its transitive
  // shared descendants. Defs that merely coexist in the two libraries (a
  // whole-design --emit-dir holds every module, not just the --ref-top subtree)
  // are outside the requested proof and must not become extra roots.
  std::vector<std::string>                order;
  absl::flat_hash_map<std::string, int>   mark;  // 0 unvisited, 1 in-progress, 2 done
  std::function<void(const std::string&)> dfs = [&](const std::string& n) {
    int& m = mark[n];
    if (m != 0) {
      return;  // done, or a cycle back-edge (modules form a DAG)
    }
    m = 1;
    if (auto it = children.find(n); it != children.end()) {
      for (const auto& c : it->second) {
        dfs(c);
      }
    }
    m = 2;
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
        if (auto* r = forest.find(canon(cr.net.get_graph()->get_name()), port)) {
          return *r;
        }
        return pname == canonical_top_name ? port : std::string{};  // a top port IS a root
      }
      if (gu::is_const_pin(cr.net)) {
        return "";
      }
      auto       n  = cr.net.get_master_node();
      const auto op = gu::type_op_of(n);
      if (op == Ntype_op::Clock_cell) {  // a gate DERIVES its reference clock
        return root_of(livehd::latch_contract::sink_driver_hier(n, "clk_ref"), pname, depth + 1);
      }
      if (op == Ntype_op::And || op == Ntype_op::Or) {  // an inline gate, likewise
        for (const auto& e : n.inp_edges()) {
          if (gu::is_const_pin(e.driver)) {
            continue;
          }
          if (std::string r = root_of(e.driver, pname, depth + 1); !r.empty()) {
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
          for (const auto& e : node.inp_edges()) {
            std::string port;
            for (const auto& d : sio->get_input_pin_decls()) {
              if (sio->get_input_port_id(d.name) == e.sink.get_port_id()) {
                port = d.name;
                break;
              }
            }
            const auto cr = livehd::latch_contract::control_root(e.driver);
            if (port.empty() || cr.net.is_invalid()) {
              continue;
            }
            by_net[std::to_string(static_cast<uint64_t>(cr.net.get_class_index().value))].push_back(port);
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
    {
      auto& row = forest.port_root[canonical_top_name];
      for (auto node : top_g->grouped_hierarchy().nodes()) {
        const auto op = gu::type_op_of(node);
        if (op != Ntype_op::Flop && op != Ntype_op::Fflop && op != Ntype_op::Memory && op != Ntype_op::Latch) {
          continue;
        }
        auto       ctrl = livehd::latch_contract::sink_driver_hier(node, op == Ntype_op::Latch ? "enable" : "clock_pin");
        const auto cr   = livehd::latch_contract::control_root(ctrl);
        if (cr.net.is_invalid() || !gu::is_graph_input_pin(cr.net) || cr.net.get_graph() != top_g) {
          continue;
        }
        const std::string port{gu::pin_name_of(cr.net)};
        row[port] = rep(port);
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
      {
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
        for (const auto& e : node.inp_edges()) {
          std::string port;
          for (const auto& d : sio->get_input_pin_decls()) {
            if (sio->get_input_port_id(d.name) == e.sink.get_port_id()) {
              port = d.name;
              break;
            }
          }
          if (port.empty()) {
            continue;
          }
          const std::string r = root_of(e.driver, pname, 0);
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
      // Same implicit-clock rule for every def, once its bound ports are known.
      for (auto& [dname, row] : forest.port_root) {
        std::string other;
        bool        unique = true;
        for (const auto& [pn, r] : row) {
          if (pn.empty() || pn == "clock" || r == "\x03ambiguous") {
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
      if (one && !only_root.empty()) {
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
  // Refuted), for the straggler diagnosis. When `budget_on` (timeout>0, rlimit==0,
  // timeout>0, >1 def), `base.timeout` is a soft TOTAL solver-time budget for
  // the DAG. Parsing, transforms, encoding, and CVC5 term construction do not
  // draw it down. The TOP def is exempt and keeps the full cap. A 1s floor once
  // spent gives a straggler a quick attempt. It is a HINT/target — a bit over
  // or under is fine. Off ⇒ each def
  // keeps the full base.timeout per-query cap (pre-scheduler behavior).
  std::vector<uint8_t>                         settled(order.size(), 0);
  bool                                         budget_on = false;
  std::atomic<long long>                       solve_spent_ms{0};
  std::atomic<int>                             defs_floored{0};  // defs dispatched past the soft total, on the min_timeout floor
  std::atomic<int>                             defs_solved{0};   // defs actually handed to the solver (the "units" of the report)
  livehd::lec::Query_result                    top_result;
  // formal.stats: the RUN total. Every def gets its own cvc5::Solver (several,
  // under the portfolio), so no single Query_result holds the run's effort —
  // fold each def's accounting in as it resolves, under report_mutex.
  livehd::lec::Cvc5_stats                      run_cvc5;
  std::vector<std::pair<std::string, int64_t>> cvc5_by_def;  // (def, conflicts), for the `hot` ranking
  bool                                         have_top = false;
  std::atomic<bool>                            any_oversize{false};  // any def refused by the design-size gate -> hard error
  std::atomic<bool>         any_unsupported{false};                  // any def the ENCODER refused (unmodeled cell) -> hard error
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

  // ── TOP-DOWN bookkeeping (formal.lec.hier_order=top_down, the default) ─────
  // Bottom-up boxes only a child ALREADY proven; top-down boxes EVERY child and
  // discharges the assumption from the same pass's other entries. See the
  // closure below and pass/lec/README.md §1.
  //
  //   assumed[i]   the child keys def i boxed WITHOUT a proof in hand. Empty in
  //                bottom-up (a box there is always a discharged premise).
  //   force_flat[i] children this def must DESCEND rather than box. Seeded by
  //                the escalation rounds: when child C is definitively refuted,
  //                its parent re-proves with C inlined — which is the ONLY step
  //                needed, because the parent's conditional proof already
  //                established that everything else about it matches.
  // A def whose Proven verdict is only BOUNDED. It may NOT discharge a parent's
  // box premise: the sequence-transducer contract a box stands for is
  // explicitly unbounded ("from reset, identical input sequences produce
  // identical output sequences"), and a k-cycle claim does not establish it. A
  // child that diverges at cycle bound+1 would otherwise compose into an
  // unbounded top proof with no caveat on the run's verdict line.
  std::vector<uint8_t>                                       bounded_proof(order.size(), 0);
  std::vector<std::vector<std::string>>                      assumed(order.size());
  std::vector<absl::flat_hash_set<std::string>>              force_flat(order.size());
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
  auto run_def = [&](size_t def_ix) {
    if (settled[def_ix]) {
      return;  // definitively decided in an earlier round — do not re-solve
    }
    const auto& name = order[def_ix];

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

    // Fail-fast on a refuted descendant (formal.lec.hier_refute=fail, the default).
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
      // This def's per-query cap is the solver budget left by completed defs.
      // Concurrent defs may temporarily oversubscribe the soft total, but no
      // parsing, transformation, or representation-building time is charged.
      //   The TOP def is exempt: it is the proof the user actually asked for,
      // and it is scheduled LAST (leaves-first DAG), exactly where a soft
      // total budget has nothing left. Starving it turns the whole run
      // UNKNOWN after every block proved. One full per-query cap for one def
      // keeps the run bounded (~2x timeout worst case), matching the
      // pre-scheduler behavior a standalone --top run gives it.
      const long long spent_s     = solve_spent_ms.load(std::memory_order_relaxed) / 1000;
      const long long remaining_s = static_cast<long long>(base.timeout) - spent_s;
      const long long floor_s     = std::max<long long>(1, base.min_timeout);
      if (remaining_s < floor_s) {
        defs_floored.fetch_add(1);  // this def is running on the floor, past the soft total
      }
      o.timeout = static_cast<int>(std::max<long long>(floor_s, remaining_s));
    }
    if (name != top_key) {
      // Sidecar paths are resolved against the selected impl top. Descendant
      // defs prove their unconditional contracts; only the top miter consumes
      // the accepted impl-side helper facts.
      o.assumptions = nullptr;
      o.assumption_key.clear();
      o.proven_helpers = o.input_assumes = o.unchecked_assumes = 0;
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
    if (auto it = children.find(name); it != children.end()) {
      for (const auto& c : it->second) {
        if (is_trusted(c)) {
          continue;  // already force-boxed via the trust seed above; an assumption, so it never flips kids_proven
        }
        auto       ci        = order_ix.find(c);
        const bool is_proven = ci != order_ix.end() && proven[ci->second] != 0;
        // TOP-DOWN: box the child whether or not it is proven YET. The premise
        // "this child pair is equivalent" is discharged by that child's OWN
        // entry in this same pass — the module DAG is well-founded, so the
        // composition is an induction, not circular reasoning. Bottom-up boxes
        // only what it has already discharged.
        //   `force_flat` overrides: an escalation round descends the ONE child
        // whose refutation this def has to absorb.
        const bool want_box  = (top_down || is_proven) && force_flat[def_ix].count(c) == 0 && !assume_cones.contains(c);
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
            // on which worker finished first (formal.jobs), and the parallel
            // determinism gate compares per-def output verbatim.
            assumed[def_ix].push_back(c);
          }
        } else {
          kids_proven = false;
          // ESCALATION: `c` is being INLINED into this def to absorb its
          // refutation. Only `c` itself has to be expanded — its own children
          // stay BOXED, so the miter grows by one def's logic and not by a whole
          // subtree. (The encoder matches a collapse name hierarchically, so a
          // grandchild named here is boxed even though it is reached through the
          // flattened `c`.) They are premises like any other box, so they join
          // `assumed` and the closure has to discharge them too.
          if (top_down && force_flat[def_ix].count(c) > 0) {
            if (auto gk = children.find(c); gk != children.end()) {
              for (const auto& g : gk->second) {
                auto gi = order_ix.find(g);
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
      if (auto hit = vcache->lookup(ckey); hit.has_value()) {
        livehd::lec::Query_result cr;
        cr.verdict       = Verdict::Proven;
        cr.engine        = "cache";
        cr.elapsed_ms    = 0;
        cr.detail        = std::format("verdict cache hit (was {} in {}ms: {})", hit->engine, hit->elapsed_ms, hit->detail);
        proven[def_ix]   = 1;
        by_cache[def_ix] = 1;
        ++cache_count;
        std::lock_guard report_lock(report_mutex);
        emit_lec_block_progress(name, cr, o, 0);
        std::print("lec[hier]: '{}' PROVEN (cache)\n", name);
        if ((name == top_key)) {
          top_result = cr;
          have_top   = true;
        }
        return true;  // no analysis at all for this def
      }
      // Unknown-attempt ledger (ruling 2026-07-10): an unchanged def that
      // already came back Unknown at this (or a larger) budget skips the
      // re-grind — it still REPORTS inconclusive, exactly as a re-run would;
      // no verdict is transferred. A digest/option change, a larger
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
      if (!retry_all && vcache->skip_unknown(ckey, base.timeout)) {
        livehd::lec::Query_result ur;
        ur.verdict    = Verdict::Unknown;
        ur.engine     = "cache-skip";
        ur.elapsed_ms = 0;
        ur.detail
            = std::format("known inconclusive at timeout<={}s with unchanged digest/options; --set formal.retry=all re-attempts",
                          o.timeout);
        std::lock_guard report_lock(report_mutex);
        emit_lec_block_progress(name, ur, o, 0);
        std::print("lec[hier]: '{}' UNKNOWN (skipped: known inconclusive; formal.retry=all re-attempts)\n", name);
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

    // M3 structural def-diff reduction: a def whose ref/impl are structurally
    // IDENTICAL (no unmatched node on either side) and whose children are ALL
    // proven is equivalent with NO solver call. A parent's own-structure match
    // does NOT cover a child's internals (the child Sub matches by name regardless),
    // so require the children proven first — leaves-first guarantees they are settled.
    // The same structural_match call doubles as the tier-2 producer
    // (state_pairing): its full-match signature pass pairs the state cells
    // tier-1 names left unmatched, and the surviving pairs are injected as
    // UNCERTAIN correspondence (2f-lec discipline enforced inside prove_equal).
    const bool want_pairing    = o.state_pairing && !pairs_from_hint;
    auto       has_direct_loop = [](hhds::Graph* graph) {
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
      so.alg                        = o.semdiff == "none" ? "structural" : o.semdiff;
      so.matching_names             = true;  // anchor flops/mems by hier name (lec's correspondence basis)
      so.state_pairing              = want_pairing;
      so.seed_pairs                 = o.match;  // explicit formal.lec.match pairs are tier-1 anchors for the signatures
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
        virtual_loop_fold        = livehd::semdiff::folded_loop_identical(compact, unrolled);
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
          vcache->insert(ckey, {sr.engine, sr.detail, ms});
        }
        if (name == top_key) {
          top_result = sr;
          have_top   = true;
        }
        return;
      }
      auto            m  = livehd::semdiff::structural_match(ref_by_name[name], impl_by_name[name], so);
      const long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
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
      if (o.semdiff != "none" && kids_proven && !o.design_assumes && livehd::semdiff::is_structural_identity(m)) {
        livehd::lec::Query_result sr;
        sr.verdict         = Verdict::Proven;
        sr.engine          = "semdiff";
        sr.elapsed_ms      = ms;
        sr.detail          = std::format("structurally identical ({}: {} matched node(s), no solver call)", so.alg, m.a_matched);
        proven[def_ix]     = 1;
        by_semdiff[def_ix] = 1;
        ++semdiff_count;
        {
          std::lock_guard report_lock(report_mutex);
          emit_lec_block_progress(name, sr, o, ms);
          std::print("lec[hier]: '{}' MATCHED (semdiff {}, no solver)\n", name, so.alg);
        }
        if (vcache != nullptr && !ckey.empty()) {
          vcache->insert(ckey, {sr.engine, sr.detail, ms});  // a structural match is a definitive Proven
        }
        if ((name == top_key)) {
          top_result = sr;
          have_top   = true;
        }
        return;  // skip the solver for this def
      }
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
    ensure_forests();
    o.clock_forest = ref_forest;
    for (const auto& [dname, row] : impl_forest.port_root) {
      auto& dst = o.clock_forest.port_root[dname];
      for (const auto& [pn, r] : row) {
        dst.emplace(pn, r);
      }
    }
    auto            t0 = std::chrono::steady_clock::now();
    auto            r  = order.size() == 1 ? livehd::lec::prove_equal(ref_by_name[name], impl_by_name[name], o, sub_lib)
                                           : livehd::lec::prove_equal_isolated(ref_by_name[name], impl_by_name[name], o, sub_lib);
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
    //    (a) CHASING A CEX (`force_flat` non-empty = an escalation round absorbing
    //        a child's known refutation). Here the flat miter is how the
    //        counterexample is found at all: dino PipelinedDualIssueCPU stays
    //        UNKNOWN collapsed and REFUTES flat.
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
    const bool      cheap_unknown          = r.elapsed_ms >= 0 && r.elapsed_ms < cheap_ms;
    const bool      unknown_under_collapse
        = r.verdict == Verdict::Unknown && !coll.empty() && !r.oversize_refused && (!force_flat[def_ix].empty() || cheap_unknown);
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
    const bool proven_absorbing = r.verdict == Verdict::Proven && !coll.empty() && !force_flat[def_ix].empty();
    bool       absorb_demoted   = false;  // set when that PROVEN is rejected: nothing may restore it
    if (refuted_under_collapse || unknown_under_collapse || proven_absorbing) {
      {
        std::lock_guard report_lock(report_mutex);
        std::print("lec[hier]: '{}' {} under collapse ({} box def(s)) -> flat {}\n",
                   name,
                   refuted_under_collapse ? "REFUTED" : (proven_absorbing ? "PROVEN while absorbing a refutation" : "UNKNOWN"),
                   coll.size(),
                   refuted_under_collapse ? "confirmation"
                   : proven_absorbing     ? "confirmation (a collapsed proof may not overrule a child's counterexample)"
                                          : "retry (UF boxes disable the eager bit-blaster)");
      }
      livehd::lec::Lec_options oflat = o;
      // Drop the SPECULATIVE proven-child boxes being confirmed, but KEEP the
      // TRUSTED boxes: they cover a cell the encoder cannot model (a Latch), so
      // re-flattening them would refuse the whole miter (exit 7) and destroy the
      // real counterexample this confirm is meant to validate. Only when the
      // trust list is empty is this the original full clear.
      oflat.collapse.assign(base.trust.begin(), base.trust.end());
      auto rf = order.size() == 1 ? livehd::lec::prove_equal(ref_by_name[name], impl_by_name[name], oflat, sub_lib)
                                  : livehd::lec::prove_equal_isolated(ref_by_name[name], impl_by_name[name], oflat, sub_lib);
      // The collapsed run really ran cvc5, so its effort is part of what this def
      // cost: carry it into the survivor BEFORE the move discards `r` (formal.stats).
      if (refuted_under_collapse) {
        rf.detail      = "flat-confirm after collapsed-box REFUTE" + std::string(rf.detail.empty() ? "" : "; ") + rf.detail
                         + (r.detail.empty() ? "" : " (collapsed run: " + r.detail + ")");
        rf.elapsed_ms  = -1;  // the progress record carries the combined wall-clock below
        rf.cvc5       += r.cvc5;
        r              = std::move(rf);
      } else if (proven_absorbing) {
        // The flat run is the authority here. If it settles, adopt it either way;
        // if it cannot, the collapsed PROVEN must NOT stand — a child's
        // counterexample is on the table and nothing discharged it, which is the
        // definition of inconclusive.
        if (rf.verdict != Verdict::Unknown) {
          rf.detail = "flat-confirm after collapsed-box PROVEN absorbing a refutation" + std::string(rf.detail.empty() ? "" : "; ")
                      + rf.detail + (r.detail.empty() ? "" : " (collapsed run: " + r.detail + ")");
          rf.elapsed_ms  = -1;
          rf.cvc5       += r.cvc5;
          r              = std::move(rf);
        } else {
          r.verdict       = Verdict::Unknown;
          r.detail        = "a collapsed proof absorbing a child's REFUTED block could not be confirmed flat"
                            + std::string(r.detail.empty() ? "" : "; collapsed run: ") + r.detail;
          r.cvc5         += rf.cvc5;
          absorb_demoted  = true;  // see the int_blast_retry guard below
        }
      } else if (rf.verdict != Verdict::Unknown) {
        rf.detail      = "flat-retry after collapsed-box UNKNOWN" + std::string(rf.detail.empty() ? "" : "; ") + rf.detail
                         + (r.detail.empty() ? "" : " (collapsed run was inconclusive: " + r.detail + ")");
        rf.elapsed_ms  = -1;
        rf.cvc5       += r.cvc5;
        r              = std::move(rf);
      } else {
        r.detail += "; flat retry (collapse cleared) also inconclusive";
        r.cvc5   += rf.cvc5;  // here `rf` is the discarded side; the effort was still spent
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
    // A refute that turns on a TRUSTED box input is not a sound disproof (the
    // trusted leaf may ignore that input): degrade it to Unknown, keeping the
    // witness for diagnosis. Under strict (and any witness-carrying Unknown) this
    // is still a hard fail — just an honest "inconclusive at a trusted boundary",
    // not a false "not equivalent".
    if (r.verdict == Verdict::Refuted) {
      if (std::string tb = lec_refute_trusted_box(r.witness, trust_set); !tb.empty()) {
        r.verdict = Verdict::Unknown;
        r.detail  = std::format(
            "INCONCLUSIVE: refuted only at trusted-box input (bbin:{}) — a trust box asserts every leaf input "
            "equal incl. functional don't-cares, and the trusted leaf cannot be flattened to tell them apart, so "
            "this is not a disproof; {}",
            tb,
            r.detail);
      }
    }
    disclose_lec_helpers(r, o);
    const long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    if (budget_on) {
      solve_spent_ms += r.solve_ms;
      defs_solved.fetch_add(1);
    }
    // formal.stats run total. OUTSIDE the budget_on gate (accounting is on iff
    // timeout>0 && rlimit==0, which has nothing to do with stats), and under
    // report_mutex: the taskflow executor runs run_def concurrently under
    // formal.jobs, so both the sum and the by-def vector would otherwise race.
    // Empty is the common no-solver case (cache / semdiff / abc settled it).
    if (base.stats && !r.cvc5.empty()) {
      std::lock_guard report_lock(report_mutex);
      run_cvc5 += r.cvc5;
      cvc5_by_def.emplace_back(name, r.cvc5.conflicts);
    }
    lec_store_cones(vcache, r);
    if (r.verdict == Verdict::Proven) {
      proven[def_ix]        = 1;
      bounded_proof[def_ix] = r.bounded ? 1 : 0;
      if (vcache != nullptr) {
        if (!ckey.empty()) {
          vcache->insert(ckey, {r.engine, r.detail, ms});  // definitive Proven only (rule F; v1 skips Refuted)
        }
        // Strategy hint keyed by entity NAME so it survives the design edit
        // that misses the digest-keyed verdict cache.
        vcache->set_hint(name, {r.engine, r.split_used, ms});
        // A PASS obtained WITH uncertain pairs validates them: persist as the
        // entity-keyed pair hint so warm runs inject them without re-running
        // the signature pass.
        lec_store_pair_hint(vcache, name, r.uncertain_pairs_used);
      }
    } else {
      if (r.verdict == Verdict::Unknown && r.witness.empty() && vcache != nullptr && !ckey.empty()) {
        // Ledger the attempt (NOT a verdict): an unchanged re-run at no more
        // budget skips this def instead of re-burning the full solver timeout.
        // Witness-CARRYING Unknowns are excluded: a partial-miter diff is a
        // potential discrepancy the exit policy escalates (hard fail) — it must
        // re-surface on every run, never be skipped.
        vcache->note_unknown(ckey, {base.timeout, ms});  // the RUN's budget; see skip_unknown above
      }
      if (pairs_from_hint && vcache != nullptr) {
        // Self-heal: the replayed pair hint validated but its solve did NOT
        // end Proven — it is stale (a crossed/outdated pairing that would
        // otherwise suppress the fresh signature pass forever). Drop it so
        // the next run re-derives the pairing fresh.
        vcache->clear_pair_hint(name);
      }
    }
    // Scheduler bookkeeping: a Proven or Refuted verdict is DEFINITIVE, so later
    // rounds skip this def; an Unknown stays unsettled and is re-tried with a
    // bigger slice next round (the verdict-cache Unknown ledger, keyed by budget,
    // makes that a genuine re-attempt rather than a cache skip).
    if (r.verdict == Verdict::Proven || r.verdict == Verdict::Refuted) {
      settled[def_ix] = 1;
    }
    if (r.oversize_refused) {
      any_oversize.store(true);
    }
    if (r.unsupported) {
      any_unsupported.store(true);
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
      emit_lec_block_progress(name, r, o, ms);
      std::print("lec[hier]: '{}' {} ({} child collapse{})\n",
                 name,
                 r.verdict == Verdict::Proven ? "PROVEN" : (r.verdict == Verdict::Refuted ? "REFUTED" : "UNKNOWN"),
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
  // `base.timeout` is a soft TOTAL solver-time budget for the DAG: each def's
  // per-query cap becomes the solver time remaining after completed defs
  // (run_def, above), so translation and CVC5 representation construction are
  // excluded. The TOP def keeps the full per-query cap (it is
  // the requested proof and runs last — see run_def). Fast defs (the common
  // case) finish well under budget and still see the full cap, so a design
  // that fits is never regressed; only a run that would out-solve `timeout`
  // is reined in. Off — the full per-query cap, byte-identical to before —
  // for the deterministic rlimit tier, an unbounded budget (timeout==0), or a
  // lone def. A single DAG pass: the verdict cache / Unknown-ledger and per-def
  // progress stay exactly as before.
  budget_on = base.timeout > 0 && base.rlimit == 0 && order.size() > 1;
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
  // strictly better answer than bottom-up's, which flattens the failing child
  // into every ancestor and grows the miter on the way up. Here the ONE def
  // that has to absorb the failure is the refuting child's immediate PARENT:
  // re-prove the parent with that child INLINED and everything else still
  // boxed. If the parent proves, its own boundary behaviour is intact, and
  // since every ancestor already proved with the parent BOXED, the whole chain
  // closes — there is nothing to check higher up (user ruling 2026-08-02).
  // Only if the parent itself refutes does the escalation move up a level.
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
      if (proven[i] == 0) {
        continue;
      }
      bool all_kids = true;
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
        // honest: the top ends up bounded, and the bounded-pass policy decides
        // whether that is a pass. The alternative, silently treating a k-cycle
        // child as an unbounded sequence-transducer contract, is the hole.
        if (bounded_proof[ci->second] != 0) {
          bounded_proof[i] = 1;
        }
      }
      unconditional[i] = all_kids ? 1 : 0;
    }
  };
  if (top_down) {
    compute_closure();
    const size_t top_ix = order_ix.at(top_key);
    // Escalation rounds. Each round picks the refuted defs whose parent boxed
    // them, inlines exactly those into their parents, and re-proves ONLY those
    // parents. Bounded by the hierarchy depth: a round that changes nothing
    // stops. The top itself has no parent to escalate into — its own flat
    // confirm (inside run_def) is already the last word.
    for (size_t round = 0; round < order.size() && unconditional[top_ix] == 0; ++round) {
      std::vector<size_t>         retry;
      absl::flat_hash_set<size_t> retry_set;
      for (size_t i = 0; i < order.size(); ++i) {
        if (refuted[i] == 0 || order[i] == top_key) {
          continue;
        }
        for (const auto& p : parents[order[i]]) {
          auto pi = order_ix.find(p);
          if (pi == order_ix.end()) {
            continue;
          }
          // Already absorbed in an earlier round, or the parent never boxed it.
          if (force_flat[pi->second].count(order[i]) > 0) {
            continue;
          }
          force_flat[pi->second].insert(order[i]);
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
        std::print("lec[hier]: ESCALATE '{}' — re-proving with {} refuted child(ren) INLINED, every other child still boxed\n",
                   order[i],
                   force_flat[i].size());
        settled[i] = 0;  // re-solve this one def
        proven[i]  = 0;
        refuted[i] = 0;
      }
      dispatch_dag(&retry);
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
        if (pi != order_ix.end() && force_flat[pi->second].count(order[i]) > 0 && proven[pi->second] != 0) {
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
    // A top proven only CONDITIONALLY is not a proof of the top. Degrade it to
    // Unknown naming the undischarged premise: PASS must mean definitively
    // equivalent, and an assumption the run never discharged is exactly the
    // "inconclusive" bucket, never a pass (see the verdict-discipline contract
    // in pass/lec/tests/lec_verdict_policy_test.sh).
    if (have_top && top_result.verdict == Verdict::Proven && unconditional[top_ix] == 0 && !open_premise_free(top_ix)) {
      std::string open_premises;
      int         n = 0;
      for (size_t i = 0; i < order.size(); ++i) {
        if (unconditional[i] == 0 && order[i] != top_key && !parents[order[i]].empty()) {
          open_premises += (n++ ? ", " : "") + order[i];
          if (n >= 8) {
            open_premises += ", ...";
            break;
          }
        }
      }
      top_result.verdict = Verdict::Unknown;
      top_result.detail  = std::format(
          "INCONCLUSIVE: '{}' proved with every child BOXED, but {} premise(s) never discharged ({}) — the top's own "
          "logic matches, so the difference is inside those block(s); {}",
          top_key,
          n,
          open_premises,
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
    const long long spent_s = solve_spent_ms.load(std::memory_order_relaxed) / 1000;
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

  // A REFUTED def anywhere in the hierarchy is the run's verdict unless the TOP
  // itself settled. Without this the driver returns top_result alone, so a block
  // the solver refuted outright is dropped on the floor: the top is skipped
  // (fail-fast) or — since a non-collapsible child forces a whole-design flat
  // miter — comes back UNKNOWN, and a witness-free UNKNOWN exits 0 under the
  // inconclusive-is-a-warning policy. That reported a design with a known
  // counterexample as a PASS.
  //   A top that PROVED outranks it (escalate mode's whole point: the child's
  // block-boundary CEX was unreachable in context), and a top that REFUTED
  // already carries a more direct counterexample.
  if (have_refuted && (!have_top || (top_result.verdict != Verdict::Proven && top_result.verdict != Verdict::Refuted))) {
    const bool skipped = !have_top;
    top_result         = refuted_result;
    top_result.detail  = std::format("hierarchical: block '{}' REFUTED{}; {}",
                                     refuted_def,
                                     skipped ? "" : " (top itself inconclusive)",
                                     top_result.detail);
    have_top           = true;
  }
  if (have_top && top_result.verdict == Verdict::Proven && top_down) {
    if (auto ti = order_ix.find(top_key); ti != order_ix.end() && bounded_proof[ti->second] != 0) {
      top_result.bounded = true;  // some premise in the tree was only bounded
    }
  }
  if (!have_top) {
    top_result.verdict          = Verdict::Unknown;
    top_result.detail           = std::format("hierarchical: top '{}' not found in both libraries", top_name);
    // The requested top never got compared, so this run decided nothing about it.
    // Without the flag it degrades to the tolerated exit-0 inconclusive warning,
    // i.e. "the module you asked me to check does not exist on both sides" is
    // indistinguishable from success to any gate reading the exit code.
    top_result.nothing_compared = true;
  }
  // A size refusal on ANY def (not just the top) is a hard admission failure for
  // the whole run, so surface it on the aggregate regardless of which def hit it.
  if (any_oversize.load()) {
    top_result.oversize_refused = true;
  }
  // Same reasoning for an ENCODER refusal on any def: that def was never
  // compared, so the hierarchical proof has a hole. Surface it on the aggregate
  // so the CLI hard-fails instead of reporting a clean inconclusive (M0).
  if (any_unsupported.load()) {
    top_result.unsupported = true;
  }
  // formal.stats: the run total, ASSIGNED not accumulated — top_result is a copy
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
struct Lecfail_mod {
  std::string                                      name;
  std::string                                      text;     // full module source
  std::vector<std::pair<std::string, std::string>> inputs;   // {name, ":type" suffix or ""}
  std::vector<std::pair<std::string, std::string>> outputs;  // {name, ":type@[..]" suffix or ""}
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

// Split a comma-separated IO list ("en, din:u8") into {name, ":type" suffix}.
void lecfail_parse_io(std::string_view list, std::vector<std::pair<std::string, std::string>>& out) {
  size_t i = 0;
  while (i <= list.size()) {
    size_t           c    = list.find(',', i);
    std::string_view item = list.substr(i, (c == std::string_view::npos ? list.size() : c) - i);
    size_t           b    = item.find_first_not_of(" \t\r\n");
    size_t           e    = item.find_last_not_of(" \t\r\n");
    if (b != std::string_view::npos) {
      item         = item.substr(b, e - b + 1);
      size_t colon = item.find(':');
      if (colon == std::string_view::npos) {
        out.emplace_back(std::string(item), std::string{});
      } else {
        out.emplace_back(std::string(item.substr(0, colon)), std::string(item.substr(colon)));
      }
    }
    if (c == std::string_view::npos) {
      break;
    }
    i = c + 1;
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
// from a module's source. The attribute block and types carry no parens, so the
// first '(' after the name is the input list. Returns false if no lambda header
// is present (e.g. an empty file-level unit).
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
  size_t io = text.find('(', p);
  if (io == std::string_view::npos) {
    return false;
  }
  size_t ic = text.find(')', io);
  if (ic == std::string_view::npos) {
    return false;
  }
  size_t body  = text.find('{', ic);
  size_t arrow = text.find("->", ic);
  if (arrow != std::string_view::npos && (body == std::string_view::npos || arrow < body)) {
    size_t oo = text.find('(', arrow);
    size_t oc = oo == std::string_view::npos ? std::string_view::npos : text.find(')', oo);
    if (oo != std::string_view::npos && oc != std::string_view::npos && (body == std::string_view::npos || oc < body)) {
      lecfail_parse_io(text.substr(oo + 1, oc - oo - 1), m.outputs);
    }
  }
  lecfail_parse_io(text.substr(io + 1, ic - io - 1), m.inputs);
  return true;
}

// Read every *.prp in `dir` and parse each lambda header. pass.prp_writer emits
// one file per SOURCE FILE, so a file can hold SEVERAL lambdas (`<file>.prp`
// carries the file scope plus every `pub mod` lifted out of it) — parse them
// all, or a top that is not the file's first lambda goes missing and the
// simfail testbench is silently skipped. Each module's `text` is its own slice
// plus the file prologue (the imports it may need).
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
    const std::string text = ss.str();

    // Start of every lambda header LINE, in order.
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
      continue;  // a file-level unit with no lambda (e.g. a package)
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
  size_t io = text.find('(', mp);
  size_t ic = io == std::string::npos ? std::string::npos : text.find(')', io);
  if (io == std::string::npos || ic == std::string::npos || ic <= io + 1) {
    return text;  // no header params
  }
  const std::string params = text.substr(io + 1, ic - io - 1);
  std::string       rebuilt;
  bool              changed = false;
  size_t            i       = 0;
  while (i <= params.size()) {
    size_t      c   = params.find(',', i);
    std::string raw = params.substr(i, (c == std::string::npos ? params.size() : c) - i);
    size_t      b   = raw.find_first_not_of(" \t");
    size_t      e   = raw.find_last_not_of(" \t");
    if (b != std::string::npos) {
      std::string name = raw.substr(b, e - b + 1);
      if (name.find(':') == std::string::npos) {  // untyped
        if (auto it = width_of.find(name); it != width_of.end()) {
          raw     = name + std::format(":u{}", it->second);
          changed = true;
        }
      }
    }
    rebuilt += (rebuilt.empty() ? "" : ", ") + raw;
    if (c == std::string::npos) {
      break;
    }
    i = c + 1;
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
  // Test name = the .prp basename stem, sanitized to a Pyrope identifier; it is
  // also the sole sim instance's VCD stem (`<workdir>/<stem>.vcd`).
  std::string       stem         = fs::path(simfail_path).stem().string();
  std::string       test_name;
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

  const std::string lhd_bin  = file_utils::get_exe_path() + "/lhd";
  const std::string impl_dir = opts.workdir + "/lecfail_impl_prp";
  const std::string ref_dir  = opts.workdir + "/lecfail_ref_prp";
  const std::string log      = next_log_path(opts, "formal.simfail");
  if (!lecfail_emit_side(lhd_bin, opts, opts.impl_kind, opts.impl_path, impl_dir, opts.workdir + "/lecfail_impl_w", log)
      || !lecfail_emit_side(lhd_bin, opts, opts.ref_kind, opts.ref_path, ref_dir, opts.workdir + "/lecfail_ref_w", log)) {
    skip(std::format("a side could not be re-emitted as Pyrope (lg:/yosys-verilog sides have no LNAST); see {}", log));
    return;
  }

  std::vector<Lecfail_mod> impl_mods = lecfail_parse_dir(impl_dir);
  std::vector<Lecfail_mod> ref_mods  = lecfail_parse_dir(ref_dir);
  if (impl_mods.empty() || ref_mods.empty()) {
    skip("no Pyrope modules were re-emitted for a side");
    return;
  }

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
  // form — `import("<file>.<top>")` needs the top to be `pub`.
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
  auto wtype = [&](const std::string& n) {
    auto it = width_of.find(n);
    return std::format(":u{}", it == width_of.end() ? 1 : it->second);
  };

  // Union of the two tops' declared inputs (order: impl first, then ref extras).
  std::vector<std::string>         win;
  absl::flat_hash_set<std::string> seen;
  for (const auto& [n, t] : impl_m->inputs) {
    if (seen.insert(n).second) {
      win.push_back(n);
    }
  }
  for (const auto& [n, t] : ref_m->inputs) {
    if (seen.insert(n).second) {
      win.push_back(n);
    }
  }

  // ---- build the wrapper module -------------------------------------------
  std::string sig_in;
  for (const auto& n : win) {
    sig_in += (sig_in.empty() ? "" : ", ") + n + wtype(n);
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
  auto call_args = [](const Lecfail_mod* m) {
    std::string a;
    for (const auto& [n, t] : m->inputs) {
      a += (a.empty() ? "" : ", ") + std::format("{} = {}", n, n);
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
  const bool reset_is_port  = std::find(win.begin(), win.end(), "reset") != win.end();
  const bool implicit_reset = width_of.count("reset") != 0 && !reset_is_port;

  std::string test_text = std::format("test {} {{\n  mut _lec_dut = {}\n", test_name, wrapper);
  for (const auto& n : win) {
    std::string arr;
    for (int c = 0; c < ncyc; ++c) {
      arr += (arr.empty() ? "" : ", ") + val_at(n, c);
    }
    test_text += std::format("  const _drv_{} = [{}]\n", n, arr);
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
    test_text += "    _lec_dut.reset = _drv_reset[clock]\n";
  }
  for (const auto& n : win) {
    test_text += std::format("    _lec_dut.{} = _drv_{}[clock]\n", n, n);
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

  // F7: machine-readable sibling artifact, keyed off the same trace (so its input
  // sequence matches the .prp `_drv_*` arrays by construction).
  std::string json_path = simfail_path.substr(0, simfail_path.size() - 4) + ".json";
  emit_witness_json(json_path, "simfail", opts.impl_path, opts.ref_path, r.trace);
  res.outputs.push_back(json_path);

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
  cmd += shell_quote(simfail_path) + " --set sim.vcd=true --workdir " + shell_quote(opts.workdir);
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

// Bring every INTEGRATED CLOCK GATE into a body the analyses can see, across
// the top AND each def the encoder will meet, and fold the defs that gained one.
// Returns {cells inlined, defs folded}.
//
// Inlining the top alone holds only for a design that instantiates its gate AT
// the top. A real one puts it further down (minion instantiates `prim_clk_gate`
// inside `minion_dcache_reduce`, `txfma_top`, `vpu_trans` and 8 more), and there
// the top body holds no cell at all -- so nothing was inlined, every gated flop
// kept an opaque `Sub` for a clock, and the encoder refused each of those defs.
//
// Folding is not optional once a def is inlined: the cell's enable latch lands
// in the def's body, and the def scan refuses ANY def holding a latch, so
// inlining alone would trade an encode refusal for a normalization refusal.
//
// P=1 IS THE WHOLE SAFETY ARGUMENT. A gate has ONE commit edge, so folding it
// into an enable is a pure retype -- no phase divider, no re-timing, hence none
// of the cross-module timing question that keeps the GENERAL per-def case (a
// genuine latch or a negedge flop, P>1) refusing. The dry run enforces exactly
// that: a def whose plan wants a divider is left untouched for the refusal to
// find, and a def with no gate at all is not touched in any way.
// 2f-latch M9 — RECOGNIZE instantiated clock gates as `Clock_cell`, everywhere
// the hierarchical driver will encode. Runs BEFORE the inline+fold below, and
// takes precedence over it: what this recognizes, the fold never sees.
//
// NO `is_boxed` FILTER, AND THAT IS THE POINT. Inlining a TRUSTED def is
// unsound -- it pulls internals the user declared out of scope into the
// compared cone, which is why `inline_clock_gate_cells` takes the predicate.
// Recognition is different in kind: nothing of the def's STATE crosses the
// boundary (the enable latch is replaced by the cell's sampling contract), only
// a pure combinational function of nets the parent ALREADY drives and already
// compares. So trust is respected rather than fought -- and since the instance
// is then gone, the def is no longer instantiated at all and its trust entry
// becomes a no-op, which is what lets `prim_clk_gate` leave the trust list.
static int materialize_clock_cells_all(hhds::Graph* top, const std::vector<hhds::Graph*>& defs) {
  int                               n = livehd::latch_contract::materialize_clock_cells(top, "pass.single_edge");
  absl::flat_hash_set<hhds::Graph*> seen{top};
  for (auto* d : defs) {
    if (d == nullptr || !seen.insert(d).second) {
      continue;  // ref and impl def lists share every --lib cell model: same Graph*
    }
    n += livehd::latch_contract::materialize_clock_cells(d, "pass.single_edge");
  }
  return n;
}

// See the call site: warn when the impl instantiates a `--lib` cell whose model
// holds STATE. encode.cpp only inlines a COMBINATIONAL cell model, so such an
// instance is blackboxed and contributes no state — every cut that reads it then
// sees two unrelated symbols and both engines answer unknown in milliseconds.
// Emitted once per run, naming the cells, with the concrete way out.
static void inline_stateful_lib_cells(const absl::flat_hash_map<hhds::Gid, hhds::Graph*>& sub_lib, hhds::Graph* impl_g) {
  if (sub_lib.empty() || impl_g == nullptr) {
    return;
  }
  absl::flat_hash_set<hhds::Gid> stateful;
  for (const auto& [gid, gp] : sub_lib) {
    if (gp == nullptr) {
      continue;
    }
    for (auto dn : gp->body().nodes(hhds::Node_order::forward)) {
      const auto op = livehd::graph_util::type_op_of(dn);
      if (op == Ntype_op::Flop || op == Ntype_op::Fflop || op == Ntype_op::Latch || op == Ntype_op::Memory) {
        stateful.insert(gid);
        break;
      }
    }
  }
  if (stateful.empty()) {
    return;
  }
  std::set<std::string>         hit;    // sorted: the message must be deterministic
  std::vector<hhds::Node_class> insts;  // collect first: never mutate while walking
  for (auto sn : impl_g->body().nodes()) {
    if (livehd::graph_util::type_op_of(sn) != Ntype_op::Sub || stateful.count(sn.get_subnode_gid()) == 0) {
      continue;
    }
    insts.push_back(sn);
    if (auto sio = sn.get_subnode_io(); sio != nullptr) {
      hit.insert(std::string(sio->get_name()));
    }
  }
  const size_t n = insts.size();
  if (n == 0) {
    return;
  }
  std::string names;
  for (const auto& s : hit) {
    names += names.empty() ? "" : ", ";
    names += s;
  }
  // INLINE them instead of blackboxing. encode.cpp's `--lib` inline path is
  // combinational-only (a stateful model falls through to the blackbox path,
  // where it contributes NO state and nothing can correspond to the ref's native
  // flop — both engines then answer unknown in milliseconds). Splicing the cell
  // body into the impl turns its internal Flop into an ordinary body flop, which
  // the existing flop-cut machinery cuts and names after the instance — and
  // pass/abc/abc_map.cpp already names each mapped DFF instance after its source
  // register bit. Same move `inline_clock_gate_cells` makes for an ICG cell; the
  // only reason it could not reach these is that a `--lib` model is not in the
  // impl's own graph library, hence the explicit-def overload.
  size_t done = 0;
  for (const auto& inst : insts) {
    auto git = sub_lib.find(inst.get_subnode_gid());
    if (git == sub_lib.end() || git->second == nullptr) {
      continue;
    }
    // The cell's own instance name is the ONLY meaningful name the spliced state
    // can carry: a gensim cell model's internal flop has no `name` attr, so
    // Sub_inliner::carry_node_attrs leaves it unnamed and the flop cut ends up
    // keyed on a synthesized net name (`n1831`) that corresponds to nothing on
    // the ref side. Snapshot the existing flops, inline, then name whatever flop
    // appeared after the instance (`id_q_0`) — abc already named the instance
    // after the source register bit.
    // Only a SINGLE-flop model may take the instance name: stamping it on two
    // flops would fuse two distinct state cuts onto one key and silently drop a
    // compare point. A multi-flop cell keeps whatever the inliner produced.
    // Counted on the MODEL (a handful of nodes), and the naming itself happens
    // inside the inliner — re-walking the whole parent body once per instance
    // would be quadratic on a design with thousands of mapped cells.
    int model_flops = 0;
    for (auto dn : git->second->body().nodes(hhds::Node_order::forward)) {
      model_flops += livehd::graph_util::is_type_flop(dn) ? 1 : 0;
    }
    done += livehd::graph_util::inline_sub_instance(impl_g, inst, "pass.lec", git->second, model_flops == 1) ? 1 : 0;
  }
  if (done == n) {
    return;  // fully inlined: the cells are ordinary logic + flops now
  }
  livehd::diag::warn("pass.lec", "stateful-lib-cell", "unsupported")
      .msg("the impl instantiates {} STATEFUL library cell(s) ({}) — lec could inline only {} of them", n, names, done)
      .hint(
          "a cell model that stays a blackbox contributes no state, so nothing corresponds to the ref's native flop "
          "and the run is INCONCLUSIVE no matter the budget; re-synthesize with `--set pass.abc.register=false` to "
          "keep registers native")
      .emit();
}

// The IMPL is one FLAT def but the REF still instantiates its hierarchy: inline
// the ref's Sub instances so both sides own the same state.
//
// `pass color flat` fuses the whole hierarchy into ONE abc region, so its netlist
// is a SINGLE graph while the ref keeps its children. The ref top then owns only
// its own flops (dino: pc, cycleCount) and EVERY child register is impl-only
// unpaired state — no flop bijection, so the flop-cut inductive miter is never
// built and the run degrades to a whole-design BMC that times out. Inlining is
// semantics-preserving and gives each spliced flop its hierarchical name
// (`pipeA_if_id.reg_0`), which is exactly what the netlist calls it, so tier-1
// name pairing resolves them. Returns how many instances were spliced.
//
// Deliberately narrow: ONLY when the impl top holds no Sub at all. A netlist that
// kept its hierarchy (`pass color synth`) already pairs def by def, and flattening
// there would throw away the decomposition that makes it tractable.
static size_t flatten_ref_to_match_flat_impl(const absl::flat_hash_map<hhds::Gid, hhds::Graph*>& sub_lib, hhds::Graph* ref_g,
                                             hhds::Graph* impl_g) {
  if (ref_g == nullptr || impl_g == nullptr || sub_lib.empty()) {
    return 0;  // no `--lib`: this is not a mapped-netlist comparison
  }
  // A mapped netlist is ALL Subs — every Liberty cell is one. Only a DESIGN
  // instance (not a `--lib` cell model) says the impl kept its hierarchy, and at
  // least one cell must actually be there: an impl with NEITHER is an ordinary
  // design whose children upass inlined, and flattening the ref would throw away
  // a per-def decomposition that already works.
  bool has_cell = false;
  for (auto n : impl_g->body().nodes()) {
    if (livehd::graph_util::type_op_of(n) != Ntype_op::Sub) {
      continue;
    }
    if (sub_lib.find(n.get_subnode_gid()) == sub_lib.end()) {
      return 0;  // hierarchical impl: leave the ref alone
    }
    has_cell = true;
  }
  if (!has_cell) {
    return 0;
  }
  size_t done = 0;
  // Splicing a child can expose the grandchildren it instantiated, so sweep until
  // the ref is flat. Bounded: each round must make progress or it stops.
  for (int round = 0; round < 64; ++round) {
    std::vector<hhds::Node_class> insts;  // collect first: never mutate while walking
    for (auto n : ref_g->body().nodes()) {
      if (livehd::graph_util::type_op_of(n) == Ntype_op::Sub) {
        insts.push_back(n);
      }
    }
    if (insts.empty()) {
      break;
    }
    size_t spliced = 0;
    for (const auto& inst : insts) {
      spliced += livehd::graph_util::inline_sub_instance(ref_g, inst, "pass.lec") ? 1 : 0;
    }
    if (spliced == 0) {
      break;  // nothing inlinable left (a real blackbox): stop rather than spin
    }
    done += spliced;
  }
  return done;
}

static std::pair<int, int> inline_clock_gates_and_fold(hhds::Graph* top, const std::vector<hhds::Graph*>& defs,
                                                       absl::flat_hash_set<hhds::Graph*>*             unfolded,
                                                       const std::function<bool(const hhds::Graph*)>& is_boxed = {}) {
  int                               cells  = livehd::latch_contract::inline_clock_gate_cells(top, "pass.single_edge", is_boxed);
  int                               folded = 0;
  // Dedupe: the ref and impl def lists share every `--lib` cell model, and a def
  // reached twice is the same Graph*. Inlining is idempotent, but the COUNT
  // would double and read as twice the work.
  absl::flat_hash_set<hhds::Graph*> seen{top};
  for (auto* d : defs) {
    if (d == nullptr || !seen.insert(d).second) {
      continue;
    }
    // STRICTLY ADDITIVE: a def that already holds a latch or a negedge flop is
    // one the def scan refuses TODAY, and that refusal is load-bearing (it is
    // what keeps a latch def from being silently blackboxed). Leave it exactly
    // as it was and let the scan speak. We only ever touch defs that pass the
    // scan today, so nothing that passes now can start failing.
    if (const auto pre = livehd::latch_contract::needs_single_edge(d); pre.n_latches > 0 || pre.n_negedge_flops > 0) {
      continue;
    }
    // PREDICT the fold failure instead of discovering it after mutating. The
    // inline is DESTRUCTIVE and has no undo, so a def whose fold then fails is
    // handed back holding an enable Latch it did NOT have when we found it —
    // and while `unfolded` keeps it out of the def SCAN, the ENCODER still
    // refuses a Latch, so a def that used to encode cleanly (an opaque gate
    // cell whose gated clock only crosses into a child) regresses from PROVEN
    // to UNKNOWN purely because this ran. "Nothing that passes now can start
    // failing" only holds for defs whose fold succeeds.
    //
    // The dominant failure is the documented one: resolve_icg folds only in a
    // SINGLE-clock design (a gate on a second domain has no reference clock to
    // be relative to), after which the orphaned latch wants a divider. That is
    // decidable BEFORE touching anything.
    if (livehd::latch_contract::Design_clocks(d).n_clock_inputs() > 1) {
      continue;
    }
    const int nd = livehd::latch_contract::inline_clock_gate_cells(d, "pass.single_edge", is_boxed);
    if (nd <= 0) {
      continue;  // no gate here: leave the def byte-for-byte as it was
    }
    cells += nd;
    // An EMPTY allow-list: this call normalizes the def's OWN body only. A
    // latch deeper still is not this call's business -- the caller's top-level
    // scan walks the whole instance tree and refuses there, as before.
    livehd::single_edge::Options dp;
    dp.dry_run                        = true;
    dp.quiet                          = true;  // a def we then decline to fold must not print a refusal
    const auto                   plan = livehd::single_edge::normalize(d, {}, dp);
    livehd::single_edge::Options ao;
    ao.quiet = true;
    if (!plan.error && plan.applied && plan.slots == 1) {
      if (const auto done = livehd::single_edge::normalize(d, {}, ao); done.applied && !done.error) {
        ++folded;
        continue;
      }
    }
    // Could not fold (a second clock net, or a plan wanting a divider). The
    // gate's enable latch is now in this def's body, which the def scan would
    // refuse -- turning what is today a single UNKNOWN def into a refusal of
    // the WHOLE run. So hand the def back to the caller to keep OUT of that
    // scan: the encoder then meets it exactly as it does today and returns the
    // same honest per-def UNKNOWN (`sequential op 'latch' not supported yet`
    // rather than `derived clock` -- same verdict, different sentence), while
    // every def that did fold is a def that now proves.
    // Residual (not predicted above): the def is now mutated and there is no
    // rollback, so at minimum say so instead of leaving a silent regression.
    if (const auto post = livehd::latch_contract::needs_single_edge(d); post.n_latches > 0) {
      livehd::diag::warn("pass.single_edge", "icg-inline-not-folded", "unsupported")
          .msg(
              "def '{}' had its clock-gate cell inlined but the fold did not apply, so it now holds an enable latch it "
              "did not have before; it will encode as UNKNOWN rather than refuse",
              d->get_name())
          .hint("flatten the design, or trust this def, to get a verdict for it")
          .emit();
    }
    if (unfolded != nullptr) {
      unfolded->insert(d);
    }
  }
  return {cells, folded};
}

void lec_command(Options& opts, Result& res) {
  // Whether the USER passed --workdir (captured before load_side_graphs' first
  // workdir() call fabricates a scratch temp dir): the lecfail witness testbench
  // + VCD are on-by-default only for a persistent, user-named --workdir.
  const bool workdir_set = !opts.workdir.empty();
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

  // The solver selects the backend: cvc5 (default) / bitwuzla discharge
  // in-process (pass/lec, no yosys); lgyosys shells out to inou/yosys/lgcheck
  // (the former `lhd check`) — the only backend that reads Verilog without a
  // front-end reader and the path for gate-level / yosys-origin netlists.
  Eprp_var::Eprp_dict labels;
  merge_sets(opts, "formal", labels);      // the shared formal.* vocabulary
  merge_sets(opts, "formal.lec", labels);  // lec-specific canonical spelling wins
  auto label = [&](std::string_view k, std::string_view def) -> std::string {
    auto it = labels.find(std::string{k});
    return it == labels.end() ? std::string{def} : it->second;
  };
  const std::string solver = label("solver", "cvc5");
  if (solver != "cvc5" && solver != "bitwuzla" && solver != "lgyosys") {
    throw Lhd_error{"usage",
                    std::format("--set formal.solver expects cvc5|bitwuzla|lgyosys, got '{}'", solver),
                    "cvc5 (default, in-process SMT) | bitwuzla (in-process SMT) | lgyosys (yosys/lgcheck)"};
  }
  if (solver == "lgyosys") {
    if (!opts.files.empty() || !opts.formal_filter.empty()) {
      throw Lhd_error{"unsupported", "formal-block LEC helpers require the cvc5 backend", "use --set formal.solver=cvc5"};
    }
    lec_lgyosys(opts, res);
    return;
  }

  // Formal BLOCKS are a `lhd formal verify` construct, not a lec one (user
  // ruling, 2026-07-25): a block is an independent test, while lec has a single
  // obligation (impl == ref) that a block's assumes could only condition
  // globally — which is precisely the cross-block poisoning that ruling removes.
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

  bool cross = label("cross", "false") != "false" && label("cross", "false") != "0";

  // Discharge in-process via pass/lec (L1). The engine is the authority on the
  // non-cross path; in cross mode we additionally run lgcheck and assert
  // agreement (the strongest encoder check).
  livehd::lec::Lec_options o;
  o.assume_check          = assume_check;
  // pass.formal has already proved and removed every checked child/local
  // obligation.  What remains is active by contract (explicit nocheck,
  // selected-top IO, or all assumptions when checking is disabled), and must
  // constrain both the flat and hierarchical cvc5 translations.
  const auto ref_assumes  = design_assume_occurrences(ref_g.get());
  const auto impl_assumes = design_assume_occurrences(impl_g.get());
  // ...with ONE exception, and it is lec-specific. pass.formal promotes an
  // UNDISCHARGED selected-top IO `assume` to an active hypothesis because a top
  // has no parent that could establish it. That is defensible for `lhd formal
  // verify`: the constraint conditions that ONE design's own assertions, and
  // every verdict line discloses it. lec is a TWO-sided obligation over SHARED
  // inputs, so the same promotion narrows the compared input space of a miter
  // whose OTHER side never made the claim — `--ref golden.v --impl design.prp`
  // with `assume(a < 4)` in the impl reports "PROVEN equivalent" and exits 0 for
  // designs that differ at every a >= 4. A false PROVEN is the one verdict a LEC
  // tool may never hand out, and the user asked for the constraint to be CHECKED,
  // so refuse instead of guessing — the same "refuse loudly rather than silently
  // ignore" rule the formal-block sidecar above follows. Both sanctioned
  // spellings still constrain the miter, they just say so in the source.
  if (ref_assumes.undischarged > 0 || impl_assumes.undischarged > 0) {
    const bool  on_ref  = ref_assumes.undischarged > 0;
    const bool  on_impl = impl_assumes.undischarged > 0;
    std::string side    = on_ref && on_impl ? "ref and impl" : (on_ref ? "ref" : "impl");
    std::string loc     = on_ref ? ref_assumes.undischarged_loc : impl_assumes.undischarged_loc;
    throw Lhd_error{"unsupported",
                    std::format("lec: the {} side has {} top-level IO assume(s) that were never discharged{} — accepting "
                                "them as miter hypotheses would restrict the compared input space without proof",
                                side,
                                ref_assumes.undischarged + impl_assumes.undischarged,
                                loc.empty() ? std::string{} : " (first at " + loc + ")"),
                    "spell it assume_nocheck (a disclosed free environment contract) or pass --set "
                    "formal.assume_check=false: either one keeps it in force over the whole miter, on the record"};
  }
  o.unchecked_assumes = ref_assumes.active + impl_assumes.active;
  o.design_assumes    = o.unchecked_assumes > 0;
  o.assumption_key    = std::format("design:{}:{}", o.unchecked_assumes, assume_check ? 1 : 0);
  o.engine            = label("engine", "auto");
  o.solver            = solver;  // cvc5 | bitwuzla
  o.gold_x            = label("gold_x", "ignore");
  o.bound             = std::atoi(label("bound", "6").c_str());
  o.timeout
      = std::atoi(label("timeout", "120").c_str());  // bound the CLI: hard miters degrade to UNKNOWN, never freeze (0 = unbounded)
  o.witness     = label("witness", "true") != "false" && label("witness", "true") != "0";
  o.decompose   = label("decompose", "auto");
  o.cones       = label("cones", "auto");
  o.conelimit   = std::atoi(label("conelimit", "10000").c_str());
  o.phase_sched = label("phase_sched", "true") != "false" && label("phase_sched", "true") != "0";
  o.box_seq     = label("box_model", "seq") != "uf";
  o.int_blast   = label("int_blast", "auto");
  o.strict      = label("strict", "true") != "false" && label("strict", "true") != "0";
  if (!o.strict) {
    // ALWAYS warn: with strict off, an INCONCLUSIVE run exits 0 and reads as a
    // pass to anything checking the exit code -- including a run that proved
    // nothing at all. It is a legitimate "quick check" mode, but it must never
    // be silent, because the failure it hides looks exactly like success.
    livehd::diag::warn("pass.lec", "strict-off", "unsupported")
        .msg("formal.strict=false: an INCONCLUSIVE verdict will exit 0 and be indistinguishable from a real proof")
        .hint(
            "this is a QUICK-CHECK mode, not an equivalence gate -- a run that decided nothing also passes. Leave "
            "formal.strict=true (the default) for anything that gates a commit")
        .emit();
  }
  o.allow_oversize      = label("allow_oversize", "false") != "false" && label("allow_oversize", "false") != "0";
  o.semdiff             = livehd::lec::lec_canon_semdiff(label("semdiff", "structural"));
  o.state_pairing       = label("state_pairing", "true") != "false" && label("state_pairing", "true") != "0";
  o.partitions          = std::atoi(label("partitions", "4").c_str());
  o.jobs                = std::max(1, std::atoi(label("jobs", "4").c_str()));
  o.split               = label("split", "auto");
  o.rlimit              = std::atoi(label("rlimit", "0").c_str());  // deterministic per-query budget (0=off; CI/repro)
  // `timeout` is a SOFT TOTAL; `min_timeout` is the per-def floor beneath it, so
  // a def dispatched after the total is spent still earns a real verdict instead
  // of a silent skip. Budget accounting is on iff timeout>0 && rlimit==0 — the
  // deterministic rlimit tier owns the bound by itself, which is why the old
  // budget_mode knob is gone.
  o.min_timeout         = std::atoi(label("min_timeout", "1").c_str());
  // Hard wall backstop on a forked proof worker, as a multiple of `timeout` (0 = off).
  // `timeout` is cvc5 tlimit-per, which cannot preempt ONE long CaDiCaL solve -- the
  // shape a flat box-free miter takes. See Lec_options::hard_timeout_mult.
  o.hard_timeout_mult   = std::atoi(label("hard_timeout_mult", "3").c_str());
  o.spec_mining_timeout = std::atoi(label("spec_mining_timeout", "0").c_str());
  o.phase               = label("phase", "after_reset");
  o.reset_cycles        = std::atoi(label("reset_cycles", "2").c_str());
  o.reset               = label("reset", "");
  // formal.stats: cvc5 solve-insight report. `--stats` is CLI sugar for the same
  // knob, so OR the two (the semdiff pattern) — a bare --stats must not be erased
  // by the registry default, and an explicit --set formal.stats=true must survive
  // without the flag.
  {
    const std::string stats_label = label("stats", "false");
    o.stats                       = opts.stats || (stats_label != "false" && stats_label != "0");
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
  // Runs after the --lib cell models load and BEFORE canonical_digest, since a
  // //graph-resident pass is outside //lhd:formal_salt and would otherwise let
  // a stale cached verdict answer for a differently-timed design.
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
    // Give lec the same false-loop preparation inou.cgen.verilog runs. A packed
    // self-reference whose feedback threads through a PURE-COMB instance is
    // invisible to lnast.tolg's per-wire splitter (a Sub is a scheduling
    // boundary there), but pass/lec/encode.cpp INLINES a combinational callee,
    // so the encoder does see the cycle and refuses the whole def ("operand has
    // no encodable driver (combinational cycle?)") -- UNKNOWN on a design that
    // is acyclic per bit and perfectly provable once the instance is dissolved.
    // flatten_false_loop_subs also repairs the word-level cycle it exposes.
    //
    // Only the two TOPS are prepared: a `--lib` cell model in sub_lib is SHARED
    // by both sides, so inlining into one would be a cross-side edit. Both steps
    // are no-ops unless a stateless Sub's output really feeds back into one of
    // its own inputs, so this costs nothing on an ordinary design.
    for (auto* prep : {ref_g.get(), impl_g.get()}) {
      if (prep != nullptr) {
        if (const int nf = livehd::graph_util::flatten_false_loop_subs(prep); nf > 0) {
          std::print("lec: dissolved {} false comb-loop instance(s) in '{}' before encoding\n", nf, prep->get_name());
        }
      }
    }

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
    if (auto nflat = flatten_ref_to_match_flat_impl(sub_lib, ref_g.get(), impl_g.get()); nflat > 0) {
      std::print("lec: flattened {} ref instance(s) to match a flat impl netlist\n", nflat);
      ref_defs.clear();  // the children are inline now; only the top is comparable
      ref_defs.push_back(ref_g.get());
      for (const auto& [gid, gp] : sub_lib) {
        if (gp != nullptr && gp != ref_g.get() && gp != impl_g.get()) {
          ref_defs.push_back(gp);
        }
      }
    }
    inline_stateful_lib_cells(sub_lib, impl_g.get());
    for (auto* d : impl_defs) {
      if (d != nullptr && d != impl_g.get() && sub_lib.find(d->get_gid()) == sub_lib.end()) {
        inline_stateful_lib_cells(sub_lib, d);
      }
    }
    // CLOCK-GATE CELLS first. A real design instantiates its ICG
    // (`prim_clk_gate u_cg(.clk_i(clk), .en_i(en), .clk_o(gclk));`), so the
    // gate sits one module level away and the flop's clock_pin is an opaque Sub
    // output that nothing can recognize. Inlining just those cells brings the
    // gate into the body, where the M8 fold turns it into a flop enable.
    // Semantics-preserving on its own and idempotent, so it runs on BOTH sides
    // before either is probed -- symmetry matters here as much as anywhere.
    //
    // Across the top AND every def the hierarchical driver will encode; see
    // inline_clock_gates_and_fold for why the per-def half is load-bearing and
    // why folding it at P=1 is sound.
    absl::flat_hash_set<hhds::Graph*> unfolded;
    auto                              note_gates = [&res](std::string_view which, std::pair<int, int> r) {
      if (r.first > 0) {
        res.recipe_steps.emplace_back(
            std::format("pass.single_edge inlined {} {} clock-gate cell(s), folded {} def(s)", r.first, which, r.second));
      }
    };
    // M9 recognition runs FIRST and on BOTH sides -- symmetry matters here as
    // much as anywhere, since a gate recognized on one side only would compare
    // a Clock_cell against a Sub.
    if (const int mr = materialize_clock_cells_all(ref_g.get(), ref_defs); mr > 0) {
      res.recipe_steps.emplace_back(std::format("pass.single_edge recognized {} ref clock gate(s) as Clock_cell", mr));
    }
    if (const int mi = materialize_clock_cells_all(impl_g.get(), impl_defs); mi > 0) {
      res.recipe_steps.emplace_back(std::format("pass.single_edge recognized {} impl clock gate(s) as Clock_cell", mi));
    }
    note_gates("ref", inline_clock_gates_and_fold(ref_g.get(), ref_defs, &unfolded, is_boxed));
    note_gates("impl", inline_clock_gates_and_fold(impl_g.get(), impl_defs, &unfolded, is_boxed));
    if (!unfolded.empty()) {
      auto drop = [&unfolded](std::vector<hhds::Graph*>& v) {
        std::erase_if(v, [&unfolded](hhds::Graph* d) { return unfolded.contains(d); });
      };
      drop(ref_defs);
      drop(impl_defs);
    }
    // ── M10: pass.single_edge stops being MANDATORY ───────────────────────────
    // A DECLINE used to be a hard failure ("lec refused the <side>"), which is
    // why a latch or a negedge flop inside a def, two clock ports tied to one
    // net, a memory under the divider, or a latch that closes on the very edge
    // its reader samples all ended the run with nothing compared. Under
    // `formal.phase_sched` (the default) a decline instead hands the design to
    // the encoder's READ-ONLY four-microstep schedule, which composes across
    // hierarchy because it rewrites nothing.
    //
    // The rewrite is still PREFERRED when it applies. It is not a fallback for
    // its own sake: it also normalizes a SYNC reset into the enable/din shape on
    // BOTH sides, and that is what lets semdiff's state pairing match a flop
    // whose reset the other front-end spells in the body (tests/equiv/
    // flop_reset_matrix). Until the pairing learns that fold, skipping the
    // rewrite where it WOULD have worked costs real verdicts.
    //
    // BOTH sides or NEITHER: a one-sided lowering compares two designs in
    // different time bases, which is the failure the decline exists to prevent.
    auto probe_side = [&](hhds::Graph* side, const std::vector<hhds::Graph*>& defs) {
      livehd::single_edge::Options po;
      po.dry_run = true;
      // QUIET: a decline is no longer a failure (the phase schedule takes over),
      // so the probe must not emit error-severity diagnostics -- the CLI counts
      // those and would fail an otherwise clean, PROVEN run.
      po.quiet   = o.phase_sched;
      return livehd::single_edge::normalize(side, defs, po);
    };
    const auto pr       = probe_side(ref_g.get(), ref_defs);
    const auto pi       = probe_side(impl_g.get(), impl_defs);
    const bool declined = pr.error || pi.error;
    if (declined && !o.phase_sched) {
      // Re-run loudly so the user gets the specific diagnostic, then fail.
      const char* which = pr.error ? "ref" : "impl";
      auto*       g_bad = pr.error ? ref_g.get() : impl_g.get();
      livehd::single_edge::normalize(g_bad, pr.error ? ref_defs : impl_defs, {});
      throw Lhd_error{"unsupported",
                      std::format("lec refused the {} side '{}': edge normalization declined ({})",
                                  which,
                                  g_bad->get_name(),
                                  pr.error ? pr.reason : pi.reason),
                      "a partial or one-sided lowering compares the two designs in different time bases"};
    }
    if (declined) {
      res.recipe_steps.emplace_back(std::format("pass.lec phase_sched: 4-microstep schedule (edge normalization declined: {})",
                                                pr.error ? pr.reason : pi.reason));
    } else if (pr.applied || pi.applied) {
      // ONE time base for both sides: the max P either side needs. A side with
      // nothing of its own to lower still gets the divider and slot 0 — that is
      // its P=1 behaviour embedded in the P-slot time base, and it is what keeps
      // an all-posedge ref comparable against a negedge impl instead of the two
      // counting time differently.
      livehd::single_edge::Options ao;
      ao.force_slots        = std::max(pr.slots, pi.slots);
      const auto rn         = livehd::single_edge::normalize(ref_g.get(), ref_defs, ao);
      // SAME GRAPH OBJECT on both sides (`--impl X --ref X`, the vacuity-guard
      // idiom, and any two --impl/--ref paths that resolve to one library):
      // normalizing again would run over the ALREADY-normalized graph, find
      // nothing left to lower, and report "skipped" with no slot count and no
      // reference clock -- which the cross-side agreement checks below would
      // then read as a disagreement and refuse a design that is trivially equal
      // to itself.
      const bool same_graph = ref_g.get() == impl_g.get();
      const auto in         = same_graph ? rn : livehd::single_edge::normalize(impl_g.get(), impl_defs, ao);
      if (rn.error || in.error) {
        throw Lhd_error{"unsupported",
                        std::format("lec: edge normalization failed after planning ({})", rn.error ? rn.reason : in.reason),
                        ""};
      }
      // Both agreement checks apply only when BOTH sides actually normalized. A
      // side that legitimately had nothing to lower reports slots=1 and no
      // reference clock, and comparing that against a normalized sibling is not
      // a disagreement -- the force_slots above already put them in one time
      // base.
      if (rn.applied && in.applied && rn.slots != in.slots) {
        throw Lhd_error{
            "unsupported",
            std::format("lec: edge normalization produced P={} on the ref side and P={} on the impl side", rn.slots, in.slots),
            "the two designs mix clock edges differently; compare like against like"};
      }
      if (rn.applied && in.applied && rn.ref_clock != in.ref_clock) {
        // Slots are expressed RELATIVE to a reference clock, so two sides
        // normalized against different clocks are in different time bases. The
        // encoder cannot catch this itself: it models a single clock as
        // "commits every step" and has no notion of clock IDENTITY, so a latch
        // gated by `clk` and one gated by `clk2` encode identically and come
        // back falsely PROVEN (lgyosys refutes the same pair).
        throw Lhd_error{"unsupported",
                        std::format("lec: the ref side normalizes against clock '{}' but the impl side against '{}'",
                                    rn.ref_clock.empty() ? "<none>" : rn.ref_clock,
                                    in.ref_clock.empty() ? "<none>" : in.ref_clock),
                        "the two designs are clocked by different nets, so their slots do not denote the same instants"};
      }
      // BMC `bound` counts STEPS, and a step is now a sub-step: at P=2 the same
      // bound buys half the design cycles. Scale it so a design keeps the depth
      // coverage its options asked for.
      if (rn.slots > 1) {
        o.bound *= rn.slots;
      }
      lec_single_edge_slots = rn.slots;
      res.recipe_steps.emplace_back(std::format("pass.single_edge slots:{} ref_latches:{} impl_latches:{} bound:{}",
                                                rn.slots,
                                                rn.latches_retyped,
                                                in.latches_retyped,
                                                o.bound));
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
  // (formal_cache.json; opt out with --set formal.cache=false). Keyed cache-wide
  // by kFormalSrcSalt — the build-time content hash of the prover sources — so
  // a prover change invalidates every stored verdict automatically. v1 wires
  // it into the hierarchical driver (the default path).
  std::unique_ptr<livehd::formal::Verdict_cache> vcache;
  if (workdir_set && label("cache", "true") != "false" && label("cache", "true") != "0") {
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
  // formal.stats: per-def (name, conflicts) ranking, filled by the hierarchical
  // driver only (the flat path is one def, so the totals already say it all).
  std::vector<std::pair<std::string, int64_t>> cvc5_hot;
  if (label("hier", "true") != "false" && label("hier", "true") != "0") {
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
                      "fail (default; a refuted block fails the run, its parents are skipped) | escalate (prove the "
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
                         o.stats ? &cvc5_hot : nullptr);
    if (vcache) {
      vcache->save();
      if (vcache->hits() > 0 || vcache->stores() > 0 || vcache->skips() > 0) {
        std::print("lec[cache]: {} hit(s), {} stored, {} skipped-unknown ({}/formal_cache.json)\n",
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
    bool pairs_from_hint = false;
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
        so.matching_names = true;
        so.state_pairing  = true;
        so.seed_pairs     = o.match;
        auto m            = livehd::semdiff::structural_match(ref_g.get(), impl_g.get(), so);
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
          r.verdict    = livehd::lec::Verdict::Proven;
          r.engine     = "cache";
          r.detail     = hit->detail.empty() ? "verdict cache hit" : hit->detail;
          r.elapsed_ms = 0;
          std::print("lec: '{}' PROVEN (cache)\n", impl_g->get_name());
          settled_by_cache = true;
        } else if (!flat_retry_all && vcache->skip_unknown(flat_ckey, o.timeout)) {
          r.verdict    = livehd::lec::Verdict::Unknown;
          r.engine     = "cache-skip";
          r.detail     = "skipped: known inconclusive at >= this budget (--set formal.retry=all to re-attempt)";
          r.elapsed_ms = 0;
          std::print("lec: '{}' UNKNOWN (skipped: known inconclusive)\n", impl_g->get_name());
          settled_by_cache = true;
        }
      }
    }
    auto t0 = std::chrono::steady_clock::now();
    if (!settled_by_cache) {
      r = livehd::lec::prove_equal(ref_g.get(), impl_g.get(), o, sub_lib_ptr);
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
        auto rf        = livehd::lec::prove_equal(ref_g.get(), impl_g.get(), oflat, sub_lib_ptr);
        rf.detail      = "flat-confirm after collapsed-box REFUTE" + std::string(rf.detail.empty() ? "" : "; ") + rf.detail
                         + (r.detail.empty() ? "" : " (collapsed run: " + r.detail + ")");
        rf.elapsed_ms  = -1;      // the progress record carries the combined wall-clock below
        rf.cvc5       += r.cvc5;  // the collapsed run's cvc5 effort was still spent (formal.stats)
        r              = std::move(rf);
      }
      // int_blast=auto second leg (same rule as the hierarchical driver): a
      // solver-give-up Unknown earns one int-blasted re-solve at min_timeout,
      // BEFORE the trusted-box demotion so a retry refute obeys it too.
      r = livehd::lec::int_blast_retry(ref_g.get(), impl_g.get(), o, std::move(r), sub_lib_ptr);
      // Same trusted-box discipline as the hierarchical driver: a refute that
      // turns on a trusted box input is not a disproof (the leaf may treat it as
      // don't-care and cannot be flattened) — degrade to Unknown, keep witness.
      if (r.verdict == livehd::lec::Verdict::Refuted) {
        absl::flat_hash_set<std::string> trust_set(o.trust.begin(), o.trust.end());
        if (std::string tb = lec_refute_trusted_box(r.witness, trust_set); !tb.empty()) {
          r.verdict = livehd::lec::Verdict::Unknown;
          r.detail  = std::format(
              "INCONCLUSIVE: refuted only at trusted-box input (bbin:{}) — trust asserts all leaf "
              "inputs equal incl. don't-cares, not flattenable, so not a disproof; {}",
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
          vcache->insert(flat_ckey, {r.engine, r.detail, ms});  // definitive Proven only (rule F)
        }
        vcache->set_hint(std::string{impl_g->get_name()}, {r.engine, r.split_used, ms});
        lec_store_pair_hint(vcache.get(), lec_entity_of(impl_g->get_name()), r.uncertain_pairs_used);
      } else if (r.verdict == livehd::lec::Verdict::Unknown && r.witness.empty() && flat_cacheable && !flat_ckey.empty()) {
        // Witness-free Unknown: ledger the attempt (a witness-carrying partial-miter
        // diff is actionable and must re-surface every run, so it is NOT ledgered).
        vcache->note_unknown(flat_ckey, {o.timeout, ms});
      } else if (pairs_from_hint) {
        // Self-heal (mirror of the hier driver): a replayed pair hint that did
        // not end Proven is stale — drop it so the next run pairs fresh.
        vcache->clear_pair_hint(lec_entity_of(impl_g->get_name()));
      }
    }
    lec_store_cones(vcache.get(), r);
    if (vcache != nullptr) {
      vcache->save();
      if (vcache->hits() > 0 || vcache->stores() > 0 || vcache->skips() > 0) {
        std::print("lec[cache]: {} hit(s), {} stored, {} skipped-unknown ({}/formal_cache.json)\n",
                   vcache->hits(),
                   vcache->stores(),
                   vcache->skips(),
                   opts.workdir);
      }
    }
    // Per-block progress (info severity): stream the verdict the moment it resolves.
    emit_lec_block_progress(impl_g->get_name(), r, o, ms);
  }

  // formal.stats: the cvc5 solve-insight report, one run total. Both paths funnel
  // into `r` — the hierarchical driver already summed every def into r.cvc5 (and
  // filled cvc5_hot); the flat path carries its single solver's numbers.
  //
  // BEFORE every throwing arm below (pass_lec.cpp:293 does the same for the same
  // reason): the oversize refusal, the encoder refusal and the strict-unknown
  // failure all exit the process, and a report printed after them is lost in
  // exactly the runs where knowing how hard the solver worked matters most —
  // after the user already paid the ~8x plugin cost for it.
  if (o.stats) {
    livehd::lec::report_cvc5_stats("lec", r.cvc5, cvc5_hot);
  }

  // Design-size refusal is a hard admission failure (like pass.abc), not a
  // solver-inconclusive UNKNOWN: exit non-zero regardless of formal.strict, naming
  // the override. (`lhd pass lec` already fatals on any UNKNOWN; this makes the
  // `lhd lec` CLI path consistent for the size case specifically.)
  if (r.oversize_refused) {
    throw Lhd_error{"unsupported",
                    std::format("lec refused '{}': {}", impl_g->get_name(), r.detail),
                    "set formal.allow_oversize=true to run it anyway (it may exhaust host memory)"};
  }

  // VERDICT TAXONOMY (user ruling 2026-08-02):
  //
  //   REFUTED   BMC found a counterexample                       -> exit 10
  //   UNKNOWN   the solver TIMED OUT / gave up, decided nothing  -> formal.strict
  //   PASS      proved INDUCTIVELY: holds for all cycles          -> exit 0
  //   PASS(n)   BMC ran to completion, no counterexample, and is
  //             EXHAUSTIVE OVER INPUTS for n cycles from reset    -> exit 0
  //
  // PASS(n) is a real pass, not an "undecided": it decided something definitive
  // and complete, just to a depth. Reporting it as UNKNOWN conflated it with a
  // solver give-up, which is the distinction `formal.strict` is meant to act on
  // -- strict is about TIMEOUTS, not about proof depth. The depth is carried in
  // the verdict word so a reader can see exactly what was established; a pair
  // that first diverges at cycle 40 reports PASS(6) and is honest about it.

  // A PROVEN verdict obtained with a non-empty trust list is CONDITIONAL on those
  // assumptions — disclose it on the verdict line and in the machine-readable
  // detail (envelope/JSON), so a trust-assisted pass is never read as an
  // unconditional proof. A REFUTED/UNKNOWN needs no such caveat (a refute is a
  // real counterexample outside the trusted cones; an unknown proves nothing).
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

  bool lec_equiv = r.verdict == livehd::lec::Verdict::Proven;
  bool lec_known = r.verdict != livehd::lec::Verdict::Unknown;

  // A completed BMC is NOT "UNKNOWN". It decided something DEFINITIVE and
  // exhaustive: for EVERY input combination, from reset, the two designs agree
  // for `bound` cycles. What it does not cover is cycle bound+1 onward, so it is
  // not EQUIVALENCE -- but reporting it with the same word the solver uses when
  // it gives up conflates a real (bounded) result with "could not decide", which
  // is the conflation the verdict-discipline contract exists to prevent.
  //
  // So it gets its own headline. The EXIT CLASS is still the inconclusive one
  // (formal.strict decides, and it is never the exit-10 "here is a
  // counterexample"), because a k-cycle result must not gate a commit as though
  // it were equivalence -- measured, a pair diverging at cycle 40 is exhaustively
  // equal for the first 39.
  // PASS(n): complete and exhaustive over inputs, to n cycles from reset. A
  // plain PASS is the inductive (all-cycles) proof. UNKNOWN now means only what
  // it says -- the solver timed out or gave up.
  const std::string pass_word = r.bounded ? std::format(
                                                "PASS({}) equivalent for {} cycles from reset (exhaustive over inputs; "
                                                "deeper cycles not checked)",
                                                o.bound,
                                                o.bound)
                                          : std::string{"PROVEN equivalent"};
  const char*       verdict   = lec_known ? (lec_equiv ? pass_word.c_str() : "REFUTED (not equivalent)") : "UNKNOWN";
  std::print("lec: '{}' {} ({})\n", impl_g->get_name(), verdict, r.detail);
  // The witness names the diverging COMMON outputs; print it on Refuted AND on the
  // Unknown-because-incomplete-correspondence case (where a matched-portion diff is
  // still the actionable iteration signal), not only on a clean Refuted.
  if (!r.witness.empty()) {
    std::print("  counterexample: {}\n", r.witness);
  }
  // simfail simulation test + VCD (`lhd lec` + --workdir). On a REFUTED
  // verdict, write simfail_<resolved-top>.prp and optionally run it. Without a
  // user --workdir both actions are off; formal.witness gates the feature.
  if (r.verdict == livehd::lec::Verdict::Refuted) {
    const bool simfail = o.witness && workdir_set && setting_enabled(labels.contains("simfail") ? labels.at("simfail") : "true");
    const bool simfail_run = setting_enabled(labels.contains("simfail_run") ? labels.at("simfail_run") : "true");
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
    } else if (simfail) {
      const std::string top = lecfail_simple_name(impl_g->get_name());
      emit_lecfail_witness(opts,
                           res,
                           r,
                           std::string(impl_g->get_name()),
                           std::string(ref_g->get_name()),
                           simfail_filename(top),
                           simfail_run);
    }
  }

  if (!cross) {
    if (r.verdict == livehd::lec::Verdict::Refuted) {
      throw Lhd_error{"equiv_fail",
                      std::format("'{}' is not equivalent ({} vs {})", impl_g->get_name(), opts.impl_path, opts.ref_path),
                      r.witness.empty() ? "" : std::format("counterexample: {}", r.witness)};
    }
    // An EMPTY miter: the module is empty, has no output/state, or does not exist
    // on both sides, so not one compare point was checked. This used to surface
    // as PROVEN (exit 0, zero warnings) or as the tolerated inconclusive warning
    // — either way a gate reading the exit code read "verified" for a run that
    // verified nothing. Hard-fail regardless of formal.strict, like the encoder
    // refusal below. Checked ahead of the verdict split on purpose: the flag, not
    // the verdict, is what says nothing was compared.
    if (r.nothing_compared) {
      throw Lhd_error{
          "equiv_fail",
          std::format("lec compared NOTHING for '{}': the module is empty or has no output/state to check", impl_g->get_name()),
          std::format("{}. An equivalence check with no compare points is not a proof; give lec a module "
                      "that exists on both sides and drives at least one output or state cell.",
                      r.detail)};
    }
    if (r.verdict == livehd::lec::Verdict::Unknown) {
      // REFUTED above disproves equivalence (a real counterexample → hard fail).
      // UNKNOWN is the solver giving up: it found NO counterexample but could not
      // complete the proof. It is STILL a hard failure, because it PROVED NOTHING and
      // an exit-0 inconclusive is indistinguishable from a real proof to any gate
      // built on this run (user ruling: "an inconclusive should be a fail, user can
      // ignore but not be the default"). `formal.strict` defaults TRUE and is the
      // opt-out: setting it false downgrades this to the loud warning below. A
      // non-empty witness fails regardless of the knob — the miter surfaced an actual
      // diff (an incomplete-correspondence partial miter, or an `auto` run whose ind
      // refuted while bmc could not clear it), which is a potential discrepancy, not
      // mere ignorance. The exit CLASS still distinguishes the two: an undecided run
      // exits `unsupported` (7), a disproof exits `equiv_fail` (10) — UNKNOWN must
      // never be conflated with REFUTED even though both now fail.
      // An encoder REFUSAL (a cell the encoder does not model) is NOT the solver
      // giving up: nothing was compared, so the miter decided nothing and no
      // extra budget can change it. Hard-fail regardless of `formal.strict`, or
      // the exit-0 "inconclusive" reads as a PASS and every gate built on this
      // run is vacuous (2f-latch M0).
      if (r.unsupported) {
        throw Lhd_error{"unsupported",
                        std::format("lec REFUSED '{}': the encoder does not model a cell in this design", impl_g->get_name()),
                        std::format("{}. This is a REFUSAL, not a timeout: nothing was compared, so the run proves "
                                    "nothing. Raising formal.timeout cannot help.",
                                    r.detail)};
      }
      if (o.strict || !r.witness.empty()) {
        throw Lhd_error{"unsupported",
                        std::format("lec could not decide equivalence of '{}'", impl_g->get_name()),
                        std::format("{}{}. This is NOT a disproof either — the solver ran out of budget or hit "
                                    "something it cannot complete. Raise formal.timeout/formal.bound, or pass "
                                    "--set formal.strict=false to accept an undecided run as a warning.",
                                    r.detail,
                                    r.witness.empty() ? std::string{} : std::format("; witness: {}", r.witness))};
      }
      // Only reachable when the caller explicitly opted OUT of strict: the default is
      // strict=true, so an undecided run fails above rather than exiting 0 (an
      // inconclusive that exits 0 is indistinguishable from a real proof to any gate
      // built on this run).
      livehd::diag::warn("pass.lec", "inconclusive", "io")
          .msg(
              "lec INCONCLUSIVE: '{}' — the solver could not complete the proof and found NO counterexample ({}). "
              "This is NOT a proof of equivalence; it is only a warning because this run set formal.strict=false.",
              impl_g->get_name(),
              r.detail)
          .emit();
      return;  // clean exit: inconclusive (warning), not a hard error
    }
    return;  // Proven
  }

  auto impl_v = fs::absolute(materialize_verilog(opts, res, opts.impl_kind, opts.impl_path, "impl")).string();
  auto ref_v  = fs::absolute(materialize_verilog(opts, res, opts.ref_kind, opts.ref_path, "ref")).string();
  // cross mode re-materializes both sides through materialize_verilog, which
  // re-records their input paths (load_side_graphs already did above) — collapse
  // res.inputs back to one entry per side (stable, first occurrence wins).
  {
    std::vector<std::string> dedup;
    for (const auto& p : res.inputs) {
      if (std::find(dedup.begin(), dedup.end(), p) == dedup.end()) {
        dedup.push_back(p);
      }
    }
    res.inputs = std::move(dedup);
  }
  auto lgcheck = locate_lgcheck();
  auto yosys   = locate_lgcheck_yosys();
  auto rundir  = fs::absolute(workdir(opts)).string();
  auto cmd     = std::format("cd {} && {} --implementation {} --reference {}",
                             shell_quote(rundir),
                             shell_quote(lgcheck),
                             shell_quote(impl_v),
                             shell_quote(ref_v));
  if (!yosys.empty()) {
    cmd += std::format(" --yosys {}", shell_quote(yosys));
  }
  if (!opts.top.empty()) {
    cmd += std::format(" --top {}", shell_quote(opts.top));
  }
  auto log       = next_log_path(opts, "lec.lgcheck");
  cmd           += std::format(" >> {} 2>&1", shell_quote(fs::absolute(log).string()));
  int  rc        = std::system(cmd.c_str());
  bool lg_equiv  = rc == 0;

  std::print("lec cross-check: engine={} -> {}; lgcheck -> {}\n",
             o.engine,
             lec_known ? (lec_equiv ? "equivalent" : "different") : "unknown",
             lg_equiv ? "equivalent" : "different");

  if (lec_known && lec_equiv != lg_equiv) {
    throw Lhd_error{"internal",
                    std::format("lec engine and lgcheck DISAGREE (engine={}, lgcheck={})",
                                lec_equiv ? "equivalent" : "different",
                                lg_equiv ? "equivalent" : "different"),
                    std::format("see {}", log)};
  }
  if (!lg_equiv) {
    throw Lhd_error{"equiv_fail", std::format("equivalence check failed ({} vs {})", opts.impl_path, opts.ref_path), ""};
  }
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

  const std::string lhd_bin    = file_utils::get_exe_path() + "/lhd";
  const std::string design_dir = opts.workdir + "/formalfail_prp";
  const std::string log        = next_log_path(opts, "formal.simfail");
  if (!lecfail_emit_side(lhd_bin, opts, design_kind, design_path, design_dir, opts.workdir + "/formalfail_w", log)) {
    skip(std::format("the design could not be re-emitted as Pyrope (lg:/yosys-verilog has no LNAST); see {}", log));
    return;
  }
  std::vector<Lecfail_mod> mods = lecfail_parse_dir(design_dir);
  if (mods.empty()) {
    skip("no Pyrope modules were re-emitted for the design");
    return;
  }
  std::string        top = lecfail_simple_name(top_full);
  const Lecfail_mod* m   = nullptr;
  for (const auto& mm : mods) {
    if (mm.name == top) {
      m = &mm;
    }
  }
  if (m == nullptr) {
    skip("could not locate the TOP module in the re-emitted Pyrope");
    return;
  }

  // Import the ORIGINAL source when it is a Pyrope file with a `pub` top (a fix
  // to the .prp then flows into a re-run of the SAME simfail test); else
  // inline the re-emitted copy (self-contained).
  const std::string design_stem = fs::path(design_path).stem().string();
  const bool        can_import  = design_kind == "pyrope" && !design_stem.empty() && lecfail_prp_top_is_pub(design_path, top);

  absl::flat_hash_map<std::string, int> width_of;
  for (const auto& cyc : tr.cycles) {
    for (const auto& in : cyc.inputs) {
      width_of[in.name] = in.width < 1 ? 1 : in.width;
    }
  }
  std::vector<std::string> win;  // the DESIGN's declared inputs, decl order
  for (const auto& [n, t] : m->inputs) {
    win.push_back(n);
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
  const bool reset_is_port  = std::find(win.begin(), win.end(), "reset") != win.end();
  const bool implicit_reset = width_of.count("reset") != 0 && !reset_is_port;

  const std::string callee    = can_import ? std::string{"dutmod"} : top;
  std::string       test_text = std::format("test {} {{\n  mut _dut = {}\n", test_name, callee);
  for (const auto& n : win) {
    std::string arr;
    for (int c = 0; c < ncyc; ++c) {
      arr += (arr.empty() ? "" : ", ") + val_at(n, c);
    }
    test_text += std::format("  const _drv_{} = [{}]\n", n, arr);
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
    test_text += "    _dut.reset = _drv_reset[clock]\n";
  }
  for (const auto& n : win) {
    test_text += std::format("    _dut.{} = _drv_{}[clock]\n", n, n);
  }
  if (!embed_assert.empty()) {
    // The violated formal-block assertion, re-targeted at the instance
    // (`__p_*` idents -> `_dut.<path>` reads): the replay TRIGGERS it through
    // the test-assert machinery at exactly the violating cycle, so the run
    // fails with a located `assert fail: clock=N` line plus the VCD.
    test_text += std::format("    if clock == {} {{\n      {}\n    }}\n", tr.diverge_cycle, embed_assert);
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
  out += test_text;

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
  cmd += shell_quote(simfail_path) + " --set sim.vcd=true --workdir " + shell_quote(opts.workdir);
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

// The machine-readable run report an agent parses each loop iteration: written
// on EVERY run (PROVEN / REFUTED / UNKNOWN alike — UNKNOWN is exactly the case
// the agent must act on), covering every obligation with its verdict, cycles,
// classification, cumulative solve time, timeout-core membership, and the
// witness artifact paths. The stdout table stays the human view; this file is
// the contract (ids are stable: kind@file:line[block]).
static void emit_formal_report(const std::string& path, const std::string& design_path, std::string_view top,
                               const livehd::lec::Verify_result& r, const livehd::lec::Lec_options& o,
                               const absl::flat_hash_map<std::string, std::string>& artifacts) {
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
  const char* agg  = r.verdict == livehd::lec::Verdict::Proven    ? "proven"
                     : r.verdict == livehd::lec::Verdict::Refuted ? "refuted"
                                                                  : "unknown";
  std::string j    = "{\n";
  j               += "  \"schema_version\": 1,\n  \"kind\": \"formal_report\",\n";
  j               += std::format("  \"design\": \"{}\",\n  \"top\": \"{}\",\n", json_esc(design_path), json_esc(top));
  j               += "  \"run\": {\n";
  j               += std::format("    \"verdict\": \"{}\",\n    \"detail\": \"{}\",\n", agg, json_esc(r.detail));
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
      "    \"budget\": {{\"timeout_s\": {}, \"min_timeout_s\": {}, \"rlimit\": {}, "
      "\"spec_mining_timeout_s\": {}, \"spent_ms\": {}, \"units\": {}, \"floored\": {}}},\n",
      o.timeout,
      o.min_timeout,
      o.rlimit,
      o.spec_mining_timeout,
      r.budget_spent_ms,
      r.budget_units,
      r.budget_floored);
  // formal.stats only: the cvc5 solve-insight object. Carries its OWN trailing
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
    const char* verdict    = env_assume                                   ? "in_force"
                             : p.verdict == livehd::lec::Verdict::Proven  ? "proven"
                             : p.verdict == livehd::lec::Verdict::Refuted ? "refuted"
                                                                          : "unknown";
    std::string why;
    if (!env_assume && p.verdict != livehd::lec::Verdict::Proven && p.verdict != livehd::lec::Verdict::Refuted) {
      const bool scope_vacuous = std::find(r.vacuous_scopes.begin(), r.vacuous_scopes.end(), p.scope) != r.vacuous_scopes.end();
      why = p.refuted_at >= 0   ? std::format("violation at cycle {} may be a blackbox artifact", p.refuted_at)
            : p.unknown_at >= 0 ? std::format("solver gave up at cycle {}", p.unknown_at)
            : scope_vacuous     ? (p.scope.empty() ? std::string{"design assume set contradictory"}
                                                   : std::format("assume set of block '{}' contradictory", p.scope))
                                : std::string{"not checked"};
      if (p.kind == "assume") {
        why += "; unproven assume — NOT used";
      }
    }
    // R1 Phase 2: `guarded` says the obligation is `guard implies cond` (written
    // inside an `if`/`match` arm), `vacuous_guard` says that antecedent is
    // unsatisfiable over the checked window — a PROVEN that checked nothing. An
    // agent should treat vacuous_guard like an unproven obligation even though
    // the verdict is honestly "proven".
    j += std::format(
        "    {{\"id\": \"{}\", \"kind\": \"{}\", \"file\": \"{}\", \"line\": {}, \"msg\": \"{}\", "
        "\"block\": \"{}\", \"instance\": \"{}\", \"aclass\": \"{}\", \"verdict\": \"{}\", \"unbounded\": {}, \"proven_to\": {}, "
        "\"refuted_at\": {}, \"unknown_at\": {}, \"unknown_why\": {}, \"solve_ms\": {}, "
        "\"in_timeout_core\": {}, \"guarded\": {}, \"vacuous_guard\": {}, \"witness\": {}}}{}\n",
        json_esc(prop_id(p)),
        json_esc(p.kind),
        json_esc(file),
        line.empty() ? "0" : line,
        json_esc(p.msg),
        json_esc(p.block),
        json_esc(p.instance),
        json_esc(p.aclass),
        verdict,
        p.unbounded ? "true" : "false",
        p.proven_to,
        p.refuted_at,
        p.unknown_at,
        why.empty() ? std::string{"null"} : "\"" + json_esc(why) + "\"",
        p.solve_ms,
        in_core.contains(i) ? "true" : "false",
        p.guarded ? "true" : "false",
        p.vacuous_guard ? "true" : "false",
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
// and per-obligation timeout isolation. Exit policy mirrors lec: only a
// REACHABLE violation hard-fails; bounded-proven passes; unknown is a loud
// warning unless formal.strict. Knobs: formal.* (shared engine), formal.lec.*
// (lec-only), formal.verify.* (verify-only), with lec.* accepted as aliases.
void formal_verify_command(Options& opts, Result& res) {
  // Captured before any workdir() call fabricates a scratch dir: the simfail
  // testbench + VCD default ON only for a persistent, user-named --workdir.
  const bool workdir_set = !opts.workdir.empty();
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
    opts.sets.emplace_back("compile.formal.hier_preflight", "false");
    if (label("assume_check", "true") == "false" || label("assume_check", "true") == "0") {
      opts.sets.emplace_back("compile.formal.assume_check", "false");
    }
    load_side_graphs(opts, res, kind, path, "impl", var);
    opts.sets.resize(saved_sets);
  }

  // Top pick: --impl-top / --top, else the sole module; entity fallback like
  // lec (pick_top_graph warns when the fallback substitutes the full name).
  auto g = pick_top_graph(var, opts.impl_top, opts.top, "", "formal verify", "pass.formal");

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
  o.strict              = label("strict", "true") != "false" && label("strict", "true") != "0";
  o.allow_oversize      = label("allow_oversize", "false") != "false" && label("allow_oversize", "false") != "0";
  o.partitions          = std::atoi(label("partitions", "4").c_str());
  o.jobs                = std::max(1, std::atoi(label("jobs", "4").c_str()));
  o.split               = label("split", "auto");
  o.rlimit              = std::atoi(label("rlimit", "0").c_str());  // deterministic per-query budget (0=off; CI/repro)
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
  // formal.stats: cvc5 solve-insight report; `--stats` is CLI sugar for the same
  // knob, so OR the two rather than letting either spelling clobber the other.
  {
    const std::string stats_label = label("stats", "false");
    o.stats                       = opts.stats || (stats_label != "false" && stats_label != "0");
  }

  std::unique_ptr<livehd::formal::Verdict_cache> vcache;
  if (workdir_set && label("cache", "true") != "false" && label("cache", "true") != "0") {
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
      throw Lhd_error{"unsupported",
                      std::format("formal verify refused '{}': edge normalization declined ({})", g->get_name(), sn.reason),
                      "the design mixes clock edges or holds latches in a shape the normalizer does not model; a "
                      "partial lowering would be a silent full-cycle error, so it declines instead"};
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
    struct Sig {
      int  w;
      bool sgn;
    };
    absl::flat_hash_map<std::string, Sig> in_tbl, out_tbl, flop_tbl;
    {
      auto gio = g->get_io();
      for (const auto& d : gio->get_input_pin_decls()) {
        auto pin       = g->get_input_pin(d.name);
        int  w         = livehd::graph_util::real_width(pin, *gio, d.name);
        // Sign comes from the IO declaration because it is authoritative at the
        // module boundary. Typing an unsigned monitor input as signed would flip
        // ordered comparisons in user properties (assume(x <= 15) could hold
        // vacuously for large x).
        in_tbl[d.name] = Sig{w == 0 ? 1 : w, !gio->is_unsign(d.name)};
      }
      for (const auto& d : gio->get_output_pin_decls()) {
        auto pin        = g->get_output_pin(d.name);
        int  w          = livehd::graph_util::real_width(pin, *gio, d.name);
        out_tbl[d.name] = Sig{w == 0 ? 1 : w, !gio->is_unsign(d.name)};
      }
      for (auto node : g->occurrences().nodes(hhds::Node_order::forward)) {
        if (livehd::graph_util::type_op_of(node) != Ntype_op::Flop) {
          continue;
        }
        auto q = node.get_driver_pin(0);
        if (q.is_invalid()) {
          continue;
        }
        int w                                                        = livehd::graph_util::real_width(q);
        flop_tbl[livehd::lec::canon_flop_name(node.get_hier_name())] = Sig{w == 0 ? 1 : w, !livehd::graph_util::is_unsign(q)};
      }
    }
    auto entity = [](std::string_view n) {
      auto d = n.rfind('.');
      return d == std::string_view::npos ? n : n.substr(d + 1);
    };
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
        // Where the block binds (user ruling, 2026-07-08): the verified top
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
              inst_prefixes.emplace_back(node.get_hier_name());
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
              for (const auto& d : sio->get_input_pin_decls()) {
                sub_port_tbl[d.name] = Sig{d.bits > 0 ? static_cast<int>(d.bits) : 1, !sio->is_unsign(d.name)};
              }
              for (const auto& d : sio->get_output_pin_decls()) {
                sub_port_tbl[d.name] = Sig{d.bits > 0 ? static_cast<int>(d.bits) : 1, !sio->is_unsign(d.name)};
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
          // The temporal vocabulary, all of it depth-1 history except `past`
          // itself. Ports are integers (a 1-bit signal is u1), so a truth test
          // is `!= 0` — pyrope keeps bool and int apart, and `and` is
          // boolean-only, so each operand is a comparison.
          struct Top {
            std::string_view name;
            int              nargs;
          };
          static constexpr std::array<Top, 5> kTemporal{
              {{"past", 2}, {"rose", 1}, {"fell", 1}, {"stable", 1}, {"changed", 1}}
          };

          for (auto& st : blk_stmts) {
            // Scan OUTSIDE string literals: an obligation's message is part of
            // the statement text, so `assert(..., "past(x, 0) is x")` would
            // otherwise be read as a call and rejected for naming no signal.
            bool in_str = false;
            for (size_t pos = 0; pos < st.text.size();) {
              const char c = st.text[pos];
              if (in_str && c == '\\') {
                pos += 2;  // escaped char inside the message
                continue;
              }
              if (c == '"') {
                in_str = !in_str;
                ++pos;
                continue;
              }
              if (in_str) {
                ++pos;
                continue;
              }
              const Top* op = nullptr;
              for (const auto& t : kTemporal) {
                if (st.text.compare(pos, t.name.size(), t.name) == 0 && pos + t.name.size() < st.text.size()
                    && st.text[pos + t.name.size()] == '(') {
                  op = &t;
                  break;
                }
              }
              // Only a bare call, not the tail of a longer identifier.
              if (op == nullptr
                  || (pos > 0 && (std::isalnum(static_cast<unsigned char>(st.text[pos - 1])) != 0 || st.text[pos - 1] == '_'))) {
                ++pos;
                continue;
              }
              // Balanced scan: `past(rose(x), 1)` must not stop at rose's `)`.
              // Such a nesting is refused below (these take a SIGNAL, not an
              // expression), but the diagnosis has to name the real call.
              const size_t open  = pos + op->name.size();
              size_t       close = std::string::npos;
              for (size_t i = open, depth = 0; i < st.text.size(); ++i) {
                if (st.text[i] == '(') {
                  ++depth;
                } else if (st.text[i] == ')' && --depth == 0) {
                  close = i;
                  break;
                }
              }
              if (close == std::string::npos) {
                throw Lhd_error{"usage",
                                std::format("formal block '{}': unterminated `{}(` in a property", blk.name, op->name),
                                "temporal operators take one signal, e.g. rose(x) or past(x, 2)"};
              }
              const std::string inner = st.text.substr(open + 1, close - open - 1);
              const auto        comma = inner.find(',');
              const std::string arg   = trim(comma == std::string::npos ? inner : inner.substr(0, comma));
              const std::string cnt   = comma == std::string::npos ? std::string{} : trim(inner.substr(comma + 1));
              if ((op->nargs == 2) != (comma != std::string::npos)) {
                throw Lhd_error{"usage",
                                std::format("formal block '{}': `{}` takes {} argument(s)", blk.name, op->name, op->nargs),
                                "past(x, 2) samples 2 cycles back; rose/fell/stable/changed take just the signal"};
              }
              int n = 1;
              if (op->nargs == 2) {
                if (cnt.empty() || cnt.find_first_not_of("0123456789") != std::string::npos) {
                  throw Lhd_error{"usage",
                                  std::format("formal block '{}': past() depth '{}' is not a literal cycle count", blk.name, cnt),
                                  "write past(x, 2) — the depth must be a compile-time number"};
                }
                n = std::stoi(cnt);
              }
              const auto* base = idx_of(arg);
              if (base == nullptr) {
                throw Lhd_error{"usage",
                                std::format("formal block '{}': `{}` argument must be one signal the block names, got '{}'",
                                            blk.name,
                                            op->name,
                                            arg),
                                "rose(acc.req) / past(acc.req, 2) are supported; an expression like rose(a and b) is not"};
              }
              // COPY the base ident first: hist_port may push a new history port
              // into blk_inputs, and that reallocation invalidates `base`.
              const std::string base_ident = base->ident;
              const std::string p1         = hist_port(*base, n);
              std::string       repl;
              if (op->name == "past") {
                repl = p1;
              } else if (op->name == "rose") {
                repl = std::format("(({} == 0) and ({} != 0))", p1, base_ident);
              } else if (op->name == "fell") {
                repl = std::format("(({} != 0) and ({} == 0))", p1, base_ident);
              } else if (op->name == "stable") {
                repl = std::format("({} == {})", base_ident, p1);
              } else {  // changed
                repl = std::format("({} != {})", base_ident, p1);
              }
              st.text.replace(pos, close - pos + 1, repl);
              if (p1 != base_ident) {
                st.idents.push_back(p1);
              }
              pos += repl.size();
            }
          }
        }

        // Port list + widths from the FIRST context (same module def => same
        // widths in every instance); binds built per instance below.
        livehd::lec::Monitor mon;
        mon.block = blk.name;
        // Assume scope = the authored block. Every instance context below copies
        // it unchanged (only `block` gains the @instance label), so one block's
        // N instances share one assume set while a sibling block never sees it.
        mon.scope = blk.name;
        std::string ports;
        for (const auto& in : blk_inputs) {
          livehd::lec::Monitor::Bind b;
          b.ident      = in.ident;
          b.delay      = in.delay;
          const Sig* s = resolve(in.path, inst_prefixes.front(), b);
          if (s == nullptr) {
            throw Lhd_error{
                "usage",
                std::format("formal block '{}': signal path '{}' does not resolve in '{}'{}",
                            blk.name,
                            in.path,
                            g->get_name(),
                            inst_prefixes.front().empty() ? std::string{} : " instance '" + inst_prefixes.front() + "'"),
                "blocks reach top input/output ports, registers (dotted through instances), and — for a "
                "submodule-bound block — the target instance's ports; internal wires and memory "
                "elements come later"};
          }
          mon.binds.push_back(std::move(b));
          ports += std::format("{}{}:{}{}", ports.empty() ? "" : ", ", in.ident, s->sgn ? "s" : "u", s->w);
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
        std::string src      = std::format("comb __fbmon({}) -> (__fb_ok:bool) {{\n", ports);
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
        load_side_graphs(opts, res, "pyrope", genp.string(), "impl", mvar);
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
          throw Lhd_error{"unsupported",
                          std::format("formal block '{}': the property holds STATE, which is not implemented", blk.name),
                          "a formal-block property must be a combinational function of the signals it names. Temporal "
                          "operators (`past`, `rose`, `fell`, `stable`, `changed`, `eventually`, `always`) are not "
                          "implemented for formal verification yet; note the pipelining `past[N](x)` is a delay stage, "
                          "not a history sample, and cannot be used here"};
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
    throw Lhd_error{"unsupported",
                    std::format("formal verify: '{}' needed edge normalization (P={}) and also has {} formal block "
                                "monitor(s), which cannot be gated to the period boundary",
                                g->get_name(),
                                single_edge_slots,
                                mons.size()),
                    "state the property as a design-body assert (it is gated automatically), or drop the formal block"};
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
  if (r.oversize_refused) {
    throw Lhd_error{"unsupported",
                    std::format("formal verify refused '{}': {}", g->get_name(), r.detail),
                    "set formal.allow_oversize=true to run it anyway (it may exhaust host memory)"};
  }
  if (vcache) {
    vcache->save();
    if (vcache->hits() > 0 || vcache->stores() > 0) {
      std::print("formal[cache]: {} obligation hit(s), {} stored ({}/formal_cache.json)\n",
                 vcache->hits(),
                 vcache->stores(),
                 opts.workdir);
    }
  }

  const char* verdict = r.verdict == livehd::lec::Verdict::Proven    ? "PROVEN (bounded)"
                        : r.verdict == livehd::lec::Verdict::Refuted ? "REFUTED"
                                                                     : "UNKNOWN";
  std::print("formal verify: '{}' {} ({}; {} ms)\n", g->get_name(), verdict, r.detail, r.elapsed_ms);
  std::string first_fail;                 // the exit policy's headline: the first refuted obligation
  bool        first_fail_assume = false;  // headline is a refuted assume-check, not a design violation
  for (const auto& p : r.props) {
    std::string where = p.loc.empty() ? std::string{} : " at " + p.loc;
    std::string msg   = p.msg.empty() ? std::string{} : " \"" + p.msg + "\"";
    if (!p.block.empty()) {
      msg += " [" + p.block + "]";  // block (+@instance) attribution
    } else if (!p.instance.empty()) {
      msg += " [" + p.instance + "]";
    }
    // R1 Phase 2 — an obligation whose GUARD can never hold proved trivially: it
    // is honestly true and honestly useless. Printed as a CONTINUATION of the
    // verdict row it qualifies (same shape as the witness lines below), never
    // as a replacement — vacuity is a diagnostic, not a downgrade. A vacuous
    // antecedent leaves the obligation genuinely TRUE; only a contradictory
    // assume set makes a proof unsound to rely on.
    auto vacuity_note = [&p]() {
      if (p.vacuous_guard) {
        std::print(
            "    VACUOUS: its `if`/`match` guard can never be true, so the property is never exercised — the "
            "branch is dead (fix the guard condition, or drop the branch)\n");
      }
    };
    if (p.kind == "assume" && livehd::lec::is_unchecked_assume_class(p.aclass)) {
      const char* why = p.aclass == "top_input"        ? "top-level IO assume cannot be checked; treated as assume_nocheck"
                        : p.aclass == "check_disabled" ? "formal.assume_check=false; treated as assume_nocheck"
                                                       : "assume_nocheck";
      std::print("  assume{}{}: in force (UNCHECKED {}; verdicts are conditional and unchecked)\n", where, msg, why);
      vacuity_note();  // an env assume whose guard never holds constrains nothing
      continue;
    }
    // Asserts AND checked assumes (prove-then-use): the same verdict rows.
    switch (p.verdict) {
      case livehd::lec::Verdict::Proven:
        if (p.unbounded) {
          std::print("  {}{}{}: PROVEN (inductive — every cycle of every bound)\n", p.kind, where, msg);
        } else {
          std::print("  {}{}{}: PROVEN to cycle {} (bounded)\n", p.kind, where, msg, p.proven_to);
        }
        break;
      case livehd::lec::Verdict::Refuted:
        std::print("  {}{}{}: REFUTED at cycle {}\n", p.kind, where, msg, p.refuted_at);
        if (!p.witness.empty()) {
          std::print("    counterexample inputs: {}\n", p.witness);
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
        // A contradictory assume set is now attributed to the SCOPE that owns
        // it, so the message names the block to fix instead of blaming the run.
        const bool  scope_vacuous = std::find(r.vacuous_scopes.begin(), r.vacuous_scopes.end(), p.scope) != r.vacuous_scopes.end();
        std::string why = p.refuted_at >= 0   ? std::format("violation at cycle {} may be a blackbox artifact", p.refuted_at)
                          : p.unknown_at >= 0 ? std::format("solver gave up at cycle {} (raise --set formal.timeout)", p.unknown_at)
                          : scope_vacuous ? (p.scope.empty() ? std::string{"the design's own assume set is contradictory"}
                                                             : std::format("assume set of block '{}' is contradictory", p.scope))
                                          : std::string{"not checked"};
        if (p.kind == "assume") {
          why += "; unproven assume — NOT used (make it provable, or spell assume_nocheck to impose it UNCHECKED)";
        }
        std::print("  {}{}{}: UNKNOWN ({})\n", p.kind, where, msg, why);
        if (!p.witness.empty()) {
          std::print("    candidate violation inputs: {}\n", p.witness);
        }
        break;
      }
    }
    vacuity_note();
  }

  // formal.stats: the cvc5 solve-insight report for the whole verify run (one
  // solver per strategy, every obligation), printed under the obligation table.
  if (o.stats) {
    livehd::lec::report_cvc5_stats("formal", r.cvc5);
  }

  // simfail simulation tests + VCDs (`--workdir`, shared with LEC): emit one
  // per refuted formal test that carries a trace (the first failed obligation
  // within each test supplies it). A design-body assertion is one implicit test
  // named after the resolved top.
  std::vector<std::string> simfail_artifact_paths;
  if (r.verdict == livehd::lec::Verdict::Refuted) {
    const bool simfail = o.witness && workdir_set && setting_enabled(labels.contains("simfail") ? labels.at("simfail") : "true");
    const bool simfail_run = setting_enabled(labels.contains("simfail_run") ? labels.at("simfail_run") : "true");
    const bool has_trace
        = std::any_of(r.props.begin(), r.props.end(), [](const auto& p) { return p.refuted_at >= 0 && !p.trace.empty(); });
    if (has_trace && simfail && single_edge_slots > 1) {
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
  // Written on EVERY run — UNKNOWN included (that is the verdict the agent must
  // act on) — BEFORE the exit-policy throws below, so a REFUTED run still leaves
  // the report. Pass --workdir to keep it across runs; without one, the file
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
      emit_formal_report(rpath, path, g->get_name(), r, o, artifacts);
      if (fs::exists(rpath)) {
        res.outputs.push_back(rpath);
        std::print("formal verify: wrote report {}\n", rpath);
      }
    }
  }

  // R1 Phase 2 — ANTECEDENT vacuity. Independent of the run verdict (the usual
  // case is a PROVEN run), so this sits ahead of the verdict ladder below.
  //
  // Severity ruling: WARNING by default, failure under `formal.strict`. It is
  // deliberately NOT the hard error a contradictory assume set gets: that one
  // makes every proof it governed unsound to rely on, whereas a vacuous
  // antecedent leaves the obligation genuinely true — it just proved nothing.
  // And a guard unreachable at THIS top can be perfectly reachable under a
  // different parent instantiation, which is a legitimate design pattern; a
  // hard error would punish it. `formal.strict` is the existing "treat a
  // proves-nothing outcome as a failure" knob, so it is the right lever.
  std::string strict_vacuous;  // set below; thrown only after the verdict ladder
  {
    std::string vac_list;
    int         n_vac = 0;
    for (const auto& p : r.props) {
      if (!p.vacuous_guard) {
        continue;
      }
      // A non-internal assume whose guard is dead constrains nothing — worth
      // the row and the warning, but it must NOT gate `formal.strict`: the
      // compile tier does not count assumes in its vacuity accounting, and the
      // two tiers must agree on whether the same source is clean (an
      // input-class assume is checked as an obligation now, but its dead-guard
      // severity keeps this ruling). Only internal-class assumes gate the exit.
      if (p.kind == "assume" && p.aclass != "internal") {
        continue;
      }
      ++n_vac;
      vac_list += (vac_list.empty() ? "" : ", ") + p.kind + (p.loc.empty() ? std::string{} : " at " + p.loc)
                  + (p.msg.empty() ? std::string{} : " \"" + p.msg + "\"");
    }
    if (n_vac > 0) {
      // The WARNING is emitted here so it is visible even on a run that goes on
      // to fail for a worse reason. The strict FAILURE is deferred to after the
      // verdict ladder below: throwing here pre-empted every more severe exit
      // class, so a design with BOTH a reachable violation and a dead branch
      // exited "unsupported: 1 VACUOUS obligation(s)" and the equiv_fail plus
      // its counterexample trace were never printed. Same masking applied to an
      // encoder refusal and to a contradictory assume set.
      strict_vacuous = std::format("formal verify: {} VACUOUS obligation(s) in '{}' — {}", n_vac, g->get_name(), vac_list);
      livehd::diag::warn("pass.formal", "formal-vacuous-guard", "io")
          .msg(
              "formal verify: {} obligation(s) proved VACUOUSLY in '{}' ({}) — the `if`/`match` guard can never be "
              "true, so the property is never exercised and its PROVEN means nothing. Fix the guard condition or drop "
              "the dead branch; under the default formal.strict=true this also FAILS the run (pass --set "
              "formal.strict=false to keep it a warning).",
              n_vac,
              g->get_name(),
              vac_list)
          .emit();
    }
  }

  if (r.verdict == livehd::lec::Verdict::Refuted) {
    if (first_fail_assume) {
      // The headline failure is an assume whose CHECK refuted — the design
      // violated nothing; the verification collateral asks for a constraint it
      // cannot justify. Name that, or the user reads "property violation" and
      // hunts a design bug that does not exist.
      throw Lhd_error{"equiv_fail",
                      std::format("'{}' has an assume that fails its check ({})", g->get_name(), first_fail),
                      "every assume is CHECKED as an assert before it is used; if this one is an intended free "
                      "environment constraint, spell it assume_nocheck(...) — it is then assumed WITHOUT check and "
                      "disclosed"};
    }
    throw Lhd_error{"equiv_fail",
                    std::format("'{}' has a reachable property violation ({})", g->get_name(), first_fail),
                    "the per-cycle input trace above reproduces it from reset"};
  }
  if (r.verdict == livehd::lec::Verdict::Unknown) {
    // An encoder REFUSAL is not a solver give-up: no obligation was ever
    // encoded, so the run proved nothing and a bigger budget cannot change
    // that. It must be a hard error regardless of `formal.strict` — an exit-0
    // warning here reads downstream as "verified" and makes every gate built on
    // this run vacuous (2f-latch M0). The report is already written above, so
    // the agent-loop artifact still exists on this path.
    if (r.unsupported) {
      throw Lhd_error{"unsupported",
                      std::format("formal verify REFUSED '{}': the encoder does not model a cell in this design", g->get_name()),
                      std::format("{}. This is a REFUSAL, not a timeout: no obligation was checked, so the run proves "
                                  "nothing. Raising formal.timeout cannot help.",
                                  r.detail)};
    }
    // A CONTRADICTORY assume set is not a solver give-up either: every proof it
    // governed was vacuous, so the run proved nothing, and it is the USER's
    // input that is wrong — a bigger budget cannot help. Hard error regardless
    // of `formal.strict`, for the same reason an encoder refusal is: an exit-0
    // warning here reads downstream as "verified", and it silently turns a
    // genuinely REFUTED design green (the assumes prune the counterexample away).
    if (r.vacuous) {
      std::string which;
      for (const auto& s : r.vacuous_scopes) {
        which += (which.empty() ? "" : ", ") + (s.empty() ? std::string{"the design itself"} : "block '" + s + "'");
      }
      throw Lhd_error{"usage",
                      std::format("formal verify: contradictory assume set in {} — every proof under it is VACUOUS",
                                  which.empty() ? std::string{"the design"} : which),
                      "no obligation was really discharged: an unsatisfiable assume set proves anything. Fix the "
                      "conflicting assumes (each block is scoped independently, so only the named one needs it)"};
    }
    if (o.strict) {
      throw Lhd_error{"unsupported",
                      std::format("formal verify could not decide '{}'", g->get_name()),
                      std::format("{}. This is NOT a disproof either — the solver ran out of budget or hit something it "
                                  "cannot complete. Raise formal.timeout/formal.bound, or pass --set formal.strict=false "
                                  "to accept an undecided run as a warning.",
                                  r.detail)};
    }
    // Only reachable when the caller explicitly opted OUT of strict: the default is
    // strict=true, so an undecided run fails above rather than exiting 0 (an
    // inconclusive that exits 0 is indistinguishable from a real proof to any gate
    // built on this run).
    livehd::diag::warn("pass.formal", "formal-inconclusive", "io")
        .msg(
            "formal verify INCONCLUSIVE: '{}' — {}. This proves nothing and disproves nothing; it is only a warning "
            "because this run set formal.strict=false.",
            g->get_name(),
            r.detail)
        .emit();
  }
  // LAST: a vacuous obligation under formal.strict fails the run, but only once
  // nothing more severe has claimed the exit (see where strict_vacuous is set).
  if (!strict_vacuous.empty() && o.strict) {
    throw Lhd_error{"unsupported",
                    strict_vacuous,
                    "each proved only because its `if`/`match` guard can never be true, so it checked nothing: the "
                    "branch is dead. Fix the guard condition, or drop the branch"};
  }
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

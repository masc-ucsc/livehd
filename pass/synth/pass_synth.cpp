// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "pass_synth.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <print>

#include "abc_region.hpp"
#include "abc_tmap.hpp"
#include "diag.hpp"
#include "file_utils.hpp"
#include "json_util.hpp"
#include "pass_abc.hpp"
#include "provenance.hpp"
#include "synth_salt.hpp"
#include "template_cache.hpp"

static Pass_plugin plugin("pass_synth", Pass_synth::setup);

void Pass_synth::setup() {
  Eprp_method m("pass.synth",
                "Unate synthesis: decompose every region into the fewest unate functions, then ABC tmap (technology "
                "mapping only) of each function; a region the optimizer cannot handle falls back to the ABC flow",
                &Pass_synth::work);
  Pass_abc::add_mapping_labels(m);
  m.add_label_optional("support", "Maximum logical inputs of one unate function (2..12)", "6");
  m.add_label_optional("literals", "Maximum literal occurrences in one unate function's SOP (2..4096)", "16");
  m.add_label_optional("series", "Maximum literals in one product term, i.e. series stack depth (2..32)", "4");
  m.add_label_optional("max_depth",
                       "Maximum unate-function network depth (logical levels, no pipeline stages); 0 = unbounded, so every "
                       "cone decomposes and the objective is the fewest functions",
                       "0");
  m.add_label_optional("split",
                       "true: per-cone bounded split -- each output cone gets at most 3 gates (unate functions); a larger "
                       "cone keeps its top 2 gates and the logic below them is a remainder that ABC only technology-maps. "
                       "false: cover the whole region with the fewest unate functions",
                       "false");
  m.add_label_optional("split_exact", "split mode: exact minimum-literal gate SOPs (false: greedy cover)", "true");
  m.add_label_optional("split_factor",
                       "split mode: a gate's literal limit applies to its algebraically factored form (the series-parallel "
                       "pull-down network) rather than its SOP",
                       "true");
  m.add_label_optional("split_cut",
                       "split mode: widest cut decomposed as two gates G(H(B), rest) with H over a bound set B of shared "
                       "variables (support..12; support disables)",
                       "10");
  m.add_label_optional("split_share", "split mode: prefer a bound-set gate H that another node already uses", "true");
  m.add_label_optional("domino_overhead",
                       "cover mode: transistors of a domino gate besides its factored pull-down literals (precharge, foot, "
                       "keeper, output inverter)",
                       "5");
  m.add_label_optional("cmos_factor", "cover mode: static CMOS transistors per factored literal of a non-unate LUT", "2");
  m.add_label_optional("static_overhead", "cover mode: transistors of a static LUT besides its literals (output inverter)", "2");
  m.add_label_optional("nonunate_penalty", "cover mode: multiplier on a non-unate (static) LUT's transistors", "2");
  m.add_label_optional("cover_cuts", "cover mode: priority cuts kept per node (1..64)", "12");
  m.add_label_optional("domino_levels",
                       "cover mode: an output buildable in at most this many chained domino gates must be built so (2 = "
                       "one cycle, a gate per half); deeper outputs only minimize transistors; 0 ignores depth",
                       "2");
  m.add_label_optional("depth_slack",
                       "cover mode: >= 0 also requires every deeper output at its minimum domino depth plus this many "
                       "levels (depth-optimal, then area recovery); -1 leaves deeper outputs to transistor count only",
                       "-1");
  m.add_label_optional("duplicate",
                       "cover mode: false = never compute logic twice: a gate absorbs a multi-reader node only when all its "
                       "readers are inside it (reconvergent fan-out), otherwise the node is a gate output",
                       "true");
  m.add_label_optional("cover_memories",
                       "cover mode: false leaves blasted memory regions (register files) to the pass.abc flow",
                       "true");
  m.add_label_optional("fanout_boundary",
                       "cover mode: a node with at least this many sinks is always a LUT boundary, never replicated inside "
                       "its readers' LUTs (0: the cost decides)",
                       "0");
  m.add_label_optional("reference",
                       "cover mode: also map each region with ABC `&if -K support -a` and classify its LUTs with the same "
                       "costs (insight only; never the netlist); dch: after `&dch` structural choices",
                       "false");
  m.add_label_optional("abc",
                       "cover mode hand-off to ABC: gate (each LUT technology-mapped on its own), tmap (the cover network, "
                       "gates as minimum SOPs, technology-mapped only), opt (that network through the full pass.abc flow), "
                       "only (no cover: the original region logic through the pass.abc flow)",
                       "gate");
  m.add_label_optional("ware_trials",
                       "true: pass.abc's ware trials re-map each arithmetic ware region under alternative architectures "
                       "(re-running this mapper) and keep the best",
                       "false");
  m.add_label_optional("work", "Deterministic unate search work per region; when it runs out, the remaining nodes use their own fan-in cut", "5000000");
  m.add_label_optional("cuts", "Maximum retained cuts per node (1..256)", "32");
  m.add_label_optional("recovery_rounds", "Shared-function recovery sweeps (0..8; 0 disables)", "2");
  m.add_label_optional("joint_limit",
                       "Maximum complete retained-choice combinations for joint recovery (0..65536; 0 disables)",
                       "256");
  m.add_label_optional("joint_windows",
                       "Maximum related-choice windows when full joint enumeration exceeds joint_limit (0..256)",
                       "8");
  m.add_label_optional("image_inputs", "Original-source enumeration bound for image/dependency proofs (0..12; 0 disables)", "8");
  m.add_label_optional("reshape_limit", "Maximum associative output roots for depth regrouping (0..4096; 0 disables)", "32");
  m.add_label_optional("encoding_limit", "Maximum shared cofactor bound-set proposals for uncovered outputs (0..4096)", "16");
  m.add_label_optional("encoding_code_limit", "Maximum injective class-code assignments per bound-set proposal (1..4096)", "8");
  m.add_label_optional("encoding_pair_limit", "Maximum disjoint bound-set pairs after single-set encoding search (0..4096)", "16");
  m.add_label_optional("symbolic_nodes", "ROBDD node bound above image_inputs (0..65536; 0 disables symbolic queries)", "4096");
  m.add_label_optional("cover_limit", "Maximum divisor sets per uncovered output (0..4096; 0 disables)", "32");
  m.add_label_optional("divisor_limit", "Maximum paid-divisor sets per demanded function (0..4096; 0 disables)", "64");
  m.add_label_optional("max_nodes", "Maximum source objects (blast-tape gates, then hashed source nodes) admitted to unate optimization (larger regions use the ABC flow)", "100000");
  m.add_label_optional("witness_bytes",
                       "Maximum total JSONL witness bytes for this invocation (0..67108864; 0 disables)",
                       "16777216");
  m.add_label_optional("timing_files", "INTERNAL common Liberty/SDC/SPEF environment from synth.*", "");
  m.add_label_optional("invocation_context", "INTERNAL kernel invocation and observed input provenance", "");
  register_pass(m);
}

namespace {
template <typename T>
bool number(std::string_view text, T& value) {
  const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
  return error == std::errc{} && end == text.data() + text.size();
}

bool read_options(const Eprp_var& var, livehd::synth::Search_options& options) {
  livehd::synth::Recipe recipe;
  if (!number(var.get_stage("work", "5000000"), options.work) || !number(var.get_stage("cuts", "32"), options.cuts_per_node)
      || !number(var.get_stage("recovery_rounds", "2"), options.recovery_rounds) || options.recovery_rounds > 8
      || !number(var.get_stage("joint_limit", "256"), options.joint_limit) || options.joint_limit > 65536
      || !number(var.get_stage("joint_windows", "8"), options.joint_windows) || options.joint_windows > 256
      || !number(var.get_stage("image_inputs", "8"), options.image_inputs) || options.image_inputs > 12
      || !number(var.get_stage("symbolic_nodes", "4096"), options.symbolic_nodes)
      || options.symbolic_nodes > livehd::synth::max_symbolic_nodes
      || !number(var.get_stage("reshape_limit", "32"), options.reshape_limit) || options.reshape_limit > 4096
      || !number(var.get_stage("encoding_limit", "16"), options.encoding_limit) || options.encoding_limit > 4096
      || !number(var.get_stage("encoding_code_limit", "8"), options.encoding_code_limit) || options.encoding_code_limit == 0
      || options.encoding_code_limit > 4096 || !number(var.get_stage("encoding_pair_limit", "16"), options.encoding_pair_limit)
      || options.encoding_pair_limit > 4096 || !number(var.get_stage("cover_limit", "32"), options.cover_limit)
      || options.cover_limit > 4096 || !number(var.get_stage("divisor_limit", "64"), options.divisor_limit)
      || options.divisor_limit > 4096 || !number(var.get_stage("max_nodes", "100000"), options.max_nodes)
      || options.cuts_per_node == 0 || options.cuts_per_node > 256 || options.max_nodes == 0
      || !number(var.get_stage("support", "6"), recipe.support) || recipe.support < 2 || recipe.support > 12
      || !number(var.get_stage("literals", "16"), recipe.literals) || recipe.literals < 2 || recipe.literals > 4096
      || !number(var.get_stage("series", "4"), recipe.series) || recipe.series < 2 || recipe.series > 32
      || !number(var.get_stage("max_depth", "0"), recipe.levels)) {
    return false;
  }
  const auto split = std::string{var.get_stage("split", "false")};
  if (split != "true" && split != "false" && split != "1" && split != "0") {
    return false;
  }
  options.split = split == "true" || split == "1";
  const auto flag = [&](std::string_view label, const char* dflt, bool& out) {
    const auto v = std::string{var.get_stage(label, dflt)};
    out          = v == "true" || v == "1";
    return v == "true" || v == "false" || v == "1" || v == "0";
  };
  auto& cc = options.cover_cost;
  if (!number(var.get_stage("domino_overhead", "5"), cc.domino_overhead) || !number(var.get_stage("cmos_factor", "2"), cc.cmos_factor)
      || !number(var.get_stage("static_overhead", "2"), cc.static_overhead)
      || !number(var.get_stage("nonunate_penalty", "2"), cc.nonunate_penalty) || cc.domino_overhead > 1000
      || cc.cmos_factor > 100 || cc.static_overhead > 1000 || cc.nonunate_penalty > 100
      || !number(var.get_stage("cover_cuts", "12"), options.cover_cuts) || options.cover_cuts == 0 || options.cover_cuts > 64
      || !number(var.get_stage("domino_levels", "2"), options.domino_levels) || options.domino_levels > 64
      || !number(var.get_stage("fanout_boundary", "0"), options.fanout_boundary) || !flag("duplicate", "true", options.duplicate)
      || !flag("cover_memories", "true", options.cover_memories)
      || !number(var.get_stage("depth_slack", "-1"), options.depth_slack) || options.depth_slack < -1 || options.depth_slack > 64 ) {
    return false;
  }
  const auto reference = std::string{var.get_stage("reference", "false")};
  if (reference != "true" && reference != "false" && reference != "dch" && reference != "1" && reference != "0") {
    return false;
  }
  options.reference         = reference != "false" && reference != "0";
  using Abc_mode   = livehd::synth::Search_options::Abc_mode;
  const auto abc   = std::string{var.get_stage("abc", "gate")};
  if (abc == "gate") {
    options.abc_mode = Abc_mode::gate;
  } else if (abc == "tmap") {
    options.abc_mode = Abc_mode::tmap;
  } else if (abc == "opt") {
    options.abc_mode = Abc_mode::opt;
  } else if (abc == "only") {
    options.abc_mode = Abc_mode::only;
  } else if (abc == "source") {
    options.abc_mode = Abc_mode::source;
  } else if (abc == "source_tmap") {
    options.abc_mode = Abc_mode::source_tmap;
  } else {
    return false;
  }
  options.reference_choices = reference == "dch";
  if (!flag("split_exact", "true", options.split_exact) || !flag("split_factor", "true", options.split_factor)
      || !flag("split_share", "true", options.split_share) || !number(var.get_stage("split_cut", "10"), options.split_cut)
      || options.split_cut > 12) {
    return false;
  }
  options.recipes = {recipe};
  return true;
}

// Every region row carries exactly one `"status":"unate"|"abc_fallback"`.
std::pair<uint64_t, uint64_t> count_status(const std::vector<std::string>& rows) {
  uint64_t unate = 0, fallback = 0;
  for (const auto& row : rows) {
    unate += row.find(R"("status":"unate")") != std::string::npos;
    fallback += row.find(R"("status":"abc_fallback")") != std::string::npos;
  }
  return {unate, fallback};
}

// Sum one numeric field over the rows (region JSON is flat for these keys).
double sum_field(const std::vector<std::string>& rows, std::string_view key) {
  double     total = 0;
  const auto tag   = std::format("\"{}\":", key);
  for (const auto& row : rows) {
    const auto pos = row.find(tag);
    if (pos != std::string::npos) {
      total += std::strtod(row.c_str() + pos + tag.size(), nullptr);
    }
  }
  return total;
}
}  // namespace

void Pass_synth::work(Eprp_var& var) {
  namespace fs = std::filesystem;
  livehd::synth::Search_options search;
  uint64_t                      witness_bytes = 16777216;
  if (!read_options(var, search) || !number(var.get_stage("witness_bytes", "16777216"), witness_bytes)
      || witness_bytes > 67108864) {
    livehd::diag::err("pass.synth", "invalid-search-options", "syntax")
        .msg(
            "invalid unate search bounds; expected support=2..12, literals=2..4096, series=2..32, max_depth>=0 (0 = "
            "unbounded), cuts=1..256, recovery_rounds=0..8, joint_limit=0..65536, joint_windows=0..256, image_inputs=0..12, "
            "divisor_limit=0..4096, cover_limit=0..4096, symbolic_nodes=0..65536, encoding_limit=0..4096, "
            "encoding_pair_limit=0..4096, encoding_code_limit=1..4096, reshape_limit=0..4096, split=true|false and "
            "witness_bytes=0..67108864")
        .emit();
    return;
  }
  // Reports sit next to the QoR file (or the netlist). A statistics-only run
  // (no out/qor) keeps them in a private scratch directory.
  const auto output = std::string(var.get("out", ""));
  const auto qor    = std::string(var.get("qor", ""));
  fs::path   scratch;
  auto       base = !qor.empty() ? qor : output;
  if (base.empty()) {
    auto pattern = (fs::temp_directory_path() / "livehd-synth-XXXXXX").string();
    if (!mkdtemp(pattern.data())) {
      livehd::diag::err("pass.synth", "scratch-create", "io").msg("cannot create a synthesis scratch directory").fatal();
      return;
    }
    scratch = pattern;
    base    = (scratch / "qor.json").string();
  }
  struct Scratch_guard {
    fs::path path;
    ~Scratch_guard() {
      if (!path.empty()) {
        std::error_code error;
        fs::remove_all(path, error);
      }
    }
  } scratch_guard{scratch};
  fs::create_directories(fs::absolute(base).parent_path());
  const auto witness_path    = base + ".witness.jsonl";
  const auto provenance_path = fs::path(base + ".provenance");
  const auto report_path     = base + ".synth.json";
  {
    std::error_code error;
    fs::remove_all(provenance_path, error);  // archive_provenance needs a fresh directory
  }
  const auto provenance = livehd::synth::archive_provenance(provenance_path,
                                                            var.get("invocation_context", ""),
                                                            std::to_string(livehd::synth::kSynthSrcSalt));
  livehd::synth::Witness_archive witnesses(witness_path, std::to_string(livehd::synth::kSynthSrcSalt), witness_bytes, search);

  std::vector<std::string> reports;
  std::vector<std::string> reused_reports;
  const auto               template_path
      = var.get("cache_dir", "").empty() ? std::string{} : std::string(var.get("cache_dir")) + "_templates.bin";
  const auto              worker_executable = livehd::file_utils::get_exe_path() + "/lhd";
  livehd::synth::Abc_tmap function_backend(worker_executable);
  // Functions map untimed, so identical ones (AND2, OR3, ...) share one mapping:
  // the cache is always on in memory, and persisted only with a cache_dir.
  livehd::synth::Template_cache templates(
      function_backend,
      livehd::synth::template_context(std::string(var.get("library", "")),
                                      "abc-tmap-v2:" + std::to_string(livehd::synth::kSynthSrcSalt)));
  if (!template_path.empty()) {
    templates.load(template_path);
  }
  const auto& recipe = search.recipes.front();
  Pass_abc::work_with(var, [&](livehd::abc::Map_options& opts) {
    // One synthesis tree at a time, including all per-function calls. This
    // deliberately OVERRIDES synth.threads/pass.synth.threads: the region loop
    // owns the process-memory budget that the per-function workers draw on, so
    // a second concurrent region would double-count it.
    opts.threads = 1;
    // A ware trial re-maps a whole region (re-running the unate search) and
    // keeps whichever stitched result is best: it would re-decide the region.
    opts.ware_trials        = var.get_stage("ware_trials", "false") == "true";
    // A split region is a different mapping: never reuse one mode's row for the other.
    opts.alternative_recipe = std::format("{}{}-v5:{}:{},{},{},{}:{}:{}:{}:{}:{}:{}:{}:{}:{}:{}:{}:{}:{}:{}",
                                          opts.ware_trials ? "wt-" : "",
                                          search.split ? std::format("split{}{}{}c{}",
                                                                     search.split_exact ? "e" : "",
                                                                     search.split_factor ? "f" : "",
                                                                     search.split_share ? "s" : "",
                                                                     search.split_cut)
                                                       : std::format("cover{}{}:{}:{}:{}:{}:{}:{}:{}:{}:{}",
                                                                     search.reference ? (search.reference_choices ? "rd" : "r") : "",
                                                                     std::array{"", "-tmap", "-opt", "-only", "-source", "-source_tmap"}[static_cast<int>(search.abc_mode)],
                                                                     search.cover_cost.domino_overhead,
                                                                     search.cover_cost.cmos_factor,
                                                                     search.cover_cost.static_overhead,
                                                                     search.cover_cost.nonunate_penalty,
                                                                     search.cover_cuts,
                                                                     search.domino_levels,
                                                                     search.fanout_boundary,
                                                                     search.depth_slack,
                                                                     std::string{search.duplicate ? "dup" : "nodup"} + (search.cover_memories ? "" : "-nomem")),
                                          livehd::synth::kSynthSrcSalt,
                                          recipe.levels,
                                          recipe.support,
                                          recipe.literals,
                                          recipe.series,
                                          search.work,
                                          search.cuts_per_node,
                                          search.max_nodes,
                                          search.recovery_rounds,
                                          search.joint_limit,
                                          search.joint_windows,
                                          search.image_inputs,
                                          search.divisor_limit,
                                          search.cover_limit,
                                          search.symbolic_nodes,
                                          search.encoding_limit,
                                          search.encoding_pair_limit,
                                          search.encoding_code_limit,
                                          search.reshape_limit);
    opts.alternative = [&](void*                                           frame,
                           void*                                           original,
                           const livehd::abc::Blast_tape&                  tape,
                           std::string_view                                name,
                           const livehd::abc::Map_options&                 mapping,
                           float                                           budget_ps,
                           livehd::abc::Map_options::Alternative_resources resources) {
      const auto witness_begin = witnesses.bytes();
      auto       report
          = livehd::synth::map_abc_region(frame, original, &tape, name, mapping, search, budget_ps, resources, templates, witnesses);
      auto evidence = livehd::synth::pack_evidence(report, witnesses.capture_since(witness_begin));
      std::print("[pass.synth] {}\n", report);
      reports.push_back(std::move(report));
      return evidence;
    };
    opts.alternative_evidence_valid = [](std::string_view cached_region, std::string_view evidence) {
      return livehd::synth::valid_evidence(evidence, cached_region);
    };
    opts.alternative_replay = [&](std::string_view region, std::string_view cached_region, std::string_view evidence) {
      reused_reports.push_back(livehd::synth::replay_evidence(witnesses, region, cached_region, evidence));
    };
  });
  witnesses.close();
  if (livehd::diag::sink().has_halting_errors()) {
    return;
  }
  const auto [unate_regions, fallback_regions] = count_status(reports);
  std::ofstream report(report_path);
  report << std::format(
      R"({{"schema_version":2,"kind":"synth","mode":"{}","recipe":{{"max_depth":{},"support":{},"literals":{},"series":{}}},"totals":{{"regions":{},"unate":{},"abc_fallback":{},"reused":{},"functions":{},"twins":{},"literals":{},"unate_ms":{:.1f},"tmap_ms":{:.1f},"cones_wire":{},"cones_2":{},"cones_3":{},"cones_more":{},"remainder_nodes":{},"remainder_cells":{},"remainder_area":{},"remainder_ms":{:.1f},"split_inverters":{},"bound_gates":{},"shared_bound_gates":{},)"
      R"("domino":{},"nonunate":{},"aliases":{},"cover_cost":{},"domino_cost":{},"nonunate_cost":{},"domino_literals":{},"flow_cost":{},"domino_in2":{},"domino_in3":{},"domino_in4":{},"domino_in5":{},"domino_in6":{},"domino_in7":{},"domino_in8":{},"domino_s1":{},"domino_s2":{},"domino_s3":{},"domino_s4":{},"domino_s5":{},"domino_s6":{},"outputs_shallow":{},"outputs_deep":{},"outputs_wire":{},"covered_nodes":{},"replicated_nodes":{},"ref_luts":{},"ref_domino":{},"ref_nonunate":{},"ref_cost":{},"ref_ms":{:.1f}}},"regions_searched":[)",
      search.split ? "split" : "cover",
      recipe.levels,
      recipe.support,
      recipe.literals,
      recipe.series,
      reports.size() + reused_reports.size(),
      unate_regions,
      fallback_regions,
      reused_reports.size(),
      sum_field(reports, "functions"),
      sum_field(reports, "twins"),
      sum_field(reports, "literals"),
      sum_field(reports, "unate_ms"),
      sum_field(reports, "tmap_ms"),
      sum_field(reports, "cones_wire"),
      sum_field(reports, "cones_2"),
      sum_field(reports, "cones_3"),
      sum_field(reports, "cones_more"),
      sum_field(reports, "remainder_nodes"),
      sum_field(reports, "remainder_cells"),
      sum_field(reports, "remainder_area"),
      sum_field(reports, "remainder_ms"),
      sum_field(reports, "split_inverters"),
      sum_field(reports, "bound_gates"),
      sum_field(reports, "shared_bound_gates"),
      sum_field(reports, "domino"),
      sum_field(reports, "nonunate"),
      sum_field(reports, "aliases"),
      sum_field(reports, "cover_cost"),
      sum_field(reports, "domino_cost"),
      sum_field(reports, "nonunate_cost"),
      sum_field(reports, "domino_literals"),
      sum_field(reports, "flow_cost"),
      sum_field(reports, "domino_in2"),
      sum_field(reports, "domino_in3"),
      sum_field(reports, "domino_in4"),
      sum_field(reports, "domino_in5"),
      sum_field(reports, "domino_in6"),
      sum_field(reports, "domino_in7"),
      sum_field(reports, "domino_in8"),
      sum_field(reports, "domino_s1"),
      sum_field(reports, "domino_s2"),
      sum_field(reports, "domino_s3"),
      sum_field(reports, "domino_s4"),
      sum_field(reports, "domino_s5"),
      sum_field(reports, "domino_s6"),
      sum_field(reports, "outputs_shallow"),
      sum_field(reports, "outputs_deep"),
      sum_field(reports, "outputs_wire"),
      sum_field(reports, "covered_nodes"),
      sum_field(reports, "replicated_nodes"),
      sum_field(reports, "ref_luts"),
      sum_field(reports, "ref_domino"),
      sum_field(reports, "ref_nonunate"),
      sum_field(reports, "ref_cost"),
      sum_field(reports, "ref_ms"));
  for (size_t i = 0; i < reports.size(); ++i) {
    report << (i ? "," : "") << reports[i];
  }
  report << "],\"regions_reused\":[";
  for (size_t i = 0; i < reused_reports.size(); ++i) {
    report << (i ? "," : "") << reused_reports[i];
  }
  const auto& worker_stats   = function_backend.worker_statistics();
  const auto& template_stats = templates.statistics();
  report << std::format(
      R"(],"function_workers":{{"scope":"time_budgeted_functions_only","calls":{},"stopped":{},"failures":{}}},"function_templates":{{"enabled":{},"hits":{},"misses":{},"loaded":{},"evictions":{},"entries":{},"bytes":{}}},"witness_archive":{{"file":"{}","scope":"searched_and_reused_candidates","records":{},"omitted":{},"bytes":{}}},"provenance":{{"directory":"{}","capture":{}}}}})",
      worker_stats.calls,
      worker_stats.stopped,
      worker_stats.failures,
      templates.enabled() ? "true" : "false",
      template_stats.hits,
      template_stats.misses,
      template_stats.loaded,
      template_stats.evictions,
      templates.entries(),
      templates.bytes(),
      livehd::json_util::escape(fs::path(witness_path).filename().string()),
      witnesses.records(),
      witnesses.omitted(),
      witnesses.bytes(),
      livehd::json_util::escape(provenance_path.filename().string()),
      provenance)
         << "\n";
  report.close();
  if (!report) {
    livehd::diag::err("pass.synth", "report-write", "io").msg("cannot write {}", report_path).fatal();
    return;
  }
  if (templates.enabled() && !template_path.empty()) {
    (void)templates.save(template_path);
  }
  const auto count = [&](std::string_view s) {
    return std::count_if(reports.begin(), reports.end(), [&](const std::string& r) {
      return r.find(std::format(R"("status":"{}")", s)) != std::string::npos;
    });
  };
  if (!search.split && search.abc_mode != livehd::synth::Search_options::Abc_mode::gate) {
    std::print("[pass.synth] ABC hand-off: {} tmap, {} opt, {} only (original logic), {} source (control)\n",
               count("abc_tmap"),
               count("abc_opt"),
               count("abc_only"),
               count("abc_source"));
  }
  std::print("[pass.synth] {} region(s): {} unate, {} ABC fallback, {} reused; {} unate functions\n",
             reports.size() + reused_reports.size(),
             unate_regions,
             fallback_regions,
             reused_reports.size(),
             sum_field(reports, "functions"));
  if (!search.split) {
    std::print("[pass.synth] cover: {} domino + {} non-unate LUT(s), cost {} (domino {}, non-unate {}; area-flow cover {}); "
               "domino inputs 2..6: {}/{}/{}/{}/{}; outputs <= {} levels: {}, deeper: {}, wires: {}; replicated nodes {} of {}\n",
               sum_field(reports, "domino"),
               sum_field(reports, "nonunate"),
               sum_field(reports, "cover_cost"),
               sum_field(reports, "domino_cost"),
               sum_field(reports, "nonunate_cost"),
               sum_field(reports, "flow_cost"),
               sum_field(reports, "domino_in2"),
               sum_field(reports, "domino_in3"),
               sum_field(reports, "domino_in4"),
               sum_field(reports, "domino_in5"),
               sum_field(reports, "domino_in6"),
               search.domino_levels ? search.domino_levels : 2,
               sum_field(reports, "outputs_shallow"),
               sum_field(reports, "outputs_deep"),
               sum_field(reports, "outputs_wire"),
               sum_field(reports, "replicated_nodes"),
               sum_field(reports, "covered_nodes"));
    if (search.reference) {
      std::print("[pass.synth] reference ABC {}&if -K {} -a: {} LUT(s) = {} domino + {} non-unate, cost {}\n",
                 search.reference_choices ? "&dch; " : "",
                 recipe.support,
                 sum_field(reports, "ref_luts"),
                 sum_field(reports, "ref_domino"),
                 sum_field(reports, "ref_nonunate"),
                 sum_field(reports, "ref_cost"));
    }
  }
  if (search.split) {
    std::print("[pass.synth] split cones: {} wire, {} in <=2 gates, {} in 3, {} more (top 2 gates + ABC-tmapped remainder: {} "
               "node(s), {} cell(s))\n",
               sum_field(reports, "cones_wire"),
               sum_field(reports, "cones_2"),
               sum_field(reports, "cones_3"),
               sum_field(reports, "cones_more"),
               sum_field(reports, "remainder_nodes"),
               sum_field(reports, "remainder_cells"));
  }
}

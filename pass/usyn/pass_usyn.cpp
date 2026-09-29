// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "pass_usyn.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <print>

#include "abc_map.hpp"  // Map_options
#include "diag.hpp"
#include "evidence.hpp"
#include "json_util.hpp"
#include "pass_abc.hpp"
#include "provenance.hpp"
#include "usyn_region.hpp"
#include "usyn_salt.hpp"

static Pass_plugin plugin("pass_usyn", Pass_usyn::setup);

void Pass_usyn::setup() {
  Eprp_method m("pass.usyn",
                "Unate synthesis: cover every region with the fewest domino gates (static LUTs where no domino gate "
                "builds a function), then hand the cover to ABC (technology mapping only, or the pass.abc flow)",
                &Pass_usyn::work);
  Pass_abc::add_mapping_labels(m);
  // The cover's ABC hand-off never takes the size tier: an over-large region
  // is already covered, and the tier's plain `&nf` would discard the cover's
  // structure the full flow keeps (abc_cleanup.md step 8).
  m.labels.erase("large_ge");
  m.add_label_optional("large_ge", "inclusive synthesis-GE threshold for large_flow (0, the pass.usyn default, disables it)", "0");
  m.add_label_optional("support", "Maximum inputs of one domino gate or LUT (2..8)", "6");
  m.add_label_optional("literals", "Maximum factored pull-down literals of one domino gate (2..4096)", "16");
  m.add_label_optional("series", "Maximum literals in one product term, i.e. series stack depth (2..32)", "4");
  m.add_label_optional("domino_overhead",
                       "transistors of a domino gate besides its factored pull-down literals (precharge, foot, keeper, "
                       "output inverter)",
                       "5");
  m.add_label_optional("cmos_factor", "static CMOS transistors per factored literal of a non-unate LUT", "2");
  m.add_label_optional("static_overhead", "transistors of a static LUT besides its literals (output inverter)", "2");
  m.add_label_optional("nonunate_penalty", "multiplier on a non-unate (static) LUT's transistors", "2");
  m.add_label_optional("cover_cuts", "priority cuts kept per node (1..64)", "12");
  m.add_label_optional("domino_levels",
                       "an output buildable in at most this many chained domino gates must be built so (2 = one cycle, a "
                       "gate per half); deeper outputs only minimize transistors; 0 ignores depth",
                       "2");
  m.add_label_optional("depth_slack",
                       ">= 0 also requires every deeper output at its minimum domino depth plus this many levels "
                       "(depth-optimal, then area recovery); -1 leaves deeper outputs to transistor count only",
                       "0");
  m.add_label_optional("duplicate",
                       "false = never compute logic twice: a gate absorbs a multi-reader node only when all its readers "
                       "are inside it (reconvergent fan-out), otherwise the node is a gate output",
                       "false");
  m.add_label_optional("cover_memories", "false leaves blasted memory regions (register files) to the pass.abc flow", "false");
  m.add_label_optional("fanout_boundary",
                       "a node with at least this many sinks is always a LUT boundary, never replicated inside its "
                       "readers' LUTs (0: the cost decides)",
                       "0");
  m.add_label_optional("abc",
                       "hand-off to ABC: tmap (the cover network, gates as minimum SOPs, technology-mapped only), opt (that "
                       "network through the full pass.abc flow), only (no cover: the original region logic through the "
                       "pass.abc flow)",
                       "tmap");
  m.add_label_optional("fallback",
                       "true: a region the cover cannot build (node limit, budget, infeasible cover) is mapped by the "
                       "pass.abc flow; false: it is an error, so every non-memory region is the cover, technology-mapped",
                       "false");
  m.add_label_optional("ware_trials",
                       "true: pass.abc's ware trials re-map each arithmetic ware region under alternative architectures "
                       "(re-running this mapper) and keep the best",
                       "false");
  m.add_label_optional("recovery_rounds", "Exact-area recovery sweeps of the cover (0..8; 0 disables)", "2");
  m.add_label_optional("max_nodes",
                       "Maximum source objects (blasted Lnet gates, then hashed source nodes) admitted to the cover (larger "
                       "regions use the ABC flow)",
                       "2000000");
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

bool read_options(const Eprp_var& var, livehd::usyn::Search_options& options) {
  auto&      recipe = options.recipe;
  const auto flag   = [&](std::string_view label, const char* dflt, bool& out) {
    const auto v = std::string{var.get_stage(label, dflt)};
    out          = v == "true" || v == "1";
    return v == "true" || v == "false" || v == "1" || v == "0";
  };
  auto& cc = options.cover_cost;
  if (!number(var.get_stage("recovery_rounds", "2"), options.recovery_rounds) || options.recovery_rounds > 8
      || !number(var.get_stage("max_nodes", "2000000"), options.max_nodes) || options.max_nodes == 0
      || !number(var.get_stage("support", "6"), recipe.support) || recipe.support < 2 || recipe.support > 8
      || !number(var.get_stage("literals", "16"), recipe.literals) || recipe.literals < 2 || recipe.literals > 4096
      || !number(var.get_stage("series", "4"), recipe.series) || recipe.series < 2 || recipe.series > 32
      || !number(var.get_stage("domino_overhead", "5"), cc.domino_overhead) || !number(var.get_stage("cmos_factor", "2"), cc.cmos_factor)
      || !number(var.get_stage("static_overhead", "2"), cc.static_overhead)
      || !number(var.get_stage("nonunate_penalty", "2"), cc.nonunate_penalty) || cc.domino_overhead > 1000
      || cc.cmos_factor > 100 || cc.static_overhead > 1000 || cc.nonunate_penalty > 100
      || !number(var.get_stage("cover_cuts", "12"), options.cover_cuts) || options.cover_cuts == 0 || options.cover_cuts > 64
      || !number(var.get_stage("domino_levels", "2"), options.domino_levels) || options.domino_levels > 64
      || !number(var.get_stage("fanout_boundary", "0"), options.fanout_boundary) || !flag("duplicate", "false", options.duplicate)
      || !flag("cover_memories", "false", options.cover_memories) || !flag("fallback", "false", options.fallback)
      || !number(var.get_stage("depth_slack", "0"), options.depth_slack) || options.depth_slack < -1 || options.depth_slack > 64) {
    return false;
  }
  using Abc_mode = livehd::usyn::Search_options::Abc_mode;
  const auto abc = std::string{var.get_stage("abc", "tmap")};
  if (abc == "tmap") {
    options.abc_mode = Abc_mode::tmap;
  } else if (abc == "opt") {
    options.abc_mode = Abc_mode::opt;
  } else if (abc == "only") {
    options.abc_mode = Abc_mode::only;
  } else {
    return false;
  }
  return true;
}

// Rows whose region carries `"status":"<status>"`.
uint64_t count_status(const std::vector<std::string>& rows, std::string_view status) {
  const auto tag = std::format(R"("status":"{}")", status);
  return std::count_if(rows.begin(), rows.end(), [&](const std::string& r) { return r.find(tag) != std::string::npos; });
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

void Pass_usyn::work(Eprp_var& var) {
  namespace fs = std::filesystem;
  livehd::usyn::Search_options search;
  if (!read_options(var, search)) {
    livehd::diag::err("pass.usyn", "invalid-search-options", "syntax")
        .msg(
            "invalid cover options; expected support=2..8, literals=2..4096, series=2..32, recovery_rounds=0..8, "
            "cover_cuts=1..64, domino_levels=0..64, depth_slack=-1..64, max_nodes>0, duplicate/cover_memories/fallback=true|false "
            "and abc=tmap|opt|only")
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
    auto pattern = (fs::temp_directory_path() / "livehd-usyn-XXXXXX").string();
    if (!mkdtemp(pattern.data())) {
      livehd::diag::err("pass.usyn", "scratch-create", "io").msg("cannot create a synthesis scratch directory").fatal();
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
  const auto provenance_path = fs::path(base + ".provenance");
  const auto report_path     = base + ".usyn.json";
  {
    std::error_code error;
    fs::remove_all(provenance_path, error);  // archive_provenance needs a fresh directory
  }
  const auto provenance = livehd::usyn::archive_provenance(provenance_path,
                                                            var.get("invocation_context", ""),
                                                            std::to_string(livehd::usyn::kUsynSrcSalt));

  std::vector<std::string> reports;
  std::vector<std::string> reused_reports;
  const auto&              recipe = search.recipe;
  Pass_abc::work_with(var, [&](livehd::abc::Map_options& opts) {
    // One synthesis tree at a time. This deliberately OVERRIDES synth.threads:
    // the region loop owns the process-memory budget the cover draws on.
    opts.threads = 1;
    // A ware trial re-maps a whole region (re-running the cover) and keeps
    // whichever stitched result is best: it would re-decide the region.
    opts.ware_trials        = var.get_stage("ware_trials", "false") == "true";
    opts.region_hook_recipe = std::format("{}lnet-cover-v7:{}:{}:{}:{}:{}:{}:{}:{}:{}:{}:{}:{}:{}:{}:{}",
                                          opts.ware_trials ? "wt-" : "",
                                          std::array{"tmap", "opt", "only"}[static_cast<int>(search.abc_mode)],
                                          livehd::usyn::kUsynSrcSalt,
                                          recipe.support,
                                          recipe.literals,
                                          recipe.series,
                                          search.cover_cost.domino_overhead,
                                          search.cover_cost.cmos_factor,
                                          search.cover_cost.static_overhead,
                                          search.cover_cost.nonunate_penalty,
                                          search.cover_cuts,
                                          search.domino_levels,
                                          search.fanout_boundary,
                                          search.depth_slack,
                                          std::string{search.duplicate ? "dup" : "nodup"} + (search.cover_memories ? "" : "-nomem"),
                                          search.max_nodes,
                                          search.recovery_rounds);
    opts.region_hook = [&](const livehd::synth::Lnet& net, const livehd::synth::Region_ctx& ctx) {
      std::string report;
      auto        rewrite = livehd::usyn::rewrite_region(net, ctx, search, report);
      rewrite.evidence    = livehd::usyn::pack_evidence(report);
      std::print("[pass.usyn] {}\n", report);
      reports.push_back(std::move(report));
      return rewrite;
    };
    opts.evidence_valid = [](std::string_view cached_region, std::string_view evidence) {
      return livehd::usyn::valid_evidence(evidence, cached_region);
    };
    opts.evidence_replay = [&](std::string_view region, std::string_view cached_region, std::string_view evidence) {
      reused_reports.push_back(livehd::usyn::replay_evidence(region, cached_region, evidence));
    };
  });
  if (livehd::diag::sink().has_halting_errors()) {
    return;
  }
  const auto    count = [&](std::string_view status) { return count_status(reports, status); };
  std::ofstream report(report_path);
  report << std::format(
      R"({{"schema_version":4,"kind":"usyn","abc":"{}","recipe":{{"support":{},"literals":{},"series":{}}},"totals":{{"regions":{},"abc_tmap":{},"abc_opt":{},"abc_only":{},"abc_fallback":{},"reused":{},"cover_ms":{:.1f},)"
      R"("domino":{},"nonunate":{},"aliases":{},"cover_cost":{},"domino_cost":{},"nonunate_cost":{},"domino_literals":{},"flow_cost":{},"domino_in2":{},"domino_in3":{},"domino_in4":{},"domino_in5":{},"domino_in6":{},"domino_in7":{},"domino_in8":{},"domino_s1":{},"domino_s2":{},"domino_s3":{},"domino_s4":{},"domino_s5":{},"domino_s6":{},"outputs_shallow":{},"outputs_deep":{},"outputs_wire":{},"covered_nodes":{},"replicated_nodes":{}}},"regions_searched":[)",
      std::array{"tmap", "opt", "only"}[static_cast<int>(search.abc_mode)],
      recipe.support,
      recipe.literals,
      recipe.series,
      reports.size() + reused_reports.size(),
      count("abc_tmap"),
      count("abc_opt"),
      count("abc_only"),
      count("abc_fallback"),
      reused_reports.size(),
      sum_field(reports, "cover_ms"),
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
      sum_field(reports, "replicated_nodes"));
  for (size_t i = 0; i < reports.size(); ++i) {
    report << (i ? "," : "") << reports[i];
  }
  report << "],\"regions_reused\":[";
  for (size_t i = 0; i < reused_reports.size(); ++i) {
    report << (i ? "," : "") << reused_reports[i];
  }
  report << std::format(R"(],"provenance":{{"directory":"{}","capture":{}}}}})",
                        livehd::json_util::escape(provenance_path.filename().string()),
                        provenance)
         << "\n";
  report.close();
  if (!report) {
    livehd::diag::err("pass.usyn", "report-write", "io").msg("cannot write {}", report_path).fatal();
    return;
  }
  std::print("[pass.usyn] {} region(s): {} tmap, {} opt, {} only, {} ABC fallback, {} reused\n",
             reports.size() + reused_reports.size(),
             count("abc_tmap"),
             count("abc_opt"),
             count("abc_only"),
             count("abc_fallback"),
             reused_reports.size());
  std::print("[pass.usyn] cover: {} domino + {} non-unate LUT(s), cost {} (domino {}, non-unate {}; area-flow cover {}); "
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
}

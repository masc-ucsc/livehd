// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "pass_usyn.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <print>
#include <set>

#include "artifact.hpp"
#include "design_synth.hpp"
#include "graph_library_singleton.hpp"
#include "host_mem.hpp"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/SHA256.h"
#include "native_report.hpp"
#include "provenance.hpp"
#include "resource_budget.hpp"
#include "tmap.hpp"
#include "usyn_salt.hpp"

static Pass_plugin plugin("pass_usyn", Pass_usyn::setup);

namespace {
// Keep old spellings registered only to produce directed migration errors.
constexpr std::string_view obsolete[]
    = {"support",          "literals",   "series",        "domino_overhead", "cmos_factor", "static_overhead",
       "nonunate_penalty", "cover_cuts", "domino_levels", "depth_slack",     "duplicate",   "cover_memories",
       "fanout_boundary",  "abc",        "fallback",      "recovery_rounds", "flow",        "area_flow",
       "large_flow",       "large_ge",   "ware_trials",   "unroll_carry"};

template <typename T>
bool parse(std::string_view text, T& value) {
  const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
  return error == std::errc{} && end == text.data() + text.size();
}

struct Options {
  livehd::usyn::Design_options design;
  livehd::synth::Tmap_options  mapping;
  std::string                  tmap        = "abc";
  uint64_t                     work        = 4000000000;
  uint32_t                     tmap_trials = 2;
};

bool read_options(const Eprp_var& var, Options& options) {
  for (const auto key : obsolete) {
    if (!var.get_stage(key, "").empty()) {
      livehd::diag::err("pass.usyn", "obsolete-option", "syntax")
          .msg("pass.usyn.{} belongs to the replaced whole-region cover", key)
          .hint(
              "use logical_inputs, stack, branches, cut_inputs and clock_phases; tmap=none|abc selects optional mapping; use "
              "pass.abc for ABC synthesis")
          .emit();
      return false;
    }
  }
  const auto read
      = [&](std::string_view key, const char* fallback, auto& value) { return parse(var.get_stage(key, fallback), value); };
  const auto flag = [&](std::string_view key, bool& value, const char* fallback = "true") {
    const auto text = var.get_stage(key, fallback);
    value           = text == "true" || text == "1" || text == "on";
    return value || text == "false" || text == "0" || text == "off";
  };
  auto&      logical            = options.design.logical;
  auto&      endpoint           = logical.endpoint;
  auto&      residual           = logical.residual;
  const auto adder_text         = var.get_stage("adder", "auto");
  options.design.auto_sum_adder = adder_text == "auto";
  const auto adder              = livehd::synth::arith::parse_adder_kind(options.design.auto_sum_adder ? "rca" : adder_text);
  if (!adder) {
    livehd::diag::err("pass.usyn", "invalid-options", "syntax")
        .msg("invalid native USYN adder '{}'", adder_text)
        .hint("adder=auto|rca|cska|cla|prefix")
        .emit();
    return false;
  }
  if (!read("adder_block", "0", options.design.adder_block) || options.design.adder_block < 0) {
    livehd::diag::err("pass.usyn", "invalid-options", "syntax")
        .msg("invalid native USYN adder_block '{}'", var.get_stage("adder_block", "0"))
        .hint("adder_block is a non-negative group width; 0 derives it from the operating width")
        .emit();
    return false;
  }
  options.design.adder       = *adder;
  const auto multiplier_text = var.get_stage("multiplier", "csa");
  const auto multiplier      = livehd::synth::arith::parse_mult_kind(multiplier_text);
  if (!multiplier) {
    livehd::diag::err("pass.usyn", "invalid-options", "syntax")
        .msg("invalid native USYN multiplier '{}'", multiplier_text)
        .hint("multiplier=csa|tree|array")
        .emit();
    return false;
  }
  options.design.multiplier = *multiplier;
  const auto mux_lowering   = var.get_stage("mux_lowering", "decode");
  if (mux_lowering != "decode" && mux_lowering != "tree") {
    livehd::diag::err("pass.usyn", "invalid-options", "syntax")
        .msg("invalid native USYN mux_lowering '{}'", mux_lowering)
        .hint("mux_lowering=decode|tree; tree is an explicit experiment until QoR validation completes")
        .emit();
    return false;
  }
  options.design.mux_tree = mux_lowering == "tree";
  options.tmap            = var.get_stage("tmap", "abc");
  options.mapping.library = var.get_stage("library", "");
  const auto target       = var.get_stage("target", "cmos");
  if (target != "cmos") {
    livehd::diag::err("pass.usyn", "unsupported-target", "unsupported")
        .msg("USYN currently emits target=cmos; physical DominoLatch emission is not implemented")
        .emit();
    return false;
  }
  const auto flatten = var.get_stage("flatten", "auto");
  if (flatten != "auto" && flatten != "true" && flatten != "false") {
    return false;
  }
  options.design.flatten = flatten == "auto"   ? livehd::partition::Flatten_mode::automatic
                           : flatten == "true" ? livehd::partition::Flatten_mode::on
                                               : livehd::partition::Flatten_mode::off;
  if ((options.tmap != "none" && options.tmap != "abc") || !read("logical_inputs", "8", endpoint.gates.logical_inputs)
      || !read("stack", "4", endpoint.gates.stack) || !read("branches", "10", endpoint.gates.branches)
      || !read("cut_inputs", "16", endpoint.window.inputs) || !read("window_nodes", "100000", endpoint.window.nodes)
      || !read("clock_phases", "2", endpoint.clock_phases) || !read("boundaries", "32", endpoint.boundaries)
      || !read("divisor_partitions", "32", endpoint.divisor_partitions) || !read("care_phases", "16", endpoint.care_phases)
      || !read("local_divisors", "32", endpoint.local_divisors) || !read("local_candidates", "64", endpoint.local_candidates)
      || !read("max_nodes", "8000000", logical.max_nodes) || logical.max_nodes == 0 || !read("work", "4000000000", options.work)
      || options.work == 0 || !read("endpoint_work", "16000000", logical.endpoint_work) || logical.endpoint_work == 0
      || !read("pair_candidates", "32", logical.pair_candidates) || logical.pair_candidates > 4096
      || !read("pair_trials", "64", logical.pair_trials) || !logical.pair_trials || logical.pair_trials > 4096
      || !read("pair_choices", "4", logical.pair_choices) || logical.pair_choices > 8
      || !read("pair_inputs", "16", logical.pair_inputs) || logical.pair_inputs == 0 || logical.pair_inputs > 16
      || !read("pair_work", "16000000", logical.pair_work) || logical.pair_work == 0
      || !read("static_and", "2", endpoint.cost.static_and) || !read("static_xor", "4", endpoint.cost.static_xor)
      || !read("static_not", "1", endpoint.cost.static_not) || !read("residual_inputs", "8", residual.resub_inputs)
      || !read("residual_divisors", "32", residual.divisors) || !read("residual_inserted", "2", residual.inserted)
      || !read("rewrite_cuts", "8", residual.rewrite_cuts) || !read("residual_depth_slack", "0", residual.depth_slack)
      || !read("balance_dup_limit", "0", residual.balance_dup_limit) || !read("sweep_inputs", "16", residual.sweep_inputs)
      || !read("sweep_table_words", "1048576", residual.sweep_table_words)
      || !read("p1_sweep_inputs", "6", residual.p1_sweep_inputs)
      || !read("mux_balance_min_arms", "3", residual.mux_balance_min_arms)
      || !read("mux_balance_area_pct", "25", residual.mux_balance_area_pct)
      || !read("tmap_sharing_fanout", "16", options.mapping.sharing_fanout) || !read("tmap_trials", "2", options.tmap_trials)
      || !options.tmap_trials || options.tmap_trials > 2
      || (options.mapping.sharing_fanout && (options.mapping.sharing_fanout < 2 || options.mapping.sharing_fanout > 4096))
      || !read("memory_budget_mb", "16384", options.mapping.memory_budget_mb) || options.mapping.memory_budget_mb <= 0
      || !read("time_budget_ms", "0", options.mapping.time_budget_ms) || !read("delay", "0", options.mapping.delay_ps)
      || !std::isfinite(options.mapping.delay_ps) || options.mapping.delay_ps < 0 || !flag("residual", logical.optimize_residual)
      || !flag("p1", logical.pre_optimize, "false") || !flag("sop_tree", options.design.sop_tree, "false")
      || !flag("multi_rep", options.design.multi_rep, "false") || !flag("cmos_cleanup", options.design.cmos_cleanup, "true")
      || !flag("npn4", residual.npn4, "true") || !flag("sweep", residual.sweep, "false")
      || !flag("balance", residual.balance, "false") || !flag("mux_balance", residual.mux_balance, "true")
      || !flag("feedback", logical.feedback) || !flag("fast_accept", endpoint.fast_accept)
      || !flag("eq_balance", options.design.eq_balance, "false") || !livehd::usyn::valid_endpoint_options(endpoint)
      || !livehd::usyn::valid_residual_options(residual)) {
    return false;
  }
  // Physical-only tmap techniques, with pass.abc's spelling, defaults and
  // ranges: buffering, gate sizing to the delay budget, the boundary
  // environment/re-size and the mapping-only slack-to-area re-map. No silent
  // fallback: a mistyped value would quietly move every mapped netlist.
  auto&      mapping        = options.mapping;
  const auto invalid_option = [&](std::string_view key, std::string_view hint) {
    livehd::diag::err("pass.usyn", "invalid-options", "syntax")
        .msg("invalid pass.usyn.{} '{}'", key, var.get_stage(key, ""))
        .hint(hint)
        .emit();
    return false;
  };
  if (!read("max_fanout", "16", mapping.max_fanout)) {
    return invalid_option("max_fanout", "an integer in [0, 4294967295]; 0 disables the `buffer -N` tail");
  }
  if (!read("area_relax", "200", mapping.area_relax_pct)) {
    return invalid_option("area_relax", "an integer in [0, 4294967295]; 0 always maps for minimum delay");
  }
  if (!read("boundary_rounds", "1", mapping.boundary_rounds) || mapping.boundary_rounds < 1 || mapping.boundary_rounds > 64) {
    return invalid_option("boundary_rounds", "an integer in [1, 64]");
  }
  if (!read("io_load", "-1", mapping.io_load) || !std::isfinite(mapping.io_load)) {
    return invalid_option("io_load", "a load in fF; negative = one typical input pin of the library");
  }
  if (!flag("boundary", mapping.boundary)) {
    return invalid_option("boundary", "true|false");
  }
  if (!flag("boundary_buffer", mapping.boundary_buffer)) {
    return invalid_option("boundary_buffer", "true|false");
  }
  mapping.boundary_drive = std::string(var.get_stage("boundary_drive", ""));
  mapping.reg_margin     = std::string(var.get_stage("reg_margin", "auto"));
  if (double margin = 0;
      mapping.reg_margin != "auto" && (!parse(mapping.reg_margin, margin) || !std::isfinite(margin) || margin < 0)) {
    return invalid_option("reg_margin", "`auto` (the mapped DFF's clk->Q + setup) or a non-negative number of ps");
  }
  options.design.max_source_nodes = logical.max_nodes;
  options.design.cost_policy      = std::string(var.get_stage("cost_mode", "proxy"));
  if (options.design.cost_policy != "proxy" && options.design.cost_policy != "cells" && options.design.cost_policy != "area") {
    return invalid_option("cost_mode", "proxy|cells|area; cells/area need a Liberty and imply multi_rep");
  }
  if (options.design.cost_policy != "proxy") {
    options.design.multi_rep      = true;
    options.design.gate_objective = options.design.cost_policy == "cells";
  }
  options.design.cache.directory = std::string(var.get_stage("cache_dir", ""));
  if (!options.design.cache.directory.empty()) {
    options.mapping.cache_directory = (options.design.cache.directory / "tmap").string();
  }
  options.design.cache.context
      = std::to_string(options.mapping.memory_budget_mb) + "/" + std::to_string(options.mapping.time_budget_ms);
  endpoint.cost_nodes = logical.max_nodes;
  residual.max_nodes  = logical.max_nodes;
  return true;
}

void write_file(const std::filesystem::path& path, const std::string& contents) {
  const auto    temporary = std::filesystem::path(path.string() + ".tmp");
  std::ofstream file(temporary);
  file << contents << '\n';
  file.close();
  if (!file) {
    std::error_code ec;
    std::filesystem::remove(temporary, ec);
    livehd::diag::err("pass.usyn", "report-write", "io").msg("cannot write {}", path.string()).fatal();
  }
  std::filesystem::rename(temporary, path);
}
bool mapped_better(const livehd::synth::Mapped_cost& candidate, const livehd::synth::Mapped_cost& incumbent) {
  if (candidate.gates != incumbent.gates) {
    return candidate.gates < incumbent.gates;
  }
  const auto compare = [](double a, double b) {
    const auto tolerance = 1e-6 * std::max({1.0, std::abs(a), std::abs(b)});
    return a < b - tolerance ? -1 : a > b + tolerance ? 1 : 0;
  };
  const auto area = compare(candidate.area, incumbent.area);
  return area ? area < 0 : compare(candidate.maximum_region_delay, incumbent.maximum_region_delay) < 0;
}
}  // namespace

void Pass_usyn::setup() {
  Eprp_method m("pass.usyn", "Native register-rooted XAG synthesis with optional technology mapping", &Pass_usyn::work);
  m.add_label_optional("tmap", "Optional technology mapping: none (logical CMOS, no Liberty) or abc (mapping only)", "abc");
  m.add_label_optional("tmap_trials",
                       "At most two complete native networks: 2 also tries p1+multi_rep and keeps fewer actual mapped gates; "
                       "shares native work and process/time limits; tmap=none runs one network",
                       "2");
  m.add_label_optional("target", "Output target; currently cmos retains original state", "cmos");
  m.add_label_optional("adder",
                       "Native arithmetic: auto (prefix wide sums, comparisons and multiplier carry), rca, cska, cla or prefix",
                       "auto");
  m.add_label_optional("adder_block", "Native CSKA/CLA group width (0: derive from operating width)", "0");
  m.add_label_optional("multiplier", "Native partial-product summation: csa (carry-save), tree or array", "csa");
  m.add_label_optional("mux_lowering",
                       "Indexed mux lowering: decode or experimental tree; predicates retain nonzero semantics",
                       "decode");
  m.add_label_optional("eq_balance", "Experimental balanced equality reduction; false retains shared prefix lowering", "false");
  m.add_label_optional("logical_inputs", "Maximum logical inputs of one selected gate, counting Q/!Q once (1..16)", "8");
  m.add_label_optional("stack", "Maximum series stack of a selected gate", "4");
  m.add_label_optional("branches", "Maximum factored parallel discharge width of a selected gate", "10");
  m.add_label_optional("cut_inputs", "Analysis-window inputs (1..16), independent of gate legality", "16");
  m.add_label_optional("window_nodes", "Maximum nodes in an endpoint analysis window", "100000");
  m.add_label_optional("clock_phases", "Selected domino structure: one or two phases; CMOS output adds no phase state", "2");
  m.add_label_optional("boundaries", "Retained endpoint frontier (1..4096); up to 12x+2 bounded move trials", "32");
  m.add_label_optional("divisor_partitions", "New functional-divisor partitions (0 disables, up to 4096)", "32");
  m.add_label_optional("care_phases", "Input-phase trials for divisor care completions (0 disables, up to 64)", "16");
  m.add_label_optional("local_divisors", "Existing divisors retained for local endpoint search (1..256)", "32");
  m.add_label_optional("local_candidates", "Witness-guided divisor-set trials per endpoint (0 disables, up to 4096)", "64");
  m.add_label_optional("fast_accept",
                       "Stop after a competitive full-cone result; false continues bounded improvement search",
                       "true");
  m.add_label_optional("max_nodes", "Source-definition and per-region logical/emission node admission limit", "8000000");
  m.add_label_optional("work",
                       "Deterministic work limit: the structural allowance (preparation, partitioning, translation, "
                       "each region's import, identity selection and freeze, emission) and, separately, the search "
                       "remainder shared by region searches in order",
                       "4000000000");
  m.add_label_optional("endpoint_work", "Maximum search work per endpoint", "16000000");
  m.add_label_optional("pair_candidates", "Shared endpoint pairs retained per sharing snapshot (0 disables, up to 4096)", "32");
  m.add_label_optional("pair_trials", "Maximum pair trials including affected revisits and admission failures (1..4096)", "64");
  m.add_label_optional("pair_choices",
                       "Pair alternatives per endpoint and alternate code trials per partition (0 disables, up to 8)",
                       "4");
  m.add_label_optional("pair_inputs", "Maximum joint independent support for a pair trial (1..16)", "16");
  m.add_label_optional("pair_work", "Maximum work for coordinated pair reselection per region", "16000000");
  m.add_label_optional("static_and", "Positive static AND cost proxy", "2");
  m.add_label_optional("static_xor", "Positive static XOR cost proxy", "4");
  m.add_label_optional("static_not", "Positive static inverter cost proxy", "1");
  m.add_label_optional("rewrite_cuts", "Maximum priority cuts per NPN4 root (1..32)", "8");
  m.add_label_optional("tmap_sharing_fanout",
                       "Complete shared-cone mapping boundary; default 16, 0 disables, otherwise 2..4096",
                       "16");
  m.add_label_optional("p1", "Bounded light native cleanup before unate endpoint selection", "false");
  m.add_label_optional("sop_tree", "Experimental symbolic SOP/mux alternative; implies final CMOS cleanup", "false");
  m.add_label_optional("multi_rep",
                       "Experimental bounded SOP/BDD/XAG choices and sharing-aware selection; implies CMOS cleanup",
                       "false");
  m.add_label_optional("cost_mode",
                       "Native choice ranking: proxy, cells or area; cells/area require Liberty and imply multi_rep",
                       "proxy");
  m.add_label_optional("cmos_cleanup", "Optimize a private final behavioral CMOS expansion", "true");
  m.add_label_optional("npn4", "Complete native NPN4 rewrite templates", "true");
  m.add_label_optional("sweep", "Exact functional sweep on at most sweep_inputs boundary inputs", "false");
  m.add_label_optional("sweep_inputs", "Exact-sweep support width in boundary inputs (1..16)", "16");
  m.add_label_optional("p1_sweep_inputs", "Exact-sweep width of the light P1 pass before unate selection (1..16)", "6");
  m.add_label_optional("sweep_table_words",
                       "Exact-sweep truth-table budget in 64-bit words (>= 2); functions past it stay structural",
                       "1048576");
  m.add_label_optional("mux_balance",
                       "Rebuild priority-mux chains as order-preserving logarithmic trees when the critical depth drops "
                       "and the estimated cost grows by at most mux_balance_area_pct",
                       "true");
  m.add_label_optional("mux_balance_min_arms", "Shortest priority-mux chain mux_balance rebuilds (2..256)", "3");
  m.add_label_optional("mux_balance_area_pct", "Largest estimated-cost growth mux_balance accepts, in percent (0..1000)", "25");
  m.add_label_optional("balance", "Bounded area-neutral native AND/XOR balancing", "false");
  m.add_label_optional("balance_dup_limit",
                       "Shared critical-edge crossings in balance (0..1024); full area/depth guard remains",
                       "0");
  m.add_label_optional("residual", "Run one native residual cleanup/rewrite/resubstitution round", "true");
  m.add_label_optional("feedback", "Reconsider affected endpoints once after residual optimization", "true");
  m.add_label_optional("residual_inputs", "Maximum independent inputs of a residual resubstitution window", "8");
  m.add_label_optional("residual_divisors", "Maximum residual resubstitution divisors", "32");
  m.add_label_optional("residual_inserted", "Maximum inserted nodes in a residual replacement (0..2)", "2");
  m.add_label_optional("residual_depth_slack", "Allowed residual depth increase", "0");
  m.add_label_optional("flatten", "Hierarchy policy: auto follows coloring, true flattens, false preserves hierarchy", "auto");
  m.add_label_optional("delay",
                       "Optional tmap timing BUDGET in ps (0: none), passed to the provider: buffering/sizing target, "
                       "minus reg_margin for flop-bearing regions. Untimed mapping reports logic levels, not ps",
                       "0");
  // Physical-only tmap knobs: the same meanings and defaults as pass.abc's.
  // They size and buffer mapped cells; none restructures the native network.
  m.add_label_optional("max_fanout",
                       "tmap fanout cap: `buffer -N <n>` plus down-sizing to the budget after mapping (0 disables)",
                       "16");
  m.add_label_optional("area_relax",
                       "max percent of measured slack a timed tmap region trades back for area by re-running the same "
                       "mapping-only `&nf -R <pct>`, then re-sizing to the budget (0 disables; needs delay and NLDM)",
                       "200");
  m.add_label_optional("reg_margin",
                       "register overhead subtracted from delay for a flop-bearing tmap region: auto = mapped DFF clk->Q + "
                       "setup, a number = ps, 0 = none",
                       "auto");
  m.add_label_optional("boundary",
                       "size every tmap region against its partition environment, then re-size each in place against the "
                       "exact stitched loads/drivers (exact re-size needs delay and an NLDM Liberty)",
                       "true");
  m.add_label_optional("boundary_buffer",
                       "tree every tmap region input's fanout past max_fanout by declaring boundary_drive as its driver",
                       "true");
  m.add_label_optional("boundary_drive",
                       "stand-in Liberty cell driving a tmap region input without a mapped driver (empty: smallest buffer; "
                       "none: ideal driver)",
                       "");
  m.add_label_optional("boundary_rounds", "rounds of the exact tmap boundary re-size (1..64)", "1");
  m.add_label_optional("io_load", "load in fF on a primary output for tmap sizing; negative = one typical input pin", "-1");
  m.add_label_optional("memory_budget_mb", "Invocation memory-growth admission limit in MiB", "16384");
  m.add_label_optional("time_budget_ms", "Invocation wall-time admission limit (0: unlimited)", "0");
  m.add_label_optional("threads", "INTERNAL shared worker setting; USYN currently serializes synthesis", "1");
  m.add_label_optional("library", "INTERNAL Liberty from synth.liberty; required only for tmap=abc", "");
  m.add_label_optional("out", "Output graph-library directory", "");
  m.add_label_optional("qor", "Output report path", "");
  m.add_label_optional("cache_dir", "INTERNAL logical/tmap cache root; controlled by workdir and lhd.incremental", "");
  m.add_label_optional("timing_files", "INTERNAL timing input provenance", "");
  m.add_label_optional("invocation_context", "INTERNAL kernel invocation and observed input provenance", "");
  for (const auto key : obsolete) {
    m.add_label_optional(std::string{key}, "DEPRECATED obsolete cover option; emits a migration diagnostic", "");
  }
  register_pass(m);
}

void Pass_usyn::work(Eprp_var& var) {
  namespace fs   = std::filesystem;
  namespace usyn = livehd::usyn;
  Options options;
  if (!read_options(var, options)) {
    if (!livehd::diag::sink().has_halting_errors()) {
      livehd::diag::err("pass.usyn", "invalid-options", "syntax")
          .msg("invalid native USYN limits, flags or tmap mode")
          .hint("logical_inputs/cut_inputs=1..16, clock_phases=1|2, positive work/node/cost limits, tmap=none|abc")
          .emit();
    }
    return;
  }
  if (options.tmap != "none" && !livehd::synth::has_tmap_provider(options.tmap)) {
    livehd::diag::err("pass.usyn", "tmap-unavailable", "unsupported")
        .msg("technology-mapping provider '{}' is unavailable in this build", options.tmap)
        .hint("use pass.usyn.tmap=none for the complete logical CMOS result")
        .emit();
    return;
  }
  const auto                   requested_top = var.get("top", "");
  std::shared_ptr<hhds::Graph> top;
  for (const auto& graph : var.graphs) {
    if (graph && (requested_top.empty() || graph->get_name() == requested_top)) {
      top = graph;
      break;
    }
  }
  if (!top) {
    livehd::diag::err("pass.usyn", "no-top", "unsupported").msg("source top '{}' not found", requested_top).emit();
    return;
  }
  // Explicit whole-design flattening is a single-module output contract.
  // Optional shared-cone partition hints must not recreate module boundaries.
  if (livehd::partition::flatten_is_single_module(top.get(), options.design.flatten)) {
    options.mapping.sharing_fanout = 0;
  }
  const auto output = std::string{var.get("out", "")};
  auto       base   = std::string{var.get("qor", "")};
  struct Scratch {
    fs::path path;
    ~Scratch() {
      if (!path.empty()) {
        std::error_code ec;
        fs::remove_all(path, ec);
      }
    }
  } scratch;
  if (base.empty()) {
    if (!output.empty()) {
      base = output + ".qor.json";
    } else {
      auto pattern = (fs::temp_directory_path() / "livehd-usyn-XXXXXX").string();
      if (!mkdtemp(pattern.data())) {
        livehd::diag::err("pass.usyn", "scratch-create", "io").msg("cannot create report scratch directory").fatal();
      }
      scratch.path = pattern;
      base         = (scratch.path / "qor.json").string();
    }
  }
  fs::create_directories(fs::absolute(base).parent_path());
  // Capture into a staging directory and swap it in only when the report is
  // published, so a failed invocation leaves the previous report's
  // provenance matching that report.
  const auto provenance_path = fs::path(base + ".provenance");
  Scratch    provenance_staging;
  provenance_staging.path = fs::path(base + ".provenance.tmp");
  {
    std::error_code ec;
    fs::remove_all(provenance_staging.path, ec);
  }
  const auto provenance
      = usyn::archive_provenance(provenance_staging.path, var.get("invocation_context", ""), std::to_string(usyn::kUsynSrcSalt));
  const auto start   = std::chrono::steady_clock::now();
  const auto elapsed = [&] { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count(); };
  usyn::Resource_budget resources;
  resources.entry_bytes         = livehd::cost::process_footprint_bytes();
  resources.growth_limit_bytes  = livehd::cost::budget_bytes(options.mapping.memory_budget_mb);
  resources.process_limit_bytes = livehd::cost::configured_budget_bytes();
  resources.time_limit_ms       = options.mapping.time_budget_ms;
  usyn::Budget work{options.work};
  work.admission = [&] { return resources.admit(elapsed(), livehd::cost::process_footprint_bytes()); };
  if (options.design.cost_policy != "proxy") {
    const auto    model_work_before = work.consumed;
    std::ifstream library(options.mapping.library, std::ios::binary);
    if (!library) {
      livehd::diag::err("pass.usyn", "native-cost-library", "io").msg("native cost_mode requires a readable Liberty file").emit();
      return;
    }
    llvm::SHA256            hash;
    std::string             library_contents;
    std::array<char, 65536> buffer;
    while (library) {
      library.read(buffer.data(), buffer.size());
      const auto length = static_cast<size_t>(library.gcount());
      if (length > 64 * 1024 * 1024 - library_contents.size()) {
        livehd::diag::err("pass.usyn", "native-cost-library-limit", "unsupported")
            .msg("Liberty content snapshot exceeds 64 MiB")
            .emit();
        return;
      }
      if (!work.spend(length)) {
        livehd::diag::err("pass.usyn", "native-cost-budget", "unsupported").msg("Liberty content hashing budget").emit();
        return;
      }
      hash.update(llvm::StringRef(buffer.data(), length));
      library_contents.append(buffer.data(), length);
    }
    if (!library.eof()) {
      livehd::diag::err("pass.usyn", "native-cost-library", "io").msg("cannot read Liberty contents").emit();
      return;
    }
    std::string    identity;
    constexpr char hex[] = "0123456789abcdef";
    for (const auto byte : hash.final()) {
      identity += hex[byte >> 4];
      identity += hex[byte & 15];
    }
    auto model_work = work.slice(100000000, 1, 4096);
    auto model      = usyn::read_native_cost_text_model(library_contents, model_work);
    work.absorb(model_work);
    options.design.cost_model_work = work.consumed - model_work_before;
    if (!model) {
      livehd::diag::err("pass.usyn", "native-cost-refused", "unsupported")
          .msg("native legal-cell cost table unavailable or over budget")
          .emit();
      return;
    }
    options.design.cost_model = std::make_shared<usyn::Native_cost_model>(std::move(*model));
    options.design.cache.context
        += "/native-cost-v1/" + options.design.cost_policy + "/" + identity + "/delay-" + std::to_string(options.mapping.delay_ps);
  }
  // A bounded alternative has a fixed share, independent of the first trial's
  // cache hits or search consumption. Reserve publication work in the parent.
  // Source graphs stay immutable; selection/artifacts and mapped output move
  // together, so the winner's correspondence is authoritative.
  const bool compare
      = options.tmap != "none" && options.tmap_trials == 2 && (!options.design.logical.pre_optimize || !options.design.multi_rep);
  const auto trial_cap   = compare ? (work.remaining - work.remaining / 16) / 2 : work.remaining;
  auto       native_work = work.slice(trial_cap);
  auto       selected    = usyn::synthesize_cmos_design(top, options.design, native_work);
  work.absorb(native_work);
  if (!selected.design) {
    livehd::diag::err("pass.usyn", "synthesis-refused", "unsupported")
        .msg("native synthesis failed: {}{}", selected.reason, resources.reason.empty() ? "" : "; " + resources.reason)
        .emit();
    return;
  }
  std::unique_ptr<livehd::synth::Mapped_design> mapped;
  std::vector<usyn::Mapping_trial>              trials;
  if (options.tmap != "none") {
    const auto mapping_cache_root = options.mapping.cache_directory;
    // Keep the incumbent's published generation at the existing tmap root.
    // The alternative uses its own generation below choices, so it cannot
    // make a cold invocation look warm by reading the first trial's stores.
    options.mapping.admission     = [&](std::string_view) { return work.admission(); };
    auto result                   = livehd::synth::technology_map(options.tmap, selected.design->top, options.mapping);
    if (!result.design) {
      livehd::diag::err("pass.usyn", "tmap-refused", "unsupported").msg("technology mapping failed: {}", result.reason).emit();
      return;
    }
    mapped = std::move(result.design);
    if (compare) {
      const auto price_admission = [&] { return work.spend(1024); };
      auto       first_cost      = livehd::synth::mapped_cost(*mapped, price_admission);
      trials.push_back({options.design.logical.pre_optimize,
                        options.design.multi_rep,
                        true,
                        trial_cap,
                        native_work.consumed,
                        elapsed(),
                        first_cost,
                        first_cost ? "" : "mapped cost unavailable; keep incumbent"});
      for (const auto& region : selected.design->regions) {
        trials.back().search_used += region.search.work + region.cmos_search.work;
      }
      // Without the incumbent's mapped cost no alternative can be selected:
      // do not spend the second native share and a second technology map.
      if (first_cost) {
        auto alternative_options                 = options.design;
        alternative_options.logical.pre_optimize = true;
        alternative_options.multi_rep            = true;
        auto       alternative_work              = work.slice(trial_cap);
        const auto trial_start                   = elapsed();
        auto       alternative                   = usyn::synthesize_cmos_design(top, alternative_options, alternative_work);
        work.absorb(alternative_work);
        usyn::Mapping_trial trial{true, true, false, trial_cap, alternative_work.consumed, 0, {}, alternative.reason};
        if (alternative.design) {
          for (const auto& region : alternative.design->regions) {
            trial.search_used += region.search.work + region.cmos_search.work;
          }
        }
        if (alternative.design && !work.resource_exhausted && work.admission()) {
          auto alternative_mapping = options.mapping;
          if (!mapping_cache_root.empty()) {
            alternative_mapping.cache_directory = (fs::path(mapping_cache_root) / "choices").string();
          }
          auto alternative_mapped = livehd::synth::technology_map(options.tmap, alternative.design->top, alternative_mapping);
          trial.reason            = alternative_mapped.reason;
          if (alternative_mapped.design) {
            trial.cost = livehd::synth::mapped_cost(*alternative_mapped.design, price_admission);
            if (!trial.cost) {
              trial.reason = "mapped cost unavailable; keep incumbent";
            } else if (mapped_better(*trial.cost, *first_cost)) {
              trials.front().selected = false;
              trial.selected          = true;
              selected                = std::move(alternative);
              mapped                  = std::move(alternative_mapped.design);
              options.design          = std::move(alternative_options);
            }
          }
        }
        trial.elapsed_ms = elapsed() - trial_start;
        trials.push_back(std::move(trial));
      }
    }
  }
  if (livehd::diag::sink().has_halting_errors()) {
    return;
  }
  if (!work.admission()) {
    livehd::diag::err("pass.usyn", "publication-refused", "unsupported").msg("{}", resources.reason).emit();
    return;
  }
  const auto artifact_path = fs::path(base + ".usyn.artifacts");
  fs::create_directories(artifact_path);
  std::vector<std::string> artifact_paths;
  usyn::Artifact_limits    artifact_limits;
  artifact_limits.nodes         = options.design.logical.max_nodes;
  artifact_limits.formula_nodes = options.design.logical.endpoint.functions.max_formula_nodes;
  for (size_t i = 0; i < selected.design->regions.size(); ++i) {
    const auto& region = selected.design->regions[i];
    auto        artifact
        = usyn::serialize_artifact(region.module_name, livehd::synth::State_target::cmos, region.selected, work, artifact_limits);
    if (artifact.status != usyn::Status::feasible) {
      livehd::diag::err("pass.usyn", "artifact-refused", "unsupported").msg("{}: {}", region.module_name, artifact.reason).emit();
      return;
    }
    // Content-addressed files keep a previous report's artifacts intact when
    // a later invocation fails before publishing its new report.
    if (!work.spend(artifact.bytes.size())) {
      livehd::diag::err("pass.usyn", "artifact-refused", "unsupported").msg("artifact publication budget").emit();
      return;
    }
    llvm::SHA256 hash;
    hash.update(llvm::StringRef(artifact.bytes));
    std::string    digest;
    constexpr char hex[] = "0123456789abcdef";
    for (const auto byte : hash.final()) {
      digest += hex[byte >> 4];
      digest += hex[byte & 15];
    }
    const auto    path      = artifact_path / (digest + ".usyn");
    const auto    temporary = fs::path(path.string() + ".tmp");
    std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
    file.write(artifact.bytes.data(), static_cast<std::streamsize>(artifact.bytes.size()));
    file.close();
    if (!file) {
      std::error_code ec;
      fs::remove(temporary, ec);
      livehd::diag::err("pass.usyn", "artifact-write", "io").msg("cannot write {}", path.string()).fatal();
    }
    fs::rename(temporary, path);
    artifact_paths.push_back((artifact_path.filename() / path.filename()).generic_string());
  }
  if (!work.admission()) {
    livehd::diag::err("pass.usyn", "publication-refused", "unsupported").msg("{}", resources.reason).emit();
    return;
  }
  const auto report = usyn::native_report(*selected.design,
                                          options.design,
                                          options.tmap,
                                          elapsed(),
                                          resources.peak_bytes,
                                          provenance_path.filename().string(),
                                          provenance,
                                          artifact_paths,
                                          trials);
  {
    // rename() cannot replace a non-empty directory: move the old capture
    // aside (restored if the swap fails), then drop it once the new one is in.
    std::error_code ec;
    const auto      previous = fs::path(base + ".provenance.old");
    fs::remove_all(previous, ec);
    bool moved_aside = false;
    if (fs::exists(provenance_path, ec)) {
      fs::rename(provenance_path, previous, ec);
      moved_aside = !ec;
    }
    ec.clear();
    fs::rename(provenance_staging.path, provenance_path, ec);
    if (ec) {
      if (moved_aside) {
        std::error_code restore_ec;
        fs::rename(previous, provenance_path, restore_ec);
      }
      livehd::diag::err("pass.usyn", "provenance-write", "io").msg("cannot publish {}", provenance_path.string()).fatal();
    }
    fs::remove_all(previous, ec);
    provenance_staging.path.clear();
  }
  write_file(base + ".usyn.json", report);
  write_file(base, mapped ? usyn::mapping_report(*mapped, options.tmap, options.mapping) : report);
  {
    // The new report is published: drop artifacts that only earlier reports
    // referenced, so a copied archive holds exactly this report's regions.
    std::set<std::string> published;
    for (const auto& relative : artifact_paths) {
      published.insert(fs::path(relative).filename().string());
    }
    std::error_code ec;
    for (fs::directory_iterator it(artifact_path, ec), end; !ec && it != end; it.increment(ec)) {
      const auto name = it->path().filename().string();
      if (it->is_regular_file() && it->path().extension() == ".usyn" && !published.contains(name)) {
        std::error_code remove_ec;
        fs::remove(it->path(), remove_ec);
      }
    }
  }
  if (!output.empty()) {
    auto& destination = livehd::Hhds_graph_library::instance(output);
    auto& source      = mapped ? mapped->library : selected.design->library;
    if (std::find_if(var.graphs.begin(),
                     var.graphs.end(),
                     [&](const auto& g) { return g && g->get_io()->get_library() == &destination; })
        != var.graphs.end()) {
      livehd::diag::err("pass.usyn", "output-is-input", "io")
          .msg("USYN output library must differ from the source library")
          .fatal();
    }
    for (const auto gid : destination.all_gids()) {
      destination.delete_graphio(destination.find_io(gid));
    }
    if (!livehd::copy_with_callees(destination, source, top->get_name())) {
      livehd::diag::err("pass.usyn", "publish-failed", "internal").msg("could not publish USYN output").fatal();
    }
  }
  std::print("[pass.usyn] native synthesis: {} region(s), target=cmos, tmap={}, {:.1f} ms\n",
             selected.design->regions.size(),
             options.tmap,
             elapsed());
}

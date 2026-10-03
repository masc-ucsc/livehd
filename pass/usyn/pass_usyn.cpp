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
  std::string                  tmap = "abc";
  uint64_t                     work = 4000000000;
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
  const auto flag = [&](std::string_view key, bool& value) {
    const auto text = var.get_stage(key, "true");
    value           = text == "true" || text == "1" || text == "on";
    return value || text == "false" || text == "0" || text == "off";
  };
  auto&      logical            = options.design.logical;
  auto&      endpoint           = logical.endpoint;
  auto&      residual           = logical.residual;
  const auto adder_text         = var.get_stage("adder", "auto");
  options.design.auto_sum_adder = adder_text == "auto";
  const auto adder              = livehd::synth::arith::parse_adder_kind(options.design.auto_sum_adder ? "rca" : adder_text);
  if (!adder || !read("adder_block", "0", options.design.adder_block) || options.design.adder_block < 0) {
    return false;
  }
  options.design.adder  = *adder;
  const auto multiplier = livehd::synth::arith::parse_mult_kind(var.get_stage("multiplier", "csa"));
  if (!multiplier) {
    return false;
  }
  options.design.multiplier = *multiplier;
  options.tmap              = var.get_stage("tmap", "abc");
  options.mapping.library   = var.get_stage("library", "");
  const auto target         = var.get_stage("target", "cmos");
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
      || !read("max_nodes", "2000000", logical.max_nodes) || logical.max_nodes == 0 || !read("work", "4000000000", options.work)
      || options.work == 0 || !read("endpoint_work", "16000000", logical.endpoint_work) || logical.endpoint_work == 0
      || !read("pair_candidates", "32", logical.pair_candidates) || logical.pair_candidates > 4096
      || !read("pair_trials", "64", logical.pair_trials) || !logical.pair_trials || logical.pair_trials > 4096
      || !read("pair_choices", "4", logical.pair_choices) || logical.pair_choices > 8
      || !read("pair_inputs", "16", logical.pair_inputs) || logical.pair_inputs == 0 || logical.pair_inputs > 16
      || !read("pair_work", "16000000", logical.pair_work) || logical.pair_work == 0
      || !read("static_and", "2", endpoint.cost.static_and) || !read("static_xor", "4", endpoint.cost.static_xor)
      || !read("static_not", "1", endpoint.cost.static_not) || !read("residual_inputs", "8", residual.resub_inputs)
      || !read("residual_divisors", "32", residual.divisors) || !read("residual_inserted", "2", residual.inserted)
      || !read("residual_depth_slack", "0", residual.depth_slack)
      || !read("memory_budget_mb", "16384", options.mapping.memory_budget_mb) || options.mapping.memory_budget_mb <= 0
      || !read("time_budget_ms", "0", options.mapping.time_budget_ms) || !read("delay", "0", options.mapping.delay_ps)
      || !std::isfinite(options.mapping.delay_ps) || options.mapping.delay_ps < 0 || !flag("residual", logical.optimize_residual)
      || !flag("feedback", logical.feedback) || !flag("fast_accept", endpoint.fast_accept)
      || !livehd::usyn::valid_endpoint_options(endpoint) || !livehd::usyn::valid_residual_options(residual)) {
    return false;
  }
  options.design.max_source_nodes = logical.max_nodes;
  options.design.cache.directory  = std::string(var.get_stage("cache_dir", ""));
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
}  // namespace

void Pass_usyn::setup() {
  Eprp_method m("pass.usyn", "Native register-rooted XAG synthesis with optional technology mapping", &Pass_usyn::work);
  m.add_label_optional("tmap", "Optional technology mapping: none (logical CMOS, no Liberty) or abc (mapping only)", "abc");
  m.add_label_optional("target", "Output target; currently cmos retains original state", "cmos");
  m.add_label_optional("adder",
                       "Native arithmetic: auto (prefix wide sums, comparisons and multiplier carry), rca, cska, cla or prefix",
                       "auto");
  m.add_label_optional("adder_block", "Native CSKA/CLA group width (0: derive from operating width)", "0");
  m.add_label_optional("multiplier", "Native partial-product summation: csa (carry-save), tree or array", "csa");
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
  m.add_label_optional("max_nodes", "Source-definition and per-region logical/emission node admission limit", "2000000");
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
  m.add_label_optional("residual", "Run one native residual cleanup/rewrite/resubstitution round", "true");
  m.add_label_optional("feedback", "Reconsider affected endpoints once after residual optimization", "true");
  m.add_label_optional("residual_inputs", "Maximum independent inputs of a residual resubstitution window", "8");
  m.add_label_optional("residual_divisors", "Maximum residual resubstitution divisors", "32");
  m.add_label_optional("residual_inserted", "Maximum inserted nodes in a residual replacement (0..2)", "2");
  m.add_label_optional("residual_depth_slack", "Allowed residual depth increase", "0");
  m.add_label_optional("flatten", "Hierarchy policy: auto follows coloring, true flattens, false preserves hierarchy", "auto");
  m.add_label_optional("delay", "Optional tmap timing target in ps (0: none)", "0");
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
  auto selected  = usyn::synthesize_cmos_design(top, options.design, work);
  if (!selected.design) {
    livehd::diag::err("pass.usyn", "synthesis-refused", "unsupported")
        .msg("native synthesis failed: {}{}", selected.reason, resources.reason.empty() ? "" : "; " + resources.reason)
        .emit();
    return;
  }
  std::unique_ptr<livehd::synth::Mapped_design> mapped;
  if (options.tmap != "none") {
    options.mapping.admission = [&](std::string_view) { return work.admission(); };
    auto result               = livehd::synth::technology_map(options.tmap, selected.design->top, options.mapping);
    if (!result.design) {
      livehd::diag::err("pass.usyn", "tmap-refused", "unsupported").msg("technology mapping failed: {}", result.reason).emit();
      return;
    }
    mapped = std::move(result.design);
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
                                          artifact_paths);
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
  write_file(base, mapped ? usyn::mapping_report(*mapped, options.tmap, options.mapping.library) : report);
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

// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "native_report.hpp"

#include <bit>

#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"

namespace livehd::usyn {
namespace {
using Writer = rapidjson::Writer<rapidjson::StringBuffer>;
void text(Writer& w, std::string_view value) { w.String(value.data(), value.size()); }
void number(Writer& w, const char* key, uint64_t value) {
  w.Key(key);
  w.Uint64(value);
}
void flag(Writer& w, const char* key, bool value) {
  w.Key(key);
  w.Bool(value);
}
const char* adder_name(synth::arith::Adder_kind kind) {
  switch (kind) {
    case synth::arith::Adder_kind::cska  : return "cska";
    case synth::arith::Adder_kind::cla   : return "cla";
    case synth::arith::Adder_kind::prefix: return "prefix";
    case synth::arith::Adder_kind::rca   : break;
  }
  return "rca";
}
const char* multiplier_name(synth::arith::Mult_kind kind) {
  switch (kind) {
    case synth::arith::Mult_kind::array: return "array";
    case synth::arith::Mult_kind::tree : return "tree";
    case synth::arith::Mult_kind::csa  : break;
  }
  return "csa";
}
void cost(Writer& w, const char* key, const Logical_cost& c) {
  w.Key(key);
  w.StartObject();
  number(w, "static_logic", c.static_logic);
  number(w, "inverters", c.inverters);
  number(w, "domino", c.domino);
  number(w, "total", c.total());
  w.EndObject();
}
}  // namespace
std::string native_report(const Logical_design& design, const Design_options& options, std::string_view tmap, double elapsed_ms,
                          uint64_t peak_bytes, std::string_view provenance_directory, std::string_view provenance,
                          std::span<const std::string> artifact_paths, std::span<const Mapping_trial> trials) {
  rapidjson::StringBuffer buffer;
  Writer                  w(buffer);
  w.StartObject();
  number(w, "schema_version", 5);
  w.Key("kind");
  text(w, "usyn");
  w.Key("algorithm");
  text(w, "register-rooted-xag-v1");
  w.Key("target");
  text(w, "cmos");
  w.Key("tmap");
  text(w, tmap);
  w.Key("top");
  text(w, design.top->get_name());
  w.Key("scope");
  text(w, "definition-regions");
  w.Key("output");
  text(w, tmap == "none" ? "logical-cmos" : "mapped-cmos");
  w.Key("mapping_trials");
  w.StartArray();
  for (const auto& trial : trials) {
    w.StartObject();
    flag(w, "p1", trial.p1);
    flag(w, "multi_rep", trial.multi_rep);
    flag(w, "selected", trial.selected);
    number(w, "work_limit", trial.work_limit);
    number(w, "work_used", trial.work_used);
    number(w, "search_used", trial.search_used);
    w.Key("elapsed_ms");
    w.Double(trial.elapsed_ms);
    w.Key("reason");
    text(w, trial.reason);
    if (trial.cost) {
      number(w, "physical_logic_gates", trial.cost->gates);
      w.Key("physical_logic_area");
      w.Double(trial.cost->area);
      // An untimed map's delay is a unit-delay level count, never picoseconds.
      w.Key(trial.cost->delay_ps ? "maximum_region_delay_ps" : "maximum_region_logic_levels");
      w.Double(trial.cost->maximum_region_delay);
    }
    w.EndObject();
  }
  w.EndArray();
  w.Key("cache");
  w.StartObject();
  flag(w, "available", true);
  flag(w, "enabled", design.cache.enabled);
  number(w, "reused", design.cache.reused);
  number(w, "misses", design.cache.misses);
  number(w, "invalid", design.cache.invalid);
  number(w, "refused", design.cache.refused);
  number(w, "credit_misses", design.cache.credit_misses);
  number(w, "kept", design.cache.kept);
  number(w, "stored", design.cache.stored);
  number(w, "store_failures", design.cache.store_failures);
  number(w, "io_work", design.cache.io_work);
  number(w, "replayed_search_work", design.cache.replayed_search_work);
  number(w, "replayed_structural_work", design.cache.replayed_structural_work);
  w.EndObject();
  w.Key("constraints");
  w.StartObject();
  number(w, "logical_inputs", options.logical.endpoint.gates.logical_inputs);
  number(w, "stack", options.logical.endpoint.gates.stack);
  number(w, "branches", options.logical.endpoint.gates.branches);
  number(w, "cut_inputs", options.logical.endpoint.window.inputs);
  number(w, "clock_phases", options.logical.endpoint.clock_phases);
  w.EndObject();
  // The lowering choices change the translated network, so a report must name
  // them: an `adder=rca multiplier=array` run and the default are otherwise
  // indistinguishable here.
  w.Key("arithmetic");
  w.StartObject();
  w.Key("adder");
  text(w, options.auto_sum_adder ? "auto" : adder_name(options.adder));
  number(w, "adder_block", static_cast<uint64_t>(options.adder_block));
  w.Key("multiplier");
  text(w, multiplier_name(options.multiplier));
  w.Key("mux_lowering");
  text(w, options.mux_tree ? "tree" : "decode");
  flag(w, "eq_balance", options.eq_balance);
  w.EndObject();
  w.Key("native_optimization");
  w.StartObject();
  flag(w, "p1", options.logical.pre_optimize);
  flag(w, "sop_tree", options.sop_tree);
  flag(w, "multi_rep", options.multi_rep);
  flag(w, "cmos_cleanup", options.cmos_cleanup || options.sop_tree || options.multi_rep);
  w.Key("cost_mode");
  text(w, options.cost_policy);
  number(w, "cost_model_work", options.cost_model_work);
  if (options.cost_model) {
    number(w, "legal_cells", options.cost_model->admitted_cells);
    number(w, "unsupported_cell_functions", options.cost_model->skipped_cells);
  }
  flag(w, "npn4", options.logical.residual.npn4);
  flag(w, "sweep", options.logical.residual.sweep);
  flag(w, "balance", options.logical.residual.balance);
  number(w, "balance_dup_limit", options.logical.residual.balance_dup_limit);
  number(w, "sweep_inputs", options.logical.residual.sweep_inputs);
  number(w, "sweep_table_words", options.logical.residual.sweep_table_words);
  number(w, "p1_sweep_inputs", options.logical.residual.p1_sweep_inputs);
  flag(w, "mux_balance", options.logical.residual.mux_balance);
  number(w, "mux_balance_min_arms", options.logical.residual.mux_balance_min_arms);
  number(w, "mux_balance_area_pct", options.logical.residual.mux_balance_area_pct);
  number(w, "rewrite_cuts", options.logical.residual.rewrite_cuts);
  w.EndObject();
  w.Key("endpoint_search");
  w.StartObject();
  flag(w, "fast_accept", options.logical.endpoint.fast_accept);
  number(w, "local_divisors", options.logical.endpoint.local_divisors);
  number(w, "local_candidates", options.logical.endpoint.local_candidates);
  number(w, "image_cache_entries", options.logical.endpoint.image_cache_entries);
  number(w, "image_cache_bytes", options.logical.endpoint.image_cache_bytes);
  number(w, "pair_candidates", options.logical.pair_candidates);
  number(w, "pair_trials", options.logical.pair_trials);
  number(w, "pair_choices", options.logical.pair_choices);
  number(w, "pair_inputs", options.logical.pair_inputs);
  number(w, "pair_work", options.logical.pair_work);
  w.EndObject();
  w.Key("elapsed_ms");
  w.Double(elapsed_ms);
  number(w, "peak_bytes", peak_bytes);
  uint64_t state = 0, endpoints = 0, cells = 0, frozen_cells = 0;
  for (const auto& region : design.regions) {
    const auto before  = cells;
    state             += region.selected.state_bits.size();
    endpoints         += region.selected.selected.endpoints.size();
    for (const auto& endpoint : region.selected.selected.endpoints) {
      cells += endpoint.cells.size();
    }
    frozen_cells += region.selected.frozen ? region.selected.frozen->cells.size() : cells - before;
  }
  w.Key("totals");
  w.StartObject();
  number(w, "regions", design.regions.size());
  number(w, "register_bits", state);
  number(w, "eligible_endpoints", endpoints);
  number(w, "selected_cells", cells);
  number(w, "frozen_cells", frozen_cells);
  number(w, "shared_phase_one_uses", cells - frozen_cells);
  w.EndObject();
  w.Key("regions");
  w.StartArray();
  size_t artifact_index = 0;
  for (const auto& region : design.regions) {
    w.StartObject();
    w.Key("module");
    text(w, region.module_name);
    flag(w, "cache_reused", region.cache_reused);
    w.Key("cache_key");
    text(w, region.cache_key);
    if (artifact_index < artifact_paths.size()) {
      w.Key("artifact");
      w.StartObject();
      number(w, "version", Frozen_region::version);
      w.Key("path");
      text(w, artifact_paths[artifact_index]);
      w.EndObject();
    }
    ++artifact_index;
    const auto& r           = region.report;
    uint64_t    local_cells = 0;
    for (const auto& endpoint : region.selected.selected.endpoints) {
      local_cells += endpoint.cells.size();
    }
    const auto unique_cells = region.selected.frozen ? region.selected.frozen->cells.size() : local_cells;
    number(w, "frozen_cells", unique_cells);
    number(w, "shared_phase_one_uses", local_cells - unique_cells);
    flag(w, "search_exhausted", r.exhausted);
    number(w, "identity_fallbacks", r.identity_fallbacks);
    number(w, "search_credits", region.search_credits);
    w.Key("credit_floor");  // `search` is the per-endpoint search array below
    w.StartObject();
    number(w, "work", region.search.work);
    number(w, "floor", region.search.floor);
    flag(w, "bound", region.search.bound);
    number(w, "credits", region.search.credits);
    w.EndObject();
    number(w, "structural_work", region.structural_work);
    cost(w, "before", r.before);
    cost(w, "after_pairs", r.after_pairs);
    cost(w, "after_residual", r.after_residual);
    cost(w, "after", r.after);
    w.Key("work");
    w.StartObject();
    number(w, "admission", r.work.admission);
    number(w, "p1", r.work.p1);
    number(w, "selection", r.work.selection);
    number(w, "pairs", r.work.pairs);
    number(w, "residual", r.work.residual);
    number(w, "feedback", r.work.feedback);
    number(w, "cleanup", r.work.cleanup);
    number(w, "cmos_cleanup", region.cmos_search.work);
    number(w, "total", r.work.total() + region.cmos_search.work);
    w.EndObject();
    w.Key("pairs");
    w.StartObject();
    number(w, "candidates", r.pairs.candidates);
    number(w, "attempts", r.pairs.attempts);
    number(w, "combinations", r.pairs.combinations);
    number(w, "choices", r.pairs.choices);
    number(w, "choice_combinations", r.pairs.choice_combinations);
    number(w, "trials", r.pairs.trials);
    number(w, "refreshes", r.pairs.refreshes);
    number(w, "requeues", r.pairs.requeues);
    number(w, "stale_skips", r.pairs.stale_skips);
    number(w, "overlap_skips", 0);  // schema-5 compatibility: valid overlaps are now refreshed
    number(w, "wins", r.pairs.wins);
    number(w, "work", r.pairs.work);
    number(w, "shared_nodes", r.pairs.shared_nodes);
    number(w, "more_than_two", r.pairs.more_than_two);
    number(w, "domain_skips", r.pairs.domain_skips);
    number(w, "support_skips", r.pairs.support_skips);
    number(w, "window_skips", r.pairs.window_skips);
    number(w, "gain_skips", r.pairs.gain_skips);
    number(w, "bounded_windows", r.pairs.bounded_windows);
    number(w, "fanout_windows", r.pairs.fanout_windows);
    number(w, "fanout_ports", r.pairs.fanout_ports);
    number(w, "fanout_skips", r.pairs.fanout_skips);
    number(w, "fanout_wins", r.pairs.fanout_wins);
    number(w, "joint_windows", r.pairs.joint_windows);
    number(w, "joint_source_pairs", r.pairs.joint_source_pairs);
    number(w, "joint_partitions", r.pairs.joint_partitions);
    number(w, "joint_candidates", r.pairs.joint_candidates);
    number(w, "joint_divisors", r.pairs.joint_divisors);
    number(w, "joint_combinations", r.pairs.joint_combinations);
    number(w, "joint_wins", r.pairs.joint_wins);
    number(w, "joint_care_windows", r.pairs.joint_care_windows);
    number(w, "joint_care_partitions", r.pairs.joint_care_partitions);
    number(w, "joint_care_phases", r.pairs.joint_care_phases);
    number(w, "joint_care_attempts", r.pairs.joint_care_attempts);
    number(w, "joint_care_retained", r.pairs.joint_care_retained);
    number(w, "joint_care_bytes", r.pairs.joint_care_bytes);
    number(w, "joint_care_combinations", r.pairs.joint_care_combinations);
    number(w, "joint_care_wins", r.pairs.joint_care_wins);
    number(w, "joint_recode_windows", r.pairs.joint_recode_windows);
    number(w, "joint_recode_partitions", r.pairs.joint_recode_partitions);
    number(w, "joint_recode_encodings", r.pairs.joint_recode_encodings);
    number(w, "joint_recode_phases", r.pairs.joint_recode_phases);
    number(w, "joint_recode_attempts", r.pairs.joint_recode_attempts);
    number(w, "joint_recode_retained", r.pairs.joint_recode_retained);
    number(w, "joint_recode_bytes", r.pairs.joint_recode_bytes);
    number(w, "joint_recode_combinations", r.pairs.joint_recode_combinations);
    number(w, "joint_recode_wins", r.pairs.joint_recode_wins);
    flag(w, "exhausted", r.pairs.exhausted);
    w.EndObject();
    w.Key("p1");
    w.StartObject();
    number(w, "cost_before", r.p1.cost_before);
    number(w, "cost_after", r.p1.cost_after);
    number(w, "rewrite_wins", r.p1.rewrite_wins);
    number(w, "sweep_wins", r.p1.sweep_wins);
    number(w, "balance_wins", r.p1.balance_wins);
    number(w, "sweep_confirmations", r.p1.sweep_confirmations);
    number(w, "mux_chains", r.p1.mux_chains);
    number(w, "mux_arms", r.p1.mux_arms);
    number(w, "mux_wins", r.p1.mux_wins);
    number(w, "work", r.work.p1);
    flag(w, "exhausted", r.p1.exhausted);
    w.Key("limits");
    w.StartArray();
    for (const auto& limit : r.p1.limits) {
      text(w, limit);
    }
    w.EndArray();
    w.EndObject();
    w.Key("cmos_cleanup");
    w.StartObject();
    number(w, "sop_roots", region.cmos_cleanup.sop_roots);
    number(w, "sop_cofactors", region.cmos_cleanup.sop_cofactors);
    number(w, "cost_before", region.cmos_cleanup.cost_before);
    number(w, "cost_after", region.cmos_cleanup.cost_after);
    number(w, "rewrite_wins", region.cmos_cleanup.rewrite_wins);
    number(w, "resub_wins", region.cmos_cleanup.resub_wins);
    number(w, "balance_wins", region.cmos_cleanup.balance_wins);
    number(w, "sweep_confirmations", region.cmos_cleanup.sweep_confirmations);
    number(w, "mux_chains", region.cmos_cleanup.mux_chains);
    number(w, "mux_arms", region.cmos_cleanup.mux_arms);
    number(w, "mux_wins", region.cmos_cleanup.mux_wins);
    number(w, "sweep_wins", region.cmos_cleanup.sweep_wins);
    number(w, "work", region.cmos_search.work);
    number(w, "credit_floor", region.cmos_search.floor);
    flag(w, "credit_bound", region.cmos_search.bound);
    number(w, "credits", region.cmos_search.credits);
    flag(w, "exhausted", region.cmos_cleanup.exhausted);
    w.Key("limits");
    w.StartArray();
    for (const auto& limit : region.cmos_cleanup.limits) {
      text(w, limit);
    }
    w.EndArray();
    w.EndObject();
    w.Key("multi_rep");
    w.StartObject();
    number(w, "classes", region.choices.classes);
    number(w, "candidates", region.choices.candidates);
    number(w, "rejected", region.choices.rejected);
    number(w, "retained", region.choices.retained);
    number(w, "extractions", region.choices.extractions);
    number(w, "selections", region.choices.selections);
    number(w, "cycles_rejected", region.choices.cycles);
    number(w, "bdd_nodes", region.choices.bdd_nodes);
    number(w, "dsd_blocks", region.choices.dsd_blocks);
    number(w, "sop_cubes", region.choices.sop_cubes);
    number(w, "scratch_nodes", region.choices.scratch_nodes);
    flag(w, "limited", region.choices.limited);
    w.Key("estimate_status_before");
    text(w, status_name(region.choices.before.status));
    w.Key("estimate_status_after");
    text(w, status_name(region.choices.after.status));
    flag(w, "estimate_limited", region.choices.before.limited || region.choices.after.limited);
    w.Key("estimate_kind");
    text(w, options.cost_model ? "legal-cell-covering-proxy" : "weighted-live-xag");
    number(w, "gates_before", region.choices.before.gates);
    number(w, "gates_after", region.choices.after.gates);
    w.Key("area_before");
    w.Double(region.choices.before.area);
    w.Key("area_after");
    w.Double(region.choices.after.area);
    w.EndObject();
    w.Key("residual");
    w.StartObject();
    flag(w, "skipped", r.residual.skipped);
    flag(w, "accepted", r.residual_accepted);
    number(w, "sweep_confirmations", r.residual.sweep_confirmations);
    number(w, "sweep_wins", r.residual.sweep_wins);
    number(w, "balance_groups", r.residual.balance_groups);
    number(w, "balance_wins", r.residual.balance_wins);
    number(w, "balance_duplicates", r.residual.balance_duplicates);
    number(w, "mux_chains", r.residual.mux_chains);
    number(w, "mux_arms", r.residual.mux_arms);
    number(w, "mux_wins", r.residual.mux_wins);
    number(w, "cost_before", r.residual.cost_before);
    number(w, "cost_after", r.residual.cost_after);
    number(w, "rewrite_windows", r.residual.rewrite_windows);
    number(w, "rewrite_wins", r.residual.rewrite_wins);
    number(w, "resub_windows", r.residual.resub_windows);
    number(w, "resub_wins", r.residual.resub_wins);
    number(w, "candidates", r.residual.candidates);
    number(w, "depth_rejections", r.residual.depth_rejections);
    number(w, "cost_rejections", r.residual.cost_rejections);
    number(w, "reference_visits", r.residual.reference_visits);
    flag(w, "exhausted", r.residual.exhausted);
    w.Key("limits");
    w.StartArray();
    for (const auto& limit : r.residual.limits) {
      text(w, limit);
    }
    w.EndArray();
    number(w, "feedback_rounds", r.feedback_rounds);
    number(w, "feedback_attempts", r.feedback_attempts);
    number(w, "feedback_wins", r.feedback_wins);
    w.EndObject();
    w.Key("search");
    w.StartArray();
    for (const auto& search : r.initial) {
      w.StartObject();
      flag(w, "whole_admitted", search.whole_admitted);
      flag(w, "window_used", search.window_used);
      number(w, "one_cell_attempts", search.one_cell_attempts);
      number(w, "two_phase_attempts", search.two_phase_attempts);
      number(w, "new_divisor_attempts", search.new_divisor_attempts);
      number(w, "single_divisor_attempts", search.single_divisor_attempts);
      number(w, "parallel_divisor_attempts", search.parallel_divisor_attempts);
      number(w, "deferred_divisor_bytes", search.deferred_divisor_bytes);
      number(w, "existing_care_attempts", search.existing_care_attempts);
      number(w, "existing_care_images", search.existing_care_images);
      number(w, "divisor_images", search.divisor_images);
      number(w, "divisor_image_hits", search.divisor_image_hits);
      number(w, "image_cache_bytes", search.image_cache_bytes);
      number(w, "local_attempts", search.local_attempts);
      number(w, "local_wins", search.local_wins);
      number(w, "local_work", search.local_work);
      number(w, "completion_phases", search.completion_phases);
      number(w, "completion_attempts", search.completion_attempts);
      number(w, "removal_attempts", search.residual_attempts);
      number(w, "boundaries", search.boundaries);
      number(w, "boundary_trials", search.boundary_trials);
      number(w, "boundary_replacements", search.boundary_replacements);
      number(w, "boundary_bytes_peak", search.boundary_bytes_peak);
      number(w, "boundary_wins", search.boundary_wins);
      number(w, "analysis_tables", search.analysis_tables);
      number(w, "analysis_hits", search.analysis_hits);
      number(w, "analysis_cache_bytes", search.analysis_cache_bytes);
      number(w, "function_hits", search.function_hits);
      number(w, "boundary_work", search.boundary_work);
      number(w, "boundary_two_cell_work", search.boundary_two_cell_work);
      number(w, "boundary_multi_cell_work", search.boundary_multi_cell_work);
      number(w, "one_cell_work", search.one_cell_work);
      number(w, "admission_work", search.admission_work);
      number(w, "two_phase_work", search.two_phase_work);
      number(w, "removal_work", search.residual_work);
      flag(w, "exhausted", search.exhausted);
      w.Key("limits");
      w.StartArray();
      for (const auto& limit : search.limits) {
        text(w, limit);
      }
      w.EndArray();
      w.EndObject();
    }
    w.EndArray();
    w.Key("endpoints");
    w.StartArray();
    for (const auto& endpoint : region.selected.selected.endpoints) {
      w.StartObject();
      w.Key("name");
      text(w, endpoint.name);
      number(w, "state_index", endpoint.state_index);
      flag(w, "whole_cone", endpoint.whole_cone);
      w.Key("origin");
      text(w, endpoint.origin);
      w.Key("cells");
      w.StartArray();
      for (const auto& cell : endpoint.cells) {
        w.StartObject();
        number(w, "phase", cell.phase);
        flag(w, "latch", cell.latch);
        number(w, "logical_inputs", std::popcount(cell.metrics.support));
        number(w, "support_mask", cell.metrics.support);
        number(w, "stack", cell.metrics.stack);
        number(w, "branches", cell.metrics.branches);
        number(w, "transistors", cell.metrics.transistors);
        flag(w, "output_inverted", cell.formula.output_inverted);
        w.Key("inputs");
        w.StartArray();
        for (size_t i = 0; i < cell.inputs.size(); ++i) {
          w.StartObject();
          number(w, "signal", cell.inputs[i].id);
          flag(w, "inverted", cell.inputs[i].inverted);
          w.Key("producer");
          w.Int(cell.producers[i]);
          w.EndObject();
        }
        w.EndArray();
        w.Key("formula");
        w.StartArray();
        for (const auto& node : cell.formula.nodes) {
          w.StartObject();
          number(w, "kind", static_cast<unsigned>(node.kind));
          number(w, "left", node.left);
          number(w, "right", node.right);
          number(w, "variable", node.variable);
          flag(w, "inverted", node.inverted);
          w.EndObject();
        }
        w.EndArray();
        w.EndObject();
      }
      w.EndArray();
      w.EndObject();
    }
    w.EndArray();
    w.EndObject();
  }
  w.EndArray();
  w.Key("provenance");
  w.StartObject();
  w.Key("directory");
  text(w, provenance_directory);
  w.Key("capture");
  w.RawValue(provenance.data(), provenance.size(), rapidjson::kObjectType);
  w.EndObject();
  w.EndObject();
  return buffer.GetString();
}

std::string mapping_report(const synth::Mapped_design& design, std::string_view provider, const synth::Tmap_options& options) {
  rapidjson::StringBuffer buffer;
  Writer                  w(buffer);
  w.StartObject();
  number(w, "schema_version", 1);
  number(w, "sharing_fanout", options.sharing_fanout);
  w.Key("kind");
  text(w, "technology-map");
  w.Key("provider");
  text(w, provider);
  w.Key("top");
  text(w, design.top->get_name());
  w.Key("library");
  text(w, options.library);
  w.Key("delay_target_ps");
  w.Double(options.delay_ps);
  w.Key("delay_unit");
  text(w, design.delay_ps ? "ps" : "logic-levels");
  w.Key("physical");
  w.StartObject();
  number(w, "max_fanout", options.max_fanout);
  flag(w, "boundary", options.boundary);
  flag(w, "boundary_buffer", options.boundary_buffer);
  w.Key("boundary_drive");
  text(w, options.boundary_drive);
  w.Key("io_load");
  w.Double(options.io_load);
  number(w, "boundary_rounds", static_cast<uint64_t>(options.boundary_rounds));
  w.Key("reg_margin");
  text(w, options.reg_margin);
  number(w, "area_relax", options.area_relax_pct);
  w.EndObject();
  w.Key("scope");
  text(w, "definition-regions");
  uint64_t hits = 0, misses = 0;
  double   hit_ms = 0, miss_ms = 0;
  for (const auto& row : design.regions) {
    if (std::string_view(row.cache) == "hit") {
      ++hits;
      hit_ms += row.ms;
    } else if (design.cache_enabled) {
      // A disabled cache maps every region without a lookup: no misses, the
      // same convention as pass.abc's incremental report.
      ++misses;
      miss_ms += row.ms;
    }
  }
  w.Key("incremental");
  w.StartObject();
  flag(w, "enabled", design.cache_enabled);
  number(w, "hits", hits);
  number(w, "misses", misses);
  number(w, "regions", design.regions.size());
  number(w, "invalid", design.cache_invalid);
  number(w, "store_failed", design.cache_store_failed);
  w.Key("hit_ms");
  w.Double(hit_ms);
  w.Key("miss_ms");
  w.Double(miss_ms);
  w.EndObject();
  w.Key("regions");
  w.StartArray();
  for (const auto& row : design.regions) {
    w.StartObject();
    w.Key("module");
    text(w, row.module);
    w.Key("cache");
    text(w, design.cache_enabled ? row.cache : "disabled");
    number(w, "gates", row.gates);
    w.Key("budget");
    w.Double(row.budget);
    w.Key("area");
    w.Double(row.area);
    // Untimed ABC delay is the unit-delay GENLIB trace (logic levels), never ps.
    w.Key("delay");
    if (design.delay_ps && row.delay >= 0) {
      w.Double(row.delay);
    } else {
      w.Null();
    }
    w.Key("logic_depth");
    w.Int(row.logic_depth);
    w.EndObject();
  }
  w.EndArray();
  w.EndObject();
  return buffer.GetString();
}
}  // namespace livehd::usyn

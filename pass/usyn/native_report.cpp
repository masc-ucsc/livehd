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
                          std::span<const std::string> artifact_paths) {
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
    number(w, "selection", r.work.selection);
    number(w, "pairs", r.work.pairs);
    number(w, "residual", r.work.residual);
    number(w, "feedback", r.work.feedback);
    number(w, "cleanup", r.work.cleanup);
    number(w, "total", r.work.total());
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
    w.Key("residual");
    w.StartObject();
    flag(w, "skipped", r.residual.skipped);
    flag(w, "accepted", r.residual_accepted);
    number(w, "rewrite_wins", r.residual.rewrite_wins);
    number(w, "resub_wins", r.residual.resub_wins);
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

std::string mapping_report(const synth::Mapped_design& design, std::string_view provider, std::string_view library) {
  rapidjson::StringBuffer buffer;
  Writer                  w(buffer);
  w.StartObject();
  number(w, "schema_version", 1);
  w.Key("kind");
  text(w, "technology-map");
  w.Key("provider");
  text(w, provider);
  w.Key("top");
  text(w, design.top->get_name());
  w.Key("library");
  text(w, library);
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
    w.Key("delay");
    w.Double(row.delay);
    w.Key("logic_depth");
    w.Int(row.logic_depth);
    w.EndObject();
  }
  w.EndArray();
  w.EndObject();
  return buffer.GetString();
}
}  // namespace livehd::usyn

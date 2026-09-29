// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "usyn_region.hpp"

#include <chrono>
#include <format>

#include "diag.hpp"
#include "host_mem.hpp"
#include "json_util.hpp"
#include "lut_cover.hpp"
#include "metrics.hpp"
#include "resource_budget.hpp"

namespace livehd::usyn {
namespace {
// An XOR source node is formable by the cover only when one domino gate admits
// its 4-literal, 2-series form on 2 inputs.
bool source_xor_nodes(const Search_options& search) {
  const auto& r = search.recipe;
  return r.support >= 2 && r.literals >= 4 && r.series >= 2;
}
}  // namespace

livehd::synth::Region_rewrite rewrite_region(const livehd::synth::Lnet& net, const livehd::synth::Region_ctx& ctx,
                                             const Search_options& search, std::string& report) {
  using Map                 = livehd::synth::Region_rewrite::Map;
  const auto&          opts = ctx.options;
  const auto           region = std::string_view{ctx.rb.module_name};
  const auto           start  = std::chrono::steady_clock::now();
  const double         entry_ms = ctx.elapsed_ms ? ctx.elapsed_ms() : 0.0;
  Resource_budget      effort;
  Resource_observation observed;
  effort.time_limit_ms = static_cast<double>(opts.time_budget_ms);
  effort.entry_bytes   = ctx.rss_entry;
  if (!opts.allow_oversize) {
    effort.growth_limit_bytes  = livehd::cost::budget_bytes(opts.memory_budget_mb);
    effort.process_limit_bytes = livehd::cost::configured_budget_bytes();
  }
  const auto sample = [&] { return observed.sample(livehd::cost::process_footprint_bytes()); };
  const auto since  = [&] {
    return entry_ms + std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
  };
  // Admission samples the process footprint (a syscall). The cover asks
  // often; re-sample at most every 2 ms.
  auto       last_admission = std::chrono::steady_clock::time_point{};
  bool       admitted_last  = true;
  const auto admit          = [&] {
    const auto now = std::chrono::steady_clock::now();
    if (admitted_last && now - last_admission < std::chrono::milliseconds(2)) {
      return true;
    }
    last_admission = now;
    admitted_last  = effort.admit(since(), sample());
    return admitted_last;
  };

  livehd::synth::Region_rewrite rewrite;
  std::string  status = "abc_fallback", reason, source_metrics = "null", variant_name = "none";
  double       cover_ms = 0;
  Cover_result cover;
  // The source network is the blaster's own gates, hashed (Lnet STRASH),
  // never optimized by the backend.
  std::optional<livehd::synth::Lnet> source;
  // Blasted memories (unless covered) and abc=only never reach the cover, so
  // no cover limit can turn them into a fallback.
  const bool backend_only = search.abc_mode == Search_options::Abc_mode::only
                            || (!search.cover_memories && region.find("cgen_memory") != std::string_view::npos);
  if (backend_only) {
    // No cover: the backend maps the original region logic with its own flow.
    status       = "abc_only";
    variant_name = "only";
  } else if (!admit()) {
    reason = effort.reason;
  } else if (net.size() > search.max_nodes) {
    reason = "source node limit";
  } else {
    source = livehd::synth::strash(net, {source_xor_nodes(search), admit});
    if (!source) {
      reason = effort.reason.empty() ? "source import incomplete" : effort.reason;
    } else if (source->size() > search.max_nodes) {
      reason = "source node limit";
      source.reset();
    }
  }
  if (source) {
    // The LUT cover: domino gates and static (non-unate) LUTs minimizing the
    // transistor proxy. `lhd lec` checks the stitched netlist.
    source_metrics           = source_metrics_json(*source);
    variant_name             = "cover";
    auto guarded_search      = search;
    guarded_search.admission = admit;
    const auto cover_start   = std::chrono::steady_clock::now();
    cover                    = lut_cover(*source, guarded_search);
    cover_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - cover_start).count();
    if (cover.status == Status::invalid) {
      livehd::diag::err("pass.usyn", "invalid-decomposition", "internal")
          .msg("LUT cover failed in region '{}': {}", region, cover.reason)
          .fatal();
      return rewrite;
    }
    if (cover.status != Status::feasible) {
      reason = cover.reason.empty() ? effort.reason : cover.reason;
    } else {
      // Hand the cover network to the backend: technology mapping only
      // (tmap), or its own optimize-and-map flow (opt).
      auto logic = cover_network(*source, cover);
      if (!logic) {
        livehd::diag::err("pass.usyn", "invalid-stitch", "internal")
            .msg("cover network construction failed in region '{}'", region)
            .fatal();
        return rewrite;
      }
      rewrite.logic = std::move(*logic);
      if (search.abc_mode == Search_options::Abc_mode::opt) {
        rewrite.map  = Map::flow;
        status       = "abc_opt";
        variant_name = "opt";
      } else {
        rewrite.map  = Map::tmap;
        status       = "abc_tmap";
        variant_name = "tmap";
      }
    }
  }
  if (status == "abc_fallback" && reason.empty()) {
    reason = effort.reason;
  }
  if (status == "abc_fallback" && !search.fallback && !effort.reason.empty() && reason == effort.reason) {
    // A time or memory budget is a resource refusal, reported exactly as the
    // pass.abc flow reports one (memory-oversize / time budget), never mapped.
    const bool process_gate = reason.starts_with("process");
    const auto limit_mib    = (process_gate ? effort.process_limit_bytes : effort.growth_limit_bytes) >> 20;
    const auto used_mib     = (process_gate || effort.peak_bytes < effort.entry_bytes ? effort.peak_bytes
                                                                                      : effort.peak_bytes - effort.entry_bytes)
                          >> 20;
    const auto why = effort.out_of_time
                         ? std::format("the usyn cover ran out of time: {:.0f} ms, budget {:.0f} ms ({})",
                                       effort.elapsed_ms, effort.time_limit_ms, reason)
                         : std::format("the usyn cover does not fit in memory: {} MiB, budget {} MiB ({})",
                                       used_mib, limit_mib, reason);
    if (effort.out_of_time ? static_cast<bool>(ctx.refuse_time) : static_cast<bool>(ctx.refuse_memory)) {
      (effort.out_of_time ? ctx.refuse_time : ctx.refuse_memory)(why);
      rewrite.map = Map::refused;
      sample();
      report = std::format(R"({{"region":"{}","status":"refused","reason":"{}"}})",
                           livehd::json_util::escape(region), livehd::json_util::escape(reason));
      return rewrite;
    }
  }
  if (status == "abc_fallback" && !search.fallback) {
    livehd::diag::err("pass.usyn", "cover-unavailable", "unsupported")
        .msg("region '{}' ({} source nodes) was not covered: {}", region, net.size(), reason.empty() ? "unknown" : reason)
        .hint("usyn maps every non-memory region through its cover; raise pass.usyn.max_nodes or the region budget, "
              "or --set pass.usyn.fallback=true to map it with the pass.abc flow instead")
        .fatal();
    return rewrite;
  }
  sample();
  const auto& din = cover.domino_inputs;
  report          = std::format(
      R"({{"region":"{}","status":"{}","reason":"{}","variant":"{}","budget_ps":{},"cover_ms":{},)"
      R"("domino":{},"nonunate":{},"aliases":{},"constants":{},"cover_cost":{},"domino_cost":{},"nonunate_cost":{},"domino_literals":{},"flow_cost":{},"domino_in2":{},"domino_in3":{},"domino_in4":{},"domino_in5":{},"domino_in6":{},"domino_in7":{},"domino_in8":{},"domino_s1":{},"domino_s2":{},"domino_s3":{},"domino_s4":{},"domino_s5":{},"domino_s6":{},"cones_1gate":{},"cones_2gates":{},"cones_3gates":{},"cones_4plus":{},"outputs_shallow":{},"outputs_deep":{},"outputs_wire":{},"covered_nodes":{},"replicated_nodes":{},"source_lits_pos":{},"source_lits_neg":{},"cover_cuts_kept":{},"cover_functions":{},)"
      R"("region_ms":{},"source_metrics":{},"resources":{{"memory_scope":"process","sampled_peak_bytes":{},"exhausted":{},"reason":"{}"}}}})",
      livehd::json_util::escape(region),
      status,
      livehd::json_util::escape(reason),
      variant_name,
      ctx.budget,
      cover_ms,
      cover.domino,
      cover.nonunate,
      cover.aliases,
      cover.constants,
      cover.cost,
      cover.domino_cost,
      cover.nonunate_cost,
      cover.domino_literals,
      cover.flow_cost,
      din[2],
      din[3],
      din[4],
      din[5],
      din[6],
      din[7],
      din[8],
      cover.domino_series[1],
      cover.domino_series[2],
      cover.domino_series[3],
      cover.domino_series[4],
      cover.domino_series[5],
      cover.domino_series[6],
      cover.cone_gates[1],
      cover.cone_gates[2],
      cover.cone_gates[3],
      cover.cone_gates[4],
      cover.outputs_shallow,
      cover.outputs_deep,
      cover.outputs_wire,
      cover.covered_nodes,
      cover.replicated_nodes,
      cover.source_literals_pos,
      cover.source_literals_neg,
      cover.cuts,
      cover.functions,
      std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count(),
      source_metrics,
      observed.peak_bytes,
      !effort.reason.empty(),
      livehd::json_util::escape(effort.reason));
  return rewrite;
}
}  // namespace livehd::usyn

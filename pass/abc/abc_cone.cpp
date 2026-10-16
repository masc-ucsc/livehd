// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include <chrono>
#include <cmath>
#include <format>

#include "abc_backend.hpp"
#include "cone_map.hpp"
#include "diag.hpp"
#include "host_mem.hpp"

namespace livehd::abc {
namespace {
std::optional<synth::Cone_mapping> map_cone(const synth::Lnet& logic, const synth::Cone_boundary& boundary,
                                            const std::string& liberty) {
  if (logic.inputs().size() > 12 || logic.outputs().empty() || logic.outputs().size() > 8 || logic.size() > 128
      || !logic.latches().empty() || boundary.inputs.size() != logic.inputs().size()
      || boundary.outputs.size() != logic.outputs().size() || !std::isfinite(boundary.budget_ps) || boundary.budget_ps <= 0) {
    return {};
  }
  for (const auto& in : boundary.inputs) {
    if (!std::isfinite(in.arrival_ps) || in.arrival_ps < 0 || !std::isfinite(in.slew_ps) || in.slew_ps < 0) {
      return {};
    }
  }
  for (const auto& out : boundary.outputs) {
    if (!std::isfinite(out.required_ps) || !std::isfinite(out.load_ff) || out.load_ff < 0) {
      return {};
    }
  }
  Map_options options;
  options.library          = liberty;
  options.delay            = std::format("{}", std::max<int64_t>(1, boundary.budget_ps));
  options.map_register     = false;
  options.boundary         = false;
  options.boundary_buffer  = false;
  options.boundary_drive   = "none";
  options.area_flow        = "none";
  options.area_relax_pct   = 0;
  options.time_budget_ms   = 5000;
  options.memory_budget_mb = 12288;
  partition::Region_body body;
  body.module_name = "placed_critical_cone";
  synth::Region_ctx ctx(body, options);
  ctx.timing_requested  = true;
  ctx.budget            = boundary.budget_ps;
  ctx.input_ge          = logic.size();
  ctx.physical_boundary = &boundary;
  auto       start      = std::chrono::steady_clock::now();
  const auto elapsed = [&] { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count(); };
  // The footprint is the whole lhd process (the placed design, its STA, ...),
  // so admit on the growth since this cone started, not on the absolute size.
  const uint64_t entry_bytes = cost::process_footprint_bytes();
  bool           refused     = false;
  ctx.elapsed_ms             = elapsed;
  ctx.admission              = [&](std::string_view) {
    const auto now = cost::process_footprint_bytes();
    return !refused && elapsed() < 5000 && (now <= entry_bytes || now - entry_bytes < (uint64_t{12} << 30));
  };
  ctx.fits        = [&](uint64_t, size_t, size_t) { return ctx.admission("memory"); };
  ctx.refuse_time = ctx.refuse_memory = [&](std::string) { refused = true; };
  ctx.trace_stage                     = [](std::string_view) {};
  ctx.pause_graph = ctx.resume_graph = [] {};
  ctx.critical_output                = [](size_t, synth::Region_qor&) {};
  // A cone is an optional trial: on a failure (a backend `.fatal()` records an
  // error, then throws) or a refusal the caller keeps the original cone, so
  // retract what the trial reported instead of failing the whole run.
  auto&      sink                    = livehd::diag::sink();
  const auto mark                    = sink.mark();
  try {
    Abc_backend backend(options);
    auto        scope = backend.region_scope();
    (void)backend.plan(ctx);
    if (backend.start(ctx) == synth::Region_backend::Start::failed) {
      sink.rollback(mark);
      return {};
    }
    // The 5 s budget is for mapping; the per-cone Liberty read does not count.
    start = std::chrono::steady_clock::now();
    synth::Region_blast blast;
    blast.lnet        = logic;
    blast.blast_total = logic.size();
    synth::Region_qor qor;
    const auto        mapped = backend.map(ctx, blast, {}, qor);
    if (!mapped || refused || !ctx.admission("complete")) {
      sink.rollback(mark);
      return {};
    }
    synth::Cone_mapping result{*mapped, backend.cells()};
    backend.end_region();
    return result;
  } catch (const std::exception& e) {
    sink.rollback(mark);
    livehd::diag::warn("pass.abc", "cone-discarded", "internal").msg("placed critical cone left unmapped: {}", e.what()).emit();
    return {};
  }
}
const bool registered = synth::register_cone_provider("abc", map_cone);
}  // namespace
}  // namespace livehd::abc

// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include <chrono>
#include <cmath>
#include <format>
#include <limits>

#include "abc_backend.hpp"
#include "abc_boundary.hpp"
#include "abc_lnet.hpp"
#include "cone_map.hpp"
#include "diag.hpp"
#include "host_mem.hpp"

// clang-format off
extern "C" {
#include "base/abc/abc.h"
#include "base/main/main.h"
int Abc_NtkDarCec(Abc_Ntk_t*, Abc_Ntk_t*, int, int, int);
}
// clang-format on

namespace livehd::abc {
namespace {
// What a provider accepts and how it maps. A critical cone is small, has five seconds, and is mapped for delay
// alone. A whole color has no size limit, ten minutes, and the area recovery of ordinary region synthesis, because
// only the paths the placement found critical should pay for speed.
struct Cut_profile {
  size_t      inputs, outputs, nodes;
  double      limit_ms;
  bool        area_recovery;
  const char* module;
};
std::optional<synth::Cone_mapping> map_cut(const synth::Lnet& logic, const synth::Cone_boundary& boundary,
                                           const std::string& liberty, const Cut_profile& profile) {
  if (logic.inputs().size() > profile.inputs || logic.outputs().empty() || logic.outputs().size() > profile.outputs
      || logic.size() > profile.nodes || !logic.latches().empty() || boundary.inputs.size() != logic.inputs().size()
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
  if (!profile.area_recovery) {
    options.area_flow      = "none";
    options.area_relax_pct = 0;
  }
  partition::Region_body body;
  body.module_name = profile.module;
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
    return !refused && elapsed() < profile.limit_ms && (now <= entry_bytes || now - entry_bytes < (uint64_t{12} << 30));
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
    std::optional<Wire_load_scope> wires;
    if (!boundary.wire.length_um.empty()) {
      synth::Wire_load model;
      model.capacitance_ff_um = boundary.wire.capacitance_ff_um;
      model.resistance_ohm_um = boundary.wire.resistance_ohm_um;
      model.length_um         = boundary.wire.length_um;
      for (const auto& [fanout, length] : model.length_um) {
        model.samples[fanout] = 1;
      }
      wires.emplace(model, profile.module);
    }
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
    livehd::diag::warn("pass.abc", "cone-discarded", "internal").msg("{} left unmapped: {}", profile.module, e.what()).emit();
    return {};
  }
}
std::optional<synth::Cone_mapping> map_cone(const synth::Lnet& logic, const synth::Cone_boundary& boundary,
                                            const std::string& liberty) {
  return map_cut(logic, boundary, liberty, {12, 8, 128, 5000, false, "placed_critical_cone"});
}
// Two ways to map a color: with the area recovery of ordinary region synthesis, and for delay alone as a cone is.
std::optional<synth::Cone_mapping> map_color(const synth::Lnet& logic, const synth::Cone_boundary& boundary,
                                             const std::string& liberty) {
  const auto none = std::numeric_limits<size_t>::max();
  return map_cut(logic, boundary, liberty, {none, none, none, 600000, true, "placed_color"});
}
std::optional<synth::Cone_mapping> map_color_delay(const synth::Lnet& logic, const synth::Cone_boundary& boundary,
                                                   const std::string& liberty) {
  const auto none = std::numeric_limits<size_t>::max();
  return map_cut(logic, boundary, liberty, {none, none, none, 600000, false, "placed_color_delay"});
}
// Bounded combinational equivalence of two networks over the same inputs, output by output.
bool prove_equal(const synth::Lnet& before, const synth::Lnet& after) {
  if (before.inputs().size() != after.inputs().size() || before.outputs().size() != after.outputs().size()
      || !before.latches().empty() || !after.latches().empty()) {
    return false;
  }
  auto* previous = Abc_FrameReadGlobalFrame();
  auto* frame    = Abc_FrameCreate();
  if (!frame) {
    return false;
  }
  Abc_FrameEnter(frame);
  const auto strash = [](const synth::Lnet& net, const char* name) -> Abc_Ntk_t* {
    auto* ntk = static_cast<Abc_Ntk_t*>(lnet_to_abc(net, name, Lnet_names::proof));
    Abc_NtkAddDummyPiNames(ntk);
    Abc_NtkAddDummyPoNames(ntk);
    Abc_Ntk_t* logic = Abc_NtkToLogic(ntk);
    Abc_Ntk_t* aig   = logic ? Abc_NtkStrash(logic, 0, 1, 0) : nullptr;
    if (logic) {
      Abc_NtkDelete(logic);
    }
    Abc_NtkDelete(ntk);
    if (aig) {
      // The miter pairs inputs and outputs by name. A proof network names an input after its object id, which
      // differs between the two sides; the position is what both sides share.
      Abc_NtkShortNames(aig);
    }
    return aig;
  };
  auto*      a     = strash(before, "color_before");
  auto*      b     = strash(after, "color_after");
  const bool equal = a && b && Abc_NtkDarCec(a, b, 10000, 0, 0) == 1;
  if (a) {
    Abc_NtkDelete(a);
  }
  if (b) {
    Abc_NtkDelete(b);
  }
  Abc_FrameLeave(previous);
  Abc_FrameDestroy(frame);
  return equal;
}
const bool registered = synth::register_cone_provider("abc", map_cone) && synth::register_cone_provider("abc.color", map_color)
                        && synth::register_cone_provider("abc.color.delay", map_color_delay)
                        && synth::register_logic_prover("abc", prove_equal);
}  // namespace
}  // namespace livehd::abc

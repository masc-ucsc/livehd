// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include <algorithm>
#include <array>
#include <filesystem>
#include <format>

#include "abc_map.hpp"
#include "abc_salt.hpp"
#include "design_prepare.hpp"
#include "diag.hpp"
#include "tmap.hpp"
#include "tmap_cache.hpp"

namespace livehd::abc {
namespace {
synth::Tmap_result map_design(const std::shared_ptr<hhds::Graph>& top, const synth::Tmap_options& request) {
  using synth::Tmap_status;
  if (!std::filesystem::is_regular_file(request.library)) {
    return {Tmap_status::invalid, {}, "technology-mapping Liberty file does not exist: " + request.library};
  }
  if (request.admission && !request.admission("prepare")) {
    return {Tmap_status::refused, {}, "caller refused technology-mapping preparation"};
  }
  const std::array roots{top};
  auto             prepared = synth::prepare_design(roots, false, "pass.usyn.tmap");
  if (!prepared) {
    return {Tmap_status::invalid, {}, "technology-mapping preparation failed"};
  }
  auto        output = std::make_unique<synth::Mapped_design>();
  Map_options options;
  options.library            = request.library;
  // ABC reads `&nf -D` as an integer (atoi): a whole-picosecond target, never
  // scientific notation such as `1e+05`. A sub-picosecond target rounds up.
  options.delay = request.delay_ps > 0 ? std::format("{}", std::max<int64_t>(1, static_cast<int64_t>(request.delay_ps))) : "";
  options.memory_budget_mb   = request.memory_budget_mb;
  options.time_budget_ms     = request.time_budget_ms;
  options.admission          = request.admission;
  options.threads            = 1;
  options.ware_trials        = false;
  options.memory_fold        = synth::Memory_fold::Never;
  options.area_flow          = "none";
  options.large_ge           = 0;
  options.region_hook_recipe = "logical-tmap-v1";
  // Every region, including output-only logic, takes the existing mapping-only
  // branch. No failed candidate or embedded region flow can select ABC synthesis.
  options.region_hook        = [](const synth::Lnet& net, const synth::Region_ctx&) {
    synth::Region_rewrite result;
    result.map   = synth::Region_rewrite::Map::tmap;
    result.logic = net;
    return result;
  };
  // Provider identity and library data never enter the native USYN cache.
  const auto                         cells = liberty::resolve_dff_cells(options.library, options.dff_cell);
  std::unique_ptr<synth::Tmap_cache> cache;
  output->cache_enabled = !request.cache_directory.empty();
  if (output->cache_enabled) {
    const auto salt       = synth::Region_cache::make_salt(kAbcSrcSalt,
                                                           options.library,
                                                           options.map_register,
                                                           options.memory_fold,
                                                           options.memory_max_bits,
                                                           liberty::dff_selection_descriptor(cells, options.dff_cell));
    cache                 = std::make_unique<synth::Tmap_cache>(request.cache_directory,
                                                                salt,
                                                                uint64_t(request.memory_budget_mb) << 20,
                                                                request.admission);
    output->cache_invalid = cache->invalid();
    if (cache->refused()) {
      return {Tmap_status::refused, {}, "caller refused mapping cache admission"};
    }
  }
  Mapper mapper{options};
  mapper.set_outlib(&output->library);
  mapper.set_dff_cells(cells);
  mapper.set_incr(cache ? cache->regions() : nullptr);
  mapper.prepare_region_opts(prepared->resolve_graphs);
  bool       failed  = false;
  bool       refused = false;
  const auto build   = [&](const partition::Region_body& rb) {
    if (failed) {
      return;
    }
    if (request.admission && !request.admission("region")) {
      failed = refused = true;
      return;
    }
    mapper.map_region(rb);
    failed = mapper.admission_refusal() || mapper.time_refusal() || diag::sink().has_halting_errors();
  };
  const bool partitioned = Pass_partition::build_decomposition(prepared->resolve_graphs,
                                                               &output->library,
                                                               top->get_name(),
                                                               false,
                                                               build,
                                                               partition::Flatten_mode::off,
                                                               mapper.incremental(),
                                                               {},
                                                               1,
                                                               prepared->loops.preserved_defs);
  mapper.stop();
  if (refused) {
    return {Tmap_status::refused, {}, "caller refused technology-mapping region"};
  }
  if (const auto* reason = mapper.admission_refusal()) {
    return {Tmap_status::refused, {}, *reason};
  }
  if (const auto* reason = mapper.time_refusal()) {
    return {Tmap_status::refused, {}, *reason};
  }
  if (!partitioned || failed) {
    return {Tmap_status::invalid, {}, "technology mapping failed"};
  }
  const auto io = output->library.find_io(top->get_name());
  output->top   = io ? io->get_graph() : nullptr;
  if (!output->top) {
    return {Tmap_status::invalid, {}, "technology mapping produced no top definition"};
  }
  output->regions = mapper.qor();
  if (cache) {
    output->cache_store_failed = !cache->save();
    if (cache->refused()) {
      return {Tmap_status::refused, {}, "caller refused mapping cache publication"};
    }
  }
  return {Tmap_status::mapped, std::move(output), {}};
}

// pass_abc is always-linked in normal builds. Removing that optional target
// removes this registration without changing the logical synthesis closure.
[[maybe_unused]] const bool registered = synth::register_tmap_provider("abc", map_design);
}  // namespace
}  // namespace livehd::abc

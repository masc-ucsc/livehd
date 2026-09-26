// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

// The region driver: what every synthesis backend shares across a whole
// decomposition (abc_cleanup.md section 5). Per region it resolves the
// options, consults the incremental cache, translates the region
// (region_blast.hpp), hands it to its backend (region_backend.hpp) and writes
// the mapped cells back (region_writer.hpp). Across regions it schedules
// parallel lanes under memory admission, and it runs the ware trials.
#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "hhds/graph.hpp"
#include "lane_scheduler.hpp"
#include "liberty_dff.hpp"
#include "pass_partition.hpp"
#include "region_backend.hpp"
#include "region_qor.hpp"
#include "region_writer.hpp"
#include "ware_module.hpp"

namespace livehd::synth {

class Region_cache;

// Stats-only mode (no --emit-dir): summarize what would be mapped.
void report_stats(const std::vector<std::shared_ptr<hhds::Graph>>& graphs, std::string_view top, const Driver_options& opts);

class Region_driver {
public:
  // `options` are the run-level ones; the backend brings its own and a lane
  // gets a private session through Region_backend::lane().
  Region_driver(const Driver_options& options, std::unique_ptr<Region_backend> backend);
  virtual ~Region_driver();

  // The backend session: started lazily by the first region that needs a
  // mapping. stop() releases this driver's session only (lanes keep theirs).
  void stop();
  void map_region(const livehd::partition::Region_body& rb);
  void map_regions(std::span<const livehd::partition::Region_body> regions);
  void finish_parallel();
  [[nodiscard]] const Parallel_stats& parallel_stats() const { return parallel_stats_; }

  // Trial only colors on a stitched mapped-cell critical path. No physical
  // flattening: the scorer keeps distinct occurrence contexts across modules.
  void optimize_ware(hhds::GraphLibrary& outlib, std::string_view top);

  void set_outlib(hhds::GraphLibrary* l) { outlib_ = l; }

  // Whole-design flatten: the run maps ONE region and the emitted netlist is
  // contracted to hold exactly one module (lhd_abc_flat_test). Suppresses the
  // shared input-bit splitter def, which would be a second module -- with a
  // single region there is nothing to share it with anyway.
  void set_flat(bool f) { flat_ = f; }

  // Incremental mapping (2opt-incr A+C): with a cache attached, map_region
  // digests each region first and clones the previously mapped netlist on a
  // hit instead of running the backend. nullptr = every region maps normally.
  void               set_incr(Region_cache* c) { incr_ = c; }
  // Whether incremental caching is active -- the partitioner uses this to decide
  // whether to build each region's pre-body (the cache's compare artifact).
  [[nodiscard]] bool incremental() const { return incr_ != nullptr; }

  // CLI-level per-region overrides (--set pass.abc.region_opts). Graph-embedded
  // overrides (coloring_info "region_opts") are read per region in map_region.
  void set_region_opts(Region_opts_map m) { region_opts_cli_ = std::move(m); }
  // Preload source attributes before the shared timing model starts.
  void prepare_region_opts(const std::vector<std::shared_ptr<hhds::Graph>>& graphs);

  // Pre-resolved register-mapping cells (liberty::resolve_dff_cells on the
  // run-level library + dff_cell option). A caller resolves them ONCE up front
  // when the incremental-cache salt needs the pick before any region maps.
  // Without this call the first region resolves them itself.
  void set_dff_cells(const liberty::Dff_selection& sel) {
    dff_              = sel.base;
    dff_ladder_       = sel.ladder;
    areset_ladder_[0] = sel.areset_ladder[0];
    areset_ladder_[1] = sel.areset_ladder[1];
    dff_preset_       = true;
  }

  // QoR rows accumulated by map_region, one per successfully mapped region.
  [[nodiscard]] const std::vector<Region_qor>& qor() const { return qor_; }

  // The backend's partition-boundary refinement, run AFTER the whole
  // decomposition has been built into `outlib` (every region mapped or
  // restored from the cache). Returns the number of cells it changed.
  uint64_t           refine_boundaries(hhds::GraphLibrary& outlib, std::string_view top);
  // False only when every region was restored from the incremental cache.
  [[nodiscard]] bool backend_started() const;

  // Set when a region was refused by memory admission. map_region cannot throw
  // (a throw out of the region callback would skip stop(), leaking the backend
  // session), so it records the refusal and the caller turns it into the fatal
  // AFTER stop() has run.
  [[nodiscard]] const std::string* admission_refusal() const { return refusal_.empty() ? nullptr : &refusal_; }
  [[nodiscard]] const std::string* time_refusal() const { return time_refusal_.empty() ? nullptr : &time_refusal_; }

protected:
  [[nodiscard]] Region_backend& backend() { return *backend_; }

private:
  std::unique_ptr<Region_backend>             backend_;
  bool                                        lanes_started_ = false;  // a lane's session started
  Parallel_stats                              parallel_stats_;
  std::mutex                                  graph_mutex_;
  Region_driver*                              coordinator_ = nullptr;
  std::atomic<unsigned>                       active_backend_{0};
  // Reuse private sessions across bounded partition batches.
  std::vector<std::unique_ptr<Region_driver>> parallel_drivers_;
  // The per-region overrides of the backend's own command strings.
  struct Backend_overrides {
    std::optional<std::string> flow, load;
  };
  struct Ware_region {
    livehd::partition::Region_body rb;
    std::vector<hhds::Node_class>  nodes;
    std::shared_ptr<hhds::Graph>   source;
    Driver_options                 options;
    Backend_overrides              overrides;
    bool                           add = false, mult = false, barrel = false;
  };
  Ware_score               score_ware(hhds::GraphLibrary& outlib, std::string_view top);
  Design_ctx               design_ctx();
  void                     remember_ware(const livehd::partition::Region_body& rb, const Driver_options& options,
                                         const Backend_overrides& overrides);
  std::vector<Ware_region> ware_regions_;
  hhds::GraphLibrary       ware_shells_, ware_sources_, ware_pre_;
  bool                     ware_trial_ = false;
  Backend_overrides        trial_overrides_;  // a ware trial's recorded region overrides
  std::string              refusal_;
  std::string              time_refusal_;

  // True (and fills refusal_) when the process has grown past the memory budget
  // while translating `region`. `blasted`/`total` describe how far the
  // translation got, so the diagnostic can project the finished size.
  // pending_bytes: memory the region will allocate but has not yet (the
  // backend network still to be built from the Lnet), counted as if resident.
  bool over_budget(std::string_view region, uint64_t rss_before, size_t blasted, size_t total, uint64_t pending_bytes = 0);

  // Startup uses the run-level options, not a region's temporary overrides
  // (notably register_max_bits can turn register mapping off for one region).
  Driver_options                   startup_opts_;
  Driver_options                   opts_;
  bool                             flat_ = false;
  // Plain posedge D-flop found in the Liberty (register mapping target). Empty
  // when map_register is off or the library has no DFF cell — the read-back then
  // keeps flops native. Resolved once (or handed in by set_dff_cells).
  std::optional<liberty::Dff_cell> dff_;
  // Drive ladder of dff_ (same pins/polarity, area ascending, dff_ first). The
  // read-back picks a rung per register by its Q net's mapped fanout because a
  // fanout buffering tail never buffers a latch output: on ASAP7 a fanout-45
  // register on DFFHQNx1 cost br_amba_axi2axil 542 -> 637 ps; x3 holds 574.
  // Below ~8 loads x1 is the FASTEST rung (73 vs 80 ps clk->Q against DFFHQx4
  // per the NLDM tables) and 97% of registers sit there.
  std::vector<liberty::Dff_cell>   dff_ladder_;
  // The asynchronous clear (index 0) / preset (index 1) register cells, each
  // a drive ladder like dff_ladder_ (liberty::Dff_selection::areset_ladder).
  // Empty: an async-reset register needing that value stays a native flop.
  std::vector<liberty::Dff_cell>   areset_ladder_[2];
  bool                             dff_preset_     = false;
  hhds::GraphLibrary*              outlib_         = nullptr;  // where blackbox cell defs are declared
  Region_cache*                    incr_           = nullptr;  // optional region cache (2opt-incr)
  std::vector<Region_qor>          qor_;
  uint32_t                         next_region_id_ = 1;  // report-only key stamped on mapped region graphs
  Region_opts_map                  region_opts_cli_;
  std::map<std::string, float>     region_delay_targets_;
  // coloring_info "region_opts" parse cache, one entry per source graph.
  std::map<const hhds::Graph*, Region_opts_map> graph_region_opts_;
  // The same coloring_info carries the three ware-family switches. A large
  // module can produce hundreds of regions, so reparsing its JSON in every
  // apply_region_overrides call is needlessly quadratic in metadata size.
  std::map<const hhds::Graph*, Ware_policy>     graph_ware_policy_;
  // rewrite_trivial_rems scans and rewrites a whole source def. A def is shared
  // by all of its colored Region_body callbacks, so doing it once per region is
  // an accidental O(regions * def_nodes) cost (528 full RenameBuffer walks).
  absl::flat_hash_set<hhds::Graph*>             rems_rewritten_graphs_;
  // The read-back of every region this driver maps (it keeps the shared
  // input-splitter defs).
  Region_writer                                 writer_;

  // A delay target anywhere in this run: the run level, the current region, or
  // any region_opts. Constant for a backend session (its timing model is set
  // up once when it starts).
  [[nodiscard]] bool   timing_requested() const;
  // The register margin `reg_margin` resolves to, in ps (0 = none).
  [[nodiscard]] double reg_margin_ps() const;
  // A region's delay budget: `target` minus the register margin when the
  // region holds flops, floored at 1 ps (a budget of 0 would read as "no
  // target" to a sizing step). <= 0 target => no budget (returns target).
  [[nodiscard]] float  region_budget(float target, bool has_flops) const;
  // Resolve dff_/dff_ladder_ from the run-level library unless preset. The
  // register margin needs the cell before the first region's recipe is
  // formed, ahead of the lazy backend start.
  void                 ensure_dff_cells();
  // The register cell a fresh backend session maps to (verbose) or the
  // warning that there is none.
  void                 report_register_cell() const;

  // Overlay any per-region overrides for rb.color onto opts_ (caller saves and
  // restores opts_ around the region); the backend's own strings land in
  // `overrides`. True when a non-empty flow override applies.
  [[nodiscard]] bool apply_region_overrides(const livehd::partition::Region_body& rb, Backend_overrides& overrides);

  // One compact, flushed record after each color has completely finished.
  // Kept separate from verbose stage tracing so long-running synthesis always
  // has a stable heartbeat that wrappers can forward without scraping the
  // backend's implementation chatter.
  void report_completion(const Region_qor& q);

  uint64_t completed_regions_ = 0;
#if defined(__APPLE__)
  // Maximal malloc-zone relief walks every Darwin allocator zone. Repeating it
  // for each of thousands of tiny colors fragments virtual address space even
  // when physical footprint is safe, so map_region rate-limits pressure scans.
  uint64_t last_pressure_relief_region_ = 0;
  bool     pressure_relief_done_        = false;
#endif
};

}  // namespace livehd::synth

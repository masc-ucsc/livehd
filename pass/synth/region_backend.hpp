// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

// The seam between the region driver (region_driver.hpp: partition callbacks,
// caching, translation, read-back, lanes, ware trials) and a mapping backend
// (ABC today, pass/abc/abc_backend.hpp): a backend turns one translated region
// into a Cell_netlist and owns whatever whole-design sizing it offers.
// abc_cleanup.md section 5.
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "arith.hpp"
#include "cell_netlist.hpp"
#include "liberty_dff.hpp"
#include "memory_module.hpp"
#include "pass_partition.hpp"
#include "region_blast.hpp"
#include "region_qor.hpp"

namespace livehd::synth {

class Region_cache;  // region_cache.hpp
struct Region_ctx;   // below

// What a region hook (unate synthesis, pass/usyn) hands the backend instead of
// the region's own logic (abc_cleanup.md section 5).
struct Region_rewrite {
  enum class Map : uint8_t {
    region,  // no rewrite: the backend maps the region's own logic with its flow
    flow,    // `logic` replaces the region's logic as the backend flow's input
    tmap,    // `logic` is technology-mapped only: no restructuring
    refused, // the hook recorded a time/memory refusal (ctx.refuse_*): map nothing
  };
  Map         map = Map::region;
  // Over the region Lnet's boundary: its inputs and latches in CI order, its
  // outputs in order. A LUT may carry its SOP (Lnet::sop) for the backend.
  Lnet        logic;
  // The hook's decision record, stored with the region's cache row.
  std::string evidence;
};
// Called once per mapped region with its RAW Lnet, before the backend maps it.
using Region_hook = std::function<Region_rewrite(const Lnet& net, const Region_ctx& ctx)>;

// The options the region pipeline itself reads. A backend adds its own.
struct Driver_options {
  // Optional region hook, and its identity in the region-cache recipe (two
  // regions with equal logic but different hook settings never share a body).
  Region_hook region_hook;
  std::string region_hook_recipe;
  // A hook's decision evidence travels with its region-cache row:
  // evidence_valid(cached_region, evidence) is checked before a cached body is
  // copied, and evidence_replay(current_region, cached_region, evidence) runs
  // after the copy succeeded.
  std::function<bool(std::string_view, std::string_view)>                   evidence_valid;
  std::function<void(std::string_view, std::string_view, std::string_view)> evidence_replay;

  // Re-map each arithmetic/shifter ware region under the alternative adder,
  // multiplier and barrel lowerings and keep the best stitched result.
  bool        ware_trials = true;
  std::string library;  // Liberty .lib
  // Sequential technology-mapping knobs (independent because their cost differs:
  // a register is one DFF cell per bit, a memory bit-blasts into a whole DFF
  // array + address decode). map_register=true maps flops to Liberty DFF cells
  // (falls back to native flops when the library has none); false keeps them
  // native (cgen emits `always @(posedge clk)`). memory=false preserves a native
  // memory instance; memory=true lowers the emitted memory RTL inside a separate
  // module and maps that body; memory=auto (the default) folds only the
  // memories no macro could be -- see Memory_fold in memory_module.hpp.
  bool        map_register      = true;
  Memory_fold memory_fold       = Memory_fold::Auto;
  // The `auto` storage threshold in bits (0 = no limit); pass.abc.memory_max_bits.
  uint64_t    memory_max_bits   = 1024;
  // Keep an oversized register payload native even when map_register is true.
  // A backend may represent every bit as a separate latch and some generated
  // blocks put thousands of state bits in one color; 0 (the default) disables
  // the per-region guard: a bit-blasted 64x64 memory alone is 4096 bits.
  uint64_t    register_max_bits = 0;
  // Optional explicit DFF cell name for register mapping (empty => auto-detect a
  // plain posedge D-flop from the Liberty).
  std::string dff_cell;
  std::string delay;  // the delay target in ps (empty: untimed)
  // Register overhead subtracted from `delay` to form a region's budget when
  // the region holds flops: "auto" = the mapped DFF cell's clk->Q + setup read
  // off its Liberty timing tables (liberty::Dff_cell), "<ps>" = that many
  // picoseconds, "0" = no margin. A backend's region timer sees only the
  // combinational cone; the period OpenSTA checks includes the launch flop's
  // clk->Q and the capture flop's setup (69 ps of a 400 ps ASAP7 period on
  // br_arb_rr), so a region sized to the full period misses it by exactly that.
  std::string reg_margin = "auto";
  bool        verbose    = false;
  // Combinational adder architecture for Sum/comparators (arith.hpp) and the
  // CSKA/CLA block width (0 => auto from the operating width).
  arith::Adder_kind adder           = arith::Adder_kind::rca;
  bool              ware            = true;  // source-section permission, not a global CLI option
  bool              ware_arith      = true;
  bool              ware_cmp        = true;
  bool              ware_shift      = true;
  bool              auto_adder      = true;
  bool              auto_multiplier = true;
  bool              auto_barrel     = true;
  bool              reverse_barrel  = false;
  int               block_size      = 0;
  // Combinational multiplier architecture for Mult (partial-product adds use the
  // `adder`/`block_size` above). Array adds rows serially; tree sums balanced pairs.
  arith::Mult_kind  multiplier      = arith::Mult_kind::array;
  // Size tier: a region at or above this synthesis-GE estimate is never ware-
  // trialed (and a backend may map it with a cheaper tier). 0 disables.
  uint64_t          large_ge        = 0;
  // Memory admission (2opt-incr subtask 0). A region is bit-blasted, which for
  // a whole-design region means millions of gates and several network forms
  // held at once; an XSCore flat run reached 221 GB on a 64 GiB host and was
  // SIGKILLed by the OS. `memory_budget_mb` pins the ceiling for reproducible
  // hosts/CI (0 => physical RAM minus an OS reserve); `allow_oversize`
  // acknowledges the risk and disables the guard.
  int               memory_budget_mb = 16384;
  unsigned          threads          = 1;  // 0: available CPUs; synth defaults to automatic
  uint64_t          time_budget_ms   = 0;  // per mapped color; 0 disables the soft gate
  bool              allow_oversize   = false;
};

// What the driver resolved for one region, and the services a backend calls
// back into while it maps it.
struct Region_ctx {
  Region_ctx(const livehd::partition::Region_body& region, const Driver_options& region_options)
      : rb(region), options(region_options) {}
  const livehd::partition::Region_body& rb;
  const Driver_options&                 options;  // the region's effective options (region_opts applied)
  // region_opts overrides of the backend's own command strings (flow, load).
  std::optional<std::string>            flow;
  std::optional<std::string>            load;
  // A ware trial re-maps a remembered region under candidate options: its
  // overrides are the ones recorded when the region was first mapped.
  bool                                  ware_trial = false;
  // A delay target anywhere in this run: the run level, this region, or any
  // region_opts (CLI or graph-embedded).
  bool                                  timing_requested = false;
  uint64_t                              input_ge         = 0;
  float                                 budget           = -1.0f;  // ps, floored; <= 0: no budget
  double                                margin_ps        = 0.0;    // the register margin folded into the budget
  uint64_t                              rss_entry        = 0;      // process footprint when the region started

  std::function<void(std::string_view)> trace_stage;
  std::function<double()>               elapsed_ms;
  // Exact memory admission after `done` of `total` translation steps; false
  // recorded the refusal. Null under allow_oversize.
  std::function<bool(uint64_t rss_before, size_t done, size_t total)> fits;
  // Time and memory admission around a long backend step; false recorded the
  // refusal.
  std::function<bool(std::string_view stage)>                         admission;
  std::function<void(std::string)>                                    refuse_time, refuse_memory;
  // A backend may release the shared graph lock while it touches only its own
  // objects; it must resume before anything that reads the region's graphs.
  std::function<void()>                                               pause_graph, resume_graph;
  // Names q.crit_output/crit_src after region output `po`.
  std::function<void(size_t po, Region_qor& q)>                       critical_output;
};

// A backend's per-region decisions, made before translation.
struct Region_plan {
  std::string recipe;  // the backend's cache-recipe fragment (verbatim, compared exactly)
  // The backend's flow keeps every latch as crossed (no retiming, no sequential
  // sweep): a QN-only DFF encoding on the latch input is then exact.
  bool        preserves_latches = false;
};

// Whole-design state a backend's refinement and scoring read and update.
struct Design_ctx {
  std::vector<Region_qor>&                rows;   // one per mapped region; refinement updates delay/area
  Region_cache*                           cache;  // refreshed after refinement; null without one
  const std::map<std::string, float>&     region_delay_targets;
  const std::optional<liberty::Dff_cell>& dff;
  const std::vector<liberty::Dff_cell>&   dff_ladder;
  // The asynchronous clear / preset register cells (Region_driver::areset_ladder_).
  const std::vector<liberty::Dff_cell>&   areset0_ladder;
  const std::vector<liberty::Dff_cell>&   areset1_ladder;
  // A region's delay budget: `target` minus the register margin when the
  // region holds flops (floored at 1 ps; <= 0 target: no budget).
  std::function<float(float target, bool has_flops)> region_budget;
  // A delay target anywhere in the run (see Region_ctx::timing_requested).
  bool                                               timing_requested = false;
};

class Region_backend {
public:
  virtual ~Region_backend() = default;

  // A private session with the same run-level options, for a parallel lane.
  [[nodiscard]] virtual std::unique_ptr<Region_backend> lane() const = 0;

  enum class Start { failed, resumed, started };
  // Lazily start (or re-enter) the session: the first region that needs a
  // mapping pays for it, an all-hit incremental run never does. `failed` was
  // diagnosed. `ctx` is that first region's.
  virtual Start                 start(const Region_ctx& ctx) = 0;
  virtual void                  stop()                       = 0;
  // A session was started at some point (false: every region was a cache hit).
  [[nodiscard]] virtual bool    started() const = 0;
  // Whatever the backend enters for a region is left when the guard dies.
  [[nodiscard]] virtual std::shared_ptr<void> region_scope() = 0;

  // Per region, before translation (and before the cache lookup).
  [[nodiscard]] virtual Region_plan                 plan(const Region_ctx& ctx) = 0;
  // Map the translated region (or the hook's rewrite of it), filling the
  // backend's part of `q`. nullopt: refused (recorded through ctx) or failed
  // (diagnosed).
  [[nodiscard]] virtual std::optional<Cell_netlist> map(const Region_ctx& ctx, const Region_blast& blast,
                                                        const Region_rewrite& rewrite, Region_qor& q)
      = 0;
  [[nodiscard]] virtual const Cell_library&         cells() const = 0;
  // Release the region's workspace once it is written.
  virtual void                                      end_region() = 0;

  // Scheduling estimate for a parallel lane mapping a region of `aig_nodes`.
  [[nodiscard]] virtual uint64_t projected_memory(uint64_t aig_nodes) const = 0;

  // Whole design, once every region exists: refine every region against the
  // exact environment beyond its boundary (returns the cells changed), and
  // score the stitched design for the ware trials.
  virtual uint64_t   refine(hhds::GraphLibrary& outlib, std::string_view top, const Design_ctx& design) = 0;
  virtual Ware_score score(hhds::GraphLibrary& outlib, std::string_view top, const Design_ctx& design)  = 0;
};

}  // namespace livehd::synth

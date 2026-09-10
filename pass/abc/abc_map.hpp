// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "abc_arith.hpp"     // arith::Adder_kind
#include "abc_boundary.hpp"  // Boundary_table
#include "abc_parallel.hpp"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "hhds/graph.hpp"
#include "liberty_dff.hpp"     // livehd::liberty::Dff_cell
#include "memory_module.hpp"   // livehd::abc::Memory_fold
#include "pass_partition.hpp"  // livehd::partition::Region_body
#include "satopt.hpp"

namespace livehd::abc {

class Incr_cache;  // abc_incr.hpp -- the 2opt-incr per-region signature cache

// How much of a met delay budget to hand back to ABC's mapper as area, in
// percent, for `&nf -R`. `achieved` and `target` are in the same (Liberty) unit;
// `cap` is Map_options::area_relax_pct. Returns 0 when there is nothing to trade
// -- the budget was missed, the slack is too small to pay for a second mapping
// pass, or recovery is switched off -- which is exactly the "leave the
// minimum-delay mapping alone" answer.
//
// Free and pure so the policy is testable without a Liberty: the QoR it produces
// depends on the cell library, but the DECISION does not.
int area_relax_percent(float target, float achieved, uint32_t cap);
// Area without a target; Liberty delay (then area) with a target. Endpoint
// delays are sorted worst first, so improving a tied critical path counts.
struct Ware_qor {
  double             area = 0.0;
  std::vector<float> delays;
};
bool  ware_qor_better(const Ware_qor& baseline, const Ware_qor& candidate, bool timing);
float ware_delay_target(std::string_view value);

struct Map_options {
  bool              satopt = true;
  std::string       library;  // Liberty .lib for read_lib
  std::string       flow;     // ABC command string (empty => built-in default)
  // Cap on the fanout of any net ABC MAPS, enforced by appending
  // `buffer -N <n>; dnsize` to a built-in flow. 0 disables the tail.
  // Nets driven by native (unblasted) nodes are outside ABC and keep their
  // fanout regardless. Default 16.
  uint32_t          max_fanout          = 16;
  // Optional size-tiered flow. Regions in [`small_min_ge`, `small_ge`] use it;
  // explicit color-keyed region_opts still win. This lets large replicated
  // logic use a deliberately cheap mapper without sacrificing the QoR of
  // small timing-sensitive cones. Disabled when empty or small_ge == 0.
  // Control groups inherit the ordinary recipe by default. A custom ctrl_flow
  // opts into the delay-only tier and its whole-region time backstop.
  std::string       ctrl_flow           = "inherit";
  uint32_t          ctrl_area_relax     = 0;
  uint64_t          ctrl_time_budget_ms = 5000;
  std::string       small_flow;
  uint64_t          small_min_ge = 0;
  uint64_t          small_ge     = 0;
  // Indivisible wide operations can exceed color.max_gate by orders of
  // magnitude. The default large tier skips ABC's unbounded structural-choice
  // synthesis and maps the already bit-blasted AIG directly. Empty or
  // large_ge==0 disables the tier; an explicit global/per-region flow wins.
  std::string       large_flow;
  uint64_t          large_ge          = 0;
  // Sequential technology-mapping knobs (independent because their cost differs:
  // a register is one DFF cell per bit, a memory bit-blasts into a whole DFF
  // array + address decode). register=true maps flops to Liberty DFF cells (falls
  // back to native flops when the library has none); false keeps them native
  // (cgen emits `always @(posedge clk)`). memory=false preserves a native memory
  // instance; memory=true lowers the emitted memory RTL inside a separate module
  // and maps that body; memory=auto (the default) folds only the memories no
  // macro could be -- see Memory_fold in memory_module.hpp.
  bool              map_register      = true;
  Memory_fold       memory_fold       = Memory_fold::Auto;
  // The `auto` storage threshold in bits (0 = no limit); pass.abc.memory_max_bits.
  uint64_t          memory_max_bits   = 1024;
  // Keep an oversized register payload native even when map_register is true.
  // ABC represents every bit as a separate latch and some generated blocks put
  // thousands of state bits in one color; 0 (the default) disables the
  // per-region guard: a bit-blasted 64x64 memory alone is 4096 bits, and a
  // native register is one yosys's normalize maps instead of pass.abc.
  uint64_t          register_max_bits = 0;
  // Optional explicit DFF cell name for register mapping (empty => auto-detect a
  // plain posedge D-flop from the Liberty).
  std::string       dff_cell;
  std::string       delay;  // {D} substitution
  std::string       load;   // {L} substitution
  // A delay target is a BUDGET, not "go as fast as you can". ABC's `&nf -D` is
  // silently IGNORED by the mapper (giaNf reads only `MapDelayTarget`, which
  // `-D` never sets), so a built-in flow always mapped for MINIMUM delay and
  // spent area no timing constraint asked for. When the mapped region beats its
  // budget, pass.abc re-runs the mapper with `&nf -R <pct>` -- ABC's own delay
  // RELAXATION ratio -- to convert the measured slack into area. This caps that
  // percentage; 0 disables the recovery entirely and restores minimum-delay
  // mapping. Needs a physical (NLDM) Liberty and a `delay` target: with neither
  // there is no budget to be inside of.
  uint32_t          area_relax_pct = 200;
  // The AREA objective (also a second candidate after meeting a delay budget):
  // empty = the former baseline with &fraig/dc2/&dch/&nf, "none"
  // disables only the second candidate, anything else is an ABC command string run
  // verbatim ({D}/{L}/{F}/{B} substituted) in place of the built-in one.
  std::string       area_flow;
  // Register overhead subtracted from `delay` to form a region's budget when
  // the region holds flops: "auto" = the mapped DFF cell's clk->Q + setup read
  // off its Liberty timing tables (liberty::Dff_cell), "<ps>" = that many
  // picoseconds, "0" = no margin. ABC's SCL timer sees only the combinational
  // cone; the period OpenSTA checks includes the launch flop's clk->Q and the
  // capture flop's setup (69 ps of a 400 ps ASAP7 period on br_arb_rr), so a
  // region sized to the full period misses it by exactly that.
  std::string       reg_margin       = "auto";
  bool              verbose          = false;
  // Combinational adder architecture for Sum/comparators (2i-abc_arith) and the
  // CSKA/CLA block width (0 => auto from the operating width).
  arith::Adder_kind adder            = arith::Adder_kind::rca;
  bool              ware             = true;  // source-section permission, not a global CLI option
  bool              ware_arith       = true;
  bool              ware_cmp         = true;
  bool              ware_shift       = true;
  bool              auto_adder       = true;
  bool              auto_multiplier  = true;
  bool              auto_barrel      = true;
  bool              reverse_barrel   = false;
  int               block_size       = 0;
  // Combinational multiplier architecture for Mult (partial-product adds use the
  // `adder`/`block_size` above). Array adds rows serially; tree sums balanced pairs.
  arith::Mult_kind  multiplier       = arith::Mult_kind::array;
  // Memory admission (2opt-incr subtask 0). A region is bit-blasted into ABC,
  // which for a whole-design region means millions of gates and several network
  // forms held at once; an XSCore flat run reached 221 GB on a 64 GiB host and
  // was SIGKILLed by the OS. `memory_budget_mb` pins the ceiling for
  // reproducible hosts/CI (0 => physical RAM minus an OS reserve);
  // `allow_oversize` acknowledges the risk and disables the guard.
  int               memory_budget_mb = 16384;
  unsigned          threads          = 1;  // 0: available CPUs; synth defaults to automatic
  uint64_t          time_budget_ms   = 0;  // per mapped color; 0 disables the soft gate
  bool              allow_oversize   = false;
  // Partition-boundary environment (abc_boundary.cpp). A region's ports are
  // ABC PIs/POs, and ABC's SCL timer used to see nothing beyond them: a PO
  // drove no load, a PI came from an ideal driver, and `buffer -N` never treed
  // a PI's fanout -- so the driver of a net crossing into five regions was
  // sized for fanout ONE and every sink region assumed an infinitely strong
  // source. With `boundary=true` (default) each region is sized against what
  // lies beyond the partition: first a static estimate from the source graph
  // (consumer pins per output bit, driver class per input), then -- once every
  // region is mapped or restored -- the exact environment read off the
  // stitched netlist (the real driver cell and the real sink pins' Liberty
  // caps, joined through the wrapper), against which every region is re-sized
  // in place (`upsize -D`/`dnsize -D` to its budget). Needs a Liberty with
  // 2-D NLDM tables (the same gate as the buffering tail); the exact re-size
  // additionally needs a `delay` target (there is no budget to size to
  // without one). A crossing net is buffered on BOTH sides: the sink side by
  // each region (boundary_buffer), the driver side by its real load.
  bool              boundary         = true;
  // Tree every region input's fanout inside the region when it exceeds
  // `max_fanout` -- the design's primary inputs and the sink side of crossing
  // nets alike, the same fanout rule internal nets follow -- by declaring
  // `boundary_drive` as ABC's driving cell: its `buffer` only trees an input
  // that has a driver (`buffer -p` is inert in the current ABC). Independent
  // of `boundary`. False leaves inputs unbuffered and lets the exact re-size
  // upsize the driver instead.
  bool              boundary_buffer  = true;
  // Stand-in Liberty cell driving a region input whose real driver is not a
  // mapped cell (a primary input, a native flop, a memory or child-instance
  // output, and every input under the static estimate): empty = the
  // library's smallest buffer, `none` = the ideal (zero-slew) driver.
  std::string       boundary_drive;
  // Load in fF on a PRIMARY output of the design (a port of --top) and on a
  // port whose sink cannot be resolved; <0 = one typical input pin of the
  // library (the average input capacitance over its smallest cells).
  float             io_load         = -1.0f;
  // Rounds of the exact re-size. Each round sizes every region under the
  // loads/drivers as they stand plus the arrival and required-time budgets
  // the previous round propagated across the hierarchy; a path through k
  // regions needs k rounds to be seen whole.
  int               boundary_rounds = 1;
};

// Per-region (color-keyed) overrides of the mapping options that vary per
// region (2opt-freq C). Unset fields inherit the global Map_options. Two
// sources, later wins: a "region_opts" member inside the source graph's
// coloring_info JSON (the block-attribute channel, 2opt-freq B), then the
// --set pass.abc.region_opts CLI JSON.
struct Region_opts {
  std::optional<bool>              ware;
  std::optional<std::string>       flow;
  std::optional<std::string>       delay;
  std::optional<std::string>       load;
  std::optional<arith::Adder_kind> adder;
  std::optional<int>               block_size;
  std::optional<arith::Mult_kind>  multiplier;
  std::optional<bool>              reverse_barrel;
};
using Region_opts_map = std::map<int, Region_opts>;

// Parse {"<color>":{"flow":…,"delay":…,"load":…,"adder":…,"block_size":…,
// "multiplier":…},…}. Unknown keys and malformed values are hard errors (a
// mistyped hint must never silently no-op). Returns nullopt after a diag.
std::optional<Region_opts_map> parse_region_opts(std::string_view json, std::string_view where);

// Per-region quality-of-results read back from ABC after mapping (2opt-freq A).
// `delay` is ABC's mapped-delay estimate from the Liberty pin-to-pin data
// (Abc_NtkDelayTrace) — the phase-1 frequency oracle. With a delay target and
// an NLDM Liberty the mapper installs a physical gain-100 GENLIB, and then
// `delay`/`area` come from ABC's SCL timer (`stime`) instead, in PICOSECONDS
// (ABC normalizes every SCL library to ps/ff on read) rather than the
// unit-delay logic depth an unset delay target reports. Per-region only: paths
// crossing region/blackbox boundaries are invisible here (pass.opentimer is
// the whole-design scorer).
struct Region_qor {
  uint64_t    satopt_facts = 0;
  std::string module;  // region module name (<top>__c<color>)
  int         color       = 0;
  bool        ctrl        = false;
  int         ware_trials = 0;
  std::string ware_selected;
  uint64_t    input_nodes = 0;  // source-region nodes before bit blasting
  uint64_t    input_ge    = 0;  // graph_util synthesis-GE estimate before ABC
  // Predicted generic-AIG size of the same cone (graph/predict_abc_size.hpp),
  // the unit `pass.color synth --set synth_alg=cones` thresholds on. Reported
  // NEXT TO input_ge, never instead of it: the two are different estimates of
  // the same input, and the only ground truth for either is `gates` below --
  // there is no per-op post-ABC attribution, so a region sum is the whole
  // measurement. Every production run therefore validates both predictors.
  uint64_t    pred_aig    = 0;
  int         gates       = 0;    // mapped standard cells actually minted (bypassed buffers excluded)
  double      area        = 0.0;  // sum of their Liberty cell areas
  // Identity buffers ABC minted to decouple a CI->CO / gate->many-CO edge that
  // the read-back aliased away (pass 1b): not in `gates`/`area`, not in the
  // netlist. Diagnostic only -- a cache hit reports 0 (the row is not
  // persisted with it; its gates/area are already net of the bypass).
  int         bypassed    = 0;
  int         logic_depth = -1;     // mapped ABC gate levels between region/state boundaries, before read-back buffer bypass
  float       delay       = -1.0f;  // critical arrival (unit-delay depth, or ps with an NLDM GENLIB); <0 => unavailable
  std::string crit_output;          // region output port with the worst arrival
  std::string crit_src;             // "file:line" of that output's original driver (may be empty)
  // The mapping-objective decision (map_region's budget ladder), diagnostic
  // only -- a cache hit reports none of it. `budget` is the region's delay
  // budget in ps (target minus the register margin when it holds flops; <0 =
  // no target). `candidate` names which mapping the region kept: "delay" (the
  // `&nf`-based delay flow, sized to the budget) or "area" (the `amap`-based
  // area flow, kept only when it ALSO met the budget with less SCL area);
  // empty when no comparison ran (no target, custom flow, dummy-PO region,
  // area_flow=none, over large_ge). The two pairs are the SCL timer's numbers
  // for each candidate at decision time (<0 = not run).
  float       budget = -1.0f;
  std::string candidate;
  float       delay_flow_delay   = -1.0f;
  double      delay_flow_area    = -1.0;
  float       area_flow_delay    = -1.0f;
  double      area_flow_area     = -1.0;
  // Blackboxed div/mod nodes in this region: their cones are NOT mapped, so
  // gates/area/delay under-report — the score is partial until the div is
  // strength-reduced away. Surfaced so an agent never trusts a blind score.
  int         div_blackbox       = 0;
  // Where this region's wall time went, and whether the incremental cache was
  // able to take it. Without these two, "the cache hit 199 of 264 regions" says
  // nothing about whether the run got faster — the misses can hold all the time.
  double      ms                 = 0.0;   // wall ms this region spent in map_region
  uint64_t    peak_rss_kb        = 0;     // whole-process high-water after this color; 0 = unavailable/cache hit
  uint64_t    color_peak_rss_kb  = 0;     // per-color peak growth over its entry RSS baseline
  const char* cache              = "";    // "" (no cache) | hit | mapped | uncacheable | store-failed
  bool        resynth            = true;  // this invocation rebuilt the region (false = incremental cache hit)
  // Partition-boundary refinement (abc_boundary.cpp): how many of this region's
  // port bits cross to another region or a primary IO, how many cells the
  // exact re-size changed, and the SCL timer's view of the region BEFORE the
  // re-size under the exact environment (<0 = the refinement did not run on
  // it). `delay`/`area` above are updated to the re-sized netlist.
  int         boundary_bits      = 0;
  int         boundary_resized   = 0;
  float       boundary_delay_pre = -1.0f;
  double      boundary_area_pre  = -1.0;
};

// Stats-only mode (no --emit-dir): summarize what would be mapped.
void report_stats(const std::vector<std::shared_ptr<hhds::Graph>>& graphs, std::string_view top, const Map_options& opts);

// Drives the ABC frame across a whole decomposition. The frame and Liberty are
// initialized on the first cache miss, then reused for the remaining regions.
// An all-hit incremental run never starts ABC or parses Liberty. The current
// network is reset per mapped region. Each region body is rebuilt as a
// standard-cell netlist of 1-bit blackbox Sub cells.
class Mapper {
public:
  explicit Mapper(const Map_options& opts) : startup_opts_(opts), opts_(opts) {}
  ~Mapper() { stop(); }

  // Idempotent lazy initialization: a private ABC frame + read_lib on the first miss.
  // The destructor is a backstop for diagnostics that unwind a region callback.
  bool start();
  void stop();  // destroy this mapper's private frame
  void map_region(const livehd::partition::Region_body& rb);
  void map_regions(std::span<const livehd::partition::Region_body> regions);
  void finish_parallel() {
    for (auto& worker : parallel_mappers_) {
      worker->stop();
    }
  }
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
  // hit instead of running ABC. nullptr = every region maps normally.
  void               set_incr(Incr_cache* c) { incr_ = c; }
  // Whether incremental caching is active -- the partitioner uses this to decide
  // whether to build each region's pre-body (the cache's compare artifact).
  [[nodiscard]] bool incremental() const { return incr_ != nullptr; }

  // CLI-level per-region overrides (--set pass.abc.region_opts). Graph-embedded
  // overrides (coloring_info "region_opts") are read per region in map_region.
  void set_region_opts(Region_opts_map m) { region_opts_cli_ = std::move(m); }
  // Preload source attributes before the shared Liberty timing model starts.
  void prepare_region_opts(const std::vector<std::shared_ptr<hhds::Graph>>& graphs);

  // Pre-resolved register-mapping cells (liberty::resolve_dff_cells on the
  // run-level library + dff_cell option). pass.abc resolves them ONCE up front
  // because the incremental-cache salt needs the pick before any region maps
  // and abc.json reports it after; start() then skips its own Liberty scan.
  // Without this call start() resolves them itself.
  void set_dff_cells(const liberty::Dff_selection& sel) {
    dff_        = sel.base;
    dff_ladder_ = sel.ladder;
    dff_preset_ = true;
  }

  // QoR rows accumulated by map_region, one per successfully mapped region.
  [[nodiscard]] const std::vector<Region_qor>& qor() const { return qor_; }

  // Partition-boundary refinement (abc_boundary.cpp), run AFTER the whole
  // decomposition has been built into `outlib` (every region mapped or
  // restored from the cache): re-import each region's mapped netlist into
  // ABC, join every port bit through the wrappers to its real driver cell and
  // real sink pins, and re-size each region against that exact environment.
  // Cell swaps are written back in place (same pins, same topology). A no-op
  // without `boundary`, a delay target, or an NLDM Liberty; returns the number
  // of cells whose drive strength changed. Must run before stop().
  uint64_t           refine_boundaries(hhds::GraphLibrary& outlib, std::string_view top);
  // False only when every region was restored from the incremental cache.
  [[nodiscard]] bool abc_started() const { return lib_loaded_; }

  // Set when a region was refused by memory admission. map_region cannot throw
  // (a throw out of the region callback would skip stop(), leaking the ABC
  // frame and every live network), so it records the refusal and work() turns it
  // into the fatal AFTER stop() has run.
  [[nodiscard]] const std::string* admission_refusal() const { return refusal_.empty() ? nullptr : &refusal_; }
  [[nodiscard]] const std::string* time_refusal() const { return time_refusal_.empty() ? nullptr : &time_refusal_; }

private:
  absl::flat_hash_map<hhds::Graph*, std::shared_ptr<const Satopt_result>> satopt_results_;
  Parallel_stats                                                          parallel_stats_;
  std::mutex                                                              graph_mutex_;
  Mapper*                                                                 coordinator_ = nullptr;
  std::atomic<unsigned>                                                   active_abc_{0};
  // Reuse private sessions across bounded partition batches.
  std::vector<std::unique_ptr<Mapper>>                                    parallel_mappers_;
  struct Ware_region {
    livehd::partition::Region_body rb;
    std::vector<hhds::Node_class>  nodes;
    std::shared_ptr<hhds::Graph>   source;
    Map_options                    options;
    bool                           add = false, mult = false, barrel = false;
  };
  struct Ware_score : Ware_qor {
    bool                                    valid = false;
    absl::flat_hash_set<std::string>        critical_regions;
    absl::flat_hash_map<std::string, float> region_path_delay;
  };
  Ware_score               score_ware(hhds::GraphLibrary& outlib, std::string_view top);
  void                     remember_ware(const livehd::partition::Region_body& rb, const Map_options& options);
  std::vector<Ware_region> ware_regions_;
  hhds::GraphLibrary       ware_shells_, ware_sources_, ware_pre_;
  bool                     ware_trial_ = false;
  std::string              refusal_;
  std::string              time_refusal_;

  // True (and fills refusal_) when the process has grown past the memory budget
  // while translating `region`. `blasted`/`total` describe how far the
  // translation got, so the diagnostic can project the finished size.
  bool over_budget(std::string_view region, uint64_t rss_before, size_t blasted, size_t total);

  // Startup uses the run-level options, not a region's temporary overrides
  // (notably register_max_bits can turn register mapping off for one region).
  Map_options                                   startup_opts_;
  // Boundary environment (abc_boundary.cpp), resolved once in start() from the
  // SCL library: the typical input-pin capacitance in fF (io_load's default and
  // the static estimate's per-sink weight), the stand-in driving cell (SC_Cell*,
  // opaque here), and whether the Liberty can size at all (lib_has_nldm_timing,
  // the buffering tail's own predicate -- unlike scl_timing_ok_ it does not
  // need a delay target).
  float                                         typical_cap_ff_ = -1.0f;
  void*                                         drive_cell_     = nullptr;
  bool                                          scl_lib_ok_     = false;
  Map_options                                   opts_;
  bool                                          flat_          = false;
  void*                                         pabc_          = nullptr;  // Abc_Frame_t*
  bool                                          lib_loaded_    = false;
  // A delay target was requested AND the Liberty carries the 2-D NLDM
  // slew/load surfaces the SCL commands walk (lib_has_nldm_timing): the
  // `buffer`/`upsize`/`dnsize` steps and the SCL QoR timer may run. The mapper
  // itself always works on `read_lib -s`'s unit-delay GENLIB (every pin 1.00):
  // the gain-100 physical GENLIB it used to install for a delay target bought
  // nothing the sizing steps do not do better -- measured over 15 lhdtrack
  // designs (geomean vs yosys, area/OpenSTA delay): gain GENLIB 1.63/0.81 on
  // ASAP7 and 1.15/0.85 on sky130 against unit-delay + `dnsize -D` 1.24/0.88
  // and 1.22/0.93 at the same number of periods met; `amap` (the area
  // candidate) ignores GENLIB delays entirely (bit-identical netlists either
  // way).
  bool                                          scl_timing_ok_ = false;
  // One-shot: the Liberty gave ABC no SCL library, so the max_fanout tail
  // cannot run (see the strip in map_region). Warn once per Mapper, not once
  // per region.
  bool                                          warned_no_scl_ = false;
  // Plain posedge D-flop found in the Liberty (register mapping target). Empty
  // when map_register is off or the library has no DFF cell — the read-back then
  // keeps flops native. Detected once in start() (or handed in by set_dff_cells).
  std::optional<liberty::Dff_cell>              dff_;
  // Drive ladder of dff_ (same pins/polarity, area ascending, dff_ first). The
  // read-back picks a rung per register by its Q net's mapped fanout because
  // ABC's `buffer -N` tail never buffers a latch output (a CI): on ASAP7 a
  // fanout-45 register on DFFHQNx1 cost br_amba_axi2axil 542 -> 637 ps; x3
  // holds 574. Below ~8 loads x1 is the FASTEST rung (73 vs 80 ps clk->Q
  // against DFFHQx4 per the NLDM tables) and 97% of registers sit there.
  std::vector<liberty::Dff_cell>                dff_ladder_;
  bool                                          dff_preset_ = false;
  hhds::GraphLibrary*                           outlib_     = nullptr;  // where blackbox cell defs are declared
  Incr_cache*                                   incr_       = nullptr;  // optional region cache (2opt-incr)
  std::vector<Region_qor>                       qor_;
  uint32_t                                      next_region_id_ = 1;  // report-only key stamped on mapped region graphs
  Region_opts_map                               region_opts_cli_;
  std::map<std::string, float>                  region_delay_targets_;
  // coloring_info "region_opts" parse cache, one entry per source graph.
  std::map<const hhds::Graph*, Region_opts_map> graph_region_opts_;
  // rewrite_trivial_rems scans and rewrites a whole source def. A def is shared
  // by all of its colored Region_body callbacks, so doing it once per region is
  // an accidental O(regions * def_nodes) cost (528 full RenameBuffer walks).
  absl::flat_hash_set<hhds::Graph*>             rems_rewritten_graphs_;
  // ABC is bit-level, but a partition boundary is a packed LGraph bus.  A
  // naïve read-back emits one constant SRA per input bit in EVERY region. Rob
  // has hundreds of regions reading the same 10k-bit bus, so that duplicates
  // millions of identical unpacking nodes. Keep one native unpacker definition
  // per width and instantiate it from each mapped region instead.
  //
  // The def is all-or-nothing (every declared output pin must exist on the
  // instance), so a region uses it only when it demands most of the bus --
  // see the three gates in map_region's `input_bit`.
  struct Input_splitter {
    std::shared_ptr<hhds::GraphIO> io;
    std::vector<hhds::Port_id>     bit_port;
  };
  absl::flat_hash_map<int, Input_splitter> input_splitters_;

  // Mio gate descriptors are shared by every instance of a Liberty cell. Rob
  // contains tens of millions of mapped cell instances, so reconstructing the
  // same pin-name vector and repeating the library name lookup for each one is
  // measurable wall time. Key by the stable Mio_Gate pointer owned by ABC's
  // run-level Liberty library (kept opaque here so abc headers stay in .cpp).
  struct Cell_desc {
    std::shared_ptr<hhds::GraphIO> io;
    std::string                    name;
    std::string                    output_name;
    std::vector<std::string>       input_names;
  };
  absl::flat_hash_map<const void*, Cell_desc> cell_descs_;
  Cell_desc&                                  cell_desc_for(void* mio_gate);  // abc_map.cpp

  // Inverting twins for the QN-cell read-back: every Liberty gate indexed by
  // (pin count, truth table) so a mapped D-cone root can be swapped for the
  // cheapest cell computing its COMPLEMENT over the same pins in the same
  // order (AND2x2 -> NAND2xp33, AOI21xp33 -> AO21x1, XOR -> XNOR). Keyed the
  // way ABC dedups gates itself: Mio truth tables are replicated 64-bit words
  // (mioUtils.c), so `~truth` is the complement for any pin count <= 6. Built
  // once per start() from the Mio library; values are Mio_Gate_t* (opaque
  // here so abc headers stay in the .cpp).
  absl::flat_hash_map<std::pair<int, uint64_t>, void*> twin_index_;

  // Whether this run asks for a physical (gain-based NLDM) GENLIB: a run-level
  // `delay`, or a CLI region_opts delay override. Constant for the whole run
  // (the GENLIB is installed once in start()) and folded into resolve_recipe.
  [[nodiscard]] bool nldm_requested() const;

  [[nodiscard]] std::string comb_flow() const;
  [[nodiscard]] std::string seq_flow() const;
  // The area candidate's command list (kAreaFlow + its sizing tail, or the
  // user's `area_flow`), substituted like comb_flow(). "none" => empty.
  [[nodiscard]] std::string area_flow() const;
  [[nodiscard]] std::string resolve_flow(std::string_view builtin) const;
  [[nodiscard]] std::string subst_flow(std::string f) const;

  // The register margin `reg_margin` resolves to, in ps (0 = none).
  [[nodiscard]] double reg_margin_ps() const;
  // A region's delay budget: `target` minus the register margin when the
  // region holds flops, floored at 1 ps (a budget of 0 would read as "no
  // target" to ABC's `-D`). <= 0 target => no budget (returns target).
  [[nodiscard]] float  region_budget(float target, bool has_flops) const;
  // The per-region `{B}` substitution (`-D <budget>` for the sizing steps of
  // the built-in tails, empty without a target): set by map_region before it
  // resolves any flow string for the region, cleared on exit.
  std::string          budget_flag_;
  // Resolve dff_/dff_ladder_ from the run-level library unless preset. Called
  // from start() and, because the register margin needs the cell before the
  // first region's recipe is formed (ahead of the lazy start()), from
  // map_region.
  void                 ensure_dff_cells();

  // abc_boundary.cpp. resolve_boundary_defaults: typical_cap_ff_ /
  // drive_cell_ / scl_lib_ok_ from the frame's SCL library (start()).
  // fill_static_boundary: the Phase A estimate for one region into `table`
  // (PI i -> region input port `pi_port[i]` or -1; PO i -> `po_order[i]`
  // (output port, bit) for i < po_order.size(), a blackbox input after);
  // returns the number of port bits that cross the partition.
  void resolve_boundary_defaults();
  int  fill_static_boundary(Boundary_table& table, const livehd::partition::Region_body& rb,
                            const absl::flat_hash_set<hhds::Node_class>& region, const std::vector<int>& pi_port,
                            const std::vector<std::pair<size_t, int>>& po_order);

  // The resolved per-region ABC recipe, serialized VERBATIM for the incremental
  // cache's recipe gate: the pre-ABC lgraph does not encode it, so two regions
  // with equal logic but different flow/arch must never share a cached netlist.
  // Call after apply_region_overrides so it reflects region_opts.
  [[nodiscard]] std::string resolve_recipe() const;

  // Overlay any per-region overrides for rb.color onto opts_ (caller saves and
  // restores opts_ around the region).
  [[nodiscard]] bool apply_region_overrides(const livehd::partition::Region_body& rb);

  // One compact, flushed record after each color has completely finished.
  // Kept separate from verbose stage tracing so long-running synthesis always
  // has a stable heartbeat that wrappers can forward without scraping ABC's
  // implementation chatter.
  void report_completion(const Region_qor& q);

  uint64_t completed_regions_ = 0;
#if defined(__APPLE__)
  // Maximal malloc-zone relief walks every Darwin allocator zone. Repeating it
  // for each of thousands of tiny colors fragments virtual address space even
  // when physical footprint is safe, so abc_map.cpp rate-limits pressure scans.
  uint64_t last_pressure_relief_region_ = 0;
  bool     pressure_relief_done_        = false;
#endif
};

}  // namespace livehd::abc

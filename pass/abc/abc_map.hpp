// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "abc_arith.hpp"  // arith::Adder_kind
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "hhds/graph.hpp"
#include "liberty_dff.hpp"     // livehd::liberty::Dff_cell
#include "pass_partition.hpp"  // livehd::partition::Region_body

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

struct Map_options {
  std::string       library;  // Liberty .lib for read_lib
  std::string       flow;     // ABC command string (empty => built-in default)
  // Cap on the fanout of any net ABC MAPS, enforced by appending
  // `buffer -N <n>; dnsize` to a built-in flow. 0 disables the tail.
  // Nets driven by native (unblasted) nodes are outside ABC and keep their
  // fanout regardless. Default 16.
  uint32_t          max_fanout = 16;
  // Optional size-tiered flow. Regions in [`small_min_ge`, `small_ge`] use it;
  // explicit color-keyed region_opts still win. This lets large replicated
  // logic use a deliberately cheap mapper without sacrificing the QoR of
  // small timing-sensitive cones. Disabled when empty or small_ge == 0.
  std::string       small_flow;
  uint64_t          small_min_ge = 0;
  uint64_t          small_ge     = 0;
  // Indivisible wide operations can exceed color.max_ge by orders of
  // magnitude. The default large tier skips ABC's unbounded structural-choice
  // synthesis and maps the already bit-blasted AIG directly. Empty or
  // large_ge==0 disables the tier; an explicit global/per-region flow wins.
  std::string       large_flow;
  uint64_t          large_ge          = 0;
  // Sequential technology-mapping knobs (independent because their cost differs:
  // a register is one DFF cell per bit, a memory bit-blasts into a whole DFF
  // array + address decode). register=true maps flops to Liberty DFF cells (falls
  // back to native flops when the library has none); false keeps them native
  // (cgen emits `always @(posedge clk)`). memory=true bit-blasts a Memory into a
  // register array + read/write mux logic; false keeps it as a native instance.
  bool              map_register      = true;
  bool              map_memory        = false;
  // Keep an oversized register payload native even when map_register is true.
  // ABC represents every bit as a separate latch and some generated blocks put
  // thousands of state bits in one color; 0 disables the per-region guard.
  uint64_t          register_max_bits = 4096;
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
  uint32_t          area_relax_pct   = 200;
  bool              verbose          = false;
  // Combinational adder architecture for Sum/comparators (2i-abc_arith) and the
  // CSKA/CLA block width (0 => auto from the operating width).
  arith::Adder_kind adder            = arith::Adder_kind::rca;
  int               block_size       = 0;
  // Combinational multiplier architecture for Mult (partial-product adds use the
  // `adder`/`block_size` above). Only `array` today; the enum is the extension
  // point for Booth/Wallace-tree variants.
  arith::Mult_kind  multiplier       = arith::Mult_kind::array;
  // Memory admission (2opt-incr subtask 0). A region is bit-blasted into ABC,
  // which for a whole-design region means millions of gates and several network
  // forms held at once; an XSCore flat run reached 221 GB on a 64 GiB host and
  // was SIGKILLed by the OS. `memory_budget_mb` pins the ceiling for
  // reproducible hosts/CI (0 => physical RAM minus an OS reserve);
  // `allow_oversize` acknowledges the risk and disables the guard.
  int               memory_budget_mb = 0;
  uint64_t          time_budget_ms   = 0;  // per mapped color; 0 disables the soft gate
  bool              allow_oversize   = false;
};

// Per-region (color-keyed) overrides of the mapping options that vary per
// region (2opt-freq C). Unset fields inherit the global Map_options. Two
// sources, later wins: a "region_opts" member inside the source graph's
// coloring_info JSON (the block-attribute channel, 2opt-freq B), then the
// --set pass.abc.region_opts CLI JSON.
struct Region_opts {
  std::optional<std::string>       flow;
  std::optional<std::string>       delay;
  std::optional<std::string>       load;
  std::optional<arith::Adder_kind> adder;
  std::optional<int>               block_size;
  std::optional<arith::Mult_kind>  multiplier;
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
  std::string module;  // region module name (<top>__c<color>)
  int         color       = 0;
  uint64_t    input_nodes = 0;  // source-region nodes before bit blasting
  uint64_t    input_ge    = 0;  // graph_util synthesis-GE estimate before ABC
  // Predicted generic-AIG size of the same cone (graph/predict_abc_size.hpp),
  // the unit `pass.color synth --set synth_alg=cones` thresholds on. Reported
  // NEXT TO input_ge, never instead of it: the two are different estimates of
  // the same input, and the only ground truth for either is `gates` below --
  // there is no per-op post-ABC attribution, so a region sum is the whole
  // measurement. Every production run therefore validates both predictors.
  uint64_t    pred_aig    = 0;
  int         gates       = 0;      // mapped standard cells
  double      area        = 0.0;    // sum of Liberty cell areas
  float       delay       = -1.0f;  // critical arrival (unit-delay depth, or ps with an NLDM GENLIB); <0 => unavailable
  std::string crit_output;          // region output port with the worst arrival
  std::string crit_src;             // "file:line" of that output's original driver (may be empty)
  // Blackboxed div/mod nodes in this region: their cones are NOT mapped, so
  // gates/area/delay under-report — the score is partial until the div is
  // strength-reduced away. Surfaced so an agent never trusts a blind score.
  int         div_blackbox      = 0;
  // Where this region's wall time went, and whether the incremental cache was
  // able to take it. Without these two, "the cache hit 199 of 264 regions" says
  // nothing about whether the run got faster — the misses can hold all the time.
  double      ms                = 0.0;   // wall ms this region spent in map_region
  uint64_t    peak_rss_kb       = 0;     // whole-process high-water after this color; 0 = unavailable/cache hit
  uint64_t    color_peak_rss_kb = 0;     // per-color peak growth over its entry RSS baseline
  const char* cache             = "";    // "" (no cache) | hit | mapped | uncacheable | store-failed
  bool        resynth           = true;  // this invocation rebuilt the region (false = incremental cache hit)
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

  // Idempotent lazy initialization: Abc_Start + read_lib on the first miss.
  // The destructor is a backstop for diagnostics that unwind a region callback.
  bool start();
  void stop();  // Abc_Stop
  void map_region(const livehd::partition::Region_body& rb);

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

  // QoR rows accumulated by map_region, one per successfully mapped region.
  [[nodiscard]] const std::vector<Region_qor>& qor() const { return qor_; }
  // False only when every region was restored from the incremental cache.
  [[nodiscard]] bool                           abc_started() const { return lib_loaded_; }

  // Set when a region was refused by memory admission. map_region cannot throw
  // (a throw out of the region callback would skip stop(), leaking the ABC
  // frame and every live network), so it records the refusal and work() turns it
  // into the fatal AFTER stop() has run.
  [[nodiscard]] const std::string* admission_refusal() const { return refusal_.empty() ? nullptr : &refusal_; }
  [[nodiscard]] const std::string* time_refusal() const { return time_refusal_.empty() ? nullptr : &time_refusal_; }

private:
  std::string refusal_;
  std::string time_refusal_;

  // True (and fills refusal_) when the process has grown past the memory budget
  // while translating `region`. `blasted`/`total` describe how far the
  // translation got, so the diagnostic can project the finished size.
  bool over_budget(std::string_view region, uint64_t rss_before, size_t blasted, size_t total);

  // Startup uses the run-level options, not a region's temporary overrides
  // (notably register_max_bits can turn register mapping off for one region).
  Map_options                                   startup_opts_;
  Map_options                                   opts_;
  bool                                          flat_               = false;
  void*                                         pabc_               = nullptr;  // Abc_Frame_t*
  bool                                          lib_loaded_         = false;
  // The active GENLIB contains all cells with physical NLDM-derived delays, so
  // ABC's sizing tail may safely introduce any SCL drive-strength variant.
  bool                                          nldm_genlib_loaded_ = false;
  // One-shot: the Liberty gave ABC no SCL library, so the max_fanout tail
  // cannot run (see the strip in map_region). Warn once per Mapper, not once
  // per region.
  bool                                          warned_no_scl_      = false;
  // Plain posedge D-flop found in the Liberty (register mapping target). Empty
  // when map_register is off or the library has no DFF cell — the read-back then
  // keeps flops native. Detected once in start().
  std::optional<liberty::Dff_cell>              dff_;
  hhds::GraphLibrary*                           outlib_ = nullptr;  // where blackbox cell defs are declared
  Incr_cache*                                   incr_   = nullptr;  // optional region cache (2opt-incr)
  std::vector<Region_qor>                       qor_;
  uint32_t                                      next_region_id_ = 1;  // report-only key stamped on mapped region graphs
  Region_opts_map                               region_opts_cli_;
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

  // Whether this run asks for a physical (gain-based NLDM) GENLIB: a run-level
  // `delay`, or a CLI region_opts delay override. Constant for the whole run
  // (the GENLIB is installed once in start()) and folded into resolve_recipe.
  [[nodiscard]] bool nldm_requested() const;

  [[nodiscard]] std::string comb_flow() const;
  [[nodiscard]] std::string seq_flow() const;
  [[nodiscard]] std::string resolve_flow(std::string_view builtin) const;
  [[nodiscard]] std::string subst_flow(std::string f) const;

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

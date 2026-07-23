// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "abc_arith.hpp"  // arith::Adder_kind
#include "hhds/graph.hpp"
#include "liberty_dff.hpp"     // livehd::liberty::Dff_cell
#include "pass_partition.hpp"  // livehd::partition::Region_body

namespace livehd::abc {

class Incr_cache;  // abc_incr.hpp -- the 2opt-incr per-region signature cache

struct Map_options {
  std::string       library;  // Liberty .lib for read_lib
  std::string       flow;     // ABC command string (empty => built-in default)
  // Sequential technology-mapping knobs (independent because their cost differs:
  // a register is one DFF cell per bit, a memory bit-blasts into a whole DFF
  // array + address decode). register=true maps flops to Liberty DFF cells (falls
  // back to native flops when the library has none); false keeps them native
  // (cgen emits `always @(posedge clk)`). memory=true bit-blasts a Memory into a
  // register array + read/write mux logic; false keeps it as a native instance.
  bool              map_register = true;
  bool              map_memory   = false;
  // Optional explicit DFF cell name for register mapping (empty => auto-detect a
  // plain posedge D-flop from the Liberty).
  std::string       dff_cell;
  std::string       delay;  // {D} substitution
  std::string       load;   // {L} substitution
  bool              verbose    = false;
  // Combinational adder architecture for Sum/comparators (2i-abc_arith) and the
  // CSKA/CLA block width (0 => auto from the operating width).
  arith::Adder_kind adder      = arith::Adder_kind::rca;
  int               block_size = 0;
  // Combinational multiplier architecture for Mult (partial-product adds use the
  // `adder`/`block_size` above). Only `array` today; the enum is the extension
  // point for Booth/Wallace-tree variants.
  arith::Mult_kind  multiplier = arith::Mult_kind::array;
  // Formal-assume don't-cares (pass.formal -> ABC, task 2f-formal). Feed assume
  // conditions to ABC so violating minterms become don't-cares (smaller logic).
  bool              use_proven_assume = true;   // feed assumes pass.formal PROVED (sound)
  bool              use_all_assume    = false;  // also feed declared (unproven) assumes (aggressive)
  // Memory admission (2opt-incr subtask 0). A region is bit-blasted into ABC,
  // which for a whole-design region means millions of gates and several network
  // forms held at once; an XSCore flat run reached 221 GB on a 64 GiB host and
  // was SIGKILLed by the OS. `memory_budget_mb` pins the ceiling for
  // reproducible hosts/CI (0 => physical RAM minus an OS reserve);
  // `allow_oversize` acknowledges the risk and disables the guard.
  int               memory_budget_mb = 0;
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
// (Abc_NtkDelayTrace) — the phase-1 frequency oracle. Per-region only: paths
// crossing region/blackbox boundaries are invisible here (pass.opentimer is
// the whole-design scorer).
struct Region_qor {
  std::string module;         // region module name (<top>__c<color>)
  int         color = 0;
  int         gates = 0;      // mapped standard cells
  double      area  = 0.0;    // sum of Liberty cell areas
  float       delay = -1.0f;  // critical arrival in library time units; <0 => unavailable
  std::string crit_output;    // region output port with the worst arrival
  std::string crit_src;       // "file:line" of that output's original driver (may be empty)
  // Blackboxed div/mod nodes in this region: their cones are NOT mapped, so
  // gates/area/delay under-report — the score is partial until the div is
  // strength-reduced away. Surfaced so an agent never trusts a blind score.
  int div_blackbox = 0;
  // Where this region's wall time went, and whether the incremental cache was
  // able to take it. Without these two, "the cache hit 199 of 264 regions" says
  // nothing about whether the run got faster — the misses can hold all the time.
  double      ms    = 0.0;   // wall ms this region spent in map_region
  const char* cache = "";    // "" (no cache) | hit | mapped | uncacheable | store-failed
};

// Stats-only mode (no --emit-dir): summarize what would be mapped.
void report_stats(const std::vector<std::shared_ptr<hhds::Graph>>& graphs, std::string_view top, const Map_options& opts);

// Drives the ABC frame across a whole decomposition. One Abc_Start/Stop per
// run; read_lib once before the region loop; the current network is reset per
// region. Each region body is rebuilt as a standard-cell netlist of 1-bit
// blackbox Sub cells.
class Mapper {
public:
  explicit Mapper(const Map_options& opts) : opts_(opts) {}

  bool start();  // Abc_Start + read_lib (false + diag on failure)
  void stop();   // Abc_Stop
  void map_region(const livehd::partition::Region_body& rb);

  void set_outlib(hhds::GraphLibrary* l) { outlib_ = l; }

  // Incremental mapping (2opt-incr A+C): with a cache attached, map_region
  // digests each region first and clones the previously mapped netlist on a
  // hit instead of running ABC. nullptr = every region maps normally.
  void set_incr(Incr_cache* c) { incr_ = c; }
  // Whether incremental caching is active -- the partitioner uses this to decide
  // whether to build each region's pre-body (the cache's compare artifact).
  [[nodiscard]] bool incremental() const { return incr_ != nullptr; }

  // CLI-level per-region overrides (--set pass.abc.region_opts). Graph-embedded
  // overrides (coloring_info "region_opts") are read per region in map_region.
  void set_region_opts(Region_opts_map m) { region_opts_cli_ = std::move(m); }

  // QoR rows accumulated by map_region, one per successfully mapped region.
  [[nodiscard]] const std::vector<Region_qor>& qor() const { return qor_; }

  // Set when a region was refused by memory admission. map_region cannot throw
  // (a throw out of the region callback would skip stop(), leaking the ABC
  // frame and every live network), so it records the refusal and work() turns it
  // into the fatal AFTER stop() has run.
  [[nodiscard]] const std::string* admission_refusal() const {
    return refusal_.empty() ? nullptr : &refusal_;
  }

private:
  std::string refusal_;

  // True (and fills refusal_) when the process has grown past the memory budget
  // while translating `region`. `blasted`/`total` describe how far the
  // translation got, so the diagnostic can project the finished size.
  bool over_budget(std::string_view region, uint64_t rss_before, size_t blasted, size_t total);

  Map_options             opts_;
  void*                   pabc_       = nullptr;  // Abc_Frame_t*
  bool                    lib_loaded_ = false;
  // Plain posedge D-flop found in the Liberty (register mapping target). Empty
  // when map_register is off or the library has no DFF cell — the read-back then
  // keeps flops native. Detected once in start().
  std::optional<liberty::Dff_cell> dff_;
  hhds::GraphLibrary*     outlib_     = nullptr;  // where blackbox cell defs are declared
  Incr_cache*             incr_       = nullptr;  // optional region cache (2opt-incr)
  std::vector<Region_qor> qor_;
  Region_opts_map         region_opts_cli_;
  // coloring_info "region_opts" parse cache, one entry per source graph.
  std::map<const hhds::Graph*, Region_opts_map> graph_region_opts_;

  [[nodiscard]] std::string comb_flow() const;
  [[nodiscard]] std::string seq_flow() const;

  // The resolved per-region ABC recipe, serialized VERBATIM for the incremental
  // cache's recipe gate: the pre-ABC lgraph does not encode it, so two regions
  // with equal logic but different flow/arch must never share a cached netlist.
  // Call after apply_region_overrides so it reflects region_opts.
  [[nodiscard]] std::string resolve_recipe() const;

  // Overlay any per-region overrides for rb.color onto opts_ (caller saves and
  // restores opts_ around the region).
  void apply_region_overrides(const livehd::partition::Region_body& rb);
};

}  // namespace livehd::abc

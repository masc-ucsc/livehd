//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "hhds/graph.hpp"
#include "ot/timer/timer.hpp"
#include "pass.hpp"
#include "sta_cache.hpp"
#include "vcd_power.hpp"

class Pass_opentimer : public Pass {
protected:
  // Driver pin (hhds::Class_index) -> opentimer net name. Used when a driver
  // pin has been renamed to a primary input/output net or to a slice of a bus.
  // FLAT-mode keying (per-def Class_index); collides across instances, so the
  // hierarchical walk uses overwrite_hnet_ (keyed by the hier-unique net name).
  absl::flat_hash_map<hhds::Class_index, std::string> overwrite_dpin2net;
  absl::flat_hash_map<std::string, std::string>       overwrite_hnet_;

  // LEGACY name-stitched hier walk (`hier=stitch` only): build_circuit/
  // compute_timing descend the instance hierarchy via forward_hier/fast_hier
  // and name every net/gate with its hier-unique get_hier_name, so ONE
  // ot::Timer holds the whole flattened design. Kept for debugging: multi-bit
  // module-boundary buses are not stitched. `hier=true` instead structurally
  // flattens the design up front (time_work) and runs the flat single-module
  // path, which is byte-for-byte unchanged when this is false.
  bool hier_mode_ = false;

  // Liberty-cell (leaf) gids: forward_hier/fast_hier must NOT descend into
  // these blackbox cell bodies (they carry IO but no logic) — they are the
  // flattened netlist's gates, emitted as leaves. Design-module Subs are not
  // in this set, so the hier walk descends into them. Empty in flat mode.
  ankerl::unordered_dense::set<hhds::Gid> opaque_gids_;

  ot::Timer timer;

  int    margin;  // % margin to mark nodes
  double freq;

  std::string odir;          // output directory power trace
  float       max_delay;     // slowest arrival time (delay) on the circuit
  float       margin_delay;  // time delay to mark any slower cell for criticality

  std::vector<std::string> sdc_file_list;
  std::vector<std::string> spef_file_list;
  std::vector<std::string> vcd_file_list;
  std::vector<Vcd_power>   vcd_list;

  // Every timing file as given (Liberty + sdc/spef/vcd), for the STA reuse
  // cache's environment hash: it keys on file CONTENT, so it needs the paths.
  std::vector<std::string> timing_file_list;
  // read_celllib is DEFERRED (a 12 MB sky130 parse) so a cache hit never pays
  // it -- the same lazy-startup rule pass.abc applies to Abc_Start/read_lib.
  std::vector<std::string> lib_file_list;
  bool                     libs_loaded_ = false;

  std::string cache_dir_;  // STA reuse cache directory; empty = no cache

  // The `slowest delay:` summary line's pin, and the `native-comb-boundary`
  // warning payload build_circuit computed: both are part of what a cache hit
  // has to replay, so they outlive the function that fills them.
  std::string              max_pin_;
  uint64_t                 opaque_logic_nodes_ = 0;
  uint64_t                 ambiguous_or_nodes_ = 0;
  std::vector<std::string> opaque_logic_examples_;
  std::vector<std::string> ambiguous_or_examples_;
  std::string              time_unit_label_;  // "ns"/"ps"/"us"; "" = library declared none
  // The rendered design block WITHOUT its `,"colors":[...]` tail -- what the
  // reuse cache stores (see render_colors).
  std::string              sta_block_;

  std::string qor_path;    // timing JSON sidecar (2opt-freq D); empty => none
  std::string top_filter;  // analyze only the def with this name; empty => the single def
  std::string
      hier_setting_;  // `hier` label: true (default, structural flatten) | false (one module per run) | stitch (legacy walk)
  std::string report_module_;  // display name for reports; empty => the analyzed graph's own name
                               // (set when hier=true times a scratch flattened def)

  bool stats_ = false;
  struct Color_qor {
    uint32_t    region_id = 0;
    std::string module;
    int         color       = 0;
    bool        resynth     = false;
    uint64_t    cells       = 0;      // occurrence-weighted Liberty cells in the timed top
    float       max_arrival = -1.0F;  // end-to-end arrival at a cell output in this region
    std::string critical_pin;
    std::string critical_src;
  };
  std::vector<Color_qor> color_qor_;

  std::vector<std::string> qor_blocks_;  // one JSON object per analyzed design

  // STA reuse telemetry, rendered into the qor report's `incremental` member
  // (harvest_sta_incremental lifts it into the result envelope).
  bool         cache_enabled_    = false;
  bool         cache_hit_        = false;
  mutable bool cache_digestable_ = true;  // set by cache_key() (const): an undigestable netlist never caches
  double       cache_lookup_ms_  = 0.0;

  static void liberty_open(Eprp_var& var);
  static void time_work(Eprp_var& var);
  static void power_work(Eprp_var& var);

  void read_files();

  // Parse the Liberty file(s) into the timer, once, on first need.
  void ensure_libs();

  // ---- STA reuse cache (sta_cache.hpp) ------------------------------------
  // The analyzed netlist's identity: canonical_digest of `g` Merkle-folded
  // through the whole netlist library, mixed with the environment hash and the
  // engine salt. Empty when the graph is not digestable (an anonymous state
  // cell) or no cache directory was given -- both mean "always re-time".
  [[nodiscard]] std::string                   cache_key(const std::shared_ptr<hhds::Graph>& g) const;
  // Re-emit a cached analysis as this run's output: the report block (with
  // `resynth` re-stamped from THIS run's graph), the summary line and the
  // boundary warning. `qor_blocks_`/max_delay are filled as a live run would.
  void                                        replay(const livehd::opentimer::Sta_record& rec);
  // Snapshot what this run computed (only after an error-free analysis).
  [[nodiscard]] livehd::opentimer::Sta_record snapshot() const;
  // Render the `,"colors":[...]` tail from color_qor_ (empty without --stats).
  [[nodiscard]] std::string                   render_colors() const;
  // The boundary warning, from the counters above. Shared by the live path and
  // the replay, so a hit is diagnostic-equal to the run that filled the cache.
  void                                        emit_boundary_warning() const;

  void set_input_delays(const std::string& pname);
  void set_output_delays(const std::string& pname);
  void setup_hier(const std::shared_ptr<hhds::Graph>& g);  // pick flat/hier + build opaque_gids_
  void build_circuit(const std::shared_ptr<hhds::Graph>& g);

  void read_vcd();
  void read_sdc_spef();
  void read_sdc(std::string_view sdc_file);
  void compute_timing(const std::shared_ptr<hhds::Graph>& g);
  void compute_power(const std::shared_ptr<hhds::Graph>& g);
  void populate_table(const std::shared_ptr<hhds::Graph>& g);

  // Leaf timing nodes of `g` (gates, flops, memories, tracker glue): flat mode
  // -> g's own fast_class; whole-design mode -> forward_hier snapshot descending
  // design modules and yielding Liberty-cell leaves (opaque_gids_).
  std::vector<hhds::Occurrence_node> leaf_nodes(const std::shared_ptr<hhds::Graph>& g) const;

  std::string get_driver_net_name(const hhds::Occurrence_pin& dpin) const;
  // Net a DRIVER pin drives, named from the traversal `owner` node (whose hier
  // chain is intact) rather than the pin's master (out_pins() pins drop it).
  std::string driver_net_of(const hhds::Occurrence_node& owner, const hhds::Occurrence_pin& dpin) const;
  void        backpath_set_color(hhds::Node_class node, int color);
  void        write_qor() const;  // write the accumulated qor_blocks_ to qor_path

public:
  Pass_opentimer(const Eprp_var& var);

  static void setup();
};

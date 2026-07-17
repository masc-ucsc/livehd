//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "hhds/graph.hpp"
#include "ot/timer/timer.hpp"
#include "pass.hpp"
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

  std::string qor_path;    // timing JSON sidecar (2opt-freq D); empty => none
  std::string top_filter;  // analyze only the def with this name; empty => the single def
  std::string hier_setting_;  // `hier` label: true (default, structural flatten) | false (one module per run) | stitch (legacy walk)
  std::string report_module_;  // display name for reports; empty => the analyzed graph's own name
                               // (set when hier=true times a scratch flattened def)

  std::vector<std::string> qor_blocks_;  // one JSON object per analyzed design

  static void liberty_open(Eprp_var& var);
  static void time_work(Eprp_var& var);
  static void power_work(Eprp_var& var);

  void read_files();

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
  std::vector<hhds::Node_class> leaf_nodes(const std::shared_ptr<hhds::Graph>& g) const;

  std::string get_driver_net_name(const hhds::Pin_class& dpin) const;
  // Net a DRIVER pin drives, named from the traversal `owner` node (whose hier
  // chain is intact) rather than the pin's master (out_pins() pins drop it).
  std::string driver_net_of(const hhds::Node_class& owner, const hhds::Pin_class& dpin) const;
  void        backpath_set_color(hhds::Node_class node, int color);
  void        write_qor() const;  // write the accumulated qor_blocks_ to qor_path

public:
  Pass_opentimer(const Eprp_var& var);

  static void setup();
};

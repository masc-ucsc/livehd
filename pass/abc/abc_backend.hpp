// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

// The ABC mapping backend (abc_cleanup.md section 5): a private ABC frame per
// lane, the built-in flows and their budget ladder, the partition-boundary
// environment and the whole-design re-size. A region arrives translated
// (synth::Region_blast), is replayed into an ABC netlist, optimized and
// technology-mapped, and leaves as a synth::Cell_netlist.
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "abc_boundary.hpp"  // Boundary_table
#include "abc_cells.hpp"
#include "abc_map.hpp"  // Map_options
#include "region_backend.hpp"

namespace livehd::abc {

class Abc_backend final : public synth::Region_backend {
public:
  explicit Abc_backend(const Map_options& opts) : base_(opts), region_(opts) {}
  ~Abc_backend() override { stop(); }

  [[nodiscard]] std::unique_ptr<synth::Region_backend> lane() const override { return std::make_unique<Abc_backend>(base_); }
  Start                                                start(const synth::Region_ctx& ctx) override;
  void                                                 stop() override;
  [[nodiscard]] bool                                   started() const override { return lib_loaded_; }
  [[nodiscard]] std::shared_ptr<void>                  region_scope() override;
  [[nodiscard]] synth::Region_plan                     plan(const synth::Region_ctx& ctx) override;
  [[nodiscard]] std::optional<synth::Cell_netlist>     map(const synth::Region_ctx& ctx, const synth::Region_blast& blast,
                                                           const synth::Region_rewrite& rewrite, synth::Region_qor& q) override;
  [[nodiscard]] const synth::Cell_library&             cells() const override { return cell_library_; }
  void                                                 end_region() override;
  [[nodiscard]] uint64_t                               projected_memory(uint64_t aig_nodes) const override;
  uint64_t          refine(hhds::GraphLibrary& outlib, std::string_view top, const synth::Design_ctx& design) override;
  synth::Ware_score score(hhds::GraphLibrary& outlib, std::string_view top, const synth::Design_ctx& design) override;

private:
  // Run-level options; `region_` is the current region's (region options and
  // overrides applied by plan(), reset by region_scope()).
  Map_options base_;
  Map_options region_;
  bool        tool_owned_flow_  = true;   // the region runs a tool-chosen flow (built-in or size tier)
  bool        builtin_flow_     = true;   // ... and it is the built-in one (keeps every latch)
  bool        timing_requested_ = false;  // the run asks for a delay target somewhere (the last plan's view)

  // Boundary environment (abc_boundary.cpp), resolved once per session from
  // the SCL library: the typical input-pin capacitance in fF (io_load's
  // default and the static estimate's per-sink weight), the stand-in driving
  // cell (SC_Cell*, opaque here), and whether the Liberty can size at all
  // (lib_has_nldm_timing, the buffering tail's own predicate -- unlike
  // scl_timing_ok_ it does not need a delay target).
  float typical_cap_ff_ = -1.0f;
  void* drive_cell_     = nullptr;
  bool  scl_lib_ok_     = false;
  void* pabc_           = nullptr;  // Abc_Frame_t*
  bool  lib_loaded_     = false;
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
  bool  scl_timing_ok_  = false;
  // One-shot: the Liberty gave ABC no SCL library, so the max_fanout tail
  // cannot run (see the strip in map()). Warn once per session, not per region.
  bool  warned_no_scl_  = false;
  // The session's GENLIB as a Cell_library.
  synth::Cell_library cell_library_;
  Gate_types          gate_type_;  // Mio_Gate_t* -> cell_library_ type
  // The Liberty cell IO decl of a Mio gate in the output library -- the SAME
  // construction for a cell a region read-back mints and for a cell a
  // boundary re-size swaps in: both must agree on port ids.
  const synth::Cell_decl& cell_desc_for(hhds::GraphLibrary& outlib, void* mio_gate);

  // The per-region `{B}` substitution (`-D <budget>` for the sizing steps of
  // the built-in tails, empty without a target): set by plan() before any
  // flow string of the region is resolved, cleared by region_scope().
  std::string budget_flag_;

  [[nodiscard]] std::string comb_flow() const;
  // The area candidate's command list (kAreaFlow + its sizing tail, or the
  // user's `area_flow`), substituted like comb_flow(). "none" => empty.
  [[nodiscard]] std::string area_flow() const;
  [[nodiscard]] std::string resolve_flow(std::string_view builtin) const;
  [[nodiscard]] std::string subst_flow(std::string f) const;
  // The resolved per-region ABC recipe, serialized VERBATIM for the region
  // cache's recipe gate: the pre-ABC lgraph does not encode it, so two regions
  // with equal logic but different flow/arch must never share a cached netlist.
  [[nodiscard]] std::string resolve_recipe(const synth::Region_ctx& ctx) const;

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
  // Session start without a region (refine/score after an all-hit run).
  bool start_session();
  // The whole-design start: the run's timing request is the driver's (a
  // parallel run never planned a region on this session).
  bool start_for_design(const synth::Design_ctx& design) {
    timing_requested_ = timing_requested_ || design.timing_requested;
    return start_session();
  }
};

}  // namespace livehd::abc

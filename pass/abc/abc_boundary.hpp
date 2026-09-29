// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

// Partition-boundary environment for pass.abc (abc_boundary.cpp).
//
// A colored region is technology-mapped as its own module, so inside ABC its
// ports are primary inputs and outputs. Left alone, ABC's SCL timer -- the
// engine behind the `buffer`/`upsize`/`dnsize` tail and the budget ladder --
// sees NOTHING beyond them: a PO drives no load, a PI comes from an ideal
// zero-slew driver, and `buffer -N` never trees a PI's fanout. The driver of a
// net that crosses into five regions was therefore sized for fanout one, and
// each sink region assumed an infinitely strong source; pass.opentimer, which
// times the stitched design, then found paths several times longer than any
// region reported (picorv32: 9.6 ns against a 4.2 ns worst region; xs
// renametable: 131 ns against 4.7 ns).
//
// This is the same lesson every hierarchical flow (OpenROAD's hierarchically
// linked mode, the commercial tools) settled on: partition boundaries carry
// port load annotations and arrival/required budgets, and a net crossing a
// boundary is buffered as a whole net, not as the stub inside one partition.
//
// The realization here keeps the per-region modules (the incremental cache,
// the per-module LEC twin and the memory admission all depend on them) and
// feeds each region's ABC session a per-PI/PO environment table -- the
// `SC_Bnd` the local packages/abc.patch teaches the SCL timer to honour:
//
//   * per PO: the external load in fF and a virtual downstream delay (the
//     region budget minus the required time at that port);
//   * per PI: the driving cell (its load-dependent delay and slew) and an
//     arrival time.
//
// It is filled twice. Phase A (`Mapper::map_region`, before the flow runs) is
// a STATIC estimate from the source graph -- consumer pins outside the region
// per output bit times the library's typical input capacitance, a stand-in
// driver on every input -- so the mapping objective already sizes against
// plausible loads; it is a pure function of the source graph and the
// Liberty, so cold and warm runs agree and it can join the incremental
// recipe. Phase B (`Mapper::refine_boundaries`, after the whole decomposition
// exists) is EXACT: every region's mapped body is re-imported into ABC, each
// port bit is joined through the wrappers to its real driver cell and its
// real sink pins (Liberty caps), and each region is re-sized in place
// against that environment. Cell swaps keep pins and topology, so the netlist
// is rewired in place and the LEC twin is untouched.

#include <cstddef>
#include <cstdint>

namespace livehd::abc {

// Owns one SC_Bnd table (its ABC vectors) and installs/uninstalls it on the
// global ABC frame. Every entry starts "unconstrained"; ABC headers stay in
// the .cpp, so the cell is opaque here. Uninstalls itself on destruction.
class Boundary_table {
public:
  Boundary_table(size_t n_pi, size_t n_po);
  ~Boundary_table();
  Boundary_table(const Boundary_table&)            = delete;
  Boundary_table& operator=(const Boundary_table&) = delete;

  // PO i: external load in fF (<0 keeps it unconstrained) and the virtual
  // delay after the port in ps (<=0 none).
  void                set_po(size_t i, float load_ff, float delay_ps);
  // PI i: driving SC_Cell* (nullptr = ideal driver) and arrival in ps (<=0 none).
  void                set_pi(size_t i, void* sc_cell, float arrival_ps);
  [[nodiscard]] float po_load(size_t i) const;

  void               install();
  void               uninstall();
  [[nodiscard]] bool installed() const { return installed_; }

private:
  void* bnd_       = nullptr;  // SC_Bnd*
  bool  installed_ = false;
};

}  // namespace livehd::abc

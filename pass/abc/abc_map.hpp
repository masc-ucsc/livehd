// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

// pass.abc's mapping entry: the region driver of pass/synth
// (region_driver.hpp) bound to the ABC backend (abc_backend.hpp), and the
// options of both.
#include <cstdint>
#include <string>

#include "region_driver.hpp"
#include "satopt_stages.hpp"

namespace livehd::abc {

using synth::parse_region_opts;
using synth::Region_opts;
using synth::Region_opts_map;
using synth::Region_qor;

// The region driver's options plus ABC's own.
struct Map_options : synth::Driver_options {
  bool        satopt = false;
  // The satopt stages pass.abc runs on its synthesis copy (pass.satopt.stages;
  // satopt=false turns every one off).
  satopt::Stage_set satopt_stages = satopt::default_stages(satopt::Profile::synthesis);
  satopt::Budget    satopt_budget;  // --set pass.satopt.<knob> (one budget for the whole synthesis copy)
  std::string flow;  // ABC command string (empty => built-in default)
  // Cap on the fanout of any net ABC MAPS, enforced by appending
  // `buffer -N <n>; dnsize` to a built-in flow. 0 disables the tail.
  // Nets driven by native (unblasted) nodes are outside ABC and keep their
  // fanout regardless. Default 16.
  uint32_t    max_fanout = 16;
  // Indivisible wide operations can exceed pass.color.synth.max_gate by orders of
  // magnitude. The default large tier (at or above Driver_options::large_ge)
  // skips ABC's unbounded structural-choice synthesis and maps the already
  // bit-blasted AIG directly. Empty or large_ge==0 disables the tier; an
  // explicit global/per-region flow wins.
  std::string large_flow;
  std::string load;  // {L} substitution
  // A delay target is a BUDGET, not "go as fast as you can". ABC's `&nf -D` is
  // silently IGNORED by the mapper (giaNf reads only `MapDelayTarget`, which
  // `-D` never sets), so a built-in flow always mapped for MINIMUM delay and
  // spent area no timing constraint asked for. When the mapped region beats its
  // budget, pass.abc re-runs the mapper with `&nf -R <pct>` -- ABC's own delay
  // RELAXATION ratio -- to convert the measured slack into area. This caps that
  // percentage; 0 disables the recovery entirely and restores minimum-delay
  // mapping. Needs a physical (NLDM) Liberty and a `delay` target: with neither
  // there is no budget to be inside of.
  uint32_t    area_relax_pct = 200;
  // The AREA objective (also a second candidate after meeting a delay budget):
  // empty = the former baseline with &fraig/dc2/&dch/&nf, "none"
  // disables only the second candidate, anything else is an ABC command string run
  // verbatim ({D}/{L}/{F}/{B} substituted) in place of the built-in one.
  std::string area_flow;
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
  bool        boundary        = true;
  // Tree every region input's fanout inside the region when it exceeds
  // `max_fanout` -- the design's primary inputs and the sink side of crossing
  // nets alike, the same fanout rule internal nets follow -- by declaring
  // `boundary_drive` as ABC's driving cell: its `buffer` only trees an input
  // that has a driver (`buffer -p` is inert in the current ABC). Independent
  // of `boundary`. False leaves inputs unbuffered and lets the exact re-size
  // upsize the driver instead.
  bool        boundary_buffer = true;
  // Stand-in Liberty cell driving a region input whose real driver is not a
  // mapped cell (a primary input, a native flop, a memory or child-instance
  // output, and every input under the static estimate): empty = the
  // library's smallest buffer, `none` = the ideal (zero-slew) driver.
  std::string boundary_drive;
  // Load in fF on a PRIMARY output of the design (a port of --top) and on a
  // port whose sink cannot be resolved; <0 = one typical input pin of the
  // library (the average input capacitance over its smallest cells).
  float       io_load         = -1.0f;
  // Rounds of the exact re-size. Each round sizes every region under the
  // loads/drivers as they stand plus the arrival and required-time budgets
  // the previous round propagated across the hierarchy; a path through k
  // regions needs k rounds to be seen whole.
  int         boundary_rounds = 1;
};

// The region driver over an ABC backend: pass.abc. The backend's session
// (a private ABC frame + read_lib) starts on the first cache miss, and every
// parallel lane gets its own.
class Mapper : public synth::Region_driver {
public:
  explicit Mapper(const Map_options& opts);
};

}  // namespace livehd::abc
